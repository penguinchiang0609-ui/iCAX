#pragma once
#include "recovered_convolution_cache.hpp"
#include "recovered_convolution_polygon.hpp"
#include <map>
#include <optional>
namespace study::recovered {
struct ConvolutionTransform {bool mirror=false;int32_t angle=0;bool operator==(const ConvolutionTransform &)const=default;};
struct ConvolutionReducedAngle {ConvolutionTransform pose;int32_t turns=0;};
struct ConvolutionDiophantine {int32_t first=0,second=0;bool valid=false;};
bool convolutionTransformLess(ConvolutionTransform,ConvolutionTransform); // 8661b0
ConvolutionReducedAngle convolutionReduceAngle(ConvolutionTransform,int32_t order); // 865600
ConvolutionDiophantine convolutionDiophantine(int32_t,int32_t,int32_t); // 866530
using ConvolutionSymmetryPair=std::array<ConvolutionTransform,2>;
struct ConvolutionSymmetryGroup {
    int32_t order = 1;
    int32_t reflectionAngle = 0;
    bool reflection = false;
};
ConvolutionSymmetryGroup convolutionGroupFromTransforms(std::span<const ConvolutionTransform>); // 863ea0
std::optional<ConvolutionSymmetryPair> convolutionMatchRotations(
    ConvolutionTransform, int32_t firstOrder, int32_t secondOrder,
    double tolerance = 5.555555555555555e-7); // 864680
// 721e40 supplies order one. The group operations also accept intrinsic cyclic
// and reflection groups with the original descriptor's supported orders 1..360.
ConvolutionSymmetryPair convolutionCanonicalPair(const ConvolutionSymmetry &,const ConvolutionSymmetry &); // 863ac0
std::vector<ConvolutionSymmetryPair> convolutionSymmetryCandidates(const ConvolutionSymmetry &,const ConvolutionSymmetry &); // 866220
class ConvolutionSymmetryCache {
public:
    using Geometry=std::shared_ptr<const ConvolutionGeometry>;
    using Polygon=std::shared_ptr<const ConvolutionPolygon>;
    struct Hit {ConvolutionTransform transform;Polygon polygon;};
    std::optional<Hit> get(const ConvolutionSymmetry &,const ConvolutionSymmetry &,Geometry,Geometry)const; // 8670d0
    void put(ConvolutionTransform,Polygon,Geometry,Geometry); // 867c40
    size_t size()const;
private:
    struct Less {bool operator()(ConvolutionTransform a,ConvolutionTransform b)const{return convolutionTransformLess(a,b);}};
    using Poses=std::map<ConvolutionTransform,Polygon,Less>;
    using Key=std::pair<int64_t,int64_t>;
    mutable std::shared_mutex mutex_;
    std::map<Key,Poses> entries_;
    std::optional<Hit> ordered(const ConvolutionSymmetry &,const ConvolutionSymmetry &,const Key &)const; // 867380
};
ConvolutionPolygon convolutionTransformCachedPolygon(const ConvolutionPolygon &,ConvolutionTransform); // 7227db
}
