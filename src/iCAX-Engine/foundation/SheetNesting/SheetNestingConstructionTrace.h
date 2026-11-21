#pragma once
#include "SheetNesting.h"
#include <limits>

namespace iCAX::SheetNesting
{
enum class ConstructionTraceStage
{
    Begin,
    Analysis,
    Match,
    Array,
    Candidate,
    Expand,
    Backtrack,
    Complete,
    Selected,
    Stopped,
    Generate,
    Filter,
    Score,
    Commit,
    Budget,
    Dispatch
};
enum class ConstructionTraceGeometryRole
{
    Original,
    Carrier,
    Recess,
    Protrusion
};
enum class ConstructionTraceCarrierKind
{
    ConvexHull,
    OuterRectangle,
    CoreRectangle
};

struct ConstructionTraceGeometry
{
    ConstructionTraceGeometryRole Role = ConstructionTraceGeometryRole::Original;
    ConstructionTraceCarrierKind CarrierKind = ConstructionTraceCarrierKind::ConvexHull;
    std::size_t CarrierIndex = 0, FeatureIndex = 0;
    // Original source-local coordinates. Polygon winding is preserved.
    std::vector<std::vector<Point>> Rings;
    double Radius = 0;
    Point Center;
};
struct ConstructionTraceMouth
{
    std::size_t CarrierIndex = 0, FeatureIndex = 0;
    Point Start, End, Into;
    double Width = 0, Depth = 0;
};
struct ConstructionTraceMember
{
    std::size_t PartIndex = 0;
    // Preparation proposals have no original instance assigned yet.
    std::size_t Instance = std::numeric_limits<std::size_t>::max();
    Pose Transform;
    std::size_t Group = 0;
};
struct ConstructionTraceEvent
{
    // Route + scope + proposal/candidate IDs identify a native observation.
    // Scope distinguishes independent trials, restarts and board competitions.
    std::string Route, Scope, Source;
    std::vector<std::pair<std::string, double>> Measures;
    // Lexicographic comparison, smaller is better. Names are recorded beside
    // values so neither the viewer nor the writer invents score semantics.
    std::vector<std::pair<std::string, double>> Rank;
    std::vector<Layout> Layouts;
    std::vector<Unplaced> Remaining;
    ConstructionTraceStage Stage = ConstructionTraceStage::Begin;
    std::uint64_t Step = 0, StateID = 0, ParentStateID = 0, ProposalID = 0, CandidateID = 0;
    std::size_t PartIndex = std::numeric_limits<std::size_t>::max();
    std::size_t StockIndex = std::numeric_limits<std::size_t>::max();
    std::size_t StockInstance = 0, BoardIndex = 0;
    double BoardWidth = 0, BoardHeight = 0;
    double Width = 0, Height = 0, MaterialArea = 0;
    // Preparation measures remain separate from board-axis Increment.
    // A zero FootprintArea denotes an unmeasured proposal footprint.
    double Saving = 0, Fill = 0, FootprintArea = 0, Increment = 0;
    double EnvelopeArea = 0, RegularRemnantArea = 0, ElapsedSeconds = 0;
    std::size_t Attempts = 0, ExpandedStates = 0;
    bool FeatureDerived = false, ArrayDerived = false, Accepted = true;
    // Truncated describes omitted observation events. AnalysisTruncated only
    // describes bounded source-feature analysis within a fully recorded event.
    bool Truncated = false, AnalysisTruncated = false;
    // A solver bound omitted proposals/states. Unlike Truncated this says
    // nothing about missing observation records.
    bool SearchTruncated = false;
    Status ResultStatus = Status::Partial;
    std::string Reason;
    std::vector<ConstructionTraceGeometry> Geometries;
    std::vector<ConstructionTraceMouth> Mouths;
    // Match/Array use normalized proposal coordinates and legal absolute
    // angles. Candidate contains only newly added original instances in
    // board coordinates. Accepted candidates have a new StateID and their
    // full state is ParentStateID plus this delta. Rejected candidates have
    // StateID=0, Accepted=false and Reason identifies the checked rejection.
    // Selected contains the full selected single-board placement set.
    std::vector<ConstructionTraceMember> Members;
};
} // namespace iCAX::SheetNesting
