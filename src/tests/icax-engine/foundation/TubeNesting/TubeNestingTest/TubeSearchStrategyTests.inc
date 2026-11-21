TEST(TubeNestingSearchStrategy, ContinuedImprovementRequiresAFiniteBudget)
{
    auto _Table = DirectedComplementaryDemand(3);
    auto _Settings = LargeBatchTestSettings();
    _Settings.ContinueImprovementUntilTimeLimit = true;
    _Settings.TimeBudgetMilliseconds = 0;
    const auto _Result = PairTableSolver{}.Solve(_Table,
        {StockType{"stock", "profile", StockKind::Standard, 100}}, _Settings);
    EXPECT_EQ(_Result.Status, SolveStatus::InvalidInput);
}

TEST(TubeNestingSearchStrategy, CompleteProofStopsWithoutSpendingTheRemainingTime)
{
    auto _Table = ManualTable({PairPartType{"ten", "profile", "order", 10, 10, 100}});
    for (const auto _A : {Direction::Forward, Direction::Reverse})
        for (const auto _B : {Direction::Forward, Direction::Reverse})
            SetPair(_Table, 0, _A, 0, _B, 0);
    auto _Settings = LargeBatchTestSettings();
    _Settings.ContinueImprovementUntilTimeLimit = true;
    _Settings.TimeBudgetMilliseconds = 1000;
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 1000}};
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Status, SolveStatus::Optimal);
    EXPECT_EQ(_Result.Metrics.RefinementRounds, 0u);
}

namespace
{
    PairTableSnapshot SearchStrategyRestartTable(const bool TwoGroups_)
    {
        std::vector<PairPartType> _Types;
        for (const auto* _Group : {"A", "B"})
        {
            if (!TwoGroups_ && std::string(_Group) == "B") break;
            for (const auto _Length : {6, 5, 3, 2})
                _Types.push_back(PairPartType{std::string(_Group) + std::to_string(_Length),
                    _Group, "order", _Length, _Length, _Length == 2 ? 3u : 1u, {true, false}});
        }
        auto _Table = ManualTable(std::move(_Types));
        for (std::size_t _A = 0; _A < _Table.Types.size(); ++_A)
            for (std::size_t _B = 0; _B < _Table.Types.size(); ++_B)
                if (_Table.Types[_A].CompatibilityKey == _Table.Types[_B].CompatibilityKey)
                    SetPair(_Table, _A, Direction::Forward, _B, Direction::Forward,
                        _A % 4 == 3 && _B % 4 == 3 ? -1 : 0, 0.125);
        return _Table;
    }
}

TEST(TubeNestingSearchStrategy, LongerSearchContinuesPastTheInitialPortfolioAndKeepsTheBest)
{
    const auto _Table = SearchStrategyRestartTable(false);
    const std::vector<StockType> _Stocks{{"stock", "A", StockKind::Standard, 10}};
    auto _Settings = LargeBatchTestSettings();
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    _Settings.TimeBudgetMilliseconds = 150;
    const auto _Fast = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    EXPECT_EQ(_Fast.Metrics.RefinementRounds, 0u);
    _Settings.ContinueImprovementUntilTimeLimit = true;
    const auto _Thorough = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Thorough, _Table, _Stocks);
    EXPECT_GT(_Thorough.Metrics.RefinementRounds, 0u);
    EXPECT_EQ(_Fast.Stocks.size(), 3u);
    EXPECT_EQ(_Thorough.Stocks.size(), 2u);
    EXPECT_EQ(_Thorough.Termination, TerminationReason::TimeLimit);
    EXPECT_NE(_Thorough.ProvenScope, ProofScope::FullIntegerModel);
}

TEST(TubeNestingSearchStrategy, EveryCompatibilityGroupRetainsAnOpportunityToFindACompletePlan)
{
    const auto _Table = SearchStrategyRestartTable(true);
    const std::vector<StockType> _Stocks{
        {"stock-A", "A", StockKind::Standard, 10}, {"stock-B", "B", StockKind::Standard, 10}};
    auto _Settings = LargeBatchTestSettings();
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    _Settings.ContinueImprovementUntilTimeLimit = true;
    _Settings.TimeBudgetMilliseconds = 200;
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Stocks.size(), 4u);
    EXPECT_GE(_Result.Metrics.RefinementRounds, 2u);
}
