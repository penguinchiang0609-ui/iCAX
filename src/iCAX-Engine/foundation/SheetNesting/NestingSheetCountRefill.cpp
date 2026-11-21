#include "SheetNestingSheetCountRefill.h"
#include "NestingInternal.h"
#include "NestingConstraintKernel.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock = std::chrono::steady_clock;
    using Identity = std::pair<std::string, std::size_t>;
    using TypeGroup = std::vector<std::size_t>;
    using PartitionSignature = std::vector<std::pair<std::string, TypeGroup>>;

    std::string EquivalentType(const Part& part, std::int64_t scale)
    {
        using namespace ConstraintKernel;
        const auto fallback = [&] { return std::string("original-id ") + part.ID; };
        if (part.FreeRotation || part.Geometry.Radius != 0 || !part.Geometry.Holes.empty())
            return fallback();
        Ring original;
        for (const auto point : part.Geometry.Outer)
        {
            IPoint converted;
            if (!Lossless(point.X, scale, converted.X) || !Lossless(point.Y, scale, converted.Y))
                return fallback();
            original.push_back(converted);
        }
        if (original.size() < 3)
            return fallback();
        if (std::abs(Area2(Hull(original))) != std::abs(Area2(original)))
            return fallback();
        std::set<Ring> directions;
        auto angles = part.AllowedAnglesDegrees;
        if (angles.empty())
            angles.push_back(0);
        for (const auto angle : angles)
        {
            const auto quarter = Quarter(angle);
            if (quarter < 0)
                return fallback();
            Ring rotated;
            for (const auto point : original)
            {
                switch (quarter)
                {
                case 0: rotated.push_back(point); break;
                case 1: rotated.push_back({-point.Y, point.X}); break;
                case 2: rotated.push_back({-point.X, -point.Y}); break;
                default: rotated.push_back({point.Y, -point.X}); break;
                }
            }
            auto low = rotated.front();
            for (const auto point : rotated)
            {
                low.X = std::min(low.X, point.X);
                low.Y = std::min(low.Y, point.Y);
            }
            for (auto& point : rotated)
            {
                point.X -= low.X;
                point.Y -= low.Y;
            }
            directions.insert(CanonicalRing(Hull(std::move(rotated))));
        }
        std::ostringstream key;
        key << std::quoted(part.Material);
        for (const auto& ring : directions)
        {
            key << " [";
            for (const auto point : ring)
                key << point.X << ',' << point.Y << ' ';
            key << ']';
        }
        return key.str();
    }

    TypeGroup BoardTypes(const Layout& board, const std::map<std::string, std::size_t>& types)
    {
        TypeGroup group;
        for (const auto& pose : board.Placements)
            group.push_back(types.at(pose.PartID));
        std::sort(group.begin(), group.end());
        return group;
    }

    PartitionSignature Signature(const Result& result, const std::map<std::string, std::size_t>& types,
        const std::map<std::string, std::string>& stocks)
    {
        PartitionSignature signature;
        for (const auto& board : result.Layouts)
            signature.emplace_back(stocks.at(board.StockID), BoardTypes(board, types));
        std::sort(signature.begin(), signature.end());
        return signature;
    }

    struct GroupMatch
    {
        std::size_t Changed = 0;
        std::vector<std::size_t> OldForNew;
    };

    GroupMatch MatchGroups(const std::vector<TypeGroup>& oldGroups, const std::vector<TypeGroup>& newGroups)
    {
        const auto count = oldGroups.size();
        std::vector<std::vector<int>> overlap(count, std::vector<int>(count));
        std::size_t members = 0;
        for (std::size_t fresh = 0; fresh < count; ++fresh)
        {
            members += newGroups[fresh].size();
            for (std::size_t old = 0; old < count; ++old)
            {
                auto a = oldGroups[old].begin();
                auto b = newGroups[fresh].begin();
                while (a != oldGroups[old].end() && b != newGroups[fresh].end())
                {
                    if (*a == *b) { ++overlap[fresh][old]; ++a; ++b; }
                    else if (*a < *b) ++a;
                    else ++b;
                }
            }
        }
        // Hungarian assignment on negative overlap. Its polynomial storage
        // and time also cover whole cohorts larger than local neighborhoods.
        std::vector<int> rowPotential(count + 1), columnPotential(count + 1);
        std::vector<std::size_t> rowAtColumn(count + 1), predecessor(count + 1);
        for (std::size_t row = 1; row <= count; ++row)
        {
            rowAtColumn[0] = row;
            std::size_t column = 0;
            std::vector<int> slack(count + 1, std::numeric_limits<int>::max());
            std::vector<bool> used(count + 1, false);
            do
            {
                used[column] = true;
                const auto activeRow = rowAtColumn[column];
                int delta = std::numeric_limits<int>::max();
                std::size_t nextColumn = 0;
                for (std::size_t candidate = 1; candidate <= count; ++candidate)
                {
                    if (used[candidate])
                        continue;
                    const int reduced = -overlap[activeRow - 1][candidate - 1] -
                        rowPotential[activeRow] - columnPotential[candidate];
                    if (reduced < slack[candidate])
                    {
                        slack[candidate] = reduced;
                        predecessor[candidate] = column;
                    }
                    if (slack[candidate] < delta)
                    {
                        delta = slack[candidate];
                        nextColumn = candidate;
                    }
                }
                for (std::size_t candidate = 0; candidate <= count; ++candidate)
                {
                    if (used[candidate])
                    {
                        rowPotential[rowAtColumn[candidate]] += delta;
                        columnPotential[candidate] -= delta;
                    }
                    else
                        slack[candidate] -= delta;
                }
                column = nextColumn;
            }
            while (rowAtColumn[column] != 0);
            do
            {
                const auto previous = predecessor[column];
                rowAtColumn[column] = rowAtColumn[previous];
                column = previous;
            }
            while (column != 0);
        }
        GroupMatch match;
        match.OldForNew.resize(count);
        std::size_t preserved = 0;
        for (std::size_t old = 1; old <= count; ++old)
        {
            const auto fresh = rowAtColumn[old] - 1;
            match.OldForNew[fresh] = old - 1;
            preserved += static_cast<std::size_t>(overlap[fresh][old - 1]);
        }
        match.Changed = members - preserved;
        return match;
    }

    struct Window
    {
        std::vector<std::size_t> Boards;
        std::size_t Members = 0;
        double TargetFill = 0;
    };

    SheetCountCurrentTaskHint CurrentWindowHint(const Problem& target, std::vector<Layout> layouts,
        const std::map<std::string, std::vector<std::size_t>>& copies)
    {
        SheetCountCurrentTaskHint hint;
        hint.Input = target;
        hint.Input.Stocks.front().Quantity = layouts.size();
        hint.Layout.State = Status::Complete;
        hint.Layout.GeometryValidated = true;
        for (std::size_t board = 0; board < layouts.size(); ++board)
        {
            auto& layout = layouts[board];
            layout.StockInstance = board;
            for (auto& pose : layout.Placements)
            {
                const auto& originalCopies = copies.at(pose.PartID);
                const auto relative = std::lower_bound(originalCopies.begin(), originalCopies.end(), pose.Instance);
                if (relative == originalCopies.end() || *relative != pose.Instance)
                    throw std::logic_error("Current-task guidance lost an original instance");
                pose.Instance = static_cast<std::size_t>(relative - originalCopies.begin());
            }
            hint.Layout.Layouts.push_back(std::move(layout));
        }
        return hint;
    }

    bool NextCombination(std::vector<std::size_t>& indices, std::size_t size)
    {
        std::size_t position = indices.size();
        while (position && indices[position - 1] == size - indices.size() + position - 1)
            --position;
        if (!position)
            return false;
        ++indices[position - 1];
        for (std::size_t i = position; i < indices.size(); ++i)
            indices[i] = indices[i - 1] + 1;
        return true;
    }

    std::vector<Window> BuildWindows(const Problem& input, const Result& current,
        const SheetCountRefillSettings& settings, const std::map<std::string, double>& areas,
        const std::function<bool()>& stop)
    {
        constexpr std::size_t enumerationLimit = 4096;
        constexpr std::size_t candidatesPerSize = 128;
        std::map<std::string, std::vector<std::size_t>> cohorts;
        std::vector<double> material(current.Layouts.size());
        for (std::size_t board = 0; board < current.Layouts.size(); ++board)
        {
            cohorts[current.Layouts[board].StockID].push_back(board);
            for (const auto& pose : current.Layouts[board].Placements)
                material[board] += areas.at(pose.PartID);
        }

        std::vector<Window> windows;
        for (auto& [stockID, cohort] : cohorts)
        {
            if (stop())
                break;
            const auto stock = std::find_if(input.Stocks.begin(), input.Stocks.end(),
                [&](const Stock& item) { return item.ID == stockID; });
            if (stock == input.Stocks.end())
                continue;
            const double usableArea = (stock->Width - 2 * input.EdgeMargin) *
                (stock->Height - 2 * input.EdgeMargin);
            if (usableArea <= 0)
                continue;
            std::stable_sort(cohort.begin(), cohort.end(), [&](std::size_t a, std::size_t b)
                { return material[a] < material[b]; });

            const auto largest = std::min(settings.MaxWindowBoards, cohort.size());
            for (std::size_t size = settings.MinWindowBoards; size <= largest; ++size)
            {
                std::vector<std::size_t> combination(size);
                std::iota(combination.begin(), combination.end(), 0);
                std::size_t candidates = 0;
                std::size_t enumerated = 0;
                do
                {
                    if (stop())
                        return windows;
                    if (enumerated++ >= enumerationLimit)
                        break;
                    Window window;
                    double area = 0;
                    for (const auto position : combination)
                    {
                        const auto board = cohort[position];
                        window.Boards.push_back(board);
                        window.Members += current.Layouts[board].Placements.size();
                        area += material[board];
                    }
                    const double targetArea = static_cast<double>(size - 1) * usableArea;
                    const double areaTolerance = input.Tolerance * (stock->Width + stock->Height) * size;
                    if (window.Members <= settings.MaxReleasedInstances && area <= targetArea + areaTolerance)
                    {
                        window.TargetFill = area / targetArea;
                        windows.push_back(std::move(window));
                        if (++candidates >= candidatesPerSize)
                            break;
                    }
                }
                while (NextCombination(combination, cohort.size()));
            }
        }
        std::stable_sort(windows.begin(), windows.end(), [](const Window& a, const Window& b)
            { return std::tie(a.TargetFill, a.Members) < std::tie(b.TargetFill, b.Members); });
        return windows;
    }

    class SheetCountRefill final : public IPostProcessor
    {
        const SheetCountRefillSettings Settings;
    public:
        explicit SheetCountRefill(SheetCountRefillSettings settings) : Settings(std::move(settings)) {}

        Result Run(const Problem& input, const Result& source, const Options& options) override
        {
            const auto started = Clock::now();
            Result kept = source;
            Result best = source;
            std::vector<std::string> notes = source.Diagnostics;
            std::optional<Detail::QualityKey> bestQuality;
            Statistics work;
            work.PreparationSeconds = 0;
            work.PlacementSeconds = 0;
            work.VerificationSeconds = 0;
            work.EvaluationSeconds = 0;
            std::size_t plans = 0;
            std::size_t accepted = 0;
            std::size_t removed = 0;
            std::size_t transferred = 0;
            std::size_t transferPlans = 0;
            std::size_t transferMoves = 0;
            std::size_t globalReductionPlans = 0;
            std::size_t globalExchangePlans = 0;
            std::size_t perturbations = 0;
            std::size_t exchanges = 0;
            std::size_t exchangedMembers = 0;
            std::size_t tabuRejected = 0;
            std::size_t restarts = 0;
            std::size_t bestUpdates = 0;
            bool cancelled = false;
            double phaseDeadline = options.TimeLimitSeconds;
            const auto elapsed = [&]
                { return std::chrono::duration<double>(Clock::now() - started).count(); };
            const auto stop = [&]
            {
                if (!cancelled && options.Cancel && options.Cancel())
                    cancelled = true;
                return cancelled || !std::isfinite(options.TimeLimitSeconds) ||
                    elapsed() >= options.TimeLimitSeconds || work.PlacementAttempts >= options.MaxPlacementAttempts;
            };
            const auto phaseStop = [&] { return stop() || elapsed() >= phaseDeadline; };
            const auto finish = [&](const std::string& reason)
            {
                Result output = best;
                output.Stats = work;
                output.Stats.CombinationGroups = source.Stats.CombinationGroups;
                output.Stats.CircleLatticeGroups = source.Stats.CircleLatticeGroups;
                output.Stats.GapFilledParts = source.Stats.GapFilledParts;
                output.Stats.ElapsedSeconds = elapsed();
                output.Stats.PostProcessingSeconds = output.Stats.ElapsedSeconds;
                if (cancelled)
                    output.State = Status::Cancelled;
                std::ostringstream note;
                note << "sheet-count.refill: " << reason << "; plans=" << plans << "; accepted=" << accepted
                    << "; removed=" << removed << "; transferred=" << transferred
                    << "; transferPlans=" << transferPlans << "; transfer-moved=" << transferMoves
                    << "; global-reductions=" << globalReductionPlans << "; global-exchanges=" << globalExchangePlans
                    << "; perturbations=" << perturbations << "; exchanges=" << exchanges
                    << "; exchanged-members=" << exchangedMembers << "; tabu-rejected=" << tabuRejected
                    << "; restarts=" << restarts << "; best-updates=" << bestUpdates
                    << "; working-boards=" << kept.Layouts.size() << "; best-boards=" << best.Layouts.size()
                    << "; retained=best"
                    << "; quota-scope=" << (Settings.EnableExchanges ? "per-reduction-phase" : "stage")
                    << "; current-job-window-reconstruction; whole-original-validation";
                output.Diagnostics = notes;
                output.Diagnostics.push_back(note.str());
                return output;
            };

            try
            {
                if (stop())
                    return finish("cancelled-or-budget-before-window");
                if (!Detail::CheckProblem(input).empty())
                    return finish("invalid original input retained");
                std::size_t originalCount = 0;
                for (const auto& layout : source.Layouts)
                    originalCount += layout.Placements.size();
                if (originalCount > options.MaxPlacementAttempts)
                    return finish("insufficient original validation budget");
                const auto checkStarted = elapsed();
                const auto initial = Validate(input, source);
                work.VerificationSeconds += elapsed() - checkStarted;
                work.PlacementAttempts += originalCount;
                if (!initial.Valid || !initial.Complete)
                    return finish("requires-valid-complete-current-job");
                kept.GeometryValidated = true;
                const auto rememberBest = [&](const Validation& checked)
                {
                    const auto evaluation = elapsed();
                    JobResult candidate;
                    candidate.Output = kept;
                    candidate.Checked = checked;
                    candidate.Remnants = EvaluateRemnants(input, kept);
                    const auto quality = Detail::Quality(candidate, input, options.Ranking);
                    work.EvaluationSeconds += elapsed() - evaluation;
                    if (!bestQuality || quality < *bestQuality)
                    {
                        const bool improvement = bestQuality.has_value();
                        bestQuality = quality;
                        best = kept;
                        if (improvement)
                            ++bestUpdates;
                        return improvement;
                    }
                    return false;
                };
                rememberBest(initial);
                if (source.Layouts.size() < 2 ||
                    (!Settings.EnableDonorTransfers && source.Layouts.size() < Settings.MinWindowBoards))
                    return finish("no-reducible-window");

                std::map<std::string, const Part*> originals;
                std::map<std::string, double> areas;
                for (const auto& part : input.Parts)
                {
                    originals.emplace(part.ID, &part);
                    areas.emplace(part.ID, Detail::Area(part.Geometry));
                }
                const auto layoutArea = [&](const Layout& layout)
                {
                    double area = 0;
                    for (const auto& pose : layout.Placements)
                        area += areas.at(pose.PartID);
                    return area;
                };
                const auto cohortAtLowerBound = [&](const Result& current, const std::string& stockID)
                {
                    const auto stock = std::find_if(input.Stocks.begin(), input.Stocks.end(),
                        [&](const Stock& item) { return item.ID == stockID; });
                    const double usable = (stock->Width - 2 * input.EdgeMargin) *
                        (stock->Height - 2 * input.EdgeMargin);
                    if (usable <= 0)
                        return true;
                    double material = 0;
                    std::size_t count = 0;
                    for (const auto& layout : current.Layouts)
                    {
                        if (layout.StockID == stockID)
                        {
                            material += layoutArea(layout);
                            ++count;
                        }
                    }
                    const double tolerance = input.Tolerance * (stock->Width + stock->Height) * count;
                    const double lower = std::ceil(std::max(0., material - tolerance) / usable);
                    return static_cast<double>(count) <= lower;
                };
                const auto clearDonor = [&](std::size_t donor, std::size_t transferQuotaStart)
                {
                    Result working = kept;
                    const auto donorStock = working.Layouts[donor].StockID;
                    const auto stock = std::find_if(input.Stocks.begin(), input.Stocks.end(),
                        [&](const Stock& item) { return item.ID == donorStock; });
                    const double usable = (stock->Width - 2 * input.EdgeMargin) *
                        (stock->Height - 2 * input.EdgeMargin);
                    const double areaTolerance = input.Tolerance * (stock->Width + stock->Height);
                    auto incoming = working.Layouts[donor].Placements;
                    std::stable_sort(incoming.begin(), incoming.end(), [&](const Placement& a, const Placement& b)
                    {
                        const auto aBounds = Detail::Bounds(originals.at(a.PartID)->Geometry, a.Transform);
                        const auto bBounds = Detail::Bounds(originals.at(b.PartID)->Geometry, b.Transform);
                        return std::tuple(-areas.at(a.PartID), -std::max(aBounds.Width, aBounds.Height)) <
                            std::tuple(-areas.at(b.PartID), -std::max(bBounds.Width, bBounds.Height));
                    });
                    std::size_t provisionalMoves = 0;
                    for (const auto& piece : incoming)
                    {
                        if (phaseStop() || transferPlans - transferQuotaStart >= Settings.MaxTransferPlans)
                            break;
                        const double donorArea = layoutArea(working.Layouts[donor]);
                        std::vector<std::size_t> targets;
                        for (std::size_t board = 0; board < working.Layouts.size(); ++board)
                        {
                            if (board == donor || working.Layouts[board].StockID != donorStock)
                                continue;
                            const double targetArea = layoutArea(working.Layouts[board]);
                            if (targetArea + areaTolerance < donorArea ||
                                targetArea + areas.at(piece.PartID) > usable + areaTolerance)
                                continue;
                            if (working.Layouts[board].Placements.size() + 1 >
                                std::min(Settings.MaxReleasedInstances, Settings.Solver.MaxInstances))
                                continue;
                            targets.push_back(board);
                        }
                        std::stable_sort(targets.begin(), targets.end(), [&](std::size_t a, std::size_t b)
                            { return layoutArea(working.Layouts[a]) > layoutArea(working.Layouts[b]); });
                        bool moved = false;
                        for (const auto target : targets)
                        {
                            if (phaseStop() || transferPlans - transferQuotaStart >= Settings.MaxTransferPlans)
                                break;
                            const auto members = working.Layouts[target].Placements.size() + 1;
                            const auto remainingAttempts = options.MaxPlacementAttempts - work.PlacementAttempts;
                            if (remainingAttempts < originalCount + 2 * members)
                                break;

                            Problem subproblem = input;
                            subproblem.Parts.clear();
                            auto oneStock = *stock;
                            oneStock.Quantity = 1;
                            subproblem.Stocks = {oneStock};
                            auto released = working.Layouts[target].Placements;
                            released.push_back(piece);
                            std::map<std::string, std::vector<std::size_t>> copies;
                            std::map<Identity, Placement> originalPlacements;
                            for (const auto& pose : released)
                            {
                                copies[pose.PartID].push_back(pose.Instance);
                                originalPlacements.emplace(Identity{pose.PartID, pose.Instance}, pose);
                            }
                            for (auto& [id, instances] : copies)
                            {
                                std::sort(instances.begin(), instances.end());
                                auto part = *originals.at(id);
                                part.Quantity = instances.size();
                                subproblem.Parts.push_back(std::move(part));
                            }
                            auto childOptions = options;
                            childOptions.TimeLimitSeconds = std::min(Settings.SecondsPerTransfer,
                                phaseDeadline - elapsed());
                            childOptions.MaxPlacementAttempts = remainingAttempts - originalCount;
                            childOptions.Seed = options.Seed + plans + transferPlans;
                            childOptions.Cancel = phaseStop;
                            ++transferPlans;
                            Layout incomingBoard{donorStock, 1, stock->Width, stock->Height, {piece}, areas.at(piece.PartID)};
                            auto guidance = CurrentWindowHint(subproblem, {working.Layouts[target], std::move(incomingBoard)}, copies);
                            const auto attempt = MakeSheetCountRepairStrategy(std::move(guidance), Settings.Solver)()->Run(subproblem, childOptions);
                            work.CandidatePatterns += attempt.Stats.CandidatePatterns;
                            work.PlacementAttempts += attempt.Stats.PlacementAttempts;
                            work.PlacementSeconds += std::max(0., attempt.Stats.PlacementSeconds);
                            work.PreparationSeconds += std::max(0., attempt.Stats.PreparationSeconds);
                            work.VerificationSeconds += std::max(0., attempt.Stats.VerificationSeconds);
                            std::ostringstream trial;
                            trial << "sheet-count.refill transfer: donor=" << working.Layouts[donor].StockInstance
                                << "; target=" << working.Layouts[target].StockInstance << "; part=" << piece.PartID
                                << "; instance=" << piece.Instance << "; released=" << members
                                << "; reconstructed=" << attempt.Layouts.size();
                            notes.push_back(trial.str());
                            for (const auto& diagnostic : attempt.Diagnostics)
                                notes.push_back("sheet-count.refill transfer subproblem | " + diagnostic);
                            if (!attempt.GeometryValidated || !attempt.Remaining.empty() || attempt.Layouts.size() != 1)
                                continue;

                            auto replacement = attempt.Layouts.front();
                            replacement.StockInstance = working.Layouts[target].StockInstance;
                            for (auto& pose : replacement.Placements)
                            {
                                pose.Instance = copies.at(pose.PartID).at(pose.Instance);
                                pose.GapFiller = originalPlacements.at({pose.PartID, pose.Instance}).GapFiller;
                                pose.Group = 0;
                            }
                            Result proposal = working;
                            proposal.Layouts[target] = std::move(replacement);
                            auto& remaining = proposal.Layouts[donor].Placements;
                            const auto from = std::find_if(remaining.begin(), remaining.end(), [&](const Placement& pose)
                                { return pose.PartID == piece.PartID && pose.Instance == piece.Instance; });
                            if (from == remaining.end())
                                throw std::logic_error("Donor transfer lost its original identity");
                            remaining.erase(from);
                            proposal.Layouts[donor].MaterialArea = layoutArea(proposal.Layouts[donor]);
                            const bool emptyDonor = remaining.empty();
                            if (emptyDonor)
                                proposal.Layouts.erase(proposal.Layouts.begin() + donor);
                            if (originalCount > options.MaxPlacementAttempts - work.PlacementAttempts)
                                return false;
                            const auto verification = elapsed();
                            const auto checked = Validate(input, proposal);
                            work.VerificationSeconds += elapsed() - verification;
                            work.PlacementAttempts += originalCount;
                            if (!checked.Valid || !checked.Complete || checked.PlacedCount != originalCount)
                            {
                                notes.push_back("sheet-count.refill transfer: whole-original validation rejected candidate");
                                continue;
                            }
                            proposal.GeometryValidated = true;
                            proposal.Diagnostics = notes;
                            ++provisionalMoves;
                            if (emptyDonor)
                            {
                                // Completeness is preserved and board count
                                // strictly decreases, so every shared ranking
                                // policy accepts this atomic donor transaction.
                                kept = std::move(proposal);
                                rememberBest(checked);
                                ++accepted;
                                ++removed;
                                transferred += provisionalMoves;
                                transferMoves += provisionalMoves;
                                return true;
                            }
                            working = std::move(proposal);
                            moved = true;
                            break;
                        }
                        if (!moved)
                            break;
                    }
                    return false;
                };
                const auto reduceWindow = [&](const Window& window, bool global)
                {
                    const auto remainingAttempts = options.MaxPlacementAttempts - work.PlacementAttempts;
                    if (remainingAttempts < originalCount + 2 * window.Members)
                        return false;

                    const auto& first = kept.Layouts[window.Boards.front()];
                    Problem subproblem = input;
                    subproblem.Parts.clear();
                    subproblem.Stocks.clear();
                    const auto stock = std::find_if(input.Stocks.begin(), input.Stocks.end(),
                        [&](const Stock& item) { return item.ID == first.StockID; });
                    auto reducedStock = *stock;
                    reducedStock.Quantity = window.Boards.size() - 1;
                    subproblem.Stocks.push_back(reducedStock);
                    std::map<std::string, std::vector<std::size_t>> copies;
                    std::map<Identity, Placement> originalPlacements;
                    std::map<Identity, std::size_t> previousBoards;
                    std::vector<std::size_t> stockInstances;
                    const std::set<std::size_t> released(window.Boards.begin(), window.Boards.end());
                    for (const auto board : window.Boards)
                    {
                        const auto& layout = kept.Layouts[board];
                        stockInstances.push_back(layout.StockInstance);
                        for (const auto& pose : layout.Placements)
                        {
                            copies[pose.PartID].push_back(pose.Instance);
                            const Identity identity{pose.PartID, pose.Instance};
                            originalPlacements.emplace(identity, pose);
                            previousBoards.emplace(identity, layout.StockInstance);
                        }
                    }
                    // Relative copies exist only within this subproblem.
                    // Map them back to each original instance after solve.
                    for (auto& [id, instances] : copies)
                    {
                        std::sort(instances.begin(), instances.end());
                        Part part = *originals.at(id);
                        part.Quantity = instances.size();
                        subproblem.Parts.push_back(std::move(part));
                    }
                    auto solverSettings = Settings.Solver;
                    if (!global)
                        solverSettings.MaxInstances = std::min(solverSettings.MaxInstances, Settings.MaxReleasedInstances);
                    auto childOptions = options;
                    childOptions.TimeLimitSeconds = std::min(Settings.SecondsPerWindow,
                        phaseDeadline - elapsed());
                    childOptions.MaxPlacementAttempts = remainingAttempts - originalCount;
                    childOptions.Seed = options.Seed + plans + transferPlans + globalReductionPlans;
                    childOptions.Cancel = phaseStop;
                    if (global) ++globalReductionPlans;
                    else ++plans;
                    std::vector<Layout> guidanceBoards;
                    for (const auto board : window.Boards)
                        guidanceBoards.push_back(kept.Layouts[board]);
                    auto guidance = CurrentWindowHint(subproblem, std::move(guidanceBoards), copies);
                    const auto attempt = MakeSheetCountRepairStrategy(std::move(guidance), solverSettings)()->Run(subproblem, childOptions);
                    work.CandidatePatterns += attempt.Stats.CandidatePatterns;
                    work.PlacementAttempts += attempt.Stats.PlacementAttempts;
                    work.PlacementSeconds += std::max(0., attempt.Stats.PlacementSeconds);
                    work.PreparationSeconds += std::max(0., attempt.Stats.PreparationSeconds);
                    work.VerificationSeconds += std::max(0., attempt.Stats.VerificationSeconds);
                    std::ostringstream trial;
                    trial << "sheet-count.refill: scope=" << (global ? "full-cohort" : "local-window") << "; window=" << window.Boards.size()
                        << "; members=" << window.Members << "; target-max=" << reducedStock.Quantity
                        << "; reconstructed=" << attempt.Layouts.size();
                    notes.push_back(trial.str());
                    for (const auto& diagnostic : attempt.Diagnostics)
                        notes.push_back("sheet-count.refill subproblem | " + diagnostic);
                    if (!attempt.GeometryValidated || !attempt.Remaining.empty() ||
                        attempt.Layouts.empty() || attempt.Layouts.size() >= window.Boards.size())
                        return false;

                    Result proposal = kept;
                    proposal.Layouts.clear();
                    for (std::size_t board = 0; board < kept.Layouts.size(); ++board)
                        if (!released.contains(board))
                            proposal.Layouts.push_back(kept.Layouts[board]);
                    std::size_t moved = 0;
                    for (auto layout : attempt.Layouts)
                    {
                        if (layout.StockInstance >= stockInstances.size())
                            throw std::logic_error("Window reconstruction produced an invalid stock instance");
                        layout.StockInstance = stockInstances[layout.StockInstance];
                        for (auto& pose : layout.Placements)
                        {
                            pose.Instance = copies.at(pose.PartID).at(pose.Instance);
                            const Identity identity{pose.PartID, pose.Instance};
                            pose.GapFiller = originalPlacements.at(identity).GapFiller;
                            pose.Group = 0;
                            if (previousBoards.at(identity) != layout.StockInstance)
                                ++moved;
                        }
                        proposal.Layouts.push_back(std::move(layout));
                    }
                    if (originalCount > options.MaxPlacementAttempts - work.PlacementAttempts)
                        return false;
                    const auto verification = elapsed();
                    const auto checked = Validate(input, proposal);
                    work.VerificationSeconds += elapsed() - verification;
                    work.PlacementAttempts += originalCount;
                    if (!checked.Valid || !checked.Complete || checked.PlacedCount != originalCount)
                    {
                        notes.push_back("sheet-count.refill: rejected reconstructed window by whole-original validation");
                        return false;
                    }
                    removed += kept.Layouts.size() - proposal.Layouts.size();
                    transferred += moved;
                    ++accepted;
                    kept = std::move(proposal);
                    kept.GeometryValidated = true;
                    rememberBest(checked);
                    if (global)
                        notes.push_back("sheet-count.refill reduction: scope=full-cohort; verified=1; working-accepted=1; window=" +
                            std::to_string(window.Boards.size()) + "; members=" + std::to_string(window.Members) +
                            "; target-max=" + std::to_string(window.Boards.size() - 1));
                    return true;
                };
                const auto runReductions = [&](double seconds)
                {
                    const auto windowQuotaStart = Settings.EnableExchanges ? plans : 0;
                    const auto transferQuotaStart = Settings.EnableExchanges ? transferPlans : 0;
                    const double phaseStart = elapsed();
                    const double reductionDeadline = std::min(options.TimeLimitSeconds, phaseStart + seconds);
                    double localStart = phaseStart;
                    if (Settings.EnableExchanges)
                    {
                        phaseDeadline = phaseStart + std::max(0., reductionDeadline - phaseStart) * .5;
                        std::map<std::string, Window> cohorts;
                        for (std::size_t board = 0; board < kept.Layouts.size(); ++board)
                        {
                            auto& cohort = cohorts[kept.Layouts[board].StockID];
                            cohort.Boards.push_back(board);
                            cohort.Members += kept.Layouts[board].Placements.size();
                        }
                        std::vector<Window> fullWindows;
                        for (const auto& [id, cohort] : cohorts)
                        {
                            if (cohort.Boards.size() < 2 || cohortAtLowerBound(kept, id))
                                continue;
                            if (cohort.Members > Settings.Solver.MaxInstances)
                            {
                                notes.push_back("sheet-count.refill reduction: scope=full-cohort; window=" +
                                    std::to_string(cohort.Boards.size()) + "; members=" + std::to_string(cohort.Members) +
                                    "; target-max=" + std::to_string(cohort.Boards.size() - 1) + "; skipped=solver-instance-scope");
                                continue;
                            }
                            fullWindows.push_back(cohort);
                        }
                        if (!fullWindows.empty() && !phaseStop())
                            reduceWindow(fullWindows[(options.Seed + globalReductionPlans) % fullWindows.size()], true);
                        localStart = elapsed();
                    }
                    phaseDeadline = Settings.EnableDonorTransfers ?
                        localStart + std::max(0., reductionDeadline - localStart) * .5 : reductionDeadline;
                    bool donorReduced = Settings.EnableDonorTransfers;
                    while (donorReduced && !phaseStop() && transferPlans - transferQuotaStart < Settings.MaxTransferPlans)
                    {
                        donorReduced = false;
                        std::vector<std::size_t> donors(kept.Layouts.size());
                        std::iota(donors.begin(), donors.end(), 0);
                        std::stable_sort(donors.begin(), donors.end(), [&](std::size_t a, std::size_t b)
                        {
                            const auto& left = kept.Layouts[a];
                            const auto& right = kept.Layouts[b];
                            return layoutArea(left) / (left.Width * left.Height) <
                                layoutArea(right) / (right.Width * right.Height);
                        });
                        for (const auto donor : donors)
                        {
                            if (phaseStop() || transferPlans - transferQuotaStart >= Settings.MaxTransferPlans)
                                break;
                            if (!cohortAtLowerBound(kept, kept.Layouts[donor].StockID) && clearDonor(donor, transferQuotaStart))
                            {
                                donorReduced = true;
                                break;
                            }
                        }
                    }
                    phaseDeadline = reductionDeadline;
                    bool improved = true;
                    while (improved && !phaseStop() && plans - windowQuotaStart < Settings.MaxPlans)
                    {
                        improved = false;
                        const auto preparing = elapsed();
                        const auto windows = BuildWindows(input, kept, Settings, areas, phaseStop);
                        work.PreparationSeconds += elapsed() - preparing;
                        for (const auto& window : windows)
                        {
                            if (phaseStop() || plans - windowQuotaStart >= Settings.MaxPlans)
                                break;
                            if (reduceWindow(window, false))
                            {
                                improved = true;
                                break;
                            }
                        }
                    }
                    phaseDeadline = options.TimeLimitSeconds;
                };
                runReductions(Settings.EnableExchanges ? options.TimeLimitSeconds * .25 : options.TimeLimitSeconds);
                std::map<std::string, std::size_t> types;
                std::map<std::string, std::size_t> equivalent;
                std::map<std::string, std::string> stockTypes;
                if (Settings.EnableExchanges)
                {
                    for (const auto& part : input.Parts)
                    {
                        const auto key = EquivalentType(part, Settings.Solver.CoordinateScale);
                        const auto found = equivalent.emplace(key, equivalent.size()).first;
                        types.emplace(part.ID, found->second);
                    }
                    for (const auto& stock : input.Stocks)
                    {
                        std::ostringstream key;
                        key << std::quoted(stock.Material) << ' ' << std::hexfloat << stock.Width << ' ' << stock.Height;
                        stockTypes.emplace(stock.ID, key.str());
                    }
                }
                std::deque<PartitionSignature> tabu;
                const auto rememberPartition = [&]
                {
                    const auto signature = Signature(kept, types, stockTypes);
                    if (std::find(tabu.begin(), tabu.end(), signature) == tabu.end())
                        tabu.push_back(signature);
                    const auto tenure = std::max<std::size_t>(2, std::min<std::size_t>(32, Settings.MaxStagnation));
                    while (tabu.size() > tenure)
                        tabu.pop_front();
                };
                if (Settings.EnableExchanges)
                    rememberPartition();
                std::size_t windowSize = Settings.MinWindowBoards;
                std::size_t stagnation = 0;
                while (Settings.EnableExchanges && !stop() && perturbations < Settings.MaxPerturbations)
                {
                    const auto updatesBefore = bestUpdates;
                    std::map<std::string, std::vector<std::size_t>> cohorts;
                    for (std::size_t board = 0; board < kept.Layouts.size(); ++board)
                        cohorts[kept.Layouts[board].StockID].push_back(board);
                    std::vector<std::string> eligible;
                    for (const auto& [id, cohort] : cohorts)
                        if (cohort.size() >= Settings.MinWindowBoards && !cohortAtLowerBound(kept, id))
                            eligible.push_back(id);
                    if (eligible.empty())
                        break;
                    std::mt19937_64 random(options.Seed + perturbations);
                    auto cohort = cohorts.at(eligible[(options.Seed + perturbations) % eligible.size()]);
                    std::stable_sort(cohort.begin(), cohort.end(), [&](std::size_t a, std::size_t b)
                        { return layoutArea(kept.Layouts[a]) < layoutArea(kept.Layouts[b]); });
                    const auto half = cohort.size() / 2;
                    const auto sparse = std::uniform_int_distribution<std::size_t>(0, half - 1)(random);
                    const auto full = std::uniform_int_distribution<std::size_t>(half, cohort.size() - 1)(random);
                    std::vector<std::size_t> window{cohort[sparse], cohort[full]};
                    std::vector<std::size_t> others;
                    for (const auto board : cohort)
                        if (board != window[0] && board != window[1])
                            others.push_back(board);
                    std::shuffle(others.begin(), others.end(), random);
                    const bool fullCohort = perturbations == 0 || (perturbations + 1) % 3 == 0;
                    const auto desired = fullCohort ? cohort.size() : std::min(windowSize, cohort.size());
                    while (window.size() < desired)
                    {
                        window.push_back(others.back());
                        others.pop_back();
                    }
                    ++perturbations;
                    std::size_t members = 0;
                    for (const auto board : window)
                        members += kept.Layouts[board].Placements.size();
                    bool exchanged = false;
                    const auto releaseLimit = fullCohort ? Settings.Solver.MaxInstances :
                        std::min(Settings.MaxReleasedInstances, Settings.Solver.MaxInstances);
                    if (members > releaseLimit)
                        notes.push_back("sheet-count.refill exchange-attempt: scope=" +
                            std::string(fullCohort ? "full-cohort" : "local-window") + "; window=" + std::to_string(window.size()) +
                            "; members=" + std::to_string(members) + "; target-max=" + std::to_string(window.size()) +
                            "; skipped=solver-instance-scope");
                    if (members > window.size() && members <= releaseLimit &&
                        options.MaxPlacementAttempts - work.PlacementAttempts >= originalCount + 2 * members)
                    {
                        Problem subproblem = input;
                        subproblem.Parts.clear();
                        const auto& first = kept.Layouts[window.front()];
                        const auto stock = std::find_if(input.Stocks.begin(), input.Stocks.end(),
                            [&](const Stock& item) { return item.ID == first.StockID; });
                        auto stockWindow = *stock;
                        stockWindow.Quantity = window.size();
                        subproblem.Stocks = {stockWindow};
                        std::map<std::string, std::vector<std::size_t>> copies;
                        std::map<Identity, Placement> originalPlacements;
                        std::vector<TypeGroup> oldGroups;
                        for (const auto board : window)
                        {
                            oldGroups.push_back(BoardTypes(kept.Layouts[board], types));
                            for (const auto& pose : kept.Layouts[board].Placements)
                            {
                                copies[pose.PartID].push_back(pose.Instance);
                                originalPlacements.emplace(Identity{pose.PartID, pose.Instance}, pose);
                            }
                        }
                        for (auto& [id, instances] : copies)
                        {
                            std::sort(instances.begin(), instances.end());
                            auto part = *originals.at(id);
                            part.Quantity = instances.size();
                            subproblem.Parts.push_back(std::move(part));
                        }
                        SheetCountPartition partition;
                        for (const auto board : window)
                        {
                            std::vector<SheetCountPartition::Identity> group;
                            for (const auto& pose : kept.Layouts[board].Placements)
                            {
                                const auto& instances = copies.at(pose.PartID);
                                const auto relative = std::lower_bound(instances.begin(), instances.end(), pose.Instance) - instances.begin();
                                group.emplace_back(pose.PartID, static_cast<std::size_t>(relative));
                            }
                            partition.Groups.push_back(std::move(group));
                        }
                        auto childOptions = options;
                        childOptions.TimeLimitSeconds = std::min(fullCohort ? Settings.SecondsPerWindow : Settings.SecondsPerPerturbation,
                            (options.TimeLimitSeconds - elapsed()) * .5);
                        childOptions.MaxPlacementAttempts = options.MaxPlacementAttempts - work.PlacementAttempts - originalCount;
                        childOptions.Cancel = stop;
                        childOptions.Seed = options.Seed + perturbations + transferPlans + plans;
                        if (fullCohort)
                            ++globalExchangePlans;
                        std::vector<Layout> guidanceBoards;
                        for (const auto board : window)
                            guidanceBoards.push_back(kept.Layouts[board]);
                        auto guidance = CurrentWindowHint(subproblem, std::move(guidanceBoards), copies);
                        const auto attempt = MakeSheetCountPerturbationStrategy(partition, std::move(guidance), Settings.Solver)()->Run(subproblem, childOptions);
                        notes.push_back("sheet-count.refill exchange-attempt: scope=" +
                            std::string(fullCohort ? "full-cohort" : "local-window") + "; window=" + std::to_string(window.size()) +
                            "; members=" + std::to_string(members) + "; target-max=" + std::to_string(window.size()) + "; executed=1");
                        work.CandidatePatterns += attempt.Stats.CandidatePatterns;
                        work.PlacementAttempts += attempt.Stats.PlacementAttempts;
                        work.PlacementSeconds += std::max(0., attempt.Stats.PlacementSeconds);
                        work.PreparationSeconds += std::max(0., attempt.Stats.PreparationSeconds);
                        work.VerificationSeconds += std::max(0., attempt.Stats.VerificationSeconds);
                        for (const auto& diagnostic : attempt.Diagnostics)
                            notes.push_back("sheet-count.refill exchange subproblem | " + diagnostic);
                        if (attempt.GeometryValidated && attempt.Remaining.empty() && attempt.Layouts.size() == window.size())
                        {
                            std::vector<TypeGroup> newGroups;
                            for (const auto& board : attempt.Layouts)
                                newGroups.push_back(BoardTypes(board, types));
                            const auto match = MatchGroups(oldGroups, newGroups);
                            if (!match.Changed)
                                notes.push_back("sheet-count.refill exchange: equivalent-type-only partition rejected");
                            else
                            {
                                Result proposal = kept;
                                proposal.Layouts.clear();
                                const std::set<std::size_t> released(window.begin(), window.end());
                                for (std::size_t board = 0; board < kept.Layouts.size(); ++board)
                                    if (!released.contains(board))
                                        proposal.Layouts.push_back(kept.Layouts[board]);
                                for (std::size_t fresh = 0; fresh < attempt.Layouts.size(); ++fresh)
                                {
                                    auto board = attempt.Layouts[fresh];
                                    board.StockInstance = kept.Layouts[window[match.OldForNew[fresh]]].StockInstance;
                                    for (auto& pose : board.Placements)
                                    {
                                        pose.Instance = copies.at(pose.PartID).at(pose.Instance);
                                        pose.GapFiller = originalPlacements.at({pose.PartID, pose.Instance}).GapFiller;
                                        pose.Group = 0;
                                    }
                                    proposal.Layouts.push_back(std::move(board));
                                }
                                const auto signature = Signature(proposal, types, stockTypes);
                                if (std::find(tabu.begin(), tabu.end(), signature) != tabu.end())
                                {
                                    ++tabuRejected;
                                    notes.push_back("sheet-count.refill exchange: recent type-partition tabu rejected");
                                }
                                else if (originalCount <= options.MaxPlacementAttempts - work.PlacementAttempts)
                                {
                                    const auto verification = elapsed();
                                    const auto checked = Validate(input, proposal);
                                    work.VerificationSeconds += elapsed() - verification;
                                    work.PlacementAttempts += originalCount;
                                    if (checked.Valid && checked.Complete && checked.PlacedCount == originalCount)
                                    {
                                        proposal.GeometryValidated = true;
                                        kept = std::move(proposal);
                                        ++exchanges;
                                        exchangedMembers += match.Changed;
                                        const bool improvedBest = rememberBest(checked);
                                        std::ostringstream event;
                                        event << "sheet-count.refill exchange: verified=1; working-accepted=1; changed-members=" << match.Changed
                                            << "; window=" << window.size() << "; best-updated=" << improvedBest
                                            << "; scope=" << (fullCohort ? "full-cohort" : "local-window") << "; members=" << members
                                            << "; working-boards=" << kept.Layouts.size() << "; best-boards=" << best.Layouts.size();
                                        notes.push_back(event.str());
                                        rememberPartition();
                                        exchanged = true;
                                    }
                                }
                            }
                        }
                    }
                    if (exchanged && !stop())
                    {
                        const double remaining = options.TimeLimitSeconds - elapsed();
                        runReductions(std::min(remaining * .5, Settings.SecondsPerWindow + 2 * Settings.SecondsPerTransfer));
                        rememberPartition();
                    }
                    if (bestUpdates > updatesBefore)
                        stagnation = 0;
                    else
                        ++stagnation;
                    if (stagnation >= Settings.MaxStagnation)
                    {
                        kept = best;
                        windowSize = windowSize < Settings.MaxWindowBoards ? windowSize + 1 : Settings.MinWindowBoards;
                        stagnation = 0;
                        ++restarts;
                        notes.push_back("sheet-count.refill exchange: restart-to-best; next-window=" + std::to_string(windowSize));
                    }
                }
                return finish("bounded current-job windows complete");
            }
            catch (const std::exception& error)
            {
                return finish(std::string("failed safely with incumbent retained: ") + error.what());
            }
            catch (...)
            {
                return finish("failed safely with incumbent retained");
            }
        }
    };
}

PostProcessStage MakeSheetCountRefillStage(SheetCountRefillSettings settings)
{
    if (settings.MinWindowBoards < 2 || settings.MaxWindowBoards < settings.MinWindowBoards ||
        settings.MaxWindowBoards > 8 || settings.MaxReleasedInstances < 1 ||
        settings.MaxReleasedInstances > 128 || settings.MaxPlans < 1 ||
        !std::isfinite(settings.SecondsPerWindow) || settings.SecondsPerWindow <= 0 ||
        settings.MaxTransferPlans < 1 || !std::isfinite(settings.SecondsPerTransfer) || settings.SecondsPerTransfer <= 0 ||
        settings.MaxPerturbations < 1 || !std::isfinite(settings.SecondsPerPerturbation) ||
        settings.SecondsPerPerturbation <= 0 || settings.MaxStagnation < 1)
        throw std::invalid_argument("Invalid sheet-count refill settings");
    MakeSheetCountStrategy(settings.Solver);
    return {"sheet-count.refill", [settings] { return std::make_unique<SheetCountRefill>(settings); }};
}
}
