#pragma once
#include "recovered_rich_conversion.hpp"
namespace study::recovered {
// 8ad220/8ad570 followed by 8ad080. Retains existing line segments and
// discretizes circle subsegments after their native X/Y extremum splits.
RichNode &amoTilingSampleOffsetInputCurve(RichGraph &,const RichNode &,double halfDistance);
// The single positive outer-path domain produced by the sampled input curve:
// 7d9ec0/7d9d10 followed by7d9540 with the original1e-6 cleanup tolerance.
std::optional<std::vector<BooleanStraightPart>> amoTilingUnionOffsetInputPath(
    const ConvolutionTopology &,ConvolutionCurves &);
}
