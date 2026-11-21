#pragma once
#include "recovered_factory.hpp"
#include "recovered_general_tiling.hpp"
#include "recovered_convolution_polygon.hpp"

namespace study::recovered {
// Context+1890, copied by 44dcb0 into a 0x20-byte worker parameter block.
// +50 is an internal fixed-division override; it is independent of head count.
struct MultiheadsHardware {
    int32_t heads48=1, fixedDivisions50=-1;
    double minimumGap58=0, maximumGap60=0;
};
struct MultiheadsParameters {
    int32_t heads=1;
    double minimumGap=0, maximumGap=std::numeric_limits<double>::max();
    int32_t fixedDivisions=-1;
    bool operator==(const MultiheadsParameters &)const=default;
};
MultiheadsParameters multiheadsParameters(const MultiheadsHardware &); // 44dcb0
struct MultiheadsPreNest {
    bool flag2b0=false,flag2b8=false,flag2c0=false;
    uint64_t modelCount198_8=0,tailCount198_38=0;
};
bool multiheadsPreNestCompatible(const MultiheadsPreNest &,double config90); // 5c8250
struct MultiheadsSpecialInput {
    bool hasPlacements=false,hasPreNestPlacements=false;
    int32_t priorityCount108=1;
    bool specialMaterial394170=false;
};
bool multiheadsDivisionApplicable(const MultiheadsSpecialInput &); // 3fdf50
enum class MultiheadsChildKind { Division,Clustering };
struct MultiheadsChild {
    MultiheadsChildKind kind=MultiheadsChildKind::Clustering;
    MultiheadsParameters parameters;
    bool instrumented=false,randomizeDivisions=false,randomCluster=false,halfClusters=false;
    int32_t clusterTrials=1;
    double clusterVariation=0;
    bool operator==(const MultiheadsChild &)const=default;
};
// 44dcb0 / 3ff930 / 3fb8f0. Instrumented variants differ only in reporting.
// Config values are converted with the original signed cvttsd2si operation.
std::vector<MultiheadsChild> multiheadsChildren(const MultiheadsHardware &,
    const MultiheadsSpecialInput &,int32_t trial13bc,double selection458=2,double reporting488=0);

struct MultiheadsDivisionInput {
    MultiheadsParameters parameters;
    double sheetHeight=0,bottom=0,top=0,maximumPartGap=0;
    std::span<const double> partHeights;
    std::span<const int32_t> availableQuantities;
};
// 3fdd30: a fixed positive division override bypasses all geometry/RNG.
int32_t multiheadsDivisionCount(const MultiheadsDivisionInput &,Random &,bool randomize);
struct MultiheadsDivisionPlan {
    int32_t divisions=0,repetitions=0;
    double laneHeight=0,repeatedLaneHeight=0,repetitionPitch=0;
    bool usable=false;
    bool operator==(const MultiheadsDivisionPlan &)const=default;
};
// Scalar preparation in 3fe9b0. The chosen pitch is the middle of the lower,
// sheet-fit and upper limits, preserving the original comparison order.
MultiheadsDivisionPlan multiheadsDivisionPlan(int32_t divisions,double usableHeight,
    double partGap,const MultiheadsParameters &);
int32_t multiheadsDivisionQuantity(int32_t quantity,const MultiheadsDivisionPlan &,
                                  bool fixedDivisionMode); // 3fe9b0
struct MultiheadsMethodGroup {
    int32_t count=0,available=0;
    double minimumY=0,maximumY=0;
};
int32_t multiheadsDivisionMethod(int32_t forcedMethod,std::span<const MultiheadsMethodGroup>,
                                double usableHeight,bool hasRectangleProvider,Random &); // 3ff2c0

struct MultiheadsClusterKey {
    int32_t heads=0;
    double pitch=0;
    bool operator==(const MultiheadsClusterKey &)const=default;
};
bool multiheadsClusterLess(const MultiheadsClusterKey &,const MultiheadsClusterKey &); // 3f76f0
using MultiheadsClusterChooser=std::function<MultiheadsClusterKey(std::vector<uintptr_t> &)>;
// 3fa100 owns the persistent chooser work vector, uses a 3*N+1 attempt limit,
// skips one-head keys and adds half clusters only after inserting a full key.
std::vector<MultiheadsClusterKey> multiheadsClusterKeys(int32_t trials,bool halves,
                                                       const MultiheadsClusterChooser &);

// 3fe9b0's actual specialized division controller. Geometric child factories
// remain explicit host services: Offset, Rectangle or Pattern. This is not an
// alias to an ordinary strategy; the narrow material and replication are part
// of this controller and must be honored by the host.
class MultiheadsDivisionHost {
public:
    virtual ~MultiheadsDivisionHost()=default;
    virtual bool hasPlacements()const=0;
    virtual void assertEmpty(bool)=0;
    virtual double usableHeight()const=0;
    virtual double usableWidth()const=0;
    virtual bool irregularSheet()const=0;
    virtual void useBoundsSheet()=0;
    virtual int32_t quantity(size_t)const=0;
    virtual size_t groupCount()const=0;
    virtual bool setQuantity(size_t,int32_t)=0;
    virtual void restoreQuantities()=0;
    virtual void useLane(double width,double height)=0;
    virtual void restoreSheet()=0;
    virtual int32_t chooseMethod()=0;
    virtual void runChild(int32_t method)=0;
    virtual void postChild()=0;
    virtual void repeat(const MultiheadsDivisionPlan &)=0;
    virtual size_t placementCount()const=0;
};
int32_t runMultiheadsDivision(const MultiheadsChild &,int32_t divisions,double maximumPartGap,
                             MultiheadsDivisionHost &);
struct MultiheadsReplicaPosition {int64_t x=0;int32_t y=0;bool operator==(const MultiheadsReplicaPosition &)const=default;};
class MultiheadsReplicationHost {
public:
    virtual ~MultiheadsReplicationHost()=default;
    virtual std::vector<size_t> placements()const=0;
    virtual MultiheadsReplicaPosition position(size_t)const=0;
    virtual void move(size_t,MultiheadsReplicaPosition)=0;
    virtual size_t createHeadGroup(size_t)=0;
    virtual void appendHeadGroup(size_t group,size_t placement)=0;
    virtual void attachHeadGroup(size_t placement,std::optional<size_t> group)=0;
    virtual std::optional<size_t> cloneAvailable(size_t source,MultiheadsReplicaPosition)=0;
    virtual void commit(size_t)=0;
    virtual bool remove(size_t)=0;
    // Native 492930/3c5a00 relationship transfer can refer only to a copy
    // already present in the (batch,head,original) table.
    virtual void copyRelationships(size_t source,size_t copy,int32_t batch,int32_t head,
        const std::map<std::tuple<int32_t,int32_t,size_t>,size_t> &)=0;
};
// Full non-owning placement/group controller in 3fdfb0. The host owns actual
// job availability and geometry handles; this function owns native ordering,
// grid translation, concurrent-head groups and fixed-mode rollback.
void multiheadsReplicate(const MultiheadsDivisionPlan &,double originX,double originY,
                        bool fixedDivisionMode,MultiheadsReplicationHost &);
using MultiheadsPitchInterval=std::array<double,2>;
// These helpers are the actual geometric interval services consumed by the
// clustering chooser, independent of its polygon/NFP acquisition service.
std::vector<MultiheadsPitchInterval> multiheadsPitchGaps(std::span<const MultiheadsPitchInterval>); //3f7fc0 nonempty input
std::vector<MultiheadsPitchInterval> multiheadsClipPitchIntervals(std::span<const MultiheadsPitchInterval>,MultiheadsPitchInterval); //3f8650
std::vector<std::array<int64_t,4>> multiheadsIntegerPitchSegments(std::array<int64_t,2>,std::span<const MultiheadsPitchInterval>); //3f8340
MultiheadsClusterKey multiheadsChooseClusterPitch(int32_t heads,std::span<const MultiheadsPitchInterval>,bool randomized,Random &); //3f7c80
struct MultiheadsClusterGeometry {int32_t identity=0;std::vector<TilingMember> members;};
MultiheadsClusterGeometry multiheadsClusterGeometry(MultiheadsClusterKey,uintptr_t part,PlacementOrientation,
                                                  Box actualVariantBounds,int32_t &nextIdentity); //3fa430 ->54b3b0/54b6c0
struct MultiheadsClusterPitchSource {
    std::vector<MultiheadsPitchInterval> intervals;
    double shapeHeight=0;
};
// Remaining 3f88b0 controller after actual source/normal-projected NFP interval
// production: hardware/stock clipping, head count, fixed-count rejection,
// randomized ranking, retained ties and final interval-span pitch request.
MultiheadsClusterKey multiheadsChooseClusterConfig(std::span<const MultiheadsClusterPitchSource>,
    int32_t available,const MultiheadsParameters &,double usableHeight,bool randomized,Random &);
// Geometric front of3f88b0 from an actual cached floating self-NFP and actual
// cached orientation basis/height entries. No orientation sampling is guessed.
std::vector<MultiheadsClusterPitchSource> multiheadsProducePitchSources(const ConvolutionPolygon &selfNfp,
    std::span<const ShakerPoint> queryDirections,std::span<const double> shapeHeights,
    double sheetHeight,Budget * =nullptr);
ShakerPoint multiheadsCachedOrientationBasis(PlacementOrientation); //520090 ->7f5ff0/7f5f70, native seed(0,10)
class MultiheadsGreedyHost {
public:
    virtual ~MultiheadsGreedyHost()=default;
    virtual int32_t prepareNext()=0;
    virtual void commitPrepared()=0;
    virtual double remainingTime()=0;
    virtual bool stopRequested()=0;
};
//3ee790: commit whole prepared groups before polling; poll on a new count/5
// bucket (including the first empty result), and preserve uint32 count wrap.
uint32_t runMultiheadsGreedy(MultiheadsGreedyHost &);
struct MultiheadsSelection {
    std::optional<size_t> placement;
    uint64_t metadata=0;
    int32_t kind=0;
};
using MultiheadsPlacement=std::optional<size_t>;
struct MultiheadsGreedyNextInput {bool predictionDisabledC0=false,hasPreNest=false,preNestCompatible=true;};
class MultiheadsGreedyNextHost {
public:
    virtual ~MultiheadsGreedyNextHost()=default;
    virtual void clearPrepared()=0;
    virtual void clearCandidateCache()=0;
    virtual MultiheadsSelection cachedCandidate()=0;
    virtual void clearCachedDetail(size_t)=0;
    virtual void select(MultiheadsSelection &)=0;
    virtual void setMetadata(uint64_t)=0;
    // Original observer callbacks receive mutable Shared handles, including
    // the ability to clear a single result or change compound members.
    virtual void notifySingle(MultiheadsPlacement &)=0;
    virtual std::vector<MultiheadsPlacement> materialize(size_t)=0;
    virtual void notifyCompound(MultiheadsPlacement &,std::vector<MultiheadsPlacement> &)=0;
    virtual bool acceptCompound(std::vector<MultiheadsPlacement> &)=0;
    virtual void appendPrepared(std::span<const MultiheadsPlacement>)=0;
    virtual void nullSelectionDiagnostic()=0;
};
//3eef90: the complete plain/compound selection/materialization control. Real
//3ee840 candidate evaluation and3eee70 member allocation are host services.
int32_t prepareMultiheadsGreedyNext(const MultiheadsGreedyNextInput &,MultiheadsGreedyNextHost &);
struct MultiheadsModuleDescriptor {
    uintptr_t geometry=0;
    int32_t memberCount=0;
    size_t catalogGroup=0;
    int32_t quantity=0;
    bool auxiliary=false;
};
std::vector<int32_t> multiheadsModuleCounts(std::span<const MultiheadsModuleDescriptor>); //3ee840
std::optional<size_t> multiheadsFindModule(std::span<const MultiheadsModuleDescriptor>,uintptr_t geometry); //3eed60
//545c10: current active catalog groups, actual remaining job demand, source
//auxiliary exclusion and exact requested module count. Preserve module order.
std::vector<size_t> multiheadsEligibleModules(std::span<const MultiheadsModuleDescriptor>,
    size_t catalogGroupCount,std::span<const size_t> activeGroups,std::span<const int32_t> placed,int32_t count);
class MultiheadsCountEvaluationHost {
public:
    virtual ~MultiheadsCountEvaluationHost()=default;
    virtual void prepareCount(int32_t)=0; //545c10
    virtual std::vector<size_t> produceCandidates()=0; //545ec0
    virtual void cacheCandidates(std::span<const size_t>)=0; //3961c0
    virtual MultiheadsSelection cachedCandidate()=0; //401090
    virtual void clearCachedDetail(size_t)=0;
};
//3eea80: score metadata is the exact double bit pattern. Preserve the old
//selection for near ties, larger candidate scores, and unordered comparisons.
void evaluateMultiheadsCount(int32_t,MultiheadsSelection &,MultiheadsCountEvaluationHost &);
void selectMultiheadsModuleCandidate(std::span<const MultiheadsModuleDescriptor>,
    MultiheadsSelection &,MultiheadsCountEvaluationHost &); //3ee840
class MultiheadsMemberAdmissionHost {
public:
    virtual ~MultiheadsMemberAdmissionHost()=default;
    virtual int32_t state(size_t)=0;
    virtual void insertTemporary(size_t)=0;
    virtual bool validateTemporary(size_t)=0;
    virtual void removeTemporary(size_t)=0;
};
//546c90: test each member against the incrementally inserted prefix, then
//remove every attempted member in reverse order, including a failed member.
bool multiheadsAdmitMembers(std::span<const size_t>,MultiheadsMemberAdmissionHost &);
} // namespace study::recovered
