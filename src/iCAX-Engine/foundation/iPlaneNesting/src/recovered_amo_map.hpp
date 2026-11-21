#pragma once
#include "recovered_amo_tiling.hpp"
#include <memory>
namespace study::recovered {
using AmoTilingContours=std::vector<std::vector<Point>>;
struct AmoTilingMapSource {
    const AmoTilingShape *shape=nullptr;
    bool flag59=false,flag5a=false;
};
struct AmoTilingMapPeriodCache {bool valid=false;double value=0;};
struct AmoTilingContourMap {
    AmoTilingMapSource first,second;
    AmoTilingContours simplified,detailed,rotatedSimplified,rotatedDetailed;
    bool usable=false,prepared=false;double angle=0;
    std::array<AmoTilingMapPeriodCache,2> cachedPeriods{}; //X at80/88, Y at90/98
    std::array<std::shared_ptr<void>,2> cachedIndices;
};
struct AmoTilingMapGeometry {
    // These are the actual floating BoolConvolution stages 8c0e50 and
    // 8c1090. Integer Minkowski and rectangle proxies are not substitutes.
    std::function<void(AmoTilingContourMap &)> simplified;
    std::function<void(AmoTilingContourMap &)> detailed;
};
struct AmoTilingMapCache {
    std::map<std::pair<const AmoTilingShape *,const AmoTilingShape *>,std::shared_ptr<AmoTilingContourMap>> entries;
};
void amoTilingRotateContourMap(AmoTilingContourMap &,double angle); //8c1480
std::shared_ptr<AmoTilingContourMap> amoTilingBuildContourMap(AmoTilingMapCache &,const AmoTilingMapSource &,
    const AmoTilingMapSource &,double angle,const AmoTilingMapGeometry &); //8bde50
using AmoTilingCandidateFamilies=std::vector<std::vector<AmoTilingCandidatePtr>>;
struct AmoTilingFamilyPreparation {
    AmoTilingMapCache *maps=nullptr;
    bool fixedTranslation=false;Point translation{};
};
struct AmoTilingMapRuntime {
    AmoTilingMapCache maps;
    std::array<AmoTilingMapSource,2> sources;
    AmoTilingMapGeometry geometry;
};
double amoTilingMapAxisY(AmoTilingContourMap &,double x); //8c12a0
double amoTilingMapAxisX(AmoTilingContourMap &,double y); //8c1390
double amoTilingPeriodY(AmoTilingMapRuntime &,AmoTilingJob &,const AmoTilingCandidate &); //8be1e0
double amoTilingPeriodX(AmoTilingMapRuntime &,AmoTilingJob &,const AmoTilingCandidate &); //8be6b0
AmoTilingCandidateFamilies amoTilingPrepareFamilies(AmoTilingFamilyPreparation &,const AmoTilingMapSource &,
    const AmoTilingMapSource &,double angle,const AmoTilingMapGeometry &); //8bd500
}
