#include "NestingExecution.h"
#include "NestingTrace.h"
#include "SheetNestingPipeline.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace iCAX::SheetNesting
{
    namespace
    {
        class SerializedConstructionTrace
        {
            const std::function<void(const ConstructionTraceEvent&)>& Callback;
            std::shared_ptr<ConstructionTraceHealth> Health;
            std::chrono::steady_clock::time_point Started;
            std::mutex Mutex;
            std::uint64_t Sequence = 0;
            bool Failed = false;
        public:
            SerializedConstructionTrace(const Options& options, std::chrono::steady_clock::time_point started)
                : Callback(options.ConstructionTrace), Health(options.ConstructionObservation), Started(started) {}
            void Record(const ConstructionTraceEvent& source) noexcept
            {
                // This mutex protects observation only. No worker-pool or wake
                // mutex is held while calling the user's observer.
                std::lock_guard lock(Mutex);
                if (!Callback) return;
                if (Failed || (Health && Health->Failed.load()))
                { if (Health) ++Health->Omitted; return; }
                try
                {
                    auto event = source;
                    if (event.Step) event.Measures.emplace_back("nativeStep", static_cast<double>(event.Step));
                    event.Step = ++Sequence;
                    event.Measures.emplace_back("portfolioWallSeconds",
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count());
                    Callback(event);
                }
                catch (...)
                {
                    Failed = true;
                    if (Health) { Health->Failed.store(true); ++Health->Omitted; }
                }
            }
            bool HasFailed()
            { std::lock_guard lock(Mutex); return Failed || (Health && Health->Failed.load()); }
        };

        void TraceQuality(ConstructionTraceEvent& event, const Detail::QualityKey& key, RankingPolicy policy)
        {
            if(policy==RankingPolicy::Utilization)
            {
                event.Rank={{"incomplete",double(std::get<0>(key))},{"partialNetMaterial",std::get<2>(key)},
                    {"negativeStockUtilization",std::get<4>(key)},{"negativeUsedSectionUtilization",std::get<5>(key)},
                    {"usedStockArea",std::get<7>(key)},{"usedSectionArea",std::get<8>(key)},
                    {"occupiedEnvelopeArea",std::get<9>(key)},{"topSum",std::get<10>(key)},{"rightSum",std::get<11>(key)},
                    {"meanY",std::get<12>(key)},{"meanX",std::get<13>(key)}};
                event.Measures.emplace_back("stockUtilization",-std::get<4>(key));
                event.Measures.emplace_back("usedSectionUtilization",-std::get<5>(key));
                event.Measures.emplace_back("envelopeUtilization",std::get<9>(key)>0?
                    -std::get<4>(key)*std::get<7>(key)/std::get<9>(key):0.);
                event.Measures.emplace_back("rankingPolicy",static_cast<double>(policy));
                return;
            }
            event.Rank.emplace_back("missing", static_cast<double>(std::get<0>(key)));
            event.Rank.emplace_back("boards", static_cast<double>(std::get<1>(key)));
            event.Rank.emplace_back("partialMaterial", std::get<2>(key));
            event.Rank.emplace_back("remnantUnavailable", std::get<3>(key) ? 1. : 0.);
            event.Rank.emplace_back("negativeRegularRemnantArea", std::get<4>(key));
            event.Rank.emplace_back("consolidation", std::get<5>(key));
            const auto& prefix = std::get<6>(key);
            // This is a vector-valued lexicographic slot, not a sum or mean.
            event.Measures.emplace_back("frontUtilizationCount", static_cast<double>(prefix.size()));
            for (std::size_t i = 0; i < prefix.size(); ++i)
                event.Rank.emplace_back("frontUtilization[" + std::to_string(i) + "]", prefix[i]);
            event.Rank.emplace_back("tailMaterial", std::get<7>(key));
            event.Rank.emplace_back("tailDensity", std::get<8>(key));
            event.Rank.emplace_back(policy == RankingPolicy::CompactArea ? "usedEnvelopeArea" : "topSum", std::get<9>(key));
            event.Rank.emplace_back(policy == RankingPolicy::CompactArea ? "topSum" : "rightSum", std::get<10>(key));
            event.Rank.emplace_back(policy == RankingPolicy::CompactArea ? "rightSum" :
                policy == RankingPolicy::LowThenLeft ? "usedEnvelopeArea" : "reservedZero", std::get<11>(key));
            event.Rank.emplace_back("meanY", std::get<12>(key));
            event.Rank.emplace_back("meanX", std::get<13>(key));
            event.Measures.emplace_back("rankingPolicy", static_cast<double>(policy));
        }
        std::string QualityDifference(const Detail::QualityKey& candidate, const Detail::QualityKey& incumbent,
            RankingPolicy policy)
        {
            if(policy==RankingPolicy::Utilization)
            {
                if(std::get<0>(candidate)!=std::get<0>(incumbent))return "complete-demand";
                if(std::get<2>(candidate)!=std::get<2>(incumbent))return "partial-net-material-progress";
                if(std::get<4>(candidate)!=std::get<4>(incumbent))return "stock-utilization";
                if(std::get<5>(candidate)!=std::get<5>(incumbent))return "used-section-utilization";
                if(std::get<7>(candidate)!=std::get<7>(incumbent))return "used-stock-area";
                if(std::get<8>(candidate)!=std::get<8>(incumbent))return "used-section-area";
                if(std::get<9>(candidate)!=std::get<9>(incumbent))return "occupied-envelope-area";
                if(std::get<10>(candidate)!=std::get<10>(incumbent))return "topSum";
                if(std::get<11>(candidate)!=std::get<11>(incumbent))return "rightSum";
                if(std::get<12>(candidate)!=std::get<12>(incumbent))return "meanY";
                if(std::get<13>(candidate)!=std::get<13>(incumbent))return "meanX";
                return "equal";
            }
            if (std::get<0>(candidate) != std::get<0>(incumbent)) return "missing";
            if (std::get<1>(candidate) != std::get<1>(incumbent)) return "boards";
            if (std::get<2>(candidate) != std::get<2>(incumbent)) return "partialMaterial";
            if (std::get<3>(candidate) != std::get<3>(incumbent)) return "remnantUnavailable";
            if (std::get<4>(candidate) != std::get<4>(incumbent)) return "negativeRegularRemnantArea";
            if (std::get<5>(candidate) != std::get<5>(incumbent)) return "consolidation";
            const auto& a = std::get<6>(candidate); const auto& b = std::get<6>(incumbent);
            for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                if (a[i] != b[i]) return "frontUtilization[" + std::to_string(i) + "]";
            if (a.size() != b.size()) return "frontUtilization.length";
            if (std::get<7>(candidate) != std::get<7>(incumbent)) return "tailMaterial";
            if (std::get<8>(candidate) != std::get<8>(incumbent)) return "tailDensity";
            if (std::get<9>(candidate) != std::get<9>(incumbent)) return policy == RankingPolicy::CompactArea ? "usedEnvelopeArea" : "topSum";
            if (std::get<10>(candidate) != std::get<10>(incumbent)) return policy == RankingPolicy::CompactArea ? "topSum" : "rightSum";
            if (std::get<11>(candidate) != std::get<11>(incumbent)) return policy == RankingPolicy::CompactArea ? "rightSum" :
                policy == RankingPolicy::LowThenLeft ? "usedEnvelopeArea" : "reservedZero";
            if (std::get<12>(candidate) != std::get<12>(incumbent)) return "meanY";
            if (std::get<13>(candidate) != std::get<13>(incumbent)) return "meanX";
            return "equal";
        }
        void TraceResult(ConstructionTraceEvent& event, const JobResult& candidate)
        {
            event.ResultStatus = candidate.Output.State;
            event.Layouts = candidate.Output.Layouts;
            event.Remaining = candidate.Output.Remaining;
            event.Attempts = candidate.Output.Stats.PlacementAttempts;
            event.Measures.emplace_back("valid", candidate.Checked.Valid ? 1. : 0.);
            event.Measures.emplace_back("complete", candidate.Checked.Complete ? 1. : 0.);
            event.Measures.emplace_back("placed", static_cast<double>(candidate.Checked.PlacedCount));
            event.Measures.emplace_back("routeElapsedSeconds", candidate.Output.Stats.ElapsedSeconds);
            event.Measures.emplace_back("remnantMeasured", candidate.Remnants.Measured ? 1. : 0.);
        }
        void TraceComparison(ConstructionTraceEvent& event, const Detail::QualityKey& score,
            const std::optional<Detail::QualityKey>& bestScore, const std::string& incumbent,
            RankingPolicy policy)
        {
            TraceQuality(event, score, policy);
            const bool better = !bestScore || score < *bestScore;
            event.Accepted = better;
            event.Reason = !bestScore ? "first-eligible-candidate" :
                std::string(better ? "strictly-better" : score == *bestScore ? "equal-kept-incumbent" : "worse-kept-incumbent") +
                    "; incumbent=" + incumbent + "; deciding-field=" + QualityDifference(score, *bestScore, policy);
            if (bestScore)
            {
                ConstructionTraceEvent prior;
                TraceQuality(prior, *bestScore, policy);
                for (const auto& [name, value] : prior.Rank) event.Measures.emplace_back("incumbent." + name, value);
                for (const auto& [name, value] : prior.Measures) event.Measures.emplace_back("incumbent." + name, value);
            }
        }

        void AddRemaining(Result& result, const Problem& p)
        { for (const auto& part : p.Parts) if (part.Quantity) result.Remaining.push_back({ part.ID, part.Quantity }); }
        JobResult Execute(const Job& job, std::unique_ptr<IStrategy> strategy, const std::string& factoryError,
            bool* failedAfterSearchStarted = nullptr)
        {
            if (failedAfterSearchStarted) *failedAfterSearchStarted = false;
            const auto started = std::chrono::steady_clock::now();
            bool searchStarted = false;
            JobResult completed;
            completed.ID = job.ID;
            completed.StrategyID = job.StrategyID;
            auto errors = Detail::CheckProblem(job.Input);
            if (!std::isfinite(job.Settings.TimeLimitSeconds) || job.Settings.TimeLimitSeconds <= 0 ||
                job.Settings.MaxPlacementAttempts == 0 || job.Settings.MaxPatterns == 0 ||
                job.Settings.MaxPatterns > 4096 || job.Settings.MaxPatternMembers < 1 || job.Settings.MaxPatternMembers > 4 ||
                job.Settings.MaxAssemblyMembers < 1 || job.Settings.MaxAssemblyMembers > 12)
                errors.push_back("Invalid job time/attempt/pattern limits (pattern members 1..4, assembly members 1..12, patterns 1..4096).");
            if (job.Settings.EnableBasicCompaction &&
                (!std::isfinite(job.Settings.BasicCompactionBudgetFraction) ||
                 job.Settings.BasicCompactionBudgetFraction <= 0 || job.Settings.BasicCompactionBudgetFraction >= 1))
                errors.push_back("Basic-compaction budget fraction must be finite and between zero and one.");
            if (job.Settings.Ranking != RankingPolicy::RegularRemnant &&
                job.Settings.Ranking != RankingPolicy::LowThenLeft && job.Settings.Ranking != RankingPolicy::CompactArea &&
                job.Settings.Ranking != RankingPolicy::Utilization)
                errors.push_back("Unknown ranking policy.");
            if (!errors.empty())
            {
                completed.Output.State = Status::InvalidInput;
                completed.Output.Diagnostics = std::move(errors);
                AddRemaining(completed.Output, job.Input);
                completed.Output.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                return completed;
            }
            try
            {
                if (!strategy) throw std::runtime_error(factoryError.empty() ? "Strategy factory returned null." : factoryError);
                if (job.Settings.EnableBasicCompaction && !strategy->OwnsRefinementLifecycle())
                    strategy = Detail::MakeRefinementExecutor(std::move(strategy));
                searchStarted = true;
                completed.Output = strategy->Run(job.Input, job.Settings);
                Detail::AssessCandidate(job.Input, completed);
            }
            catch (const std::exception& error)
            {
                if (failedAfterSearchStarted) *failedAfterSearchStarted = searchStarted;
                completed.Output = {};
                completed.Output.State = Status::Failed;
                completed.Output.Diagnostics.push_back(error.what());
                AddRemaining(completed.Output, job.Input);
            }
            catch (...)
            {
                if (failedAfterSearchStarted) *failedAfterSearchStarted = searchStarted;
                completed.Output = {};
                completed.Output.State = Status::Failed;
                completed.Output.Diagnostics.push_back("Strategy threw a non-standard exception.");
                AddRemaining(completed.Output, job.Input);
            }
            completed.Output.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            return completed;
        }

        class ConstructionPortfolio final : public IStrategy
        {
        public:
            ConstructionPortfolio(StrategyRegistry registry, std::vector<std::string> ids)
                : Registry(std::move(registry)), IDs(std::move(ids)) {}

            bool OwnsRefinementLifecycle() const noexcept override { return true; }
            bool IsPureConstruction() const noexcept override { return true; }

            Result Run(const Problem& input, const Options& options) override
            {
                const auto started = std::chrono::steady_clock::now();
                const auto elapsed = [&]
                { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
                SerializedConstructionTrace trace(options, started);
                auto observed = options;
                if (options.ConstructionTrace)
                    observed.ConstructionTrace = [&trace](const ConstructionTraceEvent& event) { trace.Record(event); };
                const auto observe = [&](ConstructionTraceStage stage, std::size_t i, const char* source, auto&& build)
                {
                    Detail::ObserveConstruction(observed, stage, [&](ConstructionTraceEvent& event)
                    {
                        if (i < IDs.size()) event.Route = IDs[i];
                        event.Source = source; event.ElapsedSeconds = elapsed(); build(event);
                    });
                };
                bool cancelled = false;
                const auto pollCancel = [&]
                {
                    if (!cancelled && options.Cancel && options.Cancel()) cancelled = true;
                    return cancelled;
                };
                Result empty;
                empty.State = Status::Partial;
                AddRemaining(empty, input);
                auto errors = Detail::CheckProblem(input);
                if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 || !options.MaxPlacementAttempts)
                    errors.push_back("Construction portfolio needs a positive finite time and attempt budget.");
                if (!errors.empty())
                {
                    empty.State = Status::InvalidInput;
                    empty.Diagnostics = std::move(errors);
                    empty.Stats.ElapsedSeconds = elapsed();
                    return empty;
                }
                JobResult best;
                std::optional<Detail::QualityKey> bestScore;
                Statistics work;
                work.VerificationSeconds = work.EvaluationSeconds = work.PostProcessingSeconds = 0;
                std::vector<std::string> notes;
                std::size_t completed = 0, full = 0;
                const auto add = [](std::size_t a, std::size_t b)
                { return a + std::min(b, std::numeric_limits<std::size_t>::max() - a); };
                std::vector<unsigned char> routeStarted(IDs.size());
                for (std::size_t i = 0; i < IDs.size(); ++i)
                    observe(ConstructionTraceStage::Dispatch, i, "scheduler.serial-register", [&](auto& event)
                    { event.Reason = "registered"; event.Measures.emplace_back("registrationIndex", static_cast<double>(i)); });
                for (std::size_t i = 0; i < IDs.size(); ++i)
                {
                    if (pollCancel() || elapsed() >= options.TimeLimitSeconds ||
                        work.PlacementAttempts >= options.MaxPlacementAttempts) break;
                    const auto slots = IDs.size() - i;
                    auto settings = observed;
                    settings.TimeLimitSeconds = std::max(1e-9, (options.TimeLimitSeconds - elapsed()) / double(slots));
                    settings.MaxPlacementAttempts = std::max<std::size_t>(1,
                        (options.MaxPlacementAttempts - work.PlacementAttempts) / slots);
                    settings.EnableBasicCompaction = false;
                    settings.Seed += i;
                    settings.ConstructionTraceRoute = IDs[i];
                    settings.Cancel = pollCancel;
                    std::unique_ptr<IStrategy> strategy;
                    std::string factoryError;
                    try
                    {
                        strategy = Registry.Create(IDs[i]);
                        if (strategy && !strategy->IsPureConstruction())
                        {
                            factoryError = "Construction portfolio requires a pure constructor. A composed refinement strategy was skipped.";
                            strategy.reset();
                        }
                    }
                    catch (const std::exception& error) { factoryError = error.what(); }
                    catch (...) { factoryError = "Construction factory threw a non-standard exception."; }
                    observe(strategy ? ConstructionTraceStage::Dispatch : ConstructionTraceStage::Filter,
                        i, "scheduler.serial-factory", [&](auto& event)
                    { event.Accepted = bool(strategy); event.Reason = factoryError.empty() ?
                        strategy ? "pure-constructor-prepared" : "factory-returned-null" : factoryError; });
                    if (pollCancel() || elapsed() >= options.TimeLimitSeconds)
                    {
                        notes.push_back("construction.job=" + IDs[i] + " stopped after factory preparation");
                        break;
                    }
                    settings.TimeLimitSeconds = std::min(settings.TimeLimitSeconds,
                        std::max(1e-9, options.TimeLimitSeconds - elapsed()));
                    bool failedAfterSearchStarted = false;
                    routeStarted[i] = 1;
                    observe(ConstructionTraceStage::Begin, i, "scheduler.serial-route", [&](auto& event)
                    { event.Reason = "constructor-started"; event.Measures = {{"localTimeSeconds", settings.TimeLimitSeconds},
                        {"attemptCap", static_cast<double>(settings.MaxPlacementAttempts)}, {"automaticBasicCompaction", 0.}}; });
                    auto candidate = Execute({"construction:" + std::to_string(i), IDs[i], input, settings},
                        std::move(strategy), factoryError, &failedAfterSearchStarted);
                    if (failedAfterSearchStarted)
                    {
                        // An exception loses the actual search count. Charge
                        // its allocated cap before sharing the remaining work.
                        candidate.Output.Stats.PlacementAttempts = settings.MaxPlacementAttempts;
                        candidate.Output.Diagnostics.push_back("Construction failed after search started; allocated attempt cap charged conservatively.");
                    }
                    ++completed;
                    observe(ConstructionTraceStage::Complete, i, "scheduler.serial-route-result", [&](auto& event)
                    { TraceResult(event, candidate); event.Accepted = Detail::Eligible(candidate);
                        event.Reason = "constructor-returned-after-validation-and-measurement"; });
                    full += Detail::Eligible(candidate) && candidate.Checked.Complete;
                    work.PlacementAttempts = add(work.PlacementAttempts, candidate.Output.Stats.PlacementAttempts);
                    work.CandidatePatterns = add(work.CandidatePatterns, candidate.Output.Stats.CandidatePatterns);
                    const auto accumulate = [](double& total, double value)
                    { if (value >= 0) total = std::max(0., total) + value; };
                    accumulate(work.PreparationSeconds, candidate.Output.Stats.PreparationSeconds);
                    accumulate(work.PlacementSeconds, candidate.Output.Stats.PlacementSeconds);
                    accumulate(work.VerificationSeconds, candidate.Output.Stats.VerificationSeconds);
                    accumulate(work.EvaluationSeconds, candidate.Output.Stats.EvaluationSeconds);
                    std::ostringstream note;
                    note << "construction.job=" << IDs[i] << " placed=" << candidate.Checked.PlacedCount
                        << " complete=" << candidate.Checked.Complete << " valid=" << candidate.Checked.Valid
                        << " attempts=" << candidate.Output.Stats.PlacementAttempts;
                    notes.push_back(note.str());
                    for (const auto& diagnostic : candidate.Output.Diagnostics)
                        notes.push_back("construction.job=" + IDs[i] + " | " + diagnostic);
                    if (candidate.Output.State == Status::Cancelled) cancelled = true;
                    if (Detail::Eligible(candidate))
                    {
                        const auto score = Detail::Quality(candidate, input, options.Ranking);
                        observe(ConstructionTraceStage::Score, i, "scheduler.serial-quality", [&](auto& event)
                        { TraceComparison(event, score, bestScore, bestScore ? best.StrategyID : std::string{}, options.Ranking); });
                        if (!bestScore || score < *bestScore) { bestScore = score; best = std::move(candidate); }
                    }
                }
                pollCancel();
                auto result = bestScore ? std::move(best.Output) : std::move(empty);
                work.CombinationGroups = result.Stats.CombinationGroups;
                work.CircleLatticeGroups = result.Stats.CircleLatticeGroups;
                work.GapFilledParts = result.Stats.GapFilledParts;
                work.ElapsedSeconds = elapsed();
                result.Stats = work;
                if (cancelled) result.State = Status::Cancelled;
                for (std::size_t i = 0; i < IDs.size(); ++i) if (!routeStarted[i])
                    observe(ConstructionTraceStage::Stopped, i, "scheduler.serial-not-started", [&](auto& event)
                    { event.Accepted = false; event.Reason = cancelled ? "original-user-cancellation" :
                        elapsed() >= options.TimeLimitSeconds ? "common-wall-deadline" :
                        work.PlacementAttempts >= options.MaxPlacementAttempts ? "global-attempt-budget-exhausted" : "stopped-after-factory-preparation"; });
                observe(ConstructionTraceStage::Selected, IDs.size(), "scheduler.serial-final-selection", [&](auto& event)
                {
                    event.Route = bestScore ? best.StrategyID : std::string{};
                    event.Accepted = bool(bestScore); event.ResultStatus = result.State;
                    event.Layouts = result.Layouts; event.Remaining = result.Remaining; event.Attempts = work.PlacementAttempts;
                    event.Reason = cancelled ? "original-user-cancellation" : elapsed() >= options.TimeLimitSeconds ? "common-wall-deadline" :
                        work.PlacementAttempts >= options.MaxPlacementAttempts ? "global-attempt-budget-exhausted" : "constructors-finished";
                    if (bestScore) TraceQuality(event, *bestScore, options.Ranking);
                });
                if (trace.HasFailed()) notes.push_back("construction observation callback failed; subsequent trace events omitted; solver results retained.");
                result.Diagnostics.insert(result.Diagnostics.end(), notes.begin(), notes.end());
                result.Diagnostics.push_back("construction.portfolio jobs=" + std::to_string(completed) +
                    "/" + std::to_string(IDs.size()) + " complete=" + std::to_string(full) +
                    " selected=" + (bestScore ? best.StrategyID : std::string("none")));
                return result;
            }
        private:
            StrategyRegistry Registry;
            std::vector<std::string> IDs;
        };

        class ParallelConstructionPortfolio final : public IStrategy
        {
        public:
            ParallelConstructionPortfolio(StrategyRegistry registry, std::vector<std::string> ids,
                ParallelConstructionSettings settings)
                : Registry(std::move(registry)), IDs(std::move(ids)), Settings(std::move(settings)) {}

            bool OwnsRefinementLifecycle() const noexcept override { return true; }
            bool IsPureConstruction() const noexcept override { return true; }

            Result Run(const Problem& input, const Options& options) override
            {
                using Clock = std::chrono::steady_clock;
                const auto started = Clock::now();
                const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
                SerializedConstructionTrace trace(options, started);
                auto observed = options;
                if (options.ConstructionTrace)
                    observed.ConstructionTrace = [&trace](const ConstructionTraceEvent& event) { trace.Record(event); };
                const auto allRoutes = std::numeric_limits<std::size_t>::max();
                const auto observe = [&](ConstructionTraceStage stage, std::size_t route, const char* source, auto&& build)
                {
                    Detail::ObserveConstruction(observed, stage, [&](ConstructionTraceEvent& event)
                    {
                        if (route < IDs.size()) event.Route = IDs[route];
                        event.Source = source;
                        event.ElapsedSeconds = elapsed();
                        build(event);
                    });
                };
                observe(ConstructionTraceStage::Begin, allRoutes, "scheduler.parallel", [&](auto& event)
                {
                    event.Measures = {{"routes", static_cast<double>(IDs.size())}, {"workersRequested", static_cast<double>(Settings.Workers)},
                        {"timeLimitSeconds", options.TimeLimitSeconds}, {"maxAttempts", static_cast<double>(options.MaxPlacementAttempts)},
                        {"fairScheduling", Settings.FairScheduling ? 1. : 0.}, {"rankingPolicy", static_cast<double>(options.Ranking)},
                        {"explicitStopPredicate", Settings.StopWhen ? 1. : 0.}};
                    event.Reason = "independent-constructors; original-demand-per-route; common-wall-deadline";
                });
                Result empty; empty.State = Status::Partial; AddRemaining(empty, input);
                auto errors = Detail::CheckProblem(input);
                if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 ||
                    !options.MaxPlacementAttempts || !options.MaxPatterns || options.MaxPatterns > 4096 ||
                    options.MaxPatternMembers < 1 || options.MaxPatternMembers > 4 ||
                    options.MaxAssemblyMembers < 1 || options.MaxAssemblyMembers > 12 || Settings.Workers > 32)
                    errors.push_back("Parallel construction needs valid time, attempts, patterns and 0..32 workers.");
                if (options.Ranking != RankingPolicy::RegularRemnant && options.Ranking != RankingPolicy::LowThenLeft &&
                    options.Ranking != RankingPolicy::CompactArea && options.Ranking != RankingPolicy::Utilization) errors.push_back("Unknown ranking policy.");
                if (!errors.empty())
                {
                    empty.State = Status::InvalidInput; empty.Diagnostics = std::move(errors);
                    observe(ConstructionTraceStage::Stopped, allRoutes, "scheduler.parallel", [&](auto& event)
                    { event.Accepted = false; event.ResultStatus = empty.State; event.Reason = "invalid-input-or-scheduler-settings"; event.Remaining = empty.Remaining; });
                    empty.Stats.ElapsedSeconds = elapsed(); return empty;
                }
                std::vector<std::string> notes;
                std::atomic<bool> stop{false}, startWorkers{false};
                bool cancelled = false, cancelFailed = false, deadline = false, targetReached = false;
                bool stopObserved = false;
                const auto pollControl = [&]
                {
                    if (!cancelled && !cancelFailed && options.Cancel)
                    {
                        try { cancelled = options.Cancel(); }
                        catch (const std::exception& error)
                        { cancelFailed = true; notes.push_back(std::string("parallel.construction cancel callback failed: ") + error.what()); }
                        catch (...) { cancelFailed = true; notes.push_back("parallel.construction cancel callback threw a non-standard exception."); }
                    }
                    deadline = deadline || elapsed() >= options.TimeLimitSeconds;
                    if (cancelled || cancelFailed || deadline || targetReached) stop.store(true, std::memory_order_release);
                    if (!stopObserved && (cancelled || cancelFailed || deadline || targetReached))
                    {
                        stopObserved = true;
                        observe(ConstructionTraceStage::Stopped, allRoutes, "scheduler.stop", [&](auto& event)
                        {
                            event.Reason = cancelled ? "original-user-cancellation" : cancelFailed ? "original-cancel-callback-failed" :
                                targetReached ? "explicit-target-predicate" : "common-wall-deadline";
                            event.Measures = {{"cancelled", cancelled ? 1. : 0.}, {"cancelFailed", cancelFailed ? 1. : 0.},
                                {"deadline", deadline ? 1. : 0.}, {"targetReached", targetReached ? 1. : 0.}};
                        });
                    }
                    return stop.load(std::memory_order_acquire);
                };
                std::vector<std::unique_ptr<IStrategy>> strategies(IDs.size());
                std::vector<unsigned char> factoryVisited(IDs.size());
                std::vector<std::size_t> runnable;
                for (std::size_t i = 0; i < IDs.size(); ++i)
                    observe(ConstructionTraceStage::Dispatch, i, "scheduler.register", [&](auto& event)
                    { event.Measures.emplace_back("registrationIndex", static_cast<double>(i)); event.Reason = "registered"; });
                // User factories are never invoked by a worker, even when a
                // factory itself owns mutable state or fails during preparation.
                for (std::size_t i = 0; i < IDs.size() && !pollControl(); ++i)
                {
                    factoryVisited[i] = 1;
                    observe(ConstructionTraceStage::Dispatch, i, "scheduler.factory", [&](auto& event)
                    { event.Reason = "factory-preparation"; });
                    std::string error;
                    try
                    {
                        strategies[i] = Registry.Create(IDs[i]);
                        if (!strategies[i]) error = "Strategy factory returned null.";
                        else if (!strategies[i]->IsPureConstruction())
                        { strategies[i].reset(); error = "A composed refinement strategy is not a pure initial constructor."; }
                    }
                    catch (const std::exception& e) { error = e.what(); }
                    catch (...) { error = "Factory threw a non-standard exception."; }
                    if (!error.empty()) notes.push_back("parallel.construction job=" + IDs[i] + " skipped: " + error);
                    if (strategies[i]) runnable.push_back(i);
                    observe(strategies[i] ? ConstructionTraceStage::Dispatch : ConstructionTraceStage::Filter,
                        i, "scheduler.factory", [&](auto& event)
                    { event.Accepted = bool(strategies[i]); event.Reason = error.empty() ? "pure-constructor-prepared" : error; });
                }
                std::vector<std::size_t> caps(IDs.size());
                std::vector<Job> jobs(IDs.size());
                const auto prepared = runnable.size();
                std::vector<std::size_t> scheduled;
                for (std::size_t rank = 0; rank < prepared; ++rank)
                {
                    const auto i = runnable[rank];
                    if (!Settings.FairScheduling)
                    {
                        caps[i] = options.MaxPlacementAttempts / prepared + (rank < options.MaxPlacementAttempts % prepared);
                        if (!caps[i])
                        {
                            notes.push_back("parallel.construction job=" + IDs[i] + " skipped: no attempt share.");
                            observe(ConstructionTraceStage::Filter, i, "scheduler.budget", [&](auto& event)
                            { event.Accepted = false; event.Reason = "no-attempt-share"; });
                            continue;
                        }
                    }
                    auto settings = observed; settings.EnableBasicCompaction = false;
                    settings.MaxPlacementAttempts = caps[i]; settings.Seed += i;
                    settings.ConstructionTraceRoute = IDs[i];
                    settings.ContinuousConstructionTime = settings.ContinuousConstructionTime ||
                        std::find(Settings.ContinuousTimeRoutes.begin(), Settings.ContinuousTimeRoutes.end(), IDs[i]) != Settings.ContinuousTimeRoutes.end();
                    settings.Cancel = [&stop] { return stop.load(std::memory_order_acquire); };
                    jobs[i] = {"parallel-construction:" + std::to_string(i), IDs[i], input, std::move(settings)};
                    scheduled.push_back(i);
                    observe(ConstructionTraceStage::Dispatch, i, "scheduler.queue", [&](auto& event)
                    { event.Reason = "queued"; event.Measures = {{"queueIndex", static_cast<double>(scheduled.size()-1)},
                        {"seed", static_cast<double>(jobs[i].Settings.Seed)}, {"automaticBasicCompaction", 0.}}; });
                }
                std::vector<JobResult> results(IDs.size());
                std::vector<std::exception_ptr> exceptions(IDs.size());
                std::vector<unsigned char> chargeCap(IDs.size()), collected(IDs.size()), routeStarted(IDs.size());
                std::vector<std::string> notStartedReasons(IDs.size());
                std::vector<std::atomic<bool>> done(IDs.size());
                for (auto& value : done) value.store(false, std::memory_order_relaxed);
                std::atomic<std::size_t> next{0}, active{0}, finished{0};
                std::atomic<bool> infrastructureFailure{false};
                std::mutex wakeMutex; std::condition_variable wake;
                struct PublishedCandidate
                {
                    std::size_t Route;
                    JobResult Value;
                    Detail::QualityKey Score;
                    double AssessmentSeconds;
                    Clock::time_point PublishedAt;
                };
                std::mutex publicationMutex;
                std::optional<PublishedCandidate> pendingPublication;
                std::optional<Detail::QualityKey> publishedBestScore;
                std::atomic<bool> publicationReady{false}, publicationFailed{false};
                std::atomic<std::size_t> validatedPublications{0};
                struct AttemptPool
                {
                    std::mutex Mutex; std::condition_variable Changed;
                    std::size_t Available, Unleased, Unfinished, Active = 0, Spent = 0;
                    AttemptPool(std::size_t available, std::size_t routes)
                        : Available(available), Unleased(routes), Unfinished(routes) {}
                } pool(options.MaxPlacementAttempts, scheduled.size());
                struct AttemptLease
                {
                    AttemptPool& Pool; std::size_t Cap = 0; bool Active = false;
                    void Return(std::size_t spent)
                    {
                        if (!Active) return;
                        {
                            std::lock_guard lock(Pool.Mutex);
                            spent = std::min(spent, Cap);
                            Pool.Available += Cap - spent; Pool.Spent += spent;
                            --Pool.Active; --Pool.Unfinished; Active = false;
                        }
                        Pool.Changed.notify_all();
                    }
                    // An infrastructure exception cannot silently donate an
                    // unknown amount of already started work to later routes.
                    ~AttemptLease() { Return(Cap); }
                };
                std::size_t effectiveWorkers = 1;
                const auto worker = [&](std::size_t workerIndex)
                {
                    try
                    {
                        {
                            std::unique_lock lock(wakeMutex);
                            wake.wait(lock, [&] { return startWorkers.load(std::memory_order_acquire); });
                        }
                        while (!stop.load(std::memory_order_acquire))
                        {
                            const auto slot = next.fetch_add(1, std::memory_order_relaxed);
                            if (slot >= scheduled.size()) break;
                            const auto i = scheduled[slot];
                            double remaining = options.TimeLimitSeconds - elapsed();
                            if (remaining <= 0) { stop.store(true, std::memory_order_release); break; }
                            AttemptLease lease{pool};
                            std::size_t availableBefore = 0, unleasedBefore = 0, unfinishedBefore = 0, waves = 1;
                            if (Settings.FairScheduling)
                            {
                                std::unique_lock lock(pool.Mutex);
                                // A tiny total cap may all be reserved by active
                                // routes. Wait for an unused lease to return,
                                // rather than starving queued routes prematurely.
                                while (!pool.Available && pool.Active && !stop.load(std::memory_order_acquire))
                                {
                                    if (elapsed() >= options.TimeLimitSeconds) break;
                                    pool.Changed.wait_for(lock, std::chrono::milliseconds(2));
                                }
                                remaining = options.TimeLimitSeconds - elapsed();
                                if (stop.load(std::memory_order_acquire) || remaining <= 0)
                                { if (remaining <= 0) stop.store(true, std::memory_order_release); break; }
                                if (!pool.Available)
                                {
                                    --pool.Unleased; --pool.Unfinished;
                                    notStartedReasons[i] = "global attempt budget exhausted";
                                    lock.unlock();
                                    observe(ConstructionTraceStage::Stopped, i, "scheduler.budget", [&](auto& event)
                                    { event.Accepted = false; event.Reason = "global-attempt-budget-exhausted-before-start";
                                        event.Measures.emplace_back("worker", static_cast<double>(workerIndex)); });
                                    continue;
                                }
                                availableBefore = pool.Available; unleasedBefore = pool.Unleased; unfinishedBefore = pool.Unfinished;
                                caps[i] = pool.Available / pool.Unleased + (pool.Available % pool.Unleased != 0);
                                pool.Available -= caps[i]; --pool.Unleased; ++pool.Active;
                                lease.Cap = caps[i]; lease.Active = true;
                                waves = pool.Unfinished / effectiveWorkers + (pool.Unfinished % effectiveWorkers != 0);
                                if (!jobs[i].Settings.ContinuousConstructionTime)
                                    remaining = std::min(remaining, std::max(1e-9, remaining / std::max<std::size_t>(1, waves)));
                            }
                            observe(ConstructionTraceStage::Budget, i, "scheduler.lease", [&](auto& event)
                            {
                                event.Reason = Settings.FairScheduling ? "remaining-budget-lease" : "fixed-attempt-share";
                                event.Measures = {{"worker", static_cast<double>(workerIndex)}, {"queueIndex", static_cast<double>(slot)},
                                    {"localTimeSeconds", remaining}, {"attemptCap", static_cast<double>(caps[i])},
                                    {"commonTimeRemaining", std::max(0., options.TimeLimitSeconds-elapsed())},
                                    {"availableAttemptsBefore", static_cast<double>(availableBefore)}, {"unleasedRoutesBefore", static_cast<double>(unleasedBefore)},
                                    {"unfinishedRoutesBefore", static_cast<double>(unfinishedBefore)}, {"waves", static_cast<double>(waves)},
                                    {"continuousConstructionTime", jobs[i].Settings.ContinuousConstructionTime ? 1. : 0.}};
                            });
                            try
                            {
                                jobs[i].Settings.TimeLimitSeconds = remaining;
                                jobs[i].Settings.MaxPlacementAttempts = caps[i];
                                jobs[i].Settings.PublishConstructionCandidate = [&, i](const Result& output)
                                {
                                    const auto publicationStarted = Clock::now();
                                    try
                                    {
                                        // Snapshot assessment and ranking use the unchanged original
                                        // problem and run outside both observation and transport locks.
                                        JobResult candidate; candidate.ID = jobs[i].ID; candidate.StrategyID = IDs[i];
                                        candidate.Output = output;
                                        Detail::AssessCandidate(input, candidate);
                                        if (!Detail::Eligible(candidate) || !candidate.Checked.Complete ||
                                            candidate.Output.Stats.PlacementAttempts > caps[i])
                                        {
                                            observe(ConstructionTraceStage::Filter, i, "scheduler.published-candidate", [&](auto& event)
                                            {
                                                TraceResult(event, candidate); event.Accepted = false;
                                                event.Reason = "snapshot-not-complete-valid-within-route-lease";
                                                event.Measures.emplace_back("snapshotAssessmentSeconds",
                                                    std::chrono::duration<double>(Clock::now() - publicationStarted).count());
                                                event.Measures.emplace_back("snapshotValidatedCount", 0.);
                                                for (const auto& error : candidate.Checked.Errors) event.Reason += "; validation=" + error;
                                            });
                                            return;
                                        }
                                        if (candidate.Output.State == Status::Cancelled) candidate.Output.State = Status::Complete;
                                        auto score = Detail::Quality(candidate, input, options.Ranking);
                                        const auto assessmentSeconds = std::chrono::duration<double>(Clock::now() - publicationStarted).count();
                                        validatedPublications.fetch_add(1, std::memory_order_relaxed);
                                        {
                                            std::lock_guard lock(publicationMutex);
                                            if (publishedBestScore && !(score < *publishedBestScore)) return;
                                            publishedBestScore = score;
                                            pendingPublication = PublishedCandidate{i, std::move(candidate), std::move(score),
                                                assessmentSeconds, publicationStarted};
                                            publicationReady.store(true, std::memory_order_release);
                                        }
                                        wake.notify_one();
                                    }
                                    catch (...) { publicationFailed.store(true, std::memory_order_release); wake.notify_one(); }
                                };
                                routeStarted[i] = 1;
                                observe(ConstructionTraceStage::Begin, i, "scheduler.route", [&](auto& event)
                                { event.Reason = "constructor-started"; event.Measures.emplace_back("worker", static_cast<double>(workerIndex)); });
                                bool failedAfterStart = false;
                                results[i] = Execute(jobs[i], std::move(strategies[i]), {}, &failedAfterStart);
                                chargeCap[i] = failedAfterStart;
                            }
                            catch (...) { exceptions[i] = std::current_exception(); chargeCap[i] = 1; }
                            const auto spent = chargeCap[i] || results[i].Output.Stats.PlacementAttempts > caps[i]
                                ? caps[i] : results[i].Output.Stats.PlacementAttempts;
                            observe(ConstructionTraceStage::Complete, i, "scheduler.worker-finished", [&](auto& event)
                            {
                                TraceResult(event, results[i]);
                                event.Accepted = Detail::Eligible(results[i]);
                                event.Reason = exceptions[i] ? "worker-exception" : "execute-returned-after-validation-and-measurement";
                                event.Measures.emplace_back("worker", static_cast<double>(workerIndex));
                            });
                            lease.Return(spent);
                            observe(ConstructionTraceStage::Budget, i, "scheduler.return-lease", [&](auto& event)
                            {
                                event.Reason = chargeCap[i] ? "exception-cap-charged" :
                                    results[i].Output.Stats.PlacementAttempts > caps[i] ? "over-reporting-cap-charged" : "actual-attempts-charged";
                                event.Attempts = spent;
                                event.Measures = {{"attemptCap", static_cast<double>(caps[i])},
                                    {"chargedAttempts", static_cast<double>(spent)},
                                    {"returnedAttempts", Settings.FairScheduling ? static_cast<double>(caps[i]-spent) : 0.}};
                            });
                            done[i].store(true, std::memory_order_release);
                            finished.fetch_add(1, std::memory_order_release); wake.notify_one();
                        }
                    }
                    catch (...) { infrastructureFailure.store(true, std::memory_order_release); }
                    active.fetch_sub(1, std::memory_order_release); wake.notify_one();
                };
                std::vector<std::jthread> workers;
                struct JoinWorkers
                {
                    std::atomic<bool>& Stop; std::atomic<bool>& Start;
                    std::condition_variable& Wake; std::vector<std::jthread>& Workers;
                    void Join()
                    {
                        Stop.store(true, std::memory_order_release); Start.store(true, std::memory_order_release); Wake.notify_all();
                        for (auto& thread : Workers) if (thread.joinable()) thread.join();
                    }
                    ~JoinWorkers() { Join(); }
                } join{stop, startWorkers, wake, workers};
                const auto automatic = std::max<std::size_t>(1, std::thread::hardware_concurrency());
                const auto requested = std::min(scheduled.size(), Settings.Workers ? Settings.Workers : automatic);
                if (!pollControl()) for (std::size_t i = 0; i < requested; ++i)
                {
                    active.fetch_add(1, std::memory_order_relaxed);
                    try { workers.emplace_back(worker, i); }
                    catch (const std::exception& error)
                    { active.fetch_sub(1, std::memory_order_relaxed); notes.push_back(std::string("parallel.construction worker creation failed: ") + error.what()); break; }
                    catch (...) { active.fetch_sub(1, std::memory_order_relaxed); notes.push_back("parallel.construction worker creation threw a non-standard exception."); break; }
                }
                effectiveWorkers = std::max<std::size_t>(1, workers.size());
                observe(ConstructionTraceStage::Dispatch, allRoutes, "scheduler.workers", [&](auto& event)
                { event.Reason = "worker-pool-released"; event.Measures = {{"workersActual", static_cast<double>(workers.size())},
                    {"scheduled", static_cast<double>(scheduled.size())}, {"prepared", static_cast<double>(prepared)}}; });
                startWorkers.store(true, std::memory_order_release); wake.notify_all();
                JobResult best; std::optional<Detail::QualityKey> bestScore;
                Statistics work; work.PostProcessingSeconds = 0;
                std::size_t completed = 0, full = 0;
                double firstCompleteSeconds = -1, lastImprovementSeconds = -1;
                bool publishEnabled = bool(Settings.OnImprovement), proofEnabled = bool(Settings.StopWhen);
                bool relayEnabled = bool(options.PublishConstructionCandidate);
                const auto add = [](std::size_t a, std::size_t b)
                { return a + std::min(b, std::numeric_limits<std::size_t>::max() - a); };
                const auto consider = [&](std::size_t i, JobResult candidate,
                    const Detail::QualityKey* assessedScore, bool streamed)
                {
                    try
                    {
                        const auto score = assessedScore ? *assessedScore : Detail::Quality(candidate, input, options.Ranking);
                        observe(ConstructionTraceStage::Score, i, "scheduler.quality", [&](auto& event)
                        { TraceComparison(event, score, bestScore, bestScore ? best.StrategyID : std::string{}, options.Ranking);
                            event.Measures.emplace_back("streamedSnapshot", streamed ? 1. : 0.); });
                        if (bestScore && !(score < *bestScore)) return;
                        bestScore = score; best = std::move(candidate);
                        lastImprovementSeconds = elapsed();
                        observe(ConstructionTraceStage::Selected, i, "scheduler.incumbent", [&](auto& event)
                        { TraceResult(event, best); TraceQuality(event, score, options.Ranking);
                            event.Reason = streamed ? "new-streamed-incumbent" : "new-incumbent"; });
                        if (best.Checked.Complete && firstCompleteSeconds < 0) firstCompleteSeconds = lastImprovementSeconds;
                        if (!best.Checked.Complete) return;
                        if (!cancelled) best.Output.State = Status::Complete;
                        // Forward an enclosing portfolio's transport separately
                        // from user observation. Both run on this Run caller,
                        // outside the worker transport and observation locks.
                        if (relayEnabled && !cancelled && !cancelFailed)
                        {
                            try { options.PublishConstructionCandidate(best.Output); }
                            catch (const std::exception& e) { relayEnabled = false; notes.push_back(std::string("parallel.construction candidate publisher failed: ") + e.what()); }
                            catch (...) { relayEnabled = false; notes.push_back("parallel.construction candidate publisher threw a non-standard exception."); }
                            pollControl();
                        }
                        // Late validated improvements remain selectable after
                        // cancellation, but do not call subscribers or StopWhen.
                        if (publishEnabled && !cancelled && !cancelFailed)
                        {
                            try { Settings.OnImprovement(best); }
                            catch (const std::exception& e) { publishEnabled = false; notes.push_back(std::string("parallel.construction improvement callback failed: ") + e.what()); }
                            catch (...) { publishEnabled = false; notes.push_back("parallel.construction improvement callback threw a non-standard exception."); }
                            pollControl();
                        }
                        if (proofEnabled && !targetReached && !cancelled && !cancelFailed)
                        {
                            try
                            {
                                targetReached = Settings.StopWhen(input, best);
                                observe(ConstructionTraceStage::Analysis, i, "scheduler.target-predicate", [&](auto& event)
                                { event.Accepted = targetReached; event.Reason = targetReached ? "explicit-target-predicate-returned-true" : "explicit-target-predicate-returned-false"; });
                                if (targetReached) stop.store(true, std::memory_order_release);
                            }
                            catch (const std::exception& e) { proofEnabled = false; notes.push_back(std::string("parallel.construction target predicate failed: ") + e.what()); }
                            catch (...) { proofEnabled = false; notes.push_back("parallel.construction target predicate threw a non-standard exception."); }
                        }
                    }
                    catch (const std::exception& e) { notes.push_back(std::string("parallel.construction candidate ranking failed: ") + e.what()); }
                    catch (...) { notes.push_back("parallel.construction candidate ranking threw a non-standard exception."); }
                };
                const auto collect = [&]
                {
                    if (publicationReady.load(std::memory_order_acquire))
                    {
                        std::optional<PublishedCandidate> publication;
                        {
                            std::lock_guard lock(publicationMutex);
                            publication = std::move(pendingPublication); pendingPublication.reset();
                            publicationReady.store(false, std::memory_order_release);
                        }
                        if (publication)
                        {
                            const auto i = publication->Route;
                            observe(ConstructionTraceStage::Selected, i, "scheduler.published-candidate", [&](auto& event)
                            {
                                TraceResult(event, publication->Value); event.Accepted = true;
                                event.Reason = "complete-original-validated-snapshot; route-still-independent";
                                event.Measures.emplace_back("streamedSnapshot", 1.);
                                event.Measures.emplace_back("countsAsCompletedRoute", 0.);
                                event.Measures.emplace_back("attemptsCharged", 0.);
                                event.Measures.emplace_back("snapshotAssessmentSeconds", publication->AssessmentSeconds);
                                event.Measures.emplace_back("snapshotPublishToParentSeconds",
                                    std::chrono::duration<double>(Clock::now() - publication->PublishedAt).count());
                                event.Measures.emplace_back("snapshotValidatedCount", 1.);
                            });
                            consider(i, std::move(publication->Value), &publication->Score, true);
                        }
                    }
                    for (const auto i : scheduled)
                    {
                        if (collected[i] || !done[i].load(std::memory_order_acquire)) continue;
                        collected[i] = 1; ++completed;
                        auto candidate = std::move(results[i]);
                        if (exceptions[i])
                        {
                            candidate.ID = jobs[i].ID; candidate.StrategyID = IDs[i]; candidate.Output.State = Status::Failed;
                            AddRemaining(candidate.Output, input);
                            try { std::rethrow_exception(exceptions[i]); }
                            catch (const std::exception& e) { candidate.Output.Diagnostics.push_back(e.what()); }
                            catch (...) { candidate.Output.Diagnostics.push_back("Worker threw a non-standard exception."); }
                        }
                        if (chargeCap[i] || candidate.Output.Stats.PlacementAttempts > caps[i])
                        {
                            if (!chargeCap[i]) candidate.Output.State = Status::Failed;
                            candidate.Output.Stats.PlacementAttempts = caps[i];
                            candidate.Output.Diagnostics.push_back("Failed or over-reporting constructor charged its allocated attempt cap conservatively.");
                        }
                        // A worker sees only the shared internal stop flag.
                        // Its Cancelled state does not identify the stop reason;
                        // select its certified geometry normally, then apply the
                        // original caller cancellation to the final result only.
                        if (candidate.Checked.Valid && candidate.Output.State == Status::Cancelled)
                            candidate.Output.State = candidate.Checked.Complete ? Status::Complete : Status::Partial;
                        work.PlacementAttempts = add(work.PlacementAttempts, candidate.Output.Stats.PlacementAttempts);
                        work.CandidatePatterns = add(work.CandidatePatterns, candidate.Output.Stats.CandidatePatterns);
                        const bool eligible = Detail::Eligible(candidate); full += eligible && candidate.Checked.Complete;
                        observe(ConstructionTraceStage::Complete, i, "scheduler.route-result", [&](auto& event)
                        {
                            TraceResult(event, candidate);
                            event.Accepted = eligible;
                            event.Reason = eligible ? "constructor-returned-valid-assessed-result" : "constructor-result-ineligible";
                            for (const auto& error : candidate.Checked.Errors) event.Reason += "; validation=" + error;
                            event.Measures.emplace_back("localTimeSeconds", jobs[i].Settings.TimeLimitSeconds);
                            event.Measures.emplace_back("attemptCap", static_cast<double>(caps[i]));
                        });
                        std::ostringstream note;
                        note << "parallel.construction job=" << IDs[i] << " placed=" << candidate.Checked.PlacedCount
                            << " complete=" << candidate.Checked.Complete << " valid=" << candidate.Checked.Valid
                            << " attempts=" << candidate.Output.Stats.PlacementAttempts << " seconds=" << candidate.Output.Stats.ElapsedSeconds;
                        notes.push_back(note.str());
                        std::ostringstream allocation;
                        allocation << "parallel.construction allocation job=" << IDs[i]
                            << " local-time-s=" << jobs[i].Settings.TimeLimitSeconds
                            << " attempt-cap=" << caps[i] << " fair-scheduling=" << Settings.FairScheduling
                            << " continuous-time=" << jobs[i].Settings.ContinuousConstructionTime;
                        notes.push_back(allocation.str());
                        for (const auto& diagnostic : candidate.Output.Diagnostics) notes.push_back("parallel.construction job=" + IDs[i] + " | " + diagnostic);
                        if (!eligible)
                        {
                            observe(ConstructionTraceStage::Filter, i, "scheduler.selection", [&](auto& event)
                            { event.Accepted = false; event.ResultStatus = candidate.Output.State; event.Reason = "invalid-or-nonselectable-result-state"; });
                            continue;
                        }
                        consider(i, std::move(candidate), nullptr, false);
                    }
                };
                while (active.load(std::memory_order_acquire))
                {
                    pollControl(); collect();
                    std::unique_lock lock(wakeMutex);
                    wake.wait_for(lock, std::chrono::milliseconds(2), [&]
                    { return publicationReady.load(std::memory_order_acquire) ||
                        finished.load(std::memory_order_acquire) > completed || !active.load(std::memory_order_acquire); });
                }
                pollControl(); collect(); pollControl(); join.Join();
                if (infrastructureFailure.load(std::memory_order_acquire)) notes.push_back("parallel.construction worker infrastructure failed; other route results retained.");
                if (publicationFailed.load(std::memory_order_acquire)) notes.push_back("parallel.construction candidate publication failed; independently assessed route results retained.");
                const auto startedCount = static_cast<std::size_t>(std::count(routeStarted.begin(), routeStarted.end(), 1));
                for (std::size_t i = 0; i < IDs.size(); ++i) if (!factoryVisited[i])
                    observe(ConstructionTraceStage::Stopped, i, "scheduler.not-started", [&](auto& event)
                    { event.Accepted = false; event.Reason = "global-stop-before-factory-preparation"; });
                for (const auto i : scheduled) if (!collected[i])
                {
                    const std::string reason = !notStartedReasons[i].empty() ? notStartedReasons[i] :
                        cancelled ? "original user cancellation" : cancelFailed ? "original cancellation callback failed" :
                        targetReached ? "successful target stop" : deadline ? "common wall deadline" :
                        workers.empty() ? "no worker available" : "worker infrastructure stopped";
                    notes.push_back("parallel.construction job=" + IDs[i] +
                        (routeStarted[i] ? " started without completed result; reason=" : " not started before shared stop; reason=") + reason);
                    observe(ConstructionTraceStage::Stopped, i, "scheduler.not-started", [&](auto& event)
                    { event.Accepted = false; event.Reason = reason; event.Measures.emplace_back("actuallyStarted", routeStarted[i] ? 1. : 0.); });
                }
                if (Settings.FairScheduling)
                {
                    std::lock_guard lock(pool.Mutex);
                    work.PlacementAttempts = pool.Spent;
                }
                auto result = bestScore ? std::move(best.Output) : std::move(empty);
                if (bestScore && result.State == Status::Cancelled) result.State = best.Checked.Complete ? Status::Complete : Status::Partial;
                if (cancelled) result.State = Status::Cancelled;
                work.CombinationGroups = result.Stats.CombinationGroups;
                work.CircleLatticeGroups = result.Stats.CircleLatticeGroups; work.GapFilledParts = result.Stats.GapFilledParts;
                // Parallel route stage times are not additive elapsed times.
                // Each route retains its own measurements in the diagnostics.
                work.ElapsedSeconds = elapsed(); result.Stats = work;
                observe(ConstructionTraceStage::Selected, allRoutes, "scheduler.final-selection", [&](auto& event)
                {
                    event.Route = bestScore ? best.StrategyID : std::string{};
                    event.Accepted = bool(bestScore); event.ResultStatus = result.State;
                    event.Layouts = result.Layouts; event.Remaining = result.Remaining;
                    event.Attempts = work.PlacementAttempts;
                    event.Reason = cancelled ? "original-user-cancellation" : cancelFailed ? "original-cancel-callback-failed" :
                        targetReached ? "explicit-target-predicate" : deadline ? "common-wall-deadline" :
                        infrastructureFailure.load(std::memory_order_acquire) ? "worker-infrastructure-stopped" :
                        workers.empty() ? "no-worker-available" : work.PlacementAttempts >= options.MaxPlacementAttempts ?
                        "global-attempt-budget-exhausted" : "scheduled-constructors-finished";
                    event.Measures = {{"prepared", static_cast<double>(prepared)}, {"scheduled", static_cast<double>(scheduled.size())},
                        {"started", static_cast<double>(startedCount)}, {"completed", static_cast<double>(completed)},
                        {"completeResults", static_cast<double>(full)}, {"workersActual", static_cast<double>(workers.size())},
                        {"snapshotValidatedCount", static_cast<double>(validatedPublications.load(std::memory_order_relaxed))},
                        {"firstCompleteReceivedSeconds", firstCompleteSeconds}, {"lastImprovementReceivedSeconds", lastImprovementSeconds},
                        {"globalAttemptsRemaining", static_cast<double>(options.MaxPlacementAttempts-std::min(work.PlacementAttempts,options.MaxPlacementAttempts))}};
                    if (bestScore) TraceQuality(event, *bestScore, options.Ranking);
                });
                if (trace.HasFailed()) notes.push_back("parallel.construction observation callback failed; subsequent trace events omitted; solver results retained.");
                result.Diagnostics.insert(result.Diagnostics.end(), notes.begin(), notes.end());
                result.Diagnostics.push_back("parallel.construction portfolio jobs=" + std::to_string(completed) + "/" + std::to_string(IDs.size()) +
                    " workers=" + std::to_string(workers.size()) + " complete=" + std::to_string(full) +
                    " selected=" + (bestScore ? best.StrategyID : std::string("none")) +
                    " eligible=" + std::to_string(prepared) + " scheduled=" + std::to_string(scheduled.size()) +
                    " completed=" + std::to_string(completed) + " started=" + std::to_string(startedCount) +
                    " fair-scheduling=" + std::to_string(Settings.FairScheduling) +
                    " target-stop=" + std::to_string(targetReached) + " deadline=" + std::to_string(deadline) +
                    " first-complete-wall-s=" + std::to_string(firstCompleteSeconds) +
                    " last-improvement-wall-s=" + std::to_string(lastImprovementSeconds) +
                    (Settings.FairScheduling
                        ? "; shared wall deadline, fair time slices or declared continuous construction time, reclaimed attempt leases; original caller-thread cancellation; all workers joined"
                        : "; shared wall deadline and fixed global attempt shares; original caller-thread cancellation; all workers joined"));
                return result;
            }
        private:
            StrategyRegistry Registry;
            std::vector<std::string> IDs;
            ParallelConstructionSettings Settings;
        };
    }
    JobResult RunJob(const Job& job, const StrategyRegistry& registry)
    {
        try { return Execute(job, registry.Create(job.StrategyID), {}); }
        catch (const std::exception& e) { return Execute(job, nullptr, e.what()); }
        catch (...) { return Execute(job, nullptr, "Strategy factory failed."); }
    }
    PortfolioResult RunPortfolio(const Problem& input, const PortfolioOptions& options, const StrategyRegistry& registry)
    {
        const auto ids = options.StrategyIDs.empty() ? registry.IDs() : options.StrategyIDs;
        const auto started = std::chrono::steady_clock::now();
        SerializedConstructionTrace trace(options.PerJob, started);
        auto observed = options.PerJob;
        if (options.PerJob.ConstructionTrace)
            observed.ConstructionTrace = [&trace](const ConstructionTraceEvent& event) { trace.Record(event); };
        const auto observe = [&](ConstructionTraceStage stage, std::size_t i, const char* source, auto&& build)
        {
            Detail::ObserveConstruction(observed, stage, [&](ConstructionTraceEvent& event)
            {
                if (i < ids.size()) event.Route = ids[i];
                event.Source = source;
                event.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                build(event);
            });
        };
        PortfolioResult portfolio;
        portfolio.Jobs.resize(ids.size());
        std::vector<Job> jobs;
        std::vector<std::unique_ptr<IStrategy>> strategies;
        std::vector<std::string> factoryErrors(ids.size());
        // Factories run serially; even factories with a mutable counter are not called concurrently.
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            observe(ConstructionTraceStage::Dispatch, i, "scheduler.portfolio-register", [&](auto& event)
            { event.Reason = "independent-per-job-budget"; event.Measures.emplace_back("registrationIndex", static_cast<double>(i)); });
            jobs.push_back({ std::to_string(i) + ":" + ids[i], ids[i], input, observed });
            jobs.back().Settings.Seed += i;
            jobs.back().Settings.Ranking = options.Ranking;
            jobs.back().Settings.ConstructionTraceRoute = ids[i];
            try { strategies.push_back(registry.Create(ids[i])); }
            catch (const std::exception& e) { strategies.push_back(nullptr); factoryErrors[i] = e.what(); }
            catch (...) { strategies.push_back(nullptr); factoryErrors[i] = "Strategy factory failed."; }
            observe(strategies.back() ? ConstructionTraceStage::Dispatch : ConstructionTraceStage::Filter,
                i, "scheduler.portfolio-factory", [&](auto& event)
            { event.Accepted = bool(strategies.back()); event.Reason = factoryErrors[i].empty() ?
                strategies.back() ? "strategy-prepared" : "strategy-factory-returned-null" : factoryErrors[i]; });
        }
        std::atomic<std::size_t> next{ 0 };
        const auto worker = [&](std::size_t workerIndex)
        {
            for (;;)
            {
                const auto i = next.fetch_add(1);
                if (i >= jobs.size()) return;
                observe(ConstructionTraceStage::Begin, i, "scheduler.portfolio-route", [&](auto& event)
                { event.Reason = "job-started"; event.Measures = {{"worker", static_cast<double>(workerIndex)},
                    {"localTimeSeconds", jobs[i].Settings.TimeLimitSeconds}, {"attemptCap", static_cast<double>(jobs[i].Settings.MaxPlacementAttempts)},
                    {"automaticBasicCompaction", jobs[i].Settings.EnableBasicCompaction ? 1. : 0.}}; });
                portfolio.Jobs[i] = Execute(jobs[i], std::move(strategies[i]), factoryErrors[i]);
                observe(ConstructionTraceStage::Complete, i, "scheduler.portfolio-route-result", [&](auto& event)
                { TraceResult(event, portfolio.Jobs[i]); event.Accepted = Detail::Eligible(portfolio.Jobs[i]);
                    event.Reason = "job-returned-after-validation-and-measurement"; });
            }
        };
        const auto count = std::min<std::size_t>(ids.size(), std::clamp<std::size_t>(options.Workers, 1, 32));
        std::vector<std::jthread> workers;
        for (std::size_t i = 1; i < count; ++i) workers.emplace_back(worker, i);
        worker(0);
        for (auto& thread : workers) thread.join();
        std::optional<Detail::QualityKey> bestScore;
        for (std::size_t i = 0; i < portfolio.Jobs.size(); ++i)
            if (Detail::Eligible(portfolio.Jobs[i]))
            {
                const auto score = Detail::Quality(portfolio.Jobs[i], input, options.Ranking);
                observe(ConstructionTraceStage::Score, i, "scheduler.portfolio-quality", [&](auto& event)
                { TraceComparison(event, score, bestScore, portfolio.BestJob < ids.size() ? ids[portfolio.BestJob] : std::string{}, options.Ranking); });
                if (!bestScore || score < *bestScore) { bestScore = score; portfolio.BestJob = i; }
            }
        observe(ConstructionTraceStage::Selected, portfolio.BestJob, "scheduler.portfolio-final-selection", [&](auto& event)
        {
            event.Accepted = portfolio.BestJob < portfolio.Jobs.size();
            event.Reason = "all-portfolio-jobs-joined";
            if (portfolio.BestJob < portfolio.Jobs.size()) TraceResult(event, portfolio.Jobs[portfolio.BestJob]);
            if (bestScore) TraceQuality(event, *bestScore, options.Ranking);
        });
        if (trace.HasFailed() && portfolio.BestJob < portfolio.Jobs.size())
            portfolio.Jobs[portfolio.BestJob].Output.Diagnostics.push_back("portfolio observation callback failed; subsequent trace events omitted; solver results retained.");
        return portfolio;
    }
    PortfolioResult RunPortfolio(const Problem& input, const PortfolioOptions& options)
    { return RunPortfolio(input, options, DefaultStrategies()); }

    StrategyFactory MakeConstructionPortfolioFactory(const StrategyRegistry& registry,
        std::vector<std::string> strategyIDs)
    {
        if (strategyIDs.empty()) strategyIDs = registry.IDs();
        if (strategyIDs.empty()) throw std::invalid_argument("Construction portfolio needs at least one constructor.");
        return [registry, ids = std::move(strategyIDs)]
        { return std::make_unique<ConstructionPortfolio>(registry, ids); };
    }

    StrategyFactory MakeParallelConstructionPortfolioFactory(const StrategyRegistry& registry,
        std::vector<std::string> strategyIDs, ParallelConstructionSettings settings)
    {
        if (settings.Workers > 32) throw std::invalid_argument("Parallel construction supports at most 32 workers.");
        if (strategyIDs.empty()) strategyIDs = registry.IDs();
        if (strategyIDs.empty()) throw std::invalid_argument("Parallel construction needs at least one constructor.");
        return [registry, ids = std::move(strategyIDs), settings = std::move(settings)]
        { return std::make_unique<ParallelConstructionPortfolio>(registry, ids, settings); };
    }
}
