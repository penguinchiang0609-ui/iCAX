#pragma once
#include "recovered_placement.hpp"
#include "recovered_shaker_input.hpp"
#include "recovered_shaker_geometry_input.hpp"
namespace study::recovered {
struct ShakerRotationMetrics {
    bool valid = false;
    double area = 0, perimeter = 0;
};
std::vector<ShakerPoint> shakerShapeHull(std::span<const ShakerSdkPolygon>); // 5207e0 / 538b10 / 53af00
bool shakerGeometryLocksRotation(ShakerRotationMetrics &, std::span<const ShakerSdkPolygon>,
                                double tolerance = .01); // 5311f0 / 531320
template<class Integer> struct ShakerAxisConstraint {
    bool optional=false;
    std::function<void(std::vector<std::array<Integer,2>>&)> apply;
};
using ShakerMobilityDiagnostic=std::function<void(bool)>;
// Constraints are supplied in the original catalog order; their individual
// technology-specific implementations are separate providers. Endpoints are
// inclusive. X uses native int64 units; Y uses 1/1000 units and CVTTSD2SI rounding.
bool shakerAllowedX(uint8_t &locked,std::array<int64_t,2> &bounds,int64_t current,
    bool forced,std::span<const ShakerAxisConstraint<int64_t>>,const ShakerMobilityDiagnostic &); // 64e470
bool shakerAllowedY(uint8_t &locked,std::array<double,2> &bounds,int32_t current,
    bool forced,std::span<const ShakerAxisConstraint<int32_t>>,const ShakerMobilityDiagnostic &); // 64e750
struct ShakerAngleProvider {
    std::function<bool()> geometryLocksRotation; // 531320(shape, .01)
    std::function<bool()> constraintsLockRotation; // 38c650
    std::function<PlacementOrientation()> equivalentOrientation; // 38a380 + 527bd0
};
std::array<int32_t,2> shakerAllowedAngle(uint8_t &locked,bool rotate,int32_t current,
    const ShakerAngleProvider &); // 64e1f0; quantized degrees, NOT radians
}
