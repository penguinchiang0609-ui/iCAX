#include "SheetNestingAxisCompaction.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr const char* ReportPrefix = "compact.axis.v1 ";

    class Fingerprint
    {
        std::uint64_t Value = 14695981039346656037ull;
    public:
        void Add(std::uint64_t value)
        {
            for (unsigned byte = 0; byte < 8; ++byte)
            {
                Value ^= (value >> (byte * 8)) & 255;
                Value *= 1099511628211ull;
            }
        }
        void Add(double value) { Add(std::bit_cast<std::uint64_t>(value)); }
        void Add(const std::string& value)
        {
            Add(static_cast<std::uint64_t>(value.size()));
            for (const unsigned char byte : value) { Value ^= byte; Value *= 1099511628211ull; }
        }
        std::uint64_t Get() const { return Value; }
    };
    std::uint64_t PoseFingerprint(const Result& result)
    {
        Fingerprint hash;
        hash.Add(static_cast<std::uint64_t>(result.State));
        std::vector<const Layout*> boards;
        for (const auto& board : result.Layouts) boards.push_back(&board);
        std::sort(boards.begin(), boards.end(), [](auto a, auto b)
        { return std::tie(a->StockID, a->StockInstance) < std::tie(b->StockID, b->StockInstance); });
        hash.Add(static_cast<std::uint64_t>(boards.size()));
        for (const auto* board : boards)
        {
            hash.Add(board->StockID); hash.Add(static_cast<std::uint64_t>(board->StockInstance));
            hash.Add(board->Width); hash.Add(board->Height);
            std::vector<const Placement*> placements;
            for (const auto& placement : board->Placements) placements.push_back(&placement);
            std::sort(placements.begin(), placements.end(), [](auto a, auto b)
            { return std::tie(a->PartID, a->Instance) < std::tie(b->PartID, b->Instance); });
            hash.Add(static_cast<std::uint64_t>(placements.size()));
            for (const auto* placement : placements)
            {
                hash.Add(placement->PartID); hash.Add(static_cast<std::uint64_t>(placement->Instance));
                hash.Add(placement->Transform.X); hash.Add(placement->Transform.Y);
                hash.Add(placement->Transform.AngleDegrees);
                hash.Add(static_cast<std::uint64_t>(placement->Transform.Mirrored));
                hash.Add(static_cast<std::uint64_t>(placement->Group));
                hash.Add(static_cast<std::uint64_t>(placement->GapFiller));
            }
        }
        std::vector<std::pair<std::string, std::size_t>> remaining;
        for (const auto& item : result.Remaining) remaining.emplace_back(item.PartID, item.Quantity);
        std::sort(remaining.begin(), remaining.end());
        hash.Add(static_cast<std::uint64_t>(remaining.size()));
        for (const auto& [id, quantity] : remaining) { hash.Add(id); hash.Add(static_cast<std::uint64_t>(quantity)); }
        return hash.Get();
    }
    std::uint64_t InputFingerprint(const Problem& problem)
    {
        Fingerprint hash;
        hash.Add(problem.PartGap); hash.Add(problem.EdgeMargin); hash.Add(problem.Tolerance);
        hash.Add(static_cast<std::uint64_t>(problem.AllowCommonEdges));
        std::vector<const Part*> parts;
        for (const auto& part : problem.Parts) parts.push_back(&part);
        std::sort(parts.begin(), parts.end(), [](auto a, auto b) { return a->ID < b->ID; });
        hash.Add(static_cast<std::uint64_t>(parts.size()));
        const auto ring = [&](const std::vector<Point>& points)
        {
            hash.Add(static_cast<std::uint64_t>(points.size()));
            for (const auto point : points) { hash.Add(point.X); hash.Add(point.Y); }
        };
        for (const auto* part : parts)
        {
            hash.Add(part->ID); hash.Add(part->Material);
            hash.Add(static_cast<std::uint64_t>(part->Quantity));
            hash.Add(static_cast<std::uint64_t>(part->FreeRotation));
            hash.Add(static_cast<std::uint64_t>(part->AllowMirror));
            hash.Add(static_cast<std::uint64_t>(part->AllowedAnglesDegrees.size()));
            for (const double angle : part->AllowedAnglesDegrees) hash.Add(angle);
            hash.Add(part->Geometry.Radius); hash.Add(part->Geometry.Center.X); hash.Add(part->Geometry.Center.Y);
            ring(part->Geometry.Outer);
            hash.Add(static_cast<std::uint64_t>(part->Geometry.Holes.size()));
            for (const auto& hole : part->Geometry.Holes) ring(hole);
        }
        std::vector<const Stock*> stocks;
        for (const auto& stock : problem.Stocks) stocks.push_back(&stock);
        std::sort(stocks.begin(), stocks.end(), [](auto a, auto b) { return a->ID < b->ID; });
        hash.Add(static_cast<std::uint64_t>(stocks.size()));
        for (const auto* stock : stocks)
        {
            hash.Add(stock->ID); hash.Add(stock->Material); hash.Add(stock->Width); hash.Add(stock->Height);
            hash.Add(static_cast<std::uint64_t>(stock->Quantity)); hash.Add(stock->Cost);
        }
        return hash.Get();
    }
    const char* ReasonName(AxisCompactionStopReason reason)
    {
        switch (reason)
        {
        case AxisCompactionStopReason::EventNeighborhoodClosed: return "event-neighborhood-closed";
        case AxisCompactionStopReason::PassLimit: return "pass-limit";
        case AxisCompactionStopReason::CandidateLimit: return "candidate-limit";
        case AxisCompactionStopReason::EventLimit: return "event-limit";
        case AxisCompactionStopReason::TimeLimit: return "time-limit";
        case AxisCompactionStopReason::AttemptLimit: return "attempt-limit";
        case AxisCompactionStopReason::Cancelled: return "cancelled";
        case AxisCompactionStopReason::Unsupported: return "unsupported";
        case AxisCompactionStopReason::InvalidInput: return "invalid-input";
        case AxisCompactionStopReason::NumericalUncertainty: return "numerical-uncertainty";
        default: return "failed";
        }
    }
    std::string Serialize(const AxisCompactionReport& report,std::size_t inheritedBoundaries,
        std::size_t inheritedPairs,std::size_t sourceBoundaryBlocks,std::size_t sourcePairBlocks)
    {
        std::ostringstream out;
        out << std::setprecision(17) << ReportPrefix
            << "closed=" << report.Closed << " supported=" << report.Supported
            << " validated=" << report.GeometryValidated << " reason=" << static_cast<int>(report.StopReason)
            << " tolerance=" << report.ClosureTolerance << " passes=" << report.CompletedPasses
            << " moves=" << report.Moves << " singles=" << report.SingleMoves
            << " tagged=" << report.TaggedGroupMoves << " whole=" << report.WholeSheetMoves
            << " common=" << report.CommonEdgeGroupMoves
            << " candidates=" << report.CandidateTests << " events=" << report.ContactEvents
            << " output=" << report.OutputFingerprint << " problem=" << report.ProblemFingerprint
            << " guard=nominal-changes-source-invariants-v2"
            << " inherited-boundary-checks=" << inheritedBoundaries << " inherited-pair-checks=" << inheritedPairs
            << " source-nominal-boundary-blocks=" << sourceBoundaryBlocks << " source-nominal-pair-blocks=" << sourcePairBlocks
            << " inheritance-counter-scope=verified-constraint-probes"
            << " source-constraints=validated-and-exactly-invariant changed-constraints=nominal-material"
            << " stop=" << ReasonName(report.StopReason)
            << " scope=bounded-fixed-angle-current-event-component-single-tagged-common-whole";
        return out.str();
    }
    struct Segment { Point A, B; };
    struct Features
    {
        Rect Box;
        std::vector<Point> Vertices;
        std::vector<Segment> Edges;
        Point Center;
        double Radius = 0;
    };
    Features MakeFeatures(const Shape& shape, Pose pose)
    {
        Features result;
        result.Box = Detail::Bounds(shape, pose);
        result.Radius = shape.Radius;
        if (shape.Radius > 0) { result.Center = Detail::Transform(shape.Center, pose); return result; }
        const auto append = [&](const std::vector<Point>& ring)
        {
            if (ring.empty()) return;
            std::vector<Point> world;
            for (const auto point : ring) world.push_back(Detail::Transform(point, pose));
            if (world.size() > 1 && world.front().X == world.back().X && world.front().Y == world.back().Y) world.pop_back();
            result.Vertices.insert(result.Vertices.end(), world.begin(), world.end());
            for (std::size_t i = 0; i < world.size(); ++i)
            {
                const auto a = world[i], b = world[(i + 1) % world.size()];
                if (a.X != b.X || a.Y != b.Y) result.Edges.push_back({a, b});
            }
        };
        append(shape.Outer);
        for (const auto& hole : shape.Holes) append(hole);
        return result;
    }
    double Coordinate(Point point, bool xAxis) { return xAxis ? point.X : point.Y; }
    double Low(Rect box, bool xAxis) { return xAxis ? box.X : box.Y; }
    bool Near(Rect a, Rect b, double distance)
    {
        return a.X + a.Width + distance >= b.X && b.X + b.Width + distance >= a.X
            && a.Y + a.Height + distance >= b.Y && b.Y + b.Height + distance >= a.Y;
    }

    class AxisCompaction final : public IPostProcessor
    {
        AxisCompactionSettings Settings;
    public:
        explicit AxisCompaction(AxisCompactionSettings settings) : Settings(settings) {}
        Result Run(const Problem& problem, const Result& source, const Options& options) override
        {
            const auto started = Clock::now();
            Result result = source;
            Statistics work;
            work.VerificationSeconds = 0;
            work.EvaluationSeconds = 0;
            AxisCompactionReport report;
            report.ClosureTolerance = std::isfinite(problem.Tolerance) && problem.Tolerance > 0
                ? std::max(problem.Tolerance * 4, 1e-9) : 0;
            report.ProblemFingerprint = InputFingerprint(problem);
            bool interrupted = false;
            bool numericalUncertainty = false;
            std::size_t inheritedBoundaryChecks=0,inheritedPairChecks=0;
            std::size_t sourceBoundaryBlocks=0,sourcePairBlocks=0;
            const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
            const auto lifeStop = [&]
            {
                if (interrupted) return true;
                if (options.Cancel && options.Cancel())
                {
                    report.StopReason = AxisCompactionStopReason::Cancelled;
                    interrupted = true; return true;
                }
                if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 || elapsed() >= options.TimeLimitSeconds)
                {
                    report.StopReason = AxisCompactionStopReason::TimeLimit;
                    interrupted = true; return true;
                }
                return false;
            };
            const auto stop = [&]
            {
                if (lifeStop()) return true;
                if (work.PlacementAttempts >= options.MaxPlacementAttempts)
                {
                    report.StopReason = AxisCompactionStopReason::AttemptLimit;
                    interrupted = true; return true;
                }
                return false;
            };
            const auto finish = [&]
            {
                if (numericalUncertainty&&report.StopReason==AxisCompactionStopReason::EventNeighborhoodClosed)
                    report.StopReason=AxisCompactionStopReason::NumericalUncertainty;
                if (report.Moves)
                {
                    const auto start = Clock::now();
                    const auto check = Validate(problem, result);
                    work.VerificationSeconds += std::chrono::duration<double>(Clock::now() - start).count();
                    if (!check.Valid)
                    {
                        result = source;
                        report.Moves = report.SingleMoves = report.TaggedGroupMoves = report.WholeSheetMoves = 0;
                        report.CommonEdgeGroupMoves = 0;
                        report.Closed = false;
                        report.GeometryValidated = false;
                        report.StopReason = AxisCompactionStopReason::Failed;
                    }
                    else { report.GeometryValidated = true; result.GeometryValidated = true; }
                }
                // Final validation can consume the remaining lifecycle budget.
                if (report.StopReason == AxisCompactionStopReason::EventNeighborhoodClosed) stop();
                report.Closed = report.StopReason == AxisCompactionStopReason::EventNeighborhoodClosed;
                if (report.StopReason == AxisCompactionStopReason::Cancelled) result.State = Status::Cancelled;
                result.Stats = work;
                result.Stats.CombinationGroups = source.Stats.CombinationGroups;
                result.Stats.CircleLatticeGroups = source.Stats.CircleLatticeGroups;
                result.Stats.GapFilledParts = source.Stats.GapFilledParts;
                result.Stats.CandidatePatterns = report.ContactEvents;
                result.Stats.ElapsedSeconds = result.Stats.PostProcessingSeconds = elapsed();
                report.OutputFingerprint = PoseFingerprint(result);
                result.Diagnostics.push_back(Serialize(report,inheritedBoundaryChecks,inheritedPairChecks,
                    sourceBoundaryBlocks,sourcePairBlocks));
                return result;
            };
            try
            {
                if (stop()) return finish();
                if (source.State == Status::Cancelled)
                { report.StopReason = AxisCompactionStopReason::Cancelled; return finish(); }
                if ((source.State != Status::Complete && source.State != Status::Partial) || !Detail::CheckProblem(problem).empty())
                {
                    report.Supported = false; report.StopReason = AxisCompactionStopReason::InvalidInput; return finish();
                }
                const auto verifyStart = Clock::now();
                const auto initial = Validate(problem, source);
                work.VerificationSeconds += std::chrono::duration<double>(Clock::now() - verifyStart).count();
                if (!initial.Valid)
                {
                    report.Supported = false; report.StopReason = AxisCompactionStopReason::InvalidInput; return finish();
                }
                report.GeometryValidated = true;
                result.GeometryValidated = true;
                std::map<std::string, const Part*> parts;
                for (const auto& part : problem.Parts) parts.emplace(part.ID, &part);

                for (std::size_t pass = 0; pass < Settings.MaxPasses; ++pass)
                {
                    bool changed = false;
                    for (auto& board : result.Layouts)
                    {
                        if (stop()) return finish();
                        const auto count = board.Placements.size();
                        std::vector<const Shape*> shapes(count);
                        std::vector<Features> features(count);
                        std::vector<AxisSafety::Geometry> strictFeatures(count);
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            shapes[i] = &parts.at(board.Placements[i].PartID)->Geometry;
                            features[i] = MakeFeatures(*shapes[i], board.Placements[i].Transform);
                            strictFeatures[i]=AxisSafety::Make(*shapes[i],board.Placements[i].Transform);
                            const auto sourceContainment=AxisSafety::Containment(strictFeatures[i],board.Width,board.Height,problem.EdgeMargin);
                            if (sourceContainment==AxisSafety::Verdict::Uncertain)
                            {
                                report.StopReason=AxisCompactionStopReason::NumericalUncertainty;
                                return finish();
                            }
                            if (sourceContainment==AxisSafety::Verdict::Blocked) ++sourceBoundaryBlocks;
                            for (std::size_t j=0;j<i;++j)
                            {
                                if (stop()) return finish();
                                if (Near(features[i].Box,features[j].Box,problem.PartGap+problem.Tolerance+
                                    strictFeatures[i].Roundoff+strictFeatures[j].Roundoff))
                                {
                                    const auto sourcePair=AxisSafety::Pair(strictFeatures[i],strictFeatures[j],problem.PartGap,
                                        problem.AllowCommonEdges,problem.Tolerance);
                                    if (sourcePair==AxisSafety::Verdict::Uncertain)
                                    {
                                        report.StopReason=AxisCompactionStopReason::NumericalUncertainty;
                                        return finish();
                                    }
                                    if (sourcePair==AxisSafety::Verdict::Blocked) ++sourcePairBlocks;
                                }
                            }
                        }
                        for (const bool xAxis : std::array<bool, 2>{Settings.XFirst, !Settings.XFirst})
                        {
                            if (stop()) return finish();
                            enum class GroupKind { Single, Tagged, CommonEdge, Whole };
                            struct Group { std::vector<std::size_t> Members; GroupKind Kind; };
                            std::vector<Group> groups;
                            std::set<std::vector<std::size_t>> seen;
                            const auto addGroup = [&](std::vector<std::size_t> members, GroupKind kind)
                            {
                                if (members.empty()) return;
                                std::sort(members.begin(), members.end());
                                if (members.size() == 1) kind = GroupKind::Single;
                                if (seen.insert(members).second) groups.push_back({std::move(members), kind});
                            };
                            std::vector<std::size_t> whole(count);
                            std::iota(whole.begin(), whole.end(), 0);
                            addGroup(whole, GroupKind::Whole);
                            std::map<std::size_t, std::vector<std::size_t>> tagged;
                            for (std::size_t i = 0; i < count; ++i)
                                if (board.Placements[i].Group) tagged[board.Placements[i].Group].push_back(i);
                            for (const auto& [tag, members] : tagged) { (void)tag; addGroup(members, GroupKind::Tagged); }
                            if (problem.AllowCommonEdges)
                            {
                                std::vector<std::size_t> parents = whole;
                                const auto root = [&](std::size_t index)
                                {
                                    while (parents[index] != index) index = parents[index];
                                    return index;
                                };
                                for (std::size_t i = 0; i < count; ++i) for (std::size_t j = 0; j < i; ++j)
                                {
                                    if (lifeStop()) return finish();
                                    if (Near(features[i].Box, features[j].Box, problem.Tolerance) &&
                                        Detail::ShapesHaveCommonEdge(*shapes[i], board.Placements[i].Transform,
                                            *shapes[j], board.Placements[j].Transform, problem.Tolerance))
                                        parents[root(i)] = root(j);
                                }
                                std::map<std::size_t, std::vector<std::size_t>> components;
                                for (std::size_t i = 0; i < count; ++i) components[root(i)].push_back(i);
                                for (const auto& [component, members] : components)
                                {
                                    (void)component;
                                    if (members.size() > 1) addGroup(members, GroupKind::CommonEdge);
                                }
                            }
                            auto singles = whole;
                            std::stable_sort(singles.begin(), singles.end(), [&](auto a, auto b)
                            { return Low(features[a].Box, xAxis) < Low(features[b].Box, xAxis); });
                            for (const auto i : singles) addGroup({i}, GroupKind::Single);

                            for (const auto& group : groups)
                            {
                                if (stop()) return finish();
                                std::vector<bool> moving(count, false);
                                double limit = std::numeric_limits<double>::infinity();
                                for (const auto i : group.Members)
                                {
                                    moving[i] = true;
                                    limit = std::min(limit, Low(features[i].Box, xAxis) -
                                        AxisSafety::ProposalMargin(strictFeatures[i],problem.EdgeMargin));
                                }
                                if (!std::isfinite(limit) || limit <= report.ClosureTolerance) continue;
                                std::set<double> events;
                                bool eventLimited = false;
                                const auto emit = [&](long double value)
                                {
                                    if (!std::isfinite(value) || value < 0 || value > limit) return;
                                    const double event = static_cast<double>(value);
                                    if (events.contains(event)) return;
                                    if (events.size() >= Settings.MaxContactEventsPerMove)
                                    { eventLimited = true; return; }
                                    events.insert(event); ++report.ContactEvents;
                                };
                                emit(0); emit(limit);
                                const Point direction = xAxis ? Point{-1, 0} : Point{0, -1};
                                const auto distanceRoots = [&](Point point, Point fixed, Point velocity, double radius)
                                {
                                    if (radius < 0) return;
                                    const long double dx = static_cast<long double>(point.X) - fixed.X;
                                    const long double dy = static_cast<long double>(point.Y) - fixed.Y;
                                    const long double along = dx * velocity.X + dy * velocity.Y;
                                    const long double across = dx * velocity.Y - dy * velocity.X;
                                    const long double r = radius;
                                    if (std::abs(across) > r) return;
                                    const long double square = (r - std::abs(across)) * (r + std::abs(across));
                                    const long double root = std::sqrt(std::max(0.L, square));
                                    emit(-along - root); emit(-along + root);
                                };
                                const auto pointSegment = [&](Point point, const Segment& edge, Point velocity, double radius)
                                {
                                    const long double ex = static_cast<long double>(edge.B.X) - edge.A.X;
                                    const long double ey = static_cast<long double>(edge.B.Y) - edge.A.Y;
                                    const long double px = static_cast<long double>(point.X) - edge.A.X;
                                    const long double py = static_cast<long double>(point.Y) - edge.A.Y;
                                    const long double length2 = ex * ex + ey * ey;
                                    if (length2 == 0) { distanceRoots(point, edge.A, velocity, radius); return; }
                                    const long double length = std::sqrt(length2);
                                    const long double initialCross = ex * py - ey * px;
                                    const long double crossRate = ex * velocity.Y - ey * velocity.X;
                                    const long double initialDot = ex * px + ey * py;
                                    const long double dotRate = ex * velocity.X + ey * velocity.Y;
                                    if (crossRate != 0)
                                        for (const int side : {-1, 1})
                                        {
                                            const long double t = (side * radius * length - initialCross) / crossRate;
                                            const long double projection = (initialDot + t * dotRate) / length2;
                                            const long double allowance = 64 * std::numeric_limits<double>::epsilon() * (1 + std::abs(projection));
                                            if (projection >= -allowance && projection <= 1 + allowance) emit(t);
                                        }
                                    // Projection endpoints matter for a parallel
                                    // overlapping strip. For a nonparallel edge,
                                    // the line-distance and endpoint-circle roots
                                    // above/below already cover contact changes.
                                    if (crossRate == 0 && std::abs(initialCross) <= radius * length && dotRate != 0)
                                        for (const long double target : {0.L, length2})
                                            emit((target - initialDot) / dotRate);
                                    distanceRoots(point, edge.A, velocity, radius);
                                    distanceRoots(point, edge.B, velocity, radius);
                                };
                                const auto pairEvents = [&](const Features& a, const Features& b,
                                    const AxisSafety::Geometry& aStrict,const AxisSafety::Geometry& bStrict)
                                {
                                    const double gap=AxisSafety::ProposalClearance(aStrict,bStrict,problem.PartGap);
                                    if (a.Radius > 0 && b.Radius > 0)
                                    {
                                        const double radius = a.Radius + b.Radius + gap;
                                        distanceRoots(a.Center, b.Center, direction, radius);
                                        return;
                                    }
                                    if (a.Radius > 0 || b.Radius > 0)
                                    {
                                        const bool movingCircle = a.Radius > 0;
                                        const auto& circle = movingCircle ? a : b;
                                        const auto& polygon = movingCircle ? b : a;
                                        const Point velocity = movingCircle ? direction : Point{-direction.X, -direction.Y};
                                        const double radius = circle.Radius + gap;
                                        for (const auto& edge : polygon.Edges)
                                        {
                                            if (lifeStop() || eventLimited) return;
                                            pointSegment(circle.Center, edge, velocity, radius);
                                        }
                                        return;
                                    }
                                    // Keep zero-gap contacts even when gap is positive: the
                                    // original predicate can waive the pair gap at a shared edge.
                                    const std::array<double, 2> distances{0, gap};
                                    for (const auto point : a.Vertices) for (const auto& edge : b.Edges)
                                    {
                                        if (lifeStop() || eventLimited) return;
                                        for (const double radius : distances) pointSegment(point, edge, direction, radius);
                                    }
                                    for (const auto point : b.Vertices) for (const auto& edge : a.Edges)
                                    {
                                        if (lifeStop() || eventLimited) return;
                                        for (const double radius : distances)
                                            pointSegment(point, edge, {-direction.X, -direction.Y}, radius);
                                    }
                                };
                                for (const auto i : group.Members)
                                {
                                    Rect swept = features[i].Box;
                                    if (xAxis) { swept.X -= limit; swept.Width += limit; }
                                    else { swept.Y -= limit; swept.Height += limit; }
                                    for (std::size_t j = 0; j < count; ++j)
                                    {
                                        if (lifeStop()) return finish();
                                        if (eventLimited) break;
                                        if (!moving[j] && Near(swept, features[j].Box, problem.PartGap + problem.Tolerance))
                                            pairEvents(features[i],features[j],strictFeatures[i],strictFeatures[j]);
                                    }
                                    if (eventLimited) break;
                                }
                                if (interrupted) return finish();
                                if (eventLimited)
                                { report.StopReason = AxisCompactionStopReason::EventLimit; return finish(); }

                                std::size_t candidateTests = 0;
                                std::vector<Pose> bestPoses;
                                double bestDistance = 0;
                                const auto safe = [&](const std::vector<Pose>& poses)
                                {
                                    if (lifeStop()) return false;
                                    if (candidateTests >= Settings.MaxCandidatesPerMove)
                                    {
                                        report.StopReason = AxisCompactionStopReason::CandidateLimit;
                                        interrupted = true; return false;
                                    }
                                    if (work.PlacementAttempts >= options.MaxPlacementAttempts)
                                    {
                                        report.StopReason = AxisCompactionStopReason::AttemptLimit;
                                        interrupted = true; return false;
                                    }
                                    ++candidateTests; ++report.CandidateTests; ++work.PlacementAttempts;
                                    std::vector<Rect> boxes(count);
                                    std::vector<AxisSafety::Geometry> strictCandidates(count);
                                    for (std::size_t i = 0; i < count; ++i)
                                    {
                                        if (lifeStop()) return false;
                                        if (moving[i] && !Detail::InsideSheet(*shapes[i], poses[i], board.Width, board.Height,
                                            problem.EdgeMargin, problem.Tolerance)) return false;
                                        boxes[i] = moving[i] ? Detail::Bounds(*shapes[i], poses[i]) : features[i].Box;
                                        if (moving[i])
                                        {
                                            strictCandidates[i]=AxisSafety::Make(*shapes[i],poses[i]);
                                            bool inherited=false;
                                            const auto containment=AxisSafety::ContainmentWithSource(strictFeatures[i],strictCandidates[i],
                                                board.Width,board.Height,problem.EdgeMargin,xAxis,inherited);
                                            if (inherited) ++inheritedBoundaryChecks;
                                            if (containment!=AxisSafety::Verdict::Safe)
                                            {
                                                numericalUncertainty=numericalUncertainty||containment==AxisSafety::Verdict::Uncertain;
                                                return false;
                                            }
                                        }
                                    }
                                    for (const auto i : group.Members) for (std::size_t j = 0; j < count; ++j)
                                    {
                                        if (i == j || (moving[j] && j > i)) continue;
                                        if (lifeStop()) return false;
                                        const auto first = std::max(i, j), second = std::min(i, j);
                                        if (Near(boxes[first], boxes[second], problem.PartGap + problem.Tolerance) &&
                                            Detail::ShapesConflict(*shapes[first], poses[first], *shapes[second], poses[second],
                                                problem.PartGap, problem.Tolerance, problem.AllowCommonEdges)) return false;
                                        const auto& firstStrict=moving[first]?strictCandidates[first]:strictFeatures[first];
                                        const auto& secondStrict=moving[second]?strictCandidates[second]:strictFeatures[second];
                                        if (Near(boxes[first],boxes[second],problem.PartGap+problem.Tolerance+
                                            firstStrict.Roundoff+secondStrict.Roundoff))
                                        {
                                            bool inherited=false;
                                            const auto verdict=AxisSafety::PairWithSource(strictFeatures[first],firstStrict,
                                                strictFeatures[second],secondStrict,problem.PartGap,
                                                problem.AllowCommonEdges,problem.Tolerance,inherited);
                                            if (inherited) ++inheritedPairChecks;
                                            if (verdict!=AxisSafety::Verdict::Safe)
                                            {
                                                numericalUncertainty=numericalUncertainty||verdict==AxisSafety::Verdict::Uncertain;
                                                return false;
                                            }
                                        }
                                    }
                                    return true;
                                };
                                const auto tryDistance = [&](double distance, bool& nominalSafe)
                                {
                                    nominalSafe = false;
                                    std::vector<Pose> poses;
                                    for (const auto& placement : board.Placements) poses.push_back(placement.Transform);
                                    double magnitude=std::max(1.,distance);
                                    for (const auto i:group.Members)
                                    {
                                        const auto& old=board.Placements[i].Transform;
                                        const auto& box=features[i].Box;
                                        const auto local=Detail::Bounds(*shapes[i]);
                                        magnitude=std::max({magnitude,std::abs(old.X),std::abs(old.Y),
                                            std::abs(box.X),std::abs(box.Y),std::abs(box.X+box.Width),
                                            std::abs(box.Y+box.Height),std::abs(local.X),std::abs(local.Y),
                                            std::abs(local.X+local.Width),std::abs(local.Y+local.Height)});
                                    }
                                    const double retreatStep=8*std::numeric_limits<double>::epsilon()*magnitude;
                                    for (unsigned retreat = 0; retreat < 9; ++retreat)
                                    {
                                        const double backed=retreat?std::ldexp(retreatStep,retreat-1):0;
                                        const double actualDistance=std::max(0.,distance-backed);
                                        for (const auto i:group.Members)
                                        {
                                            const auto& old=board.Placements[i].Transform;
                                            (xAxis?poses[i].X:poses[i].Y)=(xAxis?old.X:old.Y)-actualDistance;
                                        }
                                        if (safe(poses))
                                        {
                                            nominalSafe = retreat == 0;
                                            double actual = std::numeric_limits<double>::infinity();
                                            for (const auto i : group.Members)
                                            {
                                                const auto old = board.Placements[i].Transform;
                                                actual = std::min(actual, (xAxis ? old.X - poses[i].X : old.Y - poses[i].Y));
                                            }
                                            if (actual > bestDistance) { bestDistance = actual; bestPoses = poses; }
                                            return true;
                                        }
                                        if (interrupted) return false;
                                    }
                                    return false;
                                };
                                // Exact-value deduplication preserves isolated contact events.
                                // Every open cell and endpoint uses the original pair predicate.
                                // The connected scope stops at its first infeasible probe.
                                double previous = 0;
                                for (const double event : events)
                                {
                                    if (event <= 0) continue;
                                    bool nominalSafe = false;
                                    const double middle = previous + (event - previous) * .5;
                                    if (middle > previous && middle < event &&
                                        (!tryDistance(middle, nominalSafe) || !nominalSafe)) break;
                                    if (interrupted || !tryDistance(event, nominalSafe) || !nominalSafe) break;
                                    previous = event;
                                }
                                if (bestDistance > report.ClosureTolerance && !bestPoses.empty())
                                {
                                    for (const auto i : group.Members)
                                    {
                                        board.Placements[i].Transform = bestPoses[i];
                                        features[i] = MakeFeatures(*shapes[i], bestPoses[i]);
                                        strictFeatures[i]=AxisSafety::Make(*shapes[i],bestPoses[i]);
                                    }
                                    ++report.Moves; changed = true;
                                    if (group.Kind == GroupKind::Whole) ++report.WholeSheetMoves;
                                    else if (group.Kind == GroupKind::Tagged) ++report.TaggedGroupMoves;
                                    else if (group.Kind == GroupKind::CommonEdge) ++report.CommonEdgeGroupMoves;
                                    else ++report.SingleMoves;
                                }
                                if (interrupted) return finish();
                            }
                        }
                    }
                    ++report.CompletedPasses;
                    if (stop()) return finish();
                    if (!changed)
                    {
                        report.StopReason = AxisCompactionStopReason::EventNeighborhoodClosed;
                        return finish();
                    }
                }
                report.StopReason = AxisCompactionStopReason::PassLimit;
                return finish();
            }
            catch (const std::exception& error)
            {
                report.Closed = false; report.StopReason = AxisCompactionStopReason::Failed;
                result.Diagnostics.push_back(std::string("compact.axis failure: ") + error.what());
                return finish();
            }
            catch (...)
            {
                report.Closed = false; report.StopReason = AxisCompactionStopReason::Failed;
                result.Diagnostics.push_back("compact.axis failure: unknown exception");
                return finish();
            }
        }
    };
}

PostProcessStage MakeAxisCompactionStage(AxisCompactionSettings settings)
{
    if (!settings.MaxPasses || settings.MaxPasses > 100000 ||
        !settings.MaxCandidatesPerMove || settings.MaxCandidatesPerMove > 1000000 ||
        !settings.MaxContactEventsPerMove || settings.MaxContactEventsPerMove > 1000000)
        throw std::invalid_argument("Invalid fixed-angle axis compaction settings");
    return {"compact.axis", [settings] { return std::make_unique<AxisCompaction>(settings); }};
}

std::optional<AxisCompactionReport> GetAxisCompactionReport(const Result& result)
{
    for (auto entry = result.Diagnostics.rbegin(); entry != result.Diagnostics.rend(); ++entry)
    {
        if (entry->rfind(ReportPrefix, 0) != 0) continue;
        std::istringstream input(entry->substr(std::char_traits<char>::length(ReportPrefix)));
        std::map<std::string, std::string> values;
        std::string item;
        while (input >> item)
        {
            const auto equal = item.find('=');
            if (equal != std::string::npos) values[item.substr(0, equal)] = item.substr(equal + 1);
        }
        try
        {
            if (values.at("guard")!="nominal-changes-source-invariants-v2"||
                values.at("source-constraints")!="validated-and-exactly-invariant"||
                values.at("changed-constraints")!="nominal-material") return {};
            AxisCompactionReport report;
            report.Closed = std::stoul(values.at("closed")) != 0;
            report.Supported = std::stoul(values.at("supported")) != 0;
            report.GeometryValidated = std::stoul(values.at("validated")) != 0;
            const auto reason = std::stoul(values.at("reason"));
            if (reason > static_cast<unsigned>(AxisCompactionStopReason::Failed)) return {};
            report.StopReason = static_cast<AxisCompactionStopReason>(reason);
            report.ClosureTolerance = std::stod(values.at("tolerance"));
            report.CompletedPasses = std::stoull(values.at("passes"));
            report.Moves = std::stoull(values.at("moves")); report.SingleMoves = std::stoull(values.at("singles"));
            report.TaggedGroupMoves = std::stoull(values.at("tagged")); report.WholeSheetMoves = std::stoull(values.at("whole"));
            if (const auto common = values.find("common"); common != values.end())
                report.CommonEdgeGroupMoves = std::stoull(common->second);
            report.CandidateTests = std::stoull(values.at("candidates")); report.ContactEvents = std::stoull(values.at("events"));
            report.OutputFingerprint = std::stoull(values.at("output"));
            report.ProblemFingerprint = std::stoull(values.at("problem"));
            if (report.OutputFingerprint != PoseFingerprint(result) || !std::isfinite(report.ClosureTolerance) ||
                report.ClosureTolerance < 0 ||
                report.Closed != (report.StopReason == AxisCompactionStopReason::EventNeighborhoodClosed)) return {};
            return report;
        }
        catch (...) { return {}; }
    }
    return {};
}

std::optional<AxisCompactionReport> GetAxisCompactionReport(const Problem& problem, const Result& result)
{
    auto report = GetAxisCompactionReport(result);
    if (!report || report->ProblemFingerprint != InputFingerprint(problem)) return {};
    return report;
}
}
