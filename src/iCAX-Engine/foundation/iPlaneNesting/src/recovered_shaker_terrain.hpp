#pragma once
#include "core.hpp"

namespace study::recovered {
// Normalized 3aa6f0/3ac380/3ac7e0 state. Heights are stored right to left.
struct ShakerTerrain {
    std::vector<int32_t> heights;
    int64_t area80 = 0;
    int32_t last88 = -1, first8c = 0, minimum90 = 0, lower94 = 0, upper98 = 0;
};
struct ShakerTerrainPoint {
    int64_t x = 0;
    int32_t y = 0;
    bool operator==(const ShakerTerrainPoint &) const = default;
};
// 3ac380: find the rightmost nonempty row, then retain the top-connected
// interval while walking left; the running minimum makes monotone stairs.
ShakerTerrain shakerTerrainFromFreeRows(const Rows &,int32_t lower,int32_t upper,int32_t minimum);
// 3ac7e0: incremental staircase update using the occupied (1,0) raster.
void shakerUpdateTerrain(ShakerTerrain &,const Rows &,int32_t strip,int32_t y,int32_t layers);
int64_t shakerTerrainLoss(const ShakerTerrain &,const Rows &,int32_t strip,int32_t y,int32_t layers); // 3ad010
std::vector<ShakerTerrainPoint> shakerTerrainProfile(const ShakerTerrain &,double pitch); // 3ad1b0
std::vector<Polygon> shakerTerrainGuides(const ShakerTerrain &,double pitch); // 3ad6b0
}
