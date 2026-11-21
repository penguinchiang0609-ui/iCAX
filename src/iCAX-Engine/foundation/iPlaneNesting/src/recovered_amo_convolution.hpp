#pragma once
#include "recovered_amo_map.hpp"
#include "recovered_boolean_arrangement.hpp"
namespace study::recovered {
// The floating 7e1080 -> 7d4d90/7d51d0/7d55b0/7d4f00 chain.
// Inputs are closed straight-curve paths. This is the raw path convolution,
// before the 7e4b70 strip construction and Boolean arrangement used by shapes.
std::vector<ConvolutionTopology> amoConvolveStraightPaths(const ConvolutionTopology &,
    const ConvolutionTopology &,ConvolutionCurves &);
// 7e4b70: 100-strip raster construction followed by the original reversed
// Boolean arrangement. A disengaged result preserves an arrangement conflict.
std::optional<std::vector<BooleanStraightPart>> amoFinishConvolution(
    std::span<const ConvolutionTopology>,ConvolutionCurves &);
// 7dadb0: outer/outer, outer/holes, holes/outer; the raw path list is shared.
std::optional<std::vector<BooleanStraightPart>> amoConvolveStraightParts(
    std::span<const BooleanStraightPart>,std::span<const BooleanStraightPart>,ConvolutionCurves &);
}
