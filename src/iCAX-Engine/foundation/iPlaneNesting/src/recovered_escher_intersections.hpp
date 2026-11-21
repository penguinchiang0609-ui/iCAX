#pragma once
#include "recovered_escher_directions.hpp"
#include "recovered_convolution_filter.hpp"
namespace study::recovered {
// 81f0b0 point projection. The caller has already established intersection.
std::vector<EscherIntegerPoint> escherPairIntersectionDirections(const ConvolutionIntegerSegment &,const ConvolutionIntegerSegment &);
std::vector<ConvolutionIntegerSegment> escherIndexBoxSegments(const ConvolutionEdgeIndex &,ConvolutionIntegerBox); // 81f560 / 8204b0
std::vector<EscherIntegerPoint> escherNfpIntersectionDirections(const ConvolutionEdgeIndex &,EscherIntegerPoint); // 81edc0 / 81fc10 point projection, before 77db70 sorting/unique
}
