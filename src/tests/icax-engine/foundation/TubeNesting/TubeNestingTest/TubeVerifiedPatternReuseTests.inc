namespace
{
    PairTableSnapshot ReuseTestTable(const std::string& Version_)
    {
        auto _Table = DirectedComplementaryDemand(20);
        _Table.Version = Version_;
        return _Table;
    }

    PairTableSolverSettings ReuseTestSettings()
    {
        auto _Settings = LargeBatchTestSettings();
        _Settings.PatternMasterTimeBudgetMilliseconds = 250;
        _Settings.ReuseVerifiedPatterns = true;
        return _Settings;
    }
}

TEST(TubeNestingPatternReuse, ReusesVerifiedRoutesAcrossDemandAndInventoryQuantities)
{
    auto _Table = ReuseTestTable("reuse-quantity-and-stock-v1");
    std::vector<StockType> _Stocks{{"finite", "profile", StockKind::Standard, 100, 40}};
    const auto _Settings = ReuseTestSettings();
    const auto _Cold = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Cold, _Table, _Stocks);
    EXPECT_EQ(_Cold.Metrics.PatternCacheHits, 0u);
    EXPECT_GT(_Cold.Metrics.PatternCacheStored, 0u);

    _Table.Types[0].Quantity = 17;
    _Table.Types[1].Quantity = 11;
    _Stocks[0].Quantity = 17;
    const auto _Warm = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Warm, _Table, _Stocks);
    EXPECT_GT(_Warm.Metrics.PatternCacheHits, 0u);
    EXPECT_GT(_Warm.Metrics.PatternCacheImported, 0u);
    EXPECT_GT(_Warm.Metrics.PatternCacheConstructedPlans, 0u);
    EXPECT_EQ(_Warm.Metrics.PatternCacheRecipesRestored, 0u);
    EXPECT_EQ(_Warm.Stocks.size(), 17u);
    EXPECT_FALSE(_Warm.ExactSearchCompleted);
    EXPECT_NE(_Warm.ProvenScope, ProofScope::FullIntegerModel);
}

TEST(TubeNestingPatternReuse, DisabledReuseNeitherImportsNorPublishesPatterns)
{
    const auto _Table = ReuseTestTable("reuse-disabled-v1");
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    auto _Settings = ReuseTestSettings();
    _Settings.ReuseVerifiedPatterns = false;
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Metrics.PatternCacheHits, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheImported, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheStored, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheConstructedPlans, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheRecipesRestored, 0u);
}

TEST(TubeNestingPatternReuse, ReplaysTheFinalVerifiedCompositionBeforeSearchingAgain)
{
    const auto _Table = ReuseTestTable("reuse-final-composition-v1");
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    const auto _Settings = ReuseTestSettings();
    const auto _Cold = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Cold, _Table, _Stocks);
    const auto _Warm = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Warm, _Table, _Stocks);
    EXPECT_GT(_Warm.Metrics.PatternCacheRecipeHits, 0u);
    EXPECT_GT(_Warm.Metrics.PatternCacheRecipesRestored, 0u);
    EXPECT_LE(std::tie(_Warm.Metrics.TotalStockLength, _Warm.Metrics.OpenedStockCount),
        std::tie(_Cold.Metrics.TotalStockLength, _Cold.Metrics.OpenedStockCount));
    EXPECT_FALSE(_Warm.ExactSearchCompleted);
}

TEST(TubeNestingPatternReuse, ChangedGeometryPoseProcessAndStockRulesDoNotHitAnOldContext)
{
    const auto _Original = ReuseTestTable("reuse-invalidation-v1");
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    const auto _Settings = ReuseTestSettings();
    CheckLargeBatchPlan(PairTableSolver{}.Solve(_Original, _Stocks, _Settings), _Original, _Stocks);
    for (unsigned _Change = 0; _Change < 7; ++_Change)
    {
        auto _Table = _Original;
        auto _ChangedStocks = _Stocks;
        switch (_Change)
        {
        case 0:
            _Table.Poses[_Table.At(0, Direction::Forward, 1, Direction::Forward).PoseMetadataIndex]
                .RelativeRotationRadians += 0.375;
            break;
        case 1:
            _Table.Entries[_Table.EntryIndex(1, Direction::Forward, 1, Direction::Forward)].NetSaving = -3;
            break;
        case 2: _Table.ProcessConfigurationHash = "new-process-loss"; break;
        case 3: _ChangedStocks[0].FrontMargin = 5; break;
        case 4: _ChangedStocks[0].MaxParts = 1; break;
        case 5:
            _Table.Types[1].OrderKey = "another-order";
            _ChangedStocks[0].MaxDistinctOrders = 1;
            break;
        case 6:
            _Table.Types[0].DirectionAllowed = {false, true};
            SetPair(_Table, 0, Direction::Reverse, 0, Direction::Reverse, 0, 0.25);
            SetPair(_Table, 0, Direction::Reverse, 1, Direction::Forward, 0, 0.75);
            break;
        }
        const auto _Changed = PairTableSolver{}.Solve(_Table, _ChangedStocks, _Settings);
        CheckLargeBatchPlan(_Changed, _Table, _ChangedStocks);
        EXPECT_EQ(_Changed.Metrics.PatternCacheHits, 0u) << "change=" << _Change;
    }
}

TEST(TubeNestingPatternReuse, AnUnavailableCheaperRecipeDoesNotHideTheCurrentInventoryRecipe)
{
    auto _Table = ReuseTestTable("reuse-recipe-inventory-v1");
    _Table.Types[0].Quantity = _Table.Types[1].Quantity = 4;
    std::vector<StockType> _Stocks{
        {"short-finite", "profile", StockKind::Standard, 100, 4},
        {"long-unlimited", "profile", StockKind::Standard, 210}};
    const auto _Settings = ReuseTestSettings();
    CheckLargeBatchPlan(PairTableSolver{}.Solve(_Table, _Stocks, _Settings), _Table, _Stocks);
    _Stocks[0].Quantity = 1;
    const auto _Changed = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Changed, _Table, _Stocks);
    EXPECT_EQ(_Changed.Metrics.PatternCacheRecipesRestored, 0u);
    const auto _Warm = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
    CheckLargeBatchPlan(_Warm, _Table, _Stocks);
    EXPECT_GT(_Warm.Metrics.PatternCacheRecipesRestored, 0u);
    EXPECT_LE(std::tie(_Warm.Metrics.TotalStockLength, _Warm.Metrics.OpenedStockCount),
        std::tie(_Changed.Metrics.TotalStockLength, _Changed.Metrics.OpenedStockCount));
}

TEST(TubeNestingPatternReuse, EvictionAndConcurrentQuantityQueriesKeepOwnedSnapshotsValid)
{
    auto _Settings = ReuseTestSettings();
    std::uint64_t _Evictions = 0;
    for (unsigned _Context = 0; _Context < 10; ++_Context)
    {
        const auto _Table = ReuseTestTable("reuse-eviction-" + std::to_string(_Context));
        const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
        const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, _Settings);
        CheckLargeBatchPlan(_Result, _Table, _Stocks);
        _Evictions += _Result.Metrics.PatternCacheEvictions;
    }
    EXPECT_GT(_Evictions, 0u);
    const auto _Table = ReuseTestTable("reuse-parallel-v1");
    const std::vector<StockType> _Stocks{{"stock", "profile", StockKind::Standard, 100}};
    CheckLargeBatchPlan(PairTableSolver{}.Solve(_Table, _Stocks, _Settings), _Table, _Stocks);
    std::vector<std::future<SolveResult>> _Tasks;
    for (std::size_t _Worker = 0; _Worker < 4; ++_Worker)
        _Tasks.push_back(std::async(std::launch::async, [_Input = _Table, _Stocks, _Settings, _Worker]() mutable {
            _Input.Types[0].Quantity = 8 + _Worker;
            _Input.Types[1].Quantity = 7 + _Worker;
            return PairTableSolver{}.Solve(_Input, _Stocks, _Settings);
        }));
    for (std::size_t _Worker = 0; _Worker < _Tasks.size(); ++_Worker)
    {
        auto _Demand = _Table;
        _Demand.Types[0].Quantity = 8 + _Worker;
        _Demand.Types[1].Quantity = 7 + _Worker;
        CheckLargeBatchPlan(_Tasks[_Worker].get(), _Demand, _Stocks);
    }
}

TEST(TubeNestingPatternReuse, FixedPrefixStocksRetainTheirOriginalContinuationPath)
{
    const auto _Table = ReuseTestTable("reuse-fixed-prefix-v1");
    auto _Stock = StockType{"continued", "profile", StockKind::Remnant, 220, 1};
    _Stock.FixedInstanceID = "locked-stock";
    _Stock.InitialProcessedEnd = 100;
    _Stock.InitialPredecessorTypeID = "long";
    _Stock.InitialPredecessorRotationRadians = 0.25;
    const std::vector<StockType> _Stocks{_Stock, {"new", "profile", StockKind::Standard, 100}};
    const auto _Result = PairTableSolver{}.Solve(_Table, _Stocks, ReuseTestSettings());
    CheckLargeBatchPlan(_Result, _Table, _Stocks);
    EXPECT_EQ(_Result.Metrics.PatternCacheHits, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheImported, 0u);
    EXPECT_EQ(_Result.Metrics.PatternCacheStored, 0u);
}
