#pragma once
#include "recovered_escher_boundary_geometry.hpp"
#include <map>
namespace study::recovered {
// Weighted region tree produced by the original fuzzy Boolean construction.
// boundary stores 83c600's directed edge output, with its original order.
struct FuzzyPolygonRegion {
    bool geometryPresent=false;
    std::vector<ConvolutionIntegerSegment> boundary;
    std::vector<std::shared_ptr<FuzzyPolygonRegion>> children;
    double weight=0.;
};
struct FuzzyPolygonPrepared {
    ConvolutionIntegerPolygon polygons;
    std::vector<double> weights;
    std::shared_ptr<FuzzyPolygonRegion> root;
    std::map<EscherIntegerPoint,std::vector<int32_t>> incidentSegments;
    std::vector<std::vector<int32_t>> segmentSources;
    std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> adjacency;
};
struct FuzzyPolygonMaximum {
    const FuzzyPolygonRegion *region=nullptr;
    double weight=0.;
};
FuzzyPolygonMaximum fuzzyPolygonMaximum(const FuzzyPolygonRegion &); //83bea0
EscherIntegerPoint fuzzyPolygonDirectionalPoint(const FuzzyPolygonRegion &,
    EscherIntegerPoint primary,EscherIntegerPoint secondary); //83caf0
std::vector<EscherIntegerPoint> fuzzyPolygonNearbyVertices(
    const std::map<EscherIntegerPoint,std::vector<EscherIntegerPoint>> &,
    EscherIntegerPoint,int32_t depth); //829aa0, original hash iteration order
struct FuzzyPolygonSelection {
    EscherBoundarySampleSelection selection;
    bool success=false;
};
FuzzyPolygonSelection fuzzyPolygonBestPoint(const FuzzyPolygonPrepared &,
    EscherIntegerPoint primary={-1,0},EscherIntegerPoint secondary={0,-1}); //829c70
}
