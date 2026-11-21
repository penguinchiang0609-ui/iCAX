#pragma once
#include "recovered_boolean_arrangement.hpp"

namespace study::recovered {
enum class FloatingOffsetJoin { Round = 0, Bisector = 1, Regular = 2 };
// 7e18c0 / 7e19b0 / 7e1d40. Geometry stays in double precision; the
// resulting arc uses the original signed-radius constructor and curve IDs.
void floatingOffsetInsertAngle(ConvolutionTopology &, ConvolutionCurves &,
    FloatingOffsetJoin, double radius, double maximumAngle,
    ShakerPoint vertex, ShakerPoint previousPoint, ShakerPoint previousNormal,
    ShakerPoint nextPoint, ShakerPoint nextNormal, uint32_t vertexSource = 0xffffffffU,
    bool informVertex = true);
double floatingOffsetVertexAngle(const ConvolutionTopology &, int32_t vertex); // 7e2680
// 7e2850 -> 7e1f80, including the closed-path inset gates. These are the
// generated paths before arrangement and the 7db900 finishing stage.
std::vector<ConvolutionTopology> floatingOffsetPath(const ConvolutionTopology &,
    ConvolutionCurves &, FloatingOffsetJoin, double radius, double maximumAngle,
    double minimumLength, bool outer, bool informVertex = true);
// 7e3bc0 / 7e3940 and the part-order wrapper 7db900.
bool floatingOffsetSimplifyPath(ConvolutionTopology &, ConvolutionCurves &,
    double tolerance, double maximumLength = 1e20);
void floatingOffsetSplitLargeArcs(ConvolutionTopology &, ConvolutionCurves &);
void floatingOffsetFinishParts(std::vector<BooleanStraightPart> &,ConvolutionCurves &,
    double tolerance,double maximumLength=1e20);
// 7dbe40: raw offset, arrangement, simplification, arc splitting, zero-cutoff
// path cleanup. Nullopt preserves a Boolean hierarchy conflict.
std::optional<std::vector<BooleanStraightPart>> floatingOffsetParts(
    std::span<const BooleanStraightPart>,ConvolutionCurves &,FloatingOffsetJoin,
    double radius,double maximumAngle,double tolerance=0.);
}
