#include "SheetNestingPipeline.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>
#include <map>
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
            explicit PipelineBudget(const Options& settings) : Settings(settings) {}
            bool PollCancel()
            {
                if (!Cancelled && Settings.Cancel && Settings.Cancel()) Cancelled = true;
                return Cancelled;
            }
            bool Stop()
            { return PollCancel() || WorkSeconds() >= Settings.TimeLimitSeconds || Attempts >= Settings.MaxPlacementAttempts; }
            double WorkSeconds() const { return std::max(0.0, Seconds(Started) - AssessmentSeconds); }
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
            void Assess(const Problem& input, JobResult& candidate)
            {
                const auto start = Clock::now();
                try { Detail::AssessCandidate(input, candidate); }
                catch (...) { AssessmentSeconds += Seconds(start); throw; }
                AssessmentSeconds += Seconds(start);
            }
            bool Cancelled = false;
        private:
            const Options& Settings;
            Clock::time_point Started = Clock::now();
            std::size_t Attempts = 0;
            double AssessmentSeconds = 0;
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

        class PipelineStrategy final : public IStrategy
        {
        public:
            explicit PipelineStrategy(const StrategyPipeline& definition)
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
                    Processors.push_back({stage.ID, std::move(processor), stage.MaxPrecisionAxisShift});
                }
            }

            Result Run(const Problem& input, const Options& options) override
            {
                PipelineBudget budget(options);
                std::vector<std::string> notes;
                const auto empty = [&]
                {
                    Result result;
                    result.State = budget.Cancelled ? Status::Cancelled : Status::Partial;
                    for (const auto& part : input.Parts)
                        if (part.Quantity) result.Remaining.push_back({ part.ID, part.Quantity });
                    result.Stats.ElapsedSeconds = budget.WorkSeconds();
                    result.Diagnostics = notes;
                    return result;
                };
                if (budget.Stop()) return empty();
                std::optional<Problem> ordered;
                double orderSeconds = 0;
                if (Order)
                {
                    const auto start = Clock::now();
                    const auto indices = Order->Order(input, budget.Remaining());
                    if (budget.Stop()) return empty();
                    if (indices.size() != input.Parts.size())
                        throw std::runtime_error("Part order must be a complete permutation: " + OrderID);
                    std::vector<bool> seen(indices.size(), false);
                    for (std::size_t i = 0; i < indices.size(); ++i)
                    {
                        if ((i & 1023) == 0 && budget.Stop()) return empty();
                        if (indices[i] >= seen.size() || seen[indices[i]])
                            throw std::runtime_error("Part order contains an invalid or repeated index: " + OrderID);
                        seen[indices[i]] = true;
                    }
                    ordered.emplace(input);
                    for (std::size_t i = 0; i < indices.size(); ++i) ordered->Parts[i] = input.Parts[indices[i]];
                    orderSeconds = Seconds(start);
                    notes.push_back("pipeline.order=" + OrderID);
                }
                if (budget.Stop()) return empty();
                JobResult incumbent;
                incumbent.Output = Construct->Run(ordered ? *ordered : input, budget.Remaining());
                budget.Observe(incumbent.Output);
                if (Order && incumbent.Output.Stats.PreparationSeconds >= 0)
                    incumbent.Output.Stats.PreparationSeconds += orderSeconds;

                if (!Processors.empty()) budget.Assess(input, incumbent);
                auto work = incumbent.Output.Stats;
                double postSeconds = 0;
                if (!Processors.empty() && Detail::Eligible(incumbent))
                    for (auto& [id, processor, precisionShift] : Processors)
                    {
                        if (budget.Stop()) break;
                        JobResult candidate;
                        const auto start = Clock::now();
                        bool returned = false;
                        try
                        {
                            candidate.Output = processor->Run(input, incumbent.Output, budget.Remaining());
                            postSeconds += Seconds(start);
                            returned = true;
                            budget.Observe(candidate.Output);
                            budget.Assess(input, candidate);
                            AddWork(work, candidate.Output.Stats);
                            bool accepted=false;
                            if(Detail::Eligible(candidate))accepted=precisionShift>0
                                ? IsBoundedPrecisionRepair(incumbent.Output,candidate.Output,precisionShift)
                                : Detail::Quality(candidate,input,options.Ranking)<Detail::Quality(incumbent,input,options.Ranking);
                            if (accepted)
                            {
                                incumbent = std::move(candidate);
                                notes.push_back("pipeline.post=" + id + ": accepted");
                            }
                            else notes.push_back("pipeline.post=" + id + ": rejected");
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
                incumbent.Output.Stats.PostProcessingSeconds = Processors.empty() ? work.PostProcessingSeconds :
                    std::max(0.0, work.PostProcessingSeconds) + postSeconds;
                incumbent.Output.Stats.ElapsedSeconds = budget.WorkSeconds();
                incumbent.Output.Diagnostics.insert(incumbent.Output.Diagnostics.end(), notes.begin(), notes.end());
                return std::move(incumbent.Output);
            }
        private:
            std::unique_ptr<IStrategy> Construct;
            std::unique_ptr<IPartOrder> Order;
            std::string OrderID;
            struct Processor {std::string ID;std::unique_ptr<IPostProcessor> Value;double PrecisionShift;};
            std::vector<Processor> Processors;
        };
    }

    StrategyFactory MakePipelineFactory(StrategyPipeline definition)
    {
        if (!definition.Construct) throw std::invalid_argument("Pipeline needs a constructor factory.");
        if (definition.Order.ID.empty() != !definition.Order.Factory)
            throw std::invalid_argument("Part-order stage needs both ID and factory, or neither.");
        for (const auto& stage : definition.PostProcessors)
            if (stage.ID.empty() || !stage.Factory || !std::isfinite(stage.MaxPrecisionAxisShift) || stage.MaxPrecisionAxisShift<0)
                throw std::invalid_argument("Postprocessing stage needs an ID, factory and valid precision shift.");
        return [definition = std::move(definition)] { return std::make_unique<PipelineStrategy>(definition); };
    }
}
