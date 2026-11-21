#pragma once
#include "recovered_convolution_line.hpp"
namespace study::recovered {
// 7e2e90. Tests execute the original append/closure/curve
// factory as well as the cleanup itself. The cutoff uses curve endpoints, while
// replacement geometry is built from topological vertices. These may differ.
ConvolutionTopology booleanGlueStraightPath(const ConvolutionTopology &,double cutoff,
    const ConvolutionCurveFactory &, const ConvolutionArcFactory & = {});
// 7e30f0: replace geometry in the existing path object, preserving its identity.
void booleanCleanStraightPath(ConvolutionTopology &,double cutoff,const ConvolutionCurveFactory &,
                             const ConvolutionArcFactory & = {});
struct BooleanStraightPart {
    std::shared_ptr<ConvolutionTopology> outer;
    std::vector<std::shared_ptr<ConvolutionTopology>> holes;
};
// 7d9540: existing part/curve order, outer before inner paths.
void booleanFinishStraightParts(std::span<BooleanStraightPart>,double cutoff,const ConvolutionCurveFactory &,
                               const ConvolutionArcFactory & = {});
}
