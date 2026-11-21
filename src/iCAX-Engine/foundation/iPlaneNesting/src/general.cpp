#include "clipper2/clipper.h"
#include "core.hpp"
#include <functional>

namespace study {
namespace {
namespace clip = Clipper2Lib;
constexpr double scale = 1e8, tolerance = 1e-7;
using Region = std::vector<Polygon>;
double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
clip::Paths64 integer_paths(const Region &rings) {
    clip::Paths64 result;
    for (const auto &ring : rings) {
        clip::Path64 p;
        for (auto q : ring)
            p.emplace_back(rounded(q.x * scale), rounded(q.y * scale));
        result.push_back(std::move(p));
    }
    return result;
}
Region real_paths(const clip::Paths64 &paths) {
    Region result;
    for (const auto &p : paths) {
        Polygon ring;
        for (auto q : p)
            ring.push_back({q.x / scale, q.y / scale});
        result.push_back(std::move(ring));
    }
    return result;
}
double clipped_area(const Region &a, const Region &b, clip::ClipType op) {
    return std::abs(clip::Area(
               clip::BooleanOp(op, clip::FillRule::NonZero, integer_paths(a), integer_paths(b)))) /
           (scale * scale);
}
Region region(const PartInput &part, Pose pose, Point position = {0, 0}) {
    Region result{transform(part.polygon, part.pivot, pose)};
    for (auto hole : part.holes) {
        auto h = transform(hole, part.pivot, pose);
        if (area(h) > 0)
            std::reverse(h.begin(), h.end());
        result.push_back(std::move(h));
    }
    for (auto &ring : result)
        for (auto &p : ring) {
            p.x += position.x;
            p.y += position.y;
        }
    return result;
}
Region translated(Region a, Point offset) {
    for (auto &p : a)
        for (auto &q : p) {
            q.x += offset.x;
            q.y += offset.y;
        }
    return a;
}
double point_segment_distance(Point p, Point a, Point b) {
    double x = b.x - a.x, y = b.y - a.y, len = x * x + y * y;
    double t = len ? std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / len, 0.0, 1.0) : 0;
    return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
}
bool proper_intersection(Point a, Point b, Point c, Point d) {
    double x = cross(a, b, c), y = cross(a, b, d), z = cross(c, d, a), w = cross(c, d, b);
    return ((x > 0 && y < 0) || (x < 0 && y > 0)) && ((z > 0 && w < 0) || (z < 0 && w > 0));
}
double boundary_distance(const Region &a, const Region &b) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto &ra : a)
        for (const auto &rb : b)
            for (size_t i = 0; i < ra.size(); i++)
                for (size_t j = 0; j < rb.size(); j++) {
                    Point p = ra[i], q = ra[(i + 1) % ra.size()], r = rb[j], s = rb[(j + 1) % rb.size()];
                    if (proper_intersection(p, q, r, s))
                        return 0;
                    best =
                        std::min({best, point_segment_distance(p, r, s), point_segment_distance(q, r, s),
                                  point_segment_distance(r, p, q), point_segment_distance(s, p, q)});
                }
    return best;
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
    for (auto p : points) {
        while (result.size() > 1 && cross(result[result.size() - 2], result.back(), p) <= 0)
            result.pop_back();
        result.push_back(p);
    }
    const size_t lower = result.size();
    for (size_t i = points.size() - 1; i-- > 0;) {
        auto p = points[i];
        while (result.size() > lower && cross(result[result.size() - 2], result.back(), p) <= 0)
            result.pop_back();
        result.push_back(p);
    }
    result.pop_back();
    return result;
}
// Vertical material decomposition: outer and hole boundaries cut every slab.
// Pairing sorted intersections emits convex trapezoids of material, never voids.
Region decompose(const Region &rings) {
    std::vector<double> xs;
    for (const auto &ring : rings)
        for (auto p : ring)
            xs.push_back(p.x);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    Region cells;
    struct Edge {
        Point a, b;
        double middle;
    };
    for (size_t i = 1; i < xs.size(); i++) {
        double l = xs[i - 1], r = xs[i];
        if (r - l < 1e-10)
            continue;
        double mid = (l + r) / 2;
        std::vector<Edge> edges;
        for (const auto &ring : rings)
            for (size_t k = 0; k < ring.size(); k++) {
                auto a = ring[k], b = ring[(k + 1) % ring.size()];
                if (std::min(a.x, b.x) < mid && mid < std::max(a.x, b.x))
                    edges.push_back({a, b, a.y + (b.y - a.y) * (mid - a.x) / (b.x - a.x)});
            }
        std::sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.middle < b.middle; });
        require(edges.size() % 2 == 0, PN2S_INTERNAL, "Unbalanced material slab");
        auto at = [](const Edge &e, double x) {
            return e.a.y + (e.b.y - e.a.y) * (x - e.a.x) / (e.b.x - e.a.x);
        };
        for (size_t k = 0; k < edges.size(); k += 2) {
            auto cell = hull({{l, at(edges[k], l)},
                              {r, at(edges[k], r)},
                              {r, at(edges[k + 1], r)},
                              {l, at(edges[k + 1], l)}});
            if (cell.size() >= 3 && area(cell) > 1e-12)
                cells.push_back(std::move(cell));
        }
    }
    return cells;
}
Region forbidden(const Region &fixedCells, const Region &movingCells, double gap, Budget &budget) {
    clip::Paths64 sums;
    require(fixedCells.size() * movingCells.size() <= 200000, PN2S_RESOURCE_LIMIT,
            "Material cell pair limit");
    for (const auto &a : fixedCells)
        for (const auto &b : movingCells) {
            budget.check();
            Polygon points;
            for (auto p : a)
                for (auto q : b)
                    points.push_back({p.x - q.x, p.y - q.y});
            auto p = hull(std::move(points));
            if (p.size() > 2)
                sums.push_back(integer_paths({p})[0]);
        }
    auto result = clip::Union(sums, clip::FillRule::NonZero);
    if (gap > 0)
        result = clip::InflatePaths(result, gap * scale, clip::JoinType::Round, clip::EndType::Polygon,
                                    2.0, .000001 * scale);
    return real_paths(result);
}
struct Placed {
    Placement record;
    Region rings;
    mutable Region cells;
    Box box;
};
// A connected part can occupy another part's void only if its entire outer
// boundary fits there. When neither direction can fit even the hole bounds,
// filling both holes leaves the forbidden translations unchanged. This avoids
// a quadratic product of hundreds of circle-hole slab cells on real plates.
bool can_enter_hole(const Region &container, const Box &moving) {
    for (size_t i = 1; i < container.size(); i++) {
        auto b = bounds(container[i]);
        if (moving.x1 - moving.x0 <= b.x1 - b.x0 + tolerance &&
            moving.y1 - moving.y0 <= b.y1 - b.y0 + tolerance)
            return true;
    }
    return false;
}
struct Candidate {
    bool valid = false;
    size_t range = 0;
    double angle = 0, x = 0, y = 0, score = 0;
    Region rings;
};
bool feasible(const Input &input, size_t group, const Region &rings, const Box &box,
              const std::vector<Placed> &placed) {
    if (box.x0 < -tolerance || box.y0 < -tolerance || box.x1 > input.width + tolerance ||
        box.y1 > input.height + tolerance)
        return false;
    for (const auto &p : placed) {
        const double gap = std::max({input.gap, input.parts[group].protection,
                                    input.parts[p.record.group].protection});
        if (box.x1 + gap <= p.box.x0 + tolerance || p.box.x1 + gap <= box.x0 + tolerance ||
            box.y1 + gap <= p.box.y0 + tolerance || p.box.y1 + gap <= box.y0 + tolerance)
            continue;
        const bool nesting = can_enter_hole(p.rings, box) || can_enter_hole(rings, p.box);
        const Region a = nesting ? rings : Region{rings[0]};
        const Region b = nesting ? p.rings : Region{p.rings[0]};
        if (clipped_area(a, b, clip::ClipType::Intersection) > 1e-8)
            return false;
        if (gap > 0 && boundary_distance(a, b) + tolerance < gap)
            return false;
    }
    return true;
}
void add_point(std::vector<Point> &out, Point p, Box legal) {
    if (p.x >= legal.x0 - tolerance && p.x <= legal.x1 + tolerance && p.y >= legal.y0 - tolerance &&
        p.y <= legal.y1 + tolerance)
        out.push_back({std::clamp(p.x, legal.x0, legal.x1), std::clamp(p.y, legal.y0, legal.y1)});
}
void edge_intersection(std::vector<Point> &out, Point a, Point b, Point c, Point d, Box legal) {
    double ux = b.x - a.x, uy = b.y - a.y, vx = d.x - c.x, vy = d.y - c.y, den = ux * vy - uy * vx;
    if (std::abs(den) < 1e-14)
        return;
    double wx = c.x - a.x, wy = c.y - a.y, t = (wx * vy - wy * vx) / den, s = (wx * uy - wy * ux) / den;
    if (t >= -1e-10 && t <= 1 + 1e-10 && s >= -1e-10 && s <= 1 + 1e-10)
        add_point(out, {a.x + t * ux, a.y + t * uy}, legal);
}
Candidate position(const Input &input, size_t group, size_t range, double angle,
                   const std::vector<Placed> &placed, Budget &budget, bool contactsOnly = false) {
    budget.check();
    const auto &part = input.parts[group];
    Pose pose{angle, part.poses[range].flip};
    auto rings = region(part, pose);
    auto box = bounds(rings[0]);
    Box legal{-box.x0, -box.y0, input.width - box.x1, input.height - box.y1};
    if (legal.x1 < legal.x0 - tolerance || legal.y1 < legal.y0 - tolerance)
        return {};
    legal.x1 = std::max(legal.x0, legal.x1);
    legal.y1 = std::max(legal.y0, legal.y1);
    Polygon boundary{
        {legal.x0, legal.y0}, {legal.x1, legal.y0}, {legal.x1, legal.y1}, {legal.x0, legal.y1}};
    std::vector<Point> candidates = boundary;
    // The bottom-left legal position has the minimum possible score. Test it
    // before creating any configuration-space polygons (especially first part).
    Point origin{legal.x0, legal.y0};
    auto originRings = translated(rings, origin);
    Box originBox{box.x0 + origin.x, box.y0 + origin.y, box.x1 + origin.x, box.y1 + origin.y};
    double currentLength = 0;
    for (const auto &p : placed)
        currentLength = std::max(currentLength, p.box.x1);
    auto score = [&](Point p) {
        return std::max(currentLength, p.x + box.x1) + 1e-5 * (p.y + box.y0) + 1e-7 * (p.x + box.x0);
    };
    if (feasible(input, group, originRings, originBox, placed))
        return {true, range, angle, origin.x, origin.y, score(origin), std::move(originRings)};
    Region cells, outerCells;
    Region allForbidden;
    for (const auto &p : placed) {
        const double gap = std::max({input.gap, part.protection,
                                    input.parts[p.record.group].protection});
        for (double x : {legal.x0, p.box.x0 - gap - box.x1,
                         p.box.x1 + gap - box.x0, p.box.x0 - box.x0, p.box.x1 - box.x1})
            for (double y : {legal.y0, p.box.y0 - gap - box.y1,
                             p.box.y1 + gap - box.y0, p.box.y0 - box.y0, p.box.y1 - box.y1})
                add_point(candidates, {x, y}, legal);
        if (!contactsOnly) {
            const bool nesting = can_enter_hole(p.rings, box) || can_enter_hole(rings, p.box);
            if (nesting && cells.empty()) cells = decompose(rings);
            if (!nesting && outerCells.empty()) outerCells = decompose({rings[0]});
            if (nesting && p.cells.empty()) p.cells = decompose(p.rings);
            auto fixedCells = nesting ? p.cells : decompose({p.rings[0]});
            auto nfp = forbidden(fixedCells, nesting ? cells : outerCells, gap, budget);
            for (const auto &ring : nfp) {
                for (auto q : ring)
                    add_point(candidates, q, legal);
                allForbidden.push_back(ring);
            }
        }
        // Keep contacts with the hole's extremal supporting lines. A snug fit
        // can have zero area in translation space and vanish from Boolean output.
        for (size_t h = 1; h < p.rings.size(); h++) {
            auto voidBox = bounds(p.rings[h]);
            for (double x : {voidBox.x0 + gap - box.x0, voidBox.x1 - gap - box.x1})
                for (double y : {voidBox.y0 + gap - box.y0, voidBox.y1 - gap - box.y1})
                    add_point(candidates, {x, y}, legal);
        }
        // Zero-width feasible positions can disappear from a polygon Boolean.
        // Keep original vertex contacts, including the vertices of inner rings.
        if (gap == 0 && !contactsOnly)
            for (const auto &fixed : p.rings)
                for (const auto &moving : rings)
                    for (auto a : fixed)
                        for (auto b : moving)
                            add_point(candidates, {a.x - b.x, a.y - b.y}, legal);
    }
    if (!allForbidden.empty()) {
        auto united = real_paths(clip::Union(integer_paths(allForbidden), clip::FillRule::NonZero));
        for (const auto &ring : united)
            for (auto q : ring)
                add_point(candidates, q, legal);
        if (legal.x1 - legal.x0 > 1e-10 && legal.y1 - legal.y0 > 1e-10) {
            auto free = real_paths(clip::Difference(integer_paths({boundary}), integer_paths(united),
                                                    clip::FillRule::NonZero));
            for (const auto &ring : free)
                for (auto q : ring)
                    add_point(candidates, q, legal);
        }
        for (const auto &ring : allForbidden)
            for (size_t k = 0; k < ring.size(); k++)
                for (size_t j = 0; j < 4; j++)
                    edge_intersection(candidates, ring[k], ring[(k + 1) % ring.size()], boundary[j],
                                      boundary[(j + 1) % 4], legal);
    }
    require(candidates.size() <= 300000, PN2S_RESOURCE_LIMIT, "Configuration-space candidate limit");
    std::sort(candidates.begin(), candidates.end(), [&](Point a, Point b) {
        double x = score(a), y = score(b);
        return x < y || (x == y && (a.x < b.x || (a.x == b.x && a.y < b.y)));
    });
    std::set<std::pair<int64_t, int64_t>> seen;
    for (auto p : candidates) {
        budget.check();
        if (!seen.emplace(rounded(p.x * scale), rounded(p.y * scale)).second)
            continue;
        auto physical = translated(rings, p);
        Box b{box.x0 + p.x, box.y0 + p.y, box.x1 + p.x, box.y1 + p.y};
        if (feasible(input, group, physical, b, placed))
            return {true, range, angle, p.x, p.y, score(p), std::move(physical)};
    }
    return {};
}
std::vector<double> significant_directions(const Region &rings, size_t limit) {
    std::vector<std::pair<double, double>> edges;
    for (const auto &ring : rings)
        for (size_t i = 0; i < ring.size(); i++) {
            auto a = ring[i], b = ring[(i + 1) % ring.size()];
            edges.emplace_back(std::hypot(b.x - a.x, b.y - a.y),
                               std::atan2(b.y - a.y, b.x - a.x) * 180 / pi);
        }
    std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.first > b.first; });
    std::vector<double> result;
    for (auto [length, direction] : edges) {
        (void)length;
        if (std::none_of(result.begin(), result.end(), [&](double a) { return std::abs(a - direction) < 1e-7; }))
            result.push_back(direction);
        if (result.size() == limit) break;
    }
    return result;
}
std::vector<double> angle_seeds(const Input &input, const PartInput &part, size_t index,
                                const std::vector<Placed> &placed) {
    const auto &p = part.poses[index];
    std::vector<double> result{p.angle};
    if (p.upper == p.angle)
        return result;
    auto add = [&](double a) {
        if (a >= p.angle - 1e-9 && a <= p.upper + 1e-9)
            result.push_back(std::clamp(a, p.angle, p.upper));
    };
    add(p.upper);
    add((p.angle + p.upper) / 2);
    const int steps = std::max(1, static_cast<int>(std::ceil((p.upper - p.angle) / 15)));
    for (int i = 1; i < steps; i++)
        add(p.angle + (p.upper - p.angle) * i / steps);
    std::vector<double> fixedDirections{0, 90, 180, 270};
    Region relevant;
    for (const auto &fixed : placed)
        for (size_t h = 0; h < fixed.rings.size(); h++) {
            const auto &ring = fixed.rings[h];
            if (h > 0 && std::abs(area(part.polygon)) > std::abs(area(ring)) + tolerance)
                continue;
            relevant.push_back(ring);
        }
    auto contacts = significant_directions(relevant, 24);
    fixedDirections.insert(fixedDirections.end(), contacts.begin(), contacts.end());
    // Bound search effort by significant physical edges, not by replacing the
    // continuous allowed angle domain with a fixed-angle input restriction.
    for (double direction : significant_directions({part.polygon}, 16)) {
        for (double target : fixedDirections) {
            double base = (p.flip ? -target : target) - direction;
            int start = static_cast<int>(std::floor((p.angle - base) / 360));
            for (int k = start; k <= start + 2; k++)
                add(base + 360 * k);
        }
    }
    // Continuous width/height extrema between contact angles. This is numerical
    // minimization in an interval, not rounding the permitted range to a grid.
    std::vector<Point> windows{{input.width, input.height}};
    for (const auto &fixed : placed)
        for (size_t h = 1; h < fixed.rings.size(); h++) {
            if (std::abs(area(part.polygon)) > std::abs(area(fixed.rings[h])) + tolerance)
                continue;
            auto b = bounds(fixed.rings[h]);
            windows.push_back({b.x1 - b.x0, b.y1 - b.y0});
        }
    std::sort(windows.begin(), windows.end(), [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    windows.erase(std::unique(windows.begin(), windows.end(), [](Point a, Point b) { return std::abs(a.x-b.x)<tolerance && std::abs(a.y-b.y)<tolerance; }), windows.end());
    for (auto window : windows) {
        auto cost = [&](double a) {
            auto b = bounds(transform(part.polygon, part.pivot, {a, p.flip}));
            double w = b.x1 - b.x0, h = b.y1 - b.y0;
            return w + 1e4 * (std::max(0.0, w - window.x) + std::max(0.0, h - window.y));
        };
        for (int i = 0; i < steps; i++) {
            double lo = p.angle + (p.upper - p.angle) * i / steps,
                   hi = p.angle + (p.upper - p.angle) * (i + 1) / steps;
            constexpr double ratio = .6180339887498948482;
            double a = hi - ratio * (hi - lo), b = lo + ratio * (hi - lo), fa = cost(a), fb = cost(b);
            for (int j = 0; j < 42; j++) {
                if (fa < fb) {
                    hi = b;
                    b = a;
                    fb = fa;
                    a = hi - ratio * (hi - lo);
                    fa = cost(a);
                } else {
                    lo = a;
                    a = b;
                    fa = fb;
                    b = lo + ratio * (hi - lo);
                    fb = cost(b);
                }
            }
            add((lo + hi) / 2);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end(),
                             [](double a, double b) { return std::abs(a - b) < 1e-8; }),
                 result.end());
    require(result.size() <= 4096, PN2S_RESOURCE_LIMIT, "Continuous-angle seed limit");
    return result;
}
Candidate choose(const Input &input, size_t group, const std::vector<Placed> &placed, Budget &budget) {
    const auto &part = input.parts[group];
    Candidate best;
    for (size_t range = 0; range < part.poses.size(); range++) {
        auto seeds = angle_seeds(input, part, range, placed);
        Candidate local;
        // Screen all feature angles with inexpensive supporting-line contacts.
        // Only promising poses need the full material Minkowski construction.
        // Geometry and continuous intervals are unchanged; this is search order.
        const bool screened = input.fastConstruction || (seeds.size() > 48 && !placed.empty());
        std::vector<std::pair<double, double>> ranked;
        if (screened) {
            for (double a : seeds) {
                auto c = position(input, group, range, a, placed, budget, true);
                if (c.valid) {
                    ranked.emplace_back(c.score, a);
                    if (!local.valid || c.score < local.score) local = std::move(c);
                } else {
                    auto b = bounds(transform(part.polygon, part.pivot, {a, part.poses[range].flip}));
                    ranked.emplace_back(input.width + input.height + b.x1 - b.x0, a);
                }
            }
            std::stable_sort(ranked.begin(), ranked.end());
            seeds.clear();
            if (!input.fastConstruction || !local.valid)
                for (size_t i = 0; i < std::min<size_t>(4, ranked.size()); i++) seeds.push_back(ranked[i].second);
        }
        for (double a : seeds) {
            auto c = position(input, group, range, a, placed, budget);
            if (c.valid && (!local.valid || c.score < local.score - 1e-10))
                local = std::move(c);
        }
        if (local.valid && part.poses[range].upper > part.poses[range].angle) {
            double step = std::min(7.5, (part.poses[range].upper - part.poses[range].angle) / 4);
            for (int iteration = 0; iteration < 70 && step > 1e-6; iteration++) {
                bool improved = false;
                double center = local.angle;
                for (int sign : {-1, 1}) {
                    double a = std::clamp(center + sign * step, part.poses[range].angle,
                                          part.poses[range].upper);
                    if (a == center)
                        continue;
                    auto c = position(input, group, range, a, placed, budget, screened);
                    if (c.valid && c.score < local.score - 1e-9) {
                        local = std::move(c);
                        improved = true;
                    }
                }
                if (!improved)
                    step *= .5;
            }
        }
        if (screened && local.valid && !input.fastConstruction) {
            auto c = position(input, group, range, local.angle, placed, budget);
            if (c.valid && c.score < local.score) local = std::move(c);
        }
        if (local.valid && (!best.valid || local.score < best.score - 1e-10))
            best = std::move(local);
    }
    return best;
}
void compact(const Input &source, std::vector<Placed> &placed, Budget &parent) {
    if (placed.size() < 2) return;
    const double left = std::chrono::duration<double>(parent.deadline-std::chrono::steady_clock::now()).count();
    if (left <= .001) return;
    Budget budget(std::min(8.0,left*.2));
    Input input = source;
    input.fastConstruction = false;
    auto metrics = [](const std::vector<Placed> &parts) {
        std::array<double,3> m{};
        for (const auto &p : parts) {
            m[0] = std::max(m[0],p.box.x1);
            m[1] = std::max(m[1],p.box.y1);
            m[2] += p.box.x0+p.box.y0;
        }
        return m;
    };
    auto better = [](const auto &a, const auto &b) {
        for (size_t k = 0; k < 3; k++) {
            if (a[k] < b[k]-1e-6) return true;
            if (a[k] > b[k]+1e-6) return false;
        }
        return false;
    };
    try {
        for (int pass = 0; pass < 4; pass++) {
            bool improved = false;
            std::vector<size_t> order(placed.size());
            std::iota(order.begin(),order.end(),0);
            std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b) {
                return placed[a].box.x1 > placed[b].box.x1;
            });
            for (size_t index : order) {
                budget.check(true);
                const auto oldMetric = metrics(placed);
                auto others = placed;
                auto old = others[index];
                others.erase(others.begin()+static_cast<ptrdiff_t>(index));
                auto c = choose(input,old.record.group,others,budget);
                if (!c.valid) continue;
                auto record = old.record;
                record.poseIndex=c.range;record.solvedAngle=c.angle;
                record.exactX=c.x;record.exactY=c.y;
                record.x=rounded(c.x*1e6);record.y=rounded(c.y*1000);
                auto box = bounds(c.rings[0]);
                Placed next{record,std::move(c.rings),{},box};
                others.push_back(next);
                if (better(metrics(others),oldMetric)) {
                    placed[index]=std::move(next);
                    improved=true;
                }
            }
            if (!improved) break;
        }
    } catch (const Failure &e) {
        // Every accepted move already passed full material feasibility. A
        // bounded local search retains those moves and the complete placement.
        if (e.code != PN2S_TIMEOUT && e.code != PN2S_RESOURCE_LIMIT) throw;
    }
}
} // namespace

double material_area(const PartInput &part) {
    double value = area(part.polygon);
    for (const auto &h : part.holes)
        value -= std::abs(area(h));
    return value;
}
void validate_hole(const Polygon &outer, const std::vector<Polygon> &holes, const Polygon &hole) {
    require(clipped_area({hole}, {outer}, clip::ClipType::Difference) < 1e-10, PN2S_INVALID,
            "Hole must be contained in its outer ring");
    require(boundary_distance({hole}, {outer}) > tolerance, PN2S_INVALID,
            "Hole touches or crosses its outer ring");
    for (const auto &h : holes) {
        const auto a = bounds(hole), b = bounds(h);
        if (a.x1 + tolerance < b.x0 || b.x1 + tolerance < a.x0 ||
            a.y1 + tolerance < b.y0 || b.y1 + tolerance < a.y0)
            continue;
        require(clipped_area({hole}, {h}, clip::ClipType::Intersection) < 1e-10, PN2S_INVALID,
                "Holes overlap or contain one another");
        require(boundary_distance({hole}, {h}) > tolerance, PN2S_INVALID, "Holes touch one another");
    }
}
bool requires_general(const Input &input) {
    for (const auto &p : input.parts) {
        if (!p.holes.empty() || p.protection > 0)
            return true;
        for (auto pose : p.poses) {
            if (pose.upper != pose.angle)
                return true;
            for (auto q : transform(p.polygon, p.pivot, pose))
                if (!grid_exact(q.x, 1e6) || !grid_exact(q.y, 1000))
                    return true;
        }
    }
    return false;
}
bool valid_layout(const Input &input, const Output &output) {
    std::vector<Placed> placed;
    for (const auto &record : output.placements) {
        const auto &part = input.parts.at(record.group);
        auto pose = part.poses.at(record.poseIndex);
        if (std::isfinite(record.solvedAngle))
            pose.angle = record.solvedAngle;
        const Point position{std::isfinite(record.exactX) ? record.exactX : record.x / 1e6,
                             std::isfinite(record.exactY) ? record.exactY : record.y / 1000.};
        auto rings = region(part, pose, position);
        const auto box = bounds(rings.front());
        if (!feasible(input, record.group, rings, box, placed))
            return false;
        placed.push_back({record, std::move(rings), {}, box});
    }
    return true;
}
Output general_nest(const Input &input, Budget &budget) {
    std::vector<Placed> placed;
    std::vector<int> used(input.parts.size());
    std::vector<size_t> order(input.parts.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        double x = area(input.parts[a].polygon), y = area(input.parts[b].polygon);
        return x > y;
    });
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t group : order) {
            if (used[group] >= input.parts[group].quantity)
                continue;
            budget.check(true);
            auto candidate = choose(input, group, placed, budget);
            if (!candidate.valid)
                continue;
            Placement record;
            record.group = group;
            record.ordinal = static_cast<size_t>(used[group]++);
            record.poseIndex = candidate.range;
            record.exactX = candidate.x;
            record.exactY = candidate.y;
            record.solvedAngle = candidate.angle;
            record.x = rounded(candidate.x * 1e6);
            record.y = rounded(candidate.y * 1000);
            auto box = bounds(candidate.rings[0]);
            placed.push_back({record, std::move(candidate.rings), {}, box});
            progress = true;
        }
    }
    Output result;
    compact(input,placed,budget);
    for (const auto &p : placed)
        result.placements.push_back(p.record);
    return result;
}
} // namespace study
