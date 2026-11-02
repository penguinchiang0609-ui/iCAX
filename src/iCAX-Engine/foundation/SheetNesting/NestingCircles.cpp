#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct PlacedCircle { std::size_t PartIndex; Point Center; double Radius; };
        struct CircleBin
        {
            std::size_t StockIndex;
            Layout Output;
            std::vector<PlacedCircle> Circles;
            std::vector<Point> SquareVoids;
            std::map<double,std::size_t> NextVoid;
            double SeedRadius=0;
        };

        // Candidate centres are exact pair/board contact constructions, for arbitrary input radii.
        // The candidate list is finite; acceptance always rechecks every actual circle and board edge.
        std::vector<Point> Contacts(const CircleBin& bin, double radius, const Problem& p,
            const std::function<bool()>& stop)
        {
            std::vector<Point> points;
            const double left = p.EdgeMargin + radius, right = bin.Output.Width - p.EdgeMargin - radius;
            const double bottom = p.EdgeMargin + radius, top = bin.Output.Height - p.EdgeMargin - radius;
            if (left > right + p.Tolerance || bottom > top + p.Tolerance) return points;
            constexpr std::size_t limit = 4096;
            constexpr std::size_t boardContactLimit = 512;
            std::set<std::pair<std::int64_t, std::int64_t>> seen;
            const auto add = [&](Point point)
            {
                if (points.size() < limit && std::isfinite(point.X) && std::isfinite(point.Y) &&
                    point.X + p.Tolerance >= left && point.X <= right + p.Tolerance &&
                    point.Y + p.Tolerance >= bottom && point.Y <= top + p.Tolerance)
                {
                    const auto key = std::pair(static_cast<std::int64_t>(std::llround(point.X / p.Tolerance)),
                        static_cast<std::int64_t>(std::llround(point.Y / p.Tolerance)));
                    if (seen.insert(key).second) points.push_back(point);
                }
            };
            add({left, bottom}); add({right, bottom}); add({left, top}); add({right, top});
            for (const auto& a : bin.Circles)
            {
                if (stop()) return points;
                // Leave capacity for mixed-radius circle/circle contacts in crowded layouts.
                if (points.size() >= boardContactLimit) break;
                const double d = radius + a.Radius + p.PartGap;
                for (double x : {left, right})
                {
                    const long double dx = static_cast<long double>(x) - a.Center.X;
                    const long double h2 = static_cast<long double>(d) * d - dx * dx;
                    if (h2 < -static_cast<long double>(p.Tolerance) * (2 * d + p.Tolerance)) continue;
                    const double h = static_cast<double>(std::sqrt(std::max(0.0L, h2)));
                    add({x, a.Center.Y - h}); add({x, a.Center.Y + h});
                }
                for (double y : {bottom, top})
                {
                    const long double dy = static_cast<long double>(y) - a.Center.Y;
                    const long double h2 = static_cast<long double>(d) * d - dy * dy;
                    if (h2 < -static_cast<long double>(p.Tolerance) * (2 * d + p.Tolerance)) continue;
                    const double h = static_cast<double>(std::sqrt(std::max(0.0L, h2)));
                    add({a.Center.X - h, y}); add({a.Center.X + h, y});
                }
            }
            for (std::size_t i = 0; i < bin.Circles.size() && points.size() < limit; ++i)
                for (std::size_t j = i + 1; j < bin.Circles.size() && points.size() < limit; ++j)
                {
                    if (stop()) return points;
                    const auto& a = bin.Circles[i]; const auto& b = bin.Circles[j];
                    const long double dx = static_cast<long double>(b.Center.X) - a.Center.X;
                    const long double dy = static_cast<long double>(b.Center.Y) - a.Center.Y;
                    const long double distance = std::hypot(dx, dy);
                    const long double ra = radius + a.Radius + p.PartGap, rb = radius + b.Radius + p.PartGap;
                    if (distance <= p.Tolerance || distance > ra + rb + p.Tolerance || distance < std::abs(ra - rb) - p.Tolerance) continue;
                    const long double along = (distance + (ra - rb) * (ra + rb) / distance) / 2;
                    const long double h2 = ra * ra - along * along;
                    if (h2 < -static_cast<long double>(p.Tolerance) * (2 * ra + p.Tolerance)) continue;
                    const long double h = std::sqrt(std::max(0.0L, h2));
                    const long double x = a.Center.X + along * dx / distance, y = a.Center.Y + along * dy / distance;
                    add({static_cast<double>(x - h * dy / distance), static_cast<double>(y + h * dx / distance)});
                    add({static_cast<double>(x + h * dy / distance), static_cast<double>(y - h * dx / distance)});
                }
            std::sort(points.begin(), points.end(), [](Point a, Point b) { return std::tie(a.Y, a.X) < std::tie(b.Y, b.X); });
            points.erase(std::unique(points.begin(), points.end(), [&](Point a, Point b)
                { return std::hypot(a.X - b.X, a.Y - b.Y) <= p.Tolerance; }), points.end());
            return points;
        }
        auto ContactRank(const CircleBin& bin, Point center, double radius)
        {
            double x0 = center.X - radius, x1 = center.X + radius, y0 = center.Y - radius, y1 = center.Y + radius;
            for (const auto& circle : bin.Circles)
            {
                x0 = std::min(x0, circle.Center.X - circle.Radius); x1 = std::max(x1, circle.Center.X + circle.Radius);
                y0 = std::min(y0, circle.Center.Y - circle.Radius); y1 = std::max(y1, circle.Center.Y + circle.Radius);
            }
            return std::tuple((x1 - x0) * (y1 - y0), y1, x1, center.Y, center.X);
        }
    }

    Result RunCircleRules(const Problem& p, const Options& options, CircleOrder order)
    {
        Result result;
        const auto start = std::chrono::steady_clock::now();
        bool cancelled = false, exhausted = false;
        const auto stop = [&]
        {
            if (cancelled || exhausted) return true;
            if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
            if (result.Stats.PlacementAttempts >= options.MaxPlacementAttempts ||
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= options.TimeLimitSeconds)
            { exhausted = true; return true; }
            return false;
        };
        std::vector<std::size_t> consumed(p.Parts.size()), stockUsed(p.Stocks.size());
        std::vector<CircleBin> bins;
        std::size_t placedCount = 0;
        const auto candidates = [&]
        {
            std::vector<std::size_t> parts;
            for (std::size_t i = 0; i < p.Parts.size(); ++i)
                if (p.Parts[i].Geometry.Radius > 0 && consumed[i] < p.Parts[i].Quantity) parts.push_back(i);
            std::stable_sort(parts.begin(), parts.end(), [&](auto a, auto b)
            {
                const double ra = p.Parts[a].Geometry.Radius, rb = p.Parts[b].Geometry.Radius;
                double va = ra, vb = rb;
                if (order == CircleOrder::RemainingArea)
                { va = ra * ra * (p.Parts[a].Quantity - consumed[a]); vb = rb * rb * (p.Parts[b].Quantity - consumed[b]); }
                if (order == CircleOrder::Interleave && placedCount % 2) { va = -va; vb = -vb; }
                return va != vb ? va > vb : p.Parts[a].ID < p.Parts[b].ID;
            });
            return parts;
        };
        const auto find = [&](CircleBin& bin, std::size_t index) -> std::optional<Point>
        {
            const auto& part = p.Parts[index];
            if (part.Material != p.Stocks[bin.StockIndex].Material) return {};
            if(order==CircleOrder::SquareThenHoles&&part.Geometry.Radius<bin.SeedRadius)
            {
                auto& cursor=bin.NextVoid[part.Geometry.Radius];
                while(cursor<bin.SquareVoids.size()&&!stop())
                {
                    const auto center=bin.SquareVoids[cursor];++result.Stats.PlacementAttempts;
                    bool valid=center.X-part.Geometry.Radius+p.Tolerance>=p.EdgeMargin
                        &&center.Y-part.Geometry.Radius+p.Tolerance>=p.EdgeMargin
                        &&center.X+part.Geometry.Radius<=bin.Output.Width-p.EdgeMargin+p.Tolerance
                        &&center.Y+part.Geometry.Radius<=bin.Output.Height-p.EdgeMargin+p.Tolerance;
                    for(const auto& other:bin.Circles)
                    {
                        if(!valid||stop()){valid=false;break;}
                        if(std::hypot(center.X-other.Center.X,center.Y-other.Center.Y)+p.Tolerance
                            <part.Geometry.Radius+other.Radius+p.PartGap)valid=false;
                    }
                    if(valid)return center;
                    ++cursor;
                }
            }
            std::optional<Point> best;
            for (auto center : Contacts(bin, part.Geometry.Radius, p, stop))
            {
                if (stop()) break;
                ++result.Stats.PlacementAttempts;
                bool valid = true;
                for (const auto& other : bin.Circles)
                {
                    if (stop()) { valid = false; break; }
                    if (std::hypot(center.X - other.Center.X, center.Y - other.Center.Y) + p.Tolerance <
                        part.Geometry.Radius + other.Radius + p.PartGap) { valid = false; break; }
                }
                if (valid && (!best || ContactRank(bin, center, part.Geometry.Radius) < ContactRank(bin, *best, part.Geometry.Radius))) best = center;
            }
            return best;
        };
        const auto commit = [&](CircleBin& bin, std::size_t index, Point center)
        {
            const auto& part = p.Parts[index];
            const auto angles = CandidateAngles(part, p.Tolerance);
            const double angle = angles.empty() ? 0 : angles.front();
            const auto pivot = Transform(part.Geometry.Center, {0, 0, angle});
            bin.Output.Placements.push_back({part.ID, consumed[index]++,
                {center.X - pivot.X, center.Y - pivot.Y, angle}, placedCount + 1, false});
            bin.Output.MaterialArea += Area(part.Geometry);
            bin.Circles.push_back({index, center, part.Geometry.Radius});
            ++placedCount;
        };
        const auto seedRepeatedRadius = [&](CircleBin& bin, std::size_t first)
        {
            if ((order != CircleOrder::LargeFirst && order != CircleOrder::SquareThenHoles) || !options.EnableCircleLattice) return false;
            const double radius = p.Parts[first].Geometry.Radius;
            std::vector<std::size_t> equivalent;
            std::size_t quantity = 0;
            for (std::size_t i = 0; i < p.Parts.size(); ++i)
                if (p.Parts[i].Geometry.Radius == radius && p.Parts[i].Material == p.Stocks[bin.StockIndex].Material && consumed[i] < p.Parts[i].Quantity)
                { equivalent.push_back(i); quantity += p.Parts[i].Quantity - consumed[i]; }
            if (quantity < 2) return false;
            std::sort(equivalent.begin(), equivalent.end(), [&](auto a, auto b) { return p.Parts[a].ID < p.Parts[b].ID; });
            const double width = bin.Output.Width - 2 * p.EdgeMargin, height = bin.Output.Height - 2 * p.EdgeMargin;
            const double step = 2 * radius + p.PartGap;
            std::vector<Point> best;
            double bestArea = std::numeric_limits<double>::infinity();
            for (int transpose = 0; transpose < 2; ++transpose)
                for (int hex = 0; hex < (order==CircleOrder::SquareThenHoles?1:2); ++hex) for (int phase = 0; phase < (hex ? 2 : 1); ++phase)
                {
                    const double w = transpose ? height : width, h = transpose ? width : height;
                    const double pitch = step * (hex ? std::sqrt(3.0) / 2 : 1);
                    std::vector<Point> centers;
                    double right = 0, top = 0;
                    for (std::size_t row = 0; !stop(); ++row)
                    {
                        const double y = radius + row * pitch;
                        if (!std::isfinite(y) || y + radius > h + p.Tolerance) break;
                        const double shift = hex && (row + phase) % 2 ? step / 2 : 0;
                        for (double x = radius + shift; x + radius <= w + p.Tolerance; x += step)
                        {
                            const Point center = transpose ? Point{y, x} : Point{x, y};
                            centers.push_back({center.X + p.EdgeMargin, center.Y + p.EdgeMargin});
                            right = std::max(right, center.X + radius); top = std::max(top, center.Y + radius);
                            if (centers.size() >= quantity || centers.size() >= 10000) break;
                        }
                        if (centers.size() >= quantity || centers.size() >= 10000) break;
                    }
                    const double area = right * top;
                    if (centers.size() > best.size() || (centers.size() == best.size() && area < bestArea))
                    { best = std::move(centers); bestArea = area; }
                }
            if (best.empty() || stop()) return false;
            std::size_t type = 0, count = 0;
            for (auto center : best)
            {
                if (stop()) break;
                while (type < equivalent.size() && consumed[equivalent[type]] == p.Parts[equivalent[type]].Quantity) ++type;
                if (type == equivalent.size()) break;
                ++result.Stats.PlacementAttempts;
                bool valid = true;
                for (const auto& other : bin.Circles)
                {
                    if (stop()) { valid = false; break; }
                    if (std::hypot(center.X - other.Center.X, center.Y - other.Center.Y) + p.Tolerance < radius + other.Radius + p.PartGap)
                    { valid = false; break; }
                }
                if (!valid) break;
                commit(bin, equivalent[type], center);
                ++count;
            }
            if (count)
            {
                ++result.Stats.CircleLatticeGroups;
                if(order==CircleOrder::SquareThenHoles)
                {
                    bin.SeedRadius=radius;
                    std::vector<double> xs,ys;
                    for(const auto& circle:bin.Circles){xs.push_back(circle.Center.X);ys.push_back(circle.Center.Y);}
                    const auto unique=[&](auto& values)
                    {std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end(),[&](double a,double b){return std::abs(a-b)<=p.Tolerance;}),values.end());};
                    unique(xs);unique(ys);
                    for(std::size_t y=1;y<ys.size()&&bin.SquareVoids.size()<10000;++y)
                        for(std::size_t x=1;x<xs.size()&&bin.SquareVoids.size()<10000;++x)
                            bin.SquareVoids.push_back({(xs[x-1]+xs[x])/2,(ys[y-1]+ys[y])/2});
                }
            }
            return count > 0;
        };
        result.Stats.PreparationSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        while (!stop())
        {
            const auto parts = candidates();
            if (parts.empty()) break;
            bool placed = false;
            for (const auto index : parts)
            {
                std::optional<std::pair<std::size_t, Point>> best;
                for (std::size_t bi = 0; bi < bins.size() && !stop(); ++bi)
                    if (auto center = find(bins[bi], index))
                        if (!best || ContactRank(bins[bi], *center, p.Parts[index].Geometry.Radius) <
                            ContactRank(bins[best->first], best->second, p.Parts[index].Geometry.Radius)) best = {bi, *center};
                if (best) { commit(bins[best->first], index, best->second); placed = true; break; }
                if (stop()) break;
            }
            if (placed) continue;
            if (stop()) break;
            std::vector<std::size_t> stockOrder(p.Stocks.size());
            std::iota(stockOrder.begin(), stockOrder.end(), 0);
            std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
            { return p.Stocks[a].Cost / (p.Stocks[a].Width * p.Stocks[a].Height) < p.Stocks[b].Cost / (p.Stocks[b].Width * p.Stocks[b].Height); });
            for (auto si : stockOrder)
            {
                if (stockUsed[si] >= p.Stocks[si].Quantity || stop()) continue;
                const auto& stock = p.Stocks[si];
                CircleBin bin{si, {stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0}, {}};
                for (const auto index : parts)
                {
                    if (p.Parts[index].Material == stock.Material && seedRepeatedRadius(bin, index))
                    { ++stockUsed[si]; bins.push_back(std::move(bin)); placed = true; break; }
                    if (auto center = find(bin, index))
                    { commit(bin, index, *center); ++stockUsed[si]; bins.push_back(std::move(bin)); placed = true; break; }
                    if (stop()) break;
                }
                if (placed) break;
            }
            if (!placed) break;
        }
        for (auto& bin : bins) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t i = 0; i < p.Parts.size(); ++i)
            if (consumed[i] < p.Parts[i].Quantity) result.Remaining.push_back({p.Parts[i].ID, p.Parts[i].Quantity - consumed[i]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        if (cancelled) result.Diagnostics.push_back("Mixed-circle job cancelled; completed placements retained.");
        if (exhausted) result.Diagnostics.push_back("Mixed-circle time or attempt budget exhausted.");
        if (!exhausted && !cancelled && !result.Remaining.empty())
            result.Diagnostics.push_back("Mixed-circle contact candidates or matching stock exhausted; no infeasibility proof.");
        result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        result.Stats.PlacementSeconds=std::max(0.0,result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds);
        return result;
    }
}
