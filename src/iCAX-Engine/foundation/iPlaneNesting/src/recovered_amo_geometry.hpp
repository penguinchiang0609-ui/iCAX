#pragma once
#include "recovered_amo_convolution.hpp"
#include "recovered_amo_shape.hpp"
namespace study::recovered {
ConvolutionTopology amoTilingBooleanRing(std::span<const Point>,const AmoTilingMatrix &,ConvolutionCurves &); //8c0590
std::vector<BooleanStraightPart> amoTilingBooleanShape(const AmoTilingShape &,const AmoTilingMatrix &,ConvolutionCurves &); //8c0670
// Point and end callbacks 8c08b0 / 8c0b20. Source points are the visited
// straight edges' first evaluated endpoints, without a repeated closing point.
std::vector<Point> amoTilingSampleContour(std::span<const Point>,double threshold,bool reverse=false);
void amoTilingBuildSimplifiedMap(AmoTilingContourMap &,ConvolutionCurves &); //8c0e50
void amoTilingBuildDetailedMap(AmoTilingContourMap &,ConvolutionCurves &); //8c1090
AmoTilingMapGeometry amoTilingFloatingMapGeometry();
}
