#pragma once
#include "recovered_boolean_path.hpp"
#include "recovered_convolution_topology.hpp"
#include <optional>
#include <span>

namespace study::recovered {
// Stages of 7e4470 for straight-curve Boolean graphs.
struct BooleanRebuiltPath {
    int32_t firstEdge = -1, lastEdge = -1;
    std::array<double, 4> bounds{};
    double signedArea = 0, length = 0;
    bool positive = false;
};
struct BooleanBoundaryEvent {
    int32_t edge = -1;
    bool outgoing = false;
};
using BooleanEdgePair = std::array<int32_t, 2>;
struct BooleanArrangementGroup {
    std::vector<int32_t> vertices;
    std::vector<BooleanBoundaryEvent> events;
    double tolerance = 0;
    uint64_t identity = 0;
};
struct BooleanRebuiltCollection {
    std::vector<BooleanRebuiltPath> paths;
    // Deleted graph objects retain their payload as snapshots. Consumers use
    // the surviving group vertices and path anchors, not these retired indices.
    std::vector<int32_t> removedEdges, removedVertices;
};

// 7e9df0 / 7def50. parameter is written only after the infinite-line test passes,
// even if the subsequent segment bounds or endpoint exclusion test fails.
bool booleanStraightEdgeContainsPoint(const ConvolutionCurveSegment &, ShakerPoint, double tolerance,
                                      double &parameter);
double booleanStraightEdgePointParameter(const ConvolutionCurveSegment &, ShakerPoint); // 7df210
std::vector<std::array<double, 2>> booleanStraightEdgeIntersections(const ConvolutionCurveSegment &,
                                                                    const ConvolutionCurveSegment &,
                                                                    double tolerance); // 7e5340
bool booleanStraightEdgesIntersect(const ConvolutionCurveSegment &, const ConvolutionCurveSegment &,
                                   double tolerance); // 7e5ff0
struct BooleanInputSweepState {
    uint64_t sequence = 0;
    int32_t active = 0, maximum = 0;
    uint32_t events = 0, comparisons = 0;
};
struct BooleanInputArrangement {
    std::vector<BooleanArrangementGroup> groups;
    std::vector<int32_t> removedEdges;
    BooleanInputSweepState state;
};
class ConvolutionCurves;
// 7eb800 / 7ec9c0 and their callbacks. Inputs are disjoint topology paths with
// unset group metadata and finite positive tolerance. Splitting shares curves.
// Input path anchors become obsolete after this destructive stage. New group
// identities are one-based indices and can be rebound before hierarchy hashing.
BooleanInputArrangement booleanProduceStraightGroups(ConvolutionTopology &,
                                                     std::span<const BooleanEdgePair> paths,
                                                     double tolerance, ConvolutionCurves &,
                                                     BooleanInputSweepState initial = {});

// 7df5a0. Adds the point-to-line angular interval to the two accumulators.
// The isolated original oracle adapts atan2 to the same host CRT.
std::array<double, 2> booleanPointLineAngles(const ConvolutionCurveSegment &, ShakerPoint,
                                             double tolerance, std::array<double, 2> accumulated = {});
bool booleanStraightPathContainsPoint(const ConvolutionTopology &, int32_t first, int32_t last,
                                      ShakerPoint); // 7d43d0
ShakerPoint booleanStraightPathInteriorPoint(const ConvolutionTopology &, int32_t first,
                                             int32_t last); // 7d4510
bool booleanBoundarySourceLess(const ConvolutionTopology &, BooleanBoundaryEvent,
                               BooleanBoundaryEvent); // 7ea6a0
double booleanBoundaryRelativeAngle(const ConvolutionTopology &, BooleanBoundaryEvent,
                                    BooleanBoundaryEvent); // 7ea4b0
bool booleanBoundaryCoincidentLess(const ConvolutionTopology &, BooleanBoundaryEvent,
                                   BooleanBoundaryEvent, double tolerance); // 7ea6f0
bool booleanBoundaryBetween(const ConvolutionTopology &, BooleanBoundaryEvent candidate,
                            BooleanBoundaryEvent before, BooleanBoundaryEvent after,
                            double tolerance); // 7ea8c0
using BooleanBoundaryGroups = std::vector<std::vector<BooleanBoundaryEvent>>;
void booleanInsertBoundaryEvent(const ConvolutionTopology &, BooleanBoundaryGroups &,
                                BooleanBoundaryEvent, double tolerance); // 7eab50
std::vector<BooleanBoundaryEvent> booleanMergeBoundaryGroups(const ConvolutionTopology &,
                                                             BooleanBoundaryGroups, double tolerance);
// 7eb010 and 7e7230. Returns the exact retirement order in the collection's
// edge/vertex arrays. No paths are extracted by these preparation stages.
BooleanRebuiltCollection booleanSortBoundaryEvents(ConvolutionTopology &, BooleanArrangementGroup &);
BooleanRebuiltCollection booleanPrepareBoundaryEvents(ConvolutionTopology &,
                                                      std::span<BooleanArrangementGroup>);

double booleanStraightPathArea(const ConvolutionTopology &, int32_t first, int32_t last);   // 7d4870
double booleanStraightPathLength(const ConvolutionTopology &, int32_t first, int32_t last); // 7d47c0
BooleanEdgePair booleanPrepareCircularPath(ConvolutionTopology &, BooleanEdgePair,
                                           ConvolutionCurves &); // 7d41a0
// 7e76a0: marks visited vertices (+55) and sets each edge's owner (+d0).
// ownerIdentity is an opaque nonzero token, independent of curve identity.
std::optional<BooleanRebuiltPath> booleanExtractStraightPath(ConvolutionTopology &, int32_t vertex,
                                                             uint64_t ownerIdentity);
// 7ea400 / 7eb400. Uses curve-evaluated midpoints, never topology averages.
bool booleanStraightEdgesCoincide(const ConvolutionTopology &, int32_t, int32_t, double tolerance);
void booleanJoinBoundaryPair(ConvolutionTopology &, BooleanBoundaryEvent, BooleanBoundaryEvent,
                             double tolerance);
// 7e7410's cyclic pair/removal order. The input events remain unchanged.
void booleanJoinBoundaryEvents(ConvolutionTopology &, std::span<const BooleanBoundaryEvent>,
                               double tolerance);
// 7e7950: initializes visit flags for every group before extracting any path,
// then disconnects invalid edges and removes isolated group vertices in order.
BooleanRebuiltCollection booleanCollectStraightPaths(ConvolutionTopology &,
                                                     std::span<BooleanArrangementGroup>);
// 7e7d00 / 7e7ec0 / 7e8110. Preserves +e4 markers in opaque edge metadata;
// a fully retraced closed path clears positive without changing signedArea.
std::vector<BooleanEdgePair> booleanScanRetracedEdges(ConvolutionTopology &, const BooleanRebuiltPath &,
                                                      double tolerance);
void booleanMarkRetracedEdges(ConvolutionTopology &, std::span<const BooleanEdgePair>, double tolerance);
void booleanClassifyRetracedPath(ConvolutionTopology &, BooleanRebuiltPath &, double tolerance);

struct BooleanAssemblyNode {
    std::shared_ptr<ConvolutionTopology> path;
    bool positive = false;
    int32_t parent = -1;
    std::vector<int32_t> children;
    int32_t part = -1;
};
struct BooleanContainmentNode {
    BooleanRebuiltPath path;
    // Matches edge metadata[7]. Hash iteration uses the original opaque identity.
    uint64_t identity = 0;
    int32_t parent = -1;
    std::vector<int32_t> children;
    int32_t descendants = -1;
};
// 7e8260. Resolves parent candidates from cyclic boundary events. A conflicting
// or ambiguous parent returns false, retaining earlier assignments as original.
bool booleanResolvePathHierarchy(const ConvolutionTopology &, std::span<const BooleanArrangementGroup>,
                                 std::span<BooleanContainmentNode>);
struct BooleanHierarchySweepState {
    uint64_t sequence = 0;
    int32_t active = 0, maximum = 0;
};
// 7e9590 and its queue/list callbacks. Group identities match vertex metadata[4].
// sequence models the shared uint64 event counter, including signed tie ordering.
BooleanHierarchySweepState booleanResolveDetachedHierarchy(const ConvolutionTopology &,
                                                           std::span<const BooleanArrangementGroup>,
                                                           std::span<BooleanContainmentNode>,
                                                           double tolerance,
                                                           BooleanHierarchySweepState initial = {});
struct BooleanAssemblyResult {
    std::vector<BooleanStraightPart> parts;
    std::vector<int32_t> discarded;
};
// 7e9a60 / 7e9b20. Hierarchy and orientation come from the preceding stages.
// The winding counter has int32 wrap semantics and accepts any initial value.
BooleanAssemblyResult booleanAssembleParts(std::span<BooleanAssemblyNode>, int32_t initialWinding);
// Executes the recovered straight stages in 7e4470 order. Input is copied,
// output paths own their topology and share immutable source lines. reverse
// applies the original input/output reversal and orientation inversion.
// A containment conflict returns nullopt. An accepted empty result is engaged.
std::optional<std::vector<BooleanStraightPart>>
booleanArrangeStraightPaths(ConvolutionTopology, std::span<const BooleanEdgePair>, double tolerance,
                            ConvolutionCurves &, int32_t initialWinding = 0, bool reverse = false,
                            bool closedOnly = false);
// 7d3b00 / 7d9a00. Bounds include cached curve bounds and topology vertices.
std::array<double, 4> booleanPathBounds(const ConvolutionTopology &, BooleanEdgePair);
double booleanInputPathsScale(const ConvolutionTopology &, std::span<const BooleanEdgePair>);
// 7e31b0 / 7e3500 with the 7d9cd0 predicate. Adjacent segments must share
// their original curve object and both source-classification words (+a0/+a4).
void booleanMergeStraightPathSegments(ConvolutionTopology &, ConvolutionCurves &);
// 7d9d10, straight input branch. Zero tolerance selects scale * 1e-7.
std::optional<std::vector<BooleanStraightPart>>
booleanArrangeStraightParts(ConvolutionTopology, std::span<const BooleanEdgePair>, double tolerance,
                            ConvolutionCurves &, int32_t initialWinding = 0, bool reverse = false,
                            bool closedOnly = false);
// 7da130. Missing lists retain the original null-list shortcuts. Present empty
// lists still enter arrangement. Inputs are copied before destructive geometry
// processing, so caller-owned paths remain usable on both success and failure.
std::optional<std::vector<BooleanStraightPart>>
booleanDifferenceStraightParts(std::optional<std::vector<BooleanStraightPart>> first,
                               std::optional<std::vector<BooleanStraightPart>> second,
                               double tolerance, ConvolutionCurves &, bool reverse = false);
} // namespace study::recovered
