#include "recovered_shaker_mobility.hpp"
#include <bit>
#include <immintrin.h>
#include <numbers>
namespace study::recovered {
namespace {
template<class T> const std::array<T,2> *containing(const std::vector<std::array<T,2>> &ranges,T current){
    for(const auto &r:ranges){if(current<r[0])break;if(current<=r[1])return &r;}return nullptr;
}
int32_t quantizeY(double value){
    auto scaled=_mm_mul_sd(_mm_set_sd(value),_mm_set_sd(1000));
    scaled=_mm_cvtsd_f64(scaled)<0?_mm_sub_sd(scaled,_mm_set_sd(.5)):_mm_add_sd(scaled,_mm_set_sd(.5));
    return _mm_cvttsd_si32(scaled);
}
int32_t orientationMagnitude(int32_t angle){
    const auto magnitude=std::bit_cast<int32_t>(angle<0?uint32_t(0)-uint32_t(angle):uint32_t(angle));
    return magnitude>=36000000?magnitude-36000000:magnitude;
}
}
std::vector<ShakerPoint> shakerShapeHull(std::span<const ShakerSdkPolygon> polygons) {
    std::vector<ShakerPoint> points;
    for (const auto &polygon : polygons)
        if (!(polygon.classification <= 0))
            points.insert(points.begin(), polygon.points.begin(), polygon.points.end());
    for (const auto &point : points)
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]))
            throw std::invalid_argument("Shape hull requires finite coordinates");
    std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) {
        return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]);
    });
    std::vector<ShakerPoint> hull;
    const auto append = [&](ShakerPoint point, size_t minimum) {
        while (hull.size() >= minimum) {
            const auto &a = hull[hull.size() - 2], &b = hull.back();
            const double cross = (b[0] - a[0]) * (point[1] - a[1]) -
                                 (b[1] - a[1]) * (point[0] - a[0]);
            if (cross > 0)
                break;
            hull.pop_back();
        }
        hull.push_back(point);
    };
    for (const auto &point : points)
        append(point, 2);
    const size_t upperMinimum = hull.size() + 1;
    for (size_t index = points.size(); index > 1; --index)
        append(points[index - 2], upperMinimum);
    if (hull.size() > 1)
        hull.pop_back();
    return hull;
}
bool shakerGeometryLocksRotation(ShakerRotationMetrics &metrics,
                                std::span<const ShakerSdkPolygon> polygons, double tolerance) {
    if (!metrics.valid) {
        const auto hull = shakerShapeHull(polygons);
        double area = 0, perimeter = 0;
        if (!hull.empty()) {
            auto previous = hull.back();
            for (const auto &point : hull) {
                area += (previous[0] * point[1] - previous[1] * point[0]) * .5;
                const double dx = point[0] - previous[0], dy = point[1] - previous[1];
                perimeter += std::sqrt(dy * dy + dx * dx);
                previous = point;
            }
        }
        metrics.area = area;
        metrics.perimeter = perimeter;
        metrics.valid = true;
    }
    return std::abs(metrics.area / ((metrics.perimeter * metrics.perimeter) /
                                  (4. * std::numbers::pi)) - 1.) < tolerance;
}
bool shakerAllowedX(uint8_t &locked,std::array<int64_t,2> &bounds,int64_t current,bool forced,std::span<const ShakerAxisConstraint<int64_t>> constraints,const ShakerMobilityDiagnostic &diagnostic){
    if(forced){locked=1;return true;}
    std::vector<std::array<int64_t,2>> ranges{bounds};
    for(const auto &c:constraints)if(!c.optional){c.apply(ranges);if(ranges.empty())break;}
    if(locked)return false;const auto selected=containing(ranges,current);if(diagnostic)diagnostic(selected!=nullptr);
    if(!selected){locked=1;return true;}
    const bool changed=bounds!=*selected;bounds=*selected;return changed;
}
bool shakerAllowedY(uint8_t &locked,std::array<double,2> &bounds,int32_t current,bool forced,std::span<const ShakerAxisConstraint<int32_t>> constraints,const ShakerMobilityDiagnostic &diagnostic){
    if(forced){locked=1;return true;}
    const std::array<int32_t,2> original{quantizeY(bounds[0]),quantizeY(bounds[1])};auto result=original;
    std::vector<std::array<int32_t,2>> ranges{original};
    for(const auto &c:constraints){c.apply(ranges);if(ranges.empty())break;}
    if(ranges.empty()){ranges={original};for(const auto &c:constraints)if(!c.optional){c.apply(ranges);if(ranges.empty())break;}}
    bool changed=false;
    if(!locked){const auto selected=containing(ranges,current);if(diagnostic)diagnostic(selected!=nullptr);
        if(!selected){locked=1;changed=true;}else{result=*selected;changed=result!=original;}}
    for(size_t k=0;k<2;++k)bounds[k]=_mm_cvtsd_f64(_mm_div_sd(_mm_set_sd(double(result[k])),_mm_set_sd(1000)));
    return changed;
}
std::array<int32_t,2> shakerAllowedAngle(uint8_t &locked,bool rotate,int32_t current,const ShakerAngleProvider &provider){
    const bool geometryLock=provider.geometryLocksRotation();
    const bool disallowed=!rotate||provider.constraintsLockRotation()||geometryLock;locked|=uint8_t(disallowed);
    if(locked){const auto value=orientationMagnitude(current);return {value,value};}
    const auto match=provider.equivalentOrientation();const auto angle=orientationMagnitude(match.angle);
    const int32_t lower=match.angle<0?match.field4:match.field8,upper=match.angle<0?match.field8:match.field4;
    return {std::bit_cast<int32_t>(uint32_t(angle)-uint32_t(lower)),std::bit_cast<int32_t>(uint32_t(angle)+uint32_t(upper))};
}
}
