#pragma once
#include "recovered_shaker_broadphase.hpp"
namespace study::recovered {
struct ShakerGeometryBox {ShakerBox value{};bool empty=true;};
struct ShakerTransform {
    ShakerPoint position{},reference{};
    double angle=0,cosine=1,sine=0;
    std::array<uint8_t,2> mirror{};
    double scale=1;
};
struct ShakerGeometryComponent {
    size_t part=0;
    uintptr_t identity=0;
    std::vector<ShakerPoint> points;
    ShakerGeometryBox bounds;
};
struct ShakerGeometryPart {
    uintptr_t typeIdentity=0;
    ShakerPoint reference{};
    std::vector<size_t> components;
};
struct ShakerSceneGeometry {
    std::vector<ShakerGeometryPart> parts;
    std::vector<ShakerGeometryComponent> components;
    std::array<uintptr_t,2> excludedSpanTypes{};
};
ShakerPoint shakerTransformPoint(ShakerPoint,const ShakerTransform &);
// 714890 / 714a30: original transform order and strict bounding comparisons.
ShakerGeometryBox shakerGeometryBounds(std::span<const ShakerPoint>,const ShakerTransform &);
ShakerGeometryBox shakerProjectedBounds(std::span<const ShakerPoint>,const ShakerTransform &,const std::array<double,4> &);
// 7144f0 / 714030 / 716a10. Components may refer to shared geometry; only
// their world-space bounds and the owning part's validity flag are updated.
void shakerRefreshComponent(std::span<const ShakerPart>,ShakerSceneGeometry &,size_t);
void shakerRefreshPart(std::span<ShakerPart>,ShakerSceneGeometry &,size_t);
void shakerRefreshScene(std::span<ShakerPart>,ShakerSceneGeometry &);
double shakerSceneSpan(std::span<ShakerPart>,ShakerSceneGeometry &); // 716c10
ShakerGeometryBox shakerPartProjection(std::span<const ShakerPart>,const ShakerSceneGeometry &,size_t,ShakerPoint,ShakerPoint); // 7141b0
}
