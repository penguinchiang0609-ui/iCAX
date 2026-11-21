#pragma once
#include "recovered_catalog.hpp"
#include "recovered_offset.hpp"
#include "recovered_bt.hpp"
#include "recovered_direction.hpp"
#include "recovered_strategy.hpp"
#include "recovered_placement.hpp"
#include "recovered_factory.hpp"
#include "recovered_column.hpp"
#include "recovered_context.hpp"
namespace study {
namespace recovered {struct ShakerTerrain;}
recovered::ShakerTerrain ordinaryTerrain(const Input &,const Output &,int64_t pitch,Budget &,int32_t minimum);
struct OrdinaryControl {
    recovered::ReferenceKind reference = recovered::ReferenceKind::Normal;
    double referenceScale = 1;
    std::optional<Polygon> referenceGeometry;
    bool extendReferenceSheet = true;
    bool auxiliaryReference = false;
    double auxiliaryReferenceScale = 1;
    int64_t targetX = -1;
    std::vector<double> values;
    int placementLimit = -1, extremaCount = 4;
    // Explicit upstream 3d8ad0 limit; -1 keeps the adapter's whole-map sweep.
    // Original parameter-table +9d8/config +280 producers are not inferred.
    int extremaMaxSteps = -1;
    std::array<bool,4> directions{true,true,true,true};
    std::vector<size_t> keys; // Empty here means all groups; Catalog itself retains exact empty-key semantics.
    bool keysExplicit=false;
    std::optional<std::array<int32_t,2>> columnBounds;
    std::vector<Point> directionVectors; // Nonempty selects original DirectionCandidater, not extrema.
    std::function<double(double)> dynamicValue;
    std::function<void(int32_t)> prepareEvaluation;
    // Specialized evaluators may inspect a temporary real placement and restore
    // it before returning. Empty callbacks preserve the ordinary path.
    std::function<double(const Placement &,double)> evaluatePlacement;
    std::function<void(size_t,size_t,std::optional<size_t>,Rows &,int64_t,int64_t,int64_t)> filterModuleRows;
    int64_t fixedFront=0;
    std::optional<std::vector<Placement>> replay;
};
struct OrdinaryState {
    // The real Context map-eligibility cache is supplied by the Supervisor.
    // A false result is a definitive original 3e2150 NoMap branch.
    std::function<bool()> mapEligible;
    size_t noMapCandidateQueries=0,noMapCommitted=0;
    struct PlacementHost {
        recovered::WarmNesting nesting;
        recovered::PlacementLifecycle lifecycle;
        std::vector<std::vector<recovered::WarmPlacement>> modules;
        double requiredArea = 0, informerArea = 0, queueAggregate20 = 0;
        PlacementHost(std::span<const size_t> groups):lifecycle(nesting,groups) {
            lifecycle.validateOnCommit=false;
            lifecycle.observers.push_back({
                [this](const auto &p){informerArea+=p->area58;queueAggregate20+=p->area58;},
                [this](const auto &p){
                    informerArea-=p->area58;queueAggregate20-=p->area58;
                    // 391740 resets only the queue accumulator on its last removal.
                    if(nesting.placements.size()==1)queueAggregate20=0;
                }});
        }
    };
    Output output;
    std::optional<recovered::Catalog> catalog;
    // Heap ownership keeps the lifecycle's nesting reference stable when the
    // adapter state is moved. No mutable state is shared by a copied snapshot.
    std::unique_ptr<PlacementHost> placementHost;
};
struct ReferenceProbe {
    int64_t initialCount = 0, currentCount = 0; double height = 0;
    bool wantFirstPositions=false;
    std::vector<std::optional<int32_t>> firstRightStrip;
    // A concrete monitored instance must not borrow a catalog representative's
    // constraints when several ordinals share the same geometry.
    std::map<size_t,size_t> firstPositionOrdinals;
    std::optional<recovered::FrontInput> front;
    std::optional<recovered::Initialization> initialization;
    std::optional<recovered::ScoreState> scoreState;
};
void filterPointsProximityRows(const Input &,const OrdinaryState &,size_t,size_t,std::optional<size_t>,
                              Rows &,int64_t,int64_t,int64_t);
void validatePointsProximityInput(const Input &);
int ordinaryRun(const Input &, Budget &, OrdinaryState &, const OrdinaryControl * = nullptr, ReferenceProbe * = nullptr);
void ordinaryRestore(OrdinaryState &, size_t placementCount);
void ordinaryCommit(OrdinaryState &, const Placement &);

// Explicit candidate-pair program, for wiring/testing recovered Bt before the
// supplier's complete default candidate generator and context producers are recovered.
struct BtProgram {
    std::optional<recovered::Random> initialRandom;
    std::optional<Polygon> referenceGeometry;
    bool dynamicQueue = false;
    struct Direction10 {
        double contextField218=1,contextField220=1;int batch=1,count10=1;
        double alpha50=0,gamma58=0,perimeterProbability=.5,perimeterScale=.05;
    };
    std::optional<Direction10> direction10;
    struct Direction14 { double contextField218 = 1, contextField220 = 1; int batch = 1; };
    std::optional<Direction14> direction14;
    struct Direction18 { double contextField218 = 1, contextField220 = 1; int batch = 1, limit18 = 10; };
    std::optional<Direction18> direction18;
    struct Extremal1c { int batch=1,count1c=1;int64_t front=0;bool forceProcessInspection=false; };
    std::optional<Extremal1c> extremal1c;
    bool combineGenerators=false, replayPrediction=false, filterReplayAtFront=false;
    std::vector<OrdinaryControl> stages;
    std::vector<recovered::BtCandidate> candidates;
    std::vector<double> evaluationValues;
    recovered::ReferenceKind reference = recovered::ReferenceKind::Normal;
    double targetField14b8 = 0, queueAggregate20 = 0;
    int64_t maximumLengthX = 0;
    bool lengthMode = false;
};
struct BtTrace { size_t rounds = 0, trials = 0, restored = 0, committed = 0, predicted = 0, generatedPairs = 0, perturbedStages = 0, dynamicEvaluations=0,replayPairs=0,replayed=0; };
Output ordinaryBt(const Input &, const BtProgram &, Budget &, BtTrace * = nullptr);
int ordinaryBtContinue(const Input &, const BtProgram &, Budget &, OrdinaryState &, BtTrace * = nullptr);
struct MultiProgram {
    std::optional<recovered::Random> initialRandom;
    std::optional<Polygon> referenceGeometry;
    bool dynamicQueue = false;
    int contextBatch238=1;
    double contextField218=1,contextField220=1;
    recovered::ComplexityConfig config;
    recovered::ReferenceKind currentReference=recovered::ReferenceKind::Normal;
    double targetField14b8=0,queueAggregate20=0;
    int64_t maximumLengthX=0,fixedFront=0;
    bool lengthMode=false,forceProcessInspection=false,replayPrediction=true,filterReplayAtFront=false;
};
BtProgram multiProgram(const Input &,const MultiProgram &,recovered::MultiFactoryResult * = nullptr);
Output ordinaryMulti(const Input &,const MultiProgram &,Budget &,BtTrace * = nullptr);
int ordinaryMultiContinue(const Input &,const MultiProgram &,Budget &,OrdinaryState &,BtTrace * = nullptr);
struct OffsetProgram {
    recovered::Random random{1};
    std::vector<recovered::OffsetChildPlan> children;
    std::optional<Polygon> referenceGeometry;
    bool extendReferenceSheet=true;
    bool auxiliaryReference=true;
    double reverseReferenceScale=.55;
};
struct OffsetTrace {size_t trials=0,restored=0,committed=0,dynamicEvaluations=0;};
OffsetProgram offsetProgram(const Input &, recovered::OffsetFactoryInput = {}, const recovered::Random * = nullptr);
Output ordinaryOffset(const Input &, const OffsetProgram &, Budget &, OffsetTrace * = nullptr);
int ordinaryOffsetContinue(const Input &,const OffsetProgram &,Budget &,OrdinaryState &,OffsetTrace * = nullptr);
std::vector<recovered::ValuePart> ordinaryValueParts(const Input &,double &totalArea,int32_t &population);
recovered::OrdinaryContextMeasures ordinaryContext(const Input &);
struct ColumnProgram {
    recovered::Random random{1};recovered::OffsetValuePlan evaluation;
    recovered::ColumnConfig config;OffsetProgram fallback;
    Polygon valueReference,measurementReference;
    std::vector<recovered::ColumnPart> parts;
    std::vector<double> minimumWidth;
    double context218=0,context220=0,averageArea228=0;
};
struct ColumnTrace {size_t columns=0,trials=0,restored=0,committed=0,dynamicEvaluations=0;OffsetTrace fallback;};
ColumnProgram columnProgram(const Input &,const recovered::ColumnConfig & = {}, const recovered::Random * = nullptr);
Output ordinaryColumn(const Input &,const ColumnProgram &,Budget &,ColumnTrace * = nullptr);
int ordinaryColumnContinue(const Input &,const ColumnProgram &,Budget &,OrdinaryState &,ColumnTrace * = nullptr);
using ReverseRunner = std::function<int(const Input &, Budget &, OrdinaryState &)>;
using ReverseRunnerFactory = std::function<ReverseRunner(const Input &)>;
struct ReverseTrace {
    size_t factories = 0, trials = 0, transferred = 0;
    std::array<uint32_t, 2> salts{};
    std::array<int32_t, 2> nestingTrials{};
};
struct ReverseRuntime {
    double configuredSeed = 0;
    uint32_t salt = 0;
    int32_t trial = 0;
    int64_t bound = 0;
    double shake = 0;
    std::vector<recovered::WarmLineage> lineage;
    double fraction = 0;
    std::array<uint32_t, 5> lineageFields{};
};
// The factory creates the actual reverse child once, before the first transfer.
// The runner continues the reverse layout after its transferred warm placements.
Output ordinaryReverse(const Input &, const ReverseRunnerFactory &, Budget &,
                       OrdinaryState *existing = nullptr, ReverseTrace * = nullptr,
                       const ReverseRuntime * = nullptr);
Output ordinaryReverseOffset(const Input &, recovered::OffsetFactoryInput, Budget &,
                             OrdinaryState *existing = nullptr, ReverseTrace * = nullptr);
Output ordinaryReverseMulti(const Input &, const MultiProgram &, Budget &,
                            OrdinaryState *existing = nullptr, ReverseTrace * = nullptr);
Output ordinaryReverseColumn(const Input &, const recovered::ColumnConfig &, Budget &,
                             OrdinaryState *existing = nullptr, ReverseTrace * = nullptr);
}
