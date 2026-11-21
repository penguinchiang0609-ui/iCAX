#pragma once
#include "recovered_shaker_input.hpp"

namespace study::recovered {
// SDK records retain the supplied signed-area/classification field. It is not
// recomputed from the vertices by 55a960 (including the unordered-NaN branch).
struct ShakerSdkPolygon {
    std::vector<ShakerPoint> points;
    double classification=0;
};
struct ShakerPolygonInput {
    std::vector<ShakerPoint> outer;
    std::vector<std::vector<ShakerPoint>> holes;
    int32_t type=-1;
    std::optional<double> offset;
};
// The diagnostic is called at each isolated internal ring. The original warning
// sink may return or throw, depending on SDK policy; no implicit repair occurs.
using ShakerInputDiagnostic=std::function<void(size_t)>;
std::vector<ShakerPolygonInput> shakerPreparePolygons(std::span<const ShakerSdkPolygon>,
    int32_t type,std::optional<double> offset,const ShakerInputDiagnostic &); // 55a960 + 70ca10
std::vector<ShakerPoint> shakerCoordinatePolygon(std::span<const ShakerPoint>,bool outer); // 70ca10
std::vector<ShakerPoint> shakerCleanComponentPoints(std::span<const ShakerPoint>); // 7123d0
// Bounding boxes are returned by the rich-geometry provider, in traversal order.
// These levels prepare cutting geometry; they do NOT perform the tree split.
std::vector<double> shakerHoleCutLevels(std::span<const std::array<double,4>>); // 713200
size_t shakerCreateGeometry(ShakerInputScene &,ShakerPoint reference); // 714d90
// Only the visitor's completed polygon is admitted here. Raw outer/hole input
// still requires the separate rich-geometry split/traversal before this call.
size_t shakerAppendVisitedComponent(ShakerInputScene &,size_t shape,int32_t type,
    std::span<const ShakerPoint> visitedPoints,double polygonArea,std::optional<double> offset); // 712780 / 713a50 / 713bb0
}
