#include "SheetNestingSparrow.h"
#include "NestingInternal.h"
#include "NestingExecution.h"

// This translation unit belongs exclusively to the optional Sparrow target;
// the ordinary SheetNesting library has no JSON or Rust dependency.
#ifndef BOOST_ALL_NO_LIB
#define BOOST_ALL_NO_LIB
#endif
#include <boost/json/src.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C"
{
    char* sparrow_solve_json(const char* request, bool (*cancel)(void*), void* context);
    void sparrow_free_json(char* response);
}

namespace iCAX::SheetNesting
{
    namespace
    {
        namespace json = boost::json;
        using Clock = std::chrono::steady_clock;
        constexpr const char* Revision = "ab2aaef602755f1ed51b15406a2f0ef0950e18ce";

        double Seconds(Clock::time_point since)
        { return std::chrono::duration<double>(Clock::now() - since).count(); }

        Result EmptyResult(const Problem& problem, Status state, std::string diagnostic)
        {
            Result result;
            result.State = state;
            if (!diagnostic.empty()) result.Diagnostics.push_back(std::move(diagnostic));
            for (const auto& part : problem.Parts)
                if (part.Quantity) result.Remaining.push_back({part.ID, part.Quantity});
            return result;
        }

        std::string ScopeError(const Problem& problem, const SparrowSettings& settings)
        {
            if (problem.PartGap != 0 || problem.EdgeMargin != 0)
                return "Sparrow supports only zero part gap and zero edge margin in this adapter.";
            if (problem.Stocks.size() != 1 || problem.Stocks.front().Quantity == 0)
                return "Sparrow requires one available rectangular stock type and solves one physical sheet.";
            const auto& stock = problem.Stocks.front();
            for (const auto& part : problem.Parts)
            {
                if (!part.Quantity) continue;
                if (part.Geometry.Radius != 0 || !part.Geometry.Holes.empty() || part.Geometry.Outer.size() < 3)
                    return "Sparrow supports simple polygon exteriors without holes or circles: " + part.ID;
                if (part.Quantity && part.Material != stock.Material)
                    return "Sparrow requires all demanded parts to match the single stock material: " + part.ID;
            }
            if (settings.Threads < 1 || settings.Threads > 32 ||
                !std::isfinite(settings.ExplorationFraction) || settings.ExplorationFraction <= 0 || settings.ExplorationFraction >= 1 ||
                !std::isfinite(settings.PolygonSimplificationRatio) || settings.PolygonSimplificationRatio > 1 ||
                !std::isfinite(settings.NarrowConcavityRatio) || settings.NarrowConcavityRatio > 1 ||
                !std::isfinite(settings.NumericalGuard) || settings.NumericalGuard < 0)
                return "Invalid Sparrow thread, phase fraction, preprocessing ratio or numerical guard settings.";
            return {};
        }

        // Rust may poll from several workers. Serialize access to the caller's
        // std::function and capture every exception before returning across C.
        struct Cancellation
        {
            const Options& Settings;
            Clock::time_point Started;
            std::mutex Mutex;
            std::atomic<bool> UserCancelled{false}, CallbackFailed{false}, BudgetExpired{false};

            bool Poll() noexcept
            {
                try
                {
                    if (UserCancelled.load() || CallbackFailed.load()) return true;
                    std::lock_guard lock(Mutex);
                    if (UserCancelled.load() || CallbackFailed.load()) return true;
                    if (Settings.Cancel && Settings.Cancel())
                    { UserCancelled.store(true); return true; }
                    if (Seconds(Started) >= Settings.TimeLimitSeconds)
                    { BudgetExpired.store(true); return true; }
                    return false;
                }
                catch (...)
                { CallbackFailed.store(true); return true; }
            }

            static bool Invoke(void* context) noexcept
            { return !context || static_cast<Cancellation*>(context)->Poll(); }
        };

        json::value OptionalRatio(double ratio)
        { return ratio < 0 ? json::value(nullptr) : json::value(ratio); }

        json::object Request(const Problem& problem, const Options& options,
            const SparrowSettings& settings, double seconds, const std::vector<std::size_t>& activeParts)
        {
            json::array items;
            items.reserve(activeParts.size());
            for (std::size_t i = 0; i < activeParts.size(); ++i)
            {
                const auto& part = problem.Parts[activeParts[i]];
                json::array points;
                points.reserve(part.Geometry.Outer.size());
                // No hull replacement, source origin shift, angle rounding or
                // hidden padding is performed by the C++ transport.
                for (const auto point : part.Geometry.Outer)
                    points.push_back(json::array{point.X, point.Y});
                json::object item{{"id", i}, {"demand", part.Quantity},
                    {"shape", json::object{{"type", "simple_polygon"}, {"data", std::move(points)}}}};
                if (!part.FreeRotation)
                {
                    json::array angles;
                    if (part.AllowedAnglesDegrees.empty()) angles.push_back(0.0);
                    else for (const auto angle : part.AllowedAnglesDegrees) angles.push_back(angle);
                    item.emplace("allowed_orientations", std::move(angles));
                }
                items.push_back(std::move(item));
            }
            return {{"protocol", 1},
                {"instance", json::object{{"name", "iCAX-independent-job"},
                    {"strip_height", problem.Stocks.front().Height}, {"items", std::move(items)}}},
                {"seed", options.Seed}, {"time_limit_seconds", seconds},
                {"threads", settings.Threads}, {"exploration_fraction", settings.ExplorationFraction},
                {"polygon_simplification_ratio", OptionalRatio(settings.PolygonSimplificationRatio)},
                {"narrow_concavity_ratio", OptionalRatio(settings.NarrowConcavityRatio)},
                {"numerical_guard", settings.NumericalGuard}};
        }

        void AddInitialLayout(json::object& request, const Problem& problem, const Result& initial,
            const std::vector<std::size_t>& activeParts)
        {
            json::array poses;
            double right = 0;
            for (const auto& placement : initial.Layouts.front().Placements)
            {
                const auto found = std::find_if(activeParts.begin(), activeParts.end(), [&](std::size_t index)
                    { return problem.Parts[index].ID == placement.PartID; });
                if (found == activeParts.end()) throw std::runtime_error("Foreign original warm-refine part.");
                const auto partIndex = *found;
                const auto bounds = Detail::Bounds(problem.Parts[partIndex].Geometry, placement.Transform);
                right = std::max(right, bounds.X + bounds.Width);
                poses.push_back(json::object{{"part_index", static_cast<std::size_t>(found-activeParts.begin())},
                    {"instance", placement.Instance}, {"x", placement.Transform.X}, {"y", placement.Transform.Y},
                    {"angle_degrees", placement.Transform.AngleDegrees}});
            }
            request.emplace("mode", "warm_compress");
            request.emplace("initial_layout", std::move(poses));
            request.emplace("initial_occupied_width", right);
            request.emplace("stock_width_cap", problem.Stocks.front().Width);
        }

        std::size_t Index(const json::value& value)
        {
            std::uint64_t index = 0;
            if (value.is_uint64()) index = value.as_uint64();
            else if (value.is_int64() && value.as_int64() >= 0) index = static_cast<std::uint64_t>(value.as_int64());
            else throw std::runtime_error("Sparrow returned a nonintegral or negative source index.");
            if (index > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Sparrow source index exceeds the native index range.");
            return static_cast<std::size_t>(index);
        }

        double Number(const json::value& value)
        {
            if (!value.is_number()) throw std::runtime_error("Sparrow returned a nonnumeric pose or extent.");
            const auto number = value.to_number<double>();
            if (!std::isfinite(number)) throw std::runtime_error("Sparrow returned a nonfinite pose or extent.");
            return number;
        }

        class SparrowStrategy final : public IStrategy
        {
        public:
            explicit SparrowStrategy(SparrowSettings settings) : Settings(settings) {}

            Result Run(const Problem& input, const Options& options) override
            { return RunWithInitial(input, options, nullptr); }

            Result RunWithInitial(const Problem& input, const Options& options, const Result* initial)
            {
                const auto started = Clock::now();
                Statistics stats;
                const auto failed = [&](Status state, std::string message)
                {
                    if (initial)
                    {
                        auto result = *initial;
                        result.Diagnostics.push_back("Sparrow warm-refine retained immutable incumbent: " + message);
                        result.Stats = stats;
                        result.Stats.ElapsedSeconds = Seconds(started);
                        return result;
                    }
                    auto result = EmptyResult(input, state, std::move(message));
                    result.Stats = stats;
                    result.Stats.ElapsedSeconds = Seconds(started);
                    return result;
                };
                try
                {
                    auto errors = Detail::CheckProblem(input);
                    if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 || options.TimeLimitSeconds > 86400)
                        errors.push_back("Sparrow requires a positive finite job time limit of at most 86400 seconds.");
                    if (!errors.empty())
                    {
                        auto result = failed(Status::InvalidInput, "Invalid original Sparrow job input.");
                        result.Diagnostics.insert(result.Diagnostics.end(), errors.begin(), errors.end());
                        return result;
                    }
                    const auto scope = ScopeError(input, Settings);
                    if (!scope.empty()) return failed(Status::InvalidInput, scope);
                    if (initial)
                    {
                        const auto checked = Validate(input, *initial);
                        stats.VerificationSeconds = Seconds(started);
                        if (initial->State != Status::Complete || !checked.Valid || !checked.Complete ||
                            !initial->Remaining.empty() || initial->Layouts.size()!=1)
                            return failed(Status::InvalidInput, "warm-refine requires one complete original-native-valid layout.");
                        const auto& stock = input.Stocks.front();
                        const auto& layout = initial->Layouts.front();
                        if (layout.StockID!=stock.ID || layout.StockInstance>=stock.Quantity ||
                            layout.Width!=stock.Width || layout.Height!=stock.Height)
                            return failed(Status::InvalidInput, "warm-refine incumbent changed original stock identity or dimensions.");
                        if(std::any_of(layout.Placements.begin(),layout.Placements.end(),
                            [](const Placement& placement){return placement.Transform.Mirrored;}))
                            return failed(Status::InvalidInput,
                                "the external transport does not support mirrored initial poses; original reflection and identities retained without invoking the kernel.");
                    }
                    Cancellation cancellation{options, started};
                    if (cancellation.Poll())
                        return failed(cancellation.CallbackFailed.load() ? Status::Failed :
                            cancellation.UserCancelled.load() ? Status::Cancelled : Status::Failed,
                            cancellation.CallbackFailed.load() ? "Sparrow cancellation callback threw; no exception crossed FFI." :
                            cancellation.UserCancelled.load() ? "Sparrow job cancelled before kernel entry." : "Sparrow time budget expired before kernel entry.");

                    std::size_t required = 0;
                    std::vector<std::size_t> activeParts;
                    for (std::size_t i = 0; i < input.Parts.size(); ++i)
                    {
                        required += input.Parts[i].Quantity;
                        if (input.Parts[i].Quantity) activeParts.push_back(i);
                    }
                    if (required == 0)
                    {
                        Result result; result.State = Status::Complete;
                        result.GeometryValidated = Validate(input, result).Valid;
                        result.Stats.ElapsedSeconds = Seconds(started);
                        return result;
                    }
                    const auto remainingSeconds=std::max(0.0, options.TimeLimitSeconds - Seconds(started));
                    // A warm stage must complete its original-native and
                    // shared-quality gates inside the same owning budget.
                    // Reserve a small part for those gates instead of spending
                    // the entire budget inside a naturally time-limited kernel.
                    const auto kernelSecondsBudget=initial?remainingSeconds*0.9:remainingSeconds;
                    auto requestObject = Request(input, options, Settings,kernelSecondsBudget,activeParts);
                    if (initial) AddInitialLayout(requestObject, input, *initial, activeParts);
                    const auto request = json::serialize(requestObject);
                    stats.PreparationSeconds = Seconds(started);
                    const auto solveStarted = Clock::now();
                    std::unique_ptr<char, decltype(&sparrow_free_json)> response(
                        sparrow_solve_json(request.c_str(), &Cancellation::Invoke, &cancellation), &sparrow_free_json);
                    stats.PlacementSeconds = Seconds(solveStarted);
                    if (cancellation.CallbackFailed.load())
                        return failed(Status::Failed, "Sparrow cancellation callback threw; no exception crossed FFI.");
                    if (!response) return failed(cancellation.UserCancelled.load() ? Status::Cancelled : Status::Failed,
                        "Sparrow bridge returned no response.");
                    const auto payload = json::parse(response.get()).as_object();
                    if (Index(payload.at("protocol")) != 1)
                        return failed(Status::Failed, "Unsupported Sparrow bridge protocol.");
                    const bool kernelStopRequested = payload.at("cancelled").as_bool();
                    if (!payload.at("ok").as_bool())
                    {
                        const auto* error = payload.if_contains("error");
                        return failed(cancellation.UserCancelled.load() ? Status::Cancelled : Status::Failed, "Sparrow kernel failed: " +
                            (error && error->is_string() ? std::string(error->as_string()) : std::string("unspecified kernel error")));
                    }
                    if (payload.at("upstream_revision").as_string() != Revision)
                        return failed(Status::Failed, "Sparrow bridge revision differs from the pinned kernel.");
                    if (initial)
                    {
                        const auto origin=payload.at("initial_solution_origin").as_string();
                        if (payload.at("mode").as_string()!="warm_compress" ||
                            (origin!="provided-incumbent"&&origin!="provided-incumbent-after-internal-feasibility-repair") ||
                            Number(payload.at("stock_width_cap"))!=input.Stocks.front().Width ||
                            Number(payload.at("initial_occupied_width"))!=Number(requestObject.at("initial_occupied_width")))
                            return failed(Status::Failed,"warm-refine response is not bound to the supplied incumbent mode and cap.");
                        if (origin=="provided-incumbent-after-internal-feasibility-repair")
                        {
                            const auto& repair=payload.at("warm_feasibility_repair").as_object();
                            const auto budget=Number(repair.at("budget_seconds"));
                            const auto elapsed=Number(repair.at("elapsed_seconds"));
                            if (!repair.at("accepted").as_bool() || repair.at("imported_seed_reported_feasible").as_bool() ||
                                !repair.at("original_stock_cap_unchanged").as_bool() ||
                                budget<=0||elapsed<0||elapsed>budget||budget>std::min(15.0,Number(requestObject.at("time_limit_seconds"))*.1))
                                return failed(Status::Failed,"warm-refine feasibility repair failed its explicit phase gate.");
                        }
                    }
                    const auto width = Number(payload.at("occupied_width"));
                    const auto kernelSeconds = Number(payload.at("elapsed_seconds"));
                    const auto& stock = input.Stocks.front();
                    if (width <= 0 || kernelSeconds < 0)
                        return failed(Status::Failed, "Sparrow returned invalid strip width or elapsed time measurements.");

                    const auto& placements = payload.at("placements").as_array();
                    if (placements.size() != required)
                        return failed(Status::Failed, "Sparrow output does not contain the complete original instance count.");
                    Layout layout{stock.ID, initial?initial->Layouts.front().StockInstance:0, stock.Width, stock.Height, {}, 0};
                    layout.Placements.reserve(required);
                    std::set<std::pair<std::size_t, std::size_t>> identities;
                    double sourceRight = 0;
                    for (const auto& entry : placements)
                    {
                        const auto& row = entry.as_object();
                        const auto externalIndex = Index(row.at("part_index"));
                        const auto instance = Index(row.at("instance"));
                        if (externalIndex >= activeParts.size())
                            return failed(Status::Failed, "Sparrow output has a foreign source part index.");
                        const auto partIndex = activeParts[externalIndex];
                        if (instance >= input.Parts[partIndex].Quantity ||
                            !identities.emplace(partIndex, instance).second)
                            return failed(Status::Failed, "Sparrow output has a foreign, duplicate or out-of-demand source instance.");
                        const auto& part = input.Parts[partIndex];
                        Pose pose{Number(row.at("x")), Number(row.at("y")), Number(row.at("angle_degrees"))};
                        const auto bounds = Detail::Bounds(part.Geometry, pose);
                        sourceRight = std::max(sourceRight, bounds.X + bounds.Width);
                        std::size_t group=0;bool filler=false;
                        if (initial)
                        {
                            const auto& originals=initial->Layouts.front().Placements;
                            const auto old=std::find_if(originals.begin(), originals.end(), [&](const Placement& p)
                                { return p.PartID==part.ID&&p.Instance==instance; });
                            if (old==originals.end()) return failed(Status::Failed,"warm-refine returned a foreign original identity.");
                            group=old->Group;filler=old->GapFiller;
                        }
                        layout.Placements.push_back({part.ID, instance, pose, group, filler});
                    }
                    // Upstream's width is f32 metadata. The original source
                    // material at the actual returned native pose determines
                    // the stock cap, without trusting or padding that metadata.
                    if (sourceRight > stock.Width + input.Tolerance)
                        return failed(Status::Failed, "Sparrow original native material exceeds the single-stock width cap.");
                    if (initial && sourceRight > Number(requestObject.at("initial_occupied_width")))
                        return failed(Status::Failed, "warm-refine original material is wider than the incumbent.");
                    Result candidate; candidate.State = Status::Complete;
                    candidate.Layouts.push_back(std::move(layout));
                    const auto verifyStarted = Clock::now();
                    const auto checked = Validate(input, candidate);
                    stats.VerificationSeconds = std::max(0.0,stats.VerificationSeconds)+Seconds(verifyStarted);
                    if (!checked.Valid || !checked.Complete || checked.PlacedCount != required)
                    {
                        auto result = failed(Status::Failed, "Sparrow proposal failed complete original native validation.");
                        result.Diagnostics.insert(result.Diagnostics.end(), checked.Errors.begin(), checked.Errors.end());
                        return result;
                    }
                    // Poll once more after final validation, including callbacks
                    // which change state while the bridge or validator runs.
                    cancellation.Poll();
                    if (cancellation.CallbackFailed.load())
                        return failed(Status::Failed, "Sparrow cancellation callback threw; no exception crossed FFI.");
                    if (initial && (cancellation.UserCancelled.load() || cancellation.BudgetExpired.load()))
                        return failed(Status::Cancelled,"warm-refine deadline/cancellation reached before acceptance.");
                    if (cancellation.UserCancelled.load()) candidate.State = Status::Cancelled;
                    candidate.GeometryValidated = true;
                    candidate.Layouts.front().MaterialArea = checked.MaterialArea;
                    if (initial)
                    {
                        // The same measurement and lexicographic quality key
                        // as pipeline/portfolio selection owns acceptance.
                        const auto qualityStarted=Clock::now();
                        JobResult before,after;before.Output=*initial;after.Output=candidate;
                        Detail::AssessCandidate(input,before);Detail::AssessCandidate(input,after);
                        stats.EvaluationSeconds=Seconds(qualityStarted);
                        if (!Detail::Eligible(before) || !Detail::Eligible(after) ||
                            !(Detail::Quality(after,input,options.Ranking)<Detail::Quality(before,input,options.Ranking)))
                            return failed(Status::Failed,"warm-refine proposal did not improve the shared quality key.");
                        if (cancellation.Poll())
                            return failed(Status::Cancelled,"warm-refine deadline/cancellation reached during quality assessment.");
                        // Preserve original order and constructor provenance;
                        // upstream slotmap insertion order is search state.
                        const auto generated=candidate.Layouts.front().Placements;
                        candidate.Layouts.front().Placements=initial->Layouts.front().Placements;
                        for (auto& placement:candidate.Layouts.front().Placements)
                        {
                            const auto found=std::find_if(generated.begin(),generated.end(),[&](const Placement& p)
                                {return p.PartID==placement.PartID&&p.Instance==placement.Instance;});
                            placement.Transform=found->Transform;
                        }
                    }
                    candidate.Stats = stats;
                    candidate.Stats.ElapsedSeconds = Seconds(started);
                    std::ostringstream diagnostic;
                    diagnostic << "Sparrow independent pinned kernel " << Revision << "; original-native-complete=" << required
                        << "; occupied-width=" << sourceRight << "; reported-width=" << width << "; kernel-seconds=" << kernelSeconds
                        << "; threads=" << Settings.Threads << "; kernel-stop-requested=" << kernelStopRequested
                        << "; user-cancelled=" << cancellation.UserCancelled.load()
                        << "; deadline-polled=" << cancellation.BudgetExpired.load()
                        << (initial?"; mode=warm_compress; original identity/provenance retained; no-author-coordinate-seed; single-sheet scope."
                            :"; initialization uses upstream cooperative termination; no-author-coordinate-seed; single-sheet scope.");
                    if (initial) diagnostic<<" initial-source="<<payload.at("initial_solution_origin").as_string();
                    candidate.Diagnostics.push_back(diagnostic.str());
                    return candidate;
                }
                catch (const std::exception& error)
                { return failed(Status::Failed, std::string("Sparrow adapter failed: ") + error.what()); }
                catch (...)
                { return failed(Status::Failed, "Sparrow adapter failed with a nonstandard exception; original demand retained."); }
            }

        private:
            const SparrowSettings Settings;
        };

        class SparrowRefine final : public IPostProcessor
        {
        public:
            explicit SparrowRefine(SparrowSettings settings):Settings(settings){}
            Result Run(const Problem& input,const Result& incumbent,const Options& options) override
            { return SparrowStrategy(Settings).RunWithInitial(input,options,&incumbent); }
        private:
            const SparrowSettings Settings;
        };
    }

    StrategyFactory MakeSparrowFactory(SparrowSettings settings)
    { return [settings] { return std::make_unique<SparrowStrategy>(settings); }; }

    PostProcessStage MakeSparrowRefineStage(SparrowSettings settings)
    { return {"sparrow.warm-compress",[settings]{return std::make_unique<SparrowRefine>(settings);}}; }
}
