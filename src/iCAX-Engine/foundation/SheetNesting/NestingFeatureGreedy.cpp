#include "SheetNestingFeatureGreedy.h"
#include "SheetNestingNfp.h"
#include "NestingShapeFeatures.h"
#include "NestingNfpPlacement.h"
#include "NestingConstructionObserver.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
using Clock = std::chrono::steady_clock;
using CandidateKey = std::array<double, 7>;

struct ContactPrior
{
    std::size_t Match = 0, FixedMember = 0, MovingMember = 1;
};

struct Proposal
{
    std::size_t Part = 0, Stock = 0, EvaluatedStep = 0;
    Pose Transform;
    Rect Bounds;
    CandidateKey Key{};
    double Increment = 0, Saving = 0, Fill = 0, PriorFootprint = 0, SeedAccess = 0;
    std::uint64_t Prior = 0, Candidate = 0, State = 0;
    bool Feature = false, DomainVerified = false, BoardAnchor = false;
};

struct VerifiedProposal
{
    Proposal Value;
    AxisSafety::Geometry Geometry;
};

bool SamePose(Pose first, Pose second)
{
    return first.X == second.X && first.Y == second.Y &&
           Detail::NormalizeAngle(first.AngleDegrees) == Detail::NormalizeAngle(second.AngleDegrees);
}

bool SameValidatedPose(Pose first, Pose second, double tolerance)
{
    return std::abs(first.X - second.X) <= tolerance && std::abs(first.Y - second.Y) <= tolerance &&
           std::abs(std::remainder(first.AngleDegrees - second.AngleDegrees, 360.)) <= tolerance;
}

Rect Enclose(Rect first, Rect second)
{
    const double x = std::min(first.X, second.X), y = std::min(first.Y, second.Y);
    return {x, y, std::max(first.X + first.Width, second.X + second.Width) - x,
            std::max(first.Y + first.Height, second.Y + second.Height) - y};
}

bool Compatible(const Part &part, const Stock &stock)
{
    return part.Material.empty() || stock.Material.empty() || part.Material == stock.Material;
}

std::vector<double> FallbackAngles(const Part &part, double tolerance, const std::function<bool()> &stop)
{
    auto angles = Detail::CandidateAngles(part, tolerance);
    if (stop())
        return angles;
    std::size_t vertices = part.Geometry.Outer.size();
    if (vertices > 256)
        return angles;
    for (const auto &hole : part.Geometry.Holes)
    {
        if (hole.size() > 256 - vertices)
            return angles;
        vertices += hole.size();
    }
    using Vertex = std::pair<std::vector<double>, std::vector<double>>;
    using Edge = std::pair<Vertex, Vertex>;
    using Ring = std::vector<Edge>;
    using Boundary = std::vector<Ring>;
    const auto signature = [&](double angle) {
        if (stop())
            return Boundary{};
        const auto geometry = AxisSafety::Make(part.Geometry, {0, 0, angle});
        if (!geometry.Valid || geometry.Rings.empty())
            return Boundary{};
        auto x = geometry.Rings.front().front().X, y = geometry.Rings.front().front().Y;
        for (const auto &point : geometry.Rings.front())
        {
            if ((point.X - x).Sign() < 0)
                x = point.X;
            if ((point.Y - y).Sign() < 0)
                y = point.Y;
        }
        Boundary result;
        for (const auto &ring : geometry.Rings)
        {
            Ring edges;
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto &a = ring[i];
                const auto &b = ring[(i + 1) % ring.size()];
                Vertex first{(a.X - x).Terms, (a.Y - y).Terms};
                Vertex second{(b.X - x).Terms, (b.Y - y).Terms};
                if (second < first)
                    std::swap(first, second);
                edges.emplace_back(std::move(first), std::move(second));
            }
            std::sort(edges.begin(), edges.end());
            result.push_back(std::move(edges));
        }
        if (result.size() > 1)
            std::sort(result.begin() + 1, result.end());
        return result;
    };
    try
    {
        const auto original = signature(0);
        if (original.empty())
            return angles;
        const bool quarterSymmetry = signature(90) == original;
        const bool halfSymmetry = quarterSymmetry || signature(180) == original;
        std::vector<double> unique;
        for (const double angle : angles)
        {
            const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](double old) {
                const double difference = Detail::NormalizeAngle(angle - old);
                return difference == 0 || (halfSymmetry && difference == 180) ||
                       (quarterSymmetry && (difference == 90 || difference == 270));
            });
            if (!duplicate)
                unique.push_back(angle);
        }
        return unique;
    }
    catch (const AxisSafety::ArithmeticUncertainty &)
    {
        return angles;
    }
}

std::size_t GeometryBytes(const AxisSafety::Geometry &geometry)
{
    std::size_t bytes = sizeof(geometry) + geometry.Rings.capacity() * sizeof(std::vector<AxisSafety::Vector>) +
                        geometry.Hull.capacity() * sizeof(AxisSafety::Vector);
    const auto terms = [](const AxisSafety::Vector &point) {
        return (point.X.Terms.capacity() + point.Y.Terms.capacity()) * sizeof(double);
    };
    bytes += terms(geometry.Center);
    for (const auto &ring : geometry.Rings)
    {
        bytes += ring.capacity() * sizeof(AxisSafety::Vector);
        for (const auto &point : ring)
            bytes += terms(point);
    }

    for (const auto &point : geometry.Hull)
        bytes += terms(point);
    return bytes;
}

class FeatureGreedy final : public IStrategy
{
  public:
    FeatureGreedy(FeatureGreedySettings settings, std::shared_ptr<NfpCache> cache)
        : Settings(std::move(settings)), Cache(cache ? std::move(cache) : std::make_shared<NfpCache>())
    {
    }

    Result Run(const Problem &problem, const Options &options) override
    {
        const auto started = Clock::now();
        const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
        std::size_t attempts = 0, steps = 0, nfpQueries = 0, featurePlacements = 0;
        std::size_t truncatedQueries = 0, unsupportedQueries = 0;
        std::size_t nextPieceRejections = 0, nextPieceChoices = 0;
        std::size_t reusedPlacementChecks = 0;
        std::size_t retained = 0, peak = 0;
        bool cancelled = false, memoryLimited = false, placementAvailable = false;
        std::vector<std::size_t> used(problem.Parts.size());
        std::vector<double> areas;
        areas.reserve(problem.Parts.size());
        std::vector<double> spans;
        spans.reserve(problem.Parts.size());
        std::vector<std::vector<double>> fallbackAngles(problem.Parts.size());
        std::vector<AxisSafety::Geometry> placedGeometry;
        std::vector<std::size_t> placedParts;
        std::vector<Rect> placedBounds;
        Layout board;
        std::optional<std::size_t> selectedStock;
        Rect occupied;
        std::uint64_t currentState = 1, currentParent = 0;
        double preparedSeconds = 0, verificationSeconds = 0;
        Detail::ConstructionObserver trace(Settings.Trace, Settings.MaxTraceEvents, started, attempts, steps);
        const auto stop = [&] {
            if (!cancelled && options.Cancel && options.Cancel())
                cancelled = true;
            return cancelled || memoryLimited || elapsed() >= options.TimeLimitSeconds ||
                   attempts >= options.MaxPlacementAttempts;
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
        const auto scratch = [&](std::size_t bytes) {
            if (bytes > Settings.MaxRetainedBytes - std::min(retained, Settings.MaxRetainedBytes))
            {
                memoryLimited = true;
                return false;
            }
            peak = std::max(peak, retained + bytes);
            return true;
        };
        const auto finish = [&](const std::string &reason) {
            Result result;
            if (!board.Placements.empty())
                result.Layouts.push_back(board);
            for (std::size_t i = 0; i < problem.Parts.size(); ++i)
                if (used[i] < problem.Parts[i].Quantity)
                    result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity - used[i]});
            result.State = cancelled                  ? Status::Cancelled
                           : result.Remaining.empty() ? Status::Complete
                                                      : Status::Partial;
            const double before = elapsed();
            result.GeometryValidated = Validate(problem, result).Valid;
            verificationSeconds += elapsed() - before;
            trace.Emit(ConstructionTraceStage::Stopped, [&](ConstructionTraceEvent &event) {
                event.StateID = currentState;
                event.ParentStateID = currentParent;
                event.Reason = reason;
                event.ResultStatus = result.State;
            });
            trace.Emit(
                ConstructionTraceStage::Selected,
                [&](ConstructionTraceEvent &event) {
                    event.StateID = currentState;
                    event.ParentStateID = currentParent;
                    event.ResultStatus = result.State;
                    event.EnvelopeArea = occupied.Width * occupied.Height;
                    event.MaterialArea = board.MaterialArea;
                    event.RegularRemnantArea = -1;
                    event.Reason = "greedy-selected-original-single-piece-result";
                    if (selectedStock)
                        event.StockIndex = *selectedStock;
                    event.BoardWidth = board.Width;
                    event.BoardHeight = board.Height;
                    for (std::size_t i = 0; i < board.Placements.size(); ++i)
                    {
                        const auto &placement = board.Placements[i];
                        event.Members.push_back({placedParts[i], placement.Instance, placement.Transform, 0});
                    }
                },
                true);
            result.Stats.PlacementAttempts = attempts;
            result.Stats.ElapsedSeconds = elapsed();
            result.Stats.PreparationSeconds = preparedSeconds;
            result.Stats.VerificationSeconds = verificationSeconds;
            result.Stats.PlacementSeconds = std::max(0., elapsed() - preparedSeconds - verificationSeconds);
            std::ostringstream note;
            note << "feature-greedy scope=single-sheet one-piece-per-step=1 steps=" << steps
                 << " feature-derived-placements=" << featurePlacements << " nfp-queries=" << nfpQueries
                 << " translation-generator-ready=" << placementAvailable
                 << " truncated-nfp-queries=" << truncatedQueries << " unsupported-nfp-queries=" << unsupportedQueries
                 << " next-piece-rejections=" << nextPieceRejections
                 << " certified-placement-reuses=" << reusedPlacementChecks
                 << " next-piece-reserved-choices=" << nextPieceChoices << " retained-high-water=" << peak
                 << " memory-limited=" << memoryLimited << " trace-events=" << trace.Count
                 << " trace-truncated=" << trace.Truncated << " trace-observer-failed=" << trace.Failed
                 << " placement-generator-cache=separate-32MiB clipper-internal-scratch=unmeasured";
            result.Diagnostics.push_back(note.str());
            return result;
        };
        const auto errors = Detail::CheckProblem(problem);
        if (!errors.empty() || !std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 ||
            !options.MaxPlacementAttempts || !Settings.MaxRetainedBytes || !Settings.MaxSpecificationsPerStep ||
            !Settings.MaxAnglesPerPart || !Settings.MaxCandidatesPerPart)
        {
            Result invalid;
            invalid.State = Status::InvalidInput;
            invalid.Diagnostics = errors;
            invalid.Diagnostics.push_back("feature-greedy: invalid input or limits");
            return invalid;
        }
        trace.Emit(ConstructionTraceStage::Begin, [&](ConstructionTraceEvent &event) {
            event.StateID = 1;
            event.Reason = "independent-single-piece-greedy-construction";
            if (!problem.Stocks.empty())
            {
                event.BoardWidth = problem.Stocks.front().Width;
                event.BoardHeight = problem.Stocks.front().Height;
            }
        });
        if (stop())
            return finish(cancelled ? "cancelled" : "search-budget");
        const auto preparationStop = [&] { return stop() || elapsed() >= options.TimeLimitSeconds * .25; };
        Detail::NfpPlacementGenerator placement(problem, Cache, preparationStop);
        placementAvailable = placement.Ready();
        Detail::ShapeFeatureLimits featureLimits;
        featureLimits.MaxProposals = 128;
        featureLimits.MaxAttempts = std::max<std::size_t>(1, options.MaxPlacementAttempts / 6);
        featureLimits.MaxBytes = Settings.MaxRetainedBytes / 4;
        std::vector<Detail::ShapeFeatureModel> models(problem.Parts.size());
        if (!retain(models.capacity() * sizeof(Detail::ShapeFeatureModel) + used.capacity() * sizeof(std::size_t) +
                    (areas.capacity() + spans.capacity()) * sizeof(double) +
                    fallbackAngles.capacity() * sizeof(std::vector<double>)))
            return finish("memory-limit");
        for (std::size_t part = 0; part < problem.Parts.size(); ++part)
        {
            areas.push_back(Detail::Area(problem.Parts[part].Geometry));
            const auto originalBounds = Detail::Bounds(problem.Parts[part].Geometry);
            spans.push_back(std::max(originalBounds.Width, originalBounds.Height));
            fallbackAngles[part] = FallbackAngles(problem.Parts[part], problem.Tolerance, preparationStop);
            std::stable_sort(fallbackAngles[part].begin(), fallbackAngles[part].end(), [&](double a, double b) {
                const auto first = Detail::Bounds(problem.Parts[part].Geometry, {0, 0, a});
                const auto second = Detail::Bounds(problem.Parts[part].Geometry, {0, 0, b});
                return first.Width * first.Height < second.Width * second.Height;
            });
            if (!retain(fallbackAngles[part].capacity() * sizeof(double)))
                return finish("memory-limit");
            if (!problem.Parts[part].Quantity || preparationStop())
                continue;
            featureLimits.MaxBytes = std::min(Settings.MaxRetainedBytes / 4, Settings.MaxRetainedBytes - retained);
            models[part] = Detail::EncodeShapeFeatures(problem.Parts[part].Geometry, problem.Tolerance, featureLimits,
                                                       preparationStop);
            if (!retain(models[part].RetainedBytes))
                return finish("memory-limit");
            trace.Emit(ConstructionTraceStage::Analysis, [&](ConstructionTraceEvent &event) {
                event.PartIndex = part;
                event.AnalysisTruncated = models[part].Truncated;
                auto original = Detail::TraceShape(problem.Parts[part].Geometry);
                event.Geometries.push_back(std::move(original));
                event.Reason = models[part].Diagnostic;
                for (std::size_t c = 0; c < models[part].Carriers.size(); ++c)
                {
                    const auto &carrier = models[part].Carriers[c];
                    auto geometry = Detail::TraceShape(carrier.Geometry);
                    geometry.Role = ConstructionTraceGeometryRole::Carrier;
                    geometry.CarrierIndex = c;
                    geometry.CarrierKind = static_cast<ConstructionTraceCarrierKind>(carrier.Kind);
                    event.Geometries.push_back(std::move(geometry));
                    for (std::size_t f = 0; f < carrier.Features.size(); ++f)
                    {
                        const auto &feature = carrier.Features[f];
                        ConstructionTraceGeometry residual;
                        residual.Role = feature.Polarity == Detail::FeaturePolarity::Recess
                                            ? ConstructionTraceGeometryRole::Recess
                                            : ConstructionTraceGeometryRole::Protrusion;
                        residual.CarrierIndex = c;
                        residual.FeatureIndex = f;
                        residual.Rings = feature.Rings;
                        event.Geometries.push_back(std::move(residual));
                        for (const auto &mouth : feature.Mouths)
                            event.Mouths.push_back(
                                {c, f, mouth.Start, mouth.End, mouth.Into, mouth.Width, mouth.Depth});
                    }
                }
            });
        }
        featureLimits.MaxBytes = std::min(Settings.MaxRetainedBytes / 4, Settings.MaxRetainedBytes - retained);
        const auto matches =
            Detail::BuildShapeFeatureMatches(problem, models, featureLimits, attempts, preparationStop, Cache);
        if (!retain(matches.RetainedBytes))
            return finish("memory-limit");
        std::vector<std::uint64_t> proposalIDs(matches.Matches.size());
        std::vector<std::vector<ContactPrior>> priors(problem.Parts.size());
        for (std::size_t index = 0; index < matches.Matches.size(); ++index)
        {
            const auto &match = matches.Matches[index];
            if (match.Value.Members.size() != 2)
                continue;
            proposalIDs[index] = trace.NewProposal();
            for (std::size_t fixed = 0; fixed < 2; ++fixed)
                priors[match.Value.Members[1 - fixed].PartIndex].push_back({index, fixed, 1 - fixed});
            trace.Emit(ConstructionTraceStage::Match, [&](ConstructionTraceEvent &event) {
                event.ProposalID = proposalIDs[index];
                event.Saving = match.FootprintSaving;
                event.Fill = match.CarrierFill;
                event.FeatureDerived = true;
                Detail::TracePattern(event, match.Value);
                event.FootprintArea = match.CarrierFill > 0 ? match.Value.MaterialArea / match.CarrierFill : 0;
                event.Reason = "pair-features-propose-single-piece-contacts";
            });
        }
        std::size_t priorBytes =
            proposalIDs.capacity() * sizeof(std::uint64_t) + priors.capacity() * sizeof(std::vector<ContactPrior>);
        for (const auto &list : priors)
            priorBytes += list.capacity() * sizeof(ContactPrior);
        if (!retain(priorBytes))
            return finish("memory-limit");
        preparedSeconds = elapsed();
        const double areaQuantum =
            problem.Tolerance * std::max(1., std::accumulate(problem.Stocks.begin(), problem.Stocks.end(), 0.,
                                                             [](double value, const Stock &stock) {
                                                                 return std::max(value, stock.Width + stock.Height);
                                                             }));
        const auto quantize = [&](double value) { return std::round(value / areaQuantum) * areaQuantum; };
        const auto candidateLimit = std::min<std::size_t>(128, Settings.MaxCandidatesPerPart);
        std::size_t geometryScratch = 1024;
        for (const auto &part : problem.Parts)
        {
            std::size_t vertices = part.Geometry.Outer.size();
            for (const auto &hole : part.Geometry.Holes)
                vertices += hole.size();
            geometryScratch = std::max(geometryScratch, 1024 + vertices * 512 + part.Geometry.Holes.size() * 256);
        }
        const std::size_t stepScratch = candidateLimit * 8 * sizeof(Proposal) +
                                        problem.Parts.size() * 2 * sizeof(std::size_t) + 3 * geometryScratch + 4096;
        const auto keyFor = [&](const Proposal &value) {
            const double ratio = std::round(value.Increment / areas[value.Part] * 1e9) / 1e9;
            const double x = std::round(value.Bounds.X / problem.Tolerance) * problem.Tolerance;
            const double y = std::round(value.Bounds.Y / problem.Tolerance) * problem.Tolerance;
            if (board.Placements.empty())
                return CandidateKey{Settings.Order == FeatureGreedyOrder::RemainingArea
                                        ? -areas[value.Part] * (problem.Parts[value.Part].Quantity - used[value.Part])
                                        : -areas[value.Part],
                                    quantize(value.Bounds.Width * value.Bounds.Height),
                                    x + y,
                                    -value.SeedAccess,
                                    value.Transform.AngleDegrees,
                                    static_cast<double>(value.Part),
                                    static_cast<double>(value.Stock)};
            if (Settings.Order == FeatureGreedyOrder::LowThenLeft)
                return CandidateKey{
                    y, x, ratio, -value.Saving, -value.Fill, -areas[value.Part], value.Transform.AngleDegrees};
            if (Settings.Order == FeatureGreedyOrder::RemainingArea)
                return CandidateKey{-areas[value.Part] * (problem.Parts[value.Part].Quantity - used[value.Part]),
                                    ratio,
                                    quantize(value.Increment),
                                    y,
                                    x,
                                    -value.Saving,
                                    -value.Fill};
            // Keep independent greedy policies. Equal board-waste choices
            // may favor complementary contacts or the lower-left position.
            if (Settings.Order == FeatureGreedyOrder::IncrementalWaste)
                return CandidateKey{quantize(value.Increment - areas[value.Part]), quantize(value.Increment),
                                    -value.Saving, -value.Fill, y, x, static_cast<double>(value.Part)};
            return CandidateKey{ratio, quantize(value.Increment), y, x, -value.Saving, -value.Fill,
                                static_cast<double>(value.Part)};
        };
        const auto makeProposal = [&](std::size_t part, std::size_t stock, Pose pose) {
            Proposal value;
            value.Part = part;
            value.Stock = stock;
            value.EvaluatedStep = steps;
            value.Transform = pose;
            value.Bounds = Detail::Bounds(problem.Parts[part].Geometry, pose);
            const auto combined = board.Placements.empty() ? value.Bounds : Enclose(occupied, value.Bounds);
            value.Increment = combined.Width * combined.Height - occupied.Width * occupied.Height;
            if (board.Placements.empty())
                for (const auto &carrier : models[part].Carriers)
                {
                    if (carrier.Kind != Detail::FeatureCarrierKind::ConvexHull)
                        continue;
                    for (const auto &feature : carrier.Features)
                    {
                        if (feature.Polarity != Detail::FeaturePolarity::Recess)
                            continue;
                        const auto &sheet = problem.Stocks[stock];
                        for (const auto &mouth : feature.Mouths)
                        {
                            const auto center = Detail::Transform(
                                {(mouth.Start.X + mouth.End.X) / 2, (mouth.Start.Y + mouth.End.Y) / 2}, pose);
                            const auto outward =
                                Detail::Transform({-mouth.Into.X, -mouth.Into.Y}, {0, 0, pose.AngleDegrees});
                            const Point probe{center.X + outward.X * mouth.Depth, center.Y + outward.Y * mouth.Depth};
                            if (probe.X >= problem.EdgeMargin && probe.X <= sheet.Width - problem.EdgeMargin &&
                                probe.Y >= problem.EdgeMargin && probe.Y <= sheet.Height - problem.EdgeMargin)
                            {
                                value.SeedAccess += feature.Area;
                                break;
                            }
                        }
                    }
                }
            value.Key = keyFor(value);
            return value;
        };
        std::vector<Proposal> alternatives;
        alternatives.reserve(4);
        const auto remember = [&](const Proposal &value) {
            const auto same = std::find_if(alternatives.begin(), alternatives.end(), [&](const Proposal &old) {
                return old.Part == value.Part && old.Stock == value.Stock &&
                       SameValidatedPose(old.Transform, value.Transform, problem.Tolerance);
            });
            if (same != alternatives.end())
            {
                if (value.Key < same->Key)
                    *same = value;
                return;
            }
            if (alternatives.size() < 4)
                alternatives.push_back(value);
            else
            {
                const bool represented = std::any_of(alternatives.begin(), alternatives.end(),
                                                     [&](const Proposal &old) { return old.Part == value.Part; });
                auto worst = alternatives.end();
                for (auto it = alternatives.begin(); it != alternatives.end(); ++it)
                {
                    const auto count = std::count_if(alternatives.begin(), alternatives.end(),
                                                     [&](const Proposal &old) { return old.Part == it->Part; });
                    if (it->Part != value.Part && count == 1)
                        continue;
                    if (worst == alternatives.end() || worst->Key < it->Key)
                        worst = it;
                }
                if (worst == alternatives.end())
                    worst = std::max_element(alternatives.begin(), alternatives.end(),
                                             [](const Proposal &a, const Proposal &b) { return a.Key < b.Key; });
                const auto duplicates = std::count_if(alternatives.begin(), alternatives.end(),
                                                      [&](const Proposal &old) { return old.Part == worst->Part; });
                if ((!represented && duplicates > 1) || value.Key < worst->Key)
                    *worst = value;
            }
        };
        const auto verifyProposal = [&](Proposal value) -> std::optional<VerifiedProposal> {
            if (!trial())
                return {};
            const auto part = value.Part;
            const auto &stock = problem.Stocks[value.Stock];
            auto geometry = AxisSafety::Make(problem.Parts[part].Geometry, value.Transform);
            const char *reason = "single-piece-candidate-safe";
            bool valid = Detail::InsideSheet(problem.Parts[part].Geometry, value.Transform, stock.Width, stock.Height,
                                             problem.EdgeMargin, problem.Tolerance) &&
                         AxisSafety::Inside(geometry, stock.Width, stock.Height, problem.EdgeMargin);
            if (!valid)
                reason = "original-inside-sheet-guard";
            // Find checked these poses against this exact immutable step's
            // layout. Its Fits never contain blocked displacement proposals.
            const bool certified = value.DomainVerified && value.EvaluatedStep == steps;
            if (valid && certified)
                ++reusedPlacementChecks;
            if (valid && !certified)
                for (std::size_t fixed = 0; fixed < placedGeometry.size(); ++fixed)
                {
                    if (!trial())
                        return {};
                    if (AxisSafety::Pair(geometry, placedGeometry[fixed], problem.PartGap, problem.AllowCommonEdges,
                                         problem.Tolerance) != AxisSafety::Verdict::Safe ||
                        Detail::ShapesConflict(problem.Parts[part].Geometry, value.Transform,
                                               problem.Parts[placedParts[fixed]].Geometry,
                                               board.Placements[fixed].Transform, problem.PartGap, problem.Tolerance,
                                               problem.AllowCommonEdges))
                    {
                        valid = false;
                        reason = "single-piece-existing-material-conflict";
                        break;
                    }
                }
            value.Candidate = trace.NewCandidate();
            value.State = valid ? trace.NewState() : 0;
            trace.Emit(ConstructionTraceStage::Candidate, [&](ConstructionTraceEvent &event) {
                event.CandidateID = value.Candidate;
                event.StateID = value.State;
                event.ParentStateID = currentState;
                event.Accepted = valid;
                event.ProposalID = value.Prior;
                event.StockIndex = value.Stock;
                event.BoardWidth = stock.Width;
                event.BoardHeight = stock.Height;
                event.Width = value.Bounds.Width;
                event.Height = value.Bounds.Height;
                event.Increment = value.Increment;
                event.Saving = value.Saving;
                event.Fill = value.Fill;
                event.FootprintArea = value.PriorFootprint;
                event.MaterialArea = areas[part];
                event.FeatureDerived = value.Feature;
                event.Reason = reason;
                event.Members.push_back({part, used[part], value.Transform, 0});
            });
            if (!valid)
                return {};
            remember(value);
            return VerifiedProposal{value, std::move(geometry)};
        };
        const auto find = [&](const Layout &target, std::size_t part, double angle) {
            Detail::NfpPlacementLimits limits;
            limits.MaxCandidates = 0;
            limits.MaxRawCandidates = candidateLimit * 64;
            limits.MaxContactSegments = 128;
            limits.MaxAttempts = std::min<std::size_t>(options.MaxPlacementAttempts - attempts, candidateLimit * 64);
            ++nfpQueries;
            auto result = placement.Find(target, part, angle, limits, stop);
            attempts += result.Attempts;
            truncatedQueries += result.Truncated;
            unsupportedQueries += !result.Supported;
            return result;
        };
        while (!stop())
        {
            std::size_t layoutScratch = 3 * board.Placements.capacity() * sizeof(Placement) + 4 * sizeof(Proposal);
            for (const auto &placed : board.Placements)
                layoutScratch += 2 * placed.PartID.capacity();
            if (!scratch(stepScratch + layoutScratch))
                break;
            alternatives.clear();
            std::vector<std::size_t> available;
            for (std::size_t part = 0; part < problem.Parts.size(); ++part)
                if (used[part] < problem.Parts[part].Quantity &&
                    (!selectedStock || Compatible(problem.Parts[part], problem.Stocks[*selectedStock])))
                    available.push_back(part);
            if (available.empty())
                break;
            std::stable_sort(available.begin(), available.end(), [&](auto a, auto b) {
                if (Settings.Order == FeatureGreedyOrder::RemainingArea)
                    return areas[a] * (problem.Parts[a].Quantity - used[a]) >
                           areas[b] * (problem.Parts[b].Quantity - used[b]);
                return areas[a] > areas[b];
            });
            if (!board.Placements.empty() && available.size() > Settings.MaxSpecificationsPerStep)
                std::rotate(available.begin(), available.begin() + steps % available.size(), available.end());
            std::optional<VerifiedProposal> best;
            std::size_t specifications = 0;
            for (const auto part : available)
            {
                if (stop() || (best && specifications >= Settings.MaxSpecificationsPerStep))
                    break;
                if (board.Placements.empty() && best && Settings.Order != FeatureGreedyOrder::RemainingArea &&
                    areas[part] < areas[best->Value.Part])
                    break;
                ++specifications;
                std::vector<Proposal> raw;
                const auto rawLimit = candidateLimit * 8;
                raw.reserve(rawLimit);
                const auto rawBetter = [](const Proposal &a, const Proposal &b) {
                    if (a.DomainVerified != b.DomainVerified)
                        return a.DomainVerified;
                    if (a.BoardAnchor != b.BoardAnchor)
                        return a.BoardAnchor;
                    return a.Key < b.Key;
                };
                const auto add = [&](Proposal value) {
                    if (!Detail::AngleAllowed(problem.Parts[part], value.Transform.AngleDegrees, problem.Tolerance))
                        return;
                    const auto &stock = problem.Stocks[value.Stock];
                    if (value.Bounds.X < problem.EdgeMargin - problem.Tolerance ||
                        value.Bounds.Y < problem.EdgeMargin - problem.Tolerance ||
                        value.Bounds.X + value.Bounds.Width > stock.Width - problem.EdgeMargin + problem.Tolerance ||
                        value.Bounds.Y + value.Bounds.Height > stock.Height - problem.EdgeMargin + problem.Tolerance)
                        return;
                    if (std::any_of(raw.begin(), raw.end(), [&](const Proposal &old) {
                            return old.Stock == value.Stock && SamePose(old.Transform, value.Transform);
                        }))
                        return;
                    if (raw.size() < rawLimit)
                        raw.push_back(std::move(value));
                    else
                    {
                        const auto worst = std::max_element(raw.begin(), raw.end(), rawBetter);
                        if (rawBetter(value, *worst))
                            *worst = std::move(value);
                    }
                };
                for (std::size_t fixed = 0; fixed < board.Placements.size() && !stop(); ++fixed)
                    for (const auto &prior : priors[part])
                    {
                        if (stop())
                            break;
                        const auto &match = matches.Matches[prior.Match];
                        const auto &anchor = match.Value.Members[prior.FixedMember];
                        if (anchor.PartIndex != placedParts[fixed])
                            continue;
                        const auto &moving = match.Value.Members[prior.MovingMember];
                        const auto &actual = board.Placements[fixed].Transform;
                        const double rotation =
                            Detail::NormalizeAngle(actual.AngleDegrees - anchor.Transform.AngleDegrees);
                        const auto offset = Detail::Transform(
                            {moving.Transform.X - anchor.Transform.X, moving.Transform.Y - anchor.Transform.Y},
                            {0, 0, rotation});
                        auto value = makeProposal(part, *selectedStock,
                                                  {actual.X + offset.X, actual.Y + offset.Y,
                                                   Detail::NormalizeAngle(moving.Transform.AngleDegrees + rotation)});
                        value.Feature = true;
                        value.Prior = proposalIDs[prior.Match];
                        value.Saving = match.FootprintSaving;
                        value.Fill = match.CarrierFill;
                        value.PriorFootprint = match.CarrierFill > 0 ? match.Value.MaterialArea / match.CarrierFill : 0;
                        value.Key = keyFor(value);
                        add(std::move(value));
                    }
                std::optional<VerifiedProposal> partBest;
                const auto verify = [&] {
                    std::stable_sort(raw.begin(), raw.end(),
                                     [](const Proposal &a, const Proposal &b) { return a.Key < b.Key; });
                    std::size_t accepted = 0;
                    for (const auto &value : raw)
                    {
                        auto candidate = verifyProposal(value);
                        if (!candidate)
                            continue;
                        if (!partBest || candidate->Value.Key < partBest->Value.Key)
                            partBest = std::move(candidate);
                        if (++accepted >= candidateLimit)
                            break;
                    }
                };
                verify();
                if ((board.Placements.empty() || !partBest) && !stop())
                {
                    raw.clear();
                    auto angles = fallbackAngles[part];
                    if (angles.size() > Settings.MaxAnglesPerPart)
                        angles.resize(Settings.MaxAnglesPerPart);
                    for (std::size_t stockIndex = 0; stockIndex < problem.Stocks.size() && !stop(); ++stockIndex)
                    {
                        const auto &stock = problem.Stocks[stockIndex];
                        if (!stock.Quantity || (selectedStock && stockIndex != *selectedStock) ||
                            !Compatible(problem.Parts[part], stock))
                            continue;
                        Layout query = board;
                        query.StockID = stock.ID;
                        query.Width = stock.Width;
                        query.Height = stock.Height;
                        for (const auto angle : angles)
                        {
                            if (stop())
                                break;
                            if (placement.Ready())
                            {
                                const auto result = find(query, part, angle);
                                for (const auto pose : result.Fits)
                                {
                                    auto value = makeProposal(part, stockIndex, pose);
                                    value.DomainVerified = true;
                                    add(std::move(value));
                                }
                            }
                            const auto bounds = Detail::Bounds(problem.Parts[part].Geometry, {0, 0, angle});
                            const double x = problem.EdgeMargin - bounds.X, y = problem.EdgeMargin - bounds.Y;
                            const auto addAnchor = [&](Pose pose) {
                                auto value = makeProposal(part, stockIndex, pose);
                                value.BoardAnchor = true;
                                add(std::move(value));
                            };
                            addAnchor({x, y, angle});
                            const double scale =
                                std::max({1., stock.Width, stock.Height, std::abs(bounds.X), std::abs(bounds.Y)});
                            const double inward = 32 * std::numeric_limits<double>::epsilon() * scale;
                            const double slackX = stock.Width - 2 * problem.EdgeMargin - bounds.Width;
                            const double slackY = stock.Height - 2 * problem.EdgeMargin - bounds.Height;
                            if (slackX > 0 && slackY > 0)
                                addAnchor({x + std::min(slackX / 2, inward), y + std::min(slackY / 2, inward), angle});
                            addAnchor({stock.Width - problem.EdgeMargin - bounds.X - bounds.Width, y, angle});
                            addAnchor({x, stock.Height - problem.EdgeMargin - bounds.Y - bounds.Height, angle});
                            addAnchor({stock.Width - problem.EdgeMargin - bounds.X - bounds.Width,
                                       stock.Height - problem.EdgeMargin - bounds.Y - bounds.Height, angle});
                            for (const auto &box : placedBounds)
                            {
                                add(makeProposal(
                                    part, stockIndex,
                                    {box.X + box.Width + problem.PartGap - bounds.X, box.Y - bounds.Y, angle}));
                                add(makeProposal(
                                    part, stockIndex,
                                    {box.X - bounds.X, box.Y + box.Height + problem.PartGap - bounds.Y, angle}));
                            }
                        }
                    }
                    verify();
                }
                if (partBest && (!best || partBest->Value.Key < best->Value.Key))
                    best = std::move(partBest);
            }
            // A feasible feature contact still competes with a bounded NFP
            // query for a tighter position of this step's leading piece.
            if (best && !stop() && selectedStock && placement.Ready() && best->Value.Feature &&
                best->Value.Increment > 0)
            {
                const auto part = best->Value.Part;
                const auto result = find(board, part, best->Value.Transform.AngleDegrees);
                for (const auto pose : result.Fits)
                {
                    auto proposal = makeProposal(part, *selectedStock, pose);
                    proposal.DomainVerified = true;
                    if (!(proposal.Key < best->Value.Key))
                        continue;
                    auto candidate = verifyProposal(proposal);
                    if (candidate && candidate->Value.Key < best->Value.Key)
                        best = std::move(candidate);
                }
            }
            // Keep the immediate greedy ordering, then check one remaining
            // large specification before consuming a slot it may still need.
            std::size_t remainingPieces = 0;
            for (std::size_t part = 0; part < problem.Parts.size(); ++part)
                remainingPieces = std::min<std::size_t>(
                    9, remainingPieces + std::min<std::size_t>(9, problem.Parts[part].Quantity - used[part]));
            const char *commitReason = "greedy-committed-one-original-piece";
            if (best && !stop() && placement.Ready() && remainingPieces <= 8)
            {
                const auto preferred = best->Value;
                std::stable_sort(alternatives.begin(), alternatives.end(),
                                 [](const Proposal &a, const Proposal &b) { return a.Key < b.Key; });
                std::vector<Proposal> choices{preferred};
                for (const auto &value : alternatives)
                    if (choices.size() < 4 &&
                        (value.Part != preferred.Part || value.Stock != preferred.Stock ||
                         !SameValidatedPose(value.Transform, preferred.Transform, problem.Tolerance)))
                        choices.push_back(value);
                bool reserved = false;
                std::optional<Proposal> unknownChoice;
                for (const auto &value : choices)
                {
                    if (stop())
                        break;
                    std::optional<std::size_t> critical;
                    for (std::size_t part = 0; part < problem.Parts.size(); ++part)
                    {
                        const auto consumed = used[part] + (part == value.Part ? 1 : 0);
                        if (consumed >= problem.Parts[part].Quantity ||
                            !Compatible(problem.Parts[part], problem.Stocks[value.Stock]))
                            continue;
                        if (!critical ||
                            std::tie(spans[part], areas[part]) > std::tie(spans[*critical], areas[*critical]))
                            critical = part;
                    }
                    if (!critical)
                    {
                        best = VerifiedProposal{value,
                                                AxisSafety::Make(problem.Parts[value.Part].Geometry, value.Transform)};
                        reserved = true;
                        break;
                    }
                    Layout future = board;
                    const auto &stock = problem.Stocks[value.Stock];
                    future.StockID = stock.ID;
                    future.Width = stock.Width;
                    future.Height = stock.Height;
                    future.Placements.push_back({problem.Parts[value.Part].ID, used[value.Part], value.Transform, 0});
                    auto angles = fallbackAngles[*critical];
                    bool unknown = angles.size() > Settings.MaxAnglesPerPart, feasible = false;
                    if (angles.size() > Settings.MaxAnglesPerPart)
                        angles.resize(Settings.MaxAnglesPerPart);
                    for (const auto angle : angles)
                    {
                        if (stop())
                        {
                            unknown = true;
                            break;
                        }
                        Detail::NfpPlacementLimits limits;
                        limits.MaxCandidates = 1;
                        limits.MaxRawCandidates = 256;
                        limits.MaxContactSegments = 128;
                        limits.MaxAttempts = std::min<std::size_t>(512, options.MaxPlacementAttempts - attempts);
                        limits.StopAfterFirstFit = true;
                        limits.FastAdjacency = true;
                        ++nfpQueries;
                        const auto query = placement.Find(future, *critical, angle, limits, stop);
                        attempts += query.Attempts;
                        truncatedQueries += query.Truncated;
                        unsupportedQueries += !query.Supported;
                        unknown = unknown || query.Truncated || !query.Supported;
                        if (!query.Fits.empty())
                        {
                            feasible = true;
                            break;
                        }
                    }
                    if (feasible)
                    {
                        best = VerifiedProposal{value,
                                                AxisSafety::Make(problem.Parts[value.Part].Geometry, value.Transform)};
                        if (value.Part != preferred.Part || value.Stock != preferred.Stock ||
                            !SamePose(value.Transform, preferred.Transform))
                            ++nextPieceChoices;
                        reserved = true;
                        break;
                    }
                    if (unknown)
                    {
                        if (!unknownChoice)
                            unknownChoice = value;
                        continue;
                    }
                    ++nextPieceRejections;
                    trace.Emit(ConstructionTraceStage::Backtrack, [&](ConstructionTraceEvent &event) {
                        event.StateID = value.State;
                        event.ParentStateID = currentState;
                        event.StockIndex = value.Stock;
                        event.BoardWidth = stock.Width;
                        event.BoardHeight = stock.Height;
                        event.PartIndex = *critical;
                        event.Reason = "greedy-next-required-part-no-fit";
                    });
                }
                if (!reserved && unknownChoice)
                {
                    const auto &value = *unknownChoice;
                    best =
                        VerifiedProposal{value, AxisSafety::Make(problem.Parts[value.Part].Geometry, value.Transform)};
                    commitReason = "greedy-committed-with-next-fit-unresolved";
                }
                else if (!reserved)
                    commitReason = "greedy-kept-immediate-choice-after-lookahead-no-fit";
            }
            if (!best || cancelled || memoryLimited)
                break;
            const auto &value = best->Value;
            if (!retain(GeometryBytes(best->Geometry) + 2 * sizeof(Placement) + problem.Parts[value.Part].ID.size() +
                        2 * sizeof(std::size_t) + 2 * sizeof(Rect)))
                break;
            if (!selectedStock)
            {
                selectedStock = value.Stock;
                const auto &stock = problem.Stocks[value.Stock];
                board.StockID = stock.ID;
                board.Width = stock.Width;
                board.Height = stock.Height;
            }
            occupied = board.Placements.empty() ? value.Bounds : Enclose(occupied, value.Bounds);
            board.Placements.push_back({problem.Parts[value.Part].ID, used[value.Part]++, value.Transform, 0});
            board.MaterialArea += areas[value.Part];
            placedGeometry.push_back(std::move(best->Geometry));
            placedParts.push_back(value.Part);
            placedBounds.push_back(value.Bounds);
            currentParent = currentState;
            currentState = value.State;
            ++steps;
            if (value.Feature)
                ++featurePlacements;
            trace.Emit(ConstructionTraceStage::Expand, [&](ConstructionTraceEvent &event) {
                event.StateID = currentState;
                event.ParentStateID = currentParent;
                event.StockIndex = *selectedStock;
                event.BoardWidth = board.Width;
                event.BoardHeight = board.Height;
                event.EnvelopeArea = occupied.Width * occupied.Height;
                event.MaterialArea = board.MaterialArea;
                event.Reason = commitReason;
            });
        }
        return finish(cancelled       ? "cancelled"
                      : memoryLimited ? "memory-limit"
                      : stop()        ? "search-budget"
                                      : "greedy-no-more-feasible-single-piece-candidates");
    }

  private:
    FeatureGreedySettings Settings;
    std::shared_ptr<NfpCache> Cache;
};
} // namespace

StrategyFactory MakeFeatureGreedyFactory(FeatureGreedySettings settings, std::shared_ptr<NfpCache> cache)
{
    return [settings = std::move(settings), cache = std::move(cache)] {
        return std::make_unique<FeatureGreedy>(settings, cache);
    };
}
} // namespace iCAX::SheetNesting
