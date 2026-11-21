#include "recovered_bt.hpp"
namespace study::recovered {
std::vector<size_t> replayFrontIndices(std::span<const int64_t> rightEdges,int64_t front) {
    std::vector<size_t> out;
    for(size_t k=0;k<rightEdges.size();++k)if(rightEdges[k]<=front)out.push_back(k);
    return out;
}
BtScores btScore(const BtScoreInput &v) {
    const double remainingGridArea = (static_cast<double>(v.pitchX) / 1e6) *
        (static_cast<double>(v.currentGridCount) / 1000.);
    double projected = v.targetField14b8 - v.queueAggregate20;
    projected += v.placedArea;
    projected /= v.totalArea;
    projected *= v.totalValue;
    projected -= v.baselineValue;
    const double marginal = (v.baselineFreeArea - remainingGridArea) / (v.valueB + v.valueA);
    projected *= marginal;
    projected += v.baselineLostArea;
    const double predictedLength = projected / (v.sheetHeight - v.referenceHeight);
    const double occupied = static_cast<double>(v.maxPlacedX - rounded(v.leftTrim * 1e6)) / 1e6;
    // SSE maxsd selects the second operand on unordered/equal input, including NaN/zero.
    double length = occupied > predictedLength ? occupied : predictedLength;
    if (v.maximumLengthX > 0 && length > static_cast<double>(v.maximumLengthX) / 1e6)
        length = occupied; // It is a fallback, not clamping to maximumLengthX.
    return {projected, length};
}
std::vector<Placement> btCapture(std::span<const Placement> all, int count) {
    require(count >= -1, PN2S_INTERNAL, "Invalid subnester reported count");
    if (count == -1 || static_cast<size_t>(count) > all.size()) return {all.begin(), all.end()};
    return {all.end() - count, all.end()};
}
std::optional<BtTrial> evaluateBtTrial(BtHost &host, BtCandidate candidate) {
    try {
        const int countA = host.runStage(candidate.stageA);
        if (countA == 0) return std::nullopt; // Original returns before B and before Restore.
        BtTrial t;
        t.candidate = candidate;
        t.first = btCapture(host.placements(), countA);
        const int countB = host.runStage(candidate.stageB);
        t.prediction = btCapture(host.placements(), countB);
        t.scores = btScore(host.scoreInput(t.first, t.prediction));
        host.restore();
        return t;
    } catch (...) {
        // Exception cleanup is an engineering guarantee; normal original control flow above.
        host.restore();
        throw;
    }
}
bool betterBtTrial(const BtTrial &candidate, const BtTrial &best, bool lengthMode) {
    return lengthMode ? candidate.scores.length < best.scores.length
                      : candidate.scores.projectedArea < best.scores.projectedArea;
}
size_t runBt(BtHost &host, bool lengthMode) {
    size_t committed = 0;
    std::vector<Placement> previousPrediction;
    while (!host.stopped()) {
        auto alternatives = host.alternatives(previousPrediction);
        host.beginRound();
        std::optional<BtTrial> best;
        try {
            for (auto candidate : alternatives) {
                if (host.stopped()) break;
                auto trial = evaluateBtTrial(host, candidate);
                if (trial && (!best || betterBtTrial(*trial, *best, lengthMode))) best = std::move(trial);
            }
            if (!best || best->first.empty()) { host.endRound(); break; }
            for (const auto &p : best->first) host.commit(p);
            committed += best->first.size();
            previousPrediction = std::move(best->prediction);
            host.endRound();
        } catch (...) {
            host.restore();
            host.endRound();
            throw;
        }
    }
    return committed;
}
}
