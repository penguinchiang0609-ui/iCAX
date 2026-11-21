#pragma once
#include "core.hpp"
#include "recovered_placement.hpp"
namespace study::recovered {
struct TilingMember {
    uintptr_t part = 0;
    PlacementOrientation orientation;
    Point position{};
    Box box{};
    int32_t required = -1;
};
using TilingRelationMatrix = std::vector<std::vector<double>>;
struct TilingPattern {
    std::vector<TilingMember> members;
    Point u{}, v{};
    int32_t style = 1;
    std::array<TilingRelationMatrix, 4> commonCuts;
    int32_t maximumX = 0, maximumY = 0, maximumTotal = 0;
};
struct TilingCandidate {
    TilingPattern pattern;
    std::vector<int32_t> countsX, countsY;
    uint32_t perMemberLimit = UINT32_MAX;
};
struct TilingExpandedMember {
    size_t member = 0;
    int32_t x = 0, y = 0;
    Point position{};
};
struct TilingCommonCut { int32_t first = 0, second = 0; double value = 0.; };
struct TilingExpansion {
    std::vector<TilingExpandedMember> members;
    std::vector<TilingCommonCut> commonCuts;
};
bool tilingCandidateValid(const TilingCandidate &); //49b4c0
uint64_t tilingCandidateSize(const TilingCandidate &); //49b520
Box tilingCandidateBounds(const TilingCandidate &); //49b5a0 / 49b710
void reduceTilingCandidateDemand(TilingCandidate &, int32_t excess, Budget * = nullptr); //49a9b0
TilingCandidate buildTilingCandidate(TilingPattern, std::vector<int32_t> countsX,
    std::vector<int32_t> countsY, uint32_t perMemberLimit = UINT32_MAX, Budget * = nullptr); //49b030
TilingExpansion expandTilingCandidate(const TilingCandidate &, Budget * = nullptr); //4a4430 / 4a3be0
struct TilingRepetitionCounts {
    int32_t commonX=0,commonY=0;
    std::array<int32_t,2> extraX{},extraY{};
};
TilingCandidate selectGeneralTilingRepetition(std::span<const TilingPattern>,
    std::span<const TilingRepetitionCounts>,Budget * = nullptr); //4a5380
}
