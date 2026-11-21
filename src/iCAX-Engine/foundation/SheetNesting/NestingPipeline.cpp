#include "SheetNestingPipeline.h"
#include "SheetNestingAxisCompaction.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>

namespace iCAX::SheetNesting
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        double Seconds(Clock::time_point start)
        { return std::chrono::duration<double>(Clock::now() - start).count(); }

        class PipelineBudget
        {
        public:
            explicit PipelineBudget(const Options& settings) : Settings(settings)
            {
                if (!settings.EnableBasicCompaction) return;
                const auto fraction = settings.BasicCompactionBudgetFraction;
                if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
                    throw std::invalid_argument("Invalid basic-compaction budget fraction.");
                BasicSeconds = settings.TimeLimitSeconds * fraction;
            }
            bool PollCancel()
            {
                if (!Cancelled && Settings.Cancel && Settings.Cancel()) Cancelled = true;
                return Cancelled;
            }
            bool Stop()
            { return PollCancel() || WorkSeconds() >= Settings.TimeLimitSeconds || Attempts >= Settings.MaxPlacementAttempts; }
            double ElapsedSeconds() const { return Seconds(Started); }
            double WorkSeconds() const { return ElapsedSeconds(); }
            void Observe(const Result& result)
            {
                if (result.State == Status::Cancelled) Cancelled = true;
                const auto count = result.Stats.PlacementAttempts;
                Attempts += std::min(count, std::numeric_limits<std::size_t>::max() - Attempts);
            }
            Options Remaining()
            {
                auto settings = Settings;
                settings.TimeLimitSeconds = std::max(1e-9, Settings.TimeLimitSeconds - WorkSeconds());
                settings.MaxPlacementAttempts = Settings.MaxPlacementAttempts - std::min(Attempts, Settings.MaxPlacementAttempts);
                settings.Cancel = [this] { return PollCancel(); };
                return settings;
            }
            bool SearchStop()
            {
                if (Stop()) return true;
                const auto left = Remaining();
                return left.TimeLimitSeconds <= BasicSecondsLeft();
            }
            Options RemainingSearch()
            {
                auto settings = Remaining();
                settings.TimeLimitSeconds = std::max(1e-9, settings.TimeLimitSeconds - BasicSecondsLeft());
                return settings;
            }
            Options RemainingCompaction(std::size_t slots)
            {
                auto settings = Remaining();
                settings.TimeLimitSeconds = std::min(settings.TimeLimitSeconds,
                    BasicSecondsLeft() / double(std::max<std::size_t>(1, slots)));
                const auto attempts = settings.MaxPlacementAttempts;
                settings.MaxPlacementAttempts = std::min(settings.MaxPlacementAttempts,
                    attempts ? std::max<std::size_t>(1, attempts / std::max<std::size_t>(1, slots)) : 0);
                return settings;
            }
            void ObserveCompaction(const Result& result, double seconds)
            {
                Observe(result);
                BasicWorkSeconds += seconds;
            }
            void Assess(const Problem& input, JobResult& candidate)
            { Detail::AssessCandidate(input, candidate); }
            bool Cancelled = false;
        private:
            const Options& Settings;
            Clock::time_point Started = Clock::now();
            std::size_t Attempts = 0;
            double BasicSeconds = 0, BasicWorkSeconds = 0;
            double BasicSecondsLeft() const { return std::max(0., BasicSeconds - BasicWorkSeconds); }
        };

        void AddWork(Statistics& total, const Statistics& stage)
        {
            const auto add = [](std::size_t a, std::size_t b)
            { return a + std::min(b, std::numeric_limits<std::size_t>::max() - a); };
            total.PlacementAttempts = add(total.PlacementAttempts, stage.PlacementAttempts);
            total.CandidatePatterns = add(total.CandidatePatterns, stage.CandidatePatterns);
            if (stage.VerificationSeconds >= 0)
                total.VerificationSeconds = std::max(0.0, total.VerificationSeconds) + stage.VerificationSeconds;
            if (stage.EvaluationSeconds >= 0)
                total.EvaluationSeconds = std::max(0.0, total.EvaluationSeconds) + stage.EvaluationSeconds;
        }

        bool IsBoundedPrecisionRepair(const Result& before,const Result& after,double maxShift)
        {
            if(!(maxShift>0)||!std::isfinite(maxShift)||before.State!=after.State||before.Layouts.size()!=after.Layouts.size()||before.Remaining.size()!=after.Remaining.size())return false;
            std::map<std::string,std::size_t> remaining;
            for(const auto& x:before.Remaining)remaining[x.PartID]+=x.Quantity;
            for(const auto& x:after.Remaining){auto it=remaining.find(x.PartID);if(it==remaining.end()||it->second<x.Quantity)return false;it->second-=x.Quantity;}
            for(const auto& [id,count]:remaining)if(count)return false;
            bool moved=false;
            for(std::size_t i=0;i<before.Layouts.size();++i)
            {
                const auto& a=before.Layouts[i];const auto& b=after.Layouts[i];
                if(a.StockID!=b.StockID||a.StockInstance!=b.StockInstance||a.Width!=b.Width||a.Height!=b.Height||a.MaterialArea!=b.MaterialArea||a.Placements.size()!=b.Placements.size())return false;
                std::map<std::pair<std::string,std::size_t>,const Placement*> byKey;
                for(const auto& x:a.Placements)if(!byKey.emplace(std::make_pair(x.PartID,x.Instance),&x).second)return false;
                for(const auto& y:b.Placements)
                {
                    const auto it=byKey.find({y.PartID,y.Instance});if(it==byKey.end())return false;
                    const auto& x=*it->second;byKey.erase(it);
                    if(x.Transform.AngleDegrees!=y.Transform.AngleDegrees||x.Group!=y.Group||x.GapFiller!=y.GapFiller)return false;
                    const auto dx=std::abs(x.Transform.X-y.Transform.X),dy=std::abs(x.Transform.Y-y.Transform.Y);
                    if(!std::isfinite(dx)||!std::isfinite(dy)||dx>maxShift+1e-12||dy>maxShift+1e-12)return false;
                    moved|=dx>0||dy>0;
                }
            }
            return moved;
        }

        std::optional<Result> PrecedingSheetMoves(const JobResult& source, const Result& compacted)
        {
            using BoardIdentity = std::pair<std::string, std::size_t>;
            std::set<BoardIdentity> tails;
            for (const auto index : source.Remnants.FinalLayoutIndices)
                if (index < source.Output.Layouts.size())
                {
                    const auto& board = source.Output.Layouts[index];
                    tails.emplace(board.StockID, board.StockInstance);
                }
            if (tails.empty() || tails.size() == source.Output.Layouts.size()) return {};
            std::map<BoardIdentity, const Layout*> proposals;
            for (const auto& board : compacted.Layouts)
                proposals.emplace(BoardIdentity{board.StockID, board.StockInstance}, &board);
            auto retained = source.Output;
            bool changed = false;
            for (auto& board : retained.Layouts)
            {
                const BoardIdentity identity{board.StockID, board.StockInstance};
                const auto proposed = proposals.find(identity);
                if (tails.contains(identity) || proposed == proposals.end()) continue;
                const auto& placements = proposed->second->Placements;
                const bool same = board.Placements.size() == placements.size() &&
                    std::equal(board.Placements.begin(), board.Placements.end(), placements.begin(),
                        [](const Placement& a, const Placement& b)
                        {
                            return std::tie(a.PartID,a.Instance,a.Transform.X,a.Transform.Y,a.Transform.AngleDegrees,a.Group,a.GapFiller) ==
                                std::tie(b.PartID,b.Instance,b.Transform.X,b.Transform.Y,b.Transform.AngleDegrees,b.Group,b.GapFiller);
                        });
                if (!same) { board = *proposed->second; changed = true; }
            }
            if (!changed) return {};
            retained.Stats = {};
            retained.Stats.CombinationGroups = source.Output.Stats.CombinationGroups;
            retained.Stats.CircleLatticeGroups = source.Output.Stats.CircleLatticeGroups;
            retained.Stats.GapFilledParts = source.Output.Stats.GapFilledParts;
            retained.GeometryValidated = false;
            return retained;
        }

        class PipelineStrategy final : public IStrategy
        {
        public:
            explicit PipelineStrategy(std::unique_ptr<IStrategy> constructor) : Construct(std::move(constructor))
            {
                if (!Construct) throw std::invalid_argument("Refinement executor needs a constructor.");
            }
            explicit PipelineStrategy(const StrategyPipeline& definition)
                : RequireCompleteConstruction(definition.RequireCompleteConstruction),
                  ConstructionBudgetFraction(definition.ConstructionBudgetFraction)
            {
                Construct = definition.Construct();
                if (!Construct) throw std::runtime_error("Pipeline constructor factory returned null.");
                if (definition.Order.Factory)
                {
                    OrderID = definition.Order.ID;
                    Order = definition.Order.Factory();
                    if (!Order) throw std::runtime_error("Pipeline order factory returned null: " + OrderID);
                }
                for (const auto& stage : definition.PostProcessors)
                {
                    auto processor = stage.Factory();
                    if (!processor) throw std::runtime_error("Pipeline postprocessor factory returned null: " + stage.ID);
                    Processors.push_back({stage.ID, std::move(processor), stage.MaxPrecisionAxisShift, stage.BudgetFraction});
                }
            }

            bool OwnsRefinementLifecycle() const noexcept override { return true; }

            Result Run(const Problem& input, const Options& options) override
            {
                PipelineBudget budget(options);
                std::vector<std::string> notes;
                const auto empty = [&](std::string reason = {})
                {
                    Result result;
                    result.State = budget.Cancelled ? Status::Cancelled : Status::Partial;
                    for (const auto& part : input.Parts)
                        if (part.Quantity) result.Remaining.push_back({ part.ID, part.Quantity });
                    result.Stats.ElapsedSeconds = budget.ElapsedSeconds();
                    result.Diagnostics = notes;
                    if (reason.empty()) reason = budget.Cancelled ?
                        "pipeline: cancelled before construction; original demand retained" :
                        "pipeline: shared search budget exhausted before construction; original demand retained";
                    result.Diagnostics.push_back(std::move(reason));
                    return result;
                };
                if (budget.SearchStop()) return empty();
                std::optional<Problem> ordered;
                double orderSeconds = 0;
                if (Order)
                {
                    const auto start = Clock::now();
                    const auto indices = Order->Order(input, budget.RemainingSearch());
                    if (budget.SearchStop()) return empty();
                    if (indices.size() != input.Parts.size())
                        throw std::runtime_error("Part order must be a complete permutation: " + OrderID);
                    std::vector<bool> seen(indices.size(), false);
                    for (std::size_t i = 0; i < indices.size(); ++i)
                    {
                        if ((i & 1023) == 0 && budget.SearchStop()) return empty();
                        if (indices[i] >= seen.size() || seen[indices[i]])
                            throw std::runtime_error("Part order contains an invalid or repeated index: " + OrderID);
                        seen[indices[i]] = true;
                    }
                    ordered.emplace(input);
                    for (std::size_t i = 0; i < indices.size(); ++i) ordered->Parts[i] = input.Parts[indices[i]];
                    orderSeconds = Seconds(start);
                    notes.push_back("pipeline.order=" + OrderID);
                }
                if (budget.SearchStop()) return empty();
                JobResult incumbent;
                auto constructionOptions = budget.RemainingSearch();
                if (ConstructionBudgetFraction < 1)
                {
                    constructionOptions.TimeLimitSeconds *= ConstructionBudgetFraction;
                    const double attempts = static_cast<double>(constructionOptions.MaxPlacementAttempts) * ConstructionBudgetFraction;
                    if (attempts < static_cast<double>(constructionOptions.MaxPlacementAttempts))
                        constructionOptions.MaxPlacementAttempts = static_cast<std::size_t>(attempts);
                }
                if (constructionOptions.TimeLimitSeconds <= 0 || !constructionOptions.MaxPlacementAttempts)
                    return empty("pipeline.construction reserved share has no usable search budget, original demand retained");
                // The outer executor owns one pool, including nested composed
                // constructors. A child must not reserve another basic pool.
                constructionOptions.EnableBasicCompaction = false;
                incumbent.Output = Construct->Run(ordered ? *ordered : input, constructionOptions);
                budget.Observe(incumbent.Output);
                if (Order && incumbent.Output.Stats.PreparationSeconds >= 0)
                    incumbent.Output.Stats.PreparationSeconds += orderSeconds;

                if (options.EnableBasicCompaction || !Processors.empty() || RequireCompleteConstruction) budget.Assess(input, incumbent);
                const bool constructionComplete = !RequireCompleteConstruction ||
                    (Detail::Eligible(incumbent) && incumbent.Checked.Complete);
                if (!constructionComplete)
                    notes.push_back("pipeline.construction incomplete original demand, refinement deferred");
                auto work = incumbent.Output.Stats;
                double postSeconds = 0;
                bool schedulingFailed = false;
                auto polishSlots = Processors.size() + 1;
                std::unique_ptr<IPostProcessor> compactor;
                if (options.EnableBasicCompaction && constructionComplete)
                {
                    AxisCompactionSettings settings;
                    settings.XFirst = options.BasicCompactionXFirst;
                    compactor = MakeAxisCompactionStage(settings).Factory();
                }
                const auto polish = [&](JobResult& state, const std::string& phase)
                {
                    const auto slots = polishSlots;
                    if (polishSlots) --polishSlots;
                    if (!compactor) return;
                    if (!Detail::Eligible(state))
                    {
                        notes.push_back("pipeline.basic=" + phase + ": ineligible original result");
                        return;
                    }
                    if (budget.Stop())
                    {
                        notes.push_back("pipeline.basic=" + phase + ": time/attempt/cancellation stop before normalization");
                        return;
                    }
                    // Only an actual pose-bound no-move report can avoid a
                    // repeated scan. Rejected or stale reports cannot do so.
                    if (const auto report = GetAxisCompactionReport(input, state.Output);
                        report && report->Closed && report->Supported && report->GeometryValidated)
                    {
                        notes.push_back("pipeline.basic=" + phase + ": already event-neighborhood closed");
                        return;
                    }
                    auto settings = budget.RemainingCompaction(slots);
                    if (settings.TimeLimitSeconds <= 0 || !settings.MaxPlacementAttempts)
                    {
                        notes.push_back("pipeline.basic=" + phase + ": reserved budget unavailable");
                        return;
                    }
                    const auto start = Clock::now();
                    bool returned = false;
                    try
                    {
                        JobResult candidate;
                        candidate.Output = compactor->Run(input, state.Output, settings);
                        const auto seconds = Seconds(start);
                        returned = true;
                        postSeconds += seconds;
                        budget.ObserveCompaction(candidate.Output, seconds);
                        budget.Assess(input, candidate);
                        AddWork(work, candidate.Output.Stats);
                        // Down/left movement guides a private proposal. The
                        // published result still obeys the original full key,
                        // including the user's certified-tail priority.
                        const bool accept = Detail::Eligible(candidate) &&
                            Detail::Quality(candidate, input, options.Ranking) <= Detail::Quality(state, input, options.Ranking);
                        if (accept)
                        {
                            state = std::move(candidate);
                            notes.push_back("pipeline.basic=" + phase + ": accepted by shared quality");
                        }
                        else
                        {
                            bool keptFront = false;
                            if (Detail::Eligible(candidate) && !budget.Stop())
                                if (auto preceding = PrecedingSheetMoves(state, candidate.Output))
                                {
                                    JobResult front;
                                    front.Output = std::move(*preceding);
                                    budget.Assess(input, front);
                                    AddWork(work, front.Output.Stats);
                                    if (Detail::Eligible(front) &&
                                        Detail::Quality(front,input,options.Ranking) <= Detail::Quality(state,input,options.Ranking))
                                    {
                                        state = std::move(front);
                                        keptFront = true;
                                    }
                                }
                            notes.push_back("pipeline.basic=" + phase + (keptFront ?
                                ": preceding-sheet moves retained with original logical tail; full-candidate report not published" :
                                ": rejected; candidate report not published"));
                        }
                    }
                    catch (const std::exception& error)
                    {
                        if (!returned) postSeconds += Seconds(start);
                        schedulingFailed = true;
                        notes.push_back("pipeline.basic=" + phase + ": failed safely: " + error.what());
                    }
                    catch (...)
                    {
                        if (!returned) postSeconds += Seconds(start);
                        schedulingFailed = true;
                        notes.push_back("pipeline.basic=" + phase + ": failed safely with non-standard exception");
                    }
                };
                polish(incumbent, "after-construction");
                if (constructionComplete && !Processors.empty() && Detail::Eligible(incumbent))
                    for (auto& [id, processor, precisionShift, budgetFraction] : Processors)
                    {
                        if (schedulingFailed || budget.SearchStop()) break;
                        JobResult candidate;
                        const auto start = Clock::now();
                        bool returned = false;
                        try
                        {
                            auto stageOptions = budget.RemainingSearch();
                            stageOptions.EnableBasicCompaction = false;
                            if (budgetFraction < 1)
                            {
                                stageOptions.TimeLimitSeconds *= budgetFraction;
                                const double attempts = static_cast<double>(stageOptions.MaxPlacementAttempts) * budgetFraction;
                                // The comparison also keeps the integer cast
                                // within range when size_t rounds upward in double.
                                if (attempts < static_cast<double>(stageOptions.MaxPlacementAttempts))
                                    stageOptions.MaxPlacementAttempts = std::max<std::size_t>(1, static_cast<std::size_t>(attempts));
                            }
                            candidate.Output = processor->Run(input, incumbent.Output, stageOptions);
                            postSeconds += Seconds(start);
                            returned = true;
                            budget.Observe(candidate.Output);
                            budget.Assess(input, candidate);
                            AddWork(work, candidate.Output.Stats);
                            // Complete a normal proposal's basic rearrangement
                            // before comparing it with the public incumbent.
                            // Precision-only stages are checked first against
                            // their declared shift bound, then polished below.
                            bool accepted=false;
                            if (precisionShift == 0)
                            {
                                polish(candidate, "after-" + id);
                                if (Detail::Eligible(candidate)) accepted =
                                    Detail::Quality(candidate,input,options.Ranking) < Detail::Quality(incumbent,input,options.Ranking);
                            }
                            else if (Detail::Eligible(candidate) &&
                                IsBoundedPrecisionRepair(incumbent.Output,candidate.Output,precisionShift))
                            {
                                // Check the precision stage's own output first;
                                // subsequent basic moves have their own safety
                                // and quality gates, not the precision bound.
                                polish(candidate, "after-" + id);
                                accepted = Detail::Eligible(candidate) && (!options.EnableBasicCompaction ||
                                    Detail::Quality(candidate,input,options.Ranking) <= Detail::Quality(incumbent,input,options.Ranking));
                            }
                            if (accepted)
                            {
                                incumbent = std::move(candidate);
                                notes.push_back("pipeline.post=" + id + ": accepted");
                            }
                            else
                            {
                                const auto& before = incumbent.Output.Diagnostics;
                                const auto& proposed = candidate.Output.Diagnostics;
                                std::size_t commonPrefix = 0;
                                while (commonPrefix < before.size() && commonPrefix < proposed.size() &&
                                    before[commonPrefix] == proposed[commonPrefix]) ++commonPrefix;
                                for (std::size_t i = commonPrefix; i < proposed.size(); ++i)
                                    notes.push_back("pipeline.rejected=" + id + " | " + proposed[i]);
                                notes.push_back("pipeline.post=" + id + ": rejected");
                            }
                        }
                        catch (const std::exception& error)
                        {
                            if (!returned) postSeconds += Seconds(start);
                            notes.push_back("pipeline.post=" + id + ": failed: " + error.what());
                            break; // A throwing extension cannot report its consumed attempt budget.
                        }
                        catch (...)
                        {
                            if (!returned) postSeconds += Seconds(start);
                            notes.push_back("pipeline.post=" + id + ": failed with non-standard exception");
                            break;
                        }
                    }
                budget.PollCancel();
                if (budget.Cancelled && (incumbent.Output.State == Status::Complete || incumbent.Output.State == Status::Partial))
                    incumbent.Output.State = Status::Cancelled;
                // Constructor-origin counters stay with the retained result; work includes rejected trials.
                incumbent.Output.Stats.PlacementAttempts = work.PlacementAttempts;
                incumbent.Output.Stats.CandidatePatterns = work.CandidatePatterns;
                incumbent.Output.Stats.VerificationSeconds = work.VerificationSeconds;
                incumbent.Output.Stats.EvaluationSeconds = work.EvaluationSeconds;
                incumbent.Output.Stats.PreparationSeconds = work.PreparationSeconds;
                incumbent.Output.Stats.PlacementSeconds = work.PlacementSeconds;
                incumbent.Output.Stats.PostProcessingSeconds = Processors.empty() && !compactor ? work.PostProcessingSeconds :
                    std::max(0.0, work.PostProcessingSeconds) + postSeconds;
                incumbent.Output.Stats.ElapsedSeconds = budget.ElapsedSeconds();
                incumbent.Output.Diagnostics.insert(incumbent.Output.Diagnostics.end(), notes.begin(), notes.end());
                return std::move(incumbent.Output);
            }
        private:
            std::unique_ptr<IStrategy> Construct;
            bool RequireCompleteConstruction = false;
            double ConstructionBudgetFraction = 1;
            std::unique_ptr<IPartOrder> Order;
            std::string OrderID;
            struct Processor {std::string ID;std::unique_ptr<IPostProcessor> Value;double PrecisionShift;double BudgetFraction;};
            std::vector<Processor> Processors;
        };
    }

    StrategyFactory MakePipelineFactory(StrategyPipeline definition)
    {
        if (!definition.Construct) throw std::invalid_argument("Pipeline needs a constructor factory.");
        if (!std::isfinite(definition.ConstructionBudgetFraction) || definition.ConstructionBudgetFraction <= 0 ||
            definition.ConstructionBudgetFraction > 1)
            throw std::invalid_argument("Construction budget fraction must be in (0, 1].");
        if (definition.Order.ID.empty() != !definition.Order.Factory)
            throw std::invalid_argument("Part-order stage needs both ID and factory, or neither.");
        for (const auto& stage : definition.PostProcessors)
            if (stage.ID.empty() || !stage.Factory || !std::isfinite(stage.MaxPrecisionAxisShift) || stage.MaxPrecisionAxisShift<0 ||
                !std::isfinite(stage.BudgetFraction) || stage.BudgetFraction<=0 || stage.BudgetFraction>1)
                throw std::invalid_argument("Postprocessing stage needs an ID, factory, valid precision shift and budget fraction in (0, 1].");
        return [definition = std::move(definition)] { return std::make_unique<PipelineStrategy>(definition); };
    }

    std::unique_ptr<IStrategy> Detail::MakeRefinementExecutor(std::unique_ptr<IStrategy> constructor)
    { return std::make_unique<PipelineStrategy>(std::move(constructor)); }
}
