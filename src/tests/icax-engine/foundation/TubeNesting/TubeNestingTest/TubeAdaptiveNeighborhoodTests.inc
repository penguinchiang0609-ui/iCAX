#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace TubeAdaptiveNeighborhoodTestDetail
{
    using namespace iCAX::TubeNesting;

    struct ProbePlacement final
    {
        std::size_t Instance = 0;
        Direction PartDirection = Direction::Forward;
        bool operator==(const ProbePlacement&) const = default;
    };
    struct ProbeBar final
    {
        std::size_t Stock = 0;
        std::vector<ProbePlacement> Placements;
        bool operator==(const ProbeBar&) const = default;
    };
    struct ProbeInstance final { std::size_t Type = 0; };

    // Exercise the production selector directly with its small field contract;
    // this test does not copy the selection or scheduling implementation.
    #include <TubeNesting/TubeAdaptiveNeighborhood.inc>

    struct Fixture final
    {
        PairTableSnapshot Table;
        std::vector<ProbeInstance> Instances;
        std::vector<ProbeBar> Plan;
        std::vector<StockType> Stocks;
        PairTableSolverSettings Settings;

        Fixture(const std::vector<Length>& Lengths_,
            const std::vector<std::vector<std::size_t>>& Bars_, const Length StockLength_ = 1000)
        {
            for (std::size_t _I = 0; _I < Lengths_.size(); ++_I)
                Table.Types.push_back(PairPartType{std::to_string(_I), "same", {}, Lengths_[_I],
                    Lengths_[_I], 1, {true, false}});
            Table.Entries.resize(Table.StateCount() * Table.StateCount());
            for (auto& _Entry : Table.Entries) _Entry.Status = PairEntryStatus::Feasible;
            StockType _Stock;
            _Stock.ID = "stock";
            _Stock.CompatibilityKey = "same";
            _Stock.TotalLength = StockLength_;
            Stocks.push_back(_Stock);
            for (const auto& _Types : Bars_)
            {
                ProbeBar _Bar;
                for (const auto _Type : _Types)
                {
                    _Bar.Placements.push_back({Instances.size(), Direction::Forward});
                    Instances.push_back({_Type});
                }
                Plan.push_back(std::move(_Bar));
            }
            Settings.AdaptiveConstraintNeighborhoods = true;
            Settings.ConstraintNeighborhoodPartLimit = 9;
            Settings.ConstraintNeighborhoodMaximumPartLimit = 24;
            Settings.ConstraintNeighborhoodTimeBudgetMilliseconds = 100;
            Settings.ConstraintNeighborhoodMaximumTimeBudgetMilliseconds = 300;
        }

        PairEntry& Edge(const std::size_t A_, const std::size_t B_)
        {
            return Table.Entries[Table.EntryIndex(A_, Direction::Forward, B_, Direction::Forward)];
        }

        AdaptiveNeighborhoodSelection Select(AdaptiveNeighborhoodState& State_,
            std::mt19937& Random_, const std::uint64_t Remaining_ = 10000)
        {
            return SelectAdaptiveNeighborhood(Plan, Instances, Table, Stocks, Settings, Random_, State_, Remaining_);
        }
    };
}

TEST(TubeAdaptiveNeighborhood, StartsWithMostWastefulUnvisitedBar)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({30}, {{0, 0, 0}, {0}, {0, 0}}, 200);
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(11);
    const auto _Selection = _Fixture.Select(_State, _Random);
    ASSERT_FALSE(_Selection.BarIndices.empty());
    EXPECT_EQ(_Selection.BarIndices.front(), 1u);
    EXPECT_TRUE(_Selection.UseConstraintModel);
}

TEST(TubeAdaptiveNeighborhood, DirectedUsableConnectionsChooseComplementaryBar)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({60, 60, 60}, {{0}, {1}, {2}}, 100);
    _Fixture.Settings.ConstraintNeighborhoodPartLimit = 2;
    _Fixture.Settings.ConstraintNeighborhoodMaximumPartLimit = 2;
    _Fixture.Edge(0, 1).NetSaving = 1;
    _Fixture.Edge(0, 2).NetSaving = 30;
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(17);
    auto _Selection = _Fixture.Select(_State, _Random);
    ASSERT_EQ(_Selection.BarIndices.size(), 2u);
    EXPECT_EQ(_Selection.BarIndices[0], 0u);
    EXPECT_EQ(_Selection.BarIndices[1], 2u);
    _Fixture.Edge(0, 2).Status = PairEntryStatus::Forbidden;
    _State = {};
    _Selection = _Fixture.Select(_State, _Random);
    ASSERT_EQ(_Selection.BarIndices.size(), 2u);
    EXPECT_EQ(_Selection.BarIndices[1], 1u);
}

TEST(TubeAdaptiveNeighborhood, ComplementProbesIncludeFarEndTypesOfDiverseBars)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture(std::vector<Length>(9, 10), {{0}, std::vector<std::size_t>(8, 1),
        {1, 3, 4, 5, 6, 7, 8, 2}}, 1000);
    _Fixture.Settings.ConstraintNeighborhoodMaximumPartLimit = 9;
    _Fixture.Edge(0, 2).NetSaving = 5;
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(18);
    const auto _Selection = _Fixture.Select(_State, _Random);
    ASSERT_EQ(_Selection.BarIndices.size(), 2u);
    EXPECT_EQ(_Selection.BarIndices[0], 0u);
    EXPECT_EQ(_Selection.BarIndices[1], 2u);
}

TEST(TubeAdaptiveNeighborhood, OversizedSeedsRotateAndExposeOnlyLocalProbePositions)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    const std::vector<std::size_t> _Many(40, 0);
    Fixture _Fixture({1}, {_Many, _Many, _Many}, 1000);
    const auto _Before = _Fixture.Plan;
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(21);
    std::set<std::size_t> _Seeds;
    for (int _Round = 0; _Round < 3; ++_Round)
    {
        const auto _Selection = _Fixture.Select(_State, _Random);
        EXPECT_TRUE(_Selection.OversizedFallback);
        EXPECT_FALSE(_Selection.UseConstraintModel);
        ASSERT_FALSE(_Selection.BarIndices.empty());
        _Seeds.insert(_Selection.BarIndices.front());
        EXPECT_EQ(_Selection.PartCount, 40u);
        EXPECT_LE(_Selection.BarIndices.size(), 17u);
        EXPECT_LE(_Selection.FallbackProbePositions.size(), 16u);
        ASSERT_GE(_Selection.FallbackProbePositions.size(), 2u);
        for (const auto _Position : _Selection.FallbackProbePositions) EXPECT_LT(_Position, 40u);
        EXPECT_NE(std::find(_Selection.FallbackProbePositions.begin(),
            _Selection.FallbackProbePositions.end(), 39u), _Selection.FallbackProbePositions.end());
        RecordAdaptiveNeighborhoodOutcome(_State, false, false, 1);
    }
    EXPECT_EQ(_Seeds.size(), 3u);
    EXPECT_EQ(_Fixture.Plan, _Before);
}

TEST(TubeAdaptiveNeighborhood, MembershipCoverageSurvivesAnInternalOrderChange)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({1}, {std::vector<std::size_t>(40, 0)}, 1000);
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(22);
    const auto _First = _Fixture.Select(_State, _Random);
    ASSERT_TRUE(_First.OversizedFallback);
    std::reverse(_Fixture.Plan.front().Placements.begin(), _Fixture.Plan.front().Placements.end());
    const auto _Second = _Fixture.Select(_State, _Random);
    ASSERT_TRUE(_Second.OversizedFallback);
    ASSERT_EQ(_State.BarVisits.size(), 1u);
    EXPECT_EQ(_State.BarVisits.begin()->second, 2u);
    EXPECT_NE(_First.FallbackProbePositions, _Second.FallbackProbePositions);
}

TEST(TubeAdaptiveNeighborhood, StagnationExpandsButObservedOverrunShrinksTheModelAllowance)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({1}, std::vector<std::vector<std::size_t>>(30, {0}), 1000);
    _Fixture.Settings.ConstraintNeighborhoodPartLimit = 4;
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(24);
    const auto _Initial = _Fixture.Select(_State, _Random);
    RecordAdaptiveNeighborhoodOutcome(_State, false, false, 1);
    RecordAdaptiveNeighborhoodOutcome(_State, false, false, 1);
    const auto _Expanded = _Fixture.Select(_State, _Random);
    EXPECT_GT(_Expanded.PartLimit, _Initial.PartLimit);
    for (int _I = 0; _I < 10; ++_I) RecordAdaptiveNeighborhoodOutcome(_State, false, false, 1);
    const auto _Large = _Fixture.Select(_State, _Random);
    EXPECT_LE(_Large.BudgetMilliseconds, 300u);
    EXPECT_LE(_Large.PartLimit, 24u);
    RecordAdaptiveNeighborhoodOutcome(_State, false, true, _Large.BudgetMilliseconds * 8);
    RecordAdaptiveNeighborhoodOutcome(_State, false, true, _Large.BudgetMilliseconds * 8);
    const auto _AfterOverrun = _Fixture.Select(_State, _Random);
    EXPECT_LT(_AfterOverrun.PartLimit, _Large.PartLimit);
    EXPECT_LT(_State.ModelTermsPerMillisecond, 256.0);
}

TEST(TubeAdaptiveNeighborhood, RemainingWallClockAndContinuedStockDimensionsBoundTheSelection)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({1}, std::vector<std::vector<std::size_t>>(30, {0}), 1000);
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(30);
    const auto _NoBudget = _Fixture.Select(_State, _Random, 0);
    EXPECT_FALSE(_NoBudget.UseConstraintModel);
    EXPECT_FALSE(_NoBudget.OversizedFallback);
    EXPECT_TRUE(_NoBudget.BarIndices.empty());
    const auto _Short = _Fixture.Select(_State, _Random, 3);
    EXPECT_LE(_Short.BudgetMilliseconds + _Short.SelectionMilliseconds, 3u);
    if (_Short.UseConstraintModel)
        EXPECT_LE(_Short.EstimatedModelTerms,
            static_cast<std::uint64_t>(_Short.BudgetMilliseconds * _State.ModelTermsPerMillisecond));
    const auto _NormalTerms = AdaptiveNeighborhoodModelTerms(12, 100, 0);
    const auto _ContinuedTerms = AdaptiveNeighborhoodModelTerms(12, 100, 100);
    EXPECT_GT(_ContinuedTerms, _NormalTerms);
    EXPECT_EQ(AdaptiveNeighborhoodModelTerms((std::numeric_limits<std::size_t>::max)(), 100, 100),
        (std::numeric_limits<std::uint64_t>::max)());
}

TEST(TubeAdaptiveNeighborhood, LargeConfiguredLimitsCannotCreateAnUnboundedCpModel)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({1}, std::vector<std::vector<std::size_t>>(100, {0}), 1000);
    _Fixture.Settings.ConstraintNeighborhoodPartLimit = 1000;
    _Fixture.Settings.ConstraintNeighborhoodMaximumPartLimit = 1000;
    _Fixture.Settings.ConstraintNeighborhoodTimeBudgetMilliseconds = 300;
    _Fixture.Settings.ConstraintNeighborhoodMaximumTimeBudgetMilliseconds = 1000000;
    AdaptiveNeighborhoodState _State;
    _State.Stagnation = 16;
    std::mt19937 _Random(33);
    const auto _Selection = _Fixture.Select(_State, _Random);
    ASSERT_TRUE(_Selection.UseConstraintModel);
    EXPECT_LT(_Selection.PartCount, 100u);
    EXPECT_LE(_Selection.PartLimit, 48u);
    EXPECT_LE(_Selection.EstimatedModelTerms, 120000u);
    EXPECT_LE(_Selection.PartCount, _Selection.PartLimit);
}

TEST(TubeAdaptiveNeighborhood, FixedPrefixAndFiniteInventoryAreReadWithoutMutation)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({80, 180}, {{0}, {1}}, 200);
    _Fixture.Stocks.front().Quantity = 2;
    auto _Continued = _Fixture.Stocks.front();
    _Continued.ID = "opened";
    _Continued.FixedInstanceID = "physical-1";
    _Continued.Quantity = 1;
    _Continued.InitialProcessedEnd = 150;
    _Continued.InitialPredecessorType = 0;
    _Fixture.Stocks.push_back(_Continued);
    _Fixture.Plan.front().Stock = 1;
    _Fixture.Edge(0, 0).NetSaving = 60;
    AdaptiveNeighborhoodState _State;
    std::mt19937 _Random(31);
    const auto _Selection = _Fixture.Select(_State, _Random);
    ASSERT_FALSE(_Selection.BarIndices.empty());
    EXPECT_EQ(_Selection.BarIndices.front(), 0u); // 30/50 slack, including the predecessor saving.
    EXPECT_EQ(_Fixture.Stocks.front().Quantity, 2u);
    EXPECT_EQ(_Fixture.Stocks.back().Quantity, 1u);
    EXPECT_EQ(_Fixture.Stocks.back().InitialProcessedEnd, 150);
    EXPECT_EQ(_Fixture.Stocks.back().FixedInstanceID, "physical-1");
}

TEST(TubeAdaptiveNeighborhood, DisabledSwitchReproducesPreviousShuffleAndWholeBarSelection)
{
    using namespace TubeAdaptiveNeighborhoodTestDetail;
    Fixture _Fixture({1}, {std::vector<std::size_t>(5, 0), std::vector<std::size_t>(4, 0),
        std::vector<std::size_t>(3, 0), std::vector<std::size_t>(12, 0), {}}, 1000);
    _Fixture.Settings.AdaptiveConstraintNeighborhoods = false;
    std::mt19937 _Random(123), _ReferenceRandom(123);
    std::vector<std::size_t> _Order(_Fixture.Plan.size());
    std::iota(_Order.begin(), _Order.end(), 0);
    std::shuffle(_Order.begin(), _Order.end(), _ReferenceRandom);
    std::vector<std::size_t> _Expected;
    std::size_t _Count = 0;
    for (const auto _Bar : _Order)
    {
        const auto _Size = _Fixture.Plan[_Bar].Placements.size();
        if (_Size == 0 || _Size > 9 || (_Count && _Count + _Size > 9)) continue;
        _Expected.push_back(_Bar); _Count += _Size;
        if (_Count >= 9) break;
    }
    AdaptiveNeighborhoodState _State;
    const auto _Selection = _Fixture.Select(_State, _Random);
    EXPECT_EQ(_Selection.BarIndices, _Expected);
    EXPECT_EQ(_Selection.PartCount, _Count);
    EXPECT_EQ(_Selection.PartLimit, 9u);
    EXPECT_FALSE(_Selection.OversizedFallback);
    EXPECT_EQ(_Random(), _ReferenceRandom());
}
