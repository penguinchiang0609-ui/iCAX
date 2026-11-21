#pragma once
#include "alma_study.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
namespace study {
struct Failure : std::runtime_error {
    int code;
    Failure(int c, const std::string &s) : runtime_error(s), code(c) {}
};
inline void require(bool b, int c, const char *s) {
    if (!b)
        throw Failure(c, s);
}
using Point = PN2_Point;
using Polygon = std::vector<Point>;
constexpr double pi = 3.14159265358979323846, tau = 2 * pi;
struct Budget {
    std::chrono::steady_clock::time_point deadline;
    uint64_t work = 0;
    double requestedSeconds;
    explicit Budget(double seconds)
        : deadline(std::chrono::steady_clock::now() +
                   std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                       std::chrono::duration<double>(seconds))), requestedSeconds(seconds) {}
    void check(bool force = false) {
        if ((force || ++work % 128 == 0) && std::chrono::steady_clock::now() >= deadline)
            throw Failure(PN2S_TIMEOUT, "Cooperative time budget expired; no partial result published");
    }
};
struct Box {
    double x0, y0, x1, y1;
};
double area(const Polygon &);
Box bounds(const Polygon &);
void validate_polygon(const Polygon &);
inline int64_t rounded(double x) {
    return static_cast<int64_t>(std::trunc(x + (x < 0 ? -.5 : .5)));
}
inline bool grid_exact(double v, double scale) {
    return std::isfinite(v) && std::abs(v) <= 1e6 &&
           std::abs(static_cast<double>(rounded(v * scale)) - v * scale) <= 1e-6;
}
struct Pose {
    double angle = 0;
    bool flip = false;
    double upper = 0;
    Pose(double a = 0, bool f = false) : angle(a), flip(f), upper(a) {}
    Pose(double a, bool f, double hi) : angle(a), flip(f), upper(hi) {}
};
Polygon transform(const Polygon &, Point pivot, Pose);
using Interval = std::array<int64_t, 2>;
using Row = std::vector<Interval>;
using Rows = std::vector<Row>;
struct Raster {
    Rows rows;
    int64_t offsetStrip = 0, offsetY = 0;
};
std::vector<Polygon> convolution(const Polygon &, const Polygon &, Budget &);
Raster raster(const std::vector<Polygon> &, int64_t pitch, Budget &);
// 3a3c30(1,0) / 3a7e00 mode 0: unaligned occupied footprint, without
// mode 2's leading empty row or adjacent-strip intersection.
Raster rasterFootprint(const std::vector<Polygon> &, int64_t pitch, Budget &);
Row intersection(const Row &, const Row &);
Row difference(const Row &, const Row &);
struct PartInput {
    Polygon polygon;
    Point pivot{};
    std::vector<Pose> poses;
    int quantity = 0;
    std::vector<Polygon> holes;
    double protection = 0;
};
enum class ProximityAxis { X,Y };
struct ProximityEndpoint {
    size_t group=0,ordinal=0;
    Point point{};
};
struct PointsProximityGroup {
    ProximityAxis axis=ProximityAxis::X;
    std::array<ProximityEndpoint,2> endpoints;
    double tolerance=0;
    std::array<bool,2> watched{true,true};
};
struct Input {
    std::vector<PartInput> parts;
    double width = 0, height = 0, pitch = 0, gap = 0;
    bool fastConstruction = false;
    bool recoveredOffset = false;
    bool lOffcut = false;
    double minimumLOffcutHeight = 0;
    uint32_t randomSeed = 1;
    std::vector<PointsProximityGroup> pointGroups;
};
struct Placement {
    size_t group = 0, ordinal = 0, poseIndex = 0;
    int64_t x = 0, y = 0;
    double exactX = std::numeric_limits<double>::quiet_NaN();
    double exactY = std::numeric_limits<double>::quiet_NaN();
    double solvedAngle = std::numeric_limits<double>::quiet_NaN();
};
struct Output {
    std::vector<Placement> placements;
    int64_t pitchX = 0;
};
Output ordinary(const Input &, Budget &);
Output rectangles(const Input &, Budget &);
bool rectangle_range_nest(const Input &, Budget &, Output &);
// Material-aware configuration-space placement and continuous angle refinement.
void validate_hole(const Polygon &outer, const std::vector<Polygon> &holes, const Polygon &hole);
Output general_nest(const Input &, Budget &);
bool requires_general(const Input &);
bool valid_layout(const Input &, const Output &);
double material_area(const PartInput &);
} // namespace study
