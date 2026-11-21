#include "SheetNestingConvexConstruction.h"
#include "NestingAxisSafety.h"
#include "NestingLinearFeasibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <utility>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock = std::chrono::steady_clock;
    using Detail::LinearRow;
    using Detail::LinearState;
    using ExactPointKey = std::pair<std::vector<double>, std::vector<double>>;
    using ShapeKey = std::vector<ExactPointKey>;
    struct Direction { double Angle = 0; Rect Box; std::vector<Point> Ring; ShapeKey Key; };
    struct Piece { std::size_t PartIndex, Instance; std::vector<Direction> Directions; };
    struct Axis { double X, Y, B; };
    struct Pair { std::size_t I, J; std::vector<Axis> Axes; };
    struct Node
    {
        std::vector<std::pair<std::size_t, std::size_t>> Choices;
        std::vector<double> Guards;
        std::size_t NumericalRetries = 0;
    };

    std::optional<double> RepresentableBound(const AxisSafety::Number& value, bool upper)
    {
        double rounded = 0;
        for (const auto term : value.Terms) rounded += term;
        for (int step = 0; step < 4 && std::isfinite(rounded); ++step)
        {
            const int difference = (AxisSafety::Number(rounded) - value).Sign();
            if (upper ? difference >= 0 : difference <= 0) return rounded;
            rounded = std::nextafter(rounded, upper ? std::numeric_limits<double>::infinity() :
                -std::numeric_limits<double>::infinity());
        }
        return {};
    }

    bool ReconstructInside(const Shape& shape, const Direction& direction, const Stock& stock,
        double margin, Pose& pose, AxisSafety::Geometry& geometry, bool& adjusted)
    {
        const double minX = margin - direction.Box.X, minY = margin - direction.Box.Y;
        const double maxX = stock.Width - margin - direction.Box.X - direction.Box.Width;
        const double maxY = stock.Height - margin - direction.Box.Y - direction.Box.Height;
        if (minX > maxX || minY > maxY) return false;
        const auto original = pose;
        pose.X = std::clamp(pose.X, minX, maxX); pose.Y = std::clamp(pose.Y, minY, maxY);
        for (int pass = 0; pass < 4; ++pass)
        {
            geometry = AxisSafety::Make(shape, pose);
            if (AxisSafety::Inside(geometry, stock.Width, stock.Height, margin))
            {
                adjusted = pose.X != original.X || pose.Y != original.Y;
                return true;
            }
            if (!geometry.Valid) return false;
            const auto low = AxisSafety::Number(margin) + AxisSafety::Number(geometry.Roundoff);
            const auto highX = AxisSafety::Number(stock.Width) - AxisSafety::Number(margin) - AxisSafety::Number(geometry.Roundoff);
            const auto highY = AxisSafety::Number(stock.Height) - AxisSafety::Number(margin) - AxisSafety::Number(geometry.Roundoff);
            AxisSafety::Number deficitX, deficitY, excessX, excessY;
            const auto maximum = [](AxisSafety::Number& current, const AxisSafety::Number& candidate)
            { if ((candidate - current).Sign() > 0) current = candidate; };
            for (const auto& ring : geometry.Rings) for (const auto& point : ring)
            {
                maximum(deficitX, low - point.X); maximum(deficitY, low - point.Y);
                maximum(excessX, point.X - highX); maximum(excessY, point.Y - highY);
            }
            if ((deficitX.Sign() > 0 && excessX.Sign() > 0) || (deficitY.Sign() > 0 && excessY.Sign() > 0)) return false;
            const auto correct = [](double& coordinate, const AxisSafety::Number& deficit, const AxisSafety::Number& excess)
            {
                const bool inward = deficit.Sign() > 0;
                if (!inward && excess.Sign() <= 0) return true;
                const auto target = inward ? AxisSafety::Number(coordinate) + deficit : AxisSafety::Number(coordinate) - excess;
                const auto represented = RepresentableBound(target, inward);
                if (!represented) return false;
                coordinate = *represented; return true;
            };
            if (!correct(pose.X, deficitX, excessX) || !correct(pose.Y, deficitY, excessY)) return false;
        }
        geometry = AxisSafety::Make(shape, pose);
        adjusted = pose.X != original.X || pose.Y != original.Y;
        return AxisSafety::Inside(geometry, stock.Width, stock.Height, margin);
    }

    // Actual-world support projections propose a stronger private LP row.
    // Published geometry still passes the original material and demand guards.
    std::optional<double> RequiredAxisIncrement(const Axis& axis, const AxisSafety::Geometry& first,
        const AxisSafety::Geometry& second, double scale)
    {
        try
        {
            const auto projection = [&](const AxisSafety::Vector& point)
            { return AxisSafety::Number(axis.X) * point.X + AxisSafety::Number(axis.Y) * point.Y; };
            auto maximum = projection(first.Rings.front().front());
            auto minimum = projection(second.Rings.front().front());
            for (const auto& point : first.Rings.front())
            {
                const auto value = projection(point);
                if ((value - maximum).Sign() > 0) maximum = value;
            }
            for (const auto& point : second.Rings.front())
            {
                const auto value = projection(point);
                if ((value - minimum).Sign() < 0) minimum = value;
            }
            const auto needed = AxisSafety::Clearance(first, second, 0) + maximum - minimum;
            if (needed.Sign() <= 0) return 0.;
            const auto world = RepresentableBound(needed, true);
            if (!world) return {};
            const auto normalized = std::nextafter(*world / scale, std::numeric_limits<double>::infinity());
            if (!std::isfinite(normalized)) return {};
            return normalized;
        }
        catch (const AxisSafety::ArithmeticUncertainty&) { return {}; }
    }

    ShapeKey TranslationKey(const Shape& shape, double angle)
    {
        const auto exact = AxisSafety::Make(shape, {0, 0, angle});
        const auto& ring = exact.Rings.front();
        auto origin = ring.front();
        for (const auto& point : ring)
        {
            const int x = (point.X - origin.X).Sign();
            if (x < 0 || (x == 0 && (point.Y - origin.Y).Sign() < 0)) origin = point;
        }
        ShapeKey key;
        for (const auto& point : ring)
            key.emplace_back((point.X - origin.X).Terms, (point.Y - origin.Y).Terms);
        std::sort(key.begin(), key.end());
        return key;
    }

    class JointConvexConstructor final : public IStrategy
    {
    public:
        Result Run(const Problem& input, const Options& options) override
        {
            const auto started = Clock::now();
            const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
            std::size_t work = 0, nodes = 0, plansTried = 0, infeasible = 0, limited = 0, numerical = 0, boardAdjustments = 0;
            std::size_t repairs = 0, repairLimits = 0;
            bool cancelled = false, planListTruncated = false;
            const auto stop = [&]
            {
                if (!cancelled && options.Cancel && options.Cancel()) cancelled = true;
                return cancelled || elapsed() >= options.TimeLimitSeconds || work >= options.MaxPlacementAttempts;
            };
            Result best; best.State = Status::Partial;
            for (const auto& part : input.Parts) if (part.Quantity) best.Remaining.push_back({part.ID, part.Quantity});
            const auto finish = [&](const std::string& reason)
            {
                best.Stats.PlacementAttempts = work;
                best.Stats.ElapsedSeconds = elapsed();
                if (cancelled) best.State = Status::Cancelled;
                best.Diagnostics.push_back("joint.convex: " + reason + "; nodes=" + std::to_string(nodes) +
                    "; plans=" + std::to_string(plansTried) + "; infeasible=" + std::to_string(infeasible) +
                    "; limited=" + std::to_string(limited) + "; numerical=" + std::to_string(numerical) +
                    "; numerical-repairs=" + std::to_string(repairs) + "; numerical-repair-limits=" + std::to_string(repairLimits) +
                    "; board-adjustments=" + std::to_string(boardAdjustments) +
                    "; plan-list-truncated=" + std::to_string(planListTruncated) +
                    "; common-edge-exemption=" + std::to_string(input.AllowCommonEdges));
                return best;
            };
            const auto errors = Detail::CheckProblem(input);
            if (!errors.empty())
            {
                best.State = Status::InvalidInput; best.Diagnostics = errors;
                return finish("invalid-input");
            }
            if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 || !options.MaxPlacementAttempts)
            {
                best.State = Status::InvalidInput;
                return finish("invalid-budget");
            }
            if (stop()) return finish("budget");
            std::size_t count = 0;
            for (const auto& part : input.Parts)
            {
                if (part.Quantity > 12 - count) return finish("unsupported <=12-piece scope");
                count += part.Quantity;
            }
            if (!count) { best.State = Status::Complete; return finish("empty-complete"); }
            if (input.PartGap != 0 || input.Stocks.size() != 1 || input.Stocks[0].Quantity != 1)
                return finish("unsupported single-sheet zero-gap scope");
            const auto& stock = input.Stocks[0];
            const double scale = std::max(stock.Width, stock.Height);
            std::vector<Piece> pieces;
            for (std::size_t index = 0; index < input.Parts.size(); ++index)
            {
                const auto& part = input.Parts[index];
                if (!part.Quantity) continue;
                if (part.FreeRotation || part.Material != stock.Material || part.Geometry.Radius > 0 ||
                    !part.Geometry.Holes.empty() || part.Geometry.Outer.size() > 64)
                    return finish("unsupported fixed-angle simple convex polygon scope");
                const auto exact = AxisSafety::Make(part.Geometry, {});
                if (!exact.Valid || !exact.Convex) return finish("unsupported nonconvex geometry");
                std::vector<Direction> directions; std::set<ShapeKey> seen;
                for (const auto angle : Detail::CandidateAngles(part, input.Tolerance))
                {
                    if (stop()) return finish("preparation-budget");
                    Direction direction; direction.Angle = angle;
                    direction.Box = Detail::Bounds(part.Geometry, {0, 0, angle});
                    if (direction.Box.Width > stock.Width - 2 * input.EdgeMargin ||
                        direction.Box.Height > stock.Height - 2 * input.EdgeMargin) continue;
                    direction.Key = TranslationKey(part.Geometry, angle);
                    if (!seen.insert(direction.Key).second) continue;
                    for (const auto point : Detail::MaterialRing(part.Geometry.Outer))
                    {
                        const auto rotated = Detail::Transform(point, {0, 0, angle});
                        direction.Ring.push_back({rotated.X / scale, rotated.Y / scale});
                    }
                    directions.push_back(std::move(direction));
                }
                if (directions.empty()) return finish("no supported orientation fits sheet bounds");
                for (std::size_t instance = 0; instance < part.Quantity; ++instance)
                    pieces.push_back({index, instance, directions});
            }
            const std::size_t positions = 2 * count, variables = 2 * positions;
            std::vector<std::vector<std::size_t>> plans;
            std::vector<std::size_t> plan(count);
            const auto enumerate = [&](auto&& self, std::size_t index) -> void
            {
                if (stop()) return;
                if (plans.size() >= 256) { planListTruncated = true; return; }
                if (index == count) { plans.push_back(plan); return; }
                for (std::size_t direction = 0; direction < pieces[index].Directions.size(); ++direction)
                {
                    plan[index] = direction; self(self, index + 1);
                    if (planListTruncated || stop()) return;
                }
            };
            enumerate(enumerate, 0);
            const double prepared = elapsed();
            std::size_t bestCount = 0;
            double bestArea = 0;
            for (std::size_t planIndex = 0; planIndex < plans.size() && !stop(); ++planIndex)
            {
                ++plansTried;
                const auto slots = plans.size() - planIndex;
                const double deadline = elapsed() + std::max(0., options.TimeLimitSeconds - elapsed()) / double(slots);
                const auto attemptEnd = work + std::max<std::size_t>(1, (options.MaxPlacementAttempts - work) / slots);
                const auto localStop = [&] { return stop() || elapsed() >= deadline || work >= attemptEnd; };
                std::vector<const Direction*> directions;
                std::vector<double> lower(positions), upper(positions);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const auto& direction = pieces[i].Directions[plans[planIndex][i]];
                    directions.push_back(&direction);
                    lower[i] = (input.EdgeMargin - direction.Box.X) / scale;
                    lower[count + i] = (input.EdgeMargin - direction.Box.Y) / scale;
                    upper[i] = (stock.Width - input.EdgeMargin - direction.Box.X - direction.Box.Width) / scale;
                    upper[count + i] = (stock.Height - input.EdgeMargin - direction.Box.Y - direction.Box.Height) / scale;
                }
                std::vector<LinearRow> base;
                for (std::size_t coordinate = 0; coordinate < positions; ++coordinate)
                {
                    LinearRow bound; bound.A.resize(variables); bound.A[coordinate] = 1;
                    bound.B = upper[coordinate] - lower[coordinate]; base.push_back(std::move(bound));
                    // Minimize all positions from their lower bounds. There is
                    // no input incumbent or inherited placement anchor.
                    LinearRow objective; objective.A.resize(variables); objective.A[coordinate] = 1;
                    objective.A[positions + coordinate] = -1; base.push_back(std::move(objective));
                }
                std::vector<Pair> pairs;
                for (std::size_t i = 0; i < count; ++i) for (std::size_t j = i + 1; j < count; ++j)
                {
                    Pair pair{i, j, {}};
                    for (const auto* direction : {directions[i], directions[j]})
                        for (std::size_t edge = 0; edge < direction->Ring.size(); ++edge)
                        {
                            const auto a = direction->Ring[edge], b = direction->Ring[(edge + 1) % direction->Ring.size()];
                            const double length = std::hypot(b.X - a.X, b.Y - a.Y);
                            if (length == 0) continue;
                            for (const double sign : {-1., 1.})
                            {
                                const double nx = sign * (a.Y - b.Y) / length, ny = sign * (b.X - a.X) / length;
                                if (std::any_of(pair.Axes.begin(), pair.Axes.end(), [&](Axis axis)
                                    { return axis.X == nx && axis.Y == ny; })) continue;
                                double maximum = -std::numeric_limits<double>::infinity(), minimum = -maximum;
                                for (const auto point : directions[i]->Ring) maximum = std::max(maximum, point.X * nx + point.Y * ny);
                                for (const auto point : directions[j]->Ring) minimum = std::min(minimum, point.X * nx + point.Y * ny);
                                pair.Axes.push_back({nx, ny, minimum - maximum});
                            }
                        }
                    pairs.push_back(std::move(pair));
                }
                std::vector<Node> open(1);
                std::size_t planNodes = 0;
                while (!open.empty() && !localStop() && planNodes < 4096)
                {
                    auto node = std::move(open.back()); open.pop_back(); ++nodes; ++planNodes; ++work;
                    auto rows = base;
                    for (std::size_t choiceIndex = 0; choiceIndex < node.Choices.size(); ++choiceIndex)
                    {
                        const auto [pairIndex, axisIndex] = node.Choices[choiceIndex];
                        const auto& pair = pairs[pairIndex]; const auto axis = pair.Axes[axisIndex];
                        LinearRow row; row.A.resize(variables);
                        row.A[pair.I] = axis.X; row.A[pair.J] = -axis.X;
                        row.A[count + pair.I] = axis.Y; row.A[count + pair.J] = -axis.Y;
                        row.B = axis.B - axis.X * (lower[pair.I] - lower[pair.J]) -
                            axis.Y * (lower[count + pair.I] - lower[count + pair.J]);
                        row.B -= node.Guards[choiceIndex];
                        rows.push_back(std::move(row));
                    }
                    std::vector<double> point;
                    Detail::LinearFeasibility solver(variables, rows, work, localStop);
                    const auto state = solver.solve(positions, point);
                    if (state == LinearState::Infeasible) { ++infeasible; continue; }
                    if (state == LinearState::Limited) { ++limited; continue; }
                    std::vector<Placement> placements; std::vector<AxisSafety::Geometry> geometry;
                    std::vector<double> actualPosition(positions);
                    bool inside = true;
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const auto& part = input.Parts[pieces[i].PartIndex];
                        Pose pose{(point[i] + lower[i]) * scale, (point[count + i] + lower[count + i]) * scale,
                            directions[i]->Angle};
                        AxisSafety::Geometry actual; bool adjusted = false;
                        const bool supported = ReconstructInside(part.Geometry, *directions[i], stock, input.EdgeMargin, pose, actual, adjusted);
                        boardAdjustments += adjusted;
                        placements.push_back({part.ID, pieces[i].Instance, pose});
                        geometry.push_back(std::move(actual));
                        actualPosition[i] = pose.X / scale; actualPosition[count + i] = pose.Y / scale;
                        inside = inside && supported;
                    }
                    if (!inside) { ++numerical; continue; }
                    std::size_t conflict = pairs.size(); double worst = 0;
                    std::vector<std::vector<bool>> safe(count, std::vector<bool>(count, true));
                    for (std::size_t pairIndex = 0; pairIndex < pairs.size(); ++pairIndex)
                    {
                        const auto& pair = pairs[pairIndex];
                        const auto verdict = AxisSafety::Pair(geometry[pair.I], geometry[pair.J], 0,
                            input.AllowCommonEdges, input.Tolerance);
                        safe[pair.I][pair.J] = safe[pair.J][pair.I] = verdict == AxisSafety::Verdict::Safe;
                        if (verdict == AxisSafety::Verdict::Safe) continue;
                        double penetration = std::numeric_limits<double>::infinity();
                        for (const auto axis : pair.Axes)
                        {
                            const double delta = axis.X * (actualPosition[pair.I] - actualPosition[pair.J]) +
                                axis.Y * (actualPosition[count + pair.I] - actualPosition[count + pair.J]);
                            penetration = std::min(penetration, delta - axis.B);
                        }
                        if (conflict == pairs.size() || penetration > worst) { conflict = pairIndex; worst = penetration; }
                    }
                    // Retain only independently checked nonoverlapping pieces
                    // from a joint proposal when the bounded search is partial.
                    std::vector<std::size_t> selected;
                    for (std::size_t i = 0; i < count; ++i)
                        if (std::all_of(selected.begin(), selected.end(), [&](auto j) { return safe[i][j]; })) selected.push_back(i);
                    double area = 0;
                    for (const auto i : selected) area += Detail::Area(input.Parts[pieces[i].PartIndex].Geometry);
                    if (selected.size() > bestCount || (selected.size() == bestCount && area > bestArea))
                    {
                        Result candidate; candidate.State = selected.size() == count ? Status::Complete : Status::Partial;
                        Layout layout; layout.StockID = stock.ID; layout.Width = stock.Width; layout.Height = stock.Height;
                        std::vector<std::size_t> used(input.Parts.size());
                        for (const auto i : selected) { layout.Placements.push_back(placements[i]); ++used[pieces[i].PartIndex]; }
                        layout.MaterialArea = area; candidate.Layouts.push_back(std::move(layout));
                        for (std::size_t i = 0; i < input.Parts.size(); ++i)
                            if (used[i] < input.Parts[i].Quantity) candidate.Remaining.push_back({input.Parts[i].ID, input.Parts[i].Quantity - used[i]});
                        const auto checked = Validate(input, candidate);
                        if (checked.Valid)
                        {
                            best = std::move(candidate); best.GeometryValidated = true; bestCount = selected.size(); bestArea = area;
                            best.Stats.PreparationSeconds = prepared;
                            if (checked.Complete) return finish("validated-complete");
                        }
                    }
                    if (conflict == pairs.size()) { ++numerical; continue; }
                    const auto existing = std::find_if(node.Choices.begin(), node.Choices.end(),
                        [&](auto choice) { return choice.first == conflict; });
                    if (existing != node.Choices.end())
                    {
                        ++numerical;
                        const auto& pair = pairs[conflict];
                        const auto increment = RequiredAxisIncrement(pair.Axes[existing->second], geometry[pair.I], geometry[pair.J], scale);
                        if (node.NumericalRetries < 4 && increment)
                        {
                            const auto choiceIndex = static_cast<std::size_t>(existing - node.Choices.begin());
                            const double bound = rows[base.size() + choiceIndex].B;
                            const double ulp = bound - std::nextafter(bound, -std::numeric_limits<double>::infinity());
                            const double extra = std::max(*increment, ulp);
                            node.Guards[choiceIndex] = std::nextafter(node.Guards[choiceIndex] + extra,
                                std::numeric_limits<double>::infinity());
                            ++node.NumericalRetries; ++repairs;
                            open.push_back(std::move(node));
                        }
                        else ++repairLimits;
                        continue;
                    }
                    const auto& pair = pairs[conflict]; std::vector<std::size_t> axes(pair.Axes.size());
                    std::iota(axes.begin(), axes.end(), 0);
                    const auto movement = [&](std::size_t index)
                    {
                        const auto axis = pair.Axes[index];
                        return axis.X * (actualPosition[pair.I] - actualPosition[pair.J]) +
                            axis.Y * (actualPosition[count + pair.I] - actualPosition[count + pair.J]) - axis.B;
                    };
                    std::stable_sort(axes.begin(), axes.end(), [&](auto a, auto b) { return movement(a) > movement(b); });
                    for (const auto axis : axes)
                    {
                        auto child = node; child.Choices.emplace_back(conflict, axis); child.Guards.push_back(0); open.push_back(std::move(child));
                        if (open.size() >= 4096) break;
                    }
                }
            }
            return finish(stop() ? "budget" : "bounded-search-exhausted");
        }
    };

}

StrategyFactory MakeConvexConstructionFactory()
{
    return [] { return std::make_unique<JointConvexConstructor>(); };
}
}
