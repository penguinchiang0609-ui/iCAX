#include "NestingExecution.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>

namespace iCAX::SheetNesting
{
    namespace
    {
        void AddRemaining(Result& result, const Problem& p)
        { for (const auto& part : p.Parts) if (part.Quantity) result.Remaining.push_back({ part.ID, part.Quantity }); }
        JobResult Execute(const Job& job, std::unique_ptr<IStrategy> strategy, const std::string& factoryError)
        {
            JobResult completed;
            completed.ID = job.ID;
            completed.StrategyID = job.StrategyID;
            auto errors = Detail::CheckProblem(job.Input);
            if (!std::isfinite(job.Settings.TimeLimitSeconds) || job.Settings.TimeLimitSeconds <= 0 ||
                job.Settings.MaxPlacementAttempts == 0 || job.Settings.MaxPatterns == 0 ||
                job.Settings.MaxPatterns > 4096 || job.Settings.MaxPatternMembers < 1 || job.Settings.MaxPatternMembers > 4 ||
                job.Settings.MaxAssemblyMembers < 1 || job.Settings.MaxAssemblyMembers > 12)
                errors.push_back("Invalid job time/attempt/pattern limits (pattern members 1..4, assembly members 1..12, patterns 1..4096).");
            if (job.Settings.Ranking != RankingPolicy::RegularRemnant &&
                job.Settings.Ranking != RankingPolicy::LowThenLeft && job.Settings.Ranking != RankingPolicy::CompactArea)
                errors.push_back("Unknown ranking policy.");
            if (!errors.empty())
            {
                completed.Output.State = Status::InvalidInput;
                completed.Output.Diagnostics = std::move(errors);
                AddRemaining(completed.Output, job.Input);
                return completed;
            }
            try
            {
                if (!strategy) throw std::runtime_error(factoryError.empty() ? "Strategy factory returned null." : factoryError);
                completed.Output = strategy->Run(job.Input, job.Settings);
                Detail::AssessCandidate(job.Input, completed);
            }
            catch (const std::exception& error)
            {
                completed.Output = {};
                completed.Output.State = Status::Failed;
                completed.Output.Diagnostics.push_back(error.what());
                AddRemaining(completed.Output, job.Input);
            }
            catch (...)
            {
                completed.Output = {};
                completed.Output.State = Status::Failed;
                completed.Output.Diagnostics.push_back("Strategy threw a non-standard exception.");
                AddRemaining(completed.Output, job.Input);
            }
            return completed;
        }
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
        PortfolioResult portfolio;
        portfolio.Jobs.resize(ids.size());
        std::vector<Job> jobs;
        std::vector<std::unique_ptr<IStrategy>> strategies;
        std::vector<std::string> factoryErrors(ids.size());
        // Factories run serially; even factories with a mutable counter are not called concurrently.
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            jobs.push_back({ std::to_string(i) + ":" + ids[i], ids[i], input, options.PerJob });
            jobs.back().Settings.Seed += i;
            jobs.back().Settings.Ranking = options.Ranking;
            try { strategies.push_back(registry.Create(ids[i])); }
            catch (const std::exception& e) { strategies.push_back(nullptr); factoryErrors[i] = e.what(); }
            catch (...) { strategies.push_back(nullptr); factoryErrors[i] = "Strategy factory failed."; }
        }
        std::atomic<std::size_t> next{ 0 };
        const auto worker = [&]
        {
            for (;;)
            {
                const auto i = next.fetch_add(1);
                if (i >= jobs.size()) return;
                portfolio.Jobs[i] = Execute(jobs[i], std::move(strategies[i]), factoryErrors[i]);
            }
        };
        const auto count = std::min<std::size_t>(ids.size(), std::clamp<std::size_t>(options.Workers, 1, 32));
        std::vector<std::jthread> workers;
        for (std::size_t i = 1; i < count; ++i) workers.emplace_back(worker);
        worker();
        for (auto& thread : workers) thread.join();
        std::optional<Detail::QualityKey> bestScore;
        for (std::size_t i = 0; i < portfolio.Jobs.size(); ++i)
            if (Detail::Eligible(portfolio.Jobs[i]))
            {
                const auto score = Detail::Quality(portfolio.Jobs[i], input, options.Ranking);
                if (!bestScore || score < *bestScore) { bestScore = score; portfolio.BestJob = i; }
            }
        return portfolio;
    }
    PortfolioResult RunPortfolio(const Problem& input, const PortfolioOptions& options)
    { return RunPortfolio(input, options, DefaultStrategies()); }
}
