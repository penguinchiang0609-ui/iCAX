#include "pch.h"

#include <gtest/gtest.h>

#include "TubeNesting/TubeNesting.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <numbers>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace iCAX::TubeNesting;

    PartVariant SawVariant(
        const std::string& _ID,
        const std::string& _LeftSignature,
        const std::string& _RightSignature,
        const bool _Reversed = false)
    {
        PartVariant Result;
        Result.ID = _ID;
        Result.LeftEnd = EndDescriptor{_LeftSignature, 0, true};
        Result.RightEnd = EndDescriptor{_RightSignature, 0, true};
        Result.Reversed = _Reversed;
        return Result;
    }

    SolverSettings ExactSettings()
    {
        SolverSettings Result;
        Result.Kerf = 3;
        Result.PartGap = 4;
        Result.ConstructionRuns = 4;
        Result.LocalSearchPasses = 3;
        Result.LargeNeighborhoodIterations = 20;
        Result.ExactPartLimit = 12;
        Result.ExactNodeLimit = 200000;
        return Result;
    }

    PeriodicEndProfile NativeProfile(
        const double Constant_, std::vector<double> Cosine_ = {},
        std::vector<double> Sine_ = {})
    {
        const auto _Order = (std::max)(Cosine_.size(), Sine_.size());
        Cosine_.resize(_Order);
        Sine_.resize(_Order);
        FourierLevel _Level;
        _Level.Order = _Order;
        _Level.Constant = Constant_;
        _Level.Cosine = std::move(Cosine_);
        _Level.Sine = std::move(Sine_);
        for (std::size_t _Index = 0; _Index < _Order; ++_Index)
        {
            const auto _Harmonic = static_cast<double>(_Index + 1);
            const auto _Amplitude = std::hypot(
                _Level.Cosine[_Index], _Level.Sine[_Index]);
            _Level.FirstDerivativeUpper += _Harmonic * _Amplitude;
            _Level.SecondDerivativeUpper += _Harmonic * _Harmonic * _Amplitude;
        }
        _Level.Certified = true;
        PeriodicEndProfile _Profile;
        _Profile.SourceQuality = ProfileSourceQuality::NativeCertified;
        _Profile.CompleteSingleValued = true;
        _Profile.Levels.push_back(std::move(_Level));
        return _Profile;
    }

    PairTypeGeometry FlatGeometry(const Length Length_)
    {
        PairTypeGeometry _Geometry;
        _Geometry.Left = NativeProfile(0.0);
        _Geometry.Right = NativeProfile(static_cast<double>(Length_));
        return _Geometry;
    }

    PairTableSnapshot ManualTable(std::vector<PairPartType> Types_)
    {
        PairTableSnapshot _Table;
        _Table.Version = "test-table";
        _Table.Types = std::move(Types_);
        _Table.Entries.resize(_Table.StateCount() * _Table.StateCount());
        for (auto& _Entry : _Table.Entries)
            _Entry.Status = PairEntryStatus::Forbidden;
        _Table.Poses.push_back({});
        return _Table;
    }

    void SetPair(
        PairTableSnapshot& Table_, const std::size_t TypeA_, const Direction DirectionA_,
        const std::size_t TypeB_, const Direction DirectionB_, const Length Saving_,
        const double RelativeRotation_ = 0.0)
    {
        PairPoseMetadata _Pose;
        _Pose.RelativeRotationRadians = RelativeRotation_;
        _Pose.OptimalityGap = 0.0;
        _Pose.PoseStatus = PairPoseStatus::CertifiedEpsilonOptimal;
        _Pose.SourceQuality = ProfileSourceQuality::NativeCertified;
        Table_.Poses.push_back(_Pose);
        auto& _Entry = Table_.Entries[Table_.EntryIndex(
            TypeA_, DirectionA_, TypeB_, DirectionB_)];
        _Entry.Status = PairEntryStatus::Certified;
        _Entry.NetSaving = Saving_;
        _Entry.PoseMetadataIndex = static_cast<std::uint32_t>(Table_.Poses.size() - 1);
    }

    PairTableSolverSettings PairExactSettings()
    {
        PairTableSolverSettings _Settings;
        _Settings.Constraint = ConstraintMode::FullExact;
        _Settings.ConstructionRuns = 4;
        _Settings.LocalSearchPasses = 2;
        _Settings.ExactPartLimit = 10;
        _Settings.ExactNodeLimit = 100000;
        _Settings.FiniteInventoryLabelLimit = 100000;
        return _Settings;
    }
}

TEST(TubeNesting, AccountsForOneSharedSawKerfBetweenSquareParts)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"rail", "steel|rect40x20x2", "order-1", 1000, 2, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock-2006", "steel|rect40x20x2", StockKind::Standard, 2006},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Metrics.PartLength, 2000);
    EXPECT_EQ(Result.Metrics.GapLength, 6);
    EXPECT_EQ(Result.Metrics.CutCount, 2U);
    EXPECT_EQ(Result.Metrics.CommonCutCount, 1U);
    EXPECT_EQ(Result.Stocks.front().RemainingLength, 0);
}

TEST(TubeNesting, EncodesPeriodicCutLineAndFindsCyclicPhase)
{
    const auto Tail = EncodeCutLineFeature(160, { 0, 10, 0, 10 });
    const auto Head = EncodeCutLineFeature(160, { 10, 0, 10, 0 });

    ASSERT_TRUE(IsValidCutLineFeature(Tail));
    ASSERT_TRUE(IsValidCutLineFeature(Head));
    EXPECT_EQ(Tail.Kind, CutLineFeatureKind::Sampled);
    EXPECT_FALSE(Tail.ClassKey.empty());
    EXPECT_NE(Tail.Fingerprint, 0U);

    const auto Unshifted = EvaluateCutLineMatch(Tail, Head, 0);
    const auto Shifted = EvaluateCutLineMatch(Tail, Head, 40);
    ASSERT_TRUE(Unshifted.Valid);
    ASSERT_TRUE(Shifted.Valid);
    EXPECT_EQ(Unshifted.RequiredSeparation, 10);
    EXPECT_EQ(Shifted.RequiredSeparation, 0);

    const auto Best = FindBestCutLineMatch(Tail, Head, 16);
    ASSERT_TRUE(Best.Valid);
    EXPECT_EQ(Best.RequiredSeparation, 0);
    EXPECT_EQ(Best.PhaseOffset, 40);
}

TEST(TubeNesting, TreatsCompressedLinearFeaturesAsPeriodicAtTheSeam)
{
    const auto Feature = EncodeCutLineFeature(100, { 0, 25, 50, 75, 100 });
    ASSERT_EQ(Feature.Kind, CutLineFeatureKind::Linear);
    const auto Match = EvaluateCutLineMatch(Feature, Feature, 20);
    ASSERT_TRUE(Match.Valid);
    // The 20-unit phase crosses the U seam.  The wrapped discontinuity needs
    // the full 80-unit separation; clamping the linear code would incorrectly
    // report zero here.
    EXPECT_EQ(Match.RequiredSeparation, 80);
}

TEST(TubeNesting, UsesEncodedCutLineCostAsTransitionAllowance)
{
    const auto MakeConstant = [](const Length Value_) {
        return EncodeCutLineFeature(160, { Value_, Value_, Value_, Value_ });
    };
    PartVariant A;
    A.ID = "a";
    A.RightEnd.Feature = MakeConstant(20);
    A.LeftEnd.Feature = MakeConstant(0);
    PartVariant B;
    B.ID = "b";
    B.LeftEnd.Feature = MakeConstant(5);
    B.RightEnd.Feature = MakeConstant(100);
    const std::vector<PartDemand> Demands{
        PartDemand{"a", "steel|rect", "order-1", 100, 1, {A}},
        PartDemand{"b", "steel|rect", "order-1", 100, 1, {B}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock", "steel|rect", StockKind::Standard, 215},
    };
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    ASSERT_EQ(Result.Stocks.front().Placements.size(), 2U);
    EXPECT_EQ(Result.Stocks.front().Placements[0].DemandID, "a");
    EXPECT_EQ(Result.Stocks.front().Placements[1].DemandID, "b");
    EXPECT_EQ(Result.Stocks.front().Placements[1].GapBefore, 15);
}

TEST(TubeNesting, ChoosesOrientationThatAllowsMiterCommonCut)
{
    const PartVariant A = SawVariant("fixed", "square", "miter-45");
    const PartVariant BWrong = SawVariant("forward", "miter+45", "square");
    const PartVariant BRight = SawVariant("reversed", "miter-45", "square", true);
    const std::vector<PartDemand> Demands{
        PartDemand{"a", "steel|rect", "order-1", 1000, 1, {A}},
        PartDemand{"b", "steel|rect", "order-1", 1000, 1, {BWrong, BRight}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock", "steel|rect", StockKind::Standard, 2006},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    ASSERT_EQ(Result.Stocks.front().Placements.size(), 2U);
    EXPECT_EQ(Result.Metrics.CommonCutCount, 1U);
    EXPECT_TRUE(std::any_of(
        Result.Stocks.front().Placements.begin(), Result.Stocks.front().Placements.end(),
        [](const Placement& _Placement)
        {
            return _Placement.DemandID == "b" && _Placement.VariantID == "reversed";
        }));
}

TEST(TubeNesting, ExactlyResequencesAnglesAfterHeuristicConstruction)
{
    const PartVariant Wrong = SawVariant("a-wrong", "square", "miter+45");
    const PartVariant Right = SawVariant("z-right", "square", "miter-45");
    const PartVariant Mate = SawVariant("fixed", "miter-45", "square");
    const std::vector<PartDemand> Demands{
        PartDemand{"a", "steel|rect", "order-1", 1000, 1, {Wrong, Right}},
        PartDemand{"b", "steel|rect", "order-1", 1000, 1, {Mate}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock", "steel|rect", StockKind::Standard, 2010},
    };
    SolverSettings Settings = ExactSettings();
    Settings.ConstructionRuns = 1;
    Settings.LocalSearchPasses = 0;
    Settings.LargeNeighborhoodIterations = 0;
    Settings.ExactPartLimit = 0;
    Settings.ExactNodeLimit = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    ASSERT_EQ(Result.Status, SolveStatus::Feasible);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Metrics.CommonCutCount, 1U);
    EXPECT_TRUE(std::any_of(
        Result.Stocks.front().Placements.begin(), Result.Stocks.front().Placements.end(),
        [](const Placement& _Placement)
        {
            return _Placement.DemandID == "a" && _Placement.VariantID == "z-right";
        }));
}

TEST(TubeNesting, OverlapsCompatibleTrapezoidEndEnvelopes)
{
    PartVariant A;
    A.ID = "a";
    A.AxialLength = 120;
    A.MaterialLength = 110;
    A.RightEnd.NestingProjection = 10;
    A.RightEnd.NestingPlane = "plane-y/positive";
    A.RightEnd.AllowTrapezoidNesting = true;
    PartVariant B;
    B.ID = "b";
    B.AxialLength = 130;
    B.MaterialLength = 115;
    B.LeftEnd.NestingProjection = 15;
    B.LeftEnd.NestingPlane = "plane-y/positive";
    B.LeftEnd.AllowTrapezoidNesting = true;
    const std::vector<PartDemand> Demands{
        PartDemand{"a", "steel|rect", "order-1", 120, 1, {A}},
        PartDemand{"b", "steel|rect", "order-1", 130, 1, {B}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock", "steel|rect", StockKind::Standard, 240},
    };
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    ASSERT_EQ(Result.Stocks.front().Placements.size(), 2U);
    EXPECT_EQ(Result.Stocks.front().Placements[0].DemandID, "a");
    EXPECT_EQ(Result.Stocks.front().Placements[1].DemandID, "b");
    EXPECT_EQ(Result.Stocks.front().Placements[1].GapBefore, -10);
    EXPECT_EQ(Result.Stocks.front().Placements[1].Start, 110);
    EXPECT_EQ(Result.Stocks.front().Placements[1].End, 240);
    EXPECT_EQ(Result.Metrics.PartLength, 225);
    EXPECT_EQ(Result.Metrics.GapLength, 15);
    EXPECT_LT(Result.Metrics.GrossUtilization, 1.0);
}

TEST(TubeNesting, ReservesSafetyAllowanceForApproximatedNonCommonEnd)
{
    PartVariant A = SawVariant("safe-blank", "approx-plane-a-left", "approx-plane-a-right");
    A.LeftEnd.AllowCommonCut = false;
    A.LeftEnd.SeparationAllowance = 10;
    A.RightEnd.AllowCommonCut = false;
    A.RightEnd.SeparationAllowance = 10;
    const PartVariant B = SawVariant("square", "square", "square");
    const std::vector<PartDemand> Demands{
        PartDemand{"a", "steel|rect", "order-1", 1000, 1, {A}},
        PartDemand{"b", "steel|rect", "order-1", 1000, 1, {B}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock", "steel|rect", StockKind::Standard, 2023},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Metrics.GapLength, 23);
    EXPECT_EQ(Result.Metrics.CutCount, 3U);
    EXPECT_EQ(Result.Metrics.CommonCutCount, 0U);
    EXPECT_EQ(Result.Stocks.front().RemainingLength, 0);
}

TEST(TubeNesting, UsesRemnantBeforePurchasingStandardStock)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"post", "steel|round25", "order-1", 1000, 1, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"remnant-1100", "steel|round25", StockKind::Remnant, 1100, 1},
        StockType{"standard-6000", "steel|round25", StockKind::Standard, 6000},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Stocks.front().StockTypeID, "remnant-1100");
    EXPECT_EQ(Result.Metrics.StandardStockCount, 0U);
}

TEST(TubeNesting, SeparatesMaterialAndProfileCompatibilityGroups)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"rect-part", "steel|rect40", "A", 900, 1, {}},
        PartDemand{"round-part", "steel|round25", "A", 900, 1, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"rect-stock", "steel|rect40", StockKind::Standard, 6000},
        StockType{"round-stock", "steel|round25", StockKind::Standard, 6000},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 2U);
    EXPECT_EQ(Result.Metrics.StandardStockCount, 2U);
    EXPECT_NE(Result.Stocks[0].CompatibilityKey, Result.Stocks[1].CompatibilityKey);
}

TEST(TubeNesting, RespectsTailDeadZoneAndSelectsFeasibleStock)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"part", "steel|rect", "A", 1000, 1, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"too-short-after-dead-zone", "steel|rect", StockKind::Remnant, 1100, 1, 0, 0, 100},
        StockType{"feasible-remnant", "steel|rect", StockKind::Remnant, 1200, 1, 0, 0, 100},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Stocks.front().StockTypeID, "feasible-remnant");
    EXPECT_GE(Result.Stocks.front().RemainingLength, 100);
}

TEST(TubeNesting, UsesLexicographicSetupTieBreakForEqualMaterialCost)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"step", "steel|rect", "stairs", 1497, 4, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock-3000", "steel|rect", StockKind::Standard, 3000},
        StockType{"stock-6000", "steel|rect", StockKind::Standard, 6000},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Stocks.front().StockTypeID, "stock-6000");
    EXPECT_EQ(Result.Stocks.front().Placements.size(), 4U);
}

TEST(TubeNesting, OptimizesWholeCuttingPatternsAcrossAlternativeStockLengths)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"repeated-post", "steel|rect", "order-1", 5000, 12, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "steel|rect", StockKind::Standard, 6000},
        StockType{"stock-15000", "steel|rect", StockKind::Standard, 15000},
    };
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 0;
    Settings.ConstructionRuns = 1;
    Settings.LocalSearchPasses = 0;
    Settings.LargeNeighborhoodIterations = 0;
    Settings.ExactPartLimit = 0;
    Settings.ExactNodeLimit = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    EXPECT_TRUE(Result.ExactSearchCompleted);
    ASSERT_EQ(Result.Stocks.size(), 4U);
    EXPECT_EQ(Result.Metrics.TotalStockLength, 60000);
    EXPECT_TRUE(Result.Metrics.QualityLowerBoundAvailable);
    EXPECT_EQ(Result.Metrics.StockCostLowerBound, 60000);
    EXPECT_EQ(Result.Metrics.OpenedStockCountLowerBound, 4U);
    EXPECT_DOUBLE_EQ(Result.Metrics.StockCostRelativeGap, 0.0);
    EXPECT_DOUBLE_EQ(Result.Metrics.OpenedStockCountRelativeGap, 0.0);
    EXPECT_TRUE(std::all_of(
        Result.Stocks.begin(), Result.Stocks.end(),
        [](const StockPlan& _Stock)
        {
            return _Stock.StockTypeID == "stock-15000" && _Stock.Placements.size() == 3U;
        }));
}

TEST(TubeNesting, HandlesInteractiveScaleStraightOrderDeterministically)
{
    std::vector<PartDemand> Demands;
    Demands.reserve(82);
    for (std::size_t Index = 0; Index < 82; ++Index)
    {
        PartVariant Variant;
        Variant.AxialLength = 420 + static_cast<Length>((Index * 137) % 781);
        Variant.LeftEnd.AllowCommonCut = false;
        Variant.RightEnd.AllowCommonCut = false;
        Demands.push_back(PartDemand{
            "part-" + std::to_string(Index), "steel|rect", "order-1",
            Variant.AxialLength, 1, {Variant}});
    }
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "steel|rect", StockKind::Standard, 6000},
    };
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 5;
    Settings.ConstructionRuns = 6;
    Settings.LocalSearchPasses = 3;
    Settings.LargeNeighborhoodIterations = 32;
    Settings.ExactPartLimit = 10;

    const SolveResult First = Solver{}.Solve(Demands, Stocks, Settings);
    const SolveResult Second = Solver{}.Solve(Demands, Stocks, Settings);

    ASSERT_EQ(First.Status, SolveStatus::Optimal);
    EXPECT_TRUE(First.ExactSearchCompleted);
    EXPECT_TRUE(First.UnplacedInstances.empty());
    EXPECT_EQ(First.Metrics.PartLength, Second.Metrics.PartLength);
    EXPECT_EQ(First.Metrics.TotalStockLength, Second.Metrics.TotalStockLength);
    ASSERT_EQ(First.Stocks.size(), Second.Stocks.size());
    std::size_t PlacementCount = 0;
    for (std::size_t Index = 0; Index < First.Stocks.size(); ++Index)
    {
        PlacementCount += First.Stocks[Index].Placements.size();
        EXPECT_EQ(First.Stocks[Index].RemainingLength, Second.Stocks[Index].RemainingLength);
        EXPECT_LE(First.Stocks[Index].ProcessedEnd, First.Stocks[Index].TotalLength);
    }
    EXPECT_EQ(PlacementCount, 82U);
}

TEST(TubeNesting, HandlesLargeProductionBatchWithoutTimingOut)
{
    std::vector<PartDemand> Demands;
    Demands.reserve(88);
    for (std::size_t Index = 0; Index < 88; ++Index)
    {
        PartVariant Variant;
        Variant.AxialLength = 420 + static_cast<Length>((Index * 137) % 781);
        Variant.LeftEnd.AllowCommonCut = false;
        Variant.RightEnd.AllowCommonCut = false;
        PartVariant Alternate = Variant;
        Alternate.ID = "alternate";
        Alternate.Reversed = true;
        Demands.push_back(PartDemand{
            "part-" + std::to_string(Index), "steel|rect", "order-1",
            Variant.AxialLength, 50, {Variant, Alternate}});
    }
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "steel|rect", StockKind::Standard, 6000},
    };
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 5;
    Settings.ConstructionRuns = 1;
    Settings.LocalSearchPasses = 0;
    Settings.LargeNeighborhoodIterations = 0;
    Settings.ExactPartLimit = 0;
    Settings.ExactNodeLimit = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    EXPECT_EQ(Result.Status, SolveStatus::Feasible);
    EXPECT_FALSE(Result.ExactSearchCompleted);
    EXPECT_TRUE(Result.UnplacedInstances.empty());
    EXPECT_GE(Result.Metrics.GrossUtilization, 0.95);
    EXPECT_TRUE(std::any_of(
        Result.Diagnostics.begin(), Result.Diagnostics.end(),
        [](const std::string& _Diagnostic)
        {
            return _Diagnostic.find("scalable best-fit") != std::string::npos;
        }));
    std::size_t PlacementCount = 0;
    for (const StockPlan& Stock : Result.Stocks) PlacementCount += Stock.Placements.size();
    EXPECT_EQ(PlacementCount, 88U * 50U);
}

TEST(TubeNesting, HandlesFiveFaceProductionBatchWithEncodedCutLines)
{
    struct FixturePart
    {
        const char* Profile;
        Length PartLength;
    };
    const std::array<FixturePart, 82> Fixture{
        FixturePart{"38x25", 1800}, FixturePart{"38x25", 1800},
        FixturePart{"38x25", 1800}, FixturePart{"38x25", 1800},
        FixturePart{"38x25", 595}, FixturePart{"38x25", 595},
        FixturePart{"22x22", 595}, FixturePart{"22x22", 595},
        FixturePart{"22x22", 595}, FixturePart{"22x22", 595},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"38x25", 1189}, FixturePart{"38x25", 1189},
        FixturePart{"22x22", 1189}, FixturePart{"22x22", 1189},
        FixturePart{"22x22", 491}, FixturePart{"22x22", 98},
        FixturePart{"22x22", 491}, FixturePart{"22x22", 98},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"round19", 52}, FixturePart{"round19", 892},
        FixturePart{"round19", 52}, FixturePart{"round19", 892},
        FixturePart{"round19", 52}, FixturePart{"round19", 892},
        FixturePart{"round19", 52}, FixturePart{"round19", 892},
        FixturePart{"round19", 52}, FixturePart{"round19", 892},
        FixturePart{"38x25", 589}, FixturePart{"38x25", 589},
        FixturePart{"22x22", 589}, FixturePart{"22x22", 589},
        FixturePart{"22x22", 589}, FixturePart{"22x22", 589},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"round19", 1744}, FixturePart{"round19", 1744},
        FixturePart{"38x25", 1195}, FixturePart{"22x22", 1195},
        FixturePart{"22x22", 1195}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"38x25", 1195}, FixturePart{"22x22", 1195},
        FixturePart{"22x22", 1195}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"round19", 589}, FixturePart{"round19", 589},
        FixturePart{"25x25", 600}, FixturePart{"25x25", 600},
        FixturePart{"25x25", 800}, FixturePart{"25x25", 800},
        FixturePart{"20x20-r15", 549}, FixturePart{"20x20-r15", 549},
        FixturePart{"20x20-r15", 749}, FixturePart{"20x20-r15", 749},
        FixturePart{"20x20-r08", 529}, FixturePart{"20x20-r08", 529},
        FixturePart{"16x16-r08", 729}, FixturePart{"16x16-r08", 729},
    };

    std::vector<PartDemand> Demands;
    Demands.reserve(Fixture.size());
    for (std::size_t Index = 0; Index < Fixture.size(); ++Index)
    {
        const Length Offset = static_cast<Length>(10 + Index % 7);
        const auto Feature = EncodeCutLineFeature(1900, { 0, Offset, 0, Offset });
        PartVariant Forward;
        Forward.ID = "forward-0";
        Forward.AxialLength = Fixture[Index].PartLength;
        Forward.MaterialLength = Fixture[Index].PartLength;
        Forward.LeftEnd.Feature = Feature;
        Forward.RightEnd.Feature = Feature;
        PartVariant Reverse = Forward;
        Reverse.ID = "forward-180";
        Reverse.Reversed = true;
        Reverse.RotationRadians = 3.14159265358979323846;
        Reverse.PhaseOffset = 950;
        Demands.push_back(PartDemand{
            "part-" + std::to_string(Index), Fixture[Index].Profile, "order-1",
            Fixture[Index].PartLength, 50, { Forward, Reverse }});
    }

    std::vector<StockType> Stocks;
    for (const auto* Profile : { "38x25", "22x22", "round19", "25x25",
                                  "20x20-r15", "20x20-r08", "16x16-r08" })
        Stocks.push_back(StockType{
            std::string(Profile) + "-stock", Profile, StockKind::Standard, 6000 });

    SolverSettings Settings = ExactSettings();
    Settings.Kerf = 0;
    Settings.PartGap = 5;
    Settings.ConstructionRuns = 1;
    Settings.LocalSearchPasses = 0;
    Settings.LargeNeighborhoodIterations = 0;
    Settings.ExactPartLimit = 0;
    Settings.ExactNodeLimit = 0;

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, Settings);

    EXPECT_EQ(Result.Status, SolveStatus::Feasible);
    EXPECT_TRUE(Result.UnplacedInstances.empty());
    EXPECT_FALSE(Result.ExactSearchCompleted);
    EXPECT_GE(Result.Metrics.GrossUtilization, 0.90);
    std::size_t PlacementCount = 0;
    for (const StockPlan& Stock : Result.Stocks) PlacementCount += Stock.Placements.size();
    EXPECT_EQ(PlacementCount, Fixture.size() * 50U);
}

TEST(TubeNesting, ReportsPartThatCannotFitAnyStock)
{
    const std::vector<PartDemand> Demands{
        PartDemand{"oversize", "steel|rect", "A", 6100, 1, {}},
    };
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "steel|rect", StockKind::Standard, 6000},
    };

    const SolveResult Result = Solver{}.Solve(Demands, Stocks, ExactSettings());

    EXPECT_EQ(Result.Status, SolveStatus::Infeasible);
    ASSERT_EQ(Result.UnplacedInstances.size(), 1U);
    EXPECT_EQ(Result.UnplacedInstances.front(), "oversize#0001");
}

TEST(TubeNesting, RejectsFloatingPointStyleInvalidInputAtBoundary)
{
    SolverSettings Settings = ExactSettings();
    Settings.Kerf = -1;
    const SolveResult Result = Solver{}.Solve(
        {PartDemand{"part", "steel|rect", "A", 1000, 1, {}}},
        {StockType{"stock", "steel|rect", StockKind::Standard, 6000}},
        Settings);

    EXPECT_EQ(Result.Status, SolveStatus::InvalidInput);
    EXPECT_FALSE(Result.Diagnostics.empty());
}

TEST(TubeNestingPairPose, SolvesCertifiedSingleHarmonicPhaseWithoutAngleStepping)
{
    const PairPartType A{"a", "profile", "order", 100, 100, 1, {true, false}};
    const PairPartType B{"b", "profile", "order", 100, 100, 1, {true, false}};
    PairTypeGeometry GeometryA = FlatGeometry(100);
    PairTypeGeometry GeometryB = FlatGeometry(100);
    GeometryA.Right = NativeProfile(100.0, {2.0});
    GeometryB.Left = NativeProfile(0.0, {1.0});
    PairPoseSolverSettings Settings;
    Settings.Tolerance = 0.01;
    Settings.FixedPhaseTolerance = 0.001;

    const auto Result = SolveOrientedPairPose(
        A, GeometryA, Direction::Forward,
        B, GeometryB, Direction::Forward, {}, Settings);

    EXPECT_EQ(Result.Status, PairPoseStatus::CertifiedEpsilonOptimal);
    EXPECT_NEAR(Result.RequiredSeparationUpper, 101.0, 0.02);
    EXPECT_LT((std::min)(Result.RelativeRotationRadians,
        2.0 * std::numbers::pi_v<double> - Result.RelativeRotationRadians), 0.1);
    EXPECT_LE(Result.OptimalityGap, Settings.Tolerance);
}

TEST(TubeNestingPairPose, OptimizesTheEnvelopeInsteadOfOnlyCorrelatingDominantPhase)
{
    const PairPartType A{"a", "profile", "order", 100, 100, 1, {true, false}};
    const PairPartType B{"b", "profile", "order", 100, 100, 1, {true, false}};
    PairTypeGeometry GeometryA = FlatGeometry(100);
    PairTypeGeometry GeometryB = FlatGeometry(100);
    GeometryA.Right = NativeProfile(100.0, {2.0, 1.0});
    GeometryB.Left = NativeProfile(0.0, {2.0, -1.0});
    PairPoseSolverSettings Settings;
    Settings.Tolerance = 0.02;
    Settings.FixedPhaseTolerance = 0.002;

    const auto Result = SolveOrientedPairPose(
        A, GeometryA, Direction::Forward,
        B, GeometryB, Direction::Forward, {}, Settings);

    EXPECT_EQ(Result.Status, PairPoseStatus::CertifiedEpsilonOptimal);
    EXPECT_LT(Result.RequiredSeparationUpper, 102.0);
    EXPECT_GT(std::abs(Result.RelativeRotationRadians), 0.02);
}

TEST(TubeNestingPairPose, NeverPromotesANumericProfileToGeometryCertified)
{
    const PairPartType A{"a", "profile", "order", 100, 100, 1, {true, false}};
    const PairPartType B{"b", "profile", "order", 100, 100, 1, {true, false}};
    auto GeometryA = FlatGeometry(100);
    auto GeometryB = FlatGeometry(100);
    GeometryA.Right.SourceQuality = ProfileSourceQuality::NumericOnly;

    const auto Result = SolveOrientedPairPose(
        A, GeometryA, Direction::Forward,
        B, GeometryB, Direction::Forward, {});

    EXPECT_EQ(Result.Status, PairPoseStatus::BestFeasible);
    EXPECT_EQ(Result.SourceQuality, ProfileSourceQuality::NumericOnly);
    EXPECT_TRUE(std::isfinite(Result.RequiredSeparationUpper));
}

TEST(TubeNestingPairPose, RejectsAProfileWhoseSourceIsMissing)
{
    const PairPartType A{"a", "profile", "order", 100, 100, 1, {true, false}};
    const PairPartType B{"b", "profile", "order", 100, 100, 1, {true, false}};
    auto GeometryA = FlatGeometry(100);
    auto GeometryB = FlatGeometry(100);
    GeometryA.Right.SourceQuality = ProfileSourceQuality::Missing;

    const auto Result = SolveOrientedPairPose(
        A, GeometryA, Direction::Forward,
        B, GeometryB, Direction::Forward, {});

    EXPECT_EQ(Result.Status, PairPoseStatus::Unsupported);
}

TEST(TubeNestingPairTable, BuildsAllFourDirectionEntriesAndIncludesProcessLoss)
{
    PairTableBuildInput Input;
    Input.Version = "flat-v1";
    Input.Types.push_back(PairPartType{
        "part", "profile", "order", 100, 100, 2, {true, true}});
    Input.Geometry.push_back(FlatGeometry(100));
    Input.PairProcessLosses.assign(4, 5);
    PairTableBuildSettings BuildSettings;
    BuildSettings.Pose.Tolerance = 0.01;
    BuildSettings.Pose.FixedPhaseTolerance = 0.001;
    BuildSettings.MaximumConcurrency = 2;

    const auto Table = BuildPairTable(Input, BuildSettings);

    ASSERT_TRUE(ValidatePairTable(Table));
    for (const auto DirectionA : {Direction::Forward, Direction::Reverse})
        for (const auto DirectionB : {Direction::Forward, Direction::Reverse})
        {
            const auto& Entry = Table.At(0, DirectionA, 0, DirectionB);
            EXPECT_EQ(Entry.Status, PairEntryStatus::Certified);
            // The table is deliberately conservative by at most one integer
            // length quantum when a continuous certified bound is rounded up.
            EXPECT_GE(Entry.NetSaving, -6);
            EXPECT_LE(Entry.NetSaving, -5);
        }

    const std::vector<StockType> Stocks{
        StockType{"stock", "profile", StockKind::Standard, 206},
    };
    const auto Result = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());
    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_LE(Result.Stocks.front().OccupiedLength, 206);
    EXPECT_EQ(Result.Stocks.front().Placements.size(), 2U);
}

TEST(TubeNestingPairTable, AppliesAnIndependentRotationDomainToEachOfTheFourPoses)
{
    PairTableBuildInput Input;
    Input.Types.push_back(PairPartType{
        "part", "profile", "order", 100, 100, 2, {true, true}});
    Input.Geometry.push_back(FlatGeometry(100));
    Input.OrientedPairDomains.resize(4);
    for (std::size_t Index = 0; Index < Input.OrientedPairDomains.size(); ++Index)
        Input.OrientedPairDomains[Index].DiscreteRadians.push_back(
            static_cast<double>(Index) * 0.5);

    const auto Table = BuildPairTable(Input);

    ASSERT_TRUE(ValidatePairTable(Table));
    for (const auto DirectionA : {Direction::Forward, Direction::Reverse})
        for (const auto DirectionB : {Direction::Forward, Direction::Reverse})
        {
            const auto EntryIndex = Table.EntryIndex(0, DirectionA, 0, DirectionB);
            const auto& Entry = Table.Entries[EntryIndex];
            ASSERT_EQ(Entry.Status, PairEntryStatus::Certified);
            EXPECT_NEAR(Table.Poses[Entry.PoseMetadataIndex].RelativeRotationRadians,
                static_cast<double>(EntryIndex) * 0.5, 1.0e-12);
        }
}

TEST(TubeNestingPairTable, CompactsRepeatedPoseMetadataWithoutChangingEntries)
{
    PairTableBuildInput Input;
    Input.Version = "compact-flat-v1";
    for (Length Index = 0; Index < 40; ++Index)
    {
        const Length PartLength = 1000 + Index;
        Input.Types.push_back(PairPartType{
            "part-" + std::to_string(Index), "profile", "order",
            PartLength, PartLength, 1, {true, true}});
        Input.Geometry.push_back(FlatGeometry(PartLength));
    }

    const auto Table = BuildPairTable(Input);

    ASSERT_TRUE(ValidatePairTable(Table));
    EXPECT_EQ(Table.Entries.size(), 6400U);
    EXPECT_EQ(Table.Poses.size(), 41U);
    for (std::size_t TypeA = 0; TypeA < Input.Types.size(); ++TypeA)
        for (std::size_t TypeB = 0; TypeB < Input.Types.size(); ++TypeB)
            for (const auto DirectionA : {Direction::Forward, Direction::Reverse})
                for (const auto DirectionB : {Direction::Forward, Direction::Reverse})
                {
                    const auto& Entry = Table.At(TypeA, DirectionA, TypeB, DirectionB);
                    ASSERT_EQ(Entry.Status, PairEntryStatus::Certified);
                    ASSERT_LT(Entry.PoseMetadataIndex, Table.Poses.size());
                    EXPECT_EQ(Table.Poses[Entry.PoseMetadataIndex].RequiredSeparation,
                        Input.Types[TypeA].MaximumLength);
                }
}

TEST(TubeNestingPairTable, RejectsCertifiedEntriesBackedOnlyByNumericSamples)
{
    auto Table = ManualTable({
        PairPartType{"part", "profile", "order", 100, 100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 0, Direction::Forward, 0);
    Table.Poses.back().SourceQuality = ProfileSourceQuality::NumericOnly;

    std::vector<std::string> Diagnostics;
    EXPECT_FALSE(ValidatePairTable(Table, &Diagnostics));
    EXPECT_TRUE(std::ranges::any_of(Diagnostics, [](const std::string& Diagnostic_)
    {
        return Diagnostic_.find("certified geometry source") != std::string::npos;
    }));

    const auto Result = PairTableSolver{}.Solve(
        Table, {StockType{"stock", "profile", StockKind::Standard, 100}},
        PairExactSettings());
    EXPECT_EQ(Result.Status, SolveStatus::InvalidInput);
}

TEST(TubeNestingPairTable, KeepsOneDirectionForBothNeighboursOfTheMiddlePart)
{
    auto Table = ManualTable({
        PairPartType{"a", "profile", "order", 100, 100, 1, {true, false}},
        PairPartType{"b", "profile", "order", 100, 100, 1, {true, true}},
        PairPartType{"c", "profile", "order", 100, 100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 1, Direction::Forward, 10);
    SetPair(Table, 0, Direction::Forward, 1, Direction::Reverse, 0);
    SetPair(Table, 1, Direction::Forward, 2, Direction::Forward, 0);
    SetPair(Table, 1, Direction::Reverse, 2, Direction::Forward, 10);
    ASSERT_TRUE(ValidatePairTable(Table));
    const std::vector<StockType> Stocks{
        StockType{"stock", "profile", StockKind::Standard, 290},
    };

    const auto Result = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    ASSERT_EQ(Result.Stocks.front().Placements.size(), 3U);
    EXPECT_EQ(Result.Stocks.front().OccupiedLength, 290);
    EXPECT_EQ(Result.Stocks.front().Placements[1].DemandID, "b");
}

TEST(TubeNestingPairTable, ResequencesPartsToFitTwoFiniteLengthBars)
{
    auto Table = ManualTable({
        PairPartType{"long", "profile", "order", 3600, 3600, 2, {true, false}},
        PairPartType{"short", "profile", "order", 2400, 2400, 2, {true, false}},
    });
    for (std::size_t A = 0; A < 2; ++A)
        for (std::size_t B = 0; B < 2; ++B)
            SetPair(Table, A, Direction::Forward, B, Direction::Forward, 0);
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "profile", StockKind::Standard, 6000},
    };

    const auto Result = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 2U);
    EXPECT_TRUE(std::all_of(Result.Stocks.begin(), Result.Stocks.end(), [](const auto& Stock_)
    {
        return Stock_.OccupiedLength == 6000 && Stock_.Placements.size() == 2;
    }));
}

TEST(TubeNestingPairTable, OptimizesFiniteSixAndTwelveMetreInventoryJointly)
{
    auto Table = ManualTable({
        PairPartType{"part", "profile", "order", 3500, 3500, 3, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 0, Direction::Forward, 0);
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "profile", StockKind::Standard, 6000, 1},
        StockType{"stock-12000", "profile", StockKind::Standard, 12000, 1},
    };

    const auto Result = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    EXPECT_EQ(Result.Stocks.front().StockTypeID, "stock-12000");
    EXPECT_EQ(Result.Stocks.front().OccupiedLength, 10500);
    EXPECT_EQ(Result.Stocks.front().Placements.size(), 3U);
}

TEST(TubeNestingPairTable, ContinuesAfterFixedIrregularPrefixUsingDirectedStartPose)
{
    auto Table = ManualTable({
        PairPartType{"locked", "profile", "old-order", 100, 100, 0, {false, true}},
        PairPartType{"next", "profile", "new-order", 100, 100, 1, {true, false}},
        PairPartType{"last", "profile", "new-order", 100, 100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Reverse, 1, Direction::Forward, 30, 0.25);
    SetPair(Table, 1, Direction::Forward, 2, Direction::Forward, 10, 0.50);
    ASSERT_TRUE(ValidatePairTable(Table));

    StockType Continued;
    Continued.ID = "continued-row";
    Continued.SourceTypeID = "stock-320";
    Continued.FixedInstanceID = "locked-plan";
    Continued.CompatibilityKey = "profile";
    Continued.Kind = StockKind::Remnant;
    Continued.TotalLength = 320;
    Continued.Quantity = 1;
    Continued.CostUnits = 0;
    Continued.InitialProcessedEnd = 150;
    Continued.InitialPredecessorTypeID = "locked";
    Continued.InitialPredecessorDirection = Direction::Reverse;
    Continued.InitialPredecessorRotationRadians = 1.0;
    const std::vector<StockType> Stocks{
        Continued,
        StockType{"new-stock", "profile", StockKind::Standard, 200},
    };

    const auto Result = PairTableSolver{}.Solve(
        Table, Stocks, PairExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    const auto& Stock = Result.Stocks.front();
    EXPECT_TRUE(Stock.Continued);
    EXPECT_EQ(Stock.StockTypeID, "stock-320");
    EXPECT_EQ(Stock.StockInstanceID, "locked-plan");
    EXPECT_EQ(Stock.InitialProcessedEnd, 150);
    EXPECT_EQ(Stock.ProcessedEnd, 310);
    EXPECT_EQ(Stock.RemainingLength, 10);
    EXPECT_EQ(Stock.OccupiedLength, 160);
    ASSERT_EQ(Stock.Placements.size(), 2U);
    EXPECT_EQ(Stock.Placements[0].DemandID, "next");
    EXPECT_EQ(Stock.Placements[0].Start, 120);
    EXPECT_EQ(Stock.Placements[0].End, 220);
    EXPECT_EQ(Stock.Placements[0].GapBefore, -30);
    EXPECT_EQ(Stock.Placements[0].SavingFromPrevious, 30);
    EXPECT_NEAR(Stock.Placements[0].RotationRadians, 1.25, 1.0e-12);
    EXPECT_EQ(Stock.Placements[1].DemandID, "last");
    EXPECT_EQ(Stock.Placements[1].Start, 210);
    EXPECT_EQ(Stock.Placements[1].End, 310);
    EXPECT_NEAR(Stock.Placements[1].RotationRadians, 1.75, 1.0e-12);
    EXPECT_EQ(Result.Metrics.OpenedStockCount, 0U);
    EXPECT_EQ(Result.Metrics.StockCost, 0);
}

TEST(TubeNestingPairTable, RejectsContinuedStockWithUnknownPredecessor)
{
    auto Table = ManualTable({
        PairPartType{"part", "profile", "order", 100, 100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 0, Direction::Forward, 0);
    StockType Continued{"continued", "profile", StockKind::Remnant, 200, 1};
    Continued.FixedInstanceID = "physical-stock";
    Continued.InitialProcessedEnd = 50;
    Continued.InitialPredecessorTypeID = "missing";

    const auto Result = PairTableSolver{}.Solve(
        Table, {Continued}, PairExactSettings());

    EXPECT_EQ(Result.Status, SolveStatus::InvalidInput);
    EXPECT_TRUE(std::ranges::any_of(Result.Diagnostics, [](const auto& Diagnostic_)
    {
        return Diagnostic_.find("predecessor is missing") != std::string::npos;
    }));
}

TEST(TubeNestingPairTable, FullExactMatchesIndependentEnumerationOnRandomDirectedTables)
{
    constexpr std::size_t PartCount = 6;
    std::mt19937 Random(0x51A7U);
    const std::vector<StockType> Stocks{
        StockType{"finite-250", "profile", StockKind::Standard, 250, 1},
        StockType{"stock-430", "profile", StockKind::Standard, 430},
        StockType{"stock-900", "profile", StockKind::Standard, 900},
    };

    for (std::size_t Sample = 0; Sample < 5; ++Sample)
    {
        std::vector<PairPartType> Types;
        Types.reserve(PartCount);
        for (std::size_t Index = 0; Index < PartCount; ++Index)
        {
            const auto LengthValue = static_cast<Length>(120 + Random() % 80);
            Types.push_back(PairPartType{
                "part-" + std::to_string(Index), "profile", "order",
                LengthValue, LengthValue, 1, {true, true}});
        }
        auto Table = ManualTable(std::move(Types));
        for (std::size_t Tail = 0; Tail < PartCount; ++Tail)
            for (std::size_t Head = 0; Head < PartCount; ++Head)
            {
                if (Tail == Head) continue;
                for (const auto TailDirection : {Direction::Forward, Direction::Reverse})
                    for (const auto HeadDirection : {Direction::Forward, Direction::Reverse})
                    {
                        const auto Saving = static_cast<Length>(
                            static_cast<int>(Random() % 81) - 20);
                        SetPair(Table, Tail, TailDirection, Head, HeadDirection, Saving);
                    }
            }
        ASSERT_TRUE(ValidatePairTable(Table));

        using OracleScore = std::tuple<Length, std::size_t, Length>;
        auto Best = OracleScore{
            (std::numeric_limits<Length>::max)(),
            (std::numeric_limits<std::size_t>::max)(),
            (std::numeric_limits<Length>::max)()};
        std::vector<std::size_t> Order(PartCount);
        std::iota(Order.begin(), Order.end(), 0);
        do
        {
            for (std::size_t DirectionMask = 0;
                DirectionMask < (std::size_t{1} << PartCount); ++DirectionMask)
            {
                const auto DirectionAt = [&](const std::size_t Position)
                {
                    return (DirectionMask & (std::size_t{1} << Position))
                        ? Direction::Reverse : Direction::Forward;
                };
                for (std::size_t BreakMask = 0;
                    BreakMask < (std::size_t{1} << (PartCount - 1)); ++BreakMask)
                {
                    std::array<Length, PartCount> SegmentLengths{};
                    std::size_t SegmentCount = 0;
                    Length Segment = Table.Types[Order.front()].MaximumLength;
                    Length TotalOccupied = 0;
                    for (std::size_t Position = 1; Position < PartCount; ++Position)
                    {
                        if (BreakMask & (std::size_t{1} << (Position - 1)))
                        {
                            SegmentLengths[SegmentCount++] = Segment;
                            TotalOccupied += Segment;
                            Segment = Table.Types[Order[Position]].MaximumLength;
                            continue;
                        }
                        const auto& Entry = Table.At(
                            Order[Position - 1], DirectionAt(Position - 1),
                            Order[Position], DirectionAt(Position));
                        ASSERT_TRUE(Entry.Status == PairEntryStatus::Feasible
                            || Entry.Status == PairEntryStatus::Certified);
                        Segment += Table.Types[Order[Position]].MaximumLength
                            - Entry.NetSaving;
                    }
                    SegmentLengths[SegmentCount++] = Segment;
                    TotalOccupied += Segment;

                    constexpr Length Infinity = (std::numeric_limits<Length>::max)() / 4;
                    std::array<Length, 2> Cost{0, Infinity};
                    for (std::size_t SegmentIndex = 0;
                        SegmentIndex < SegmentCount; ++SegmentIndex)
                    {
                        const auto Occupied = SegmentLengths[SegmentIndex];
                        std::array<Length, 2> Next{Infinity, Infinity};
                        for (std::size_t FiniteUsed = 0; FiniteUsed < Cost.size(); ++FiniteUsed)
                        {
                            if (Cost[FiniteUsed] >= Infinity) continue;
                            for (std::size_t StockIndex = 0;
                                StockIndex < Stocks.size(); ++StockIndex)
                            {
                                if (Occupied > Stocks[StockIndex].TotalLength) continue;
                                const auto NextFinite = FiniteUsed
                                    + (StockIndex == 0 ? 1U : 0U);
                                if (NextFinite >= Next.size()) continue;
                                Next[NextFinite] = (std::min)(Next[NextFinite],
                                    Cost[FiniteUsed] + Stocks[StockIndex].TotalLength);
                            }
                        }
                        Cost = Next;
                    }
                    const auto Nominal = (std::min)(Cost[0], Cost[1]);
                    if (Nominal >= Infinity) continue;
                    Best = (std::min)(Best, OracleScore{
                        Nominal, SegmentCount, TotalOccupied});
                }
            }
        } while (std::next_permutation(Order.begin(), Order.end()));

        auto Settings = PairExactSettings();
        Settings.ConstraintTimeBudgetMilliseconds = 5000;
        const auto Result = PairTableSolver{}.Solve(Table, Stocks, Settings);
        ASSERT_EQ(Result.Status, SolveStatus::Optimal) << "sample " << Sample;
        ASSERT_TRUE(Result.ExactSearchCompleted) << "sample " << Sample;
        Length Occupied = 0;
        for (const auto& Stock : Result.Stocks) Occupied += Stock.OccupiedLength;
        EXPECT_EQ(Result.Metrics.TotalStockLength, std::get<0>(Best)) << "sample " << Sample;
        EXPECT_EQ(Result.Stocks.size(), std::get<1>(Best)) << "sample " << Sample;
        EXPECT_EQ(Occupied, std::get<2>(Best)) << "sample " << Sample;
    }
}

TEST(TubeNestingPairTable, TreatsDirectedAdjacencyAsAsymmetric)
{
    auto Table = ManualTable({
        PairPartType{"a", "profile", "order", 100, 100, 1, {true, false}},
        PairPartType{"b", "profile", "other-order", 100, 100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 1, Direction::Forward, 20, 0.25);
    SetPair(Table, 1, Direction::Forward, 0, Direction::Forward, 0, 0.75);
    const std::vector<StockType> Stocks{
        StockType{"stock", "profile", StockKind::Standard, 180},
    };

    const auto Result = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());

    ASSERT_EQ(Result.Status, SolveStatus::Optimal);
    ASSERT_EQ(Result.Stocks.size(), 1U);
    ASSERT_EQ(Result.Stocks.front().Placements.size(), 2U);
    EXPECT_EQ(Result.Stocks.front().Placements[0].DemandID, "a");
    EXPECT_EQ(Result.Stocks.front().Placements[1].DemandID, "b");
    EXPECT_NEAR(Result.Stocks.front().Placements[1].RotationRadians, 0.25, 1.0e-9);
    EXPECT_EQ(Result.Stocks.front().CutCount, 3U);
    EXPECT_EQ(Result.Metrics.CutCount, 3U);
    EXPECT_EQ(Result.Metrics.CommonCutCount, 0U);
    EXPECT_EQ(Result.Metrics.OrderChangeCount, 1U);
}

TEST(TubeNestingPairTable, DistinguishesProvenInfeasibleFromNoIncumbent)
{
    auto Table = ManualTable({
        PairPartType{"oversize", "profile", "order", 6100, 6100, 1, {true, false}},
    });
    SetPair(Table, 0, Direction::Forward, 0, Direction::Forward, 0);
    const std::vector<StockType> Stocks{
        StockType{"stock-6000", "profile", StockKind::Standard, 6000},
    };

    const auto Proven = PairTableSolver{}.Solve(Table, Stocks, PairExactSettings());
    EXPECT_EQ(Proven.Status, SolveStatus::Infeasible);
    EXPECT_TRUE(Proven.ExactSearchCompleted);
    EXPECT_EQ(Proven.ProvenScope, ProofScope::FullIntegerModel);

    auto HeuristicSettings = PairExactSettings();
    HeuristicSettings.Constraint = ConstraintMode::Disabled;
    const auto Unproven = PairTableSolver{}.Solve(Table, Stocks, HeuristicSettings);
    EXPECT_EQ(Unproven.Status, SolveStatus::NoSolutionFound);
    EXPECT_FALSE(Unproven.ExactSearchCompleted);
    EXPECT_EQ(Unproven.ProvenScope, ProofScope::None);
}
