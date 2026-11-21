#include "SheetNestingSheetCountRefill.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    Part Box(std::string id, double width, double height, std::size_t quantity)
    {
        Part part;
        part.ID = std::move(id);
        part.Material = "material";
        part.Quantity = quantity;
        part.FreeRotation = false;
        part.AllowedAnglesDegrees = {0};
        part.Geometry.Outer = {{0, 0}, {width, 0}, {width, height}, {0, height}};
        return part;
    }

    bool Has(const Result& result, const char* text)
    {
        return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(),
            [&](const std::string& note) { return note.find(text) != std::string::npos; });
    }

    bool HasValidatedGuidance(const Result& result, const char* prefix)
    {
        return std::any_of(result.Diagnostics.begin(), result.Diagnostics.end(), [&](const std::string& note)
        {
            return note.starts_with(prefix) && note.find("current-task-hint=1") != std::string::npos &&
                note.find("hint-source-validated=1") != std::string::npos &&
                note.find("hint-hard-locks=0") != std::string::npos;
        });
    }

    Problem CrossBoardProblem()
    {
        Problem input;
        input.Tolerance = 1e-7;
        input.EdgeMargin = .2;
        input.PartGap = .2;
        input.AllowCommonEdges = true;
        input.Stocks.push_back({"stock", 10.4, 10.4, 8, 1, "material"});
        input.Parts = {Box("wide", 6, 10, 3), Box("narrow", 4, 10, 3)};
        return input;
    }

    Result SpreadAcrossFourBoards()
    {
        Result result;
        result.State = Status::Complete;
        result.GeometryValidated = true;
        result.Layouts = {
            {"stock", 0, 10.4, 10.4,
                {{"wide", 0, {.2, .2, 0}, 11, false}, {"narrow", 0, {6.2, .2, 0}, 11, false}}, 100},
            {"stock", 3, 10.4, 10.4, {{"wide", 1, {.2, .2, 0}, 12, false}}, 60},
            {"stock", 7, 10.4, 10.4, {{"wide", 2, {.2, .2, 0}, 13, false}}, 60},
            {"stock", 5, 10.4, 10.4,
                {{"narrow", 1, {.2, .2, 0}, 0, true}, {"narrow", 2, {4.4, .2, 0}, 0, false}}, 80}
        };
        return result;
    }

    std::pair<Problem, Result> SplitDonor(bool sharedEdges)
    {
        Problem input;
        input.Tolerance = 1e-7;
        input.EdgeMargin = .2;
        input.PartGap = .2;
        input.AllowCommonEdges = sharedEdges;
        const double width = sharedEdges ? 9.4 : 10.4;
        const double fullIncomingX = sharedEdges ? 6.2 : 6.4;
        input.Stocks.push_back({"stock", width, 10.4, 8, 1, "material"});
        input.Parts = {Box("incoming", 3, 10, 4), Box("receiver", 6, 10, 4)};
        Result result;
        result.State = Status::Complete;
        result.GeometryValidated = true;
        result.Layouts = {
            {"stock", 7, width, 10.4,
                {{"incoming", 1, {.2, .2, 0}, 0, true}, {"incoming", 3, {3.4, .2, 0}, 0, false}}, 60},
            {"stock", 2, width, 10.4, {{"receiver", 1, {.2, .2, 0}, 1, false}}, 60},
            {"stock", 5, width, 10.4, {{"receiver", 3, {.2, .2, 0}, 2, false}}, 60},
            {"stock", 0, width, 10.4,
                {{"receiver", 0, {.2, .2, 0}, 3, false}, {"incoming", 0, {fullIncomingX, .2, 0}, 3, false}}, 90},
            {"stock", 1, width, 10.4,
                {{"receiver", 2, {.2, .2, 0}, 4, false}, {"incoming", 2, {fullIncomingX, .2, 0}, 4, false}}, 90}
        };
        return {input, result};
    }
}

int main() try
{
    SheetCountRefillSettings settings;
    settings.Solver.Workers = 1;
    settings.Solver.SecondsPerBoardCount = 1;
    settings.MaxWindowBoards = 3;
    settings.MaxReleasedInstances = 8;
    settings.SecondsPerWindow = 2;
    Options options;
    options.TimeLimitSeconds = 10;
    options.MaxPlacementAttempts = 1000;

    const auto input = CrossBoardProblem();
    const auto spread = SpreadAcrossFourBoards();
    Require(Validate(input, spread).Complete, "invalid cross-board source fixture");
    const auto merged = MakeSheetCountRefillStage(settings).Factory()->Run(input, spread, options);
    const auto checked = Validate(input, merged);
    Require(checked.Valid && checked.Complete && checked.PlacedCount == 6 && merged.Layouts.size() == 3,
        "released original instances were not reconstructed with one fewer board");
    Require(Has(merged, "removed=1") && Has(merged, "accepted=1"), "successful cross-board reduction was not recorded");
    Require(HasValidatedGuidance(merged, "sheet-count.refill subproblem |"),
        "window reduction did not consume the validated current-task layout guidance");
    const auto fixed = std::find_if(merged.Layouts.begin(), merged.Layouts.end(),
        [](const Layout& board) { return board.StockInstance == 0; });
    Require(fixed != merged.Layouts.end() && fixed->Placements.size() == 2 &&
        fixed->Placements[0].Transform.X == .2 && fixed->Placements[1].Transform.X == 6.2 &&
        fixed->Placements[0].Group == 11, "a board outside the released window was changed");
    std::map<std::pair<std::string, std::size_t>, std::size_t> boards;
    bool retainedProvenance = false;
    for (const auto& board : merged.Layouts)
    {
        for (const auto& pose : board.Placements)
        {
            boards.emplace(std::pair(pose.PartID, pose.Instance), board.StockInstance);
            if (pose.PartID == "narrow" && pose.Instance == 1)
                retainedProvenance = pose.GapFiller;
        }
    }
    Require(boards.at({"narrow", 1}) != 5 && boards.at({"narrow", 2}) != 5 && retainedProvenance,
        "reconstruction did not transfer donor instances or restore original provenance");
    Require(spread.Layouts.size() == 4 && spread.Layouts[3].Placements[0].Instance == 1,
        "postprocessing mutated the current-job source");

    auto pairOnly = settings;
    pairOnly.MaxWindowBoards = 2;
    const auto limited = MakeSheetCountRefillStage(pairOnly).Factory()->Run(input, spread, options);
    Require(Validate(input, limited).Complete && limited.Layouts.size() == 4,
        "a two-board-only stage silently used a larger release window");

    auto renamed = input;
    auto renamedSource = spread;
    renamed.Stocks[0].ID = "another-stock";
    renamed.Parts[0].ID = "arbitrary-key-a";
    renamed.Parts[1].ID = "arbitrary-key-b";
    for (auto& board : renamedSource.Layouts)
    {
        board.StockID = renamed.Stocks[0].ID;
        for (auto& pose : board.Placements)
            pose.PartID = pose.PartID == "wide" ? renamed.Parts[0].ID : renamed.Parts[1].ID;
    }
    const auto renamedResult = MakeSheetCountRefillStage(settings).Factory()->Run(renamed, renamedSource, options);
    Require(Validate(renamed, renamedResult).Complete && renamedResult.Layouts.size() == 3,
        "task identities affected generic window reconstruction");

    auto transferSettings = settings;
    transferSettings.MaxWindowBoards = 2;
    transferSettings.EnableDonorTransfers = true;
    transferSettings.SecondsPerTransfer = 1;
    transferSettings.MaxTransferPlans = 8;
    for (const bool sharedEdges : {false, true})
    {
        const auto [transferInput, donorSource] = SplitDonor(sharedEdges);
        Require(Validate(transferInput, donorSource).Complete, "invalid split-donor fixture");
        auto disabledTransfers = transferSettings;
        disabledTransfers.EnableDonorTransfers = false;
        const auto windowOnly = MakeSheetCountRefillStage(disabledTransfers).Factory()->Run(transferInput, donorSource, options);
        Require(Validate(transferInput, windowOnly).Complete && windowOnly.Layouts.size() == 5 &&
            Has(windowOnly, "transferPlans=0"), "disabled donor transfers changed the existing two-board window contract");
        const auto split = MakeSheetCountRefillStage(transferSettings).Factory()->Run(transferInput, donorSource, options);
        Require(Validate(transferInput, split).Complete && split.Layouts.size() == 4 &&
            Has(split, "transferPlans=2") && Has(split, "transfer-moved=2") && Has(split, "removed=1"),
            "a donor could not be split between two targets when no two-board merge was possible");
        Require(HasValidatedGuidance(split, "sheet-count.refill transfer subproblem |"),
            "receiver reconstruction discarded the current target and incoming original poses");
        std::map<std::size_t, std::size_t> incomingBoards;
        bool gapFillerRetained = false;
        for (const auto& board : split.Layouts)
        {
            Require(board.StockInstance != 7, "cleared donor board was retained");
            for (const auto& pose : board.Placements)
            {
                if (pose.PartID == "incoming")
                {
                    incomingBoards.emplace(pose.Instance, board.StockInstance);
                    if (pose.Instance == 1)
                        gapFillerRetained = pose.GapFiller;
                }
            }
        }
        Require(incomingBoards.size() == 4 && incomingBoards.at(1) != incomingBoards.at(3) &&
            incomingBoards.at(0) == 0 && incomingBoards.at(2) == 1 && gapFillerRetained,
            "split donor lost noncontinuous original copies, target assignment or construction provenance");

        auto onePlan = transferSettings;
        onePlan.MaxTransferPlans = 1;
        const auto incompleteTransaction = MakeSheetCountRefillStage(onePlan).Factory()->Run(transferInput, donorSource, options);
        Require(Validate(transferInput, incompleteTransaction).Complete && incompleteTransaction.Layouts.size() == 5 &&
            Has(incompleteTransaction, "transferPlans=1") && Has(incompleteTransaction, "transfer-moved=0"),
            "a partially moved donor was published without a whole-quality acceptance gate");
        auto transferBudget = options;
        transferBudget.MaxPlacementAttempts = 20;
        const auto transferExhausted = MakeSheetCountRefillStage(transferSettings).Factory()->Run(transferInput, donorSource, transferBudget);
        Require(Validate(transferInput, transferExhausted).Complete && transferExhausted.Layouts.size() == 5 &&
            transferExhausted.Stats.PlacementAttempts == 20 && Has(transferExhausted, "transferPlans=1") &&
            Has(transferExhausted, "released=2; reconstructed=1"),
            "validation budget exhaustion published a partial donor transaction or lost the incumbent");
        auto cancelTransfer = options;
        cancelTransfer.Cancel = [] { return true; };
        const auto cancelledTransfer = MakeSheetCountRefillStage(transferSettings).Factory()->Run(transferInput, donorSource, cancelTransfer);
        Require(cancelledTransfer.State == Status::Cancelled && Validate(transferInput, cancelledTransfer).Complete &&
            cancelledTransfer.Layouts.size() == 5, "donor-transfer cancellation discarded original complete demand");
    }

    auto exchangeSettings = settings;
    exchangeSettings.MaxWindowBoards = 2;
    exchangeSettings.MaxPlans = 1;
    exchangeSettings.EnableExchanges = true;
    exchangeSettings.MaxPerturbations = 16;
    exchangeSettings.SecondsPerPerturbation = 1;
    exchangeSettings.MaxStagnation = 2;
    // Isolate the local quota regression with an explicit backend scope below
    // this complete job. Full-cohort skips must be diagnosed, while its small
    // local exchanges and subsequent reduction remain supported.
    exchangeSettings.Solver.MaxInstances = 4;
    for (const bool ordinaryGap : {false, true})
    {
        auto exchangeInput = input;
        auto exchangeSource = spread;
        exchangeInput.Parts[0].Geometry.Outer = {{0, 0}, {6, 0}, {4, 10}, {0, 10}};
        exchangeSource.Layouts[0].Placements[0].Transform.X = 4.2;
        exchangeSource.Layouts[0].Placements[1].Transform.X = .2;
        exchangeSource.Layouts[0].MaterialArea = 90;
        exchangeSource.Layouts[1].MaterialArea = 50;
        exchangeSource.Layouts[2].MaterialArea = 50;
        if (ordinaryGap)
        {
            exchangeInput.AllowCommonEdges = false;
            exchangeInput.Stocks[0].Width = 10.6;
            for (auto& board : exchangeSource.Layouts)
                board.Width = 10.6;
            exchangeSource.Layouts.front().Placements[0].Transform.X = 4.4;
        }
        Require(Validate(exchangeInput, exchangeSource).Complete, "invalid phase-quota exchange fixture");
        const auto shuffled = MakeSheetCountRefillStage(exchangeSettings).Factory()->Run(exchangeInput, exchangeSource, options);
        const bool phaseQuotaPassed = Validate(exchangeInput, shuffled).Complete && shuffled.Layouts.size() == 3 &&
            Has(shuffled, "exchange: verified=1; working-accepted=1; changed-members=") && !Has(shuffled, "; exchanges=0;") &&
            Has(shuffled, "; plans=2;") && Has(shuffled, "quota-scope=per-reduction-phase");
        if (!phaseQuotaPassed)
        {
            std::cerr << "phase quota diagnostic ordinaryGap=" << ordinaryGap << '\n';
            for (const auto& diagnostic : shuffled.Diagnostics)
                std::cerr << diagnostic << '\n';
        }
        Require(phaseQuotaPassed,
            "an initial failed reduction exhausted the later post-exchange phase quota");
    }

    // Both complete partitions use two sheets. The balanced working partition
    // has a smaller reusable tail than the deliberately compact AA/BB source.
    // It may be explored, while publication must retain the shared best quality.
    Problem localMinimum;
    localMinimum.Tolerance = 1e-7;
    localMinimum.EdgeMargin = .2;
    localMinimum.PartGap = .2;
    localMinimum.Stocks.push_back({"stock", 8.8, 8.3, 2, 1, "material"});
    localMinimum.Parts = {Box("A", 4, 4, 2), Box("B", 4.1, 4, 2)};
    Result compactSource;
    compactSource.State = Status::Complete;
    compactSource.GeometryValidated = true;
    compactSource.Layouts = {
        {"stock", 0, 8.8, 8.3, {{"A", 0, {.2, .2, 0}, 0, false}, {"A", 1, {4.4, .2, 0}, 0, false}}, 32},
        {"stock", 1, 8.8, 8.3, {{"B", 0, {.2, .2, 0}, 0, false}, {"B", 1, {4.5, .2, 0}, 0, false}}, 32.8}
    };
    Require(Validate(localMinimum, compactSource).Complete, "invalid working-versus-best fixture");
    auto oneExchange = exchangeSettings;
    oneExchange.Solver.MaxInstances = settings.Solver.MaxInstances;
    oneExchange.MaxPlans = 1;
    oneExchange.MaxPerturbations = 1;
    const auto protectedBest = MakeSheetCountRefillStage(oneExchange).Factory()->Run(localMinimum, compactSource, options);
    Require(Validate(localMinimum, protectedBest).Complete && protectedBest.Layouts.size() == 2 &&
        Has(protectedBest, "exchange: verified=1;") && Has(protectedBest, "; exchanges=1;") &&
        protectedBest.Layouts[0].Placements[0].PartID == "A" && protectedBest.Layouts[0].Placements[1].PartID == "A" &&
        protectedBest.Layouts[1].Placements[0].PartID == "B" && protectedBest.Layouts[1].Placements[1].PartID == "B",
        "a temporarily worse legal working exchange replaced the best global-quality result");

    // Different IDs for equivalent square material must not turn identity
    // relabeling into a claimed exchange of different physical specifications.
    auto equivalentInput = localMinimum;
    equivalentInput.Stocks[0].Width = 8.6;
    equivalentInput.Parts = {Box("same-a", 4, 4, 1), Box("same-b", 4, 4, 1),
        Box("same-c", 4, 4, 1), Box("same-d", 4, 4, 1)};
    auto equivalentSource = compactSource;
    for (auto& board : equivalentSource.Layouts)
        board.Width = 8.6;
    equivalentSource.Layouts[0].Placements[0].PartID = "same-a";
    equivalentSource.Layouts[0].Placements[1].PartID = "same-b";
    equivalentSource.Layouts[1].Placements[0].PartID = "same-c";
    equivalentSource.Layouts[1].Placements[1].PartID = "same-d";
    for (auto& board : equivalentSource.Layouts)
    {
        board.Placements[0].Instance = 0;
        board.Placements[1].Instance = 0;
        board.Placements[1].Transform.X = 4.4;
        board.MaterialArea = 32;
    }
    Require(Validate(equivalentInput, equivalentSource).Complete, "invalid equivalent-type fixture");
    const auto copyOnly = MakeSheetCountRefillStage(oneExchange).Factory()->Run(equivalentInput, equivalentSource, options);
    Require(Validate(equivalentInput, copyOnly).Complete && copyOnly.Layouts.size() == 2 &&
        Has(copyOnly, "equivalent-type-only partition rejected") && Has(copyOnly, "; exchanges=0;"),
        "equivalent geometry with changed IDs was reported as a physical specification exchange");
    Require(HasValidatedGuidance(copyOnly, "sheet-count.refill exchange subproblem |"),
        "same-count perturbation discarded its current-task guidance");
    auto cancelledExchange = options;
    cancelledExchange.Cancel = [] { return true; };
    const auto cancelledSearch = MakeSheetCountRefillStage(exchangeSettings).Factory()->Run(input, spread, cancelledExchange);
    Require(cancelledSearch.State == Status::Cancelled && Validate(input, cancelledSearch).Complete &&
        cancelledSearch.Layouts.size() == 4, "exchange cancellation discarded the complete best");

    // Whole-cohort attempts exceed both fast-neighborhood caps and eight
    // boards. The large squares force one per stock, so global K-1 fails while
    // the legal fixed-K perturbation can redistribute the small original copies.
    constexpr std::size_t cohortBoards = 9;
    constexpr std::size_t fillersPerBoard = 4;
    Problem globalInput;
    globalInput.Tolerance = 1e-7;
    globalInput.EdgeMargin = .2;
    globalInput.PartGap = .2;
    globalInput.Stocks.push_back({"global-stock", 10.4, 10.4, cohortBoards, 1, "material"});
    globalInput.Parts = {Box("filler", .1, .1, cohortBoards * fillersPerBoard)};
    Result globalSource;
    globalSource.State = Status::Complete;
    globalSource.GeometryValidated = true;
    for (std::size_t board = 0; board < cohortBoards; ++board)
    {
        const auto id = "large-" + std::to_string(board);
        const double width = 6. + .2 * board;
        globalInput.Parts.push_back(Box(id, width, 6, 1));
        Layout layout{"global-stock", board, 10.4, 10.4, {{id, 0, {.2, .2, 0}, 0, false}}, width * 6 + .04};
        for (std::size_t filler = 0; filler < fillersPerBoard; ++filler)
        {
            layout.Placements.push_back({"filler", board * fillersPerBoard + filler,
                {8.0 + .3 * (filler % 2), .2 + .3 * (filler / 2), 0}, 0, false});
        }
        globalSource.Layouts.push_back(std::move(layout));
    }
    Require(Validate(globalInput, globalSource).Complete, "invalid large full-cohort fixture");
    auto globalSettings = oneExchange;
    globalSettings.MaxWindowBoards = 4;
    globalSettings.MaxReleasedInstances = 32;
    globalSettings.Solver.MaxInstances = 128;
    globalSettings.SecondsPerWindow = 10;
    globalSettings.MaxPerturbations = 1;
    auto globalOptions = options;
    globalOptions.TimeLimitSeconds = 30;
    const auto globalResult = MakeSheetCountRefillStage(globalSettings).Factory()->Run(globalInput, globalSource, globalOptions);
    const auto scope = "scope=full-cohort; window=" + std::to_string(cohortBoards) + "; members=" +
        std::to_string(cohortBoards * (fillersPerBoard + 1));
    for (const auto& diagnostic : globalResult.Diagnostics)
    {
        if (diagnostic.starts_with("sheet-count.refill: scope=full-cohort") ||
            diagnostic.starts_with("sheet-count.refill exchange-attempt: scope=full-cohort") ||
            diagnostic.starts_with("sheet-count.refill exchange: verified=1") ||
            diagnostic.starts_with("sheet-count.refill: bounded current-job") ||
            diagnostic.find("sheet-count.perturb: target=") != std::string::npos)
            std::cout << "global fixture | " << diagnostic << '\n';
    }
    Require(Validate(globalInput, globalResult).Complete && globalResult.Layouts.size() == cohortBoards &&
        Has(globalResult, (scope + "; target-max=" + std::to_string(cohortBoards - 1)).c_str()) &&
        Has(globalResult, (scope + "; target-max=" + std::to_string(cohortBoards) + "; executed=1").c_str()) &&
        Has(globalResult, "global-reductions=2") && Has(globalResult, "global-exchanges=1") &&
        Has(globalResult, "exchange: verified=1; working-accepted=1; changed-members="),
        "local board/member caps blocked the explicitly scheduled complete cohort");

    Problem spaced;
    spaced.Tolerance = 1e-7;
    spaced.EdgeMargin = .2;
    spaced.PartGap = .2;
    spaced.Stocks.push_back({"stock", 8.6, 5.4, 2, 1, "material"});
    spaced.Parts.push_back(Box("copies", 4, 5, 2));
    Result separate;
    separate.State = Status::Complete;
    separate.GeometryValidated = true;
    separate.Layouts = {
        {"stock", 0, 8.6, 5.4, {{"copies", 0, {.2, .2, 0}, 0, false}}, 20},
        {"stock", 1, 8.6, 5.4, {{"copies", 1, {.2, .2, 0}, 0, false}}, 20}
    };
    const auto spacedResult = MakeSheetCountRefillStage(settings).Factory()->Run(spaced, separate, options);
    Require(Validate(spaced, spacedResult).Complete && spacedResult.Layouts.size() == 1,
        "positive ordinary spacing prevented a valid cross-board reduction");
    auto nonGridInput = spaced;
    nonGridInput.Stocks[0].Height = 5.6;
    auto nonGridSource = separate;
    for (auto& board : nonGridSource.Layouts)
    {
        board.Height = 5.6;
        board.Placements[0].Transform.X = .213;
        board.Placements[0].Transform.Y = .217;
    }
    Require(Validate(nonGridInput, nonGridSource).Complete, "invalid non-grid current-task guidance fixture");
    const auto guidedNonGrid = MakeSheetCountRefillStage(settings).Factory()->Run(nonGridInput, nonGridSource, options);
    Require(Validate(nonGridInput, guidedNonGrid).Complete && guidedNonGrid.Layouts.size() == 1 &&
        HasValidatedGuidance(guidedNonGrid, "sheet-count.refill subproblem |") &&
        Has(guidedNonGrid, "rounded-hint-instances=1") &&
        nonGridSource.Layouts[0].Placements[0].Transform.X == .213 &&
        nonGridSource.Layouts[0].Placements[0].Transform.Y == .217,
        "continuous current poses were changed or rejected instead of supplying safe soft grid guidance");
    spaced.Stocks[0].Width = 8.4;
    for (auto& board : separate.Layouts)
        board.Width = 8.4;
    const auto blockedGap = MakeSheetCountRefillStage(settings).Factory()->Run(spaced, separate, options);
    Require(Validate(spaced, blockedGap).Complete && blockedGap.Layouts.size() == 2,
        "window reconstruction discarded ordinary part clearance");
    spaced.AllowCommonEdges = true;
    const auto shared = MakeSheetCountRefillStage(settings).Factory()->Run(spaced, separate, options);
    Require(Validate(spaced, shared).Complete && shared.Layouts.size() == 1,
        "a genuine common edge did not permit donor-board elimination");

    auto cap = settings;
    cap.MaxReleasedInstances = 3;
    const auto capped = MakeSheetCountRefillStage(cap).Factory()->Run(input, spread, options);
    Require(Validate(input, capped).Complete && capped.Layouts.size() == 4,
        "released-instance limit was ignored");
    auto exactBudget = options;
    exactBudget.MaxPlacementAttempts = 6;
    const auto retained = MakeSheetCountRefillStage(settings).Factory()->Run(input, spread, exactBudget);
    Require(Validate(input, retained).Complete && retained.Layouts.size() == 4 && retained.Stats.PlacementAttempts <= 6,
        "validation exhaustion discarded the incumbent or overran the attempt budget");
    auto cancelled = options;
    cancelled.Cancel = [] { return true; };
    const auto stopped = MakeSheetCountRefillStage(settings).Factory()->Run(input, spread, cancelled);
    Require(stopped.State == Status::Cancelled && Validate(input, stopped).Complete && stopped.Layouts.size() == 4,
        "cancellation discarded complete source identities");
    auto noTime = options;
    noTime.TimeLimitSeconds = 0;
    const auto exhausted = MakeSheetCountRefillStage(settings).Factory()->Run(input, spread, noTime);
    Require(Validate(input, exhausted).Complete && exhausted.Layouts.size() == 4,
        "zero stage budget changed the complete incumbent");

    bool invalidSettingsRejected = false;
    auto invalid = settings;
    invalid.MaxPlans = 0;
    try
    {
        MakeSheetCountRefillStage(invalid);
    }
    catch (const std::invalid_argument&)
    {
        invalidSettingsRejected = true;
    }
    Require(invalidSettingsRejected, "invalid refill settings were accepted");
    std::cout << "Sheet-count refill tests passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
