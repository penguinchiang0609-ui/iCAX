#include "pch.h"
#include <TubeDesigner/NestingAdapter.h>
#include <TubeDesigner/NestingResultAccess.h>
#include <TubeDesigner/TubeDesignerComponents.h>
#include <Data/VariantSerializer.h>

#include <algorithm>
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

    iCAX::TubeNesting::PeriodicEndProfile ConstantProfile(const double PositionMm_)
    {
        iCAX::TubeNesting::FourierLevel _Level;
        _Level.Constant = PositionMm_ * 100.0;
        _Level.Certified = true;
        iCAX::TubeNesting::PeriodicEndProfile _Profile;
        _Profile.SourceQuality = iCAX::TubeNesting::ProfileSourceQuality::NativeCertified;
        _Profile.CompleteSingleValued = true;
        _Profile.Levels.push_back(std::move(_Level));
        return _Profile;
    }

    iCAX::TubeNesting::PairTypeGeometry InsetRightProfile(
        const double LengthMm_, const double InsetMm_)
    {
        iCAX::TubeNesting::PairTypeGeometry _Geometry;
        _Geometry.Left = ConstantProfile(0.0);
        _Geometry.Right = ConstantProfile(LengthMm_ - InsetMm_);
        return _Geometry;
    }

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
    EXPECT_FALSE(IsTubeManufacturingPart({}));
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

TEST(TubeDesignerNesting, PositiveGapIsCombinedWithPeriodicEndProfileNesting)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    SNestingPart _Part{ "p", "section", 120.0, 2, { _Variant } };
    _Part.PairGeometry = InsetRightProfile(120.0, 10.0);
    _Part.DirectionAllowed = { true, false };
    const auto _Result = SolveManufacturingNesting(
        { _Part }, { { "s", "section", 235.0, 1 } }, 5.0);
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

TEST(TubeDesignerNesting, ContinuedIrregularStockAppendsAfterLockedPrefix)
{
    SNestingPart _Locked{ "locked", "section", 100.0, 0 };
    _Locked.PairGeometry = InsetRightProfile(100.0, 20.0);
    _Locked.DirectionAllowed = { true, false };
    SNestingPart _Next{ "next", "section", 100.0, 1 };
    _Next.PairGeometry = InsetRightProfile(100.0, 0.0);
    _Next.DirectionAllowed = { true, false };

    SNestingStock _Continued;
    _Continued.ID = "continued-row";
    _Continued.ProfileKey = "section";
    _Continued.Length = 250.0;
    _Continued.Quantity = 1;
    _Continued.SourceStockTypeID = "stock-250";
    _Continued.InstanceID = "locked-plan";
    _Continued.InitialProcessedEnd = 150.0;
    _Continued.InitialPredecessorPartID = "locked";
    _Continued.InitialPredecessorRotationRadians = 0.4;

    const auto _Result = SolveManufacturingNesting(
        { _Locked, _Next },
        { _Continued, { "new-stock", "section", 100.0, -1 } }, 5.0);

    ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 1U);
    const auto _Plan = _Plans.front().To<ObjectMap>();
    EXPECT_TRUE(_Plan.at("continued").To<bool>());
    EXPECT_EQ(_Plan.at("id").To<std::string>(), "locked-plan");
    EXPECT_EQ(_Plan.at("stockTypeId").To<std::string>(), "stock-250");
    EXPECT_NEAR(_Plan.at("initialProcessedEnd").To<double>(), 150.0, 1.0e-8);
    EXPECT_NEAR(_Plan.at("usedLength").To<double>(), 235.0, 1.0e-8);
    EXPECT_NEAR(_Plan.at("remainingLength").To<double>(), 15.0, 1.0e-8);
    const auto _Placements = _Plan.at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 1U);
    const auto _Placement = _Placements.front().To<ObjectMap>();
    EXPECT_EQ(_Placement.at("partId").To<std::string>(), "next");
    EXPECT_NEAR(_Placement.at("start").To<double>(), 135.0, 1.0e-8);
    EXPECT_NEAR(_Placement.at("end").To<double>(), 235.0, 1.0e-8);
    EXPECT_NEAR(_Placement.at("gapBefore").To<double>(), -15.0, 1.0e-8);
    EXPECT_TRUE(_Placement.at("nestedWithPrevious").To<bool>());
    EXPECT_NEAR(_Placement.at("rotationRadians").To<double>(), 0.4, 1.0e-12);
}

TEST(TubeDesignerNesting, PlacementStoresAuthoritativeTransformForSolvedDirection)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter-rotated";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    _Variant.Reversed = true;
    _Variant.RotationRadians = 1.57079632679489661923;
    const std::array<double, 3> _LocalCenter{ 10.0, 2.0, 3.0 };
    SNestingPart _Part{ "p", "section", 120.0, 1, { _Variant }, _LocalCenter };
    _Part.DirectionAllowed = { false, true };
    const auto _Result = SolveManufacturingNesting(
        { _Part }, { { "s", "section", 200.0, 1 } }, 0.0);
    const auto _Plan = _Result.at("plans").To<VariantArray>().front().To<ObjectMap>();
    const auto _Placement = _Plan.at("placements").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_TRUE(_Placement.at("reversed").To<bool>());
    EXPECT_NEAR(_Placement.at("rotationRadians").To<double>(), 0.0, 1e-12);
    const auto _Actual = _Placement.at("trsf").To<VariantArray>();
    const auto _Expected = MakeNestingPlacementTransform(
        _LocalCenter, _Placement.at("start").To<double>(), _Placement.at("end").To<double>(),
        true, 0.0);
    ASSERT_EQ(_Actual.size(), _Expected.size());
    for (std::size_t _Index = 0; _Index < _Actual.size(); ++_Index)
        EXPECT_NEAR(_Actual[_Index].To<double>(), _Expected[_Index].To<double>(), 1e-12);
    // The reverse base pose is a proper 180-degree rotation about local Y.
    // It maps the spectrum's circumferential coordinate theta to -theta,
    // exactly matching PairPoseTable::MakeSeries.
    EXPECT_NEAR(_Actual[0].To<double>(), -1.0, 1e-12);
    EXPECT_NEAR(_Actual[5].To<double>(), 1.0, 1e-12);
    EXPECT_NEAR(_Actual[10].To<double>(), -1.0, 1e-12);
}

TEST(TubeDesignerNesting, PlacementCarriesSolvedPairPoseMetadata)
{
    SNestingPart _A{ "a", "section", 100.0, 1 };
    SNestingPart _B{ "b", "section", 100.0, 1 };
    _A.PairGeometry = InsetRightProfile(100.0, 10.0);
    _B.PairGeometry = InsetRightProfile(100.0, 0.0);
    _B.PairGeometry->RelativeRotationDomain.DiscreteRadians = { 0.75 };
    _A.DirectionAllowed = { true, false };
    _B.DirectionAllowed = { true, false };
    const auto _Result = SolveManufacturingNesting(
        { _A, _B }, { { "s", "section", 190.0, 1 } }, 0.0);
    const auto _Placements = _Result.at("plans").To<VariantArray>().front()
        .To<ObjectMap>().at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 2u);
    const auto _Placement = _Placements[1].To<ObjectMap>();
    EXPECT_NEAR(_Placement.at("rotationRadians").To<double>(), 0.75, 1e-12);
    EXPECT_GT(_Placement.at("pairPoseIndex").To<unsigned long long>(), 0u);
    EXPECT_NEAR(_Placement.at("savingFromPrevious").To<double>(), 10.0, 1e-8);
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

TEST(TubeDesignerNesting, CompatiblePeriodicEndProfilesProduceCertifiedOverlap)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120.0;
    _Variant.MaterialLength = 110.0;
    SNestingPart _Part{ "miter-part", "section", 120.0, 2, { _Variant } };
    _Part.PairGeometry = InsetRightProfile(120.0, 10.0);
    _Part.DirectionAllowed = { true, false };
    const std::vector<SNestingPart> _Parts{ _Part };
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
    EXPECT_EQ(_Second.at("variantId").To<std::string>(), "forward");
    EXPECT_NEAR(_Plan.at("partLength").To<double>(), 220.0, 1e-8);
    EXPECT_NEAR(_Plan.at("usedLength").To<double>(), 230.0, 1e-8);
    const auto _Proof = _Result.at("proof").To<ObjectMap>();
    EXPECT_TRUE(_Proof.at("integerModelOptimalityProven").To<bool>());
    EXPECT_TRUE(_Proof.at("nativePairBoundsCertified").To<bool>());
}

TEST(TubeDesignerNesting, NumericPairProfilesNeverClaimNativeGeometryCertification)
{
    SNestingPart _Part{ "numeric", "section", 120.0, 2 };
    _Part.PairGeometry = InsetRightProfile(120.0, 10.0);
    _Part.PairGeometry->Left.SourceQuality =
        iCAX::TubeNesting::ProfileSourceQuality::NumericOnly;
    _Part.PairGeometry->Right.SourceQuality =
        iCAX::TubeNesting::ProfileSourceQuality::NumericOnly;
    _Part.DirectionAllowed = { true, false };

    const auto _Result = SolveManufacturingNesting(
        { _Part }, { { "stock", "section", 230.0, 1 } }, 0.0);

    EXPECT_EQ(_Result.at("status").To<std::string>(), "partial");
    EXPECT_EQ(_Result.at("unplaced").To<VariantArray>().size(), 1U);
    ASSERT_EQ(_Result.at("plans").To<VariantArray>().size(), 1U);
    EXPECT_EQ(_Result.at("plans").To<VariantArray>().front().To<ObjectMap>()
        .at("partCount").To<unsigned long long>(), 1U);
    const auto _Proof = _Result.at("proof").To<ObjectMap>();
    EXPECT_FALSE(_Proof.at("integerModelOptimalityProven").To<bool>());
    EXPECT_FALSE(_Proof.at("nativePairBoundsCertified").To<bool>());
}

namespace
{
    VariantArray SectionPoint(const double X_, const double Y_) { return { X_, Y_ }; }

    ObjectMap RoundedSectionLoop(const double Width_, const double Depth_, const double Radius_, const bool Inner_)
    {
        const double _X = Width_ / 2, _Y = Depth_ / 2, _R = Radius_;
        const double _Diagonal = _R / std::sqrt(2.0);
        const auto _Line = [](VariantArray Start_, VariantArray End_) {
            return ObjectMap{{"kind", std::string("line")}, {"start", Start_}, {"end", End_}};
        };
        const auto _Arc = [](VariantArray Start_, VariantArray Middle_, VariantArray End_) {
            return ObjectMap{{"kind", std::string("arc")}, {"start", Start_}, {"middle", Middle_}, {"end", End_}};
        };
        return {{"closed", true}, {"inner", Inner_}, {"segments", VariantArray{
            _Line(SectionPoint(-_X + _R, -_Y), SectionPoint(_X - _R, -_Y)),
            _Arc(SectionPoint(_X - _R, -_Y), SectionPoint(_X - _R + _Diagonal, -_Y + _R - _Diagonal), SectionPoint(_X, -_Y + _R)),
            _Line(SectionPoint(_X, -_Y + _R), SectionPoint(_X, _Y - _R)),
            _Arc(SectionPoint(_X, _Y - _R), SectionPoint(_X - _R + _Diagonal, _Y - _R + _Diagonal), SectionPoint(_X - _R, _Y)),
            _Line(SectionPoint(_X - _R, _Y), SectionPoint(-_X + _R, _Y)),
            _Arc(SectionPoint(-_X + _R, _Y), SectionPoint(-_X + _R - _Diagonal, _Y - _R + _Diagonal), SectionPoint(-_X, _Y - _R)),
            _Line(SectionPoint(-_X, _Y - _R), SectionPoint(-_X, -_Y + _R)),
            _Arc(SectionPoint(-_X, -_Y + _R), SectionPoint(-_X + _R - _Diagonal, -_Y + _R - _Diagonal), SectionPoint(-_X + _R, -_Y))}}};
    }

    ObjectMap RoundedSectionProfile(const double Wall_ = 1.0, const double Radius_ = 2.0)
    {
        return {{"id", std::string("rhs")}, {"kind", std::string("roundedRectangle")},
            {"width", 22.0}, {"depth", 18.0}, {"wallThickness", Wall_}, {"cornerRadius", Radius_}, {"hollow", true},
            {"contours", VariantArray{RoundedSectionLoop(22, 18, Radius_, false),
                RoundedSectionLoop(22 - 2 * Wall_, 18 - 2 * Wall_, Radius_ - Wall_, true)}}};
    }

    ObjectMap RetraverseSectionLoop(ObjectMap Loop_, const std::size_t Start_, const bool Reverse_)
    {
        auto _Segments = Loop_.at("segments").To<VariantArray>();
        std::rotate(_Segments.begin(), _Segments.begin() + Start_ % _Segments.size(), _Segments.end());
        if (Reverse_) {
            std::reverse(_Segments.begin(), _Segments.end());
            for (auto& _Value : _Segments) {
                auto _Edge = _Value.To<ObjectMap>();
                std::swap(_Edge.at("start"), _Edge.at("end"));
                _Value = std::move(_Edge);
            }
        }
        Loop_["segments"] = std::move(_Segments);
        return Loop_;
    }
}

TEST(TubeDesignerNesting, ProfileKeyUsesRealSectionAndIgnoresResourceAndTraversal)
{
    const auto _Original = RoundedSectionProfile();
    const auto _Key = BuildNestingSectionIdentity(_Original);
    ASSERT_FALSE(_Key.empty());
    // Imported and recognized profiles can omit declarations or label the
    // same real curves with R=0. Those metadata differences do not change stock.
    auto _IncompleteDeclarations = _Original;
    for (const auto* _Field : {"width", "depth", "diameter", "wallThickness", "hollow"})
        _IncompleteDeclarations.erase(_Field);
    _IncompleteDeclarations["cornerRadius"] = 0.0;
    EXPECT_EQ(BuildNestingSectionIdentity(_IncompleteDeclarations), _Key);
    EXPECT_TRUE(MatchesNestingProfileKey(_Key, _IncompleteDeclarations, "imported-part"));
    for (const bool _Reverse : {false, true}) for (std::size_t _Start = 0; _Start < 8; ++_Start) {
        SCOPED_TRACE(_Start);
        SCOPED_TRACE(_Reverse);
        auto _Profile = _Original;
        const auto _Loops = _Original.at("contours").To<VariantArray>();
        _Profile["contours"] = VariantArray{RetraverseSectionLoop(_Loops[1].To<ObjectMap>(), (_Start + 3) % 8, _Reverse),
            RetraverseSectionLoop(_Loops[0].To<ObjectMap>(), _Start, _Reverse)};
        _Profile["id"] = std::string("a-different-profile-resource");
        _Profile["kind"] = std::string("irregular");
        _Profile["sectionResource"] = ObjectMap{{"url", std::string("resource://another-section")}, {"version", 9ull}};
        _Profile["material"] = std::string("aluminium");
        _Profile["displayName"] = std::string("another user name");
        _Profile["parameters"] = ObjectMap{{"catalogId", std::string("another-library-entry")}};
        auto _DeclaredLoops = _Profile.at("contours").To<VariantArray>();
        for (auto& _Loop : _DeclaredLoops) {
            auto _Value = _Loop.To<ObjectMap>();
            _Value["inner"] = !_Value.at("inner").To<bool>();
            _Loop = std::move(_Value);
        }
        _Profile["contours"] = std::move(_DeclaredLoops);
        EXPECT_EQ(BuildNestingSectionIdentity(_Profile), _Key);
        EXPECT_TRUE(MatchesNestingProfileKey(_Key, _Profile, "a-different-part"));
    }
    ObjectMap _Polygon{{"contours", VariantArray{ObjectMap{{"closed", true}, {"points", VariantArray{
        SectionPoint(-11, -9), SectionPoint(11, -9), SectionPoint(11, 9), SectionPoint(-11, 9)}}}}}};
    const auto _PolygonKey = BuildNestingSectionIdentity(_Polygon);
    auto _Loop = _Polygon.at("contours").To<VariantArray>().front().To<ObjectMap>();
    auto _Points = _Loop.at("points").To<VariantArray>();
    std::rotate(_Points.begin(), _Points.begin() + 1, _Points.end()); std::reverse(_Points.begin(), _Points.end());
    _Loop["points"] = _Points; _Polygon["contours"] = VariantArray{_Loop};
    EXPECT_EQ(BuildNestingSectionIdentity(_Polygon), _PolygonKey);
}

TEST(TubeDesignerNesting, SectionIdentityDistinguishesActualWallRadiusInnerContourAndComplementaryArc)
{
    const auto _Original = RoundedSectionProfile();
    const auto _Key = BuildNestingSectionIdentity(_Original);
    // Both assertions change the true outer/inner closed curves, not just their declarations.
    EXPECT_NE(BuildNestingSectionIdentity(RoundedSectionProfile(1.5, 2.0)), _Key);
    EXPECT_NE(BuildNestingSectionIdentity(RoundedSectionProfile(1.0, 3.0)), _Key);
    auto _DifferentInner = _Original;
    auto _Loops = _Original.at("contours").To<VariantArray>();
    _Loops[1] = RoundedSectionLoop(19, 16, 1, true);
    _DifferentInner["contours"] = _Loops;
    EXPECT_NE(BuildNestingSectionIdentity(_DifferentInner), _Key);
    auto _DifferentOuter = _Original;
    _Loops = _Original.at("contours").To<VariantArray>();
    _Loops[0] = RoundedSectionLoop(24, 18, 2, false);
    _DifferentOuter["contours"] = _Loops;
    EXPECT_NE(BuildNestingSectionIdentity(_DifferentOuter), _Key);
    const auto _ArcProfile = [](const VariantArray& Middle_) {
        return ObjectMap{{"contours", VariantArray{ObjectMap{{"closed", true}, {"segments", VariantArray{
            ObjectMap{{"kind", std::string("arc")}, {"start", SectionPoint(1, 0)}, {"middle", Middle_}, {"end", SectionPoint(0, 1)}},
            ObjectMap{{"kind", std::string("line")}, {"start", SectionPoint(0, 1)}, {"end", SectionPoint(1, 0)}}}}}}}};
    };
    const auto _ShortArc = _ArcProfile(SectionPoint(std::sqrt(0.5), std::sqrt(0.5)));
    const auto _LongArc = _ArcProfile(SectionPoint(-1, 0));
    EXPECT_NE(BuildNestingSectionIdentity(_ShortArc), BuildNestingSectionIdentity(_LongArc));
}

TEST(TubeDesignerNesting, SectionIdentityNeverTrustsClientIdentityOrMissingContours)
{
    auto _Profile = RoundedSectionProfile();
    const auto _Key = BuildNestingSectionIdentity(_Profile);
    _Profile["sectionIdentity"] = std::string("client-forged-identity");
    EXPECT_EQ(BuildNestingSectionIdentity(_Profile), _Key);
    EXPECT_TRUE(MatchesNestingProfileKey(_Key, _Profile, "p"));
    EXPECT_FALSE(MatchesNestingProfileKey("client-forged-identity", _Profile, "p"));
    EXPECT_FALSE(MatchesNestingProfileKey(_Key + " ", _Profile, "p"));
    _Profile.erase("contours");
    EXPECT_THROW(BuildNestingSectionIdentity(_Profile), std::invalid_argument);
    EXPECT_FALSE(MatchesNestingProfileKey(_Key, _Profile, "p"));
    EXPECT_FALSE(MatchesNestingProfileKey("unknown:p", {}, "p"));
}

TEST(TubeDesignerNesting, SectionIdentityPreservesSplineTrimsPeriodicDataAndEllipseIntervals)
{
    const auto _Section = [](const ObjectMap& Edge_) {
        return ObjectMap{{"contours", VariantArray{ObjectMap{{"closed", true}, {"segments", VariantArray{Edge_}}}}}};
    };
    ObjectMap _Bezier{{"kind", std::string("bezier")}, {"controlPoints", VariantArray{
        SectionPoint(0, 0), SectionPoint(-2, 3), SectionPoint(4, 2), SectionPoint(0, 0)}},
        {"weights", VariantArray{1.0, 0.5, 0.75, 1.0}}};
    auto _Reversed = _Bezier;
    for (const auto* _Field : {"controlPoints", "weights"}) {
        auto _Values = _Reversed.at(_Field).To<VariantArray>();
        std::reverse(_Values.begin(), _Values.end()); _Reversed[_Field] = _Values;
    }
    EXPECT_EQ(BuildNestingSectionIdentity(_Section(_Bezier)), BuildNestingSectionIdentity(_Section(_Reversed)));
    // Reversing controls with the same asymmetric trim denotes a different
    // curve subset; an endpoint-only or unconditional reversal would merge it.
    _Bezier["startParameter"] = 0.1; _Bezier["endParameter"] = 0.7;
    _Reversed["startParameter"] = 0.1; _Reversed["endParameter"] = 0.7;
    EXPECT_NE(BuildNestingSectionIdentity(_Section(_Bezier)), BuildNestingSectionIdentity(_Section(_Reversed)));
    _Bezier.erase("startParameter"); _Bezier.erase("endParameter");
    _Reversed.erase("startParameter"); _Reversed.erase("endParameter");
    _Bezier["kind"] = std::string("bspline"); _Reversed["kind"] = std::string("bspline");
    _Bezier["periodic"] = true; _Reversed["periodic"] = true;
    _Bezier["knots"] = VariantArray{0.0, 0.5, 1.0}; _Reversed["knots"] = _Bezier.at("knots");
    EXPECT_NE(BuildNestingSectionIdentity(_Section(_Bezier)), BuildNestingSectionIdentity(_Section(_Reversed)));
    ObjectMap _Ellipse{{"kind", std::string("ellipseArc")}, {"center", SectionPoint(0, 0)},
        {"majorRadius", 5.0}, {"minorRadius", 2.0}, {"startParameter", 0.0}, {"endParameter", 1.0}};
    const auto _EllipseKey = BuildNestingSectionIdentity(_Section(_Ellipse));
    _Ellipse["endParameter"] = 1.0 + 2.0 * std::acos(-1.0);
    EXPECT_NE(BuildNestingSectionIdentity(_Section(_Ellipse)), _EllipseKey);
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

TEST(TubeDesignerNesting, StrictPriorityFreezesEachLevelAndContinuesOnTheSamePhysicalStock)
{
    SNestingPart _First{ "first", "section", 1000, 1 };
    SNestingPart _Second{ "second", "section", 500, 1 };
    _First.Priority = 2;
    _Second.Priority = 7;
    const std::vector<SNestingPart> _Parts{ _Second, _First };
    const std::vector<SNestingStock> _Stocks{ { "stock", "section", 2000, 1 } };

    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5);
    ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 1u);
    const auto _Placements = _Plans.front().To<ObjectMap>().at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 2u);
    EXPECT_EQ(_Placements[0].To<ObjectMap>().at("partId").To<std::string>(), "first");
    EXPECT_EQ(_Placements[1].To<ObjectMap>().at("partId").To<std::string>(), "second");
    EXPECT_EQ(_Placements[0].To<ObjectMap>().at("priority").To<unsigned long long>(), 2u);
    EXPECT_EQ(_Placements[1].To<ObjectMap>().at("priority").To<unsigned long long>(), 7u);
    EXPECT_NEAR(_Placements[1].To<ObjectMap>().at("start").To<double>(), 1005.0, 1e-8);
    EXPECT_EQ(_Result.at("proof").To<ObjectMap>().at("scope").To<std::string>(),
        "strict-priority-fixed-prefix-layers");
    CheckAccounting(_Result, _Parts, _Stocks, 5);
}

TEST(TubeDesignerNesting, StrictPriorityLayersAppendToOneExistingIrregularStock)
{
    SNestingPart _Locked{ "locked", "section", 100, 0 };
    SNestingPart _First{ "first", "section", 100, 1 };
    SNestingPart _Second{ "second", "section", 50, 1 };
    _Locked.PairGeometry = InsetRightProfile(100.0, 0.0);
    _First.PairGeometry = InsetRightProfile(100.0, 0.0);
    _Second.PairGeometry = InsetRightProfile(50.0, 0.0);
    _Locked.DirectionAllowed = { true, false };
    _First.DirectionAllowed = { true, false };
    _Second.DirectionAllowed = { true, false };
    _First.Priority = 2;
    _Second.Priority = 7;

    SNestingStock _Continued;
    _Continued.ID = "continued-row";
    _Continued.ProfileKey = "section";
    _Continued.Length = 400.0;
    _Continued.Quantity = 1;
    _Continued.SourceStockTypeID = "stock-400";
    _Continued.InstanceID = "existing-plan";
    _Continued.InitialProcessedEnd = 150.0;
    _Continued.InitialPredecessorPartID = "locked";

    const std::vector<SNestingPart> _Parts{ _Second, _Locked, _First };
    const std::vector<SNestingStock> _Stocks{
        _Continued, { "new-stock", "section", 400.0, -1 }
    };
    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 5.0);

    ASSERT_TRUE(_Result.at("unplaced").To<VariantArray>().empty());
    const auto _Plans = _Result.at("plans").To<VariantArray>();
    ASSERT_EQ(_Plans.size(), 1U);
    const auto _Plan = _Plans.front().To<ObjectMap>();
    EXPECT_TRUE(_Plan.at("continued").To<bool>());
    EXPECT_EQ(_Plan.at("id").To<std::string>(), "existing-plan");
    EXPECT_EQ(_Plan.at("stockTypeId").To<std::string>(), "stock-400");
    EXPECT_NEAR(_Plan.at("initialProcessedEnd").To<double>(), 150.0, 1.0e-8);
    EXPECT_NEAR(_Plan.at("usedLength").To<double>(), 310.0, 1.0e-8);
    EXPECT_NEAR(_Plan.at("remainingLength").To<double>(), 90.0, 1.0e-8);
    const auto _Placements = _Plan.at("placements").To<VariantArray>();
    ASSERT_EQ(_Placements.size(), 2U);
    const auto _FirstPlacement = _Placements[0].To<ObjectMap>();
    const auto _SecondPlacement = _Placements[1].To<ObjectMap>();
    EXPECT_EQ(_FirstPlacement.at("partId").To<std::string>(), "first");
    EXPECT_EQ(_SecondPlacement.at("partId").To<std::string>(), "second");
    EXPECT_EQ(_FirstPlacement.at("priority").To<unsigned long long>(), 2U);
    EXPECT_EQ(_SecondPlacement.at("priority").To<unsigned long long>(), 7U);
    EXPECT_NEAR(_FirstPlacement.at("start").To<double>(), 155.0, 1.0e-8);
    EXPECT_NEAR(_SecondPlacement.at("start").To<double>(), 260.0, 1.0e-8);
    EXPECT_EQ(_Result.at("proof").To<ObjectMap>().at("scope").To<std::string>(),
        "strict-priority-fixed-prefix-layers");
}

TEST(TubeDesignerNesting, StrictPriorityDoesNotStartLowerLevelWhenHigherLevelIsUnplaced)
{
    SNestingPart _Oversize{ "oversize", "section", 7000, 1 };
    SNestingPart _Fits{ "fits", "section", 1000, 1 };
    _Oversize.Priority = 0;
    _Fits.Priority = 1;
    const std::vector<SNestingPart> _Parts{ _Fits, _Oversize };
    const std::vector<SNestingStock> _Stocks{ { "stock", "section", 6000, 1 } };

    const auto _Result = SolveManufacturingNesting(_Parts, _Stocks, 0);
    EXPECT_EQ(_Result.at("status").To<std::string>(), "infeasible");
    EXPECT_TRUE(_Result.at("plans").To<VariantArray>().empty());
    const auto _Unplaced = _Result.at("unplaced").To<VariantArray>();
    ASSERT_EQ(_Unplaced.size(), 2u);
    const auto _Lower = std::ranges::find_if(_Unplaced, [](const auto& Value_)
    {
        return Value_.To<ObjectMap>().at("partId").To<std::string>() == "fits";
    });
    ASSERT_NE(_Lower, _Unplaced.end());
    EXPECT_NE(_Lower->To<ObjectMap>().at("reason").To<std::string>().find("未启动"),
        std::string::npos);
    CheckAccounting(_Result, _Parts, _Stocks, 0);
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

TEST(TubeDesignerNesting, LargeResultIsDeliveredAsNoticeCatalogAndSelectedPlan)
{
    namespace access = iCAX::TubeDesigner::NestingResultAccess;
    VariantArray plans;
    plans.reserve(9499);
    for (std::size_t index = 0; index < 9499; ++index)
    {
        VariantArray placements;
        for (std::size_t part = 0; part < 5; ++part)
            placements.emplace_back(ObjectMap{
                { "partId", std::string("part-") + std::to_string(part) },
                { "instanceId", std::string("instance-") + std::to_string(index * 5 + part) },
                { "start", static_cast<double>(part * 100) },
                { "end", static_cast<double>((part + 1) * 100) },
                { "gapBefore", 0.0 }, { "reversed", false },
                { "nestedWithPrevious", false }, { "rotationRadians", 0.0 },
                { "variantId", std::string("default") }
            });
        plans.emplace_back(ObjectMap{
            { "id", std::string("plan-") + std::to_string(index) },
            { "profileKey", std::string("profile-") + std::to_string(index % 7) },
            { "stockTypeId", std::string("stock-") + std::to_string(index % 7) },
            { "stockLength", 1800.0 }, { "usedLength", 500.0 },
            { "remainingLength", 1300.0 }, { "partLength", 500.0 },
            { "utilization", 500.0 / 1800.0 }, { "placements", std::move(placements) }
        });
    }
    const ObjectMap task{
        { "revision", std::string("large-revision") },
        { "parts", VariantArray{} },
        { "request", ObjectMap{ { "lockedPlans", VariantArray{ plans.front() } } } },
        { "result", ObjectMap{ { "status", std::string("feasible") },
            { "metrics", ObjectMap{ { "placedPartCount", 47495ull } } },
            { "unplaced", VariantArray{} }, { "plans", std::move(plans) } } }
    };
    const auto summary = access::Summary(task);
    const auto snapshot = access::TaskSummary(task);
    const auto catalog = access::Catalog(task, "large-revision");
    const auto selected = access::Plan(task, "large-revision", "plan-42");
    EXPECT_FALSE(summary.contains("plans"));
    EXPECT_FALSE(access::Object(snapshot, "result").contains("plans"));
    EXPECT_EQ(access::Array(access::Object(snapshot, "request"), "lockedPlans").size(), 1u);
    EXPECT_EQ(access::Array(catalog, "rows").size(), 9499u);
    EXPECT_EQ(access::Array(selected, "placements").size(), 5u);
    EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(summary)).size(), 4096u);
    EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(iCAX::Data::Variant(catalog)).size(), 16u * 1024u * 1024u);
    EXPECT_THROW(access::Catalog(task, "old-revision"), std::invalid_argument);
    EXPECT_THROW(access::Plan(task, "large-revision", "missing"), std::invalid_argument);
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

TEST(TubeDesignerNesting, ParallelGroupsPreservePairProfilesAndRecoverAfterInvalidInput)
{
    SNestingVariant _Variant;
    _Variant.ID = "miter";
    _Variant.EnvelopeLength = 120;
    _Variant.MaterialLength = 110;
    SNestingPart _A{ "a", "a", 120, 2, { _Variant } };
    SNestingPart _B{ "b", "b", 120, 2, { _Variant } };
    _A.PairGeometry = InsetRightProfile(120.0, 10.0);
    _B.PairGeometry = InsetRightProfile(120.0, 10.0);
    _A.DirectionAllowed = { true, false };
    _B.DirectionAllowed = { true, false };
    const std::vector<SNestingPart> _Parts{ _A, _B };
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
    EXPECT_EQ(_Registry->GetPropertyPersistenceByName(CManufacturingPartComponent::S_ClassName,
        CManufacturingPartComponent::PropertyName_NestingPriority), EPropertyPersistence::Persistent);
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
