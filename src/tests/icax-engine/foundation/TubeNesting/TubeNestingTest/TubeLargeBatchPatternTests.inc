namespace
{
    void CheckLargeBatchPlan(const SolveResult& Result_, const PairTableSnapshot& Table_,
        const std::vector<StockType>& Stocks_)
    {
        ASSERT_TRUE(Result_.Status == SolveStatus::Feasible || Result_.Status == SolveStatus::Optimal);
        ASSERT_TRUE(Result_.UnplacedInstances.empty());
        std::map<std::string, std::size_t> _Types, _Stocks, _DemandCounts;
        for (std::size_t _I = 0; _I < Table_.Types.size(); ++_I) _Types.emplace(Table_.Types[_I].ID, _I);
        for (std::size_t _I = 0; _I < Stocks_.size(); ++_I) _Stocks.emplace(Stocks_[_I].ID, _I);
        std::vector<std::size_t> _StockUses(Stocks_.size(), 0);
        std::set<std::string> _Instances;
        for (const auto& _Plan : Result_.Stocks)
        {
            ASSERT_TRUE(_Stocks.contains(_Plan.StockTypeID));
            const auto _StockIndex = _Stocks.at(_Plan.StockTypeID);
            const auto& _Stock = Stocks_[_StockIndex];
            ++_StockUses[_StockIndex];
            if (_Stock.MaxParts) EXPECT_LE(_Plan.Placements.size(), _Stock.MaxParts);
            std::set<std::string> _Orders;
            for (std::size_t _I = 0; _I < _Plan.Placements.size(); ++_I)
            {
                const auto& _Part = _Plan.Placements[_I];
                ASSERT_TRUE(_Types.contains(_Part.DemandID));
                const auto _TypeIndex = _Types.at(_Part.DemandID);
                const auto& _Type = Table_.Types[_TypeIndex];
                EXPECT_TRUE(_Instances.insert(_Part.InstanceID).second);
                ++_DemandCounts[_Part.DemandID];
                const auto _Direction = static_cast<std::size_t>(_Part.PartDirection);
                ASSERT_LT(_Direction, 2u);
                EXPECT_TRUE(_Type.DirectionAllowed[_Direction]);
                EXPECT_EQ(_Type.CompatibilityKey, _Stock.CompatibilityKey);
                if (!_Type.OrderKey.empty()) _Orders.insert(_Type.OrderKey);
                Length _Start = _Stock.FixedInstanceID.empty() ? _Stock.FrontMargin : _Stock.InitialProcessedEnd;
                double _Rotation = _Stock.FixedInstanceID.empty() ? 0.0 : _Stock.InitialPredecessorRotationRadians;
                const PairEntry* _Entry = nullptr;
                if (_I)
                {
                    const auto& _Previous = _Plan.Placements[_I - 1];
                    _Entry = &Table_.At(_Types.at(_Previous.DemandID), _Previous.PartDirection,
                        _TypeIndex, _Part.PartDirection);
                    _Start = _Previous.End;
                    _Rotation = _Previous.RotationRadians;
                }
                else if (!_Stock.InitialPredecessorTypeID.empty())
                    _Entry = &Table_.At(_Types.at(_Stock.InitialPredecessorTypeID),
                        _Stock.InitialPredecessorDirection, _TypeIndex, _Part.PartDirection);
                if (_Entry)
                {
                    ASSERT_TRUE(_Entry->Status == PairEntryStatus::Feasible || _Entry->Status == PairEntryStatus::Certified);
                    ASSERT_LT(_Entry->PoseMetadataIndex, Table_.Poses.size());
                    _Start -= _Entry->NetSaving;
                    _Rotation += Table_.Poses[_Entry->PoseMetadataIndex].RelativeRotationRadians;
                }
                EXPECT_EQ(_Part.Start, _Start);
                EXPECT_EQ(_Part.End, _Start + _Type.MaximumLength);
                EXPECT_LE(_Part.End, _Stock.TotalLength - _Stock.TailDeadZone);
                EXPECT_NEAR(std::remainder(_Part.RotationRadians - _Rotation,
                    2.0 * std::numbers::pi_v<double>), 0.0, 1.0e-9);
                EXPECT_FALSE(_Part.CommonCutWithPrevious);
            }
            if (_Stock.MaxDistinctOrders) EXPECT_LE(_Orders.size(), _Stock.MaxDistinctOrders);
        }
        for (const auto& _Type : Table_.Types) EXPECT_EQ(_DemandCounts[_Type.ID], _Type.Quantity);
        for (std::size_t _I = 0; _I < Stocks_.size(); ++_I)
            if (Stocks_[_I].Quantity) EXPECT_LE(_StockUses[_I], Stocks_[_I].Quantity);
        EXPECT_GE(Result_.Metrics.FirstFeasibleMilliseconds, 0.0);
    }

    PairTableSolverSettings LargeBatchTestSettings()
    {
        PairTableSolverSettings _Settings;
        _Settings.Constraint = ConstraintMode::Disabled;
        _Settings.ConstructionRuns = 2;
        _Settings.LocalSearchPasses = 0;
        _Settings.LargeNeighborhoodIterations = 0;
        _Settings.ExactPartLimit = 0;
        _Settings.ConstraintWorkers = 1;
        _Settings.PatternMasterTimeBudgetMilliseconds = 500;
        _Settings.TimeBudgetMilliseconds = 2000;
        return _Settings;
    }

    PairTableSnapshot DirectedComplementaryDemand(const std::size_t Quantity_)
    {
        auto _Table = ManualTable({
            PairPartType{"long", "profile", "order", 60, 60, Quantity_, {true, false}},
            PairPartType{"short", "profile", "order", 40, 40, Quantity_, {true, false}} });
        SetPair(_Table, 0, Direction::Forward, 0, Direction::Forward, 0, 0.25);
        SetPair(_Table, 0, Direction::Forward, 1, Direction::Forward, 0, 0.75);
        SetPair(_Table, 1, Direction::Forward, 1, Direction::Forward, -1, 0.5);
        // short->long is forbidden, making this an ordered pattern problem.
        return _Table;
    }
}

TEST(TubeNestingPatternMaster, ImprovesNonconstantDirectedPackingWithoutClaimingGlobalProof)
{
    auto _Table = ManualTable({
        PairPartType{"six", "profile", "order", 6, 6, 1, {true, false}},
        PairPartType{"five", "profile", "order", 5, 5, 1, {true, false}},
        PairPartType{"three", "profile", "order", 3, 3, 1, {true, false}},
        PairPartType{"two", "profile", "order", 2, 2, 3, {true, false}} });
    for (std::size_t _A = 0; _A < 4; ++_A)
        for (std::size_t _B = 0; _B < 4; ++_B)
            SetPair(_Table, _A, Direction::Forward, _B, Direction::Forward,
                _A == 3 && _B == 3 ? -1 : 0, 0.125);
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 10}};
    auto _Settings = LargeBatchTestSettings();
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    const auto _Before = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    _Settings.PatternMasterTimeBudgetMilliseconds = 500;
    const auto _After = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    EXPECT_EQ(_Before.Stocks.size(), 3u);
    EXPECT_EQ(_After.Stocks.size(), 2u);
    CheckLargeBatchPlan(_After, _Table, _Stocks);
    EXPECT_EQ(_After.Status, SolveStatus::Feasible);
    EXPECT_NE(_After.ProvenScope, ProofScope::FullIntegerModel);
    EXPECT_TRUE(std::ranges::any_of(_After.Diagnostics, [](const auto& _Text) {
        return _Text.find("scope=restricted-pattern-pool") != std::string::npos; }));
}

TEST(TubeNestingPatternMaster, ReusesGeneralDirectedPatternsWithUniquePhysicalInstances)
{
    const auto _Table = DirectedComplementaryDemand(120);
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, LargeBatchTestSettings());
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Stocks.size(), 120u);
    EXPECT_FALSE(_Result.ExactSearchCompleted);
}

TEST(TubeNestingPatternMaster, FindsCompleteCoverWhenFiniteStockDefeatsInitialOrders)
{
    const auto _Table = DirectedComplementaryDemand(30);
    const std::vector<StockType> _Stocks{{"finite", "profile", StockKind::Standard, 100, 30}};
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, LargeBatchTestSettings());
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Stocks.size(), 30u);
    EXPECT_FALSE(_Result.ExactSearchCompleted);
}

TEST(TubeNestingPatternMaster, PreservesFiniteMixedStocksMarginsOrderLimitsAndProcurementObjective)
{
    auto _Table = DirectedComplementaryDemand(24);
    _Table.Types[1].OrderKey = "another-order";
    std::vector<StockType> _Stocks{
        {"finite", "profile", StockKind::Standard, 110, 12},
        {"unlimited", "profile", StockKind::Standard, 210} };
    for (auto& _Stock : _Stocks)
    {
        _Stock.FrontMargin = 3;
        _Stock.TailDeadZone = 4;
        _Stock.MaxParts = 3;
        _Stock.MaxDistinctOrders = 2;
    }
    _Stocks[0].CostUnits = 100;
    _Stocks[1].CostUnits = 150;
    auto _Settings = LargeBatchTestSettings();
    _Settings.ObjectivePolicy = PairObjectivePolicy::MinPurchaseCost;
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    const auto _Before = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    _Settings.PatternMasterTimeBudgetMilliseconds = 500;
    const auto _After = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_After, _Table, _Stocks);
    EXPECT_LE(std::tie(_After.Metrics.StockCost, _After.Metrics.TotalStockLength, _After.Metrics.OpenedStockCount),
        std::tie(_Before.Metrics.StockCost, _Before.Metrics.TotalStockLength, _Before.Metrics.OpenedStockCount));
}

TEST(TubeNestingPatternMaster, LeavesLockedPrefixAndOneOffStockIdentityIntact)
{
    auto _Table = DirectedComplementaryDemand(20);
    StockType _Continued{"continued", "profile", StockKind::Remnant, 220, 1};
    _Continued.FixedInstanceID = "locked-physical-stock";
    _Continued.InitialProcessedEnd = 100;
    _Continued.InitialPredecessorTypeID = "long";
    _Continued.InitialPredecessorRotationRadians = 0.4;
    const std::vector<StockType> _Stocks{_Continued,
        StockType{"new", "profile", StockKind::Standard, 100}};
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, LargeBatchTestSettings());
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_LE(std::ranges::count_if(_Result.Stocks, [](const auto& _Stock) { return _Stock.Continued; }), 1);
    EXPECT_TRUE(std::ranges::any_of(_Result.Diagnostics, [](const auto& _Text) {
        return _Text.find("fixed-prefix stocks retain the original solver") != std::string::npos; }));
}

TEST(TubeNestingPatternMaster, OversizedBarsReceiveValidatedMovesWithoutBuildingHugeConstraintModels)
{
    auto _Table = ManualTable({
        PairPartType{"five", "profile", "order", 5, 5, 50, {true, false}},
        PairPartType{"six", "profile", "order", 6, 6, 50, {true, false}} });
    for (std::size_t _A = 0; _A < 2; ++_A)
        for (std::size_t _B = 0; _B < 2; ++_B)
            SetPair(_Table, _A, Direction::Forward, _B, Direction::Forward, _A == _B ? 0 : -1);
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 200}};
    auto _Settings = LargeBatchTestSettings();
    _Settings.Constraint = ConstraintMode::HybridLargeNeighborhood;
    _Settings.ExactPartLimit = 9;
    _Settings.LargeNeighborhoodIterations = 2;
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    _Settings.ConstraintNeighborhoodMaximumPartLimit = 12;
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_TRUE(std::ranges::any_of(_Result.Diagnostics, [](const auto& _Text) {
        return _Text.find("oversized bar scheduled") != std::string::npos; }));
}

TEST(TubeNestingPatternMaster, ReportsFirstCompleteIncumbentAndRejectsInvalidNewLimits)
{
    const auto _Table = DirectedComplementaryDemand(3);
    auto _Settings = LargeBatchTestSettings();
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    CheckLargeBatchPlan(PairTableSolver{}.Solve(_Table, _Stocks, _Settings), _Table, _Stocks);
    const auto _Failed = PairTableSolver{}.Solve(_Table,
        {StockType{"too-short", "profile", StockKind::Standard, 10}}, _Settings);
    EXPECT_LT(_Failed.Metrics.FirstFeasibleMilliseconds, 0.0);
    _Settings.PatternMasterMaximumPatterns = 0;
    EXPECT_EQ(PairTableSolver{}.Solve(_Table, _Stocks, _Settings).Status, SolveStatus::InvalidInput);
}

TEST(TubeNestingPatternMaster, OversizedSeedCanAbsorbAndEliminateASmallDonorStock)
{
    auto _Table = ManualTable({
        PairPartType{"donor", "profile", "order", 30, 30, 1, {true, false}},
        PairPartType{"many-short", "profile", "order", 5, 5, 30, {true, false}} });
    SetPair(_Table, 0, Direction::Forward, 0, Direction::Forward, 0);
    SetPair(_Table, 1, Direction::Forward, 1, Direction::Forward, 0);
    SetPair(_Table, 1, Direction::Forward, 0, Direction::Forward, 0, 0.25);
    // donor cannot precede many-short. The original descending/chain seeds
    // therefore open two bars; the 30-piece bar can absorb donor at its tail.
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 200}};
    auto _Settings = LargeBatchTestSettings();
    _Settings.Constraint = ConstraintMode::HybridLargeNeighborhood;
    _Settings.ExactPartLimit = 9;
    _Settings.PatternMasterTimeBudgetMilliseconds = 0;
    _Settings.AdaptiveConstraintNeighborhoods = false;
    _Settings.LargeNeighborhoodIterations = 0;
    const auto _Before = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    ASSERT_EQ(_Before.Stocks.size(), 2u);
    _Settings.AdaptiveConstraintNeighborhoods = true;
    _Settings.LargeNeighborhoodIterations = 4;
    _Settings.ConstraintNeighborhoodMaximumPartLimit = 12;
    const auto _After = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_After, _Table, _Stocks);
    EXPECT_EQ(_After.Stocks.size(), 1u);
    EXPECT_EQ(_After.Metrics.TotalStockLength, 200);
}
