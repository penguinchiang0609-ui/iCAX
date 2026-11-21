#include "recovered_general_tiling.hpp"
namespace study::recovered {
TilingCandidate selectGeneralTilingRepetition(std::span<const TilingPattern> patterns,
    std::span<const TilingRepetitionCounts> counts,Budget *budget) {
    require(patterns.size()==counts.size(),PN2S_INVALID,"General tiling patterns and repetition descriptors do not match");
    std::optional<TilingCandidate> best;
    for(size_t index=0;index<patterns.size();++index) {
        if(budget)budget->check();if(patterns[index].members.size()!=2)continue;
        std::vector<int32_t> x(2),y(2);for(size_t member=0;member<2;++member){x[member]=int32_t(uint32_t(counts[index].commonX)+uint32_t(counts[index].extraX[member]));y[member]=int32_t(uint32_t(counts[index].commonY)+uint32_t(counts[index].extraY[member]));}
        auto candidate=buildTilingCandidate(patterns[index],std::move(x),std::move(y),UINT32_MAX,budget);
        if(!best){best=std::move(candidate);continue;}
        const auto priorSize=tilingCandidateSize(*best),nextSize=tilingCandidateSize(candidate);
        const auto priorBounds=tilingCandidateBounds(*best),nextBounds=tilingCandidateBounds(candidate);
        const double priorArea=(priorBounds.y1-priorBounds.y0)*(priorBounds.x1-priorBounds.x0),nextArea=(nextBounds.y1-nextBounds.y0)*(nextBounds.x1-nextBounds.x0);
        if(priorSize<nextSize||(priorSize==nextSize&&nextArea<priorArea))best=std::move(candidate);
    }
    return best?std::move(*best):TilingCandidate{};
}
namespace {
int32_t maximum(const std::vector<int32_t> &values) {
    return values.empty() ? 0 : *std::max_element(values.begin(), values.end());
}
void validateCounts(const TilingCandidate &candidate) {
    require(candidate.countsX.size() >= candidate.pattern.members.size() &&
            candidate.countsY.size() >= candidate.pattern.members.size(), PN2S_INVALID,
            "Tiling candidate repetition vectors do not cover its member records");
}
}
bool tilingCandidateValid(const TilingCandidate &candidate) {
    return !candidate.pattern.members.empty() && maximum(candidate.countsX) > 0 && maximum(candidate.countsY) > 0;
}
uint64_t tilingCandidateSize(const TilingCandidate &candidate) {
    validateCounts(candidate);
    uint64_t result = 0;
    for (size_t member = 0; member < candidate.pattern.members.size(); ++member) {
        // The original multiplies in 32 bits, sign-extends the product, then
        // accumulates into the returned 64-bit unsigned count.
        const auto product = int32_t(uint32_t(candidate.countsX[member]) * uint32_t(candidate.countsY[member]));
        result += uint64_t(int64_t(product));
    }
    return result;
}
Box tilingCandidateBounds(const TilingCandidate &candidate) {
    validateCounts(candidate);
    require(!candidate.pattern.members.empty(), PN2S_INVALID, "Tiling bounds need a member record");
    const auto &members = candidate.pattern.members;
    Point low{members.back().box.x0, members.back().box.y0};
    Point high{members.back().box.x1, members.back().box.y1};
    for (const auto &member : members) {
        if (!(low.x <= member.box.x0)) low.x = member.box.x0;
        if (!(low.y <= member.box.y0)) low.y = member.box.y0;
        if (!(member.box.x1 <= high.x)) high.x = member.box.x1;
        if (!(member.box.y1 <= high.y)) high.y = member.box.y1;
    }
    Box result{high.x < low.x ? high.x : low.x, high.y < low.y ? high.y : low.y,
               high.x > low.x ? high.x : low.x, high.y > low.y ? high.y : low.y};
    for (size_t index = 0; index < members.size(); ++index) {
        const auto box = members[index].box;
        for (const auto [count, step] : {std::pair{candidate.countsX[index], candidate.pattern.u},
                                        std::pair{candidate.countsY[index], candidate.pattern.v}}) {
            const double repeats = double(count) - 1.;
            const double x0 = repeats * step.x + box.x0, y0 = repeats * step.y + box.y0;
            const double x1 = repeats * step.x + box.x1, y1 = repeats * step.y + box.y1;
            if (x0 < result.x0) result.x0 = x0;
            if (y0 < result.y0) result.y0 = y0;
            if (result.x1 < x1) result.x1 = x1;
            if (result.y1 < y1) result.y1 = y1;
        }
    }
    return result;
}
void reduceTilingCandidateDemand(TilingCandidate &candidate, int32_t excess, Budget *budget) {
    validateCounts(candidate);
    const size_t count = candidate.countsX.size();
    require(count > 0, PN2S_INVALID, "Tiling demand reduction requires a member");
    const auto select = [&](const std::vector<int32_t> &values, bool y) {
        size_t result = 0;
        for (size_t index = 1; index < count; ++index) {
            const double next = y ? candidate.pattern.members[index].box.y0 : candidate.pattern.members[index].box.x0;
            const double prior = y ? candidate.pattern.members[result].box.y0 : candidate.pattern.members[result].box.x0;
            if (values[index] > values[result] || (values[index] == values[result] && next > prior)) result = index;
        }
        return result;
    };
    while (excess > 0) {
        if (budget) budget->check();
        const size_t x = select(candidate.countsX, false), y = select(candidate.countsY, true);
        if (candidate.countsX[x] < candidate.countsY[y] && candidate.countsX[x] < 4) {
            --candidate.countsY[y];
            excess = int32_t(uint32_t(excess) - uint32_t(candidate.countsX[y == 0 ? 0 : 1]));
        } else {
            --candidate.countsX[x];
            excess = int32_t(uint32_t(excess) - uint32_t(candidate.countsY[x == 0 ? 0 : 1]));
        }
    }
}
TilingCandidate buildTilingCandidate(TilingPattern pattern, std::vector<int32_t> x,
                                    std::vector<int32_t> y, uint32_t limit, Budget *budget) {
    TilingCandidate result{std::move(pattern), std::move(x), std::move(y), limit};
    if (!tilingCandidateValid(result)) return result;
    validateCounts(result);
    int32_t required = -1;
    for (size_t repeat = 0; repeat < result.pattern.members.size(); ++repeat)
        for (const auto &member : result.pattern.members)
            required = required > 0 ? std::min(required, member.required) : member.required;
    auto capPositive = [](int32_t value, int32_t cap) { return value > 0 ? std::min(value, cap) : cap; };
    if (result.pattern.maximumTotal > 0)
        result.perMemberLimit = uint32_t(capPositive(int32_t(result.perMemberLimit), result.pattern.maximumTotal));
    if (result.pattern.maximumX > 0) for (auto &value : result.countsX) value = capPositive(value, result.pattern.maximumX);
    if (result.pattern.maximumY > 0) for (auto &value : result.countsY) value = capPositive(value, result.pattern.maximumY);
    const int32_t excess = int32_t(uint32_t(tilingCandidateSize(result)) - uint32_t(required));
    if (excess > 0) reduceTilingCandidateDemand(result, excess, budget);
    const int32_t cap = int32_t(result.perMemberLimit);
    if (cap < 4) {
        if (cap > 0) for (size_t index = 0; index < result.pattern.members.size(); ++index) {
            auto &nx = result.countsX[index], &ny = result.countsY[index];
            if (cap < int32_t(uint32_t(nx) * uint32_t(ny))) {
                if (nx < ny) { ny = cap; nx = 1; } else { ny = 1; nx = cap; }
            }
        }
    } else {
        for (size_t index = 0; index < result.pattern.members.size(); ++index) {
            auto &nx = result.countsX[index], &ny = result.countsY[index];
            while (cap < int32_t(uint32_t(nx) * uint32_t(ny))) {
                if (budget) budget->check();
                if (nx < ny && nx < 4) --ny; else --nx;
            }
        }
        const int32_t minimumX = *std::min_element(result.countsX.begin(), result.countsX.end());
        const int32_t minimumY = *std::min_element(result.countsY.begin(), result.countsY.end());
        for (auto &value : result.countsX) value = std::min(value, int32_t(uint32_t(minimumX) + 1));
        for (auto &value : result.countsY) value = std::min(value, int32_t(uint32_t(minimumY) + 1));
    }
    return result;
}
TilingExpansion expandTilingCandidate(const TilingCandidate &candidate, Budget *budget) {
    TilingExpansion result;
    if (!tilingCandidateValid(candidate)) return result;
    validateCounts(candidate);
    const size_t count = candidate.pattern.members.size();
    const int32_t nx = maximum(candidate.countsX), ny = maximum(candidate.countsY);
    require(uint64_t(nx) * uint64_t(ny) <= SIZE_MAX / count, PN2S_INVALID, "Tiling candidate index grid too large");
    std::vector<int32_t> indices(size_t(nx) * size_t(ny) * count, -1);
    std::vector<uint32_t> remaining(count, candidate.perMemberLimit);
    const auto grid = [&](int32_t x, int32_t y, size_t member) -> int32_t & {
        return indices[(size_t(y) * size_t(nx) + size_t(x)) * count + member];
    };
    const auto relation = [&](size_t table, size_t first, size_t second) {
        const auto row = std::min(first, second), column = std::max(first, second);
        const auto &matrix = candidate.pattern.commonCuts[table];
        if (row >= matrix.size() || column >= matrix[row].size()) return -1.;
        return matrix[row][column];
    };
    const auto append = [&](int32_t x, int32_t y, size_t previous, int32_t current, double value) {
        const int32_t prior = grid(x, y, previous);
        if (value >= 0. && prior >= 0 && current >= 0) result.commonCuts.push_back({prior, current, value});
    };
    for (int32_t x = 0; x < nx; ++x)
        for (int32_t y = 0; y < ny; ++y)
            for (size_t member = 0; member < count; ++member) {
                if (budget) budget->check();
                if (x >= candidate.countsX[member] || y >= candidate.countsY[member] || !remaining[member]) continue;
                const auto position = candidate.pattern.members[member].position;
                const auto u = candidate.pattern.u, v = candidate.pattern.v;
                const int32_t current = int32_t(result.members.size());
                result.members.push_back({member, x, y,
                    {(position.x + u.x * x) + v.x * y, (position.y + u.y * x) + v.y * y}});
                grid(x, y, member) = current; --remaining[member];
                for (size_t prior = 0; prior < member; ++prior)
                    append(x, y, prior, current, relation(0, prior, member));
                if (x > 0) for (size_t prior = member; prior < count; ++prior)
                    append(x - 1, y, prior, current, relation(1, prior, member));
                if (y > 0) for (size_t prior = member; prior < count; ++prior)
                    append(x, y - 1, prior, current, relation(2, prior, member));
                if (x > 0 && y > 0) for (size_t prior = member; prior < count; ++prior)
                    append(x - 1, y - 1, prior, current, relation(3, prior, member));
            }
    return result;
}
}
