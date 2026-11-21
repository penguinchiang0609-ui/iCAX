#pragma once
#include "recovered_scheduler.hpp"
#include <functional>
namespace study::recovered {
struct Preset {
    std::map<int,StrategyEntry> entries;
    double totalWeight=0;
};
// 4567f0: a repeated manager ID replaces its entry, while the stored total is
// incremented by the NEW weight. Do not silently normalize this original rule.
void addPreset(Preset &,StrategyEntry);
// 456ab0: least significant decimal digit is ID 1; zero digits do not create
// managers or test applicability. The factory supports IDs 1..10.
void decimalPreset(Preset &,int64_t,const std::function<std::optional<StrategyEntry>(int)> &);
int32_t strategyCalibration(int id,int32_t offsetD8,double columnConfig2b0);
struct EligibilityPart {
    uint64_t identity=0;
    bool optional=false;
    int32_t required=0, geometry7c=1;
    bool descriptor238=true;
    double minX=0,minY=0,maxX=0,maxY=0,area=0,value=0,overrideValue=-1;
};
struct MapEligibilityState {int32_t status=3;double costLimit=-1;};
struct MapEligibilityInput {
    std::span<const EligibilityPart> parts;
    double typeLimit188=0;
    int32_t problemScale=0;
    int64_t pitch230=0;
    // TechnologyOptions+d0 is P_MAX_MEMORY (default 8000), not a time limit.
    double memoryLimitD0=8000;
    std::function<double()> estimateCost;
};
// 36f0e0: caller serializes the state; the original lazy read/write lock is
// outside this arithmetic kernel. estimateCost is called only on that branch.
bool mapEligible(MapEligibilityState &,const MapEligibilityInput &);
bool columnEligible(bool map,int32_t population88,int32_t scale,bool sheetSpecial);
bool sheetIsSpecial(size_t segmentCount,std::array<double,4> margins);
bool preNestMixed(std::span<const int32_t> keys);
struct PreNestInput {
    int32_t mode90=0;
    bool finalized2c0=true,flag2ba=true,flag2b2=true;
    uint64_t constraintCount8=0,constraintCount30=0;
    std::span<const uint8_t> constraintTileable;
};
bool preNestCompatible(const PreNestInput &);
struct EligibilityInput {
    std::span<const EligibilityPart> parts;
    bool hasPreNest=false,preTileable=true,preMixed=false,hasTiles28=false;
    int32_t heads48=1;
    bool preMultiheadsCompatible=false;
    // Original groups may be empty, homogeneous or mixed. Identity is the
    // original part type index; homogeneous qualifying types count once.
    std::vector<std::vector<size_t>> rectangleGroups;
    bool rectangleForceB0=false,preFlag4c=false;
    int32_t preCount50=0;
    double rectangleCount998=0;
    double sheetHeight=0;
};
bool escherEligible(int32_t &cacheD8,const EligibilityInput &);
bool rectangleEligible(const EligibilityInput &);
std::vector<uint64_t> rectangleTypeOrder(std::span<const uint64_t> insertions);
bool multiheadsEligible(const EligibilityInput &);
bool unplacedEligible(std::span<const uint8_t> moduleSlot16);
}
