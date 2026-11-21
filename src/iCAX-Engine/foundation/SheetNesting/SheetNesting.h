#pragma once
#include <atomic>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::SheetNesting
{
    struct Point { double X = 0, Y = 0; };
    // Local reflection precedes rotation: world = T + R(angle) * (x, mirrored ? -y : y).
    struct Pose { double X = 0, Y = 0, AngleDegrees = 0; bool Mirrored = false; };
    struct Rect { double X = 0, Y = 0, Width = 0, Height = 0; };
    // Input geometry classification, not a list of independent packing strategies.
    // Triangles/trapezoids provide pairing motifs for rectangle/parallelogram packing.
    enum class ShapeKind { Rectangle, NearRectangle, Triangle, Parallelogram, Trapezoid, Circle, Irregular };

    // Algorithm input in millimetres. A circle uses Radius>0 and Center, with no polygon rings.
    // Polygon rings describe material: one outer ring and zero or more disjoint inner holes.
    struct Shape
    {
        std::vector<Point> Outer;
        std::vector<std::vector<Point>> Holes;
        double Radius = 0;
        Point Center;
        // Optional common-edge eligibility in Outer, Holes order. Empty keeps
        // the historical all-rings policy. Approximated curved rings remain
        // material boundaries but must be false so their chords are not seams.
        std::vector<bool> CommonEdgeRings;
    };
    struct Part
    {
        std::string ID;
        Shape Geometry;
        std::size_t Quantity = 1;
        std::string Material;
        bool FreeRotation = true;
        std::vector<double> AllowedAnglesDegrees;
        bool AllowMirror = false;
    };
    struct Stock
    {
        std::string ID;
        double Width = 0, Height = 0;
        std::size_t Quantity = 1;
        double Cost = 1;
        std::string Material;
    };
    struct Problem
    {
        std::vector<Part> Parts;
        std::vector<Stock> Stocks;
        double PartGap = 0;
        double EdgeMargin = 0;
        double Tolerance = 1e-6;
        // Straight polygon boundaries may share a positive-length segment.
        // Other pairs retain PartGap; all pairs must still have disjoint material.
        bool AllowCommonEdges = false;
    };
    struct Classification
    {
        ShapeKind Kind = ShapeKind::Irregular;
        double Area = 0;
        double EnvelopeWidth = 0, EnvelopeHeight = 0;
        double AlignmentDegrees = 0;
        double EnvelopeFill = 0;
    };
    struct Placement
    {
        std::string PartID;
        std::size_t Instance = 0;
        Pose Transform;
        std::size_t Group = 0; // Current planning tag, not a rigid constraint; a postprocessor may detach members.
        bool GapFiller = false; // Construction provenance, retained when a placement is moved.
    };
    struct Layout
    {
        std::string StockID;
        std::size_t StockInstance = 0;
        double Width = 0, Height = 0;
        std::vector<Placement> Placements;
        double MaterialArea = 0;
    };
    struct Unplaced { std::string PartID; std::size_t Quantity = 0; };
    struct Statistics
    {
        std::size_t CandidatePatterns = 0;
        // Constructor-origin counts; not counts of rigid groups surviving postprocessing.
        std::size_t CombinationGroups = 0;
        std::size_t CircleLatticeGroups = 0;
        std::size_t GapFilledParts = 0;
        std::size_t PlacementAttempts = 0;
        double ElapsedSeconds = 0;
        double PreparationSeconds = -1; // Negative means this stage was not instrumented.
        double PlacementSeconds = -1;
        double VerificationSeconds = -1;
        double EvaluationSeconds = -1;
        double PostProcessingSeconds = -1;
    };
    enum class Status { Complete, Partial, InvalidInput, Failed, Cancelled };
    struct Result
    {
        Status State = Status::Failed;
        std::vector<Layout> Layouts;
        std::vector<Unplaced> Remaining;
        std::vector<std::string> Diagnostics;
        Statistics Stats;
        bool GeometryValidated = false;
    };
    // Utilization is the default objective. Complete original demand is a hard
    // eligibility tier; compare net material / consumed stock area, then net
    // material / full-stock-width used sections when stock utilization ties.
    // Lateral and leading blanks belong to those sections. Occupied part
    // envelopes break only equal section utilization; no count/remnant bonus.
    // Explicit older policies remain available to their existing callers.
    enum class RankingPolicy { RegularRemnant, LowThenLeft, CompactArea, Utilization };
    // A constructor may spend its budget on a subset of legal orientations.
    // This never changes the original part's mirror permission or validation.
    enum class OrientationProposalPolicy { AllowedReflections, OriginalOnly };
    struct ConstructionTraceEvent;
    struct ConstructionTraceHealth
    {
        std::atomic<bool> Failed{false};
        std::atomic<std::size_t> Omitted{0};
    };
    struct Options
    {
        double TimeLimitSeconds = 5;
        std::size_t MaxPlacementAttempts = 200000;
        std::size_t MaxPatterns = 256; // Additional combination candidates; base specifications/directions are never trimmed by this cap.
        std::size_t MaxPatternMembers = 4;
        bool EnableCombinations = true;
        bool EnableGapFill = true;
        bool EnableCircleLattice = true;
        std::uint64_t Seed = 1; // Available to stochastic strategies; built-in rule strategies are deterministic.
        std::function<bool()> Cancel;
        std::size_t MaxAssemblyMembers = 12; // Independent seam-graph scope; 1..12 original members.
        RankingPolicy Ranking = RankingPolicy::Utilization; // Shared by pipeline acceptance and portfolio selection.
        // RunJob and explicit pipelines reserve time for fixed-angle down/left
        // refinement. Construction keeps the full attempt cap; refinement uses
        // its remaining balance. Disable for isolated constructor/stage work.
        bool EnableBasicCompaction = true;
        double BasicCompactionBudgetFraction = .15;
        bool BasicCompactionXFirst = false;
        // Observation of actual native construction and portfolio decisions.
        // Empty by default. Parallel portfolios serialize delivery; observers
        // must not modify solve state. Callback failure cannot fail a solve.
        std::function<void(const ConstructionTraceEvent&)> ConstructionTrace;
        std::string ConstructionTraceRoute;
        std::string ConstructionTraceScope;
        std::shared_ptr<ConstructionTraceHealth> ConstructionObservation;
        OrientationProposalPolicy OrientationProposals = OrientationProposalPolicy::AllowedReflections;
        // Internal transport of completed candidates from child constructors to
        // their owning portfolio, independent of ConstructionTrace observation.
        // May be called on worker threads: the receiver must synchronize access
        // and copy an immutable snapshot during the call, never retain this
        // reference. It must validate against the unchanged original problem
        // and apply the same final ranking before accepting any candidate.
        std::function<void(const Result&)> PublishConstructionCandidate;
        // Scheduling hint for designated main initial-construction routes to
        // use the shared remaining wall deadline. Attempt totals, cancellation
        // and original-material validation remain unchanged. False preserves
        // the generic portfolio's existing fair local time leases.
        bool ContinuousConstructionTime = false;
    };
    struct Validation
    {
        bool Valid = false;
        bool Complete = false;
        std::vector<std::string> Errors;
        std::size_t PlacedCount = 0;
        double MaterialCost = 0;
        double MaterialArea = 0;
        double SheetArea = 0;
        double UsedEnvelopeArea = 0;
    };
    struct RegularRemnant
    {
        Rect Bounds;
        double Area = 0; // Area of an explicitly verified reusable rectangle (a lower bound).
        double AreaUpperBound = 0; // Optimistic grid bound; not a second reusable rectangle.
        std::size_t Resolution = 0;
        bool Verified = false;
    };
    struct RemnantEvaluation
    {
        std::vector<RegularRemnant> PerLayout;
        std::vector<std::size_t> FinalLayoutIndices; // one material-compatible tail, selected by free material area first
        double LastSheetFreeArea = 0; // Total unoccupied material area on those selected tails; distinct from a usable rectangle.
        double LastSheetRegularArea = 0;
        double LastSheetAreaUpperBound = 0;
        bool Measured = false;
    };

    Classification Classify(const Shape& shape, double tolerance = 1e-6);
    Validation Validate(const Problem& problem, const Result& result);
    RegularRemnant FindRegularRemnant(const Problem& problem, const Layout& layout, std::size_t resolution = 64);
    RemnantEvaluation EvaluateRemnants(const Problem& problem, const Result& result, std::size_t resolution = 64);

    // A factory creates a fresh strategy object for each job. All mutable solve state is job-local.
    class IStrategy
    {
    public:
        virtual ~IStrategy() = default;
        virtual Result Run(const Problem& input, const Options& options) = 0;
        // A composed executor owns its construction/refinement budget itself.
        // RunJob supplies the common executor to ordinary constructors instead.
        // This is a lifecycle capability, independent of an algorithm's ID.
        virtual bool OwnsRefinementLifecycle() const noexcept { return false; }
        // Pure construction wrappers may own their budget/lifecycle without
        // running layout refinement. Initial portfolios use this separate role.
        virtual bool IsPureConstruction() const noexcept { return !OwnsRefinementLifecycle(); }
    };
    using StrategyFactory = std::function<std::unique_ptr<IStrategy>()>;
    class StrategyRegistry
    {
    public:
        void Register(std::string id, StrategyFactory factory);
        std::vector<std::string> IDs() const;
        std::unique_ptr<IStrategy> Create(const std::string& id) const;
    private:
        struct Entry { std::string ID; StrategyFactory Factory; };
        std::vector<Entry> Entries;
    };
    struct Job
    {
        std::string ID;
        std::string StrategyID;
        Problem Input;
        Options Settings;
    };
    struct JobResult
    {
        std::string ID;
        std::string StrategyID;
        Result Output;
        Validation Checked;
        RemnantEvaluation Remnants;
    };
    struct PortfolioOptions
    {
        Options PerJob;
        std::vector<std::string> StrategyIDs; // empty selects all registered strategies
        std::size_t Workers = 1;
        RankingPolicy Ranking = RankingPolicy::Utilization;
    };
    struct PortfolioResult
    {
        std::vector<JobResult> Jobs;
        std::size_t BestJob = static_cast<std::size_t>(-1);
    };

    StrategyRegistry DefaultStrategies();
    JobResult RunJob(const Job& job, const StrategyRegistry& registry);
    PortfolioResult RunPortfolio(const Problem& input, const PortfolioOptions& options,
        const StrategyRegistry& registry);
    PortfolioResult RunPortfolio(const Problem& input, const PortfolioOptions& options = {});
}
