#pragma once
#include "recovered_construction.hpp"
namespace study::recovered {
struct WarmPlacement:ProcessPlacement {
    virtual ~WarmPlacement()=default;
    int64_t leftE0=0,rightF0=0,x48=0;
    int32_t bottomE8=0,topF8=0,y50=0;
    int32_t instance18=0;
    bool auxiliary3c1=false;
    double quantity220=-1,area58=0;
    bool rounded2c=false;
    int32_t marker28=0;
    std::map<std::string,std::string> attributes68;
    std::shared_ptr<const Polygon> geometryOwner;
};
using WarmPlacementPtr=std::shared_ptr<WarmPlacement>;
struct PlacementCloneInput {
    int64_t dx=0,pitch=1,originX=0;
    int32_t dy=0,instance18=0;
    uintptr_t instance40=0;
};
// 3c91a0/3c5220: shared immutable geometry, independent properties and process
// containers, source group slot reuse, then commit with validation disabled.
WarmPlacementPtr clonePlacement(const WarmPlacement &,const PlacementCloneInput &,
    const PlacementCopyLookup &,bool &validateOnCommit,const std::function<void(const WarmPlacementPtr&)> &commit);
struct WarmOrderInput {double blend=0,x=0,y=0;int32_t sign=1;};
double warmOrderScore(const WarmPlacement &,const WarmOrderInput &);
// Original 449e20 introsort and its insertion, median, partition, heap paths.
// The index vector preserves source ownership and every equal-key swap.
std::vector<size_t> warmPlacementOrder(std::span<const WarmPlacementPtr>,const WarmOrderInput &,
    std::optional<size_t> initialDepth=std::nullopt);
struct WarmLineage {
    int32_t source=0,trial=0;
    uint64_t retained=0;
    auto operator<=>(const WarmLineage &)const=default;
};
struct WarmNesting:TrialNesting {
    Random random{0};
    std::vector<WarmPlacementPtr> placements;
    int32_t placedCount13d8=0,columnLimit1540=0;
    double quantity18=0,quantity28=0,totalValue78=0;
    double sheet70=0,sheet78=0,box08=0,box10=0;
    int64_t maximumRight=0;
    int32_t integerMeasure=0,machineCount1f8=0;
    double machineSpacing1f0=0;
    bool machineAvailable=false,machineAllowsOrder=false;
    std::vector<WarmLineage> lineage14f8;
    double fraction1510=0;
    int32_t source1518=0,sourceTrial151c=0,depth1520=0;
};
class WarmStartHost {
public:
    virtual ~WarmStartHost()=default;
    virtual WarmNesting &current()=0;
    virtual bool isColumnNesting()=0; // original dynamic_cast
    virtual void resetCurrent()=0; // mode=1, including failure rollback
    virtual void removeTrailingPlacements()=0; // 3cab20, removes from tail until empty or removal fails
    virtual void notifyStartPredefined()=0; // 3cd050
    virtual void resetSelectionQueue()=0; // 36e430
    virtual bool instanceAvailable(int32_t)=0;
    virtual bool acceptsPlacement(const WarmPlacement &)=0; // 389fd0
    virtual WarmPlacementPtr cloneAndInsert(const WarmPlacement &)=0; // 3c91a0
    virtual double copyProcesses(const WarmPlacement &)=0; // 3c85d0
    virtual void notifyEndPredefined()=0; // 3cd370
    virtual void pruneAuxiliaryGroups()=0;
    virtual double debugColorConfig750()=0;
    virtual void colorPlacements(const char *color)=0;
};
struct WarmApplication {bool applied=false,reordered=false,resetAfterFailure=false;size_t retained=0,copied=0;double accumulated=0;};
// 449180. Implements selection/order/truncation, replay admission, process
// accounting, reset and lineage. Geometry cloning/constraints are explicit
// host providers; this is not a substitute implementation of those providers.
WarmApplication applyWarmStart(WarmStartHost &,const TrialRecordPtr &,double fraction,bool high);
}
