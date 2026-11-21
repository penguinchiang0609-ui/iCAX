#include "core.hpp"
namespace study {
double area(const Polygon &p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        auto x = p[i], y = p[(i + 1) % p.size()];
        a += x.x * y.y - y.x * x.y;
    }
    return a * .5;
}
Box bounds(const Polygon &p) {
    require(!p.empty(), PN2S_INVALID, "Empty polygon");
    Box b{p[0].x, p[0].y, p[0].x, p[0].y};
    for (auto q : p) {
        b.x0 = std::min(b.x0, q.x);
        b.y0 = std::min(b.y0, q.y);
        b.x1 = std::max(b.x1, q.x);
        b.y1 = std::max(b.y1, q.y);
    }
    return b;
}
static double cross(Point a, Point b, Point c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
void validate_polygon(const Polygon &p) {
    require(p.size() >= 3 && p.size() <= 8192, PN2S_INVALID, "Need 3..8192 polygon vertices");
    for (size_t i = 0; i < p.size(); i++) {
        auto a = p[i], b = p[(i + 1) % p.size()];
        require(std::isfinite(a.x) && std::isfinite(a.y) && std::abs(a.x) <= 1e6 && std::abs(a.y) <= 1e6,
                PN2S_INVALID, "Invalid coordinate");
        require(a.x != b.x || a.y != b.y, PN2S_INVALID, "Zero-length edge");
        for (size_t j = i + 1; j < p.size(); j++) {
            if (j == i + 1 || (i == 0 && j == p.size() - 1))
                continue;
            auto c = p[j], d = p[(j + 1) % p.size()];
            if (std::max(std::min(a.x, b.x), std::min(c.x, d.x)) >
                    std::min(std::max(a.x, b.x), std::max(c.x, d.x)) ||
                std::max(std::min(a.y, b.y), std::min(c.y, d.y)) >
                    std::min(std::max(a.y, b.y), std::max(c.y, d.y)))
                continue;
            double u = cross(a, b, c), v = cross(a, b, d), w = cross(c, d, a), z = cross(c, d, b);
            require(!((u <= 0 && v >= 0 || u >= 0 && v <= 0) && (w <= 0 && z >= 0 || w >= 0 && z <= 0)),
                    PN2S_INVALID, "Self-intersection or self-contact");
        }
    }
    require(area(p) > 0, PN2S_INVALID, "Simple counterclockwise outer ring required");
}
Polygon transform(const Polygon &p, Point pivot, Pose pose) {
    Polygon o;
    double r = pose.angle * pi / 180, c = std::cos(r), s = std::sin(r);
    // Native orientation matrices use exact axis values at quarter turns.
    // CRT sin(pi) leaves an error that grows with the part dimensions and can
    // incorrectly reject an otherwise exact-grid reverse variant.
    const double cardinal=std::remainder(pose.angle,360.);
    if(cardinal==0.){c=1.;s=0.;}
    else if(cardinal==90.){c=0.;s=1.;}
    else if(cardinal==-90.){c=0.;s=-1.;}
    else if(cardinal==180.||cardinal==-180.){c=-1.;s=0.;}
    for (auto q : p) {
        double x = q.x - pivot.x, y = q.y - pivot.y;
        o.push_back({x * c - y * s, (pose.flip ? -1 : 1) * (x * s + y * c)});
    }
    if (pose.flip)
        std::reverse(o.begin(), o.end());
    return o;
}
static double wrap(double a) {
    while (a < 0)
        a += tau;
    while (a >= tau)
        a -= tau;
    return a;
}
// Explicit quadrant reduction avoids CRT atan2 rounding differences at an axis.
// Keep the low part of pi until the subtraction: strict angular comparisons in
// the recovered event sweep otherwise change adjacency by one representable bit.
static double tangent_angle(double y, double x) {
    constexpr double piLow = 1.2246467991473531772e-16;
    if (y == 0)
        return std::signbit(x) ? std::copysign(pi, y) : y;
    if (x == 0)
        return std::copysign(pi / 2, y);
    double z = std::atan(std::abs(y / x));
    if (!std::signbit(x))
        return std::copysign(z, y);
    return std::signbit(y) ? (z - piLow) - pi : pi - (z - piLow);
}
struct Entity {
    bool vertex, shortArc;
    int index;
    Point aPoint, bPoint;
    double a, b;
};
static std::vector<Entity> entities(const Polygon &p) {
    int n = static_cast<int>(p.size());
    std::vector<double> angles;
    for (int i = 0; i < n; i++) {
        auto a = p[i], b = p[(i + 1) % n];
        angles.push_back(wrap(tangent_angle(-(b.y - a.y), -(b.x - a.x))));
    }
    std::vector<Entity> e;
    for (int i = 0; i < n; i++) {
        double a = angles[(i + n - 1) % n], b = angles[i];
        e.push_back({true, wrap(b - a) < pi, 2 * i, p[i], p[i], a, b});
        e.push_back({false, true, 2 * i + 1, p[i], p[(i + 1) % n], b, b});
    }
    return e;
}
static bool compatible(const Entity &first, const Entity &second, bool keep) {
    const auto &x = keep ? first : second;
    const auto &y = keep ? second : first;
    double f1 = y.shortArc ? y.a : y.b, f2 = y.shortArc ? y.b : y.a, f8 = x.shortArc ? x.a : x.b,
           f3 = x.shortArc ? x.b : x.a;
    if (f2 < f1) {
        if (f3 < f8 || f8 <= f2)
            return true;
    } else {
        if (f3 < f8)
            return f1 < f3 || f8 <= f2;
        if (f2 < f8)
            return false;
    }
    return f1 < f3;
}
std::vector<Polygon> convolution(const Polygon &pa, const Polygon &pb, Budget &budget) {
    auto A = entities(pa), B = entities(pb);
    struct Event {
        double time;
        int phase, id, kind, side, index;
    };
    struct Pair {
        int a, b, next = -1, prev = -1;
    };
    std::vector<Event> events;
    auto add = [&](const std::vector<Entity> &E, int side, int id) {
        for (int i = 0; i < static_cast<int>(E.size()); i++) {
            auto e = E[i];
            double a = e.shortArc ? e.a : e.b, b = e.shortArc ? e.b : e.a;
            if (b < a)
                b += tau;
            events.push_back({a - 1e-8, 0, id++, 0, side, i});
            events.push_back({a + tau, 0, id++, 1, side, i});
            events.push_back({b + 1e-8, 1, id++, 2, side, i});
        }
    };
    add(A, 1, 0);
    add(B, 0, static_cast<int>(3 * A.size()));
    std::sort(events.begin(), events.end(), [](auto a, auto b) {
        return std::tie(a.time, a.phase, a.id) < std::tie(b.time, b.phase, b.id);
    });
    std::array<std::vector<int>, 2> active;
    std::vector<Pair> pairs;
    for (auto event : events) {
        budget.check();
        auto &act = active[event.side];
        const auto &e = (event.side ? A : B)[event.index];
        if (event.kind == 2) {
            auto it = std::find(act.begin(), act.end(), event.index);
            require(it != act.end(), PN2S_UNSUPPORTED, "Convolution event ordering");
            act.erase(it);
            continue;
        }
        for (int oi : active[1 - event.side]) {
            const auto &other = (event.side ? B : A)[oi];
            if (compatible(e, other, event.side == 0))
                pairs.push_back(event.side ? Pair{event.index, oi} : Pair{oi, event.index});
        }
        if (event.kind == 0)
            act.push_back(event.index);
    }
    std::set<std::pair<int, int>> unique;
    for (auto p : pairs)
        require(unique.emplace(p.a, p.b).second, PN2S_UNSUPPORTED,
                "Duplicate source-pair ordering unresolved");
    for (int side = 0; side < 2; side++) {
        std::vector<int> order(pairs.size());
        std::iota(order.begin(), order.end(), 0);
        auto fixed = [&](int i) { return side ? pairs[i].b : pairs[i].a; };
        auto moving = [&](int i) { return side ? pairs[i].a : pairs[i].b; };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return std::make_pair(fixed(a), moving(a)) < std::make_pair(fixed(b), moving(b));
        });
        for (size_t begin = 0; begin < order.size();) {
            size_t end = begin + 1;
            while (end < order.size() && fixed(order[end]) == fixed(order[begin]))
                end++;
            for (size_t k = begin; k < end; k++) {
                int a = order[k], b = order[k + 1 < end ? k + 1 : begin];
                if ((moving(a) + 1) % static_cast<int>(side ? A.size() : B.size()) != moving(b))
                    continue;
                bool shortArc = (side ? B : A)[fixed(a)].shortArc;
                if (shortArc) {
                    pairs[a].next = b;
                    pairs[b].prev = a;
                } else {
                    pairs[b].next = a;
                    pairs[a].prev = b;
                }
            }
            begin = end;
        }
    }
    std::vector<bool> visited(pairs.size());
    std::vector<Polygon> contours;
    for (int initial = 0; initial < static_cast<int>(pairs.size()); initial++) {
        if (visited[initial])
            continue;
        Polygon points;
        int i = initial;
        while (i >= 0 && !visited[i]) {
            budget.check();
            visited[i] = true;
            auto pair = pairs[i];
            auto a = A[pair.a], b = B[pair.b];
            if (a.vertex != b.vertex) {
                auto v = a.vertex ? a : b, e = a.vertex ? b : a;
                Point s{v.aPoint.x + e.aPoint.x, v.aPoint.y + e.aPoint.y},
                    t{v.aPoint.x + e.bPoint.x, v.aPoint.y + e.bPoint.y};
                if (!v.shortArc)
                    std::swap(s, t);
                if (points.empty()) {
                    points.push_back(s);
                    points.push_back(t);
                } else if (t.x != points.back().x || t.y != points.back().y)
                    points.push_back(t);
            }
            i = pair.next;
        }
        if (points.size() >= 2 && std::abs(points[0].x - points.back().x) < 1e-9 &&
            std::abs(points[0].y - points.back().y) < 1e-9)
            points.pop_back();
        if (points.size() >= 3)
            contours.push_back(points);
    }
    require(!contours.empty(), PN2S_UNSUPPORTED, "Empty or degenerate convolution");
    return contours;
}
Row intersection(const Row &a, const Row &b) {
    Row out;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        auto l = std::max(a[i][0], b[j][0]), u = std::min(a[i][1], b[j][1]);
        if (l <= u)
            out.push_back({l, u});
        if (a[i][1] < b[j][1])
            i++;
        else if (b[j][1] < a[i][1])
            j++;
        else {
            i++;
            j++;
        }
    }
    return out;
}
Row difference(const Row &a, const Row &b) {
    Row out = a;
    for (auto cut : b) {
        Row next;
        for (auto r : out) {
            if (cut[1] < r[0] || r[1] < cut[0]) {
                next.push_back(r);
                continue;
            }
            if (r[0] <= cut[0])
                next.push_back({r[0], std::min(r[1], cut[0])});
            if (cut[1] <= r[1])
                next.push_back({std::max(r[0], cut[1]), r[1]});
        }
        out = std::move(next);
    }
    return out;
}
static bool isTurn(const Polygon &p, size_t k) {
    size_t n = p.size();
    auto pr = p[(k + n - 1) % n], c = p[k % n], nx = p[(k + 1) % n];
    double u = nx.x - c.x, v = c.x - pr.x;
    if (u > 0 && v > 0 || u < 0 && v < 0)
        return false;
    if (u > 0 && v < 0 || u < 0 && v > 0)
        return true;
    double iy = c.y - pr.y;
    if (u > 0)
        return iy < 0;
    if (u < 0)
        return iy > 0;
    double oy = nx.y - c.y;
    if (v > 0)
        return oy < 0;
    if (v < 0)
        return oy > 0;
    return (oy > 0) == (iy < 0);
}
static bool openingEnd(const Polygon &p, size_t k) {
    size_t n = p.size();
    auto pr = p[(k + n - 1) % n], c = p[k % n], nx = p[(k + 1) % n];
    double u = nx.x - c.x, v = c.x - pr.x;
    if (u > 0 || v < 0)
        return false;
    if (u < 0) {
        if (v > 0)
            return true;
        return c.y > pr.y;
    }
    if (v > 0)
        return nx.y < c.y;
    return c.y > pr.y && nx.y < c.y;
}
static Raster rasterImpl(const std::vector<Polygon> &contours, int64_t pitch, Budget &budget,bool footprint) {
    constexpr double ex = 1e-6, ey = 1e-7;
    double h = static_cast<double>(pitch) / 1e6, minX = 1e100, maxX = -1e100, minY = 1e100;
    for (auto &p : contours)
        for (auto q : p) {
            minX = std::min(minX, q.x);
            maxX = std::max(maxX, q.x);
            minY = std::min(minY, q.y);
        }
    double gridLeft = footprint ? minX : -std::ceil(-minX / h - ey) * h, scanX = gridLeft + .5 * h;
    int firstOffset = static_cast<int>(std::floor((scanX - gridLeft - ex) / h + .5));
    double rawN = firstOffset + std::floor((maxX - scanX - ex) / h + .5) + 1;
    require(std::isfinite(rawN) && rawN > 0 && rawN <= 200000, PN2S_RESOURCE_LIMIT,
            "Convolution strip-count limit");
    int n = static_cast<int>(rawN);
    require(n > 0 && n <= 200000, PN2S_RESOURCE_LIMIT, "Convolution strip-count limit");
    double origin = scanX - (firstOffset + .5) * h;
    struct Event {
        bool opening;
        double y;
    };
    std::vector<std::vector<Event>> events(static_cast<size_t>(n));
    for (const auto &p : contours) {
        size_t start = 0;
        while (start < p.size() && !isTurn(p, start))
            start++;
        require(start < p.size(), PN2S_UNSUPPORTED, "No monotone turn");
        size_t k = start;
        while (k < p.size()) {
            Polygon chain{p[k % p.size()]};
            do {
                ++k;
                chain.push_back(p[k % p.size()]);
                require(chain.size() <= p.size() + 2, PN2S_UNSUPPORTED, "Unclosed contour");
            } while (!isTurn(p, k));
            bool opening = openingEnd(p, k);
            if (!opening)
                std::reverse(chain.begin(), chain.end());
            int lo = static_cast<int>(std::floor((chain[0].x - origin + ex) / h)),
                last = static_cast<int>(std::floor((chain.back().x - origin - ex) / h)) + 1;
            int begin = std::max(0, lo), end = std::min(n, std::max(lo, last));
            Point prev = chain[0];
            size_t nextIndex = 1;
            double rawY = prev.y;
            for (int i = begin; i < end; i++) {
                budget.check();
                double upper = (i + 1) * h + origin, minYrow = rawY, maxYrow = rawY, value = rawY;
                while (nextIndex < chain.size()) {
                    Point next = chain[nextIndex];
                    if (upper <= next.x + (opening ? -ex : ex)) {
                        double y = prev.y, dx = next.x - prev.x;
                        if (dx > ex)
                            y = (next.y - prev.y) / dx * (upper - prev.x) + prev.y;
                        y = std::clamp(y, std::min(prev.y, next.y), std::max(prev.y, next.y));
                        rawY = y;
                        minYrow = std::min(minYrow, y);
                        maxYrow = std::max(maxYrow, y);
                        value = opening ? minYrow : maxYrow;
                        break;
                    }
                    rawY = next.y;
                    minYrow = std::min(minYrow, rawY);
                    maxYrow = std::max(maxYrow, rawY);
                    value = opening ? minYrow : maxYrow;
                    prev = next;
                    nextIndex++;
                }
                events[i].push_back({opening, value});
            }
        }
    }
    Rows integer;
    for (auto &es : events) {
        std::stable_sort(es.begin(), es.end(), [](auto a, auto b) {
            return a.y < b.y || (a.y == b.y && a.opening < b.opening);
        });
        std::vector<std::array<double, 2>> floating;
        int winding = 0;
        double start = std::numeric_limits<double>::quiet_NaN();
        for (auto e : es) {
            if (e.opening) {
                if (++winding == 1)
                    start = e.y;
            } else if (--winding == 0) {
                require(std::isfinite(start), PN2S_UNSUPPORTED, "Unbalanced raster events");
                if (!floating.empty() && start - (minY + floating.back()[1]) < .001)
                    floating.back()[1] = e.y - minY;
                else
                    floating.push_back({start - minY, e.y - minY});
            }
        }
        Row row;
        for (auto r : floating)
            row.push_back({static_cast<int64_t>(std::floor((r[0] + ey) * 1000)),
                           static_cast<int64_t>(std::ceil((r[1] - ey) * 1000))});
        integer.push_back(std::move(row));
    }
    Raster result;
    if (footprint) result.rows=std::move(integer);
    else {
        result.rows.resize(integer.size());
        for (size_t i = 1; i < integer.size(); i++)
            result.rows[i] = intersection(integer[i - 1], integer[i]);
    }
    result.offsetStrip = static_cast<int64_t>(
        std::floor(static_cast<double>(rounded(-gridLeft * 1e6)) / static_cast<double>(pitch)));
    result.offsetY = rounded(-minY * 1000);
    return result;
}
Raster raster(const std::vector<Polygon> &contours,int64_t pitch,Budget &budget) {
    return rasterImpl(contours,pitch,budget,false);
}
Raster rasterFootprint(const std::vector<Polygon> &contours,int64_t pitch,Budget &budget) {
    return rasterImpl(contours,pitch,budget,true);
}
} // namespace study
