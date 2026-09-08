#include "pch.h"
#include <TubeDesigner/NestingAdapter.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <Data/VariantSerializer.h>

#include <cmath>
#include <limits>
#include <map>
#include <Task/Task.h>
#include <chrono>
#include <iostream>

namespace
{
    using namespace iCAX::TubeDesigner;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

    void CheckAccounting(const ObjectMap& Result_, const std::vector<SNestingPart>& Parts_,
        const std::vector<SNestingStock>& Stocks_, const double Gap_)
    {
        std::map<std::string, std::size_t> _Counts, _StockCounts;
        for (const auto& _PlanValue : Result_.at("plans").To<VariantArray>())
        {
            const auto _Plan = _PlanValue.To<ObjectMap>();
            const auto _StockID = _Plan.at("stockTypeId").To<std::string>();
            ++_StockCounts[_StockID];
            const auto _Placements = _Plan.at("placements").To<VariantArray>();
            double _End = 0.0;
            for (std::size_t _Index = 0; _Index < _Placements.size(); ++_Index)
            {
                const auto _Placement = _Placements[_Index].To<ObjectMap>();
                ++_Counts[_Placement.at("partId").To<std::string>()];
                EXPECT_NEAR(_Placement.at("start").To<double>(), _Index ? _End + Gap_ : 0.0, 1e-8);
                EXPECT_NEAR(_Placement.at("gapBefore").To<double>(), _Index ? Gap_ : 0.0, 1e-8);
                EXPECT_FALSE(_Placement.at("nestedWithPrevious").To<bool>());
                const auto _Transform = _Placement.at("trsf").To<VariantArray>();
                ASSERT_EQ(_Transform.size(), 16u);
                for (const auto& _Value : _Transform)
                    EXPECT_TRUE(std::isfinite(_Value.To<double>()));
                _End = _Placement.at("end").To<double>();
            }
            EXPECT_NEAR(_End, _Plan.at("usedLength").To<double>(), 1e-8);
            EXPECT_LE(_End, _Plan.at("stockLength").To<double>() + 1e-8);
            EXPECT_NEAR(_End + _Plan.at("remainingLength").To<double>(), _Plan.at("stockLength").To<double>(), 1e-8);
        }
        for (const auto& _Value : Result_.at("unplaced").To<VariantArray>())
        {
            const auto _Item = _Value.To<ObjectMap>();
            _Counts[_Item.at("partId").To<std::string>()] += _Item.at("quantity").To<unsigned long long>();
        }
        for (const auto& _Part : Parts_) EXPECT_EQ(_Counts[_Part.ID], _Part.Quantity);
        for (const auto& _Stock : Stocks_) if (_Stock.Quantity >= 0)
            EXPECT_LE(_StockCounts[_Stock.ID], static_cast<std::size_t>(_Stock.Quantity));
        EXPECT_EQ(Result_.at("metrics").To<ObjectMap>().at("stockCount").To<unsigned long long>(), Result_.at("plans").To<VariantArray>().size());
    }
}

TEST(TubeDesignerNesting, ManufacturingKindGuardExcludesPlateAndUnknownItems)
{
    EXPECT_TRUE(IsTubeManufacturingPart({}));
    EXPECT_TRUE(IsTubeManufacturingPart({ { "manufacturing.partKind", std::string("tube") } }));
    EXPECT_TRUE(IsTubeManufacturingPart({ { "manufacturing.partKind", std::string(" PROFILE ") },
        { "manufacturing.materialCategory", std::string("linear") } }));
    for (const auto* _Kind : { "plate", "accessory", "glass", "unknown", "", "  " })
    {
        SCOPED_TRACE(_Kind);
        EXPECT_FALSE(IsTubeManufacturingPart({ { "manufacturing.partKind", std::string(_Kind) } }));
        EXPECT_FALSE(IsTubeManufacturingPart({ { "manufacturing.materialCategory", std::string(_Kind) } }));
    }
    EXPECT_FALSE(IsTubeManufacturingPart({ { "manufacturing.partKind", 1 } }));
    EXPECT_FALSE(IsTubeManufacturingPart({ { "manufacturing.partKind", std::string("tube") },
        { "manufacturing.materialCategory", std::string("plate") } }));
    EXPECT_FALSE(IsTubeManufacturingPart({ { "manufacturing.plate", ObjectMap{
        { "width", 300.0 }, { "height", 600.0 }, { "thickness", 2.0 } } } }));
}

TEST(TubeDesignerNesting, ManufacturingProcessGuardExcludesPurchasedAndBentPartsButKeepsMadeStraightTubes)
{
    const ObjectMap _Straight{
        { "manufacturing.partKind", std::string("tube") },
        { "manufacturing.materialCategory", std::string("linear") },
        { "manufacturing.sourcing", std::string("made") },
        { "manufacturing.process", std::string("straight-cut") },
        { "manufacturing.requiresBending", false }
    };
    EXPECT_TRUE(IsTubeManufacturingPart(_Straight));
    auto _Part = _Straight;
    _Part["manufacturing.sourcing"] = std::string(" PURCHASED \t");
    EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    for (const auto* _Process : { "purchased", "bent", "curved", "bending", "tube-bending", "bent-tube", " CURVED \t" })
    {
        SCOPED_TRACE(_Process);
        _Part = _Straight;
        _Part["manufacturing.process"] = std::string(_Process);
        EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    }
    _Part = _Straight;
    _Part["manufacturing.requiresBending"] = true;
    EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    _Part["manufacturing.requiresBending"] = std::string("false");
    EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    _Part["manufacturing.requiresBending"] = 0;
    EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    for (const auto* _Field : { "manufacturing.sourcing", "manufacturing.process" })
    {
        SCOPED_TRACE(_Field);
        _Part = _Straight;
        _Part[_Field] = 1;
        EXPECT_FALSE(IsTubeManufacturingPart(_Part));
    }
    _Part = _Straight;
    _Part.erase("manufacturing.requiresBending");
    EXPECT_TRUE(IsTubeManufacturingPart(_Part));
}

TEST(TubeDesignerNesting, FirstPlacementNeverNestsWithPositiveGap)
{
    for (const double _Gap : { 0.0, 1.0, 5.0 })
    {
        SCOPED_TRACE(_Gap);
        const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 1 } };
        const std::vector<SNestingStock> _Stocks{ { "s", "section", 6000, -1 } };
        const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, _Gap);
        ASSERT_EQ(_Result.at("plans").To<VariantArray>().size(), 1u);
        ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
        CheckAccounting(_Result, _Parts, _Stocks, _Gap);
    }
}

TEST(TubeDesignerNesting, EachNewStockStartsWithoutNestingWithPrevious)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 3 } };
    const std::vector<SNestingStock> _Stocks{ { "s", "section", 1000, -1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5.0);
    ASSERT_EQ(_Result.at("plans").To<VariantArray>().size(), 3u);
    ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    CheckAccounting(_Result, _Parts, _Stocks, 5.0);
}

TEST(TubeDesignerNesting, PositiveGapRetainsLegalTrapezoidNestingAfterFirstPart)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    _Variant.LeftEnd = { 10.0, "50000:0", true };
    _Variant.RightEnd = { 10.0, "50000:0", true };
    const auto _Result = SolveManufacturingNesting(
        { { "p", "section", 120.0, 2, { _Variant } } }, { { "s", "section", 235.0, 1 } }, 5.0);
    ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 1u);
    const auto _Placements = _Plans.front().To<ObjectMap>().at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 2u);
    const auto _First = _Placements.front().To<ObjectMap>();
    const auto _Second = _Placements[1].To<ObjectMap>();
    EXPECT_FALSE(_First.at("nestedWithPrevious").To<bool>());
    EXPECT_NEAR(_First.at("gapBefore").To<double>(), 0.0, 1e-8);
    EXPECT_TRUE(_Second.at("nestedWithPrevious").To<bool>());
    EXPECT_NEAR(_Second.at("gapBefore").To<double>(), -5.0, 1e-8);
    EXPECT_NEAR(_Second.at("end").To<double>(), 235.0, 1e-8);
}

TEST(TubeDesignerNesting, PlacementStoresAuthoritativeTransformIncludingAxialRotation)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter-rotated";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    _Variant.Reversed = true;
    _Variant.RotationRadians = 1.57079632679489661923;
    const std::array<double, 3> _LocalCenter{ 10.0, 2.0, 3.0 };
    const auto _Result = SolveManufacturingNesting(
        { { "p", "section", 120.0, 1, { _Variant }, _LocalCenter } },
        { { "s", "section", 200.0, 1 } }, 0.0);
    const auto _Plan = _Result.at("plans").To<VariantArray>().front().To<ObjectMap>();
    const auto _Placement = _Plan.at("placements").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_TRUE(_Placement.at("reversed").To<bool>());
    EXPECT_NEAR(_Placement.at("rotationRadians").To<double>(), _Variant.RotationRadians, 1e-12);
    const auto _Actual = _Placement.at("trsf").To<VariantArray>();
    const auto _Expected = MakeNestingPlacementTransform(
        _LocalCenter, _Placement.at("start").To<double>(), _Placement.at("end").To<double>(),
        true, _Variant.RotationRadians);
    ASSERT_EQ(_Actual.size(), _Expected.size());
    for (std::size_t _Index = 0; _Index < _Actual.size(); ++_Index)
        EXPECT_NEAR(_Actual[_Index].To<double>(), _Expected[_Index].To<double>(), 1e-12);
}

TEST(TubeDesignerNesting, PlacementCarriesCircumferentialPhaseWithoutDuplicatingGeometry)
{
    SNestingVariant _Variant;
    _Variant.ID = "phase-variant";
    _Variant.EnvelopeLength = 100.0;
    _Variant.MaterialLength = 100.0;
    _Variant.PhaseOffset = 37.25;
    const auto _Result = SolveManufacturingNesting(
        { { "p", "section", 100.0, 1, { _Variant } } },
        { { "s", "section", 200.0, 1 } }, 0.0);
    const auto _Placement = _Result.at("plans").To<VariantArray>().front()
        .To<ObjectMap>().at("placements").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_NEAR(_Placement.at("phaseOffset").To<double>(), 37.25, 1e-8);
}

TEST(TubeDesignerNesting, ExactFitUsesOnlyInteriorGapAndFiniteStock)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 2 } };
    const std::vector<SNestingStock> _Stocks{ { "s", "section", 2005, 1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "optimal");
    ASSERT_EQ(_Result.at("plans").To<VariantArray>().size(), 1u);
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, AdditionalLengthRowsAreAvailableToSolver)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1500, 3 } };
    const std::vector<SNestingStock> _Stocks{ { "short", "section", 1500, 1 }, { "long", "section", 3004, 1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 4);
    EXPECT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    EXPECT_EQ(_Result.at("plans").To<VariantArray>().size(), 2u);
    CheckAccounting(_Result, _Parts, _Stocks, 4);
}

TEST(TubeDesignerNesting, StockShortageRetainsPartialPlanAndAccountsEveryPiece)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 3 } };
    const std::vector<SNestingStock> _Stocks{ { "s", "section", 2005, 1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "partial");
    EXPECT_EQ(_Result.at("metrics").To<ObjectMap>().at("placedPartCount").To<unsigned long long>(), 2u);
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, OverlongPartDoesNotHideOtherPartsInSameSection)
{
    const std::vector<SNestingPart> _Parts{ { "long", "section", 3000, 2 }, { "short", "section", 1000, 2 }, { "missing", "other", 100, 4 } };
    const std::vector<SNestingStock> _Stocks{ { "s", "section", 2005, 1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "partial");
    EXPECT_EQ(_Result.at("unplaced").To<VariantArray>().size(), 2u);
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, ZeroStockIsExcludedAndInvalidLengthsRejected)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 1 } };
    EXPECT_EQ(SolveManufacturingNesting(_Parts, { { "s", "section", 6000, 0 } }, 0).at("status").To<std::string>(), "infeasible");
    EXPECT_THROW(SolveManufacturingNesting(_Parts, { { "s", "section", 6000, -2 } }, 0), std::invalid_argument);
    EXPECT_THROW(SolveManufacturingNesting(_Parts, { { "s", "section", std::numeric_limits<double>::infinity(), 1 } }, 0), std::invalid_argument);
    EXPECT_THROW(SolveManufacturingNesting(_Parts, { { "s", "section", 6000, 1 } }, -1), std::invalid_argument);
    const auto _LargeDemand = SolveManufacturingNesting({ { "p", "section", 10, 10000 } }, {}, 0);
    EXPECT_EQ(_LargeDemand.at("status").To<std::string>(), "infeasible");
    EXPECT_THROW(SolveManufacturingNesting({ { "p", "section", 10, 1'000'001 } }, {}, 0), std::invalid_argument);
}

TEST(TubeDesignerNesting, QuantizationNeverShortensPartOrExtendsStock)
{
    const auto _Result = SolveManufacturingNesting({ { "p", "section", 1000.001, 1 } }, { { "s", "section", 1000.009, 1 } }, 0);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "infeasible");
    ASSERT_EQ(_Result.at("unplaced").To<VariantArray>().size(), 1u);
}

TEST(TubeDesignerNesting, CompatiblePlanarEndsUseConservativeTrapezoidOverlap)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    _Variant.LeftEnd = { 10.0, "50000:0", true };
    _Variant.RightEnd = { 10.0, "50000:0", true };
    const std::vector<SNestingPart> _Parts{ { "miter-part", "section", 120.0, 2, { _Variant } } };
    const auto _Result = SolveManufacturingNesting(
        _Parts, { { "stock", "section", 230.0, 1 } }, 0.0);
    ASSERT_EQ(_Result.at("status").To<std::string>(), "optimal");
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 1u);
    const auto _Plan = _Plans.front().To<ObjectMap>();
    const auto _Placements = _Plan.at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 2u);
    const auto _Second = _Placements[1].To<ObjectMap>();
    EXPECT_NEAR(_Second.at("start").To<double>(), 110.0, 1e-8);
    EXPECT_NEAR(_Second.at("gapBefore").To<double>(), -10.0, 1e-8);
    EXPECT_TRUE(_Second.at("nestedWithPrevious").To<bool>());
    EXPECT_EQ(_Second.at("variantId").To<std::string>(), "miter");
    EXPECT_NEAR(_Plan.at("partLength").To<double>(), 220.0, 1e-8);
    EXPECT_NEAR(_Plan.at("usedLength").To<double>(), 230.0, 1e-8);
}

TEST(TubeDesignerNesting, ProfileKeyUsesSectionDataAndIgnoresMaterial)
{
    ObjectMap _Profile{ { "id", std::string("rhs") }, { "kind", std::string("roundedRectangle") },
        { "width", 22.0 }, { "depth", 22.0 }, { "wallThickness", 1.0 }, { "hollow", true },
        { "material", std::string("steel") }, { "displayName", std::string("user name") } };
    const std::string _Key = R"({"depth":22,"hollow":true,"id":"rhs","kind":"roundedRectangle","wallThickness":1,"width":22})";
    EXPECT_TRUE(MatchesNestingProfileKey(_Key, _Profile, "p"));
    _Profile["material"] = std::string("aluminium");
    EXPECT_TRUE(MatchesNestingProfileKey(_Key, _Profile, "p"));
    _Profile["width"] = 25.0;
    EXPECT_FALSE(MatchesNestingProfileKey(_Key, _Profile, "p"));
    EXPECT_TRUE(MatchesNestingProfileKey("unknown:p", {}, "p"));
    EXPECT_FALSE(MatchesNestingProfileKey("unknown:q", {}, "p"));
}

TEST(TubeDesignerNesting, FiniteStockHasHardPriorityOverCheaperUnlimitedStock)
{
    const std::vector<SNestingPart> _Parts{ { "p", "section", 1000, 5 } };
    const std::vector<SNestingStock> _Stocks{ { "finite", "section", 2500, 1 }, { "unlimited", "section", 1000, -1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 4u);
    EXPECT_EQ(_Plans.front().To<ObjectMap>().at("stockTypeId").To<std::string>(), "finite");
    EXPECT_EQ(_Plans.front().To<ObjectMap>().at("partCount").To<unsigned long long>(), 2u);
    std::set<std::string> _Instances;
    for (const auto& _Plan : _Plans)
        for (const auto& _Placement : _Plan.To<ObjectMap>().at("placements").To<VariantArray>())
            EXPECT_TRUE(_Instances.insert(_Placement.To<ObjectMap>().at("instanceId").To<std::string>()).second);
    EXPECT_EQ(_Instances.size(), 5u);
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, UnlimitedStockFillsRemainingAndOversizePartsRemainAccounted)
{
    const std::vector<SNestingPart> _Parts{ { "short", "section", 1000, 3 }, { "long", "section", 4000, 2 }, { "over", "section", 7000, 1 } };
    const std::vector<SNestingStock> _Stocks{ { "finite", "section", 2005, 1 }, { "unlimited", "section", 6000, -1 } };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "partial");
    EXPECT_EQ(_Result.at("metrics").To<ObjectMap>().at("placedPartCount").To<unsigned long long>(), 5u);
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, SettingsValidateAndRoundTripUnlimitedDefaults)
{
    const ObjectMap _Settings{ { "version", 2 }, { "stocks", VariantArray{
        ObjectMap{ { "profileKey", std::string("section") }, { "rows", VariantArray{
            ObjectMap{ { "id", std::string("default") }, { "length", 6000.0 }, { "quantity", -1 } },
            ObjectMap{ { "id", std::string("finite") }, { "length", 2500.0 }, { "quantity", 3 } },
            ObjectMap{ { "id", std::string("disabled") }, { "length", std::string() }, { "quantity", 0 } }
        } } }
    } }, { "parameters", ObjectMap{ { "partGap", 5.0 } } } };
    const auto _Normalized = NormalizeNestingSettings(_Settings);
    const auto _Text = iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(_Normalized));
    EXPECT_EQ(iCAX::Data::VariantSerializer::Deserialize(_Text).To<ObjectMap>(), _Normalized);
    const auto _Rows = _Normalized.at("stocks").To<VariantArray>().front().To<ObjectMap>().at("rows").To<VariantArray>();
    EXPECT_EQ(_Rows.front().To<ObjectMap>().at("quantity").To<long long>(), -1);
    auto _Bad = _Settings;
    _Bad["stocks"] = VariantArray{ ObjectMap{ { "profileKey", std::string("section") }, { "rows", VariantArray{
        ObjectMap{ { "id", std::string("invalid") }, { "length", 6000.0 }, { "quantity", -2 } }
    } } } };
    EXPECT_THROW(NormalizeNestingSettings(_Bad), std::invalid_argument);
}

TEST(TubeDesignerNesting, ParallelProfileGroupsMatchSerialIncludingPartialInventoryAndOrder)
{
    std::vector<SNestingPart> _Parts;
    std::vector<SNestingStock> _Stocks;
    for (int _Index = 0; _Index < 13; ++_Index)
    {
        const auto _Key = "profile-" + std::to_string(_Index);
        _Parts.push_back({ _Key + "-short", _Key, 1000, 5 });
        _Parts.push_back({ _Key + "-long", _Key, 7000, 1 });
        if (_Index == 0) continue; // Missing stock must not affect other groups.
        _Stocks.push_back({ _Key + "-finite", _Key, 2500, 1 });
        if (_Index % 2) _Stocks.push_back({ _Key + "-unlimited", _Key, 6000, -1 });
    }
    const auto _Serial = SolveManufacturingNesting(_Parts, _Stocks, 5, 1);
    for (const auto _Workers : { 0u, 2u, 8u, 100u })
    {
        const auto _Parallel = SolveManufacturingNesting(_Parts, _Stocks, 5, _Workers);
        EXPECT_EQ(_Serial, _Parallel);
        CheckAccounting(_Parallel, _Parts, _Stocks, 5);
    }
}

TEST(TubeDesignerNesting, ParallelGroupsPreserveTrapezoidVariantsAndRecoverAfterInvalidInput)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120;
    _Variant.MaterialLength = 110;
    _Variant.LeftEnd = { 10, "50000:0", true };
    _Variant.RightEnd = { 10, "50000:0", true };
    const std::vector<SNestingPart> _Parts{ { "a", "a", 120, 2, { _Variant } }, { "b", "b", 120, 2, { _Variant } } };
    const std::vector<SNestingStock> _Stocks{ { "a-stock", "a", 230, 1 }, { "b-stock", "b", 230, 1 } };
    auto _Invalid = _Parts;
    _Invalid.back().ID = "a";
    EXPECT_THROW(SolveManufacturingNesting(_Invalid, _Stocks, 0), std::invalid_argument);
    EXPECT_EQ(SolveManufacturingNesting(_Parts, _Stocks, 0, 1), SolveManufacturingNesting(_Parts, _Stocks, 0, 8));
    auto _Pool = std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(1);
    const auto _Task = iCAX::Tasks::Run([&] { return SolveManufacturingNesting(_Parts, _Stocks, 0); }, _Pool);
    ASSERT_TRUE(_Task.WaitFor(std::chrono::seconds(5)));
    EXPECT_EQ(SolveManufacturingNesting(_Parts, _Stocks, 0, 1), _Task.Result());
}

TEST(TubeDesignerNesting, DISABLED_ProfileGroupParallelBenchmark)
{
    std::vector<SNestingPart> _Parts;
    std::vector<SNestingStock> _Stocks;
    for (int _Group = 0; _Group < 8; ++_Group)
    {
        const auto _Key = "section-" + std::to_string(_Group);
        for (int _Part = 0; _Part < 12; ++_Part)
            _Parts.push_back({ _Key + "-" + std::to_string(_Part), _Key, 500.0 + (_Part * 173 % 1700), 3 });
        _Stocks.push_back({ _Key + "-stock", _Key, 6000, -1 });
    }
    const auto _Timed = [&](std::size_t Workers_) {
        const auto _Start = std::chrono::steady_clock::now();
        auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 3, Workers_);
        std::cout << "Nesting 8 groups / 288 pieces, workers=" << Workers_ << ": "
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - _Start).count() << " s\n";
        return _Result;
    };
    const auto _Serial = _Timed(1);
    EXPECT_EQ(_Serial, _Timed(8));
}

TEST(TubeDesignerNesting, SettingsArePersistentTransactionalSceneFields)
{
    using namespace iCAX::Database;
    auto _Registry = CreateMetaRegistry();
    // Register this executable's component definition, not an older production
    // DLL's factory (whose root component does not yet contain the new field).
    wchar_t _ExePath[32768]{};
    wchar_t _DatabasePath[32768]{};
    ASSERT_GT(GetModuleFileNameW(nullptr, _ExePath, 32768), 0u);
    ASSERT_GT(GetModuleFileNameW(GetModuleHandleW(L"Database.dll"), _DatabasePath, 32768), 0u);
    CMetaRegistrationCatalog::ReplayByModulePaths(*_Registry,
        { std::filesystem::path(_DatabasePath).string(), std::filesystem::path(_ExePath).string() });
    EXPECT_EQ(_Registry->GetPropertyPersistenceByName(CTubeDesignerRootComponent::S_ClassName,
        CTubeDesignerRootComponent::PropertyName_NestingSettings), EPropertyPersistence::Persistent);
    auto _Repository = GenerateRepository(iCAX::Data::GenerateNewUUID(), _Registry);
    _Repository->BeginLoadBaseline();
    const auto _Entity = _Repository->GetMetaEntity();
    ASSERT_TRUE(_Entity);
    const auto _Root = _Entity->AddComponent<CTubeDesignerRootComponent>();
    ASSERT_TRUE(_Root);
    _Repository->EndLoadBaseline();
    const ObjectMap _Settings = NormalizeNestingSettings(ObjectMap{ { "version", 2 }, { "stocks", VariantArray{} },
        { "parameters", ObjectMap{ { "partGap", 8.0 } } } });
    auto _Undo = _Repository->BeginUndoCommand("Save nesting settings");
    auto& _Transaction = _Repository->BeginTransaction("Change nesting settings");
    _Transaction.ModifyComponent(_Entity->GetID(), CTubeDesignerRootComponent::S_ClassName,
        { { CTubeDesignerRootComponent::PropertyName_NestingSettings, _Settings } });
    std::string _Error;
    ASSERT_TRUE(_Repository->CommitTransaction(_Transaction, _Error)) << _Error;
    _Undo->End();
    EXPECT_EQ(_Root->GetNestingSettings(), _Settings);
    ASSERT_TRUE(_Repository->Undo());
    EXPECT_TRUE(_Root->GetNestingSettings().empty());
    ASSERT_TRUE(_Repository->Redo());
    EXPECT_EQ(_Root->GetNestingSettings(), _Settings);
}
