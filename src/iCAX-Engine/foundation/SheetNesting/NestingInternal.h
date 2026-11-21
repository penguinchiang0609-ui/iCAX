#pragma once
#include "SheetNesting.h"
#include <optional>

namespace iCAX::SheetNesting::Detail
{
    std::vector<std::string> CheckProblem(const Problem& problem);
    constexpr double Pi = 3.14159265358979323846;
    // Read-only material contour copy. Coalesce exactly equal consecutive
    // points and redundant closing points while retaining every distinct point.
    std::vector<Point> MaterialRing(const std::vector<Point>& source);
    // Private reflected geometry for proposal/cache preparation; source and ring permissions are unchanged.
    Shape ReflectedShape(const Shape& source);
    Point Transform(Point p, Pose pose);
    Point InverseTransform(Point p, Pose pose);
    // Apply child first, then parent, retaining reflection parity.
    Pose ComposePose(Pose parent, Pose child);
    Pose InversePose(Pose pose);
    double NormalizeAngle(double degrees);
    bool AngleAllowed(const Part& part, double degrees, double tolerance);
    bool PoseAllowed(const Part& part, Pose pose, double tolerance);
    std::vector<double> CandidateAngles(const Part& part, double tolerance);
    // Zero-translation legal poses; policy limits proposals, never permission.
    // Allowed reflected directions are interleaved with original directions.
    std::vector<Pose> CandidateOrientations(const Part& part, double tolerance,
        OrientationProposalPolicy policy = OrientationProposalPolicy::AllowedReflections);
    Rect Bounds(const Shape& shape, Pose pose = {});
    double Area(const Shape& shape);
    bool ShapeValid(const Shape& shape, double tolerance, std::string& error);
    bool ShapesConflict(const Shape& a, Pose pa, const Shape& b, Pose pb, double gap, double tolerance,
        bool allowCommonEdge = false);
    bool ShapesHaveCommonEdge(const Shape& a, Pose pa, const Shape& b, Pose pb, double tolerance);
    bool InsideSheet(const Shape& shape, Pose pose, double width, double height, double margin, double tolerance);

    struct Member { std::size_t PartIndex = 0; Pose Transform; };
    // A placeable combination contains instances of one original type only.
    // Distinct originals remain distinct even when their contours are equal.
    inline bool HasSinglePartType(const std::vector<Member>& members) noexcept
    {
        if (members.empty()) return false;
        const auto part = members.front().PartIndex;
        for (const auto& member : members) if (member.PartIndex != part) return false;
        return true;
    }
    struct Pattern
    {
        std::vector<Member> Members;
        double Width = 0, Height = 0, MaterialArea = 0;
        ShapeKind Kind = ShapeKind::Irregular;
    };
    void NormalizePattern(Pattern& pattern, const Problem& problem);
    std::vector<Pattern> BuildPatterns(const Problem& problem, const Options& options,
        const std::function<bool()>& stop, std::size_t* attemptsUsed = nullptr);
    std::vector<Pattern> BuildClosurePatterns(const Problem& problem, const Options& options,
        const std::function<bool()>& stop);
    std::vector<Pattern> BuildComplementChains(const Problem& problem, const Options& options,
        const std::function<bool()>& stop);

    enum class RectScore { ShortSide, Area, LongSide, BottomLeft };
    Result RunRectangleOrdered(const Problem& problem, const Options& options, RectScore score);
    Result RunRectangleSkyline(const Problem& problem, const Options& options);
    Result RunEnvelopeSkyline(const Problem& problem, const Options& options);
    enum class CavityOrder { Area, Width, Height };
    Result RunRectangleCavity(const Problem& problem, const Options& options, CavityOrder order = CavityOrder::Area, bool columns = false);
    Result RunRectanglePinwheel(const Problem& problem, const Options& options);
    enum class BlockRootPolicy { RoleOrder, InventoryMrv };
    Result RunRectangleBlockLibrary(const Problem& problem, const Options& options,
        BlockRootPolicy rootPolicy = BlockRootPolicy::RoleOrder);
    enum class BlockRolloutPolicy {Score, Diverse};
    Result RunRectangleBlockRollout(const Problem& problem, const Options& options,
        BlockRolloutPolicy policy=BlockRolloutPolicy::Score);
    Result RunRectangleBlockResidual(const Problem& problem,const Options& options);
    Result RunRectangleBlockRepair(const Problem& problem,const Options& options);
    enum class CircleOrder { LargeFirst, RemainingArea, Interleave, SquareThenHoles };
    enum class CombinationMode { Contacts, CornerChains, Closure };
    enum class PatternOrder { DenseAnchor, AnchorFit, BestFit };
    enum class PatternBatchMode { Disabled, OnStall, AfterBatch, AreaFirst };
    Result RunRules(const Problem& problem, const Options& options, RectScore score,
        bool combinations, bool circleLattice, double preparationShare = .75, bool batchHoles = false,
        CombinationMode mode = CombinationMode::Contacts, PatternOrder order = PatternOrder::DenseAnchor,
        PatternBatchMode batches = PatternBatchMode::Disabled);
    Result RunCircleRules(const Problem& problem, const Options& options, CircleOrder order);
    Result RunCircleShelves(const Problem& problem, const Options& options);
    Result RunSeamAssembly(const Problem& problem, const Options& options);
    Result RunSeamAssemblyIf(const Problem& problem, const Options& options,
        const std::function<bool(const Result&)>& accept);
    Result RunRectangleMerge(const Problem& problem, const Options& options);
    Result RunRectangleLookahead(const Problem& problem, const Options& options);
    Result RunMixedAssemblyRules(const Problem& problem, const Options& options);
    Result RunParallelogramRules(const Problem& problem, const Options& options);
    Result RunParallelogramShelves(const Problem& problem, const Options& options);
    Result RunRectangleShelves(const Problem& problem, const Options& options, bool columns);
    Result RunRectanglePatternShelves(const Problem& problem, const Options& options, bool columns);
    Result RunStripRules(const Problem& problem, const Options& options, bool narrowWidth);
    enum class ContactOrder { LargeFirst, BalancedSpecifications, LowThenLeft, ScarceCandidates, PreserveRemnant, BalancedRemnant };
    Result RunContactRules(const Problem& problem, const Options& options, ContactOrder order);
    struct RectFit { Rect Box; double Primary = 0, Secondary = 0; };
    class MaxRects
    {
    public:
        MaxRects(double width, double height, double tolerance);
        std::optional<RectFit> Find(double width, double height, RectScore score) const;
        void Occupy(const Rect& used);
        const std::vector<Rect>& FreeRectangles() const { return Free; }
    private:
        std::vector<Rect> Free;
        double Tolerance;
    };
}
