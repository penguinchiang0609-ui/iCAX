#include "rectangle_runtime.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include <functional>
namespace study {
namespace {
constexpr double eps = 1e-9;
using Dims = std::array<double, 3>;
using Counts = std::array<int, 3>;
using Axes = std::vector<int>;
template <class T> static auto product(const T &v) {
    typename T::value_type r = 1;
    for (auto x : v)
        r *= x;
    return r;
}
bool near(double a, double b) {
    return std::abs(a - b) < eps;
}
struct Module {
    int id, group;
    size_t pose;
    Dims b, p;
    Box box;
    std::vector<std::pair<size_t, int>> composition;
    std::vector<Placement> members;
    Counts maximum{1000, 1000, 1};
};
struct Node {
    int module = -1, axis = 0;
    Counts n{};
    Dims size{};
    std::vector<std::shared_ptr<Node>> children;
};
using Tree = std::shared_ptr<Node>;
struct Item {
    int module, q, qMax, cross, qty;
    Counts counts;
    double value, weight, density;
};
struct Cut {
    int axis;
    double t, density, score = 0;
};
struct Problem {
    const Input &input;
    Budget &budget;
    std::vector<Module> modules;
    std::vector<int> used;
    std::vector<bool> participating;
    std::vector<double> price;
    std::vector<double> modulePrice;
    Dims D;
    Problem(const Input &in, Budget &b, const std::vector<RectangleTilingModel> &models)
        : input(in), budget(b), used(in.parts.size()), participating(in.parts.size()), D{in.width + in.gap, in.height + in.gap, 1} {
        for (size_t i = 0; i < in.parts.size(); i++) {
            auto &p = in.parts[i];
            auto box = bounds(p.polygon);
            price.push_back(std::max(.001, area(p.polygon)));
            if (!models.empty()) continue;
            require(p.polygon.size() == 4 && p.pivot.x == 0 && p.pivot.y == 0 && box.x0 == 0 &&
                        box.y0 == 0,
                    PN2S_UNSUPPORTED, "Rectangle strategy needs zero-origin rectangle and pivot");
            for (size_t j = 0; j < 4; j++) {
                auto a = p.polygon[j], c = p.polygon[(j + 1) % 4];
                require(a.x == c.x || a.y == c.y, PN2S_UNSUPPORTED,
                        "Rectangle strategy requires axis-aligned rectangles");
            }
            for (size_t j = 0; j < p.poses.size(); j++) {
                auto pose = p.poses[j];
                require(!pose.flip && (pose.angle == 0 || pose.angle == 90), PN2S_UNSUPPORTED,
                        "Rectangle strategy supports supplied 0/90 degree poses without mirrors");
                Box box2 = pose.angle == 0 ? box : Box{-box.y1, 0, 0, box.x1};
                double w = box2.x1 - box2.x0, h = box2.y1 - box2.y0;
                modules.push_back({static_cast<int>(modules.size()),
                                   static_cast<int>(i),
                                   j,
                                   {w + in.gap, h + in.gap, 1},
                                   {w + in.gap, h + in.gap, 1},
                                   box2});
                modules.back().composition.push_back({i, 1});
                participating[i] = true;
            }
        }
        for (const auto &model : models) {
            require(!model.members.empty() && std::abs(model.u.y) < eps && std::abs(model.v.x) < eps &&
                    model.u.x >= 0. && model.v.y >= 0., PN2S_UNSUPPORTED,
                    "Rectangle module requires nonnegative axis-aligned repetition vectors");
            const auto box = rectangleModelBounds(in, model);
            std::map<size_t, int> composition;
            for (const auto &member : model.members) ++composition[member.group];
            Module module{int(modules.size()), int(composition.begin()->first), model.members.front().poseIndex,
                          {box.x1 - box.x0 + in.gap, box.y1 - box.y0 + in.gap, 1.},
                          {model.u.x, model.v.y, 1.}, box};
            module.composition.assign(composition.begin(), composition.end()); module.members = model.members;
            for (const auto [group, coefficient] : composition) { (void)coefficient; participating[group] = true; }
            module.maximum = {model.maximumX == 0 ? 1000 : std::max(1, model.maximumX),
                              model.maximumY == 0 ? 1000 : std::max(1, model.maximumY), 1};
            require(model.maximumTotal == 0, PN2S_UNSUPPORTED,
                    "Rectangle total repetition limit must be expanded into its original two modules");
            modules.push_back(std::move(module));
        }
    }
    int remaining(int m) const {
        int result = INT32_MAX;
        for (const auto [group, coefficient] : modules[m].composition)
            result = std::min(result, (input.parts[group].quantity - used[group]) / coefficient);
        return result;
    }
    void updateModule(int module, int count) {
        for (const auto [group, coefficient] : modules[module].composition) {
            used[group] += count * coefficient;
            require(used[group] >= 0 && used[group] <= input.parts[group].quantity, PN2S_INTERNAL,
                    "Rectangle demand state corrupted");
        }
    }
    double priceOf(int module) const {
        if (!modulePrice.empty()) return modulePrice.at(module);
        double result = 0.;
        for (const auto [group, coefficient] : modules[module].composition) result += coefficient * price[group];
        return result;
    }
    int maxCount(int m, int a, double d) const {
        const auto &v = modules[m];
        if (v.b[a] - d > eps)
            return 0;
        double r = d - v.b[a];
        if (r < eps)
            return 1;
        double q = v.p[a] > eps ? std::floor((r + eps) / v.p[a]) : 0;
        return static_cast<int>(std::min(q + 1, double(v.maximum[a])));
    }
    Counts fitCounts(int m, const Dims &d) const {
        Counts n{};
        int chosen = 1, u = remaining(m);
        for (int a = 0; a < 3; a++) {
            int x = std::min(maxCount(m, a, d[a]), u / chosen);
            if (x < 1)
                return {};
            n[a] = x;
            chosen *= x;
        }
        return n;
    }
    std::vector<int> eligible(const std::vector<int> &mods, const Dims &d) const {
        std::vector<int> out;
        for (int m : mods)
            if (remaining(m) > 0 && modules[m].b[0] - d[0] <= eps && modules[m].b[1] - d[1] <= eps &&
                modules[m].b[2] - d[2] <= eps)
                out.push_back(m);
        return out;
    }
    std::vector<Item> items(const std::vector<int> &mods, const Dims &d, const Axes &axes) {
        std::vector<Item> out;
        for (int m : eligible(mods, d)) {
            budget.check();
            auto low = d;
            for (int a : axes)
                low[a] = modules[m].b[a];
            auto counts = fitCounts(m, low);
            int cross = product(counts);
            if (cross <= 0)
                continue;
            int max = 1;
            for (int a : axes)
                max = std::min(1000000, max * maxCount(m, a, d[a]));
            max = std::min(max, remaining(m) / cross);
            double base = 1, pitch = 1;
            for (int a : axes) {
                base *= modules[m].b[a];
                pitch *= modules[m].p[a];
            }
            for (int q = 1; q <= max; q++) {
                auto n = counts;
                if (axes.size() == 1)
                    n[axes[0]] = q;
                int qty = cross * q;
                double value = priceOf(m) * qty, weight = base + (q - 1) * pitch;
                out.push_back({m, q, max, cross, qty, n, value, weight, value / weight});
            }
        }
        std::set<int> ids;
        for (auto x : out)
            ids.insert(x.module);
        if (ids.size() == 1)
            std::stable_sort(out.begin(), out.end(), [](auto a, auto b) { return a.q > b.q; });
        else {
            std::stable_sort(out.begin(), out.end(),
                             [](auto a, auto b) { return a.density > b.density; });
            for (size_t i = 0; i < out.size();) {
                size_t j = i + 1;
                while (j < out.size() && out[j].module == out[i].module)
                    j++;
                std::stable_sort(out.begin() + static_cast<ptrdiff_t>(i),
                                 out.begin() + static_cast<ptrdiff_t>(j),
                                 [](auto a, auto b) { return a.q > b.q; });
                i = j;
            }
        }
        return out;
    }
    Tree terminal(int m, Counts n) {
        auto t = std::make_shared<Node>();
        t->module = m;
        t->n = n;
        for (int a = 0; a < 3; a++)
            t->size[a] = modules[m].b[a] + (n[a] - 1) * modules[m].p[a];
        return t;
    }
    Tree cut(int axis, std::vector<Tree> children) {
        std::vector<Tree> cs;
        for (auto c : children) {
            if (!c)
                continue;
            if (c->module < 0 && c->axis == axis)
                cs.insert(cs.end(), c->children.begin(), c->children.end());
            else
                cs.push_back(c);
        }
        if (cs.empty())
            return {};
        if (cs.size() == 1)
            return cs[0];
        auto t = std::make_shared<Node>();
        t->axis = axis;
        t->children = std::move(cs);
        for (auto c : t->children)
            for (int a = 0; a < 3; a++) {
                if (a == axis)
                    t->size[a] += c->size[a];
                else
                    t->size[a] = std::max(t->size[a], c->size[a]);
            }
        return t;
    }
    void update(Tree t, int sign) {
        if (!t)
            return;
        if (t->module >= 0) {
            updateModule(t->module, sign * product(t->n));
        } else
            for (auto c : t->children)
                update(c, sign);
    }
    double value(Tree t) const {
        if (!t)
            return 0;
        if (t->module >= 0)
            return priceOf(t->module) * product(t->n);
        double r = 0;
        for (auto c : t->children)
            r += value(c);
        return r;
    }
    Tree build(const std::vector<int> &mods, const Dims &d, int axis, int limit = 100) {
        auto its = items(mods, d, {axis});
        std::set<int> blocked;
        std::vector<Item> chain, bestChain;
        double best = -1;
        std::function<int(size_t, double, int, double)> visit = [&](size_t start, double cap, int left,
                                                                    double v) {
            budget.check();
            size_t i = start;
            for (; i < its.size(); i++) {
                auto x = its[i];
                if (!blocked.contains(x.module) && x.weight - cap <= eps && remaining(x.module) >= x.qty)
                    break;
            }
            if (i == its.size()) {
                if (!chain.empty() && v > best) {
                    best = v;
                    bestChain = chain;
                }
                return 1;
            }
            auto x = its[i];
            updateModule(x.module, x.qty);
            if (x.q != x.qMax)
                blocked.insert(x.module);
            chain.push_back(x);
            int spent = visit(i, cap - x.weight, left, v + x.value);
            chain.pop_back();
            updateModule(x.module, -x.qty);
            if (x.q != x.qMax)
                blocked.erase(x.module);
            if (spent < left)
                spent += visit(i + 1, cap, left - spent, v);
            return spent;
        };
        visit(0, d[axis], limit, 0);
        std::vector<Tree> leaves;
        for (auto x : bestChain)
            leaves.push_back(terminal(x.module, x.counts));
        return cut(axis, leaves);
    }
    double score(const std::vector<int> &mods, const Dims &d, const Axes &axes, int limit) {
        auto its = items(mods, d, axes);
        std::set<int> blocked;
        double best = 0;
        std::function<int(size_t, double, int, double)> visit = [&](size_t start, double cap, int left,
                                                                    double v) {
            budget.check();
            size_t i = start;
            int nMax = 0;
            for (; i < its.size(); i++) {
                auto x = its[i];
                int u = std::min(1000000, remaining(x.module) / x.cross);
                if (blocked.contains(x.module) || x.q > u)
                    continue;
                double nd = std::trunc(
                    (cap + eps) /
                    x.weight); /* residualK=0: literal recovered second expression is identical. */
                nMax = static_cast<int>(std::max(
                    0.0, std::min({nd, static_cast<double>(u / x.q),
                                   x.q == x.qMax ? std::numeric_limits<double>::infinity() : 1.0})));
                if (nMax > 0)
                    break;
            }
            if (i == its.size()) {
                best = std::max(best, v);
                return 1;
            }
            auto x = its[i];
            int spent = 0;
            for (int n = nMax; n >= 0 && spent < left; n--) {
                updateModule(x.module, n * x.qty);
                if (x.q != x.qMax && n)
                    blocked.insert(x.module);
                spent += visit(i + 1, cap - n * x.weight, left - spent, v + n * x.value);
                updateModule(x.module, -n * x.qty);
                if (x.q != x.qMax)
                    blocked.erase(x.module);
            }
            return spent;
        };
        double cap = 1;
        for (int a : axes)
            cap *= d[a];
        visit(0, cap, limit, 0);
        return best;
    }
    std::vector<Cut> directional(const std::vector<int> &mods, const Dims &d, int a) {
        std::vector<Cut> cuts;
        for (int m : eligible(mods, d)) {
            int u = remaining(m), limit = std::min(maxCount(m, a, d[a]), u);
            for (int q = 1; q <= limit; q++) {
                double t = modules[m].b[a] + (q - 1) * modules[m].p[a];
                if (near(t, d[a]))
                    break;
                if (t <= -eps || d[a] - t <= -eps)
                    continue;
                int n = maxCount(m, a, t);
                require(n > 0, PN2S_INTERNAL, "Invalid rectangle cut count");
                int batches = u / n;
                double b = 1, p = 1;
                for (int j = 0; j < 3; j++)
                    if (j != a) {
                        b *= modules[m].b[j];
                        p *= modules[m].p[j];
                    }
                cuts.push_back(
                    {a, t, priceOf(m) * (batches * n) / (t * (b + (batches - 1) * p))});
            }
        }
        std::stable_sort(cuts.begin(), cuts.end(), [](auto a, auto b) {
            return a.t < b.t || (a.t == b.t && a.density > b.density);
        });
        std::vector<Cut> unique;
        double prev = 0;
        bool first = true;
        int count = 0;
        for (auto c : cuts) {
            if (first || !near(c.t, prev))
                count = 0;
            first = false;
            prev = c.t;
            if (count++ < 1000)
                unique.push_back(c);
        }
        if (unique.size() <= 10)
            return unique;
        double maxT = unique.back().t;
        std::array<std::vector<Cut>, 5> buckets;
        for (auto c : unique) {
            int bucket =
                std::clamp(static_cast<int>(std::floor(std::log(maxT / c.t) / std::log(2.0))), 0, 4);
            buckets[bucket].push_back(c);
        }
        cuts.clear();
        for (int i = 4; i >= 0; i--) {
            auto &b = buckets[i];
            std::stable_sort(b.begin(), b.end(), [](auto a, auto c) {
                return a.density > c.density || (a.density == c.density && a.t < c.t);
            });
            for (size_t j = 0; j < std::min<size_t>(2, b.size()); j++)
                cuts.push_back(b[j]);
        }
        return cuts;
    }
    std::vector<Cut> stageCuts(const std::vector<int> &mods, const Dims &d, const Axes &axes) {
        std::vector<Cut> cuts;
        for (int a : axes) {
            auto c = directional(mods, d, a);
            cuts.insert(cuts.end(), c.begin(), c.end());
        }
        if (cuts.size() > 50)
            cuts.resize(50);
        for (auto &c : cuts) {
            auto l = d, r = d;
            l[c.axis] = c.t;
            r[c.axis] = d[c.axis] - c.t;
            Axes other;
            for (int a : axes)
                if (a != c.axis)
                    other.push_back(a);
            double A = score(mods, l, other, 5), B = score(mods, r, axes, 1);
            c.score = A + std::min(B, A / l[c.axis] * r[c.axis]);
        }
        std::stable_sort(cuts.begin(), cuts.end(), [](auto a, auto b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.axis != b.axis)
                return a.axis < b.axis;
            return a.t < b.t;
        });
        return cuts;
    }
    Tree construct(std::vector<int> mods, Dims d, Axes axes = {0, 1}, int stages = 20, int previous = 3,
                   int depth = 0) {
        budget.check();
        require(depth <= 40, PN2S_RESOURCE_LIMIT, "Rectangle recursion limit");
        mods = eligible(mods, d);
        if (mods.empty() || axes.empty())
            return {};
        if (axes.size() == 1)
            return build(mods, d, axes[0]);
        double mono = 0;
        int monoAxis = axes[0];
        bool hasMono = false;
        for (int a : axes) {
            double v = score(mods, d, {a}, 20);
            if (v > mono) {
                mono = v;
                monoAxis = a;
                hasMono = true;
            }
        }
        if (stages == 1)
            return hasMono ? build(mods, d, monoAxis) : Tree{};
        auto cuts = stageCuts(mods, d, axes);
        if (!hasMono && cuts.empty())
            return {};
        if (hasMono && (cuts.empty() || mono >= .98 * cuts[0].score))
            return build(mods, d, monoAxis);
        auto c = cuts[0];
        auto l = d, r = d;
        l[c.axis] = c.t;
        r[c.axis] = d[c.axis] - c.t;
        int next = stages - (c.axis != previous);
        Axes other;
        for (int a : axes)
            if (a != c.axis)
                other.push_back(a);
        auto left = construct(mods, l, other, next, c.axis, depth + 1);
        update(left, 1);
        auto right = construct(mods, r, axes, next, c.axis, depth + 1);
        update(left, -1);
        auto split = cut(c.axis, {left, right});
        if (!hasMono || value(split) > mono * 1.000001)
            return split;
        auto monoTree = build(mods, d, monoAxis);
        double sv = value(split), mv = value(monoTree), mx = std::max(sv, mv);
        return mx > 0 && sv / mx - mv / mx <= -eps ? monoTree : split;
    }
    void flatten(Tree t, Dims origin, Output &out, std::vector<size_t> &next) {
        if (!t)
            return;
        if (t->module < 0) {
            for (auto c : t->children) {
                flatten(c, origin, out, next);
                origin[t->axis] += c->size[t->axis];
            }
            return;
        }
        auto m = modules[t->module];
        if (!m.members.empty()) {
            for (int i = 0; i < t->n[0]; ++i)
                for (int j = 0; j < t->n[1]; ++j)
                    for (auto member : m.members) {
                        budget.check();
                        const double x = std::isfinite(member.exactX) ? member.exactX : double(member.x) / 1e6;
                        const double y = std::isfinite(member.exactY) ? member.exactY : double(member.y) / 1000.;
                        member.exactX = (origin[0] - m.box.x0) + x + i * m.p[0];
                        member.exactY = (origin[1] - m.box.y0) + y + j * m.p[1];
                        member.x = rounded(member.exactX * 1e6); member.y = rounded(member.exactY * 1000.);
                        member.exactX = double(member.x) / 1e6; member.exactY = double(member.y) / 1000.;
                        member.ordinal = next[member.group]++;
                        out.placements.push_back(member);
                    }
            return;
        }
        int64_t tx = rounded(origin[0] * 1e6) + rounded(-m.box.x0 * 1e6),
                ty = rounded(origin[1] * 1000) + rounded(-m.box.y0 * 1000);
        for (int i = 0; i < t->n[0]; i++)
            for (int j = 0; j < t->n[1]; j++) {
                budget.check();
                out.placements.push_back({static_cast<size_t>(m.group), next[m.group]++, m.pose,
                                          tx + rounded(i * m.p[0] * 1e6),
                                          ty + rounded(j * m.p[1] * 1000)});
            }
    }
};
} // namespace
Box rectangleModelBounds(const Input &input, const RectangleTilingModel &model) {
    require(!model.members.empty(), PN2S_INVALID, "Empty Rectangle tiling model");
    Box result{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto &member : model.members) {
        const auto &part = input.parts.at(member.group);
        auto pose = part.poses.at(member.poseIndex);
        if (std::isfinite(member.solvedAngle)) pose.angle = member.solvedAngle;
        const auto box = bounds(transform(part.polygon, part.pivot, pose));
        const double x = std::isfinite(member.exactX) ? member.exactX : double(member.x) / 1e6;
        const double y = std::isfinite(member.exactY) ? member.exactY : double(member.y) / 1000.;
        result.x0 = std::min(result.x0, box.x0 + x); result.y0 = std::min(result.y0, box.y0 + y);
        result.x1 = std::max(result.x1, box.x1 + x); result.y1 = std::max(result.y1, box.y1 + y);
    }
    return result;
}
RectangleCoreResult rectangleCore(const Input &input, Budget &budget, const RectangleCoreOptions &settings) {
    auto options = settings;
    if (std::any_of(options.modules.begin(), options.modules.end(), [](const auto &model) { return model.maximumTotal != 0; })) {
        if (!options.modulePrices.empty()) {
            require(options.modulePrices.size() == options.modules.size(), PN2S_INVALID,
                    "Rectangle module price count mismatch");
            std::vector<double> expanded;
            for (size_t index = 0; index < options.modules.size(); ++index) {
                expanded.push_back(options.modulePrices[index]);
                if (options.modules[index].maximumTotal != 0) expanded.push_back(options.modulePrices[index]);
            }
            options.modulePrices = std::move(expanded);
        }
        if (!options.moduleOrder.empty()) {
            std::vector<int> offsets{0}, expanded;
            for (const auto &model : options.modules) offsets.push_back(offsets.back() + (model.maximumTotal == 0 ? 1 : 2));
            for (const auto index : options.moduleOrder) {
                require(index >= 0 && size_t(index) < options.modules.size(), PN2S_INVALID, "Invalid Rectangle module order");
                for (int module = offsets[index]; module < offsets[index + 1]; ++module) expanded.push_back(module);
            }
            options.moduleOrder = std::move(expanded);
        }
        options.modules = rectangleExpandModelLimits(options.modules);
    }
    require(grid_exact(input.gap, 1e6) && grid_exact(input.gap, 1000), PN2S_UNSUPPORTED,
            "Rectangle gap must fit host grid");
    for (auto &p : input.parts)
        for (auto q : p.polygon)
            require(grid_exact(q.x, 1e6) && grid_exact(q.y, 1000), PN2S_UNSUPPORTED,
                    "Rectangle dimensions must fit host grid");
    if (options.modules.empty()) for (auto &part : input.parts)
        for (auto pose : part.poses)
            for (auto q : transform(part.polygon, part.pivot, pose))
                require(grid_exact(q.x, 1e6) && grid_exact(q.y, 1000), PN2S_UNSUPPORTED,
                        "Rotated rectangle does not fit host grid");
    Problem p(input, budget, options.modules);
    if (!options.groupPrices.empty()) {
        require(options.groupPrices.size() == p.price.size(), PN2S_INVALID,
                "Rectangle group price count mismatch");
        p.price = options.groupPrices;
    }
    if (!options.modulePrices.empty()) {
        require(options.modulePrices.size() == p.modules.size(), PN2S_INVALID,
                "Rectangle module price count mismatch");
        p.modulePrice = options.modulePrices;
    }
    if (options.targetWidth != 0.) p.D[0] = options.targetWidth;
    std::vector<int> mods(p.modules.size());
    std::iota(mods.begin(), mods.end(), 0);
    if (!options.moduleOrder.empty()) {
        auto sorted = options.moduleOrder;
        std::sort(sorted.begin(), sorted.end());
        require(sorted == mods, PN2S_INVALID, "Rectangle module order must be a permutation");
        mods = options.moduleOrder;
    }
    auto tree = p.construct(mods, p.D);
    for (int u : p.used)
        require(u == 0, PN2S_INTERNAL, "Unrestored rectangle demand");
    RectangleCoreResult result;
    result.hasTree = bool(tree);
    if (tree) result.dimensions = tree->size;
    result.counts.resize(input.parts.size());
    std::vector<size_t> next(input.parts.size());
    p.flatten(tree, {0, 0, 0}, result.output, next);
    result.complete = true;
    for (size_t group = 0; group < next.size(); ++group) {
        result.counts[group] = static_cast<int>(next[group]);
        if (p.participating[group] && result.counts[group] < input.parts[group].quantity) result.complete = false;
    }
    return result;
}
Output rectangles(const Input &input, Budget &budget) {
    return rectangleCore(input, budget, {}).output;
}
bool rectangle_range_nest(const Input &input, Budget &budget, Output &output) {
    size_t required = 0;
    for (const auto &part : input.parts) required += static_cast<size_t>(part.quantity);
    if (!required) return false;
    Output candidate;
    if (!ordinaryRectangle(input, budget, candidate) || candidate.placements.size() != required) return false;
    output = std::move(candidate);
    return true;
}
} // namespace study
