#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::SheetNesting
{
    struct Point { double X = 0, Y = 0; };
    struct Pose { double X = 0, Y = 0, AngleDegrees = 0; };
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
    };
    struct Part
    {
        std::string ID;
        Shape Geometry;
        std::size_t Quantity = 1;
        std::string Material;
        bool FreeRotation = true;
        std::vector<double> AllowedAnglesDegrees;
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
    enum class RankingPolicy { RegularRemnant, LowThenLeft, CompactArea };
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
        RankingPolicy Ranking = RankingPolicy::RegularRemnant; // Shared by pipeline acceptance and portfolio selection.
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
        std::vector<std::size_t> FinalLayoutIndices; // one selectable final sheet per compatible material key
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
        RankingPolicy Ranking = RankingPolicy::RegularRemnant;
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
