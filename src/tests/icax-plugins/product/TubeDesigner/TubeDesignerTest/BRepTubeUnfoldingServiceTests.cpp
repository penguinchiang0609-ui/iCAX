#include "pch.h"

#include <TubeDesigner/BRepTubeUnfoldingService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>

#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <memory>
#include <string>

namespace
{
    TopoDS_Shape HollowRectangularTube()
    {
        const auto _Outer = BRepPrimAPI_MakeBox(
            gp_Pnt(125.0, 30.0, 40.0), 200.0, 30.0, 20.0).Shape();
        const auto _Inner = BRepPrimAPI_MakeBox(
            gp_Pnt(124.0, 32.0, 42.0), 202.0, 26.0, 16.0).Shape();
        return BRepAlgoAPI_Cut(_Outer, _Inner).Shape();
    }

    TopoDS_Shape SlantedEndTube()
    {
        const auto _Tube = HollowRectangularTube();
        const gp_Pnt _Origin(325.0, 45.0, 50.0);
        const gp_Dir _Normal(1.0, -0.55, -0.25);
        const auto _Plane = BRepBuilderAPI_MakeFace(
            gp_Pln(_Origin, _Normal)).Face();
        const auto _Keep = BRepPrimAPI_MakeHalfSpace(
            _Plane, _Origin.Translated(gp_Vec(_Normal) * -100.0)).Solid();
        return BRepAlgoAPI_Common(_Tube, _Keep).Shape();
    }

    std::shared_ptr<iCAX::TubeDesigner::IBRepTubeUnfoldingService> Unfolder(
        iCAX::Services::CServiceProvider& Services_)
    {
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(Services_);
        return Services_.Resolve<iCAX::TubeDesigner::IBRepTubeUnfoldingService>();
    }

    std::string Diagnostic(const iCAX::TubeDesigner::STubeBRepUnfoldingResult& Result_)
    {
        return Result_.Diagnostic.empty() ? "no diagnostic" : Result_.Diagnostic;
    }
}

TEST(BRepTubeUnfoldingService, UnfoldsHollowRectangularTubeIntoOwnArcLengthSpace)
{
    iCAX::Services::CServiceProvider _Services;
    const auto _Service = Unfolder(_Services);
    ASSERT_NE(_Service, nullptr);

    const auto _Result = _Service->Unfold(HollowRectangularTube());
    ASSERT_TRUE(_Result.bOK) << Diagnostic(_Result);
    EXPECT_EQ(_Result.CoordinateSpace, "axial-arc-length");
    EXPECT_FALSE(_Result.Method.empty());
    EXPECT_TRUE(_Result.Periodic);
    EXPECT_NEAR(_Result.Length, 200.0, 0.5);
    EXPECT_NEAR(_Result.Perimeter, 100.0, 0.5);
    EXPECT_GE(_Result.ProfileStationCount, 5u);
    ASSERT_FALSE(_Result.Surfaces.empty());
    ASSERT_TRUE(_Result.LateralRectangle.Available);
    EXPECT_NEAR(_Result.LateralRectangle.SStart, 0.0, 1.0e-6);
    EXPECT_NEAR(_Result.LateralRectangle.SEnd, 200.0, 0.5);
    EXPECT_NEAR(_Result.LateralRectangle.UStart, 0.0, 1.0e-6);
    EXPECT_NEAR(_Result.LateralRectangle.UEnd, 100.0, 0.5);
    ASSERT_TRUE(_Result.Surfaces.front().Rectangle.Available);
    EXPECT_NEAR(_Result.Surfaces.front().Rectangle.SEnd, 200.0, 0.5);
    EXPECT_NEAR(_Result.Surfaces.front().Rectangle.UEnd, 100.0, 0.5);
    ASSERT_FALSE(_Result.Surfaces.front().Panels.empty());
    EXPECT_GT(_Result.Surfaces.front().UPeriod, 0.0);
    EXPECT_GT(_Result.PointCount, 0u);

    const auto _Snapshot = iCAX::TubeDesigner::SerializeTubeBRepUnfolding(_Result);
    EXPECT_EQ(_Snapshot.at("schema").To<std::string>(), "icax.tube-brep-unfolding");
    EXPECT_EQ(_Snapshot.at("schemaVersion").To<unsigned long long>(), 1ull);
    EXPECT_TRUE(_Snapshot.at("available").To<bool>());
    ASSERT_TRUE(_Snapshot.at("lateralRectangle").Is<iCAX::Data::ObjectMap>());
    EXPECT_TRUE(_Snapshot.at("lateralRectangle").To<iCAX::Data::ObjectMap>()
        .at("available").To<bool>());
    EXPECT_FALSE(_Snapshot.at("surfaces").To<iCAX::Data::VariantArray>().empty());
}

TEST(BRepTubeUnfoldingService, AcceptsThePersistedBRepModelContract)
{
    iCAX::Services::CServiceProvider _Services;
    const auto _Service = Unfolder(_Services);
    const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
        HollowRectangularTube(), "tube", "source");
    const auto _Result = _Service->Unfold(_BRep);
    ASSERT_TRUE(_Result.bOK) << Diagnostic(_Result);
    EXPECT_NEAR(_Result.Length, 200.0, 0.5);
    EXPECT_NEAR(_Result.Perimeter, 100.0, 0.5);
}

TEST(BRepTubeUnfoldingService, KeepsTheSameContractForRotatedPlacement)
{
    gp_Trsf _Rotation;
    _Rotation.SetRotation(
        gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 2.0, 3.0)), 0.73);
    const auto _Rotated = BRepBuilderAPI_Transform(
        HollowRectangularTube(), _Rotation, true).Shape();

    const auto _Result = iCAX::TubeDesigner::UnfoldTubeBRepSurface(_Rotated);
    ASSERT_TRUE(_Result.bOK) << Diagnostic(_Result);
    EXPECT_NEAR(_Result.Length, 200.0, 0.5);
    EXPECT_NEAR(_Result.Perimeter, 100.0, 0.5);
    EXPECT_TRUE(std::isfinite(_Result.MaximumProjectionResidual));
}

TEST(BRepTubeUnfoldingService, PreservesSlantedEndBoundariesAsUnwrappedWires)
{
    const auto _Result = iCAX::TubeDesigner::UnfoldTubeBRepSurface(SlantedEndTube());
    ASSERT_TRUE(_Result.bOK) << Diagnostic(_Result);
    ASSERT_FALSE(_Result.Surfaces.empty());
    std::size_t _EndBoundaryCount = 0;
    for (const auto& _Surface : _Result.Surfaces)
        for (const auto& _Wire : _Surface.Wires)
            if (_Wire.Role == "end-boundary") ++_EndBoundaryCount;
    EXPECT_GT(_EndBoundaryCount, 0u);
    EXPECT_TRUE(std::isfinite(_Result.ProfileMismatch));
}

TEST(BRepTubeUnfoldingService, EncodesPeriodicEndCurvesForNesting)
{
    const auto _Result = iCAX::TubeDesigner::UnfoldTubeBRepSurface(SlantedEndTube());
    ASSERT_TRUE(_Result.bOK) << Diagnostic(_Result);

    const auto _Features = iCAX::TubeDesigner::EncodeTubeUnfoldedEndFeatures(_Result, 32);
    ASSERT_TRUE(_Features.bOK) << _Features.Diagnostic;
    EXPECT_TRUE(iCAX::TubeNesting::IsValidCutLineFeature(_Features.Left));
    EXPECT_TRUE(iCAX::TubeNesting::IsValidCutLineFeature(_Features.Right));
    EXPECT_EQ(_Features.Left.Period, _Features.Right.Period);
    // A flat end is intentionally compressed to Constant/Linear. Only a
    // genuinely non-linear slanted end needs the full periodic sample array.
    if (_Features.Left.Kind == iCAX::TubeNesting::CutLineFeatureKind::Sampled)
        EXPECT_EQ(_Features.Left.Samples.size(), 32U);
    if (_Features.Right.Kind == iCAX::TubeNesting::CutLineFeatureKind::Sampled)
        EXPECT_EQ(_Features.Right.Samples.size(), 32U);
    EXPECT_TRUE(_Features.Left.Kind != iCAX::TubeNesting::CutLineFeatureKind::Invalid);
    EXPECT_TRUE(_Features.Right.Kind != iCAX::TubeNesting::CutLineFeatureKind::Invalid);

    const auto _Snapshot = iCAX::TubeDesigner::SerializeTubeBRepUnfolding(_Result);
    ASSERT_TRUE(_Snapshot.at("nestingFeatures").Is<iCAX::Data::ObjectMap>());
    EXPECT_TRUE(_Snapshot.at("nestingFeatures").To<iCAX::Data::ObjectMap>()
        .at("available").To<bool>());
}

TEST(BRepTubeUnfoldingService, RejectsEmptyInputWithoutThrowing)
{
    const auto _Result = iCAX::TubeDesigner::UnfoldTubeBRepSurface(TopoDS_Shape{});
    EXPECT_FALSE(_Result.bOK);
    EXPECT_FALSE(_Result.Diagnostic.empty());
}
