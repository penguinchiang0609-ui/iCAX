#pragma once
#include "recovered_shaker_scene.hpp"
#include "recovered_shaker_step.hpp"
namespace study::recovered {
struct ShakerFinalizeState {
    std::vector<ShakerPoint> specialReferences;
    std::optional<ShakerPoint> specialDirection;
    std::vector<int32_t> freeParts;
    std::array<int32_t,2> directionMultiplicities{}; // +10c/+110 are signed integers
    std::array<double,4> ranges{}; // +128,+130,+138,+140
    ShakerPoint allowances{}; // +148,+150
};
void shakerSpecialLineRow(LinearModel &,std::array<int32_t,2>,ShakerPoint,ShakerPoint); // 7249e0
void shakerSpecialDirectionRows(LinearModel &,std::array<int32_t,2>,ShakerPoint,ShakerPoint,ShakerPoint); // 724bb0
size_t shakerFilterDeferred(std::vector<DeferredShakerRow> &,ShakerPoint); // 72adb0
std::map<int32_t,std::array<uint64_t,2>> shakerSpecialPartRows(LinearModel &,std::array<int32_t,2>,int32_t,
    std::vector<DeferredShakerRow> &,ShakerPoint,ShakerPoint,std::optional<ShakerPoint>); // 724f70
void shakerDirectionalCornerRows(const ShakerProblem &,LinearModel &,bool,bool); // 725600
void shakerFinalizeModel(ShakerProblem &,std::span<ShakerPart>,LinearModel &,const ShakerFinalizeState &,
    ShakerReportState &,ShakerSceneGeometry &); // 725a30
}
