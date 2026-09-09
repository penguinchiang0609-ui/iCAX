#include "pch.h"

#include <gtest/gtest.h>

#include "TubeNesting/TubeNesting.h"

#include <algorithm>
#include <set>
#include <string>
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
