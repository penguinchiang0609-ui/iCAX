#pragma once
#include "recovered_escher_integer.hpp"
#include "recovered_convolution_integer.hpp"

namespace study::recovered {
void escherRotateIntegerRing(ConvolutionIntegerRing &); // 819260
// 81a190 after the caller's lexicographic point sort. At least two points.
ConvolutionIntegerRing escherIntegerHull(std::span<const ConvolutionIntegerPoint> sorted);
// 8198b0: the original one-pass concavity/area simplification, not a generic
// distance-based polygon simplifier. Input has already passed 819400 cleanup.
void escherSimplifyIntegerRing(ConvolutionIntegerRing &, double allowedArea);
// 801f10: normalize one ring through the integer polygon-set Boolean sweep.
// Multiple output components are rejected and leave the input unchanged.
bool escherRepairIntegerRing(ConvolutionIntegerRing &);
// 84f950 checks the split edge population and duplicate undirected edges.
bool escherIntegerRingsNeedUnion(const ConvolutionIntegerPolygon &);
void escherUnionIntegerRings(ConvolutionIntegerPolygon &); // 802260
ConvolutionIntegerPolygon escherDifferenceIntegerRings(
    const ConvolutionIntegerPolygon &, const ConvolutionIntegerPolygon &); // 803860
}
