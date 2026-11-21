#include "SheetNestingFeatureConstruction.h"
#include "SheetNestingNfp.h"
#include "NestingShapeFeatures.h"
#include "NestingAxisSafety.h"
#include "NestingExecution.h"
#include "NestingConstructionObserver.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace iCAX::SheetNesting
{
namespace
{
using Clock = std::chrono::steady_clock;
using Clipper2Lib::PathD;
using Clipper2Lib::PathsD;
using Detail::Pattern;
using Detail::ConstructionObserver;
using Detail::TracePattern;
using Detail::TraceShape;

struct Block
{
    Pattern Value;
    std::vector<std::pair<std::size_t, std::size_t>> Demand;
    double Saving = 0, Fill = 0;
    bool Feature = false, Array = false;
    std::uint64_t TraceProposal = 0;
};
struct SearchState
{
    std::vector<Layout> Boards;
    std::vector<std::size_t> Used, StockUsed;
    std::size_t FeatureGroups = 0, Arrays = 0;
    double PlacedArea = 0;
    std::uint64_t TraceState = 0, TraceParent = 0;
};
struct Child
{
    SearchState State;
    double Increment = 0, Saving = 0, Fill = 0, Right = 0, Top = 0;
    std::size_t RetainedBytes = 0;
};
struct Lattice
{
    Point U, V;
    double Determinant = 0, Length = 0;
};

bool InForbidden(Point point, const PathsD &paths)
{
    int winding = 0;
    for (const auto &path : paths)
    {
        const auto location = Clipper2Lib::PointInPolygon(Clipper2Lib::PointD(point.X, point.Y), path);
        if (location == Clipper2Lib::PointInPolygonResult::IsOn)
            return false;
        if (location == Clipper2Lib::PointInPolygonResult::IsInside)
            winding += Clipper2Lib::IsPositive(path) ? 1 : -1;
    }
    return winding != 0;
}

std::size_t PatternBytes(const Pattern &pattern)
{
    return 2 * sizeof(Block) + pattern.Members.capacity() * sizeof(Detail::Member);
}

std::size_t GeometryWorkBytes(const Shape &shape)
{
    std::size_t vertices = shape.Outer.size();
    for (const auto &hole : shape.Holes)
        vertices += hole.size();
    // Expansion coordinates, interval hulls, boundary split events and
    // their vector bookkeeping coexist in the nominal geometry guard.
    return 4096 + vertices * 2048;
}

Rect BoardBounds(const Problem &problem, const Layout &board)
{
    double left = board.Width, bottom = board.Height, right = 0, top = 0;
    for (const auto &placed : board.Placements)
    {
        const auto part = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                       [&](const Part &item) { return item.ID == placed.PartID; });
        const auto box = Detail::Bounds(part->Geometry, placed.Transform);
        left = std::min(left, box.X);
        bottom = std::min(bottom, box.Y);
        right = std::max(right, box.X + box.Width);
        top = std::max(top, box.Y + box.Height);
    }
    return board.Placements.empty() ? Rect{} : Rect{left, bottom, right - left, top - bottom};
}

std::size_t StateBytes(const SearchState &state)
{
    std::size_t bytes = sizeof(SearchState) + state.Boards.capacity() * sizeof(Layout) +
                        (state.Used.capacity() + state.StockUsed.capacity()) * sizeof(std::size_t);
    for (const auto &board : state.Boards)
    {
        bytes += board.StockID.capacity() + board.Placements.capacity() * sizeof(Placement);
        for (const auto &placed : board.Placements)
            bytes += placed.PartID.capacity();
    }
    return bytes;
}

class FeatureConstructor final : public IStrategy
{
  public:
    FeatureConstructor(FeatureConstructionSettings settings, std::shared_ptr<NfpCache> cache)
        : Settings(settings), Cache(cache ? std::move(cache) : std::make_shared<NfpCache>())
    {
    }

    Result Run(const Problem &problem, const Options &options) override
    {
        const auto started = Clock::now();
        const auto seconds = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
        Result empty;
        empty.State = Status::Partial;
        for (const auto &part : problem.Parts)
            if (part.Quantity)
                empty.Remaining.push_back({part.ID, part.Quantity});
        const auto errors = Detail::CheckProblem(problem);
        if (!errors.empty() || !std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 ||
            !options.MaxPlacementAttempts || !Settings.MaxSearchStates || !Settings.MaxRetainedBytes)
        {
            empty.State = Status::InvalidInput;
            empty.Diagnostics = errors;
            empty.Diagnostics.push_back("feature-construction: invalid input or search limits");
            return empty;
        }

        std::size_t attempts = 0, retained = 0, peak = 0, temporaryPeak = 0, expanded = 0;
        std::size_t nfpHits = 0, unions = 0, generated = 0, complete = 0;
        ConstructionObserver trace(Settings, started, attempts, expanded);
        trace.Emit(ConstructionTraceStage::Begin, [&](ConstructionTraceEvent &event) {
            event.StateID = 1;
            event.Reason = "independent-single-sheet-construction";
        });
        const auto traceEmpty = [&](const char *reason) {
            trace.Emit(ConstructionTraceStage::Stopped, [&](ConstructionTraceEvent &event) {
                event.StateID = 1;
                event.Reason = reason;
                event.ResultStatus = empty.State;
            });
            trace.Emit(
                ConstructionTraceStage::Selected,
                [&](ConstructionTraceEvent &event) {
                    event.StateID = 1;
                    event.Reason = reason;
                    event.ResultStatus = empty.State;
                },
                true);
            if (Settings.Trace)
            {
                std::ostringstream diagnostic;
                diagnostic << "feature-construction trace-events=" << trace.Count
                           << " trace-truncated=" << trace.Truncated << " trace-observer-failed=" << trace.Failed;
                empty.Diagnostics.push_back(diagnostic.str());
            }
        };
        bool cancelled = false, memoryLimited = false;
        const auto stop = [&] {
            if (!cancelled && options.Cancel && options.Cancel())
                cancelled = true;
            return cancelled || seconds() >= options.TimeLimitSeconds || attempts >= options.MaxPlacementAttempts;
        };
        const auto trial = [&] {
            if (stop())
                return false;
            ++attempts;
            return true;
        };
        const auto retain = [&](std::size_t bytes) {
            if (bytes > Settings.MaxRetainedBytes - std::min(retained, Settings.MaxRetainedBytes))
            {
                memoryLimited = true;
                return false;
            }
            retained += bytes;
            peak = std::max(peak, retained);
            return true;
        };
        const auto temporary = [&](std::size_t bytes) {
            if (bytes > Settings.MaxRetainedBytes - std::min(retained, Settings.MaxRetainedBytes))
            {
                memoryLimited = true;
                return false;
            }
            temporaryPeak = std::max(temporaryPeak, bytes);
            peak = std::max(peak, retained + bytes);
            return true;
        };
        const auto buildOptions = [&] {
            NfpBuildOptions build;
            build.TimeLimitSeconds = std::max(1e-6, options.TimeLimitSeconds - seconds());
            build.MaxConvexPairs = std::max<std::size_t>(
                1, std::min<std::size_t>(16384, options.MaxPlacementAttempts -
                                                    std::min(attempts, options.MaxPlacementAttempts)));
            build.Cancel = stop;
            return build;
        };

        const double preparationDeadline = options.TimeLimitSeconds * .50;
        const auto preparationStop = [&] { return stop() || seconds() >= preparationDeadline; };
        const auto encodingStop = [&] { return preparationStop() || seconds() >= options.TimeLimitSeconds * .12; };
        const auto matchingStop = [&] { return preparationStop() || seconds() >= options.TimeLimitSeconds * .24; };
        Detail::ShapeFeatureLimits limits;
        limits.MatchMode = Detail::ShapeFeatureMatchMode::LegacyPositiveObb;
        limits.MaxBytes = Settings.MaxRetainedBytes / 4;
        limits.MaxProposals = std::max<std::size_t>(1, std::min<std::size_t>(256, Settings.MaxArrayVariants * 4));
        limits.MaxAttempts = std::max<std::size_t>(1, options.MaxPlacementAttempts / 4);
        const std::size_t baseBytes = problem.Parts.size() * (sizeof(Detail::ShapeFeatureModel) +
                                                              sizeof(NfpPreparedShape) + sizeof(std::vector<double>));
        if (!retain(baseBytes))
        {
            empty.Diagnostics.push_back("feature-construction: base payload exceeded memory limit");
            traceEmpty("base-payload-memory-limit");
            return empty;
        }
        std::vector<Detail::ShapeFeatureModel> models(problem.Parts.size());
        std::vector<NfpPreparedShape> prepared(problem.Parts.size());
        std::vector<std::vector<double>> directions(problem.Parts.size());
        for (std::size_t i = 0; i < problem.Parts.size() && !encodingStop(); ++i)
        {
            const auto &part = problem.Parts[i];
            if (!part.Quantity)
                continue;
            limits.MaxBytes = std::min(Settings.MaxRetainedBytes / 4, Settings.MaxRetainedBytes - retained);
            models[i] = Detail::EncodeShapeFeatures(part.Geometry, problem.Tolerance, limits, encodingStop);
            if (!retain(models[i].RetainedBytes))
            {
                models[i] = {};
                break;
            }
            prepared[i] = Cache->Prepare(part.Geometry, buildOptions());
        }
        const double encodingSeconds = seconds();
        for (std::size_t i = 0; i < models.size(); ++i)
            trace.Emit(ConstructionTraceStage::Analysis, [&](ConstructionTraceEvent &event) {
                event.StateID = 1;
                event.PartIndex = i;
                event.Reason = models[i].Diagnostic;
                event.AnalysisTruncated = models[i].Truncated;
                event.Geometries.push_back(TraceShape(problem.Parts[i].Geometry));
                for (std::size_t carrierIndex = 0; carrierIndex < models[i].Carriers.size(); ++carrierIndex)
                {
                    const auto &carrier = models[i].Carriers[carrierIndex];
                    const auto kind = static_cast<ConstructionTraceCarrierKind>(carrier.Kind);
                    auto body = TraceShape(carrier.Geometry);
                    body.Role = ConstructionTraceGeometryRole::Carrier;
                    body.CarrierIndex = carrierIndex;
                    body.CarrierKind = kind;
                    event.Geometries.push_back(std::move(body));
                    for (std::size_t featureIndex = 0; featureIndex < carrier.Features.size(); ++featureIndex)
                    {
                        const auto &feature = carrier.Features[featureIndex];
                        ConstructionTraceGeometry residual;
                        residual.Role = feature.Polarity == Detail::FeaturePolarity::Recess
                                            ? ConstructionTraceGeometryRole::Recess
                                            : ConstructionTraceGeometryRole::Protrusion;
                        residual.CarrierIndex = carrierIndex;
                        residual.FeatureIndex = featureIndex;
                        residual.CarrierKind = kind;
                        residual.Rings = feature.Rings;
                        event.Geometries.push_back(std::move(residual));
                        for (const auto &mouth : feature.Mouths)
                            event.Mouths.push_back({carrierIndex, featureIndex, mouth.Start, mouth.End, mouth.Into,
                                                    mouth.Width, mouth.Depth});
                    }
                }
            });
        limits.MaxBytes = std::min(Settings.MaxRetainedBytes / 4, Settings.MaxRetainedBytes - retained);
        const auto matches = Detail::BuildShapeFeatureMatches(problem, models, limits, attempts, matchingStop, Cache);
        if (!retain(matches.RetainedBytes))
        {
            empty.Diagnostics.push_back("feature-construction: match payload exceeded memory limit");
            traceEmpty("match-payload-memory-limit");
            return empty;
        }
        const double matchingSeconds = seconds() - encodingSeconds;
        std::vector<std::uint64_t> matchProposals;
        if (trace.Enabled())
            matchProposals.resize(matches.Matches.size());
        for (std::size_t i = 0; i < matches.Matches.size() && trace.Enabled(); ++i)
        {
            matchProposals[i] = trace.NewProposal();
            trace.Emit(ConstructionTraceStage::Match, [&](ConstructionTraceEvent &event) {
                const auto &match = matches.Matches[i];
                event.StateID = 1;
                event.ProposalID = matchProposals[i];
                event.FeatureDerived = true;
                event.Saving = match.FootprintSaving;
                event.Fill = match.CarrierFill;
                event.FootprintArea = match.CarrierFill > 0 ? match.Value.MaterialArea / match.CarrierFill : 0;
                TracePattern(event, match.Value);
            });
        }
        for (const auto &match : matches.Matches)
            for (const auto &member : match.Value.Members)
                if (member.PartIndex < directions.size() &&
                    Detail::AngleAllowed(problem.Parts[member.PartIndex], member.Transform.AngleDegrees,
                                         problem.Tolerance))
                    directions[member.PartIndex].push_back(member.Transform.AngleDegrees);
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        {
            const auto basic = Detail::CandidateAngles(problem.Parts[i], problem.Tolerance);
            directions[i].insert(directions[i].end(), basic.begin(), basic.end());
            auto &values = directions[i];
            std::vector<double> unique;
            for (const double value : values)
                if (std::none_of(unique.begin(), unique.end(), [&](double old) {
                        return std::abs(Detail::NormalizeAngle(value - old)) <= problem.Tolerance;
                    }))
                    unique.push_back(value);
            values = std::move(unique);
            if (!retain(values.capacity() * sizeof(double)))
            {
                values.clear();
                break;
            }
        }

        std::vector<Block> blocks;
        const auto addBlock = [&](Pattern pattern, double saving, double fill, bool feature, bool array,
                                  std::uint64_t proposal = 0) {
            if (pattern.Members.size() < 2 || blocks.size() >= Settings.MaxArrayVariants)
                return false;
            std::size_t guardBytes = 0;
            for (const auto &member : pattern.Members)
            {
                if (member.PartIndex >= problem.Parts.size())
                    return false;
                guardBytes += GeometryWorkBytes(problem.Parts[member.PartIndex].Geometry);
            }
            if (!temporary(guardBytes))
                return false;
            Detail::NormalizePattern(pattern, problem);
            for (const auto &old : blocks)
                if (old.Value.Members.size() == pattern.Members.size() &&
                    std::equal(pattern.Members.begin(), pattern.Members.end(), old.Value.Members.begin(),
                               [](const Detail::Member &a, const Detail::Member &b) {
                                   return a.PartIndex == b.PartIndex && a.Transform.X == b.Transform.X &&
                                          a.Transform.Y == b.Transform.Y &&
                                          a.Transform.AngleDegrees == b.Transform.AngleDegrees;
                               }))
                    return false;
            std::map<std::size_t, std::size_t> demand;
            std::vector<AxisSafety::Geometry> geometry;
            for (const auto &member : pattern.Members)
            {
                if (!trial() || member.PartIndex >= problem.Parts.size())
                    return false;
                const auto &part = problem.Parts[member.PartIndex];
                if (++demand[member.PartIndex] > part.Quantity ||
                    !Detail::AngleAllowed(part, member.Transform.AngleDegrees, problem.Tolerance))
                    return false;
                const auto original = AxisSafety::Make(part.Geometry, member.Transform);
                for (std::size_t j = 0; j < geometry.size(); ++j)
                {
                    if (!trial())
                        return false;
                    const auto &other = pattern.Members[j];
                    if (Detail::ShapesConflict(part.Geometry, member.Transform, problem.Parts[other.PartIndex].Geometry,
                                               other.Transform, problem.PartGap, problem.Tolerance,
                                               problem.AllowCommonEdges) ||
                        AxisSafety::Pair(original, geometry[j], problem.PartGap, problem.AllowCommonEdges,
                                         problem.Tolerance) != AxisSafety::Verdict::Safe)
                        return false;
                }
                geometry.push_back(original);
            }
            const auto bytes = PatternBytes(pattern) + demand.size() * sizeof(std::pair<std::size_t, std::size_t>);
            if (!retain(bytes))
                return false;
            Block block;
            block.Value = std::move(pattern);
            block.Saving = saving;
            block.Fill = fill;
            block.Feature = feature;
            block.Array = array;
            block.TraceProposal = proposal ? proposal : trace.NewProposal();
            block.Demand.assign(demand.begin(), demand.end());
            blocks.push_back(std::move(block));
            if (array)
                trace.Emit(ConstructionTraceStage::Array, [&](ConstructionTraceEvent &event) {
                    const auto &kept = blocks.back();
                    event.StateID = 1;
                    event.ProposalID = kept.TraceProposal;
                    event.ArrayDerived = true;
                    event.Saving = saving;
                    event.Fill = fill;
                    event.FootprintArea = fill > 0 ? kept.Value.MaterialArea / fill : 0;
                    TracePattern(event, kept.Value);
                });
            return true;
        };

        // Offline feature saving is measured in the pair's free orientation.
        // Board-position scoring below uses the stock-axis occupied envelope.
        std::vector<std::size_t> matchOrder(matches.Matches.size());
        std::iota(matchOrder.begin(), matchOrder.end(), 0);
        std::stable_sort(matchOrder.begin(), matchOrder.end(), [&](auto a, auto b) {
            return matches.Matches[a].FootprintSaving > matches.Matches[b].FootprintSaving;
        });
        const auto matchSlots = Settings.MaxArrayVariants / 3;
        const auto fitsAnyStock = [&](const Pattern &pattern) {
            return std::any_of(problem.Stocks.begin(), problem.Stocks.end(), [&](const Stock &stock) {
                return stock.Quantity && pattern.Width <= stock.Width - 2 * problem.EdgeMargin &&
                       pattern.Height <= stock.Height - 2 * problem.EdgeMargin;
            });
        };
        using PatternKey = std::vector<std::tuple<std::size_t, double, double, double>>;
        const auto patternKey = [](const Pattern &pattern) {
            PatternKey key;
            for (const auto &member : pattern.Members)
                key.emplace_back(member.PartIndex, member.Transform.X, member.Transform.Y,
                                 member.Transform.AngleDegrees);
            return key;
        };
        std::set<PatternKey> matchedPairs;
        for (const auto index : matchOrder)
        {
            if (preparationStop() || blocks.size() >= (matchSlots + 1) / 2)
                break;
            const auto &match = matches.Matches[index];
            if (match.Value.Members.size() != 2 || !fitsAnyStock(match.Value))
                continue;
            if (matchedPairs.insert(patternKey(match.Value)).second)
                addBlock(match.Value, match.FootprintSaving, match.CarrierFill, true, false,
                         matchProposals.empty() ? 0 : matchProposals[index]);
        }
        // Preserve high-fill candidates across different specifications as
        // well as the largest absolute savings. Neither metric replaces
        // original-material feasibility or the shared final quality.
        std::stable_sort(matchOrder.begin(), matchOrder.end(), [&](auto a, auto b) {
            return std::tie(matches.Matches[a].CarrierFill, matches.Matches[a].FootprintSaving) >
                   std::tie(matches.Matches[b].CarrierFill, matches.Matches[b].FootprintSaving);
        });
        std::set<PatternKey> fillPairs;
        for (const auto index : matchOrder)
        {
            if (preparationStop() || blocks.size() >= matchSlots)
                break;
            const auto &match = matches.Matches[index];
            if (match.Value.Members.size() != 2 || !fitsAnyStock(match.Value))
                continue;
            if (fillPairs.insert(patternKey(match.Value)).second)
                addBlock(match.Value, match.FootprintSaving, match.CarrierFill, true, false,
                         matchProposals.empty() ? 0 : matchProposals[index]);
        }

        const double arraysStarted = seconds();
        double maxWidth = 0, maxHeight = 0;
        for (const auto &stock : problem.Stocks)
        {
            maxWidth = std::max(maxWidth, stock.Width - 2 * problem.EdgeMargin);
            maxHeight = std::max(maxHeight, stock.Height - 2 * problem.EdgeMargin);
        }
        const auto activeSpecifications = std::count_if(problem.Parts.begin(), problem.Parts.end(),
                                                        [](const Part &part) { return part.Quantity > 1; });
        const std::size_t perSpecification = std::max<std::size_t>(
            1, (Settings.MaxArrayVariants - blocks.size()) / std::max<std::size_t>(1, activeSpecifications));
        for (std::size_t index = 0; index < problem.Parts.size() && !preparationStop(); ++index)
        {
            const auto &part = problem.Parts[index];
            if (part.Quantity < 2 || directions[index].empty())
                continue;
            const auto count = std::min<std::size_t>(part.Quantity, 64);
            Pattern singleFootprint;
            singleFootprint.Members.push_back({index, {0, 0, directions[index].front()}});
            const auto singleObb = Detail::MinimumPatternObbArea(singleFootprint, problem, preparationStop);
            if (!singleObb)
                continue;
            std::vector<Pattern> frontier;
            const auto consider = [&](Pattern pattern) {
                Detail::NormalizePattern(pattern, problem);
                if (pattern.Width > maxWidth || pattern.Height > maxHeight)
                    return;
                for (const auto &old : frontier)
                    if (old.Width <= pattern.Width && old.Height <= pattern.Height)
                        return;
                frontier.erase(std::remove_if(frontier.begin(), frontier.end(),
                                              [&](const Pattern &old) {
                                                  return pattern.Width <= old.Width && pattern.Height <= old.Height;
                                              }),
                               frontier.end());
                if (frontier.size() >= 32)
                    return;
                if (!temporary((frontier.size() + 1) * (sizeof(Pattern) + count * sizeof(Detail::Member))))
                    return;
                frontier.push_back(std::move(pattern));
            };
            for (std::size_t angleIndex = 0;
                 angleIndex < std::min<std::size_t>(4, directions[index].size()) && !preparationStop(); ++angleIndex)
            {
                constexpr std::size_t latticeInputBytes = 4096 * 64 + 96 * 96 * sizeof(Lattice);
                if (!temporary(latticeInputBytes + 32 * (sizeof(Pattern) + count * sizeof(Detail::Member))))
                    break;
                const double angle = directions[index][angleIndex];
                const auto box = Detail::Bounds(part.Geometry, {0, 0, angle});
                PathsD self;
                if (prepared[index])
                {
                    const auto lookup = Cache->Get(prepared[index], angle, prepared[index], angle, buildOptions());
                    if (lookup.CacheHit)
                        ++nfpHits;
                    if (lookup.Region)
                        for (const auto &ring : lookup.Region->Loops)
                        {
                            std::size_t selfBytes = 0;
                            for (const auto &path : self)
                                selfBytes += sizeof(PathD) + path.capacity() * sizeof(Clipper2Lib::PointD);
                            if (!temporary(latticeInputBytes + selfBytes + sizeof(PathD) +
                                           ring.size() * sizeof(Clipper2Lib::PointD)))
                                break;
                            PathD path;
                            for (const auto vertex : ring)
                            {
                                const auto point = Detail::Transform(vertex, lookup.Transform);
                                path.emplace_back(point.X, point.Y);
                            }
                            self.push_back(std::move(path));
                        }
                }
                std::set<std::pair<double, double>> vectorSet;
                const auto vector = [&](double x, double y) {
                    if ((x == 0 && y == 0) || vectorSet.size() >= 96 || !std::isfinite(x) || !std::isfinite(y))
                        return;
                    if (!InForbidden({x, y}, self))
                        vectorSet.emplace(x, y);
                };
                vector(box.Width + problem.PartGap, 0);
                vector(0, box.Height + problem.PartGap);
                if (problem.PartGap == 0)
                    for (const auto &path : self)
                        for (std::size_t e = 0; e < path.size(); ++e)
                        {
                            const auto a = path[e], b = path[(e + 1) % path.size()];
                            vector(a.x, a.y);
                            vector((a.x + b.x) / 2, (a.y + b.y) / 2);
                            if (a.x != b.x && a.x * b.x <= 0)
                                vector(0, a.y - a.x * (b.y - a.y) / (b.x - a.x));
                            if (a.y != b.y && a.y * b.y <= 0)
                                vector(a.x - a.y * (b.x - a.x) / (b.y - a.y), 0);
                        }
                for (const auto &match : matches.Matches)
                {
                    if (match.Value.Members.size() != 2)
                        continue;
                    const auto &first = match.Value.Members[0];
                    const auto &second = match.Value.Members[1];
                    if (first.PartIndex == index && second.PartIndex == index &&
                        first.Transform.AngleDegrees == angle && second.Transform.AngleDegrees == angle)
                        vector(second.Transform.X - first.Transform.X, second.Transform.Y - first.Transform.Y);
                }
                std::vector<Point> vectors;
                for (auto [x, y] : vectorSet)
                    vectors.push_back({x, y});
                std::vector<Lattice> proposals;
                for (std::size_t i = 0; i < vectors.size(); ++i)
                    for (std::size_t j = i + 1; j < vectors.size(); ++j)
                    {
                        if (!trial() || preparationStop())
                            break;
                        const auto u = vectors[i], v = vectors[j];
                        const double determinant = std::abs(u.X * v.Y - u.Y * v.X);
                        if (determinant + problem.Tolerance < Detail::Area(part.Geometry) || determinant == 0)
                            continue;
                        proposals.push_back({u, v, determinant, u.X * u.X + u.Y * u.Y + v.X * v.X + v.Y * v.Y});
                    }
                std::stable_sort(proposals.begin(), proposals.end(), [](const Lattice &a, const Lattice &b) {
                    return std::tie(a.Determinant, a.Length) < std::tie(b.Determinant, b.Length);
                });
                std::size_t bases = 0;
                for (const auto &lattice : proposals)
                {
                    if (preparationStop() || bases >= 12)
                        break;
                    const auto u = lattice.U, v = lattice.V;
                    const double determinant = lattice.Determinant;
                    const double aBound =
                        std::ceil((std::abs(v.Y) * box.Width + std::abs(v.X) * box.Height) / determinant) + 1;
                    const double bBound =
                        std::ceil((std::abs(u.Y) * box.Width + std::abs(u.X) * box.Height) / determinant) + 1;
                    if (aBound > 64 || bBound > 64)
                        continue;
                    bool valid = true;
                    for (int a = -static_cast<int>(aBound); a <= static_cast<int>(aBound) && valid; ++a)
                        for (int b = -static_cast<int>(bBound); b <= static_cast<int>(bBound) && valid; ++b)
                        {
                            if (!a && !b)
                                continue;
                            if (!trial() || preparationStop())
                            {
                                valid = false;
                                break;
                            }
                            valid = !InForbidden({a * u.X + b * v.X, a * u.Y + b * v.Y}, self);
                        }
                    if (!valid)
                        continue;
                    ++bases;
                    const double aa =
                        std::ceil((std::abs(v.Y) * maxWidth + std::abs(v.X) * 2 * maxHeight) / determinant) + 2;
                    const double bb =
                        std::ceil((std::abs(u.Y) * maxWidth + std::abs(u.X) * 2 * maxHeight) / determinant) + 2;
                    if (aa > 128 || bb > 128)
                        continue;
                    std::set<std::pair<double, double>> points;
                    for (int a = -static_cast<int>(aa);
                         a <= static_cast<int>(aa) && points.size() < 4096 && !preparationStop(); ++a)
                        for (int b = -static_cast<int>(bb); b <= static_cast<int>(bb); ++b)
                        {
                            if (preparationStop())
                                break;
                            const Point p{a * u.X + b * v.X, a * u.Y + b * v.Y};
                            if (p.X >= 0 && p.X <= maxWidth - box.Width && p.Y >= -maxHeight && p.Y <= maxHeight)
                                points.emplace(p.X, p.Y);
                            if (points.size() >= 4096)
                                break;
                        }
                    std::set<double> phases, heights;
                    for (auto [x, y] : points)
                    {
                        if (y >= -maxHeight + box.Height && y <= 0)
                            phases.insert(y);
                        if (y >= 0 && y <= maxHeight - box.Height)
                            heights.insert(y);
                    }
                    for (const double phase : phases)
                        for (const double height : heights)
                        {
                            if (!trial() || preparationStop())
                                break;
                            Pattern pattern;
                            for (auto [x, y] : points)
                            {
                                if (y < phase || y > phase + height)
                                    continue;
                                pattern.Members.push_back({index, {x - box.X, y - box.Y, angle}});
                                if (pattern.Members.size() == count)
                                    break;
                            }
                            if (pattern.Members.size() == count)
                                consider(std::move(pattern));
                        }
                }
            }
            std::stable_sort(frontier.begin(), frontier.end(), [](const Pattern &a, const Pattern &b) {
                return std::tuple(a.Width * a.Height, a.Width, a.Height) <
                       std::tuple(b.Width * b.Height, b.Width, b.Height);
            });
            // A bounded sample of the whole long/short frontier retains useful
            // rows and columns even when their standalone box is less dense.
            if (frontier.size() > perSpecification)
            {
                std::vector<Pattern> diverse;
                diverse.push_back(std::move(frontier.front()));
                std::vector<std::size_t> order;
                for (std::size_t i = 1; i < frontier.size(); ++i)
                    order.push_back(i);
                std::stable_sort(order.begin(), order.end(),
                                 [&](auto a, auto b) { return frontier[a].Width < frontier[b].Width; });
                for (std::size_t i = 0; i + 1 < perSpecification; ++i)
                {
                    const auto position = perSpecification <= 2 ? 0 : i * (order.size() - 1) / (perSpecification - 2);
                    diverse.push_back(std::move(frontier[order[position]]));
                }
                frontier = std::move(diverse);
            }
            for (auto &pattern : frontier)
            {
                if (preparationStop())
                    break;
                const auto arrayObb = Detail::MinimumPatternObbArea(pattern, problem, preparationStop);
                if (!arrayObb)
                    continue;
                const double saving = count * *singleObb - *arrayObb;
                const double fill = pattern.MaterialArea / *arrayObb;
                addBlock(std::move(pattern), saving, fill, false, true);
            }
        }
        const double arraySeconds = seconds() - arraysStarted;
        const double preparedSeconds = seconds();

        std::stable_sort(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) {
            return std::tuple(-a.Saving, -a.Fill, -a.Value.MaterialArea) <
                   std::tuple(-b.Saving, -b.Fill, -b.Value.MaterialArea);
        });
        SearchState initial;
        initial.TraceState = trace.Enabled() ? 1 : 0;
        initial.Used.resize(problem.Parts.size());
        initial.StockUsed.resize(problem.Stocks.size());
        std::optional<Detail::QualityKey> bestQuality;
        std::optional<Detail::QualityKey> bestFeatureQuality;
        Result best = empty;
        std::uint64_t selectedStateID = initial.TraceState, selectedParentID = 0;
        std::size_t bestCount = 0, selectedFeature = 0, selectedArrays = 0;
        std::size_t featureQueries = 0, featureLegalStates = 0, featureComplete = 0;
        double bestFeatureEnvelope = -1, bestFeatureObb = -1, bestFeatureRegular = -1;
        double selectedEnvelope = -1, selectedObb = -1, selectedRegular = -1;
        double bestArea = -1, evaluationSeconds = 0, verificationSeconds = 0;
        const auto occupiedObb = [&](const Result &result) {
            double total = 0;
            for (const auto &board : result.Layouts)
            {
                Pattern footprint;
                for (const auto &placement : board.Placements)
                {
                    const auto source = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                                     [&](const Part &part) { return part.ID == placement.PartID; });
                    footprint.Members.push_back(
                        {static_cast<std::size_t>(source - problem.Parts.begin()), placement.Transform});
                }
                const auto area = Detail::MinimumPatternObbArea(footprint, problem, stop);
                if (!area)
                    return -1.;
                total += *area;
            }
            return total;
        };
        const auto asResult = [&](const SearchState &state) {
            Result result;
            result.State = Status::Complete;
            result.Layouts = state.Boards;
            for (std::size_t i = 0; i < problem.Parts.size(); ++i)
                if (state.Used[i] < problem.Parts[i].Quantity)
                {
                    result.State = Status::Partial;
                    result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity - state.Used[i]});
                }
            result.Stats.CombinationGroups = state.FeatureGroups + state.Arrays;
            return result;
        };
        const auto evaluate = [&](const SearchState &state, bool isComplete) {
            const auto count = std::accumulate(state.Used.begin(), state.Used.end(), std::size_t(0));
            if (!isComplete &&
                (bestQuality || state.PlacedArea < bestArea || (state.PlacedArea == bestArea && count <= bestCount)))
                return;
            auto candidate = asResult(state);
            const double beforeValidation = seconds();
            const auto checked = Validate(problem, candidate);
            verificationSeconds += seconds() - beforeValidation;
            if (!checked.Valid)
                return;
            candidate.GeometryValidated = true;
            if (isComplete)
            {
                ++complete;
                const double beforeEvaluation = seconds();
                JobResult job;
                job.Output = candidate;
                job.Checked = checked;
                job.Remnants = EvaluateRemnants(problem, candidate);
                const auto key = Detail::Quality(job, problem, options.Ranking);
                trace.Emit(ConstructionTraceStage::Complete, [&](ConstructionTraceEvent &event) {
                    event.StateID = state.TraceState;
                    event.ParentStateID = state.TraceParent;
                    event.MaterialArea = state.PlacedArea;
                    event.EnvelopeArea = checked.UsedEnvelopeArea;
                    event.RegularRemnantArea = job.Remnants.LastSheetRegularArea;
                    event.FeatureDerived = state.FeatureGroups > 0;
                    event.ArrayDerived = state.Arrays > 0;
                    event.ResultStatus = Status::Complete;
                    event.Reason = "original-result-validated";
                });
                if (state.FeatureGroups)
                {
                    ++featureComplete;
                    if (!bestFeatureQuality || key < *bestFeatureQuality)
                    {
                        bestFeatureQuality = key;
                        bestFeatureEnvelope = checked.UsedEnvelopeArea;
                        bestFeatureRegular = job.Remnants.LastSheetRegularArea;
                        bestFeatureObb = occupiedObb(candidate);
                    }
                }
                evaluationSeconds += seconds() - beforeEvaluation;
                if (bestQuality && !(key < *bestQuality))
                    return;
                bestQuality = key;
                selectedRegular = job.Remnants.LastSheetRegularArea;
                const double beforeObb = seconds();
                selectedObb = occupiedObb(candidate);
                evaluationSeconds += seconds() - beforeObb;
            }
            best = std::move(candidate);
            selectedStateID = state.TraceState;
            selectedParentID = state.TraceParent;
            bestCount = count;
            bestArea = state.PlacedArea;
            selectedFeature = state.FeatureGroups;
            selectedArrays = state.Arrays;
            selectedEnvelope = checked.UsedEnvelopeArea;
        };

        const auto fits = [&](const SearchState &state, const Block &block, std::size_t boardIndex, const Layout &board,
                              std::vector<Child> &children) {
            if (block.Feature)
                ++featureQueries;
            const double low = problem.EdgeMargin;
            const double right = board.Width - low - block.Value.Width;
            const double top = board.Height - low - block.Value.Height;
            if (right < low || top < low)
                return;
            PathsD forbidden;
            std::size_t pathBytes = 0;
            std::size_t guardBytes = 0;
            for (const auto &member : block.Value.Members)
                guardBytes += GeometryWorkBytes(problem.Parts[member.PartIndex].Geometry);
            for (const auto &fixed : board.Placements)
            {
                const auto part = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                               [&](const Part &item) { return item.ID == fixed.PartID; });
                guardBytes += GeometryWorkBytes(part->Geometry);
            }
            if (!temporary(guardBytes))
                return;
            bool domainComplete = true;
            for (const auto &fixed : board.Placements)
            {
                if (stop())
                    return;
                const auto source = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                                 [&](const Part &part) { return part.ID == fixed.PartID; });
                const auto fixedIndex = static_cast<std::size_t>(source - problem.Parts.begin());
                for (const auto &moving : block.Value.Members)
                {
                    if (!trial())
                        return;
                    if (!prepared[fixedIndex] || !prepared[moving.PartIndex])
                    {
                        domainComplete = false;
                        continue;
                    }
                    const auto lookup =
                        Cache->Get(prepared[fixedIndex], fixed.Transform.AngleDegrees, prepared[moving.PartIndex],
                                   moving.Transform.AngleDegrees, buildOptions());
                    if (lookup.CacheHit)
                        ++nfpHits;
                    if (!lookup.Region)
                    {
                        domainComplete = false;
                        continue;
                    }
                    for (const auto &ring : lookup.Region->Loops)
                    {
                        const auto bytes = sizeof(PathD) + ring.size() * sizeof(Clipper2Lib::PointD);
                        if (!temporary(guardBytes + pathBytes + bytes))
                        {
                            domainComplete = false;
                            break;
                        }
                        PathD path;
                        path.reserve(ring.size());
                        for (const auto vertex : ring)
                        {
                            const auto p = Detail::Transform(vertex, lookup.Transform);
                            path.emplace_back(p.X + fixed.Transform.X - moving.Transform.X,
                                              p.Y + fixed.Transform.Y - moving.Transform.Y);
                        }
                        pathBytes += bytes;
                        forbidden.push_back(std::move(path));
                    }
                }
            }
            if (!forbidden.empty() && temporary(guardBytes + pathBytes * 3))
            {
                forbidden = Clipper2Lib::Union(forbidden, Clipper2Lib::FillRule::NonZero, 8);
                ++unions;
                pathBytes = forbidden.capacity() * sizeof(PathD);
                for (const auto &path : forbidden)
                    pathBytes += path.capacity() * sizeof(Clipper2Lib::PointD);
                if (!temporary(guardBytes + pathBytes))
                {
                    PathsD{}.swap(forbidden);
                    pathBytes = 0;
                    domainComplete = false;
                }
            }
            std::set<std::pair<double, double>> anchors;
            double coordinateScale = std::max({1., board.Width, board.Height});
            for (const auto &member : block.Value.Members)
            {
                coordinateScale =
                    std::max({coordinateScale, std::abs(member.Transform.X), std::abs(member.Transform.Y)});
                for (const auto point : problem.Parts[member.PartIndex].Geometry.Outer)
                    coordinateScale = std::max({coordinateScale, std::abs(point.X), std::abs(point.Y)});
            }
            const double inward = coordinateScale * std::numeric_limits<double>::epsilon() * 32;
            const auto add = [&](double x, double y) {
                if (anchors.size() >= 2048 || !std::isfinite(x) || !std::isfinite(y) || x < low || x > right ||
                    y < low || y > top)
                    return;
                if (!temporary(guardBytes + pathBytes + (anchors.size() + 2) * 128))
                    return;
                anchors.emplace(x, y);
                // Source extrema rounded onto a board boundary can require a
                // representable inward translation. Both events face the same
                // original-material and exact world-coordinate guards.
                if (right - low > 2 * inward && top - low > 2 * inward && anchors.size() < 2048)
                    anchors.emplace(std::clamp(x, low + inward, right - inward),
                                    std::clamp(y, low + inward, top - inward));
            };
            add(low, low);
            add(right, low);
            add(low, top);
            add(right, top);
            for (const auto &path : forbidden)
                for (std::size_t edge = 0; edge < path.size(); ++edge)
                {
                    if (stop())
                        return;
                    const auto a = path[edge], b = path[(edge + 1) % path.size()];
                    add(a.x, a.y);
                    if (problem.PartGap > 0)
                    {
                        add(a.x + problem.PartGap, a.y);
                        add(a.x, a.y + problem.PartGap);
                    }
                    for (const double x : {low, right})
                        if (a.x != b.x)
                        {
                            const double t = (x - a.x) / (b.x - a.x);
                            if (t >= 0 && t <= 1)
                                add(x, a.y + t * (b.y - a.y));
                        }
                    for (const double y : {low, top})
                        if (a.y != b.y)
                        {
                            const double t = (y - a.y) / (b.y - a.y);
                            if (t >= 0 && t <= 1)
                                add(a.x + t * (b.x - a.x), y);
                        }
                }
            if (!domainComplete || problem.PartGap > 0)
                for (const auto &placed : board.Placements)
                {
                    const auto source = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                                     [&](const Part &part) { return part.ID == placed.PartID; });
                    const auto bounds = Detail::Bounds(source->Geometry, placed.Transform);
                    add(bounds.X + bounds.Width + problem.PartGap, low);
                    add(low, bounds.Y + bounds.Height + problem.PartGap);
                    add(bounds.X + bounds.Width + problem.PartGap, bounds.Y);
                    add(bounds.X, bounds.Y + bounds.Height + problem.PartGap);
                }
            const auto occupied = BoardBounds(problem, board);
            struct Anchor
            {
                Point Position;
                double Increment, Right, Top;
            };
            std::vector<Anchor> ordered;
            for (auto [x, y] : anchors)
            {
                if (!trial())
                    return;
                if (InForbidden({x, y}, forbidden))
                    continue;
                const double minX = board.Placements.empty() ? x : std::min(occupied.X, x);
                const double minY = board.Placements.empty() ? y : std::min(occupied.Y, y);
                const double maxX = std::max(occupied.X + occupied.Width, x + block.Value.Width);
                const double maxY = std::max(occupied.Y + occupied.Height, y + block.Value.Height);
                ordered.push_back(
                    {{x, y}, (maxX - minX) * (maxY - minY) - occupied.Width * occupied.Height, maxX, maxY});
            }
            std::stable_sort(ordered.begin(), ordered.end(), [](const Anchor &a, const Anchor &b) {
                return std::tuple(a.Increment, a.Right, a.Top, a.Position.Y, a.Position.X) <
                       std::tuple(b.Increment, b.Right, b.Top, b.Position.Y, b.Position.X);
            });
            std::vector<AxisSafety::Geometry> existing;
            for (const auto &placed : board.Placements)
            {
                const auto source = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                                 [&](const Part &part) { return part.ID == placed.PartID; });
                existing.push_back(AxisSafety::Make(source->Geometry, placed.Transform));
            }
            std::size_t accepted = 0;
            std::vector<Point> acceptedAnchors;
            for (const auto &anchor : ordered)
            {
                if (stop() || complete >= 16)
                    return;
                if (std::any_of(acceptedAnchors.begin(), acceptedAnchors.end(), [&](Point old) {
                        return std::abs(old.X - anchor.Position.X) <= inward * 4 &&
                               std::abs(old.Y - anchor.Position.Y) <= inward * 4;
                    }))
                    continue;
                const auto candidateID = trace.NewCandidate();
                std::vector<Placement> placements;
                std::vector<AxisSafety::Geometry> geometry;
                bool valid = true;
                const char *rejection = "original-geometry-guard";
                std::vector<std::size_t> used = state.Used;
                const auto candidateEvent = [&](bool acceptedCandidate, std::uint64_t stateID, const char *reason) {
                    trace.Emit(ConstructionTraceStage::Candidate, [&](ConstructionTraceEvent &event) {
                        event.CandidateID = candidateID;
                        event.StateID = stateID;
                        event.ParentStateID = state.TraceState;
                        event.ProposalID = block.TraceProposal;
                        event.Accepted = acceptedCandidate;
                        event.Reason = reason;
                        event.BoardIndex = boardIndex;
                        event.StockInstance = board.StockInstance;
                        const auto stock = std::find_if(problem.Stocks.begin(), problem.Stocks.end(),
                                                        [&](const Stock &item) { return item.ID == board.StockID; });
                        event.StockIndex = static_cast<std::size_t>(stock - problem.Stocks.begin());
                        event.BoardWidth = board.Width;
                        event.BoardHeight = board.Height;
                        event.Width = block.Value.Width;
                        event.Height = block.Value.Height;
                        event.MaterialArea = block.Value.MaterialArea;
                        event.Saving = block.Saving;
                        event.Fill = block.Fill;
                        event.FootprintArea = block.Fill > 0 ? block.Value.MaterialArea / block.Fill : 0;
                        event.Increment = anchor.Increment;
                        event.FeatureDerived = block.Feature;
                        event.ArrayDerived = block.Array;
                        if (acceptedCandidate)
                        {
                            for (std::size_t i = 0; i < placements.size(); ++i)
                                event.Members.push_back({block.Value.Members[i].PartIndex, placements[i].Instance,
                                                         placements[i].Transform, placements[i].Group});
                        }
                        else
                        {
                            auto proposed = state.Used;
                            for (const auto &member : block.Value.Members)
                            {
                                auto pose = member.Transform;
                                pose.X += anchor.Position.X;
                                pose.Y += anchor.Position.Y;
                                event.Members.push_back(
                                    {member.PartIndex, proposed[member.PartIndex]++, pose,
                                     block.Value.Members.size() > 1 ? board.Placements.size() + 1 : 0});
                            }
                        }
                    });
                };
                for (const auto &member : block.Value.Members)
                {
                    if (!trial())
                    {
                        candidateEvent(false, 0, "work-limit-before-original-member-check");
                        return;
                    }
                    const auto &part = problem.Parts[member.PartIndex];
                    auto pose = member.Transform;
                    pose.X += anchor.Position.X;
                    pose.Y += anchor.Position.Y;
                    auto original = AxisSafety::Make(part.Geometry, pose);
                    if (!Detail::InsideSheet(part.Geometry, pose, board.Width, board.Height, low, problem.Tolerance) ||
                        !AxisSafety::Inside(original, board.Width, board.Height, low))
                    {
                        rejection = "original-inside-sheet-guard";
                        valid = false;
                        break;
                    }
                    for (const auto &other : existing)
                    {
                        if (!trial())
                        {
                            candidateEvent(false, 0, "work-limit-before-existing-pair-check");
                            return;
                        }
                        if (AxisSafety::Pair(original, other, problem.PartGap, problem.AllowCommonEdges,
                                             problem.Tolerance) != AxisSafety::Verdict::Safe)
                        {
                            rejection = "nominal-existing-pair-guard";
                            valid = false;
                            break;
                        }
                    }
                    if (!valid)
                        break;
                    for (const auto &other : geometry)
                    {
                        if (!trial())
                        {
                            candidateEvent(false, 0, "work-limit-before-group-pair-check");
                            return;
                        }
                        if (AxisSafety::Pair(original, other, problem.PartGap, problem.AllowCommonEdges,
                                             problem.Tolerance) != AxisSafety::Verdict::Safe)
                        {
                            rejection = "nominal-group-pair-guard";
                            valid = false;
                            break;
                        }
                    }
                    if (!valid)
                        break;
                    placements.push_back({part.ID, used[member.PartIndex]++, pose,
                                          block.Value.Members.size() > 1 ? board.Placements.size() + 1 : 0});
                    geometry.push_back(std::move(original));
                }
                if (!valid)
                {
                    candidateEvent(false, 0, rejection);
                    continue;
                }
                std::size_t stringBytes = 0;
                for (const auto &placement : placements)
                    stringBytes += placement.PartID.capacity();
                const auto childBytes = 2 * (StateBytes(state) + placements.size() * sizeof(Placement) + stringBytes +
                                             sizeof(Layout) + sizeof(Child));
                if (children.size() >= Settings.MaxSearchStates ||
                    !temporary(guardBytes + pathBytes + anchors.size() * 128 + childBytes) || !retain(childBytes))
                {
                    candidateEvent(false, 0,
                                   children.size() >= Settings.MaxSearchStates ? "search-state-limit" : "memory-limit");
                    return;
                }
                auto next = state;
                next.TraceParent = state.TraceState;
                next.TraceState = trace.NewState();
                if (boardIndex == state.Boards.size())
                {
                    next.Boards.push_back(board);
                    const auto stock = std::find_if(problem.Stocks.begin(), problem.Stocks.end(),
                                                    [&](const Stock &stock) { return stock.ID == board.StockID; });
                    ++next.StockUsed[static_cast<std::size_t>(stock - problem.Stocks.begin())];
                }
                auto &target = next.Boards[boardIndex];
                target.Placements.insert(target.Placements.end(), placements.begin(), placements.end());
                target.MaterialArea += block.Value.MaterialArea;
                next.Used = std::move(used);
                next.PlacedArea += block.Value.MaterialArea;
                next.FeatureGroups += block.Feature;
                next.Arrays += block.Array;
                if (block.Feature)
                    ++featureLegalStates;
                candidateEvent(true, next.TraceState, "retained-original-material-safe-candidate");
                acceptedAnchors.push_back(anchor.Position);
                const bool terminal =
                    std::equal(next.Used.begin(), next.Used.end(), problem.Parts.begin(),
                               [](std::size_t used, const Part &part) { return used == part.Quantity; });
                // Preserve a verified result immediately. Later candidate
                // generation may consume the remaining search allowance.
                evaluate(next, terminal);
                if (terminal)
                {
                    retained -= childBytes;
                    ++generated;
                    if (++accepted >= 3)
                        return;
                    continue;
                }
                children.push_back({std::move(next), anchor.Increment, block.Saving, block.Fill, anchor.Right,
                                    anchor.Top, childBytes});
                ++generated;
                if (++accepted >= 3)
                    return;
            }
        };

        const auto search = [&](auto &&self, const SearchState &state, std::size_t depth) -> void {
            const bool all = std::equal(state.Used.begin(), state.Used.end(), problem.Parts.begin(),
                                        [](std::size_t used, const Part &part) { return used == part.Quantity; });
            if (all)
            {
                evaluate(state, true);
                return;
            }
            if (stop() || expanded >= Settings.MaxSearchStates || complete >= 16 || depth >= Settings.MaxSearchStates)
                return;
            evaluate(state, all);
            ++expanded;
            trace.Emit(ConstructionTraceStage::Expand, [&](ConstructionTraceEvent &event) {
                event.StateID = state.TraceState;
                event.ParentStateID = state.TraceParent;
                event.MaterialArea = state.PlacedArea;
                event.Reason = "selected-search-state-for-expansion";
            });
            std::vector<Child> children;
            const auto use = [&](const Block &block) {
                for (auto [part, quantity] : block.Demand)
                    if (quantity > problem.Parts[part].Quantity - state.Used[part])
                        return;
                bool currentFit = false;
                for (std::size_t board = 0; board < state.Boards.size() && !stop(); ++board)
                {
                    const auto stock =
                        std::find_if(problem.Stocks.begin(), problem.Stocks.end(),
                                     [&](const Stock &item) { return item.ID == state.Boards[board].StockID; });
                    if (std::any_of(block.Demand.begin(), block.Demand.end(), [&](const auto &demand) {
                            const auto &material = problem.Parts[demand.first].Material;
                            return !material.empty() && !stock->Material.empty() && material != stock->Material;
                        }))
                        continue;
                    const auto before = children.size();
                    fits(state, block, board, state.Boards[board], children);
                    currentFit = currentFit || children.size() > before;
                }
                // This constructor currently searches one stock sheet.
                // Other stock types are alternative roots, never extra sheets.
                if (currentFit || !state.Boards.empty())
                    return;
                for (std::size_t stockIndex = 0; stockIndex < problem.Stocks.size() && !stop(); ++stockIndex)
                {
                    const auto &stock = problem.Stocks[stockIndex];
                    if (state.StockUsed[stockIndex] >= stock.Quantity)
                        continue;
                    if (std::any_of(block.Demand.begin(), block.Demand.end(), [&](const auto &demand) {
                            const auto &material = problem.Parts[demand.first].Material;
                            return !material.empty() && !stock.Material.empty() && material != stock.Material;
                        }))
                        continue;
                    Layout board;
                    board.StockID = stock.ID;
                    board.StockInstance = state.StockUsed[stockIndex];
                    board.Width = stock.Width;
                    board.Height = stock.Height;
                    fits(state, block, state.Boards.size(), board, children);
                }
            };
            for (const auto &block : blocks)
            {
                if (stop())
                    break;
                use(block);
            }
            for (std::size_t part = 0; part < problem.Parts.size() && !stop(); ++part)
            {
                if (state.Used[part] >= problem.Parts[part].Quantity)
                    continue;
                for (const double angle : directions[part])
                {
                    if (stop())
                        break;
                    Block single;
                    single.Value.Members.push_back({part, {0, 0, angle}});
                    Detail::NormalizePattern(single.Value, problem);
                    single.Demand.push_back({part, 1});
                    use(single);
                }
            }
            std::stable_sort(children.begin(), children.end(), [](const Child &a, const Child &b) {
                return std::tuple(a.State.Boards.size(), -a.State.PlacedArea, a.Increment, -a.Saving, -a.Fill, a.Right,
                                  a.Top) < std::tuple(b.State.Boards.size(), -b.State.PlacedArea, b.Increment,
                                                      -b.Saving, -b.Fill, b.Right, b.Top);
            });
            std::map<std::vector<std::size_t>, std::size_t> perInventory;
            std::size_t childRetained = 0;
            for (const auto &child : children)
                childRetained += child.RetainedBytes;
            for (const auto &child : children)
            {
                if (stop() || complete >= 16)
                    break;
                const bool terminal =
                    std::equal(child.State.Used.begin(), child.State.Used.end(), problem.Parts.begin(),
                               [](std::size_t used, const Part &part) { return used == part.Quantity; });
                if (!terminal && expanded >= Settings.MaxSearchStates)
                {
                    trace.Emit(ConstructionTraceStage::Backtrack, [&](ConstructionTraceEvent &event) {
                        event.StateID = child.State.TraceState;
                        event.ParentStateID = child.State.TraceParent;
                        event.Reason = "retained-child-not-expanded-search-state-limit";
                    });
                    continue;
                }
                if (perInventory[child.State.Used]++ >= 4)
                {
                    trace.Emit(ConstructionTraceStage::Backtrack, [&](ConstructionTraceEvent &event) {
                        event.StateID = child.State.TraceState;
                        event.ParentStateID = child.State.TraceParent;
                        event.Reason = "retained-child-not-expanded-inventory-diversity-limit";
                    });
                    continue;
                }
                self(self, child.State, depth + 1);
            }
            retained -= childRetained;
            trace.Emit(ConstructionTraceStage::Backtrack, [&](ConstructionTraceEvent &event) {
                event.StateID = state.TraceState;
                event.ParentStateID = state.TraceParent;
                event.MaterialArea = state.PlacedArea;
                if (cancelled)
                    event.Reason = "cancelled";
                else if (attempts >= options.MaxPlacementAttempts)
                    event.Reason = "placement-attempt-limit";
                else if (seconds() >= options.TimeLimitSeconds)
                    event.Reason = "time-limit";
                else if (expanded >= Settings.MaxSearchStates)
                    event.Reason = "search-state-limit";
                else if (complete >= 16)
                    event.Reason = "complete-candidate-limit";
                else
                    event.Reason = "retained-children-exhausted";
            });
        };
        if (retain(StateBytes(initial)))
            search(search, initial, 0);
        if (cancelled)
            best.State = Status::Cancelled;
        trace.Emit(ConstructionTraceStage::Stopped, [&](ConstructionTraceEvent &event) {
            event.StateID = selectedStateID;
            event.ParentStateID = selectedParentID;
            event.ResultStatus = best.State;
            if (cancelled)
                event.Reason = "cancelled";
            else if (attempts >= options.MaxPlacementAttempts)
                event.Reason = "placement-attempt-limit";
            else if (seconds() >= options.TimeLimitSeconds)
                event.Reason = "time-limit";
            else if (expanded >= Settings.MaxSearchStates)
                event.Reason = "search-state-limit";
            else if (complete >= 16)
                event.Reason = "complete-candidate-limit";
            else if (memoryLimited)
                event.Reason = "search-finished-with-memory-rejections";
            else
                event.Reason = "bounded-search-finished";
        });
        trace.Emit(
            ConstructionTraceStage::Selected,
            [&](ConstructionTraceEvent &event) {
                event.StateID = selectedStateID;
                event.ParentStateID = selectedParentID;
                event.ResultStatus = best.State;
                event.Reason = "shared-quality-selected-original-result";
                event.MaterialArea = bestArea > 0 ? bestArea : 0;
                event.EnvelopeArea = selectedEnvelope;
                event.FootprintArea = selectedObb;
                event.RegularRemnantArea = selectedRegular;
                event.Fill = selectedObb > 0 ? event.MaterialArea / selectedObb : 0;
                event.FeatureDerived = selectedFeature > 0;
                event.ArrayDerived = selectedArrays > 0;
                for (std::size_t boardIndex = 0; boardIndex < best.Layouts.size(); ++boardIndex)
                {
                    const auto &board = best.Layouts[boardIndex];
                    const auto stock = std::find_if(problem.Stocks.begin(), problem.Stocks.end(),
                                                    [&](const Stock &item) { return item.ID == board.StockID; });
                    event.BoardIndex = boardIndex;
                    event.StockIndex = static_cast<std::size_t>(stock - problem.Stocks.begin());
                    event.StockInstance = board.StockInstance;
                    event.BoardWidth = board.Width;
                    event.BoardHeight = board.Height;
                    const auto bounds = BoardBounds(problem, board);
                    event.Width = bounds.Width;
                    event.Height = bounds.Height;
                    for (const auto &placement : board.Placements)
                    {
                        const auto source = std::find_if(problem.Parts.begin(), problem.Parts.end(),
                                                         [&](const Part &part) { return part.ID == placement.PartID; });
                        event.Members.push_back({static_cast<std::size_t>(source - problem.Parts.begin()),
                                                 placement.Instance, placement.Transform, placement.Group});
                    }
                }
            },
            true);
        best.Stats.CandidatePatterns = blocks.size();
        best.Stats.PlacementAttempts = attempts;
        best.Stats.ElapsedSeconds = seconds();
        best.Stats.PreparationSeconds = preparedSeconds;
        best.Stats.VerificationSeconds = verificationSeconds;
        best.Stats.EvaluationSeconds = evaluationSeconds;
        best.Stats.PlacementSeconds =
            std::max(0., seconds() - preparedSeconds - verificationSeconds - evaluationSeconds);
        std::ostringstream diagnostic;
        diagnostic << "feature-construction scope=single-sheet encoding-seconds=" << encodingSeconds
                   << " matching-seconds=" << matchingSeconds << " arrays-seconds=" << arraySeconds
                   << " feature-proposals=" << matches.Matches.size()
                   << " feature-match-groups-used=" << selectedFeature << " array-groups-used=" << selectedArrays
                   << " feature-fit-queries=" << featureQueries << " feature-legal-states=" << featureLegalStates
                   << " feature-complete-candidates=" << featureComplete
                   << " best-feature-envelope=" << bestFeatureEnvelope << " best-feature-obb=" << bestFeatureObb
                   << " best-feature-regular-remnant=" << bestFeatureRegular
                   << " selected-envelope=" << selectedEnvelope << " selected-obb=" << selectedObb
                   << " selected-regular-remnant=" << selectedRegular << " nfp-cache-hits=" << nfpHits
                   << " unions=" << unions << " generated-states=" << generated << " expanded-states=" << expanded
                   << " complete-candidates=" << complete << " retained-high-water=" << peak
                   << " temporary-input-high-water=" << temporaryPeak << " memory-limited=" << memoryLimited
                   << " feature-truncated=" << matches.Truncated << " clipper-internal-scratch=unmeasured";
        best.Diagnostics.push_back(diagnostic.str());
        if (Settings.Trace)
        {
            std::ostringstream traceDiagnostic;
            traceDiagnostic << "feature-construction trace-events=" << trace.Count
                            << " trace-truncated=" << trace.Truncated << " trace-observer-failed=" << trace.Failed;
            best.Diagnostics.push_back(traceDiagnostic.str());
        }
        return best;
    }

  private:
    FeatureConstructionSettings Settings;
    std::shared_ptr<NfpCache> Cache;
};
} // namespace

StrategyFactory MakeFeatureConstructionFactory(FeatureConstructionSettings settings, std::shared_ptr<NfpCache> cache)
{
    return [settings, cache = std::move(cache)] { return std::make_unique<FeatureConstructor>(settings, cache); };
}
} // namespace iCAX::SheetNesting
