// Demo material host with finite poses and continuous angular intervals.
// host. It adds the actual stock complement to translation configuration space.
// It is not a recovered supplier irregular-stock entry and does not alter DLL.
#include "polygon_geometry.hpp"
#include "clipper2/clipper.h"
#include <optional>
#include <queue>
#include <set>

namespace icax::iplane::geometry {
namespace {
using namespace study;
namespace clip = Clipper2Lib;
using Region = std::vector<Polygon>;
constexpr double scale = 1e8, linearTolerance = 1e-6, areaTolerance = 1e-6;
constexpr size_t maximumCells = 200000, maximumCachePoints = 1000000;
struct ValidationWork {
    Budget *time;
    uint64_t work = 0;
    explicit ValidationWork(Budget *budget = nullptr) : time(budget) {}
    void step() {
        require(++work <= 16000000, PN2S_RESOURCE_LIMIT, "Geometry validation work limit");
        if (time)
            time->check();
    }
    void finish() {
        if (time)
            time->check(true);
    }
};
// These offsets generate search candidates. Accepted layouts still pass exact
// material Booleans and Euclidean boundary/gap checks at linearTolerance.
double candidateArcTolerance(double delta) {
    return std::max(1e-8, std::min(.001, delta * .001));
}
uint64_t seedPriority(uint32_t seed, size_t group, size_t pose = 0) {
    uint64_t value =
        (uint64_t(seed) << 32) ^ uint64_t(group + 1) * 0x9e3779b97f4a7c15ULL ^ uint64_t(pose + 1);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
clip::Paths64 paths(const Region &rings) {
    clip::Paths64 result;
    for (const auto &ring : rings) {
        clip::Path64 p;
        for (const auto point : ring)
            p.emplace_back(rounded(point.x * scale), rounded(point.y * scale));
        result.push_back(std::move(p));
    }
    return result;
}
Region rings(const clip::Paths64 &source) {
    Region result;
    for (const auto &path : source) {
        Polygon p;
        for (const auto point : path)
            p.push_back({double(point.x) / scale, double(point.y) / scale});
        if (p.size() >= 3)
            result.push_back(std::move(p));
    }
    return result;
}
Region material(const Polygon &outer, const std::vector<Polygon> &holes) {
    Region result{outer};
    for (auto hole : holes) {
        if (area(hole) > 0)
            std::reverse(hole.begin(), hole.end());
        result.push_back(std::move(hole));
    }
    return result;
}
Region shape(const PartInput &part, Pose pose, Point position = {}) {
    auto result = material(part.polygon, part.holes);
    for (auto &ring : result) {
        ring = transform(ring, part.pivot, pose);
        for (auto &point : ring) {
            point.x += position.x;
            point.y += position.y;
        }
    }
    return result;
}
Region translated(Region source, Point point) {
    for (auto &ring : source)
        for (auto &p : ring) {
            p.x += point.x;
            p.y += point.y;
        }
    return source;
}
double clippedArea(const Region &a, const Region &b, clip::ClipType op) {
    return std::abs(clip::Area(clip::BooleanOp(op, clip::FillRule::NonZero, paths(a), paths(b)))) /
           (scale * scale);
}
double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
double segmentDistance(Point point, Point a, Point b) {
    const double x = b.x - a.x, y = b.y - a.y, square = x * x + y * y;
    const double t =
        square ? std::clamp(((point.x - a.x) * x + (point.y - a.y) * y) / square, 0., 1.) : 0.;
    return std::hypot(point.x - a.x - t * x, point.y - a.y - t * y);
}
bool crossing(Point a, Point b, Point c, Point d) {
    const auto x = cross(a, b, c), y = cross(a, b, d), z = cross(c, d, a), w = cross(c, d, b);
    return ((x > 0 && y < 0) || (x < 0 && y > 0)) && ((z > 0 && w < 0) || (z < 0 && w > 0));
}
double distance(const Region &a, const Region &b, ValidationWork *work = nullptr) {
    double result = std::numeric_limits<double>::infinity();
    for (const auto &one : a)
        for (const auto &two : b)
            for (size_t i = 0; i < one.size(); ++i)
                for (size_t j = 0; j < two.size(); ++j) {
                    if (work)
                        work->step();
                    const auto p = one[i], q = one[(i + 1) % one.size()], r = two[j],
                               s = two[(j + 1) % two.size()];
                    if (crossing(p, q, r, s))
                        return 0.;
                    result = std::min({result, segmentDistance(p, r, s), segmentDistance(q, r, s),
                                       segmentDistance(r, p, q), segmentDistance(s, p, q)});
                }
    return result;
}
Polygon hull(Polygon points) {
    std::sort(points.begin(), points.end(),
              [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    points.erase(std::unique(points.begin(), points.end(),
                             [](Point a, Point b) { return a.x == b.x && a.y == b.y; }),
                 points.end());
    if (points.size() < 3)
        return {};
    Polygon result;
    for (const auto point : points) {
        while (result.size() > 1 && cross(result[result.size() - 2], result.back(), point) <= 0)
            result.pop_back();
        result.push_back(point);
    }
    const auto lower = result.size();
    for (size_t i = points.size() - 1; i-- > 0;) {
        const auto point = points[i];
        while (result.size() > lower && cross(result[result.size() - 2], result.back(), point) <= 0)
            result.pop_back();
        result.push_back(point);
    }
    result.pop_back();
    return result;
}
// Exact material slabs: hole/outer crossings delimit convex trapezoids. This
// decomposes a Boolean region, rather than filling its holes with a hull.
Region decompose(const Region &source, Budget *time = nullptr) {
    std::vector<double> xs;
    for (const auto &ring : source)
        for (const auto point : ring)
            xs.push_back(point.x);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    Region result;
    struct Edge {
        Point a, b;
        double middle;
    };
    for (size_t n = 1; n < xs.size(); ++n) {
        if (time)
            time->check(true);
        const double left = xs[n - 1], right = xs[n], middle = (left + right) * .5;
        if (right - left < 1e-12)
            continue;
        std::vector<Edge> edges;
        for (const auto &ring : source)
            for (size_t k = 0; k < ring.size(); ++k) {
                const auto a = ring[k], b = ring[(k + 1) % ring.size()];
                if (std::min(a.x, b.x) < middle && middle < std::max(a.x, b.x))
                    edges.push_back({a, b, a.y + (b.y - a.y) * (middle - a.x) / (b.x - a.x)});
            }
        std::sort(edges.begin(), edges.end(),
                  [](const auto &a, const auto &b) { return a.middle < b.middle; });
        require(edges.size() % 2 == 0, PN2S_INVALID, "Unbalanced polygon material slab");
        const auto at = [](const Edge &edge, double x) {
            return edge.a.y + (edge.b.y - edge.a.y) * (x - edge.a.x) / (edge.b.x - edge.a.x);
        };
        for (size_t k = 0; k < edges.size(); k += 2) {
            auto cell = hull({{left, at(edges[k], left)},
                              {right, at(edges[k], right)},
                              {right, at(edges[k + 1], right)},
                              {left, at(edges[k + 1], left)}});
            if (cell.size() > 2 && area(cell) > 1e-12) {
                require(result.size() < maximumCells, PN2S_RESOURCE_LIMIT,
                        "Polygon material decomposition cell limit");
                result.push_back(std::move(cell));
            }
        }
    }
    return result;
}
Region forbidden(const Region &fixed, const Region &moving, double gap, Budget &budget) {
    require(!fixed.empty() && !moving.empty(), PN2S_RESOURCE_LIMIT,
            "Cannot construct a nondegenerate material configuration space");
    require(fixed.size() * moving.size() <= 200000, PN2S_RESOURCE_LIMIT,
            "Polygon configuration-space complexity limit");
    clip::Paths64 sums;
    for (const auto &a : fixed)
        for (const auto &b : moving) {
            budget.check();
            Polygon points;
            points.reserve(a.size() * b.size());
            for (const auto p : a)
                for (const auto q : b)
                    points.push_back({p.x - q.x, p.y - q.y});
            const auto cell = hull(std::move(points));
            if (cell.size() > 2)
                sums.push_back(paths({cell}).front());
        }
    auto result = clip::Union(sums, clip::FillRule::NonZero);
    if (gap > 0)
        result = clip::InflatePaths(result, gap * scale, clip::JoinType::Round, clip::EndType::Polygon,
                                    2., candidateArcTolerance(gap) * scale);
    budget.check(true);
    require(!result.empty(), PN2S_RESOURCE_LIMIT,
            "Material configuration space collapsed at integer precision");
    return rings(result);
}
struct Placed {
    Placement record;
    Region material;
    mutable Region cells;
    Box box;
};
struct PoseGeometry {
    Region material, cells, outerCells;
    Box box;
};
struct SearchPolicy {
    bool vertical = false, right = false, top = false;
    uint32_t variation = 0;
};
struct Domain {
    Region physical, usable, excludedCells, excludedComponents;
    double margin;
    std::vector<double> holeMargins;
    std::map<std::tuple<size_t, size_t, double>, Region> forbiddenCache;
    std::map<std::tuple<size_t, size_t, double>, PoseGeometry> poseCache;
    std::map<std::tuple<size_t, size_t, double, size_t, size_t, double, double>, Region> pairCache;
    size_t pairCachePoints = 0, pairCacheHits = 0, pairCacheMisses = 0;
    size_t poseCachePoints = 0, stockCachePoints = 0;
    size_t continuousEvaluations = 0, continuousRefinements = 0;
    size_t continuousIntervalMinimizations = 0, continuousSubdivisions = 0;
    SearchPolicy policy;
    Domain(const Board &board, Box box, Budget &time)
        : physical(material(board.outer, board.holes)), usable(physical), margin(board.margin),
          holeMargins(board.holeMargins) {
        require(std::isfinite(margin) && margin >= 0 &&
                    (holeMargins.empty() || holeMargins.size() == board.holes.size()),
                PN2S_INVALID, "Invalid stock boundary margins");
        for (const auto value : holeMargins)
            require(std::isfinite(value) && value >= 0, PN2S_INVALID, "Invalid stock hole margin");
        if (!holeMargins.empty()) {
            auto outer = paths({board.outer});
            if (margin > 0)
                outer = clip::InflatePaths(outer, -margin * scale, clip::JoinType::Round,
                                          clip::EndType::Polygon, 2., candidateArcTolerance(margin) * scale);
            clip::Paths64 excludedHoles;
            for (size_t n = 0; n < board.holes.size(); ++n) {
                time.check(true);
                auto hole = board.holes[n]; if (area(hole) < 0) std::reverse(hole.begin(), hole.end());
                auto expanded = paths({hole});
                if (holeMargins[n] > 0)
                    expanded = clip::InflatePaths(expanded, holeMargins[n] * scale,
                        clip::JoinType::Round, clip::EndType::Polygon, 2.,
                        candidateArcTolerance(holeMargins[n]) * scale);
                excludedHoles.insert(excludedHoles.end(), expanded.begin(), expanded.end());
            }
            usable = rings(clip::Difference(outer, excludedHoles, clip::FillRule::NonZero));
        } else if (margin > 0)
            usable = rings(clip::InflatePaths(paths(physical), -margin * scale, clip::JoinType::Round,
                                              clip::EndType::Polygon, 2.,
                                              candidateArcTolerance(margin) * scale));
        const Region rectangle{{{box.x0, box.y0}, {box.x1, box.y0}, {box.x1, box.y1}, {box.x0, box.y1}}};
        const auto excluded =
            rings(clip::Difference(paths(rectangle), paths(usable), clip::FillRule::NonZero));
        for (const auto &ring : excluded)
            if (area(ring) > 0)
                excludedComponents.push_back(ring);
        excludedCells = decompose(excluded, &time);
    }
};
size_t pointCount(const Region &region) {
    size_t count = 0;
    for (const auto &ring : region)
        count += ring.size();
    return count;
}
PoseGeometry &poseGeometry(const Input &input, size_t group, size_t pose, double angle, Domain &domain, Budget &budget) {
    const auto key = std::tuple{group, pose, angle};
    auto found = domain.poseCache.find(key);
    if (found == domain.poseCache.end()) {
        auto actual = input.parts[group].poses[pose]; actual.angle = actual.upper = angle;
        auto physical = shape(input.parts[group], actual);
        validatePrecision(physical.front(), Region(physical.begin() + 1, physical.end()), &budget);
        const auto points = pointCount(physical);
        require(domain.poseCachePoints + points <= maximumCachePoints, PN2S_RESOURCE_LIMIT,
                "Polygon pose cache vertex limit");
        domain.poseCachePoints += points;
        const auto box = bounds(physical.front());
        found = domain.poseCache.emplace(key, PoseGeometry{std::move(physical), {}, {}, box}).first;
    }
    return found->second;
}
const Region &poseCells(PoseGeometry &pose, bool withHoles, Domain &domain, Budget &budget) {
    auto &cells = withHoles ? pose.cells : pose.outerCells;
    if (cells.empty()) {
        auto result = decompose(withHoles ? pose.material : Region{pose.material.front()}, &budget);
        const auto points = pointCount(result);
        require(domain.poseCachePoints + points <= maximumCachePoints, PN2S_RESOURCE_LIMIT,
                "Polygon pose decomposition cache vertex limit");
        domain.poseCachePoints += points;
        cells = std::move(result);
    }
    return cells;
}
bool canEnterHole(const Region &container, const Box &moving) {
    for (size_t n = 1; n < container.size(); ++n) {
        const auto box = bounds(container[n]);
        if (moving.x1 - moving.x0 <= box.x1 - box.x0 + linearTolerance &&
            moving.y1 - moving.y0 <= box.y1 - box.y0 + linearTolerance)
            return true;
    }
    return false;
}
Region pairForbidden(const Input &input, size_t group, size_t pose, double angle, const Placed &fixed, double gap,
                     Domain &domain, Budget &budget) {
    const auto &record = fixed.record;
    const double fixedAngle = std::isfinite(record.solvedAngle) ? record.solvedAngle :
                              input.parts[record.group].poses[record.poseIndex].angle;
    const auto key = std::tuple{record.group, record.poseIndex, fixedAngle, group, pose, angle, gap};
    auto found = domain.pairCache.find(key);
    const Point translation{record.exactX, record.exactY};
    if (found != domain.pairCache.end()) {
        ++domain.pairCacheHits;
        return translated(found->second, translation);
    }
    ++domain.pairCacheMisses;
    auto &movingPose = poseGeometry(input, group, pose, angle, domain, budget);
    auto &fixedPose = poseGeometry(input, record.group, record.poseIndex, fixedAngle, domain, budget);
    const bool nesting = canEnterHole(fixedPose.material, movingPose.box) ||
                         canEnterHole(movingPose.material, fixedPose.box);
    auto nfp = forbidden(poseCells(fixedPose, nesting, domain, budget),
                         poseCells(movingPose, nesting, domain, budget), gap, budget);
    size_t points = 0;
    for (const auto &ring : nfp)
        points += ring.size();
    // Each origin NFP is shared by instances and restart passes. Bound cache
    // memory for inventories containing many unrelated part and pose pairs.
    if (domain.pairCache.size() < 2048 && domain.pairCachePoints + points <= 1000000) {
        domain.pairCachePoints += points;
        found = domain.pairCache.emplace(key, std::move(nfp)).first;
        return translated(found->second, translation);
    }
    return translated(std::move(nfp), translation);
}
bool separate(Box a, Box b, double gap) {
    return a.x1 + gap <= b.x0 + linearTolerance || b.x1 + gap <= a.x0 + linearTolerance ||
           a.y1 + gap <= b.y0 + linearTolerance || b.y1 + gap <= a.y0 + linearTolerance;
}
bool feasible(const Input &input, size_t group, const Region &physical, Box box, const Domain &domain,
              const std::vector<Placed> &placed, Budget &budget) {
    ValidationWork work(&budget);
    if (domain.usable.empty())
        return false;
    if (std::abs(clip::Area(paths(physical))) / (scale * scale) <= areaTolerance)
        return false;
    if (clippedArea(physical, domain.physical, clip::ClipType::Difference) > areaTolerance)
        return false;
    if (domain.holeMargins.empty()) {
        if (domain.margin > 0 && distance(physical, domain.physical, &work) + linearTolerance < domain.margin)
            return false;
    } else {
        if (domain.margin > 0 && distance(physical, {domain.physical.front()}, &work) + linearTolerance < domain.margin)
            return false;
        for (size_t n = 0; n < domain.holeMargins.size(); ++n)
            if (domain.holeMargins[n] > 0 && distance(physical, {domain.physical[n + 1]}, &work) + linearTolerance < domain.holeMargins[n])
                return false;
    }
    for (const auto &part : placed) {
        const auto gap = std::max(
            {input.gap, input.parts[group].protection, input.parts[part.record.group].protection});
        if (separate(box, part.box, gap))
            continue;
        if (clippedArea(physical, part.material, clip::ClipType::Intersection) > areaTolerance)
            return false;
        if (gap > 0 && distance(physical, part.material, &work) + linearTolerance < gap)
            return false;
    }
    work.finish();
    return true;
}
void addPoint(std::vector<Point> &out, Point p, Box legal) {
    if (p.x >= legal.x0 - linearTolerance && p.x <= legal.x1 + linearTolerance &&
        p.y >= legal.y0 - linearTolerance && p.y <= legal.y1 + linearTolerance) {
        require(out.size() < 300000, PN2S_RESOURCE_LIMIT,
                "Polygon placement candidate complexity limit");
        out.push_back({std::clamp(p.x, legal.x0, legal.x1), std::clamp(p.y, legal.y0, legal.y1)});
    }
}
void intersection(std::vector<Point> &out, Point a, Point b, Point c, Point d, Box legal) {
    const double ux = b.x - a.x, uy = b.y - a.y, vx = d.x - c.x, vy = d.y - c.y, den = ux * vy - uy * vx;
    if (std::abs(den) < 1e-14)
        return;
    const double wx = c.x - a.x, wy = c.y - a.y, t = (wx * vy - wy * vx) / den,
                 s = (wx * uy - wy * ux) / den;
    if (t >= -1e-10 && t <= 1 + 1e-10 && s >= -1e-10 && s <= 1 + 1e-10)
        addPoint(out, {a.x + t * ux, a.y + t * uy}, legal);
}
bool insideForbidden(Point point, const clip::Paths64 &polygons, const Region &boundaries) {
    const clip::Point64 integer{rounded(point.x * scale), rounded(point.y * scale)};
    int winding = 0;
    for (const auto &path : polygons) {
        const auto relation = clip::PointInPolygon(integer, path);
        if (relation == clip::PointInPolygonResult::IsOn)
            return false;
        if (relation == clip::PointInPolygonResult::IsInside)
            winding += clip::IsPositive(path) ? 1 : -1;
    }
    if (!winding)
        return false;
    // Retain contacts and tolerance-near boundary candidates for the unchanged
    // material/distance validator. Only discard points strictly in NFP interior.
    constexpr double epsilon = linearTolerance + 2 / scale;
    for (const auto &ring : boundaries)
        for (size_t n = 0; n < ring.size(); ++n) {
            const auto a = ring[n], b = ring[(n + 1) % ring.size()];
            if (point.x < std::min(a.x, b.x) - epsilon || point.x > std::max(a.x, b.x) + epsilon ||
                point.y < std::min(a.y, b.y) - epsilon || point.y > std::max(a.y, b.y) + epsilon)
                continue;
            if (segmentDistance(point, a, b) <= epsilon)
                return false;
        }
    return true;
}
bool convex(const Polygon &polygon) {
    double direction = 0;
    for (size_t n = 0; n < polygon.size(); ++n) {
        const auto value =
            cross(polygon[n], polygon[(n + 1) % polygon.size()], polygon[(n + 2) % polygon.size()]);
        if (!value)
            continue;
        if (direction && (value < 0) != (direction < 0))
            return false;
        direction = value;
    }
    return true;
}
struct ConvexForbidden {
    clip::Paths64 paths;
    Region boundaries;
    Box box;
};
struct Candidate {
    bool valid = false;
    size_t pose = 0;
    Point position{};
    double score = 0;
    Region material;
    double angle = 0;
};
Candidate position(const Input &input, size_t group, size_t pose, double angle, const std::vector<Placed> &placed,
                   Domain &domain, Budget &budget) {
    budget.check(true);
    const auto &part = input.parts[group];
    auto &movingPose = poseGeometry(input, group, pose, angle, domain, budget);
    const auto &physical = movingPose.material;
    const auto box = movingPose.box;
    Box legal{domain.margin - box.x0, domain.margin - box.y0, input.width - domain.margin - box.x1,
              input.height - domain.margin - box.y1};
    if (legal.x1 < legal.x0 - linearTolerance || legal.y1 < legal.y0 - linearTolerance ||
        domain.usable.empty())
        return {};
    legal.x1 = std::max(legal.x0, legal.x1);
    legal.y1 = std::max(legal.y0, legal.y1);
    const Polygon boundary{
        {legal.x0, legal.y0}, {legal.x1, legal.y0}, {legal.x1, legal.y1}, {legal.x0, legal.y1}};
    std::vector<Point> candidates = boundary;
    const auto &policy = domain.policy;
    double used = 0;
    for (const auto &old : placed) {
        const double x = policy.right ? input.width - old.box.x0 : old.box.x1;
        const double y = policy.top ? input.height - old.box.y0 : old.box.y1;
        used = std::max(used, policy.vertical ? y : x);
    }
    const auto score = [&](Point p) {
        const double xExtent = policy.right ? input.width - p.x - box.x0 : p.x + box.x1;
        const double yExtent = policy.top ? input.height - p.y - box.y0 : p.y + box.y1;
        const double xOrigin = policy.right ? input.width - p.x - box.x1 : p.x + box.x0;
        const double yOrigin = policy.top ? input.height - p.y - box.y1 : p.y + box.y0;
        return std::max(used, policy.vertical ? yExtent : xExtent) +
               1e-5 * (policy.vertical ? xOrigin : yOrigin) +
               1e-7 * (policy.vertical ? yOrigin : xOrigin);
    };
    const auto candidate = [&](Point p) -> Candidate {
        auto shifted = translated(physical, p);
        const Box actual{box.x0 + p.x, box.y0 + p.y, box.x1 + p.x, box.y1 + p.y};
        if (feasible(input, group, shifted, actual, domain, placed, budget))
            return {true, pose, p, score(p), std::move(shifted), angle};
        return {};
    };
    if (auto first = candidate({policy.right ? legal.x1 : legal.x0, policy.top ? legal.y1 : legal.y0});
        first.valid)
        return first;
    Region allForbidden;
    std::vector<ConvexForbidden> quickForbidden;
    if (!domain.excludedCells.empty()) {
        const auto key = std::tuple{group, pose, angle};
        auto found = domain.forbiddenCache.find(key);
        if (found == domain.forbiddenCache.end()) {
            // Filling a moving void is safe only when no connected excluded stock
            // component can fit in that void. Test each component separately: an
            // isolated stock hole may fit even though the exterior complement cannot.
            const bool nesting = std::any_of(
                domain.excludedComponents.begin(), domain.excludedComponents.end(),
                [&](const Polygon &component) { return canEnterHole(physical, bounds(component)); });
            auto result = forbidden(domain.excludedCells, poseCells(movingPose, nesting, domain, budget),
                                    0, budget);
            const auto points = pointCount(result);
            require(domain.stockCachePoints + points <= maximumCachePoints, PN2S_RESOURCE_LIMIT,
                    "Stock configuration-space cache vertex limit");
            domain.stockCachePoints += points;
            found = domain.forbiddenCache.emplace(key, std::move(result)).first;
        }
        for (const auto &ring : found->second) {
            for (const auto point : ring)
                addPoint(candidates, point, legal);
            allForbidden.push_back(ring);
        }
        // A component fitting exactly inside a moving void can leave only a
        // point or line of legal translations. Area Booleans may discard that
        // zero-area domain, so retain the opposing boundary contact candidates.
        for (const auto &component : domain.excludedComponents) {
            const auto fixed = bounds(component);
            for (size_t n = 1; n < physical.size(); ++n) {
                const auto hole = bounds(physical[n]);
                if (fixed.x1 - fixed.x0 > hole.x1 - hole.x0 + linearTolerance ||
                    fixed.y1 - fixed.y0 > hole.y1 - hole.y0 + linearTolerance)
                    continue;
                for (const auto x : {fixed.x0 - hole.x0, fixed.x1 - hole.x1})
                    for (const auto y : {fixed.y0 - hole.y0, fixed.y1 - hole.y1})
                        addPoint(candidates, {x, y}, legal);
            }
        }
        for (const auto &fixed : domain.usable)
            for (const auto &moving : physical)
                for (const auto a : fixed) {
                    budget.check(true);
                    for (const auto b : moving)
                        addPoint(candidates, {a.x - b.x, a.y - b.y}, legal);
                }
    }
    for (const auto &old : placed) {
        const auto gap =
            std::max({input.gap, part.protection, input.parts[old.record.group].protection});
        for (const auto x : {legal.x0, old.box.x0 - gap - box.x1, old.box.x1 + gap - box.x0,
                             old.box.x0 - box.x0, old.box.x1 - box.x1})
            for (const auto y : {legal.y0, old.box.y0 - gap - box.y1, old.box.y1 + gap - box.y0,
                                 old.box.y0 - box.y0, old.box.y1 - box.y1})
                addPoint(candidates, {x, y}, legal);
        const auto nfp = pairForbidden(input, group, pose, angle, old, gap, domain, budget);
        require(!nfp.empty(), PN2S_RESOURCE_LIMIT, "Empty part configuration space");
        for (const auto &ring : nfp) {
            for (const auto point : ring)
                addPoint(candidates, point, legal);
            allForbidden.push_back(ring);
        }
        // A Boolean union can erase an isolated legal point or line between
        // touching obstacles, or inside a part void. Test each proven convex
        // pair separately; retain the full exact validator for all other cases.
        const bool nesting = canEnterHole(old.material, box) || canEnterHole(physical, old.box);
        if (!nesting && convex(old.material.front()) && convex(physical.front()))
            quickForbidden.push_back({paths(nfp), nfp, bounds(nfp.front())});
        for (size_t n = 1; n < old.material.size(); ++n) {
            const auto hole = bounds(old.material[n]);
            for (const auto x : {hole.x0 + gap - box.x0, hole.x1 - gap - box.x1})
                for (const auto y : {hole.y0 + gap - box.y0, hole.y1 - gap - box.y1})
                    addPoint(candidates, {x, y}, legal);
        }
        if (gap == 0)
            for (const auto &fixedRing : old.material)
                for (const auto &moving : physical)
                    for (const auto a : fixedRing)
                        for (const auto b : moving)
                            addPoint(candidates, {a.x - b.x, a.y - b.y}, legal);
    }
    clip::Paths64 unitedPaths;
    Region united;
    if (!allForbidden.empty()) {
        unitedPaths = clip::Union(paths(allForbidden), clip::FillRule::NonZero);
        united = rings(unitedPaths);
        for (const auto &ring : united)
            for (const auto point : ring)
                addPoint(candidates, point, legal);
        if (legal.x1 - legal.x0 > 1e-10 && legal.y1 - legal.y0 > 1e-10) {
            const auto free =
                rings(clip::Difference(paths({boundary}), paths(united), clip::FillRule::NonZero));
            for (const auto &ring : free)
                for (const auto point : ring)
                    addPoint(candidates, point, legal);
        }
        for (const auto &ring : allForbidden)
            for (size_t n = 0; n < ring.size(); ++n)
                for (size_t k = 0; k < 4; ++k)
                    intersection(candidates, ring[n], ring[(n + 1) % ring.size()], boundary[k],
                                 boundary[(k + 1) % 4], legal);
    }
    require(candidates.size() <= 300000, PN2S_RESOURCE_LIMIT,
            "Polygon placement candidate complexity limit");
    std::sort(candidates.begin(), candidates.end(), [&](Point a, Point b) {
        const auto x = score(a), y = score(b);
        return x < y || (x == y && (a.x < b.x || (a.x == b.x && a.y < b.y)));
    });
    std::set<std::pair<int64_t, int64_t>> seen;
    for (const auto point : candidates) {
        budget.check();
        if (!seen.emplace(rounded(point.x * scale), rounded(point.y * scale)).second)
            continue;
        if (std::any_of(quickForbidden.begin(), quickForbidden.end(), [&](const ConvexForbidden &fixed) {
                if (point.x < fixed.box.x0 - linearTolerance ||
                    point.x > fixed.box.x1 + linearTolerance ||
                    point.y < fixed.box.y0 - linearTolerance || point.y > fixed.box.y1 + linearTolerance)
                    return false;
                return insideForbidden(point, fixed.paths, fixed.boundaries);
            }))
            continue;
        if (auto found = candidate(point); found.valid)
            return found;
    }
    return {};
}
bool angleInRange(const Pose &range, double angle) {
    return std::isfinite(range.angle) && std::isfinite(range.upper) &&
           range.angle <= range.upper && range.upper - range.angle <= 360. + 1e-9 &&
           std::isfinite(angle) && angle >= range.angle && angle <= range.upper;
}
std::vector<double> edgeDirections(const Region &region, size_t limit, Budget &budget) {
    std::vector<std::pair<double, double>> edges;
    for (const auto &ring : region) for (size_t n = 0; n < ring.size(); ++n) {
        budget.check(); const auto a = ring[n], b = ring[(n + 1) % ring.size()];
        edges.emplace_back(std::hypot(b.x - a.x, b.y - a.y),
                           std::atan2(b.y - a.y, b.x - a.x) * 180. / pi);
    }
    std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.first > b.first; });
    std::vector<double> result;
    for (const auto &[length, raw] : edges) {
        (void)length; const double value = std::fmod(raw + 360., 180.);
        if (std::none_of(result.begin(), result.end(), [&](double old) {
                return std::abs(std::remainder(old - value, 180.)) < 1e-9;
            })) result.push_back(value);
        if (result.size() >= limit) break;
    }
    return result;
}
struct AngularRange {
    size_t index;
    Pose source;
    std::vector<double> features, critical;
    std::set<double> tested;
};
double boxFitCost(const PartInput &part, const Pose &source, double angle, Point window) {
    const auto b = bounds(transform(part.polygon, part.pivot, {angle, source.flip}));
    return std::max((b.x1 - b.x0) / std::max(window.x, 1e-12),
                    (b.y1 - b.y0) / std::max(window.y, 1e-12));
}
Candidate choose(const Input &input, size_t group, const std::vector<Placed> &placed, Domain &domain,
                 Budget &budget) {
    Candidate result;
    const auto &part = input.parts[group];
    std::vector<size_t> order(part.poses.size()); std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto seed = input.randomSeed ^ domain.policy.variation * 0x9e3779b9U;
        return seedPriority(seed, group, a) < seedPriority(seed, group, b);
    });
    std::vector<AngularRange> ranges;
    size_t evaluated = 0;
    const auto evaluate = [&](size_t index, double angle, bool continuous) {
        budget.check(true);
        require(angleInRange(part.poses[index], angle), PN2S_INVALID, "Angle outside source interval");
        if (continuous) { ++domain.continuousEvaluations; ++evaluated; }
        auto candidate = position(input, group, index, angle, placed, domain, budget);
        if (candidate.valid && (!result.valid || candidate.score < result.score - 1e-10))
            result = std::move(candidate);
    };
    try {
        for (const auto index : order) {
            const auto &pose = part.poses[index];
            require(angleInRange(pose, pose.angle), PN2S_INVALID, "Invalid continuous angle interval");
            if (pose.angle == pose.upper) evaluate(index, pose.angle, false);
            else ranges.push_back({index, pose, {}, {pose.angle, pose.upper}, {}});
        }
        if (ranges.empty()) return result;
        const auto test = [&](AngularRange &range, double angle) {
            angle = std::clamp(angle, range.source.angle, range.source.upper);
            if (range.tested.insert(angle).second) evaluate(range.index, angle, true);
        };
        // Each source range/flip gets its endpoints and midpoint before one
        // interval can consume the remaining search effort.
        for (auto &range : ranges) {
            test(range, range.source.angle); test(range, range.source.upper);
            test(range, (range.source.angle + range.source.upper) * .5);
        }
        Region moving{part.polygon}; moving.insert(moving.end(), part.holes.begin(), part.holes.end());
        Region fixed = domain.usable;
        for (const auto &old : placed) fixed.insert(fixed.end(), old.material.begin(), old.material.end());
        const auto movingDirections = edgeDirections(moving, 24, budget);
        const auto fixedDirections = edgeDirections(fixed, 32, budget);
        const Point boardWindow{input.width - 2 * domain.margin, input.height - 2 * domain.margin};
        const auto periodic = [&](std::vector<double> &values, const Pose &source, double base, double period) {
            const double first = std::ceil((source.angle - base) / period);
            for (int n = 0; n < 4; ++n) {
                const double angle = base + (first + n) * period;
                if (angle > source.upper) break;
                if (angle >= source.angle) values.push_back(angle);
            }
        };
        for (auto &range : ranges) {
            // Actual moving/fixed supporting-edge contacts, including all
            // board/part voids. These angles come from geometry, not a grid.
            for (const double direction : movingDirections) {
                periodic(range.critical, range.source, -direction, 90.);
                for (const double target : fixedDirections)
                    periodic(range.features, range.source,
                             (range.source.flip ? -target : target) - direction, 180.);
            }
            std::sort(range.critical.begin(), range.critical.end());
            range.critical.erase(std::unique(range.critical.begin(), range.critical.end()), range.critical.end());
            std::sort(range.features.begin(), range.features.end());
            range.features.erase(std::unique(range.features.begin(), range.features.end()), range.features.end());
            std::vector<std::pair<double, double>> ranked;
            for (const auto angle : range.features) {
                budget.check(true); ranked.emplace_back(boxFitCost(part, range.source, angle, boardWindow), angle);
            }
            std::stable_sort(ranked.begin(), ranked.end()); range.features.clear();
            for (const auto &[cost, angle] : ranked) { (void)cost; range.features.push_back(angle); }
        }
        for (size_t n = 0; n < 32; ++n) for (auto &range : ranges)
            if (n < range.features.size() && evaluated < 192) test(range, range.features[n]);
        // Minimize continuous support-span violations between geometry-derived
        // critical angles. This finds feasible narrow windows whose angle is
        // neither an endpoint nor a parallel-edge contact.
        std::vector<Point> windows{boardWindow};
        for (const auto &old : placed) for (size_t h = 1; h < old.material.size() && windows.size() < 24; ++h) {
            const auto b = bounds(old.material[h]); windows.push_back({b.x1 - b.x0 - 2 * input.gap, b.y1 - b.y0 - 2 * input.gap});
        }
        if (!result.valid) for (auto &range : ranges) {
            for (const auto window : windows) for (size_t n = 1; n < range.critical.size() && evaluated < 160; ++n) {
                budget.check(true); double lo = range.critical[n - 1], hi = range.critical[n];
                ++domain.continuousIntervalMinimizations;
                constexpr double ratio = .6180339887498948482;
                double a = hi - ratio * (hi - lo), b = lo + ratio * (hi - lo);
                double fa = boxFitCost(part, range.source, a, window), fb = boxFitCost(part, range.source, b, window);
                for (int iteration = 0; iteration < 48; ++iteration) {
                    budget.check();
                    if (fa < fb) { hi = b; b = a; fb = fa; a = hi - ratio * (hi - lo); fa = boxFitCost(part, range.source, a, window); }
                    else { lo = a; a = b; fa = fb; b = lo + ratio * (hi - lo); fb = boxFitCost(part, range.source, b, window); }
                }
                test(range, (lo + hi) * .5);
            }
        }
        // Adaptive interval subdivision is a fallback for nonparallel material
        // contacts. Geometry-derived cells are bisected, rather than replacing
        // the allowed interval by any fixed angle table.
        if (!result.valid) {
            struct Cell { size_t range; double lo, hi; bool operator<(const Cell &b) const { return hi - lo < b.hi - b.lo; } };
            std::priority_queue<Cell> cells;
            for (size_t r = 0; r < ranges.size(); ++r) for (size_t n = 1; n < ranges[r].critical.size(); ++n)
                cells.push({r, ranges[r].critical[n - 1], ranges[r].critical[n]});
            while (!cells.empty() && evaluated < 192 && !result.valid) {
                budget.check(true); const auto cell = cells.top(); cells.pop();
                if (cell.hi - cell.lo < 1e-8) continue;
                ++domain.continuousSubdivisions;
                const double middle = (cell.lo + cell.hi) * .5; test(ranges[cell.range], middle);
                cells.push({cell.range, cell.lo, middle}); cells.push({cell.range, middle, cell.hi});
            }
        }
        if (result.valid && part.poses[result.pose].upper > part.poses[result.pose].angle) {
            const auto &source = part.poses[result.pose];
            double step = (source.upper - source.angle) / 8;
            for (int iteration = 0; iteration < 40 && step > 1e-8 && evaluated < 256; ++iteration) {
                budget.check(true); const double center = result.angle, before = result.score;
                for (const int sign : {-1, 1}) {
                    const double angle = std::clamp(center + sign * step, source.angle, source.upper);
                    if (angle == center) continue;
                    ++domain.continuousRefinements; evaluate(result.pose, angle, true);
                }
                if (result.score >= before - 1e-10) step *= .5;
            }
        }
    } catch (const Failure &error) {
        if (!result.valid || (error.code != PN2S_TIMEOUT && error.code != PN2S_RESOURCE_LIMIT)) throw;
    }
    return result;
}
std::array<double, 3> compactMetric(const std::vector<Placed> &parts) {
    if (parts.empty())
        return {};
    Box box = parts.front().box;
    double coordinateSum = 0;
    for (const auto &part : parts) {
        box.x0 = std::min(box.x0, part.box.x0);
        box.y0 = std::min(box.y0, part.box.y0);
        box.x1 = std::max(box.x1, part.box.x1);
        box.y1 = std::max(box.y1, part.box.y1);
        coordinateSum += part.box.x0 + part.box.y0;
    }
    const double width = box.x1 - box.x0, height = box.y1 - box.y0;
    return {width * height, width + height, coordinateSum};
}
bool compactBetter(const std::array<double, 3> &a, const std::array<double, 3> &b) {
    for (size_t n = 0; n < a.size(); ++n) {
        if (a[n] < b[n] - linearTolerance)
            return true;
        if (a[n] > b[n] + linearTolerance)
            return false;
    }
    return false;
}
bool compact(const Input &input, std::vector<Placed> &placed, Domain &domain, Budget &parent) {
    if (placed.size() < 2)
        return false;
    const auto left =
        std::chrono::duration<double>(parent.deadline - std::chrono::steady_clock::now()).count();
    if (left < .002)
        return false;
    Budget time(std::min(2., left * .15));
    bool anyChange = false;
    try {
        for (int pass = 0; pass < 2; ++pass) {
            bool changed = false;
            for (size_t n = placed.size(); n-- > 0;) {
                time.check(true);
                const auto before = compactMetric(placed);
                auto remaining = placed;
                auto original = remaining[n];
                remaining.erase(remaining.begin() + ptrdiff_t(n));
                auto candidate = choose(input, original.record.group, remaining, domain, time);
                if (!candidate.valid)
                    continue;
                auto record = original.record;
                record.poseIndex = candidate.pose;
                record.exactX = candidate.position.x;
                record.exactY = candidate.position.y;
                record.x = rounded(record.exactX * 1e6);
                record.y = rounded(record.exactY * 1e3);
                record.solvedAngle = candidate.angle;
                const auto box = bounds(candidate.material.front());
                Placed moved{record, std::move(candidate.material), {}, box};
                remaining.push_back(moved);
                if (compactBetter(compactMetric(remaining), before)) {
                    placed[n] = std::move(moved);
                    changed = anyChange = true;
                }
            }
            if (!changed)
                break;
        }
    } catch (const Failure &error) {
        if (error.code != PN2S_TIMEOUT && error.code != PN2S_RESOURCE_LIMIT)
            throw;
    }
    return anyChange;
}
void fill(const Input &input, std::vector<Placed> &placed, const std::vector<size_t> &order,
          bool batchGroups, Domain &domain, Budget &budget) {
    std::vector<size_t> used(input.parts.size());
    for (const auto &part : placed)
        ++used[part.record.group];
    const auto add = [&](size_t group) {
        if (used[group] >= size_t(input.parts[group].quantity))
            return false;
        budget.check(true);
        auto candidate = choose(input, group, placed, domain, budget);
        if (!candidate.valid)
            return false;
        Placement record;
        record.group = group;
        record.ordinal = used[group]++;
        record.poseIndex = candidate.pose;
        record.exactX = candidate.position.x;
        record.exactY = candidate.position.y;
        record.solvedAngle = candidate.angle;
        record.x = rounded(record.exactX * 1e6);
        record.y = rounded(record.exactY * 1e3);
        const auto box = bounds(candidate.material.front());
        placed.push_back({record, std::move(candidate.material), {}, box});
        return true;
    };
    if (batchGroups) {
        for (const auto group : order)
            while (add(group)) {
            }
    } else {
        bool progress = true;
        while (progress) {
            progress = false;
            for (const auto group : order)
                progress = add(group) || progress;
        }
    }
}
bool layoutBetter(const std::vector<Placed> &candidate, const std::vector<Placed> &best,
                  const Input &input) {
    if (candidate.size() != best.size())
        return candidate.size() > best.size();
    const auto netArea = [&](const std::vector<Placed> &parts) {
        double result = 0;
        for (const auto &part : parts) {
            const auto &source = input.parts[part.record.group];
            result += materialArea(source.polygon, source.holes);
        }
        return result;
    };
    const double area = netArea(candidate), previous = netArea(best);
    if (std::abs(area - previous) > areaTolerance)
        return area > previous;
    return compactBetter(compactMetric(candidate), compactMetric(best));
}
} // namespace

double materialArea(const study::Polygon &outer, const std::vector<study::Polygon> &holes) {
    double result = std::abs(study::area(outer));
    for (const auto &hole : holes)
        result -= std::abs(study::area(hole));
    return result;
}
namespace {
void validatePolygon(const Polygon &polygon, ValidationWork &work) {
    require(polygon.size() >= 3 && polygon.size() <= 8192, PN2S_INVALID,
            "Need 3..8192 polygon vertices");
    for (size_t i = 0; i < polygon.size(); ++i) {
        work.step();
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        require(std::isfinite(a.x) && std::isfinite(a.y) && std::abs(a.x) <= 1e6 && std::abs(a.y) <= 1e6,
                PN2S_INVALID, "Invalid coordinate");
        require(a.x != b.x || a.y != b.y, PN2S_INVALID, "Zero-length edge");
        for (size_t j = i + 1; j < polygon.size(); ++j) {
            work.step();
            if (j == i + 1 || (i == 0 && j == polygon.size() - 1))
                continue;
            const auto c = polygon[j], d = polygon[(j + 1) % polygon.size()];
            if (std::max(std::min(a.x, b.x), std::min(c.x, d.x)) >
                    std::min(std::max(a.x, b.x), std::max(c.x, d.x)) ||
                std::max(std::min(a.y, b.y), std::min(c.y, d.y)) >
                    std::min(std::max(a.y, b.y), std::max(c.y, d.y)))
                continue;
            const double u = cross(a, b, c), v = cross(a, b, d), w = cross(c, d, a), z = cross(c, d, b);
            require(!((u <= 0 && v >= 0 || u >= 0 && v <= 0) && (w <= 0 && z >= 0 || w >= 0 && z <= 0)),
                    PN2S_INVALID, "Self-intersection or self-contact");
        }
    }
    require(area(polygon) > 0, PN2S_INVALID, "Simple counterclockwise outer ring required");
}
double quantizedRingArea(const Polygon &polygon, ValidationWork &work) {
    clip::Path64 path;
    std::set<std::pair<int64_t, int64_t>> vertices;
    for (const auto point : polygon) {
        work.step();
        require(std::isfinite(point.x) && std::isfinite(point.y) &&
                    std::abs(point.x * scale) <= double(clip::MAX_COORD) * .5 &&
                    std::abs(point.y * scale) <= double(clip::MAX_COORD) * .5,
                PN2S_INVALID, "轮廓坐标超出整数几何精度范围");
        const auto x = rounded(point.x * scale), y = rounded(point.y * scale);
        require(vertices.emplace(x, y).second, PN2S_INVALID,
                "轮廓存在小于当前计算精度的细节，未用于排样，请检查图形");
        path.emplace_back(x, y);
    }
    const double result = std::abs(clip::Area(path)) / (scale * scale);
    require(path.size() >= 3 && result > 0, PN2S_INVALID,
            "轮廓存在小于当前计算精度的细节，未用于排样，请检查图形");
    return result;
}
void precision(const Polygon &outer, const std::vector<Polygon> &holes, ValidationWork &work) {
    double net = quantizedRingArea(outer, work);
    for (const auto &hole : holes)
        net -= quantizedRingArea(hole, work);
    require(net > areaTolerance, PN2S_INVALID, "材料净面积小于当前计算精度，未用于排样，请检查图形");
}
} // namespace
void validatePrecision(const study::Polygon &outer, const std::vector<study::Polygon> &holes,
                       study::Budget *budget) {
    ValidationWork work(budget);
    precision(outer, holes, work);
    work.finish();
}
void validateMaterial(const study::Polygon &outer, const std::vector<study::Polygon> &holes,
                      study::Budget *budget) {
    using namespace study;
    ValidationWork work(budget);
    precision(outer, holes, work);
    validatePolygon(outer, work);
    std::vector<Polygon> previous;
    for (const auto &hole : holes) {
        validatePolygon(hole, work);
        work.finish();
        require(clippedArea({hole}, {outer}, clip::ClipType::Difference) <= areaTolerance, PN2S_INVALID,
                "孔必须完整位于外轮廓内");
        require(distance({hole}, {outer}, &work) > linearTolerance, PN2S_INVALID,
                "孔不能接触或穿过外轮廓");
        for (const auto &other : previous) {
            work.step();
            require(clippedArea({hole}, {other}, clip::ClipType::Intersection) <= areaTolerance &&
                        distance({hole}, {other}, &work) > linearTolerance,
                    PN2S_INVALID, "孔不能重叠、互相包含或接触");
        }
        previous.push_back(hole);
    }
    require(materialArea(outer, holes) > areaTolerance, PN2S_INVALID, "材料净面积必须大于零");
    work.finish();
}
void validateAdditionalHole(const study::Polygon &outer,
                            const std::vector<study::Polygon> &alreadyValidatedHoles,
                            const study::Polygon &newHole, study::Budget *budget) {
    using namespace study;
    // The immutable existing material has already passed validateMaterial.
    // Keep its full quantized/floating net-area guards, without repeating any
    // old self-intersection, old/outer or old/old relation checks.
    ValidationWork work(budget);
    auto holes = alreadyValidatedHoles;
    holes.push_back(newHole);
    precision(outer, holes, work);
    validatePolygon(newHole, work);
    work.finish();
    require(clippedArea({newHole}, {outer}, clip::ClipType::Difference) <= areaTolerance,
            PN2S_INVALID, "孔必须完整位于外轮廓内");
    require(distance({newHole}, {outer}, &work) > linearTolerance, PN2S_INVALID,
            "孔不能接触或穿过外轮廓");
    for (const auto &other : alreadyValidatedHoles) {
        work.step();
        require(clippedArea({newHole}, {other}, clip::ClipType::Intersection) <= areaTolerance &&
                    distance({newHole}, {other}, &work) > linearTolerance,
                PN2S_INVALID, "孔不能重叠、互相包含或接触");
    }
    require(materialArea(outer, holes) > areaTolerance, PN2S_INVALID, "材料净面积必须大于零");
    work.finish();
}
RunResult nest(const study::Input &input, const Board &board, study::Budget &budget) {
    using namespace study;
    RunResult result;
    std::optional<Domain> domain;
    std::vector<Placed> best;
    size_t required = 0;
    for (const auto &part : input.parts) {
        require(part.quantity >= 0 && !part.poses.empty(), PN2S_INVALID, "Invalid part quantity or pose domain");
        for (const auto &pose : part.poses)
            require(angleInRange(pose, pose.angle), PN2S_INVALID, "Invalid continuous angle interval");
        required += size_t(part.quantity);
    }
    const auto stopped = [&](const Failure &error) {
        if (error.code != PN2S_TIMEOUT && error.code != PN2S_RESOURCE_LIMIT)
            throw error;
        result.stopped = true;
        result.warning = error.code == PN2S_TIMEOUT
                             ? "当前板材搜索已达到分配的时间预算，保留已验证排入的零件"
                             : "异形板搜索达到几何复杂度上限，保留已验证排入的零件";
    };
    try {
        domain.emplace(board, Box{0, 0, input.width, input.height}, budget);
        if (domain->usable.empty()) {
            result.stopped = true;
            result.warning = "板边距已用尽该板材的有效区域，未使用该板材";
            return result;
        }
        size_t stalePasses = 0;
        // The first pass retains the previous round-robin, descending-area
        // behavior. Further passes vary scan axes, group order and anchoring,
        // while sharing translation-invariant configuration-space entries.
        for (size_t pass = 0; pass < 32; ++pass) {
            budget.check(true);
            ++result.searchPasses;
            domain->policy = {bool(pass & 1), bool((pass / 8) & 1), bool((pass / 16) & 1),
                              uint32_t(pass)};
            const bool ascending = (pass / 2) & 1;
            const bool batchGroups = (pass / 4) & 1;
            std::vector<size_t> order(input.parts.size());
            std::iota(order.begin(), order.end(), size_t(0));
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                const double x = std::abs(area(input.parts[a].polygon));
                const double y = std::abs(area(input.parts[b].polygon));
                if (x != y)
                    return ascending ? x < y : x > y;
                const auto seed = input.randomSeed ^ domain->policy.variation * 0x9e3779b9U;
                return seedPriority(seed, a) < seedPriority(seed, b);
            });
            std::vector<Placed> trial;
            bool exhausted = false;
            try {
                fill(input, trial, order, batchGroups, *domain, budget);
                if (compact(input, trial, *domain, budget))
                    fill(input, trial, order, batchGroups, *domain, budget);
            } catch (const Failure &error) {
                stopped(error);
                exhausted = error.code == PN2S_TIMEOUT;
            }
            if (layoutBetter(trial, best, input)) {
                best = std::move(trial);
                result.bestPass = pass + 1;
                stalePasses = 0;
            } else {
                ++stalePasses;
            }
            if (exhausted || required <= 1 || (best.size() == required && pass >= 7 && stalePasses >= 3))
                break;
        }
    } catch (const Failure &error) {
        stopped(error);
    }
    if (domain) {
        result.pairCacheHits = domain->pairCacheHits;
        result.pairCacheMisses = domain->pairCacheMisses;
        result.continuousEvaluations = domain->continuousEvaluations;
        result.continuousRefinements = domain->continuousRefinements;
        result.continuousIntervalMinimizations = domain->continuousIntervalMinimizations;
        result.continuousSubdivisions = domain->continuousSubdivisions;
    }
    for (const auto &part : best)
        result.output.placements.push_back(part.record);
    return result;
}
Validation validate(const study::Input &input, const Board &board, const study::Output &output,
                    study::Budget *budget) {
    using namespace study;
    Validation result;
    ValidationWork work(budget);
    require(std::isfinite(board.margin) && board.margin >= 0, PN2S_INVALID, "Invalid stock border margin");
    require(board.holeMargins.empty() || board.holeMargins.size() == board.holes.size(),
            PN2S_INVALID, "Invalid stock hole margin count");
    const auto stock = material(board.outer, board.holes);
    std::vector<Placed> placed;
    std::set<std::pair<size_t, size_t>> identities;
    for (const auto &record : output.placements) {
        work.finish();
        if (record.group >= input.parts.size() ||
            record.ordinal >= size_t(input.parts[record.group].quantity) ||
            record.poseIndex >= input.parts[record.group].poses.size() ||
            !identities.emplace(record.group, record.ordinal).second) {
            result.valid = false;
            return result;
        }
        const auto &part = input.parts[record.group];
        auto pose = part.poses[record.poseIndex];
        const double actualAngle = std::isfinite(record.solvedAngle) ? record.solvedAngle :
                                   pose.angle == pose.upper ? pose.angle : std::numeric_limits<double>::quiet_NaN();
        if (!angleInRange(pose, actualAngle)) { result.valid = false; return result; }
        pose.angle = pose.upper = actualAngle;
        const Point point{std::isfinite(record.exactX) ? record.exactX : double(record.x) / 1e6,
                          std::isfinite(record.exactY) ? record.exactY : double(record.y) / 1e3};
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            result.valid = false;
            return result;
        }
        auto physical = shape(part, pose, point);
        if (std::abs(clip::Area(paths(physical))) / (scale * scale) <= areaTolerance) {
            result.valid = false;
            return result;
        }
        const auto box = bounds(physical.front());
        const auto outside = clippedArea(physical, stock, clip::ClipType::Difference);
        result.outsideArea += outside;
        if (outside > areaTolerance)
            result.valid = false;
        if (board.holeMargins.empty() && board.margin > 0) {
            const auto margin = distance(physical, stock, &work);
            result.minimumBoardMargin = std::min(result.minimumBoardMargin, margin);
            if (margin + linearTolerance < board.margin)
                result.valid = false;
        } else if (!board.holeMargins.empty()) {
            const auto margin = distance(physical, {stock.front()}, &work);
            result.minimumBoardMargin = std::min(result.minimumBoardMargin, margin);
            if (margin + linearTolerance < board.margin) result.valid = false;
            for (size_t n = 0; n < board.holeMargins.size(); ++n) {
                const auto actual = distance(physical, {stock[n + 1]}, &work);
                result.minimumBoardMargin = std::min(result.minimumBoardMargin, actual);
                if (!std::isfinite(board.holeMargins[n]) || board.holeMargins[n] < 0 ||
                    actual + linearTolerance < board.holeMargins[n]) result.valid = false;
            }
        }
        for (const auto &old : placed) {
            const double gap =
                std::max({input.gap, part.protection, input.parts[old.record.group].protection});
            if (separate(box, old.box, gap))
                continue;
            const auto overlap = clippedArea(physical, old.material, clip::ClipType::Intersection);
            result.maximumOverlapArea = std::max(result.maximumOverlapArea, overlap);
            if (overlap > areaTolerance)
                result.valid = false;
            if (gap > 0) {
                const auto actual = distance(physical, old.material, &work);
                result.minimumPartGap = std::min(result.minimumPartGap, actual);
                if (actual + linearTolerance < gap)
                    result.valid = false;
            }
        }
        placed.push_back({record, std::move(physical), {}, box});
    }
    work.finish();
    return result;
}
} // namespace icax::iplane::geometry
