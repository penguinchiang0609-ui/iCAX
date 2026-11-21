#pragma once
#include "recovered_construction.hpp"
#include "recovered_preset.hpp"
#include "recovered_strategy.hpp"

namespace study::recovered {
// Defaults in fc18e0/fc22f0, consumed through the configuration object.
struct ManagerFactoryConfig {
    double offsetTryCount278 = 10;
    double columnTryCount2b0 = 50;
    double initialTrialCount7c0 = 1;
    double candidateLimit1c0 = 4;
    double reverseProblem1d0 = 1;
};
struct StrategyManager {
    int32_t baseId = 0;
    bool reversed = false;
    uintptr_t reverseSource = 0;
    int32_t sourceId = -1;
    int32_t candidateLimit = 0;
    int32_t offsetTryCount = 0;
    int32_t escherEligibility = 2;
    TrialManagerState trials;
    // The original manager owns its local pool. Values and synchronization
    // remain with the construction host using this descriptor.
};
// 44e420 / 450140 / 44fa50. Out-of-range IDs return an empty owner.
std::optional<StrategyManager> strategyManager(int32_t baseId, bool reversed,
    uintptr_t reverseSource, const ManagerFactoryConfig & = {});
int32_t strategyManagerId(const StrategyManager &);
int32_t strategyManagerCalibration(const StrategyManager &, const ManagerFactoryConfig & = {});
struct FactoryApplicabilityContext {
    MapEligibilityState map;
    MapEligibilityInput mapInput;
    EligibilityInput input;
    int32_t population88 = 0;
    bool specialSheet = false;
    std::vector<uint8_t> unplacedModules;
};
bool reverseStrategyEligible(double mode, int32_t population88, int32_t problemScale); // 450fe0
bool strategyManagerApplicable(StrategyManager &, FactoryApplicabilityContext &,
                               const ManagerFactoryConfig & = {});

using ManagerApplicability = std::function<bool(StrategyManager &)>;
struct FactoryPreset {
    Preset preset;
    std::map<int32_t, StrategyManager> managers;
};
// 456ab0 / 456c50. Each nonzero decimal position creates a new manager,
// queries applicability and, if admitted, replaces that ID in the preset.
void appendFactoryPreset(FactoryPreset &, uint64_t encoded, bool reversed,
    uintptr_t reverseSource, const ManagerFactoryConfig &, const ManagerApplicability &);
// 5a15a0. Reverse managers are requested only when their source exists.
FactoryPreset initialFactoryPreset(uint64_t direct, uint64_t reverse, uintptr_t reverseSource,
    const ManagerFactoryConfig &, const ManagerApplicability &);
// 5a49f0. Selected managers bypass applicability, matching the recovered
// branch. An empty selected preset falls back to the fast decimal preset.
FactoryPreset selectedFactoryPreset(std::span<const StrategyResult>, int32_t trials,
    double config198, double config190, uint64_t fastPreset, uintptr_t reverseSource,
    const ManagerFactoryConfig &, const ManagerApplicability &);

struct OffsetCandidaterInput {
    double enableRightToLeft = 1;
    bool nestingFlag14b0 = false;
    uint64_t priorityGroups = 1;
    bool sparseLayout = false;
    bool specialSheet = false;
    double maximumForwardStrips280 = 100, stripsPerPart9d8 = 50;
};
struct OffsetCandidaterPlan {
    int32_t maximumForwardStrips = 100;
    int32_t extremaCount = 4;
    bool field90 = true;
    bool field92 = true;
    // The right-to-left flag is stored at +91 by 3d8ad0.
    bool rightToLeft = false;
    bool field93 = false;
};
// 44bdc0. The optional RNG draw precedes the priority and sparse-layout
// overrides, so those overrides do not suppress an already required draw.
OffsetCandidaterPlan offsetCandidaterPlan(Random &, const OffsetCandidaterInput &);
int32_t extremalForwardLimit(double maximum280, double stripsPerPart9d8); // 3d8ad0
struct OffsetSparsePart {
    int32_t required = 0;
    int32_t placed = 0;
    bool optional = false;
    double area = 0;
};
struct OffsetSparseInput {
    std::span<const OffsetSparsePart> parts;
    int32_t placementCount = 0;
    double placedArea = 0;
    double sheetWidth = 0, sheetHeight = 0;
    std::array<double, 4> margins{};
    bool specialSheet = false;
};
bool offsetSparseLayout(const OffsetSparseInput &); // 3cd920
struct DecreasingRandomEvaluation {
    double amplitude = 0;
    int32_t totalCount = 0;
    double inverse = 1, range = 0, normalization = 1, minimum = 1;
    double randomLow = 0, randomHigh = 1;
};
DecreasingRandomEvaluation decreasingRandomEvaluation(double amplitude, int32_t totalCount); // 366c40
void decreasingRandomSetPlaced(DecreasingRandomEvaluation &, int32_t placed); // 366de0
// 366d50. Drawing and constructing the factor precedes the base provider call.
double decreasingRandomValue(const DecreasingRandomEvaluation &, Random &,
                             const std::function<double()> &baseValue);

struct OffsetValuePlan {
    ReferenceKind reference = ReferenceKind::Normal;
    Complexity complexity;
    std::vector<double> values;
    std::optional<DecreasingRandomEvaluation> decreasing;
};
struct OffsetFactoryInput {
    int32_t count = 10;
    int32_t population88 = 0;
    bool dynamicEvaluation = false;
    ComplexityConfig complexity;
    OffsetCandidaterInput candidater;
    bool forceProcessInspection = false;
    double reverseReferenceScale290 = .55;
};
struct OffsetChildPlan {
    OffsetValuePlan evaluation;
    OffsetCandidaterPlan candidater;
    bool inspectProcesses = false;
    double evaluationReferenceScale = 1;
};
// 367420 / 44ccf0 / 44ce90. All value objects are constructed before any
// candidater or process-inspection draw. The no-process branch still draws.
OffsetValuePlan offsetValue(Random &, const OffsetFactoryInput &,
                            const std::vector<ValuePart> &, double totalArea);
std::vector<OffsetChildPlan> offsetFactory(Random &, const OffsetFactoryInput &,
                                         const std::vector<ValuePart> &, double totalArea);
std::vector<OffsetChildPlan> offsetChildren(Random &,const OffsetFactoryInput &,std::span<const OffsetValuePlan>);

struct FinalizeKey {
    uint8_t complete = 0;
    int64_t correctedLength = 0;
    double value = 0;
};
bool finalizeLess(const FinalizeKey &, const FinalizeKey &); // 3ecaa0
class FinalizeHost {
public:
    virtual ~FinalizeHost() = default;
    virtual size_t childCount() const = 0;
    virtual double remainingTime() = 0;
    virtual bool stopRequested() = 0;
    virtual void checkpoint() = 0;
    virtual int runChild(size_t) = 0; // attach stop owner, prepare and run (3ee340)
    virtual FinalizeKey evaluate(int32_t sourceId) = 0;
    virtual std::span<const Placement> placements() const = 0;
    virtual void restore() = 0;
    virtual void commit(const Placement &) = 0;
};
// 3ecb20. Singleton children bypass time/stop checks and all ranking work.
int runFinalize(FinalizeHost &);
class CombinedHost {
public:
    virtual ~CombinedHost()=default;
    virtual uint64_t totalInstanceCount()=0;
    virtual int32_t runChild(size_t)=0;
    virtual double remainingTime()=0;
    virtual bool stopRequested()=0;
};
// 40bb00: run the first child unconditionally; run the continuation only
// unless remaining time is ordered <=0, a stop is requested or the reported
// count equals the low 32 bits of the original instance-vector length.
int32_t runCombined(CombinedHost &);
struct ColumnTrialConfig {
    double longLengthRatio2b8=.5,superLongOverrideRate2c0=.3;
    double superLongLengthRatio2c8=2,smallLengthRatio2d0=.1;
};
struct ColumnTrialPlan {int32_t mode=0;double lengthFraction=0;std::vector<Point> directions;};
ColumnTrialPlan columnTrialPlan(Random &,double context218,double context220,bool flag119,
                                const ColumnTrialConfig & = {}); // 3f0330
struct ColumnPart {
    size_t key=0;double width1b0=0,area58=0;int32_t required254=0,placed=0;bool optional3c1=false;
};
struct ColumnPartition {std::vector<size_t> primary,secondary;double remainingArea=0;};
ColumnPartition columnPartition(std::span<const ColumnPart>,double widthThreshold,double areaThreshold); // 3f0f00
std::vector<size_t> columnSelectGroups(Random &,std::span<const ColumnPart>,std::span<const size_t>,
                                      uint32_t limit,bool weighted); // 3d78b0 / 3d7570
}
