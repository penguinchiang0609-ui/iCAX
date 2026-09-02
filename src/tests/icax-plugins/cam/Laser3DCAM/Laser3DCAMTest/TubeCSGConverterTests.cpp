#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h>
#include <IntentToolpath/TubeMachiningIntent.h>

#include <Data/uuid.h>
#include <Resources/ResourceAccess.h>
#include <Resources/ResourceImportExport.h>
#include <Resources/ResourceLibrary.h>
#include <Resources/ResourceLoaderRegistrationCatalog.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Resources/ResourceURL.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>

namespace
{
    TopoDS_Wire MakeRectangleWire(
        IN double dMinX_,
        IN double dMinY_,
        IN double dMaxX_,
        IN double dMaxY_,
        IN double dZ_,
        IN bool bCounterClockwise_)
    {
        BRepBuilderAPI_MakePolygon _Polygon;
        if (bCounterClockwise_)
        {
            _Polygon.Add(gp_Pnt(dMinX_, dMinY_, dZ_));
            _Polygon.Add(gp_Pnt(dMaxX_, dMinY_, dZ_));
            _Polygon.Add(gp_Pnt(dMaxX_, dMaxY_, dZ_));
            _Polygon.Add(gp_Pnt(dMinX_, dMaxY_, dZ_));
        }
        else
        {
            _Polygon.Add(gp_Pnt(dMinX_, dMinY_, dZ_));
            _Polygon.Add(gp_Pnt(dMinX_, dMaxY_, dZ_));
            _Polygon.Add(gp_Pnt(dMaxX_, dMaxY_, dZ_));
            _Polygon.Add(gp_Pnt(dMaxX_, dMinY_, dZ_));
        }
        _Polygon.Close();
        return _Polygon.Wire();
    }

    TopoDS_Shape MakeDoubleCavityExtrusion()
    {
        auto _FaceBuilder = BRepBuilderAPI_MakeFace(
            MakeRectangleWire(0.0, 0.0, 100.0, 60.0, 0.0, true));
        _FaceBuilder.Add(MakeRectangleWire(8.0, 8.0, 46.0, 52.0, 0.0, false));
        _FaceBuilder.Add(MakeRectangleWire(54.0, 8.0, 92.0, 52.0, 0.0, false));
        EXPECT_TRUE(_FaceBuilder.IsDone());
        BRepPrimAPI_MakePrism _Prism(_FaceBuilder.Face(), gp_Vec(0.0, 0.0, 200.0));
        EXPECT_TRUE(_Prism.IsDone());
        return _Prism.Shape();
    }

    TopoDS_Shape MakeRoundTube()
    {
        const auto _Outer = BRepPrimAPI_MakeCylinder(50.0, 200.0).Shape();
        const auto _Inner = BRepPrimAPI_MakeCylinder(40.0, 200.0).Shape();
        BRepAlgoAPI_Cut _Cut(_Outer, _Inner);
        EXPECT_TRUE(_Cut.IsDone());
        return _Cut.Shape();
    }

    TopoDS_Shape CutRoundTubeWithSteppedWrappedEnd()
    {
        const auto _Tube = MakeRoundTube();
        const auto _PositiveXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(0.0, 0.0, 160.0),
            60.0,
            60.0,
            40.0).Shape();
        const auto _NegativeXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(-60.0, 0.0, 140.0),
            60.0,
            60.0,
            60.0).Shape();
        BRepAlgoAPI_Fuse _WrappedRemoval(_PositiveXRemoval, _NegativeXRemoval);
        EXPECT_TRUE(_WrappedRemoval.IsDone());
        BRepAlgoAPI_Cut _Cut(_Tube, _WrappedRemoval.Shape());
        EXPECT_TRUE(_Cut.IsDone());
        return _Cut.Shape();
    }

    TopoDS_Shape ConvertAnalyticGeometryToNurbs(IN const TopoDS_Shape& Shape_)
    {
        BRepBuilderAPI_NurbsConvert _Converter(Shape_, true);
        EXPECT_TRUE(_Converter.IsDone());
        return _Converter.Shape();
    }

    bool ContainsSurfaceType(
        IN const TopoDS_Shape& Shape_,
        IN GeomAbs_SurfaceType Type_)
    {
        for (TopExp_Explorer _Explorer(Shape_, TopAbs_FACE);
            _Explorer.More();
            _Explorer.Next())
        {
            if (BRepAdaptor_Surface(TopoDS::Face(_Explorer.Current()), true).GetType()
                == Type_)
            {
                return true;
            }
        }
        return false;
    }

    TopoDS_Shape CutWithMiteredEnds(IN const TopoDS_Shape& Base_)
    {
        const auto _StartPlane = gp_Pln(gp_Pnt(0.0, 0.0, 10.0), gp_Dir(0.2, 0.0, 1.0));
        const auto _StartFace = BRepBuilderAPI_MakeFace(_StartPlane).Face();
        const auto _StartRemoval = BRepPrimAPI_MakeHalfSpace(
            _StartFace,
            gp_Pnt(0.0, 0.0, -100.0)).Solid();
        BRepAlgoAPI_Cut _StartCut(Base_, _StartRemoval);
        EXPECT_TRUE(_StartCut.IsDone());

        const auto _EndPlane = gp_Pln(gp_Pnt(0.0, 0.0, 190.0), gp_Dir(-0.2, 0.0, 1.0));
        const auto _EndFace = BRepBuilderAPI_MakeFace(_EndPlane).Face();
        const auto _EndRemoval = BRepPrimAPI_MakeHalfSpace(
            _EndFace,
            gp_Pnt(0.0, 0.0, 300.0)).Solid();
        BRepAlgoAPI_Cut _EndCut(_StartCut.Shape(), _EndRemoval);
        EXPECT_TRUE(_EndCut.IsDone());
        return _EndCut.Shape();
    }

    const iCAX::GeometryData::Tube::SExtrudedRegionNode& GetBaseNode(
        IN const iCAX::GeometryData::Tube::CTubeNeutralGeometry& Geometry_)
    {
        const auto _Iter = std::find_if(
            Geometry_.SolidNodes.begin(),
            Geometry_.SolidNodes.end(),
            [&Geometry_](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
                return Node_.ID == Geometry_.BaseNodeID;
            });
        EXPECT_NE(Geometry_.SolidNodes.end(), _Iter);
        EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(_Iter->Data));
        return std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(_Iter->Data);
    }

    double ShapeVolume(IN const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return std::abs(_Properties.Mass());
    }
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesDoubleCavityWithoutProfileBranches)
{
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(MakeDoubleCavityExtrusion());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::Exact,
        _Result.Geometry.RecognitionStatus);
    const auto& _Base = GetBaseNode(_Result.Geometry);
    EXPECT_EQ(3u, _Base.Section.Boundaries.size());
    ASSERT_EQ(3u, _Base.SectionPrimitives.size());
    EXPECT_EQ("base/section/outer", _Base.SectionPrimitives[0].ID);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ESectionPrimitiveRole::OuterBoundary,
        _Base.SectionPrimitives[0].Role);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ESectionPrimitiveKind::Rectangle,
        _Base.SectionPrimitives[0].Kind);
    for (std::size_t _Index = 1; _Index < _Base.SectionPrimitives.size(); ++_Index)
    {
        EXPECT_EQ(
            "base/section/cavity-" + std::to_string(_Index),
            _Base.SectionPrimitives[_Index].ID);
        EXPECT_EQ(
            iCAX::GeometryData::Tube::ESectionPrimitiveRole::Cavity,
            _Base.SectionPrimitives[_Index].Role);
        EXPECT_EQ(
            iCAX::GeometryData::Tube::ESectionPrimitiveKind::Rectangle,
            _Base.SectionPrimitives[_Index].Kind);
        EXPECT_TRUE(_Result.PreviewShapes.contains(
            _Base.SectionPrimitives[_Index].ID));
    }
    EXPECT_TRUE(_Result.PreviewShapes.contains("base/section/outer"));
    ASSERT_EQ(3u, _Base.SideAtlases.size());
    EXPECT_EQ("outer", _Base.SideAtlases[0].LoopID);
    EXPECT_EQ("inner-1", _Base.SideAtlases[1].LoopID);
    EXPECT_EQ("inner-2", _Base.SideAtlases[2].LoopID);
    EXPECT_GT(_Base.SideAtlases[0].UPeriod, 0.0);
    EXPECT_NEAR(200.0, _Base.SideAtlases[0].VLast, 1.0e-6);
    EXPECT_NEAR(200.0, _Base.Last - _Base.First, 1.0e-6);
    EXPECT_NEAR(1.0, std::abs(_Base.Frame.ZDirection.Z), 1.0e-6);
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, ReevaluatesMultiCavityBodyAndPrimitiveShapes)
{
    const auto _Source = MakeDoubleCavityExtrusion();
    const auto _Conversion = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Source);
    const auto _Evaluation = iCAX::OpenCascade::EvaluateTubeCSG(
        _Conversion.Geometry);

    ASSERT_TRUE(_Evaluation.bOK)
        << (_Evaluation.Diagnostics.empty() ? "" : _Evaluation.Diagnostics.front());
    EXPECT_NEAR(ShapeVolume(_Source), ShapeVolume(_Evaluation.Shape), 1.0e-4);
    EXPECT_TRUE(_Evaluation.NodeShapes.contains("base"));
    EXPECT_TRUE(_Evaluation.SectionPrimitiveShapes.contains(
        "base/section/outer"));
    EXPECT_TRUE(_Evaluation.SectionPrimitiveShapes.contains(
        "base/section/cavity-1"));
    EXPECT_TRUE(_Evaluation.SectionPrimitiveShapes.contains(
        "base/section/cavity-2"));
}

TEST(OpenCascadeTubeCSGConverterTest, ChangedCavityRebuildsFinalSolid)
{
    const auto _Source = MakeDoubleCavityExtrusion();
    auto _Geometry = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Source).Geometry;
    auto _BaseIter = std::find_if(
        _Geometry.SolidNodes.begin(),
        _Geometry.SolidNodes.end(),
        [&_Geometry](IN const auto& Node_) {
            return Node_.ID == _Geometry.BaseNodeID;
        });
    ASSERT_NE(_Geometry.SolidNodes.end(), _BaseIter);
    auto& _Base = std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
        _BaseIter->Data);
    ASSERT_GE(_Base.Section.Boundaries.size(), 2u);
    for (auto& _Curve : _Base.Section.Boundaries[1].Curves)
    {
        auto* _pSegment = std::get_if<iCAX::GeometryData::Segment2>(
            &_Curve.Segment.Curve);
        ASSERT_NE(nullptr, _pSegment);
        _pSegment->Start.X = 27.0 + 0.5 * (_pSegment->Start.X - 27.0);
        _pSegment->End.X = 27.0 + 0.5 * (_pSegment->End.X - 27.0);
    }
    _Base.SectionPrimitives[1].Width->Value *= 0.5;

    const auto _Evaluation = iCAX::OpenCascade::EvaluateTubeCSG(_Geometry);
    ASSERT_TRUE(_Evaluation.bOK)
        << (_Evaluation.Diagnostics.empty() ? "" : _Evaluation.Diagnostics.front());
    EXPECT_GT(ShapeVolume(_Evaluation.Shape), ShapeVolume(_Source));
    EXPECT_TRUE(_Evaluation.SectionPrimitiveShapes.contains(
        "base/section/cavity-1"));
}

TEST(OpenCascadeTubeCSGConverterTest, PreservesCircularInnerLoopAsTubeCavity)
{
    const auto _Shape = MakeRoundTube();
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Shape);

    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::Exact,
        _Result.Geometry.RecognitionStatus);
    const auto& _Base = GetBaseNode(_Result.Geometry);
    ASSERT_EQ(2u, _Base.Section.Boundaries.size());
    ASSERT_EQ(2u, _Base.SectionPrimitives.size());
    EXPECT_EQ("base/section/outer", _Base.SectionPrimitives[0].ID);
    EXPECT_EQ("base/section/cavity-1", _Base.SectionPrimitives[1].ID);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ESectionPrimitiveKind::Circle,
        _Base.SectionPrimitives[0].Kind);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ESectionPrimitiveKind::Circle,
        _Base.SectionPrimitives[1].Kind);
    ASSERT_TRUE(_Base.SectionPrimitives[0].Radius.has_value());
    ASSERT_TRUE(_Base.SectionPrimitives[1].Radius.has_value());
    EXPECT_NEAR(50.0, _Base.SectionPrimitives[0].Radius->Value, 1.0e-6);
    EXPECT_NEAR(40.0, _Base.SectionPrimitives[1].Radius->Value, 1.0e-6);
    EXPECT_EQ("outer", _Base.SideAtlases[0].LoopID);
    EXPECT_EQ("inner-1", _Base.SideAtlases[1].LoopID);
    EXPECT_NEAR(200.0, _Base.Last - _Base.First, 1.0e-6);
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
}

TEST(OpenCascadeTubeCSGConverterTest, PreservesZeroDepthGeometryAsProcessNeutralUVFeature)
{
    auto _Geometry = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        MakeDoubleCavityExtrusion()).Geometry;

    iCAX::GeometryData::Tube::SUVVectorGeometry _UVGeometry;
    _UVGeometry.Support.ExtrusionNodeID = _Geometry.BaseNodeID;
    _UVGeometry.Support.LoopID = "outer";
    iCAX::GeometryData::CompositeCurve2 _Path;
    iCAX::GeometryData::CurveSegment2 _Segment;
    _Segment.Curve = iCAX::GeometryData::Segment2{
        { 10.0, 40.0 },
        { 30.0, 40.0 }
    };
    _Segment.Range = { 0.0, 1.0, false };
    _Path.Segments.push_back(std::move(_Segment));
    _UVGeometry.Paths.push_back(std::move(_Path));

    iCAX::GeometryData::Tube::SUVFeature _UVFeature;
    _UVFeature.ID = "surface-curve-1";
    _UVFeature.Label = "Zero-depth surface curve";
    _UVFeature.Data = std::move(_UVGeometry);
    _Geometry.UVFeatures.push_back(std::move(_UVFeature));

    const auto _Intent = iCAX::CAM::Intent::Tube::CompileTubeMachiningIntent(_Geometry);
    const auto _UVIntent = std::find_if(
        _Intent.Features.begin(),
        _Intent.Features.end(),
        [](IN const iCAX::CAM::Intent::Tube::SMachiningFeature& Feature_) {
            return std::holds_alternative<iCAX::CAM::Intent::Tube::SUVVectorIntent>(
                Feature_.Data);
        });
    ASSERT_NE(_Intent.Features.end(), _UVIntent);
    ASSERT_EQ(1u, _UVIntent->BoundaryTraces.size());
    EXPECT_EQ(
        iCAX::CAM::Intent::Tube::EBoundaryTracePurpose::UVPath,
        _UVIntent->BoundaryTraces.front().Purpose);
    EXPECT_EQ("true", _UVIntent->Metadata.at("processUnassigned"));
    EXPECT_EQ(_UVIntent->Metadata.end(), _UVIntent->Metadata.find("machiningProcess"));
}

TEST(OpenCascadeTubeCSGConverterTest, RepresentsVariableBevelAsOpeningProfileField)
{
    iCAX::GeometryData::Tube::SOpeningProfileSweepNode _Sweep;
    _Sweep.SourceCutNodeID = "cut-1";
    _Sweep.EvaluatedVolumeNodeID = "profile-volume-1";
    _Sweep.ProfileField.ContourFirst = 0.0;
    _Sweep.ProfileField.ContourLast = 120.0;
    _Sweep.ProfileField.Periodic = true;
    _Sweep.ProfileField.Interpolation =
        iCAX::GeometryData::Tube::EOpeningProfileInterpolation::PeriodicBSpline;
    for (const auto& [_Station, _Offset] : std::vector<std::pair<double, double>>{
        { 0.0, 1.0 },
        { 40.0, 3.0 },
        { 80.0, 0.5 }
    })
    {
        iCAX::GeometryData::Tube::SOpeningProfileKey _Key;
        _Key.ContourParameter.Value = _Station;
        iCAX::GeometryData::CurveSegment2 _Segment;
        _Segment.Curve = iCAX::GeometryData::Segment2{
            { 0.0, 0.0 },
            { 6.0, _Offset }
        };
        _Segment.Range = { 0.0, 1.0, false };
        _Key.Profile.Segments.push_back(std::move(_Segment));
        _Sweep.ProfileField.Keys.push_back(std::move(_Key));
    }

    iCAX::GeometryData::Tube::SSolidNode _Node;
    _Node.ID = "variable-opening-profile";
    _Node.Data = std::move(_Sweep);
    ASSERT_TRUE(std::holds_alternative<
        iCAX::GeometryData::Tube::SOpeningProfileSweepNode>(_Node.Data));
    const auto& _Stored = std::get<
        iCAX::GeometryData::Tube::SOpeningProfileSweepNode>(_Node.Data);
    EXPECT_TRUE(_Stored.ProfileField.Periodic);
    EXPECT_EQ(3u, _Stored.ProfileField.Keys.size());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EOpeningProfileInterpolation::PeriodicBSpline,
        _Stored.ProfileField.Interpolation);
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesSteppedEndAsStableUVNormalTruncation)
{
    const auto _Shape = CutRoundTubeWithSteppedWrappedEnd();
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Shape);
    const auto _Wrapped = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<
                iCAX::GeometryData::Tube::SWrappedVolumeNode>(Node_.Data);
        });
    std::string _WrappedDiagnostic;
    for (const auto& [_Key, _Value] : _Result.Geometry.Metadata)
    {
        if (_Key.find("wrappedNormal.") == 0)
        {
            _WrappedDiagnostic += _Key + "=" + _Value + "; ";
        }
    }
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _Wrapped) << _WrappedDiagnostic;
    ASSERT_TRUE(_Wrapped->Relations);
    ASSERT_TRUE(_Wrapped->Relations->MaterialEffect);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EFeatureMaterialRole::Truncation,
        _Wrapped->Relations->MaterialEffect->Role);
    EXPECT_EQ("wrapped-normal-end-trim", _Wrapped->Metadata.at("featureSemantic"));
    const auto& _WrappedData = std::get<
        iCAX::GeometryData::Tube::SWrappedVolumeNode>(_Wrapped->Data);
    EXPECT_EQ("base", _WrappedData.Support.ExtrusionNodeID);
    EXPECT_EQ("outer", _WrappedData.Support.LoopID);
    EXPECT_FALSE(_WrappedData.UVRegion.Boundaries.empty());
    EXPECT_LT(std::get<double>(_WrappedData.LowerNormalOffset), -9.9);
    EXPECT_NEAR(0.0, std::get<double>(_WrappedData.UpperNormalOffset), 1.0e-9);
    EXPECT_GT(std::stod(_Wrapped->Metadata.at("normalRayCoverage")), 0.98);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    const auto _Reevaluation = iCAX::OpenCascade::EvaluateTubeCSG(
        _Result.Geometry);
    ASSERT_TRUE(_Reevaluation.bOK)
        << (_Reevaluation.Diagnostics.empty()
            ? ""
            : _Reevaluation.Diagnostics.front());
    EXPECT_NEAR(
        ShapeVolume(_Shape),
        ShapeVolume(_Reevaluation.Shape),
        500.0);
}

TEST(OpenCascadeTubeCSGConverterTest, RecoversVirtualSectionWhenBothEndsAreMitered)
{
    const auto _Shape = CutWithMiteredEnds(MakeDoubleCavityExtrusion());
    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.RemovalBRepResourceID = "test://miter-removal";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Shape, _Options);

    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::EquivalentButAmbiguous,
        _Result.Geometry.RecognitionStatus);
    const auto& _Base = GetBaseNode(_Result.Geometry);
    EXPECT_EQ(3u, _Base.Section.Boundaries.size());
    EXPECT_GT(_Base.Last - _Base.First, 190.0);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_EQ("base-minus-removals", _Result.Geometry.RootNodeID);
    const auto _ObliqueEndTrimCount = std::count_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Semantic = Node_.Metadata.find("featureSemantic");
            return std::holds_alternative<iCAX::GeometryData::Tube::SHalfSpaceNode>(Node_.Data)
                && _Semantic != Node_.Metadata.end()
                && _Semantic->second == "oblique-end-trim";
        });
    EXPECT_GE(_ObliqueEndTrimCount, 2);
    EXPECT_TRUE(std::any_of(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return Node_.Relations
                && Node_.Relations->MaterialEffect
                && Node_.Relations->MaterialEffect->Role
                    == iCAX::GeometryData::Tube::EFeatureMaterialRole::Truncation;
        }));
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, PromotesSingleWallHoleToExtrudedRemovalNode)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Cutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        3.0,
        30.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _Cutter);
    ASSERT_TRUE(_Cut.IsDone());

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.RemovalBRepResourceID = "test://hole-residual";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape(), _Options);

    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::EquivalentButAmbiguous,
        _Result.Geometry.RecognitionStatus);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    const auto _ExtrudedRemoval = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return _Source != Node_.Metadata.end()
                && (_Source->second == "occ.removal-extrusion"
                    || _Source->second == "occ.part-surface-group");
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _ExtrudedRemoval);
    EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
        _ExtrudedRemoval->Data));
    EXPECT_EQ("perpendicular-penetration", _ExtrudedRemoval->Metadata.at("featureSemantic"));
    ASSERT_TRUE(_ExtrudedRemoval->RemovalSemantics.has_value());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERemovalExtentKind::Through,
        _ExtrudedRemoval->RemovalSemantics->Extent);
    EXPECT_GE(_ExtrudedRemoval->RemovalSemantics->BoundaryOpeningPatchCount, 2u);
    EXPECT_EQ(0u, _ExtrudedRemoval->RemovalSemantics->InternalTerminationCount);
    ASSERT_TRUE(_ExtrudedRemoval->Relations.has_value());
    ASSERT_TRUE(_ExtrudedRemoval->Relations->MaterialEffect.has_value());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EFeatureMaterialRole::Penetration,
        _ExtrudedRemoval->Relations->MaterialEffect->Role);
    ASSERT_TRUE(_ExtrudedRemoval->Relations->Extent.has_value());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EFeatureExtentRule::ThroughSelectedHostBoundaries,
        _ExtrudedRemoval->Relations->Extent->Rule);
    EXPECT_TRUE(_ExtrudedRemoval->Relations->Extent->RecomputeAgainstHost);
    EXPECT_EQ("base", _ExtrudedRemoval->Relations->Extent->HostNodeID);
    EXPECT_GE(_ExtrudedRemoval->Relations->Extent->Boundaries.size(), 2u);
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EParameterObservability::Underconstrained,
        _ExtrudedRemoval->Relations->Extent->PrimitiveLast.Observability);
    EXPECT_EQ("base-minus-removals", _Result.Geometry.RootNodeID);
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesNurbsHoleFromCommonTangentDirection)
{
    const auto _Base = BRepPrimAPI_MakeBox(100.0, 60.0, 200.0).Shape();
    const auto _Cutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        3.0,
        80.0).Shape();
    const auto _NurbsCutter = ConvertAnalyticGeometryToNurbs(_Cutter);
    BRepAlgoAPI_Cut _Cut(_Base, _NurbsCutter);
    ASSERT_TRUE(_Cut.IsDone());
    const auto _NurbsPart = _Cut.Shape();
    ASSERT_FALSE(ContainsSurfaceType(_NurbsPart, GeomAbs_Cylinder));

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.SourceBRepResourceID = "test://nurbs-hole-part";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _NurbsPart,
        _Options);
    const auto _ExtrudedRemoval = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _AxisRecognition = Node_.Metadata.find("axisRecognition");
            return std::holds_alternative<
                    iCAX::GeometryData::Tube::SExtrudedRegionNode>(Node_.Data)
                && _AxisRecognition != Node_.Metadata.end()
                && _AxisRecognition->second == "tangent-plane-common-direction";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _ExtrudedRemoval);
    EXPECT_EQ(
        "perpendicular-penetration",
        _ExtrudedRemoval->Metadata.at("featureSemantic"));
    ASSERT_TRUE(_ExtrudedRemoval->Relations.has_value());
    EXPECT_FALSE(_ExtrudedRemoval->Relations->SurfaceEvidence.empty());
    EXPECT_TRUE(std::all_of(
        _ExtrudedRemoval->Relations->SurfaceEvidence.begin(),
        _ExtrudedRemoval->Relations->SurfaceEvidence.end(),
        [](IN const iCAX::GeometryData::Tube::SSurfaceEvidenceReference& Evidence_) {
            return Evidence_.SourceResourceID == "test://nurbs-hole-part"
                && Evidence_.Role
                    == iCAX::GeometryData::Tube::ESurfaceEvidenceRole::GeneratedLateral;
        }));
    const auto& _Extrusion = std::get<
        iCAX::GeometryData::Tube::SExtrudedRegionNode>(_ExtrudedRemoval->Data);
    ASSERT_FALSE(_Extrusion.Section.Boundaries.empty());
    EXPECT_TRUE(std::any_of(
        _Extrusion.Section.Boundaries.front().Curves.begin(),
        _Extrusion.Section.Boundaries.front().Curves.end(),
        [](IN const iCAX::GeometryData::Tube::SRegionCurve2& Curve_) {
            return std::holds_alternative<iCAX::GeometryData::NURBS2>(
                    Curve_.Segment.Curve)
                || std::holds_alternative<iCAX::GeometryData::BSpline2>(
                    Curve_.Segment.Curve);
        }));
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-5);
}

TEST(OpenCascadeTubeCSGConverterTest, ClassifiesObliquePenetrationFromAxisRelationship)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Cutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 95.0), gp_Dir(0.0, -1.0, 0.25)),
        3.0,
        32.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _Cutter);
    ASSERT_TRUE(_Cut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape());
    const auto _ObliquePenetration = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return _Source != Node_.Metadata.end()
                && (_Source->second == "occ.removal-extrusion"
                    || _Source->second == "occ.part-surface-group");
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _ObliquePenetration);
    EXPECT_EQ("oblique-penetration", _ObliquePenetration->Metadata.at("featureSemantic"));
    EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
        _ObliquePenetration->Data));
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesBlindRemovalWithoutChoosingMachiningProcess)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _BlindCutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        3.0,
        15.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _BlindCutter);
    ASSERT_TRUE(_Cut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape());
    const auto _BlindRemoval = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return Node_.RemovalSemantics
                && Node_.RemovalSemantics->Extent
                    == iCAX::GeometryData::Tube::ERemovalExtentKind::Blind;
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _BlindRemoval);
    ASSERT_TRUE(_BlindRemoval->RemovalSemantics.has_value());
    EXPECT_GE(_BlindRemoval->RemovalSemantics->BoundaryOpeningPatchCount, 1u);
    EXPECT_GE(_BlindRemoval->RemovalSemantics->InternalTerminationCount, 1u);
    EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
        _BlindRemoval->Data));
    ASSERT_TRUE(_BlindRemoval->Relations.has_value());
    ASSERT_TRUE(_BlindRemoval->Relations->Extent.has_value());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EFeatureExtentRule::BlindFromHostBoundary,
        _BlindRemoval->Relations->Extent->Rule);
    EXPECT_TRUE(_BlindRemoval->Relations->Extent->RecomputeAgainstHost);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);

    const auto _Intent = iCAX::CAM::Intent::Tube::CompileTubeMachiningIntent(
        _Result.Geometry);
    const auto _BlindIntent = std::find_if(
        _Intent.Features.begin(),
        _Intent.Features.end(),
        [](IN const iCAX::CAM::Intent::Tube::SMachiningFeature& Feature_) {
            return Feature_.RemovalSemantics
                && Feature_.RemovalSemantics->Extent
                    == iCAX::GeometryData::Tube::ERemovalExtentKind::Blind;
        });
    ASSERT_NE(_Intent.Features.end(), _BlindIntent);
    EXPECT_EQ(
        _BlindIntent->Metadata.end(),
        _BlindIntent->Metadata.find("machiningProcess"));
}

TEST(OpenCascadeTubeCSGConverterTest, RepresentsOpenedChamberAndInternalWallMachiningWithoutOrder)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _OuterWallOpening = BRepPrimAPI_MakeBox(
        gp_Pnt(10.0, 50.0, 40.0),
        34.0,
        20.0,
        120.0).Shape();
    BRepAlgoAPI_Cut _OpenChamber(_Base, _OuterWallOpening);
    ASSERT_TRUE(_OpenChamber.IsDone());

    const auto _InternalWallDrill = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(40.0, 30.0, 100.0), gp_Dir(1.0, 0.0, 0.0)),
        3.0,
        20.0).Shape();
    BRepAlgoAPI_Cut _DrillInternalWall(_OpenChamber.Shape(), _InternalWallDrill);
    ASSERT_TRUE(_DrillInternalWall.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_DrillInternalWall.Shape());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::EquivalentButAmbiguous,
        _Result.Geometry.RecognitionStatus);
    EXPECT_EQ(3u, GetBaseNode(_Result.Geometry).Section.Boundaries.size());
    const auto _RecognizedRemovalCount = std::count_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return _Source != Node_.Metadata.end()
                && (_Source->second == "occ.removal-extrusion"
                    || _Source->second == "occ.part-surface-group");
        });
    EXPECT_GE(_RecognizedRemovalCount, 2);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_EQ("base-minus-removals", _Result.Geometry.RootNodeID);
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, PreservesEveryEquivalentExtrusionAxisAsOptionalCandidate)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Cutter = BRepPrimAPI_MakeBox(
        gp_Pnt(18.0, 52.0, 84.0),
        10.0,
        8.0,
        12.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _Cutter);
    ASSERT_TRUE(_Cut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape());
    const auto _AlternativeIter = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return std::holds_alternative<iCAX::GeometryData::Tube::SAlternativeNode>(Node_.Data)
                && _Source != Node_.Metadata.end()
                && _Source->second == "occ.enumerated-hypotheses";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _AlternativeIter);
    const auto& _Alternative = std::get<iCAX::GeometryData::Tube::SAlternativeNode>(
        _AlternativeIter->Data);
    EXPECT_GE(_Alternative.Candidates.size(), 2u);
    EXPECT_TRUE(_Alternative.SelectedCandidateID.empty());
    EXPECT_FALSE(_Alternative.RecommendedCandidateID.empty());
    for (const auto& _Candidate : _Alternative.Candidates)
    {
        EXPECT_TRUE(_Candidate.Optional);
        EXPECT_LT(_Candidate.RelativeGeometryError, 1.0e-4);
        EXPECT_TRUE(_Candidate.Evaluation.GeometryAccepted);
        EXPECT_GT(_Candidate.Evaluation.RecommendationScore, 0.0);
        EXPECT_FALSE(_Candidate.CandidateNodeID.empty());
        EXPECT_FALSE(_Candidate.Evidence.empty());
    }
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, PreservesTrueConeAndBevelAsOptionalInterpretations)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _ConicalCutter = BRepPrimAPI_MakeCone(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        5.0,
        2.0,
        30.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _ConicalCutter);
    ASSERT_TRUE(_Cut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::EquivalentButAmbiguous,
        _Result.Geometry.RecognitionStatus);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());

    const auto _AlternativeIter = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SAlternativeNode>(Node_.Data);
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _AlternativeIter);
    const auto& _Alternative = std::get<iCAX::GeometryData::Tube::SAlternativeNode>(
        _AlternativeIter->Data);
    ASSERT_EQ(2u, _Alternative.Candidates.size());
    EXPECT_TRUE(_Alternative.Candidates[0].Optional);
    EXPECT_TRUE(_Alternative.Candidates[1].Optional);
    EXPECT_EQ("true-tapered-penetration", _Alternative.RecommendedCandidateID);
    EXPECT_TRUE(_Alternative.SelectedCandidateID.empty());
    EXPECT_TRUE(_Result.PreviewShapes.contains(_AlternativeIter->ID));
    for (const auto& _Candidate : _Alternative.Candidates)
    {
        EXPECT_TRUE(_Result.PreviewShapes.contains(_Candidate.CandidateNodeID));
    }

    const auto _Intent = iCAX::CAM::Intent::Tube::CompileTubeMachiningIntent(
        _Result.Geometry);
    EXPECT_EQ(
        iCAX::CAM::Intent::Tube::ECompilationStatus::RequiresInterpretationSelection,
        _Intent.Status);
    const auto _AlternativeIntent = std::find_if(
        _Intent.Features.begin(),
        _Intent.Features.end(),
        [](IN const iCAX::CAM::Intent::Tube::SMachiningFeature& Feature_) {
            return std::holds_alternative<iCAX::CAM::Intent::Tube::SAlternativeCutIntent>(
                Feature_.Data);
        });
    ASSERT_NE(_Intent.Features.end(), _AlternativeIntent);
    const auto& _AlternativeCut = std::get<iCAX::CAM::Intent::Tube::SAlternativeCutIntent>(
        _AlternativeIntent->Data);
    EXPECT_EQ(2u, _AlternativeCut.CandidateIDs.size());
    EXPECT_TRUE(_AlternativeCut.SelectedCandidateID.empty());

    auto _SelectedGeometry = _Result.Geometry;
    auto _SelectedAlternativeIter = std::find_if(
        _SelectedGeometry.SolidNodes.begin(),
        _SelectedGeometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SAlternativeNode>(Node_.Data);
        });
    ASSERT_NE(_SelectedGeometry.SolidNodes.end(), _SelectedAlternativeIter);
    auto& _SelectedAlternative = std::get<iCAX::GeometryData::Tube::SAlternativeNode>(
        _SelectedAlternativeIter->Data);
    _SelectedAlternative.SelectedCandidateID = "straight-penetration-with-bevel";
    _SelectedAlternative.SelectionSource =
        iCAX::GeometryData::Tube::EAlternativeSelectionSource::User;

    const auto _SelectedIntent = iCAX::CAM::Intent::Tube::CompileTubeMachiningIntent(
        _SelectedGeometry);
    EXPECT_EQ(
        iCAX::CAM::Intent::Tube::ECompilationStatus::RequiresGeometricIntersection,
        _SelectedIntent.Status);
    const auto _BeveledCut = std::find_if(
        _SelectedIntent.Features.begin(),
        _SelectedIntent.Features.end(),
        [](IN const iCAX::CAM::Intent::Tube::SMachiningFeature& Feature_) {
            return std::holds_alternative<iCAX::CAM::Intent::Tube::SExtrudedCutIntent>(
                Feature_.Data)
                && !Feature_.OpeningProfileTargets.empty();
        });
    EXPECT_NE(_SelectedIntent.Features.end(), _BeveledCut);

    EXPECT_TRUE(std::any_of(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::STaperedRegionNode>(Node_.Data);
        }));
    EXPECT_TRUE(std::any_of(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<
                iCAX::GeometryData::Tube::SOpeningProfileSweepNode>(Node_.Data);
        }));
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesNurbsConeFromCommonTangentApex)
{
    const auto _Base = BRepPrimAPI_MakeBox(100.0, 60.0, 200.0).Shape();
    const auto _ConicalCutter = BRepPrimAPI_MakeCone(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        5.0,
        2.0,
        80.0).Shape();
    const auto _NurbsCutter = ConvertAnalyticGeometryToNurbs(_ConicalCutter);
    BRepAlgoAPI_Cut _Cut(_Base, _NurbsCutter);
    ASSERT_TRUE(_Cut.IsDone());
    const auto _NurbsPart = _Cut.Shape();
    ASSERT_FALSE(ContainsSurfaceType(_NurbsPart, GeomAbs_Cone));

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_NurbsPart);
    const auto _Tapered = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return std::holds_alternative<
                    iCAX::GeometryData::Tube::STaperedRegionNode>(Node_.Data)
                && _Source != Node_.Metadata.end()
                && _Source->second == "brep.tangent-plane-common-apex";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _Tapered);
    EXPECT_GT(
        std::stod(_Tapered->Metadata.at("tangentPlaneEvidenceCoverage")),
        0.5);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-7);
}

TEST(OpenCascadeTubeCSGConverterTest, RecognizesCylinderWithLocalConeAsExplicitBevel)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Axis = gp_Ax2(
        gp_Pnt(25.0, 70.0, 100.0),
        gp_Dir(0.0, -1.0, 0.0));
    const auto _NominalCutter = BRepPrimAPI_MakeCylinder(_Axis, 2.0, 30.0).Shape();
    const auto _BevelEnvelope = BRepPrimAPI_MakeCone(_Axis, 5.0, 2.0, 14.0).Shape();
    BRepAlgoAPI_Fuse _Cutter(_NominalCutter, _BevelEnvelope);
    ASSERT_TRUE(_Cutter.IsDone());
    BRepAlgoAPI_Cut _Cut(_Base, _Cutter.Shape());
    ASSERT_TRUE(_Cut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::ERecognitionStatus::EquivalentButAmbiguous,
        _Result.Geometry.RecognitionStatus);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_FALSE(std::any_of(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SAlternativeNode>(Node_.Data);
        }));
    const auto _BevelIter = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<
                iCAX::GeometryData::Tube::SOpeningProfileSweepNode>(Node_.Data);
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _BevelIter);
    const auto& _Bevel = std::get<iCAX::GeometryData::Tube::SOpeningProfileSweepNode>(
        _BevelIter->Data);
    EXPECT_FALSE(_Bevel.SourceCutNodeID.empty());
    EXPECT_FALSE(_Bevel.EvaluatedVolumeNodeID.empty());
    EXPECT_TRUE(_Bevel.TargetOpening.has_value());
    ASSERT_EQ(1u, _Bevel.ProfileField.Keys.size());
    ASSERT_EQ(1u, _Bevel.ProfileField.Keys.front().Profile.Segments.size());
    EXPECT_EQ(
        "source-direction,signed-removal-offset",
        _Bevel.ProfileField.Metadata.at("profileCoordinates"));
    EXPECT_GT(
        std::stod(_Bevel.ProfileField.Metadata.at("derivedSemiAngleRadians")),
        0.0);
    EXPECT_GT(std::stod(_Bevel.ProfileField.Metadata.at("derivedLand")), 0.0);
    EXPECT_TRUE(_Result.PreviewShapes.contains(_Bevel.SourceCutNodeID));
    EXPECT_TRUE(_Result.PreviewShapes.contains(_Bevel.EvaluatedVolumeNodeID));
    EXPECT_TRUE(_Result.PreviewShapes.contains(_BevelIter->ID));
    const auto _ExplicitRoot = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return _Source != Node_.Metadata.end()
                && _Source->second == "occ.explicit-bevel";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _ExplicitRoot);
    EXPECT_TRUE(_Result.PreviewShapes.contains(_ExplicitRoot->ID));
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
    const auto _Reevaluation = iCAX::OpenCascade::EvaluateTubeCSG(
        _Result.Geometry);
    ASSERT_TRUE(_Reevaluation.bOK)
        << (_Reevaluation.Diagnostics.empty()
            ? ""
            : _Reevaluation.Diagnostics.front());
    EXPECT_NEAR(
        ShapeVolume(_Cut.Shape()),
        ShapeVolume(_Reevaluation.Shape),
        1.0e-3);
}

TEST(OpenCascadeTubeCSGConverterTest, DecomposesBlindHoleClippedByObliqueEndTrim)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _BlindCutter = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 190.0), gp_Dir(0.0, -1.0, 0.0)),
        6.0,
        15.0).Shape();
    BRepAlgoAPI_Cut _BlindCut(_Base, _BlindCutter);
    ASSERT_TRUE(_BlindCut.IsDone());

    const auto _TrimPlane = gp_Pln(
        gp_Pnt(25.0, 0.0, 190.0),
        gp_Dir(0.25, 0.0, 1.0));
    const auto _TrimFace = BRepBuilderAPI_MakeFace(_TrimPlane).Face();
    const auto _TrimVolume = BRepPrimAPI_MakeHalfSpace(
        _TrimFace,
        gp_Pnt(25.0, 0.0, 300.0)).Solid();
    BRepAlgoAPI_Cut _FinalCut(_BlindCut.Shape(), _TrimVolume);
    ASSERT_TRUE(_FinalCut.IsDone());

    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_FinalCut.Shape());
    const auto _SubsetHalfSpaces = std::count_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Mode = Node_.Metadata.find("recognitionMode");
            return std::holds_alternative<iCAX::GeometryData::Tube::SHalfSpaceNode>(Node_.Data)
                && _Mode != Node_.Metadata.end()
                && _Mode->second == "global-subset";
        });
    const auto _SurfaceGroupedExtrusions = std::count_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(Node_.Data)
                && _Source != Node_.Metadata.end()
                && _Source->second == "occ.part-surface-group";
        });
    EXPECT_GE(_SubsetHalfSpaces, 1);
    EXPECT_GE(_SurfaceGroupedExtrusions, 1);
    EXPECT_FALSE(std::any_of(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SCompositeVolumeNode>(
                Node_.Data);
        }));
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, GroupsDisconnectedWallCutsFromPartSurfaceEvidence)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Branch = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(25.0, 70.0, 100.0), gp_Dir(0.0, -1.0, 0.0)),
        5.0,
        80.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _Branch);
    ASSERT_TRUE(_Cut.IsDone());

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.SourceBRepResourceID = "test://hollow-tube-part";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(
        _Cut.Shape(),
        _Options);
    const auto _GroupedFeature = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return std::holds_alternative<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
                    Node_.Data)
                && _Source != Node_.Metadata.end()
                && _Source->second == "occ.part-surface-group";
        });
    std::ostringstream _RecognitionDebug;
    for (const auto& [_Key, _Value] : _Result.Geometry.Metadata)
    {
        if (_Key.starts_with("partSurfaceGraph.") || _Key == "partSurfaceGroupCount")
        {
            _RecognitionDebug << _Key << '=' << _Value << ';';
        }
    }
    for (const auto& _Node : _Result.Geometry.SolidNodes)
    {
        const auto _Source = _Node.Metadata.find("source");
        if (_Source != _Node.Metadata.end())
        {
            _RecognitionDebug << _Node.ID << ':' << _Source->second << ';';
        }
    }
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _GroupedFeature)
        << _RecognitionDebug.str();
    EXPECT_EQ("2", _GroupedFeature->Metadata.at("coveredRemovalComponentCount"));
    ASSERT_TRUE(_GroupedFeature->Relations.has_value());
    EXPECT_EQ(2u, _GroupedFeature->Relations->MaterialSpans.size());
    for (const auto& _Span : _GroupedFeature->Relations->MaterialSpans)
    {
        EXPECT_EQ(
            iCAX::GeometryData::Tube::EMaterialSpanEndpointKind::HostBoundary,
            _Span.Start.Kind);
        EXPECT_EQ(
            iCAX::GeometryData::Tube::EMaterialSpanEndpointKind::HostBoundary,
            _Span.End.Kind);
    }
    EXPECT_GE(_GroupedFeature->Relations->SurfaceEvidence.size(), 2u);
    for (const auto& _Evidence : _GroupedFeature->Relations->SurfaceEvidence)
    {
        EXPECT_EQ("test://hollow-tube-part", _Evidence.SourceResourceID);
    }
    ASSERT_TRUE(_GroupedFeature->Relations->Extent.has_value());
    EXPECT_EQ(
        iCAX::GeometryData::Tube::EFeatureExtentRule::ThroughSelectedHostBoundaries,
        _GroupedFeature->Relations->Extent->Rule);

    const auto _GroupingAlternative = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            const auto _Source = Node_.Metadata.find("source");
            return std::holds_alternative<iCAX::GeometryData::Tube::SAlternativeNode>(
                    Node_.Data)
                && _Source != Node_.Metadata.end()
                && _Source->second == "occ.part-surface-group-alternative";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _GroupingAlternative);
    const auto& _Alternative = std::get<iCAX::GeometryData::Tube::SAlternativeNode>(
        _GroupingAlternative->Data);
    ASSERT_EQ(2u, _Alternative.Candidates.size());
    EXPECT_EQ("single-cross-material-generator", _Alternative.RecommendedCandidateID);
    EXPECT_TRUE(_Alternative.SelectedCandidateID.empty());
    const auto _GeneratorPreview = _Result.PreviewShapes.find(_GroupedFeature->ID);
    ASSERT_NE(_Result.PreviewShapes.end(), _GeneratorPreview);
    EXPECT_FALSE(_GeneratorPreview->second.IsNull());
    EXPECT_NE(
        _Result.PreviewShapes.end(),
        _Result.PreviewShapes.find(_GroupingAlternative->ID));
    GProp_GProps _PreviewProperties;
    GProp_GProps _BaseProperties;
    GProp_GProps _PartProperties;
    BRepGProp::VolumeProperties(_GeneratorPreview->second, _PreviewProperties);
    BRepGProp::VolumeProperties(_Base, _BaseProperties);
    BRepGProp::VolumeProperties(_Cut.Shape(), _PartProperties);
    EXPECT_GT(
        std::abs(_PreviewProperties.Mass()),
        std::abs(_BaseProperties.Mass() - _PartProperties.Mass()));
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, UsesEditableCompositeBeforeOpaqueResidual)
{
    const auto _Base = MakeDoubleCavityExtrusion();
    const auto _Bubble = BRepPrimAPI_MakeSphere(gp_Pnt(4.0, 30.0, 100.0), 2.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Base, _Bubble);
    ASSERT_TRUE(_Cut.IsDone());

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.RemovalBRepResourceID = "test://composite-removal";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Cut.Shape(), _Options);
    const auto _CompositeIter = std::find_if(
        _Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(),
        [](IN const iCAX::GeometryData::Tube::SSolidNode& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SCompositeVolumeNode>(
                Node_.Data);
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _CompositeIter);
    const auto& _Composite = std::get<iCAX::GeometryData::Tube::SCompositeVolumeNode>(
        _CompositeIter->Data);
    EXPECT_EQ("test://composite-removal", _Composite.BRepResourceID);
    EXPECT_FALSE(_Composite.AnalyticPatches.empty());
    EXPECT_TRUE(std::any_of(
        _Composite.AnalyticPatches.begin(),
        _Composite.AnalyticPatches.end(),
        [](IN const auto& Patch_) {
            return Patch_.Kind
                == iCAX::GeometryData::Tube::EAnalyticSurfaceKind::Sphere;
        }));
    EXPECT_GT(_Result.Geometry.RelativeUnparameterizedVolume, 0.0);
    EXPECT_LT(_Result.Geometry.RelativeOpaqueResidualVolume, 1.0e-12);

    const auto _Intent = iCAX::CAM::Intent::Tube::CompileTubeMachiningIntent(
        _Result.Geometry);
    EXPECT_TRUE(std::any_of(
        _Intent.Features.begin(),
        _Intent.Features.end(),
        [](IN const iCAX::CAM::Intent::Tube::SMachiningFeature& Feature_) {
            const auto _CompositeIntent = std::get_if<
                iCAX::CAM::Intent::Tube::SCompositeCutIntent>(&Feature_.Data);
            return _CompositeIntent
                && _CompositeIntent->SupportsManualFeaturePromotion
                && _CompositeIntent->AnalyticPatchCount > 0;
        }));
}

TEST(OpenCascadeTubeCSGConverterTest, PreviewMeshIsReadDirectlyFromResourceUrl)
{
    const auto _TemporaryPath = std::filesystem::temp_directory_path()
        / ("tube-one-preview-" +
            iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) +
            ".step");
    struct STemporaryFile final
    {
        std::filesystem::path Path;
        ~STemporaryFile()
        {
            std::error_code _Error;
            std::filesystem::remove(Path, _Error);
        }
    } _TemporaryFile{ _TemporaryPath };

    STEPControl_Writer _Writer;
    ASSERT_EQ(IFSelect_RetDone, _Writer.Transfer(MakeRoundTube(), STEPControl_AsIs));
    ASSERT_EQ(IFSelect_RetDone, _Writer.Write(_TemporaryPath.string().c_str()));

    auto _pRegistry = std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
    iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayAll(*_pRegistry);
    iCAX::Resource::CResourceLibrary _Resources(_pRegistry);
    _Resources.SetScope(iCAX::Resource::MakeSceneResourceScope(
        "icax-test",
        "tube-one-test",
        iCAX::Data::GenerateNewUUID(),
        iCAX::Data::GenerateNewUUID()));

    iCAX::Resource::CResourceImportRequest _ImportRequest;
    _ImportRequest.SourcePath = _TemporaryPath.string();
    _ImportRequest.TargetResourceID = _Resources.AllocateResourceURL();
    _ImportRequest.FormatID = "cad.step";
    _ImportRequest.Persistence =
        iCAX::Resource::EResourcePersistenceMode::Embedded;
    const auto _ImportResult = _Resources.Import(_ImportRequest);
    ASSERT_TRUE(_ImportResult.IsOK()) << _ImportResult.Error;

    const auto _Preview = std::find_if(
        _ImportResult.Items.begin(),
        _ImportResult.Items.end(),
        [](IN const iCAX::Resource::CResourceImportItem& Item_) {
            return Item_.Role == "tube.csg.preview";
        });
    ASSERT_NE(_ImportResult.Items.end(), _Preview);
    EXPECT_TRUE(iCAX::Resource::TryParseResourceURL(_Preview->ResourceID).has_value());
    EXPECT_EQ("resource.flatbuffer", _Preview->Info.ResourceTypeID);
    EXPECT_EQ("T1PM", _Preview->Info.FlatBufferIdentifier);
    EXPECT_EQ(1u, _Preview->Info.nSchemaVersion);
    const auto _TubeNeutral = std::find_if(
        _ImportResult.Items.begin(),
        _ImportResult.Items.end(),
        [](IN const iCAX::Resource::CResourceImportItem& Item_) {
            return Item_.Role == "tube.neutral_geometry";
        });
    ASSERT_NE(_ImportResult.Items.end(), _TubeNeutral);
    EXPECT_NE(
        _TubeNeutral->Info.Dependencies.end(),
        std::find(
            _TubeNeutral->Info.Dependencies.begin(),
            _TubeNeutral->Info.Dependencies.end(),
            iCAX::Resource::CResourceReference{
                _Preview->ResourceID,
                _Preview->Info.nVersion }));

    iCAX::Resource::CResourceRequest _Request;
    _Request.Method = iCAX::Resource::EResourceMethod::Get;
    _Request.URL = _Preview->ResourceID;
    _Request.Headers["Accept"] = "application/vnd.icax.flatbuffer";
    _Request.Headers["ICAX-Resource-Version"] =
        std::to_string(_Preview->Info.nVersion);
    iCAX::Resource::CResourceAccessService _Access(_Resources);
    const auto _Response = _Access.Request(_Request);
    ASSERT_EQ(200u, _Response.nStatus);
    ASSERT_TRUE(_Response.HasBody());
    ASSERT_GE(_Response.Body.Size(), 8u);
    EXPECT_EQ('T', _Response.Body.Data()[4]);
    EXPECT_EQ('1', _Response.Body.Data()[5]);
    EXPECT_EQ('P', _Response.Body.Data()[6]);
    EXPECT_EQ('M', _Response.Body.Data()[7]);
    const auto _ReadUInt32 = [&_Response](IN const std::size_t nOffset_) {
        uint32_t _Value = 0;
        std::memcpy(
            &_Value,
            _Response.Body.Data() + nOffset_,
            sizeof(_Value));
        return _Value;
    };
    const auto _ReadInt32 = [&_Response](IN const std::size_t nOffset_) {
        int32_t _Value = 0;
        std::memcpy(
            &_Value,
            _Response.Body.Data() + nOffset_,
            sizeof(_Value));
        return _Value;
    };
    const auto _ReadUInt16 = [&_Response](IN const std::size_t nOffset_) {
        uint16_t _Value = 0;
        std::memcpy(
            &_Value,
            _Response.Body.Data() + nOffset_,
            sizeof(_Value));
        return _Value;
    };
    const auto _Root = static_cast<std::size_t>(_ReadUInt32(0));
    ASSERT_LT(_Root + 4, _Response.Body.Size());
    const auto _VTable = static_cast<std::size_t>(
        static_cast<std::ptrdiff_t>(_Root) - _ReadInt32(_Root));
    ASSERT_LT(_VTable + 10, _Response.Body.Size());
    const auto _PositionField = static_cast<std::size_t>(
        _ReadUInt16(_VTable + 6));
    const auto _IndexField = static_cast<std::size_t>(
        _ReadUInt16(_VTable + 8));
    ASSERT_NE(0u, _PositionField);
    ASSERT_NE(0u, _IndexField);
    const auto _PositionReference = _Root + _PositionField;
    const auto _IndexReference = _Root + _IndexField;
    const auto _PositionVector = _PositionReference
        + _ReadUInt32(_PositionReference);
    const auto _IndexVector = _IndexReference
        + _ReadUInt32(_IndexReference);
    EXPECT_GE(_ReadUInt32(_PositionVector), 9u);
    EXPECT_EQ(0u, _ReadUInt32(_PositionVector) % 3u);
    EXPECT_GE(_ReadUInt32(_IndexVector), 3u);
    EXPECT_EQ(0u, _ReadUInt32(_IndexVector) % 3u);
    EXPECT_EQ(
        "T1PM",
        iCAX::Resource::GetResourceHeader(
            _Response.Headers,
            "ICAX-FlatBuffer-Identifier").value_or(""));
}
