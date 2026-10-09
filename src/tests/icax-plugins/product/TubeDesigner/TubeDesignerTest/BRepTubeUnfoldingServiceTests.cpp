#include "pch.h"

#include <TubeDesigner/BRepTubeUnfoldingService.h>
#include <TubeDesigner/TubeSideSketchCurveService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <TemplateRuntime/StandardJsonCodec.h>

#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <chrono>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

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

    TopoDS_Shape WideHollowRectangularTube()
    {
        const auto _Outer=BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),200.0,80.0,60.0).Shape();
        const auto _Inner=BRepPrimAPI_MakeBox(gp_Pnt(-1,2,2),202.0,76.0,56.0).Shape();
        return BRepAlgoAPI_Cut(_Outer,_Inner).Shape();
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

    std::size_t CountSketchCurveEdges(const TopoDS_Shape& Shape_, GeomAbs_CurveType Type_)
    {
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> _Edges;
        TopExp::MapShapes(Shape_, TopAbs_EDGE, _Edges);
        std::size_t _Count = 0;
        for (int _Index = 1; _Index <= _Edges.Extent(); ++_Index)
            if (BRepAdaptor_Curve(TopoDS::Edge(_Edges(_Index))).GetType() == Type_)
                ++_Count;
        return _Count;
    }

    std::size_t SketchEdgeCount(const TopoDS_Shape& Shape_)
    {
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> _Edges;
        TopExp::MapShapes(Shape_, TopAbs_EDGE, _Edges);
        return static_cast<std::size_t>(_Edges.Extent());
    }

    std::size_t CountNonlinearLateralPCurves(const TopoDS_Shape& Shape_)
    {
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> _Edges;
        for (TopExp_Explorer _Faces(Shape_, TopAbs_FACE); _Faces.More(); _Faces.Next())
        {
            const auto _Face = TopoDS::Face(_Faces.Current());
            if (BRepAdaptor_Surface(_Face).GetType() == GeomAbs_Plane) continue;
            for (TopExp_Explorer _Iterator(_Face, TopAbs_EDGE); _Iterator.More(); _Iterator.Next())
            {
                const auto _Edge = TopoDS::Edge(_Iterator.Current());
                double _First = 0.0, _Last = 0.0;
                const auto _PCurve = BRep_Tool::CurveOnSurface(_Edge, _Face, _First, _Last);
                if (_PCurve.IsNull() || _Last - _First <= 1.0e-10) continue;
                // A blank extrusion's end boundaries and axial seam have line
                // pcurves. Only a mapped sketch curve introduces a nonlinear one.
                if (Geom2dAdaptor_Curve(_PCurve, _First, _Last).GetType() != GeomAbs_Line
                    && BRepAdaptor_Curve(_Edge).GetType() != GeomAbs_Line)
                    _Edges.Add(_Edge);
            }
        }
        return static_cast<std::size_t>(_Edges.Extent());
    }

    iCAX::Data::ObjectMap CurveSketch(double Perimeter_, iCAX::Data::VariantArray Entities_)
    {
        return {{"length",200.0},{"faceHeight",Perimeter_},
            {"coordinateSpace",std::string("arc-length-axial")},
            {"trajectoryWidth",1.0},{"entities",std::move(Entities_)}};
    }

    TopoDS_Shape RoundTube(double OuterRadius_ = 15.0, double InnerRadius_ = 13.0,
        double Offset_ = 0.0)
    {
        return BRepAlgoAPI_Cut(
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(),gp::DX()),OuterRadius_,200.0).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,Offset_,0),gp::DX()),InnerRadius_,200.0).Shape()).Shape();
    }

    TopoDS_Shape RoundedRectangleTube(double Radius_ = 5.0)
    {
        const auto _Section = [](double W_, double H_, double R_) {
            if (R_ <= 1.0e-8)
                return BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),200.0,W_,H_).Shape();
            const auto _Point = [](double Y_, double Z_) { return gp_Pnt(0,Y_,Z_); };
            const auto _Q = R_ / std::sqrt(2.0);
            BRepBuilderAPI_MakeWire _Wire;
            const std::array<std::array<gp_Pnt,3>,4> _Corners{{
                {_Point(W_-R_,0),_Point(W_-R_+_Q,R_-_Q),_Point(W_,R_)},
                {_Point(W_,H_-R_),_Point(W_-R_+_Q,H_-R_+_Q),_Point(W_-R_,H_)},
                {_Point(R_,H_),_Point(R_-_Q,H_-R_+_Q),_Point(0,H_-R_)},
                {_Point(0,R_),_Point(R_-_Q,R_-_Q),_Point(R_,0)}}};
            for (std::size_t _Index=0; _Index<4; ++_Index)
            {
                _Wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(
                    _Corners[_Index][0],_Corners[_Index][1],_Corners[_Index][2]).Value()).Edge());
                _Wire.Add(BRepBuilderAPI_MakeEdge(_Corners[_Index][2],_Corners[(_Index+1)%4][0]).Edge());
            }
            return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(_Wire.Wire()).Face(),gp_Vec(200,0,0)).Shape();
        };
        gp_Trsf _InnerPlacement;
        _InnerPlacement.SetTranslation(gp_Vec(0,2,2));
        return BRepAlgoAPI_Cut(_Section(30,20,Radius_),
            BRepBuilderAPI_Transform(_Section(26,16,std::max(0.0,Radius_-2.0)),_InnerPlacement,true).Shape()).Shape();
    }

    TopoDS_Shape EllipticalTube()
    {
        const auto _Solid=[](double Major_,double Minor_) {
            const gp_Elips _Ellipse(gp_Ax2(gp::Origin(),gp::DX(),gp::DY()),Major_,Minor_);
            const auto _Wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(_Ellipse).Edge()).Wire();
            return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(_Wire).Face(),gp_Vec(200,0,0)).Shape();
        };
        return BRepAlgoAPI_Cut(_Solid(20.0,12.0),_Solid(18.0,10.0)).Shape();
    }

    TopoDS_Shape QuarteredConicTube(bool Elliptical_)
    {
        const auto _Solid = [Elliptical_](double Major_, double Minor_) {
            BRepBuilderAPI_MakeWire _Wire;
            const gp_Ax2 _Frame(gp::Origin(), gp::DX(), gp::DY());
            for (int _Index = 0; _Index < 4; ++_Index)
            {
                const auto _First = _Index * M_PI_2;
                const auto _Last = (_Index + 1) * M_PI_2;
                _Wire.Add(Elliptical_
                    ? BRepBuilderAPI_MakeEdge(gp_Elips(_Frame, Major_, Minor_), _First, _Last).Edge()
                    : BRepBuilderAPI_MakeEdge(gp_Circ(_Frame, Major_), _First, _Last).Edge());
            }
            return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(_Wire.Wire()).Face(),
                gp_Vec(200.0, 0.0, 0.0)).Shape();
        };
        return BRepAlgoAPI_Cut(_Solid(20.0, 12.0), _Solid(18.0, 10.0)).Shape();
    }

    TopoDS_Shape SegmentedRectangleTube()
    {
        const std::array<gp_Pnt, 8> _Points{gp_Pnt(0, 0, 0), gp_Pnt(0, 15, 0),
            gp_Pnt(0, 30, 0), gp_Pnt(0, 30, 10), gp_Pnt(0, 30, 20),
            gp_Pnt(0, 15, 20), gp_Pnt(0, 0, 20), gp_Pnt(0, 0, 10)};
        BRepBuilderAPI_MakeWire _Wire;
        for (std::size_t _Index = 0; _Index < _Points.size(); ++_Index)
            _Wire.Add(BRepBuilderAPI_MakeEdge(_Points[_Index],
                _Points[(_Index + 1) % _Points.size()]).Edge());
        const auto _Outer = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(_Wire.Wire()).Face(),
            gp_Vec(200.0, 0.0, 0.0)).Shape();
        return BRepAlgoAPI_Cut(_Outer,
            BRepPrimAPI_MakeBox(gp_Pnt(-1.0, 2.0, 2.0), 202.0, 26.0, 16.0).Shape()).Shape();
    }

    std::vector<double> ProfileJunctionCoordinates(
        const iCAX::TubeDesigner::STubeBRepUnfoldingResult& Reference_)
    {
        std::vector<double> _Coordinates;
        for (const auto& _Surface : Reference_.Surfaces)
            for (const auto& _Wire : _Surface.Wires)
                if (_Wire.Role == "profile-junction")
                {
                    EXPECT_FALSE(_Surface.Inner);
                    EXPECT_FALSE(_Wire.Closed);
                    EXPECT_FALSE(_Wire.WrapsAroundSeam);
                    EXPECT_EQ(_Wire.Points.size(), 2u);
                    if (_Wire.Points.size() != 2u) continue;
                    EXPECT_DOUBLE_EQ(_Wire.Points.front().S, 0.0);
                    EXPECT_DOUBLE_EQ(_Wire.Points.back().S, Reference_.Length);
                    EXPECT_DOUBLE_EQ(_Wire.Points.front().U, _Wire.Points.back().U);
                    EXPECT_GT(_Wire.Points.front().U, 0.0);
                    EXPECT_LT(_Wire.Points.front().U, Reference_.Perimeter);
                    _Coordinates.push_back(_Wire.Points.front().U);
                }
        return _Coordinates;
    }
}

TEST(BRepTubeUnfoldingService, SideSketchReferenceExportsExactProfileJunctions)
{
    const auto _Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(
        HollowRectangularTube());
    ASSERT_TRUE(_Reference.bOK) << _Reference.Diagnostic;
    const auto _Coordinates = ProfileJunctionCoordinates(_Reference);
    ASSERT_EQ(_Coordinates.size(), 3u);
    const std::array<double, 3> _Expected{30.0, 50.0, 80.0};
    for (std::size_t _Index = 0; _Index < _Expected.size(); ++_Index)
        EXPECT_NEAR(_Coordinates[_Index], _Expected[_Index], 1.0e-7);
    EXPECT_EQ(_Reference.PointCount, 10u);
    const auto _Snapshot = iCAX::TubeDesigner::SerializeTubeBRepUnfolding(_Reference);
    std::size_t _SerializedJunctions = 0;
    for (const auto& _SurfaceValue : _Snapshot.at("surfaces").To<iCAX::Data::VariantArray>())
        for (const auto& _WireValue : _SurfaceValue.To<iCAX::Data::ObjectMap>()
            .at("wires").To<iCAX::Data::VariantArray>())
            if (_WireValue.To<iCAX::Data::ObjectMap>().at("role").To<std::string>()
                == "profile-junction") ++_SerializedJunctions;
    EXPECT_EQ(_SerializedJunctions, 3u);
}

TEST(BRepTubeUnfoldingService, SideSketchReferenceKeepsTangentJunctionsAndExcludesArtificialSeam)
{
    const auto _Shape = RoundedRectangleTube();
    const auto _Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Reference.bOK) << _Reference.Diagnostic;
    const auto _Coordinates = ProfileJunctionCoordinates(_Reference);
    ASSERT_EQ(_Coordinates.size(), 7u);
    const auto _Arc = 5.0 * 3.14159265358979323846 / 2.0;
    const std::array<double, 7> _Expected{_Arc, _Arc + 20.0,
        2.0 * _Arc + 20.0, 2.0 * _Arc + 30.0, 3.0 * _Arc + 30.0,
        3.0 * _Arc + 50.0, 4.0 * _Arc + 50.0};
    for (std::size_t _Index = 0; _Index < _Expected.size(); ++_Index)
        EXPECT_NEAR(_Coordinates[_Index], _Expected[_Index], 1.0e-7);
    gp_Trsf _Rotation;
    _Rotation.SetRotation(gp_Ax1(gp::Origin(), gp::DX()), 0.37);
    const auto _Rotated = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(
        BRepBuilderAPI_Transform(_Shape, _Rotation, true).Shape());
    ASSERT_TRUE(_Rotated.bOK) << _Rotated.Diagnostic;
    const auto _RotatedCoordinates = ProfileJunctionCoordinates(_Rotated);
    ASSERT_EQ(_RotatedCoordinates.size(), 8u);
    std::vector<double> _Spans;
    for (std::size_t _Index = 1; _Index < _RotatedCoordinates.size(); ++_Index)
        _Spans.push_back(_RotatedCoordinates[_Index] - _RotatedCoordinates[_Index - 1]);
    _Spans.push_back(_Rotated.Perimeter - _RotatedCoordinates.back()
        + _RotatedCoordinates.front());
    std::sort(_Spans.begin(), _Spans.end());
    const std::array<double, 8> _ExpectedSpans{_Arc, _Arc, _Arc, _Arc, 10.0, 10.0, 20.0, 20.0};
    for (std::size_t _Index = 0; _Index < _Spans.size(); ++_Index)
        EXPECT_NEAR(_Spans[_Index], _ExpectedSpans[_Index], 1.0e-7);
}

TEST(BRepTubeUnfoldingService, SideSketchReferenceHasNoJunctionInsideOneCompleteCircleOrEllipse)
{
    gp_Trsf _Placement;
    _Placement.SetRotation(gp_Ax1(gp::Origin(), gp_Dir(1.0, 2.0, 3.0)), 0.73);
    _Placement.SetTranslationPart(gp_Vec(81.0, -27.0, 39.0));
    for (const auto& _Shape : {RoundTube(), EllipticalTube()})
        for (const auto& _Placed : {_Shape,
            BRepBuilderAPI_Transform(_Shape, _Placement, true).Shape()})
        {
            const auto _Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Placed);
            ASSERT_TRUE(_Reference.bOK) << _Reference.Diagnostic;
            EXPECT_TRUE(ProfileJunctionCoordinates(_Reference).empty());
            EXPECT_EQ(_Reference.PointCount, 4u);
        }
}

TEST(BRepTubeUnfoldingService, SideSketchReferenceIgnoresArtificialAnalyticCurveSegments)
{
    gp_Trsf _Placement;
    _Placement.SetRotation(gp_Ax1(gp::Origin(), gp_Dir(1.0, 2.0, 3.0)), 0.73);
    _Placement.SetTranslationPart(gp_Vec(81.0, -27.0, 39.0));
    for (const auto& _Shape : {QuarteredConicTube(false), QuarteredConicTube(true)})
        for (const auto& _Placed : {_Shape,
            BRepBuilderAPI_Transform(_Shape, _Placement, true).Shape()})
        {
            ASSERT_TRUE(BRepCheck_Analyzer(_Placed).IsValid());
            const auto _Reference = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Placed);
            ASSERT_TRUE(_Reference.bOK) << _Reference.Diagnostic;
            EXPECT_TRUE(ProfileJunctionCoordinates(_Reference).empty());
            EXPECT_EQ(_Reference.PointCount, 4u);
        }
    const auto _Segmented = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(
        SegmentedRectangleTube());
    ASSERT_TRUE(_Segmented.bOK) << _Segmented.Diagnostic;
    const auto _Coordinates = ProfileJunctionCoordinates(_Segmented);
    ASSERT_EQ(_Coordinates.size(), 3u);
    const std::array<double, 3> _Expected{30.0, 50.0, 80.0};
    for (std::size_t _Index = 0; _Index < _Expected.size(); ++_Index)
        EXPECT_NEAR(_Coordinates[_Index], _Expected[_Index], 1.0e-7);
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

    const auto _Profiles = iCAX::TubeDesigner::EncodeTubeNestingEndProfiles(
        _Result, 64, 16);
    ASSERT_TRUE(_Profiles.bOK) << _Profiles.Diagnostic;
    EXPECT_GT(_Profiles.MaximumLength, 0);
    EXPECT_TRUE(_Profiles.Geometry.Left.CompleteSingleValued);
    EXPECT_TRUE(_Profiles.Geometry.Right.CompleteSingleValued);
    ASSERT_FALSE(_Profiles.Geometry.Left.Levels.empty());
    ASSERT_FALSE(_Profiles.Geometry.Right.Levels.empty());
    EXPECT_EQ(_Profiles.Geometry.Left.SourceQuality,
        iCAX::TubeNesting::ProfileSourceQuality::NumericOnly);
    EXPECT_EQ(_Profiles.Geometry.Right.SourceQuality,
        iCAX::TubeNesting::ProfileSourceQuality::NumericOnly);

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

TEST(BRepTubeUnfoldingService, AppliesCompleteWrappedRegionToTheActualBRep)
{
    const auto _Shape = HollowRectangularTube();
    iCAX::Data::ObjectMap _Sketch{
        { "schema", std::string("icax.tube-sketch") },
        { "schemaVersion", 1ull },
        { "kind", std::string("side") },
        { "unit", std::string("mm") },
        { "length", 200.0 },
        { "faceHeight", 100.0 },
        { "entities", iCAX::Data::VariantArray{
            iCAX::Data::ObjectMap{
                { "id", std::string("window") },
                { "kind", std::string("rectangle") },
                { "x", 60.0 }, { "y", 20.0 },
                { "width", 30.0 }, { "height", 25.0 },
                { "radius", 0.0 }, { "closed", true }
            }
        }}
    };

    GProp_GProps _BeforeProperties;
    BRepGProp::VolumeProperties(_Shape, _BeforeProperties);
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape, _Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(_Result.Changed);
    EXPECT_EQ(_Result.Method, "curve-preserving-profile-surface");
    EXPECT_EQ(_Result.ClosedLoopCount, 1u);
    EXPECT_EQ(_Result.OpenTrajectoryCount, 0u);
    ASSERT_FALSE(_Result.Shape.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());

    GProp_GProps _AfterProperties;
    BRepGProp::VolumeProperties(_Result.Shape, _AfterProperties);
    EXPECT_LT(_AfterProperties.Mass(), _BeforeProperties.Mass());
}

TEST(BRepTubeUnfoldingService, AppliesOpenWrappedTrajectoryWithAProcessWidth)
{
    const auto _Shape = HollowRectangularTube();
    iCAX::Data::ObjectMap _Sketch{
        { "schema", std::string("icax.tube-sketch") },
        { "schemaVersion", 1ull },
        { "kind", std::string("side") },
        { "unit", std::string("mm") },
        { "length", 200.0 },
        { "faceHeight", 100.0 },
        { "entities", iCAX::Data::VariantArray{
            iCAX::Data::ObjectMap{
                { "id", std::string("seam") },
                { "kind", std::string("line") },
                { "x1", 70.0 }, { "y1", 32.0 },
                { "x2", 130.0 }, { "y2", 32.0 },
                { "closed", false }
            }
        }}
    };
    iCAX::TubeDesigner::STubeSideSketchOptions _Options;
    _Options.TrajectoryWidth = 1.0;
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape, _Sketch, _Options);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(_Result.Changed);
    EXPECT_EQ(_Result.ClosedLoopCount, 0u);
    EXPECT_EQ(_Result.OpenTrajectoryCount, 1u);
    EXPECT_GT(_Result.ToolPatchCount, 0u);
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
}

TEST(BRepTubeUnfoldingService, KeepsASeamToSeamTrajectoryAsAFullCircumferenceCut)
{
    const auto _Shape = HollowRectangularTube();
    iCAX::Data::ObjectMap _Sketch{
        { "schema", std::string("icax.tube-sketch") },
        { "schemaVersion", 1ull },
        { "kind", std::string("side") },
        { "unit", std::string("mm") },
        { "length", 200.0 },
        { "faceHeight", 100.0 },
        { "entities", iCAX::Data::VariantArray{
            iCAX::Data::ObjectMap{
                { "id", std::string("cutoff") },
                { "kind", std::string("line") },
                { "x1", 100.0 }, { "y1", 0.0 },
                { "x2", 100.0 }, { "y2", 100.0 },
                { "closed", false }
            }
        }}
    };
    iCAX::TubeDesigner::STubeSideSketchOptions _Options;
    _Options.TrajectoryWidth = 2.0;
    GProp_GProps _BeforeProperties;
    BRepGProp::VolumeProperties(_Shape, _BeforeProperties);
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape, _Sketch, _Options);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(_Result.Changed);
    EXPECT_EQ(_Result.OpenTrajectoryCount, 1u);
    EXPECT_GT(_Result.ToolPatchCount, 0u);
    ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());

    GProp_GProps _AfterProperties;
    BRepGProp::VolumeProperties(_Result.Shape, _AfterProperties);
    EXPECT_LT(_AfterProperties.Mass(), _BeforeProperties.Mass());
}

TEST(BRepTubeUnfoldingService, PreservesWideAndSeamCrossingFilledRegions)
{
    const auto _Shape = HollowRectangularTube();
    GProp_GProps _Before;
    BRepGProp::VolumeProperties(_Shape, _Before);
    for (const auto _Bounds : {std::pair{20.0, 65.0}, std::pair{-5.0, 10.0}, std::pair{95.0, 10.0}})
    {
        iCAX::Data::ObjectMap _Sketch{
            { "length", 200.0 }, { "faceHeight", 100.0 },
            { "entities", iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
                { "id", std::string("wide-window") }, { "kind", std::string("rectangle") },
                { "x", 60.0 }, { "y", _Bounds.first }, { "width", 30.0 }, { "height", _Bounds.second }
            }}}
        };
        const auto _Applied = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape, _Sketch);
        ASSERT_TRUE(_Applied.bOK) << _Applied.Diagnostic << " y=" << _Bounds.first;
        ASSERT_TRUE(BRepCheck_Analyzer(_Applied.Shape).IsValid());
        GProp_GProps _After;
        BRepGProp::VolumeProperties(_Applied.Shape, _After);
        const auto _Removed = _Before.Mass() - _After.Mass();
        const auto _Corners = _Bounds.second > 50.0 ? 3.0 : 1.0;
        // Adjacent 2 mm walls share the 2*2 corner square. Its material
        // is removed once even though the outside unfold counts both sides.
        EXPECT_NEAR(30.0 * (_Bounds.second * 2.0 - _Corners * 4.0), _Removed, 1.0)
            << "filled surface area must preserve the literal unfolded span";
    }
}

TEST(BRepTubeUnfoldingService, RoundTubeSeamWindowKeepsTheOppositeWall)
{
    const gp_Ax2 _Axis(gp_Pnt(125.0, 30.0, 40.0), gp::DX());
    const auto _Outer = BRepPrimAPI_MakeCylinder(_Axis, 15.0, 200.0).Shape();
    const auto _Inner = BRepPrimAPI_MakeCylinder(_Axis, 13.0, 200.0).Shape();
    const auto _Shape = BRepAlgoAPI_Cut(_Outer, _Inner).Shape();
    const auto _Unfold = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
    iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",_Unfold.Perimeter},
        {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
            {"id",std::string("seam-window")},{"kind",std::string("rectangle")},
            {"x",60.0},{"y",-5.0},{"width",30.0},{"height",10.0}}}}};
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape, _Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    GProp_GProps _Before, _After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_NEAR(30.0 * 10.0 * (15.0 * 15.0 - 13.0 * 13.0) / 30.0,
        _Before.Mass() - _After.Mass(), 3.0);
}

TEST(BRepTubeUnfoldingService, OpenPolylineKeepsItsWidthAtTheJoinAndSupportsANarrowKerf)
{
    const auto _Shape = HollowRectangularTube();
    GProp_GProps _Before;
    BRepGProp::VolumeProperties(_Shape,_Before);
    for (const auto _Width : {2.0, 0.01})
    {
        iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",100.0},{"trajectoryWidth",_Width},
            {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
                {"id",std::string("L-kerf")},{"kind",std::string("polyline")},{"closed",false},
                {"points",iCAX::Data::VariantArray{iCAX::Data::VariantArray{60.0,7.0},
                    iCAX::Data::VariantArray{70.0,7.0},iCAX::Data::VariantArray{70.0,17.0}}}}}}};
        const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        GProp_GProps _After; BRepGProp::VolumeProperties(_Result.Shape,_After);
        EXPECT_NEAR(20.0 * _Width * 2.0, _Before.Mass() - _After.Mass(), std::max(0.005,_Width * 0.1));
    }
}

TEST(BRepTubeUnfoldingService, FullLapEdgeInATriangleDoesNotBecomeARectangularBand)
{
    const auto _Shape = HollowRectangularTube();
    iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",100.0},
        {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
            {"id",std::string("triangle")},{"kind",std::string("polyline")},{"closed",true},
            {"points",iCAX::Data::VariantArray{iCAX::Data::VariantArray{10.0,0.0},
                iCAX::Data::VariantArray{10.0,100.0},iCAX::Data::VariantArray{50.0,50.0}}}}}}};
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    GProp_GProps _Before,_After;BRepGProp::VolumeProperties(_Shape,_Before);BRepGProp::VolumeProperties(_Result.Shape,_After);
    const auto _Removed = _Before.Mass() - _After.Mass();
    EXPECT_GT(_Removed,2500.0);EXPECT_LT(_Removed,4000.0);
}

TEST(BRepTubeUnfoldingService, EccentricAndLargeRoundWindowsLeaveNeitherInnerNorOuterSkin)
{
    for (const auto _Dimensions : {std::array<double,3>{50.0,30.0,15.0}, std::array<double,3>{400.0,398.0,0.0}})
    {
        const auto _R = _Dimensions[0], _InnerRadius = _Dimensions[1], _Offset = _Dimensions[2];
        const auto _Outer = BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(),gp::DX()),_R,2000.0).Shape();
        const auto _Inner = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,_Offset,0),gp::DX()),_InnerRadius,2000.0).Shape();
        const auto _Shape = BRepAlgoAPI_Cut(_Outer,_Inner).Shape();
        const auto _Unfold = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
        ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
        iCAX::Data::ObjectMap _Sketch{{"length",2000.0},{"faceHeight",_Unfold.Perimeter},
            {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
                {"id",std::string("through-wall")},{"kind",std::string("rectangle")},
                {"x",600.0},{"y",_Unfold.Perimeter * 0.25 - 20.0},{"width",30.0},{"height",40.0}}}}};
        const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        BRepClass3d_SolidClassifier _Classifier(_Result.Shape);
        const auto _Outside = [&](double Angle_, bool Inner_) {
            const auto _Cos = std::cos(Angle_), _Sin = std::sin(Angle_);
            const auto _InnerR = _Offset * _Cos + std::sqrt(_InnerRadius * _InnerRadius - _Offset * _Offset * _Sin * _Sin);
            const auto _Radius = Inner_ ? _InnerR + 0.01 : _R - 0.01;
            _Classifier.Perform(gp_Pnt(615.0,_Radius * _Cos,_Radius * _Sin),1.0e-6);
            return _Classifier.State() == TopAbs_OUT;
        };
        std::size_t _CutSamples = 0;
        for (int _Index = 0; _Index < 720; ++_Index)
        {
            const auto _Angle = _Index * 2.0 * std::acos(-1.0) / 720.0;
            // Exclude the two side boundaries of the sampled window.
            const bool _Exterior = _Outside(_Angle,false) && _Outside(_Angle - 0.02,false) && _Outside(_Angle + 0.02,false);
            const bool _Interior = _Outside(_Angle,true) && _Outside(_Angle - 0.02,true) && _Outside(_Angle + 0.02,true);
            if (!_Exterior && !_Interior) continue;
            ++_CutSamples;
            EXPECT_TRUE(_Outside(_Angle,false)) << "outside skin radius=" << _R;
            EXPECT_TRUE(_Outside(_Angle,true)) << "inside skin radius=" << _R;
        }
        EXPECT_GT(_CutSamples,2u);
    }
}

TEST(BRepTubeUnfoldingService, NarrowAxialKerfAtASharpSeamKeepsItsRequestedWidth)
{
    const auto _Shape = HollowRectangularTube();
    GProp_GProps _Before; BRepGProp::VolumeProperties(_Shape,_Before);
    for (const auto _U : {0.0,-0.001,0.001})
    {
        iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",100.0},{"trajectoryWidth",0.01},
            {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
                {"id",std::string("corner-kerf")},{"kind",std::string("line")},
                {"x1",60.0},{"y1",_U},{"x2",100.0},{"y2",_U}}}}};
        const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        GProp_GProps _After; BRepGProp::VolumeProperties(_Result.Shape,_After);
        EXPECT_NEAR(0.799,_Before.Mass()-_After.Mass(),0.005);
    }
}

TEST(BRepTubeUnfoldingService, RoundTubeCircleAndFractionalRectangleKeepBoundedToolCount)
{
    const gp_Ax2 _Axis(gp::Origin(), gp::DX());
    const auto _Shape = BRepAlgoAPI_Cut(
        BRepPrimAPI_MakeCylinder(_Axis, 31.0, 1000.0).Shape(),
        BRepPrimAPI_MakeCylinder(_Axis, 29.0, 1000.0).Shape()).Shape();
    const auto _Unfold = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
    iCAX::Data::ObjectMap _Sketch{{"length",1000.0},{"faceHeight",_Unfold.Perimeter},
        {"coordinateSpace",std::string("arc-length-axial")},
        {"entities",iCAX::Data::VariantArray{
            iCAX::Data::ObjectMap{{"id",std::string("window")},{"kind",std::string("rectangle")},
                {"x",18.000001192027575},{"y",595.00002906436012},
                {"width",34.999996871862827},{"height",289.99998546781995}},
            iCAX::Data::ObjectMap{{"id",std::string("round-hole")},{"kind",std::string("circle")},
                {"cx",80.000001192027575},{"cy",550.00002906436012},{"radius",23.000001192027575}}
        }}};
    const auto _Start = std::chrono::steady_clock::now();
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    const auto _Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-_Start).count();
    RecordProperty("seconds",std::to_string(_Seconds));
    RecordProperty("toolPatches",static_cast<int>(_Result.ToolPatchCount));
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_TRUE(_Result.Changed);
    EXPECT_EQ(_Result.ClosedLoopCount,2u);
    EXPECT_EQ(_Result.BooleanCutCount,1u)
        << "all complete contours must be removed together in one boolean operation";
    EXPECT_LE(_Result.ToolPatchCount,4u)
        << "each complete contour must remain a cutter, not a fan of triangles";
    EXPECT_EQ(_Result.Method,"curve-preserving-profile-surface");
    EXPECT_LT(_Seconds,30.0);
    GProp_GProps _Before, _After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    const auto _ExpectedRemoved = (35.0*290.0 + std::acos(-1.0)*23.0*23.0)*60.0/31.0;
    EXPECT_NEAR(_Before.Mass()-_After.Mass(),_ExpectedRemoved,_ExpectedRemoved*0.02);
}

TEST(BRepTubeUnfoldingService, ConcaveFilledRegionKeepsItsNotch)
{
    const auto _Shape = HollowRectangularTube();
    iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",100.0},
        {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
            {"id",std::string("notched-window")},{"kind",std::string("polyline")},{"closed",true},
            {"points",iCAX::Data::VariantArray{
                iCAX::Data::VariantArray{60.0,7.0}, iCAX::Data::VariantArray{80.0,7.0},
                iCAX::Data::VariantArray{80.0,12.0}, iCAX::Data::VariantArray{70.0,12.0},
                iCAX::Data::VariantArray{70.0,17.0}, iCAX::Data::VariantArray{60.0,17.0}}}
        }}}};
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    GProp_GProps _Before, _After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_NEAR(_Before.Mass()-_After.Mass(),300.0,0.01);
}

TEST(BRepTubeUnfoldingService, EccentricRoundTubeCurveCircleKeepsItsVariableThicknessWallCut)
{
    constexpr double _OuterRadius = 50.0, _InnerRadius = 30.0, _Offset = 15.0;
    const auto _Shape = BRepAlgoAPI_Cut(
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(),gp::DX()),_OuterRadius,200.0).Shape(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,_Offset,0),gp::DX()),_InnerRadius,200.0).Shape()).Shape();
    const auto _Unfold = iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
    iCAX::Data::ObjectMap _Sketch{{"length",200.0},{"faceHeight",_Unfold.Perimeter},
        {"entities",iCAX::Data::VariantArray{iCAX::Data::ObjectMap{
            {"id",std::string("round-window")},{"kind",std::string("circle")},
            {"cx",100.0},{"cy",_Unfold.Perimeter*0.25},{"radius",6.0}}}}};
    const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    BRepClass3d_SolidClassifier _Classifier(_Result.Shape);
    std::size_t _CutSamples = 0;
    for (int _Index = 0; _Index < 720; ++_Index)
    {
        const auto _Angle = _Index*2.0*std::acos(-1.0)/720.0;
        const auto _Outside = [&](double Angle_, bool Inner_) {
            const auto _Cos = std::cos(Angle_), _Sin = std::sin(Angle_);
            const auto _InnerR = _Offset*_Cos + std::sqrt(_InnerRadius*_InnerRadius-_Offset*_Offset*_Sin*_Sin);
            const auto _Radius = Inner_ ? _InnerR+0.01 : _OuterRadius-0.01;
            _Classifier.Perform(gp_Pnt(100.0,_Radius*_Cos,_Radius*_Sin),1.0e-6);
            return _Classifier.State()==TopAbs_OUT;
        };
        const bool _Exterior = _Outside(_Angle,false) && _Outside(_Angle-0.02,false) && _Outside(_Angle+0.02,false);
        const bool _Interior = _Outside(_Angle,true) && _Outside(_Angle-0.02,true) && _Outside(_Angle+0.02,true);
        if (!_Exterior && !_Interior) continue;
        ++_CutSamples;
        EXPECT_TRUE(_Outside(_Angle,false)) << "outside skin angle=" << _Angle;
        EXPECT_TRUE(_Outside(_Angle,true)) << "inside skin angle=" << _Angle;
    }
    EXPECT_GT(_CutSamples,2u);
    EXPECT_GT(CountNonlinearLateralPCurves(_Result.Shape),0u);
    EXPECT_LT(SketchEdgeCount(_Result.Shape),32u);
    EXPECT_EQ(_Result.BooleanCutCount,1u);
}

TEST(BRepTubeUnfoldingService, PlanarCircleAndEllipseKeepAnalyticEdgesAndRemoveTheirWholeInterior)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape = HollowRectangularTube();
    GProp_GProps _Before;
    BRepGProp::VolumeProperties(_Shape,_Before);
    const std::array<ObjectMap,3> _Entities{{
        {{"id",std::string("circle")},{"kind",std::string("circle")},
            {"cx",10.0},{"cy",80.0},{"radius",4.0}},
        {{"id",std::string("ellipse")},{"kind",std::string("ellipse")},
            {"cx",10.0},{"cy",80.0},{"radiusX",6.0},{"radiusY",4.0},{"rotation",0.25}},
        {{"id",std::string("tall-ellipse")},{"kind",std::string("ellipse")},
            {"cx",10.0},{"cy",80.0},{"radiusX",4.0},{"radiusY",6.0},{"rotation",0.25}}}};
    for (std::size_t _Index=0; _Index<_Entities.size(); ++_Index)
    {
        SCOPED_TRACE(_Index);
        const auto _Result = iCAX::TubeDesigner::ApplyTubeSideSketch(
            _Shape,CurveSketch(100.0,VariantArray{_Entities[_Index]}));
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
        EXPECT_EQ(_Result.Method,"curve-preserving-profile-surface");
        EXPECT_FALSE(_Result.Approximate);
        EXPECT_EQ(_Result.ClosedLoopCount,1u);
        EXPECT_EQ(_Result.OpenTrajectoryCount,0u);
        EXPECT_EQ(_Result.BooleanCutCount,1u);
        EXPECT_LE(_Result.ToolPatchCount,2u);
        const auto _Type = _Index == 0 ? GeomAbs_Circle : GeomAbs_Ellipse;
        EXPECT_GE(CountSketchCurveEdges(_Result.Shape,_Type),2u)
            << "outer and inner hole boundaries must retain the source conic";
        EXPECT_LT(SketchEdgeCount(_Result.Shape),32u);
        GProp_GProps _After;
        BRepGProp::VolumeProperties(_Result.Shape,_After);
        const auto _Area = std::acos(-1.0) * (_Index == 0 ? 16.0 : 24.0);
        EXPECT_NEAR(_Before.Mass()-_After.Mass(),_Area*2.0,0.002);
    }
}

TEST(BRepTubeUnfoldingService, CircleOnRoundTubeKeepsSmoothSurfaceCurvesAcrossThePeriodicSeam)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=RoundTube();
    const auto _Unfold=iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
    GProp_GProps _Before;
    BRepGProp::VolumeProperties(_Shape,_Before,1.0e-9);
    double _ReferenceVolume=0.0;
    for (const auto _Periods : {0.0,1000000.0,-5.0})
    {
        SCOPED_TRACE(_Periods);
        const ObjectMap _Circle{{"id",std::string("periodic-circle")},{"kind",std::string("circle")},
            {"cx",_Periods*_Unfold.Perimeter},{"cy",80.0},{"radius",6.0}};
        const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(
            _Shape,CurveSketch(_Unfold.Perimeter,VariantArray{_Circle}));
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
        EXPECT_FALSE(_Result.Approximate);
        EXPECT_EQ(_Result.BooleanCutCount,1u);
        EXPECT_LE(_Result.ToolPatchCount,2u);
        EXPECT_GT(CountNonlinearLateralPCurves(_Result.Shape),0u)
            << "a developed circle on a cylinder is a smooth space curve, not a 3D circle";
        EXPECT_LT(SketchEdgeCount(_Result.Shape),24u)
            << "curve resolution must not produce one BRep edge per display sample";
        GProp_GProps _After;
        BRepGProp::VolumeProperties(_Result.Shape,_After,1.0e-9);
        const auto _Removed=_Before.Mass()-_After.Mass();
        EXPECT_NEAR(_Removed,std::acos(-1.0)*36.0*(15.0*15.0-13.0*13.0)/30.0,0.01);
        if (_Periods==0.0) _ReferenceVolume=_After.Mass();
        else EXPECT_NEAR(_After.Mass(),_ReferenceVolume,0.002);
    }
}

TEST(BRepTubeUnfoldingService, OpenCircleArcKerfKeepsAnalyticOffsetsWithItsProcessWidth)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=HollowRectangularTube();
    const ObjectMap _Arc{{"id",std::string("curved-kerf")},{"kind",std::string("circleArc")},
        {"cx",10.0},{"cy",80.0},{"radius",5.0},{"startAngle",0.0},
        {"sweep",std::acos(-1.0)},{"closed",false}};
    const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape,CurveSketch(100.0,VariantArray{_Arc}));
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_EQ(_Result.OpenTrajectoryCount,1u);
    EXPECT_EQ(_Result.ClosedLoopCount,0u);
    EXPECT_EQ(_Result.BooleanCutCount,1u);
    EXPECT_GE(CountSketchCurveEdges(_Result.Shape,GeomAbs_Circle),4u);
    EXPECT_LE(_Result.ToolPatchCount,2u);
    EXPECT_LT(SketchEdgeCount(_Result.Shape),SketchEdgeCount(_Shape)+16u)
        << "one complete curved kerf adds its cap and through-wall boundaries";
    GProp_GProps _Before,_After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_NEAR(_Before.Mass()-_After.Mass(),std::acos(-1.0)*5.0*1.0*2.0,0.01);
}

TEST(BRepTubeUnfoldingService, OpenSplineKerfKeepsTheWholeCurveWithAZeroSpeedTail)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=WideHollowRectangularTube();
    const std::array<std::array<gp_Pnt,4>,3> _Samples{{
        {gp_Pnt(20,60,0),gp_Pnt(30,85,0),gp_Pnt(45,65,0),gp_Pnt(55,100,0)},
        {gp_Pnt(20,70,0),gp_Pnt(24,75,0),gp_Pnt(28,71,0),gp_Pnt(32,77,0)},
        {gp_Pnt(20,70,0),gp_Pnt(30,72,0),gp_Pnt(40,73,0),gp_Pnt(50,75,0)}}};
    // Independent fine integration of the three original quadratic chains.
    const std::array<double,3> _Lengths{67.27529608441034,16.083022676627618,30.42692570376764};
    GProp_GProps _Before;
    ASSERT_GE(BRepGProp::VolumePropertiesGK(_Shape,_Before,1.0e-9,false,true),0.0);
    for(std::size_t _Index=0;_Index<_Samples.size();++_Index)
    {
        SCOPED_TRACE(_Index);
        VariantArray _Points;
        for(const auto& _Point:_Samples[_Index]) _Points.emplace_back(VariantArray{_Point.X(),_Point.Y()});
        const ObjectMap _Spline{{"id",std::string("continuous-spline")},{"kind",std::string("spline")},
            {"closed",false},{"points",_Points}};
        auto _Sketch=CurveSketch(280.0,VariantArray{_Spline});
        _Sketch["trajectoryWidth"]=0.8;
        const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
        EXPECT_EQ(_Result.OpenTrajectoryCount,1u);
        EXPECT_EQ(_Result.BooleanCutCount,1u);
        EXPECT_LT(SketchEdgeCount(_Result.Shape),SketchEdgeCount(_Shape)+80u)
            << "a smooth spline kerf must not become a fan of sampled line edges";
        GProp_GProps _After;
        // Integrate the raw extrusion/offset surfaces by their knot spans;
        // their representation differs from a serialized/rebuilt BRep.
        ASSERT_GE(BRepGProp::VolumePropertiesGK(_Result.Shape,_After,1.0e-9,false,true),0.0);
        EXPECT_NEAR(_Before.Mass()-_After.Mass(),_Lengths[_Index]*0.8*2.0,0.03);
        EXPECT_EQ(_Sketch.at("entities").To<VariantArray>().front().To<ObjectMap>().at("points").To<VariantArray>().size(),4u);
    }
}

TEST(BRepTubeUnfoldingService, ContinuousSplitBezierParallelKeepsBothKerfSides)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    // A regular 1mm spline parallel which previously lost one complete native
    // offset side at tangent editing-span boundaries. Its last cubic is an
    // exact monotone line with repeated terminal poles; preserve saved poles.
    const auto _Path=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json(
    {"id":"split-parallel","kind":"path","closed":false,"segments":[
      {"kind":"bezier","points":[[19.071523309114742,60.3713906763541],[20.75046294353449,64.56873976240347],[22.38104705122773,68.04492569143522],[23.9775644139514,70.81222245348958]]},
      {"kind":"bezier","points":[[23.9775644139514,70.81222245348958],[25.57408177667507,73.57951921554394],[27.128911357097742,75.6418589428399],[28.72420862654403,77.0092566023653]]},
      {"kind":"bezier","points":[[28.72420862654403,77.0092566023653],[29.521857261267176,77.69295543212799],[30.33423998416582,78.204065182942],[31.176123144176405,78.51587376072371]]},
      {"kind":"bezier","points":[[31.176123144176405,78.51587376072371],[32.01800630418699,78.82768233850541],[32.8935211607783,78.93199262433919],[33.74580718425394,78.80087169765062]]},
      {"kind":"bezier","points":[[33.74580718425394,78.80087169765062],[34.59809320772958,78.66975077096205],[35.414302548059986,78.30893590945675],[36.17024222584507,77.76465934145149]]},
      {"kind":"bezier","points":[[36.17024222584507,77.76465934145149],[36.92618190363016,77.22038277344623],[37.629,76.49466666666666],[38.3,75.6]]},
      {"kind":"bezier","points":[[38.3,75.6],[39.492,74.01066666666667],[40.60282123513603,73.04035816344677],[41.50755294430118,72.6291164774626]]},
      {"kind":"bezier","points":[[41.50755294430118,72.6291164774626],[41.959918798883756,72.42349563447053],[42.35376160997525,72.3483360786478],[42.72587235058455,72.36605563772444]]},
      {"kind":"bezier","points":[[42.72587235058455,72.36605563772444],[43.09798309119385,72.38377519680108],[43.46156941538455,72.4931320281998],[43.860504244572475,72.73249292571255]]},
      {"kind":"bezier","points":[[43.860504244572475,72.73249292571255],[44.65837390294832,73.21121472073806],[45.57135845803907,74.25816420537062],[46.45996208365294,75.93663772041903]]},
      {"kind":"bezier","points":[[46.45996208365294,75.93663772041903],[47.34856570926681,77.61511123546744],[48.217669311335634,79.90189753431497],[49.03847605235918,82.77472112789738]]},
      {"kind":"bezier","points":[[49.03847605235918,82.77472112789738],[52.371809385692515,94.44138779456405],[54.03847605235918,100.27472112789738],[54.03847605235918,100.27472112789738]]}]}
    )json").To<ObjectMap>();
    const auto _Shape=WideHollowRectangularTube();
    auto _Sketch=CurveSketch(280.0,VariantArray{_Path});
    _Sketch["trajectoryWidth"]=0.8;
    const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_EQ(_Result.OpenTrajectoryCount,1u);
    EXPECT_EQ(_Result.BooleanCutCount,1u);
    EXPECT_LT(SketchEdgeCount(_Result.Shape),SketchEdgeCount(_Shape)+80u);
    GProp_GProps _Before,_After;
    ASSERT_GE(BRepGProp::VolumePropertiesGK(_Shape,_Before,1.0e-9,false,true),0.0);
    ASSERT_GE(BRepGProp::VolumePropertiesGK(_Result.Shape,_After,1.0e-9,false,true),0.0);
    EXPECT_NEAR(_Before.Mass()-_After.Mass(),67.17110793131545*0.8*2.0,0.03);
    EXPECT_EQ(_Sketch.at("entities").To<VariantArray>().front().To<ObjectMap>().at("segments").To<VariantArray>().size(),12u);
}

TEST(BRepTubeUnfoldingService, CurvedRegionIsClippedAtTheAxialEndWithoutClippingItsSourceCurve)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=HollowRectangularTube();
    const auto _Sketch=CurveSketch(100.0,VariantArray{ObjectMap{
        {"id",std::string("end-circle")},{"kind",std::string("circle")},
        {"cx",10.0},{"cy",0.0},{"radius",6.0}}});
    const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(_Shape,_Sketch);
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_GE(CountSketchCurveEdges(_Result.Shape,GeomAbs_Circle),2u);
    EXPECT_LE(_Result.ToolPatchCount,2u);
    EXPECT_EQ(_Result.BooleanCutCount,1u);
    GProp_GProps _Before,_After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_NEAR(_Before.Mass()-_After.Mass(),std::acos(-1.0)*36.0,0.005)
        << "only the half-circle inside the original blank removes material";
    const auto _SavedCircle=_Sketch.at("entities").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_DOUBLE_EQ(_SavedCircle.at("cy").To<double>(),0.0);
    EXPECT_DOUBLE_EQ(_SavedCircle.at("radius").To<double>(),6.0);
    auto _OutsideCircle=_SavedCircle;
    _OutsideCircle["cy"]=-20.0;
    const auto _Outside=iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape,CurveSketch(100.0,VariantArray{_OutsideCircle}));
    ASSERT_TRUE(_Outside.bOK) << _Outside.Diagnostic;
    EXPECT_FALSE(_Outside.Changed);
    EXPECT_EQ(_Outside.ToolPatchCount,0u);
    EXPECT_EQ(_Outside.BooleanCutCount,0u);
    GProp_GProps _Uncut;
    BRepGProp::VolumeProperties(_Outside.Shape,_Uncut);
    EXPECT_NEAR(_Uncut.Mass(),_Before.Mass(),0.001);
}

TEST(BRepTubeUnfoldingService, ClosedBezierPathAndOpenEllipseArcUseWholeCurvedCutters)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=HollowRectangularTube();
    const ObjectMap _Bezier{{"id",std::string("bezier-window")},{"kind",std::string("path")},
        {"closed",true},{"segments",VariantArray{
            ObjectMap{{"kind",std::string("bezier")},{"points",VariantArray{
                VariantArray{6.0,110.0},VariantArray{6.0,118.0},VariantArray{14.0,118.0},VariantArray{14.0,110.0}}}},
            ObjectMap{{"kind",std::string("line")},{"start",VariantArray{14.0,110.0}},
                {"end",VariantArray{6.0,110.0}}}}}};
    const ObjectMap _Arc{{"id",std::string("ellipse-kerf")},{"kind",std::string("ellipseArc")},
        {"cx",10.0},{"cy",60.0},{"radiusX",6.0},{"radiusY",4.0},{"rotation",0.0},
        {"startAngle",0.0},{"sweep",std::acos(-1.0)},{"closed",false}};
    const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape,CurveSketch(100.0,VariantArray{_Bezier,_Arc}));
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    ASSERT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_EQ(_Result.ClosedLoopCount,1u);
    EXPECT_EQ(_Result.OpenTrajectoryCount,1u);
    EXPECT_TRUE(_Result.Approximate)
        << "the ellipse offset persists as a controlled continuous spline";
    EXPECT_LE(_Result.ToolPatchCount,4u);
    EXPECT_EQ(_Result.BooleanCutCount,1u)
        << "the closed Bezier region and open kerf must share one final cut";
    EXPECT_GT(CountSketchCurveEdges(_Result.Shape,GeomAbs_BezierCurve)
        +CountSketchCurveEdges(_Result.Shape,GeomAbs_BSplineCurve)
        +CountSketchCurveEdges(_Result.Shape,GeomAbs_OffsetCurve),0u);
    EXPECT_LT(SketchEdgeCount(_Result.Shape),SketchEdgeCount(_Shape)+32u)
        << "two whole curved regions may add their actual cap and wall intersections";
    GProp_GProps _Before,_After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_GT(_Before.Mass()-_After.Mass(),80.0);
    EXPECT_LT(_Before.Mass()-_After.Mass(),130.0);
}

TEST(BRepTubeUnfoldingService, RoundedProfileCircleMappingKeepsCurvesAndWholeWallRemoval)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    for (const auto _Radius : {5.0,1.0})
    {
        SCOPED_TRACE(_Radius);
        const auto _Shape=RoundedRectangleTube(_Radius);
        ASSERT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
        const auto _Unfold=iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
        ASSERT_TRUE(_Unfold.bOK) << _Unfold.Diagnostic;
        // At the seam a circle spans the actual planar/round profile panels.
        const ObjectMap _Circle{{"id",std::string("round-corner-window")},{"kind",std::string("circle")},
            {"cx",0.0},{"cy",80.0},{"radius",6.0}};
        const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(
            _Shape,CurveSketch(_Unfold.Perimeter,VariantArray{_Circle}));
        ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
        EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
        EXPECT_LE(_Result.ToolPatchCount,12u);
        EXPECT_EQ(_Result.BooleanCutCount,1u);
        EXPECT_GT(CountNonlinearLateralPCurves(_Result.Shape),0u);
        EXPECT_LT(SketchEdgeCount(_Result.Shape),SketchEdgeCount(_Shape)+56u)
            << "allow native panel intersections while excluding sample-sized edge fans";
        GProp_GProps _Before,_After;
        BRepGProp::VolumeProperties(_Shape,_Before);
        BRepGProp::VolumeProperties(_Result.Shape,_After);
        EXPECT_GT(_Before.Mass()-_After.Mass(),100.0);
        EXPECT_LT(_Before.Mass()-_After.Mass(),250.0);
    }
}

TEST(BRepTubeUnfoldingService, EllipticalProfileUsesSmoothNativeExtrusionCurvesWithBoundedMapping)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Shape=EllipticalTube();
    ASSERT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
    const auto _Reference=iCAX::TubeDesigner::UnfoldTubeSideSketchCurveReference(_Shape);
    ASSERT_TRUE(_Reference.bOK) << _Reference.Diagnostic;
    const ObjectMap _Circle{{"id",std::string("ellipse-profile-window")},{"kind",std::string("circle")},
        {"cx",20.0},{"cy",100.0},{"radius",4.0}};
    const auto _Result=iCAX::TubeDesigner::ApplyTubeSideSketch(
        _Shape,CurveSketch(_Reference.Perimeter,VariantArray{_Circle}));
    ASSERT_TRUE(_Result.bOK) << _Result.Diagnostic;
    EXPECT_TRUE(BRepCheck_Analyzer(_Result.Shape).IsValid());
    EXPECT_TRUE(_Result.Approximate)
        << "ellipse arc-length mapping uses one smooth bounded-error curve, not line pieces";
    EXPECT_EQ(_Result.BooleanCutCount,1u);
    EXPECT_LE(_Result.ToolPatchCount,2u);
    EXPECT_GT(CountNonlinearLateralPCurves(_Result.Shape),0u);
    EXPECT_GT(CountSketchCurveEdges(_Result.Shape,GeomAbs_BSplineCurve),0u);
    EXPECT_LT(SketchEdgeCount(_Result.Shape),24u);
    GProp_GProps _Before,_After;
    BRepGProp::VolumeProperties(_Shape,_Before);
    BRepGProp::VolumeProperties(_Result.Shape,_After);
    EXPECT_GT(_Before.Mass()-_After.Mass(),80.0);
    EXPECT_LT(_Before.Mass()-_After.Mass(),160.0);
}
