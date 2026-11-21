namespace
{
    PairTableSnapshot ConsistentPoseTable()
    {
        auto _Table = ManualTable({
            PairPartType{"pose-A", "profile", "order", 100, 100, 1, {true, false}},
            PairPartType{"pose-B", "profile", "order", 40, 40, 1, {true, false}}});
        SetPair(_Table, 0, Direction::Forward, 1, Direction::Forward, 20, 0.25);
        auto& _Pose = _Table.Poses.back();
        _Pose.RequiredSeparation = 80;
        _Pose.ContinuousSeparationUpper = 79.25;
        _Pose.ContinuousSeparationLower = 78.75;
        _Pose.OptimalityGap = 0.5;
        return _Table;
    }

    PairTableSolverSettings PoseConsistencySettings()
    {
        auto _Settings = LargeBatchTestSettings();
        _Settings.PatternMasterTimeBudgetMilliseconds = 0;
        return _Settings;
    }
}

TEST(TubeNestingPairConsistency, BuilderQuantizesSeparationAndRetainsExtraProcessLoss)
{
    PairTableBuildInput _Input;
    _Input.Types = {
        PairPartType{"quantized-A", "profile", "order", 100, 100, 1, {true, false}},
        PairPartType{"quantized-B", "profile", "order", 80, 80, 1, {true, false}}};
    PairTypeGeometry _A{NativeProfile(0.0), NativeProfile(99.25), {}};
    PairTypeGeometry _B{NativeProfile(1.0), NativeProfile(79.5), {}};
    _Input.Geometry = {_A, _B};
    _Input.PairProcessLosses.assign(16, 3);
    const auto _Table = BuildPairTable(_Input);
    ASSERT_TRUE(ValidatePairTable(_Table));
    const auto& _Entry = _Table.At(0, Direction::Forward, 1, Direction::Forward);
    const auto& _Pose = _Table.Poses[_Entry.PoseMetadataIndex];
    EXPECT_DOUBLE_EQ(_Pose.ContinuousSeparationUpper, 98.25);
    EXPECT_EQ(_Pose.RequiredSeparation, 99);
    EXPECT_EQ(_Entry.NetSaving, -2);

    const auto _Result = PairTableSolver{}.Solve(_Table,
        {StockType{"stock", "profile", StockKind::Standard, 500}}, PoseConsistencySettings());
    ASSERT_TRUE(_Result.UnplacedInstances.empty());
    ASSERT_EQ(_Result.Stocks.size(), 1u);
    const auto& _Placements = _Result.Stocks.front().Placements;
    ASSERT_EQ(_Placements.size(), 2u);
    ASSERT_EQ(_Placements[0].DemandID, "quantized-A");
    EXPECT_EQ(_Placements[1].Start - _Placements[0].Start, 102);
    EXPECT_GE(_Placements[1].Start - _Placements[0].Start, _Pose.RequiredSeparation);
    EXPECT_FALSE(_Placements[1].CommonCutWithPrevious);
}

TEST(TubeNestingPairConsistency, RejectsStaleSavingThatConsumesLessThanTheReferencedPose)
{
    auto _Table = ConsistentPoseTable();
    ASSERT_TRUE(ValidatePairTable(_Table));
    const auto _Version = _Table.Version;
    auto& _Entry = _Table.Entries[_Table.EntryIndex(
        0, Direction::Forward, 1, Direction::Forward)];
    ++_Entry.NetSaving;
    EXPECT_EQ(_Table.Version, _Version);
    EXPECT_FALSE(ValidatePairTable(_Table));
    const auto _Result = PairTableSolver{}.Solve(_Table,
        {StockType{"stock", "profile", StockKind::Standard, 200}}, PoseConsistencySettings());
    EXPECT_EQ(_Result.Status, SolveStatus::InvalidInput);
    EXPECT_TRUE(_Result.Stocks.empty());
}

TEST(TubeNestingPairConsistency, RejectsAnIntegerSeparationRoundedBelowItsContinuousBound)
{
    auto _Table = ConsistentPoseTable();
    --_Table.Poses.back().RequiredSeparation;
    EXPECT_FALSE(ValidatePairTable(_Table));
    _Table.Poses.back().RequiredSeparation = 80;
    EXPECT_TRUE(ValidatePairTable(_Table));
}

TEST(TubeNestingPairConsistency, RejectsAnUnderstatedPoseGapWithoutInventingALengthTolerance)
{
    auto _Table = ConsistentPoseTable();
    _Table.Poses.back().OptimalityGap = std::nextafter(0.5, 0.0);
    EXPECT_FALSE(ValidatePairTable(_Table));
    _Table.Poses.back().OptimalityGap = 0.5;
    EXPECT_TRUE(ValidatePairTable(_Table));
}

TEST(TubeNestingPairConsistency, KeepsConservativeManualTablesAndUnknownGeometryFeasible)
{
    auto _Table = ConsistentPoseTable();
    auto& _Entry = _Table.Entries[_Table.EntryIndex(
        0, Direction::Forward, 1, Direction::Forward)];
    _Entry.Status = PairEntryStatus::Feasible;
    _Entry.NetSaving = 18;
    auto& _Pose = _Table.Poses.back();
    _Pose.RequiredSeparation = 81;
    _Pose.OptimalityGap = 1.0;
    _Pose.SourceQuality = ProfileSourceQuality::Missing;
    for (const auto _Status : {PairPoseStatus::BestFeasible,
        PairPoseStatus::CertifiedEpsilonOptimal, PairPoseStatus::BudgetExhausted})
    {
        _Pose.PoseStatus = _Status;
        EXPECT_TRUE(ValidatePairTable(_Table));
    }
    const auto _Result = PairTableSolver{}.Solve(_Table,
        {StockType{"stock", "profile", StockKind::Standard, 200}}, PoseConsistencySettings());
    ASSERT_EQ(_Result.Stocks.size(), 1u);
    ASSERT_EQ(_Result.Stocks.front().Placements.size(), 2u);
    EXPECT_EQ(_Result.Stocks.front().Placements[1].Start, 82);
    EXPECT_FALSE(_Result.ExactSearchCompleted);
}

TEST(TubeNestingPairConsistency, RejectsUnusablePoseStatusesForOtherwiseFeasibleEntries)
{
    auto _Table = ConsistentPoseTable();
    _Table.Entries[_Table.EntryIndex(0, Direction::Forward, 1,
        Direction::Forward)].Status = PairEntryStatus::Feasible;
    for (const auto _Status : {PairPoseStatus::InvalidInput,
        PairPoseStatus::Forbidden, PairPoseStatus::Unsupported})
    {
        _Table.Poses.back().PoseStatus = _Status;
        EXPECT_FALSE(ValidatePairTable(_Table));
    }
}

TEST(TubeNestingPairConsistency, ComparesQuantizedIntegersExactlyAboveDoubleIntegerPrecision)
{
    auto _Table = ConsistentPoseTable();
    constexpr Length _Bound = (Length{1} << 53) + 4;
    for (auto& _Type : _Table.Types)
        _Type.MaximumLength = _Type.MaterialLength = Length{1} << 54;
    _Table.Entries[_Table.EntryIndex(0, Direction::Forward, 1,
        Direction::Forward)].NetSaving = 0;
    auto& _Pose = _Table.Poses.back();
    _Pose.ContinuousSeparationUpper = _Pose.ContinuousSeparationLower = static_cast<double>(_Bound);
    _Pose.OptimalityGap = 0.0;
    _Pose.RequiredSeparation = _Bound - 1;
    EXPECT_FALSE(ValidatePairTable(_Table));
    _Pose.RequiredSeparation = _Bound;
    EXPECT_TRUE(ValidatePairTable(_Table));
}

TEST(TubeNestingPairConsistency, RejectsAContinuousBoundOutsideTheIntegerRepresentation)
{
    auto _Table = ConsistentPoseTable();
    for (auto& _Type : _Table.Types)
        _Type.MaximumLength = _Type.MaterialLength = (std::numeric_limits<Length>::max)();
    _Table.Entries[_Table.EntryIndex(0, Direction::Forward, 1,
        Direction::Forward)].NetSaving = 0;
    auto& _Pose = _Table.Poses.back();
    _Pose.RequiredSeparation = (std::numeric_limits<Length>::max)();
    _Pose.ContinuousSeparationUpper = _Pose.ContinuousSeparationLower =
        static_cast<double>((std::numeric_limits<Length>::max)());
    _Pose.OptimalityGap = 0.0;
    EXPECT_FALSE(ValidatePairTable(_Table));
}
