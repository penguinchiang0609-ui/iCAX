#pragma once
#include "core.hpp"
#include <optional>
#include <span>
namespace study::recovered {
// Fields are named only as far as their producers have been established.
struct BtScoreInput {
    int64_t pitchX = 1, currentGridCount = 0, maxPlacedX = 0, maximumLengthX = 0;
    double targetField14b8 = 0, queueAggregate20 = 0, placedArea = 0, totalArea = 1;
    double totalValue = 0, baselineValue = 0, baselineFreeArea = 0, baselineLostArea = 0;
    double valueA = 0, valueB = 0, sheetHeight = 0, referenceHeight = 0, leftTrim = 0;
};
struct BtScores { double projectedArea = 0, length = 0; };
BtScores btScore(const BtScoreInput &); // 3e3ec0, 3e41ff..3e4378
struct BtCandidate { size_t stageA = 0, stageB = 0; int tag = 0; };
std::vector<size_t> replayFrontIndices(std::span<const int64_t> rightEdges,int64_t front); // 3e5dd0
struct BtTrial {
    BtCandidate candidate;
    std::vector<Placement> first, prediction;
    BtScores scores;
};
// Original 3ee340 return -1 or an excessive count selects the complete list.
std::vector<Placement> btCapture(std::span<const Placement>, int reportedCount);
class BtHost {
public:
    virtual ~BtHost() = default;
    virtual bool stopped() = 0;
    virtual std::vector<BtCandidate> alternatives(std::span<const Placement> previousPrediction) = 0;
    virtual void beginRound() = 0;
    virtual int runStage(size_t stage) = 0;
    virtual std::span<const Placement> placements() const = 0;
    virtual BtScoreInput scoreInput(std::span<const Placement> first, std::span<const Placement> prediction) = 0;
    virtual void restore() = 0;
    virtual void commit(const Placement &) = 0;
    virtual void endRound() = 0;
};
std::optional<BtTrial> evaluateBtTrial(BtHost &, BtCandidate);
bool betterBtTrial(const BtTrial &candidate, const BtTrial &best, bool lengthMode);
size_t runBt(BtHost &, bool lengthMode); // append/rollback/first-batch commit loop, 3e4720
}
