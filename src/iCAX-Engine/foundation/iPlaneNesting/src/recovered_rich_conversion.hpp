#pragma once
#include "recovered_rich_traversal.hpp"
#include "recovered_boolean_path.hpp"

namespace study::recovered {
struct RichGraph {
    std::vector<std::unique_ptr<RichNode>> nodes;
    RichNode &add(RichKind);
};
RichNode &richStartCurve(RichGraph &,ShakerPoint,double tolerance);
void richAttach(RichNode &,RichNode *next); // b7e8a0
RichNode *richAppendLine(RichGraph &,RichNode &curve,ShakerPoint); // b82d40 / b82530
RichNode *richAppendArc(RichGraph &, RichNode &curve, ShakerPoint, ShakerPoint center, bool positive); // b82e80 / b79b80
bool richClose(RichNode &curve); // b81ea0
void richReverse(RichNode &curve); // b820a0 / b85420 / b7e740
bool richSetOuter(RichNode &polygon,RichNode *curve); // b8bb80
void richAppendHole(RichNode &polygon,RichNode *curve); // b8bc00
// bedb70. Uses evaluated Boolean endpoints, rich line/circle construction and tolerance,
// and source attributes when the optional registry contains the edge's label.
RichNode *booleanStraightPathToRich(RichGraph &,const ConvolutionTopology *,double tolerance,
                                  const RichSourceAttributes * = nullptr);
RichNode &booleanStraightPartsToRich(RichGraph &,std::span<const BooleanStraightPart>,double tolerance,
                                   const RichSourceAttributes * = nullptr); // bee5a0 / bee940
// bed330 / beda40 / bedae0. The registry spans all paths in a tree operation.
// The original discards segments of parameter length <= 1e-6 before appending.
std::shared_ptr<ConvolutionTopology> richStraightPathToBoolean(const RichNode &, ConvolutionCurves &,
                                                             RichSourceAttributes * = nullptr);
std::vector<BooleanStraightPart> richStraightTreeToBoolean(const RichNode &, ConvolutionCurves &,
                                                          RichSourceAttributes * = nullptr);
// bec030 with the recovered line/circle conversion, destructive difference, result
// cleanup and rich reconstruction stages. A null part result becomes an empty root.
RichNode &richDifferenceStraightTrees(RichGraph &, const RichNode &, const RichNode &,
                                     double tolerance, ConvolutionCurves &);
struct RichBounds {
    bool empty = true;
    std::array<double, 4> coordinates{};
};
RichBounds richStraightBounds(const RichNode &);
// 712a80 / 712bb0. Uses the SDK's supplied outer/hole order and rich closure.
RichNode &shakerBuildRichPolygonInput(RichGraph &, const ShakerPolygonInput &);
// 713380. Each selected hole height generates a closed, doubled horizontal
// segment. The original Boolean difference then opens the hole into components.
RichNode &shakerBuildHoleCuts(RichGraph &, const RichNode &input);
// 7128d0, including rich geometry creation, hole cuts, Boolean difference,
// result traversal and existing 712780 component normalization.
size_t shakerAppendPolygonComponents(ShakerInputScene &, size_t shape,
                                     const ShakerPolygonInput &, ConvolutionCurves &);
}
