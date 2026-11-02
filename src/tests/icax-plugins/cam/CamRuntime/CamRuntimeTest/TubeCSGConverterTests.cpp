#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h>
#include <OpenCascadeResourceImport/OpenCascadeCancellation.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <Task/TaskStatus.h>
#include <BRepAlgoAPI_Common.hxx>
#include <BOPAlgo_PaveFiller.hxx>
#include <NCollection_List.hxx>
#include <atomic>
#include <GeometryData/TubeNeutralPersistence.h>
#include <TubeIntentAdapter/TubeMachiningIntent.h>

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
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
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
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

namespace
{
#if defined(_WIN32) && defined(_DEBUG)
    // An unattended kernel assertion must fail the test process, not open a
    // desktop dialog or continue with an invalid geometric result.
    int __cdecl FailOnKernelAssertion(int type, char* message, int*)
    {
        if (type != _CRT_ASSERT) return FALSE;
        std::fprintf(stderr, "%s\n", message);
        const auto process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
        void* frames[64]{};
        const auto count = CaptureStackBackTrace(0, 64, frames, nullptr);
        for (USHORT i = 0; i < count; ++i)
        {
            const auto address = reinterpret_cast<DWORD64>(frames[i]);
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 offset = 0;
            if (symbols && SymFromAddr(process, address, &offset, symbol))
                std::fprintf(stderr, "  %s + 0x%llx", symbol->Name, offset);
            else
                std::fprintf(stderr, "  0x%llx", address);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD displacement = 0;
            if (symbols && SymGetLineFromAddr64(process, address, &displacement, &line))
                std::fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
            std::fputc('\n', stderr);
        }
        std::fflush(stderr);
        std::_Exit(3);
    }

    const bool KernelAssertionHandlerInstalled = [] {
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, FailOnKernelAssertion);
        return true;
    }();
#endif

    TEST(OpenCascadeShutdown, CancelsInsideBooleanAndRestoresCallerScope)
    {
        using iCAX::OpenCascade::COpenCascadeCancellationScope;
        const auto _Left = BRepPrimAPI_MakeBox(100, 100, 100).Shape();
        const auto _Right = BRepPrimAPI_MakeBox(gp_Pnt(50, 50, 50), 100, 100, 100).Shape();
        BRepAlgoAPI_Common _Common;
        NCollection_List<TopoDS_Shape> _Arguments, _Tools;
        _Arguments.Append(_Left); _Tools.Append(_Right);
        _Common.SetArguments(_Arguments); _Common.SetTools(_Tools);
        std::atomic_uint _Checks = 0;
        {
            // Build's only own checks are before and after OCC. Reaching three
            // calls proves UserBreak was queried inside the boolean algorithm.
            COpenCascadeCancellationScope _Cancellation([&] { return ++_Checks >= 3; });
            EXPECT_THROW(COpenCascadeCancellationScope::Build(_Common), iCAX::Tasks::TaskCanceledException);
            EXPECT_GE(_Checks.load(), 3u);
            EXPECT_FALSE(_Common.IsDone());
        }
        EXPECT_FALSE(static_cast<bool>(COpenCascadeCancellationScope::Capture()));
        EXPECT_NO_THROW(COpenCascadeCancellationScope::Build(_Common));
        EXPECT_TRUE(_Common.IsDone());
        EXPECT_TRUE(BRepCheck_Analyzer(_Common.Shape()).IsValid());
    }

    TEST(OpenCascadeShutdown, ConversionHonorsCancellationBeforeProducingResult)
    {
        iCAX::OpenCascade::COpenCascadeCancellationScope _Cancellation([] { return true; });
        iCAX::OpenCascade::STubeCSGConversionOptions _Options;
        EXPECT_THROW(iCAX::OpenCascade::ConvertBRepToTubeCSG({}, _Options),
            iCAX::Tasks::TaskCanceledException);
        iCAX::OpenCascade::STubeCSGEvaluationOptions _ReplayOptions;
        EXPECT_THROW(iCAX::OpenCascade::EvaluateTubeCSG({}, _ReplayOptions),
            iCAX::Tasks::TaskCanceledException);
        EXPECT_THROW(iCAX::OpenCascade::ValidateTubeCSGReplay({}, {}, _ReplayOptions),
            iCAX::Tasks::TaskCanceledException);
    }

    TEST(OpenCascadeShutdown, CancelsInsideSharedIntersectionAndRestoresCallerScope)
    {
        using iCAX::OpenCascade::COpenCascadeCancellationScope;
        BOPAlgo_PaveFiller _Intersections;
        NCollection_List<TopoDS_Shape> _Arguments;
        _Arguments.Append(BRepPrimAPI_MakeBox(100, 100, 100).Shape());
        _Arguments.Append(BRepPrimAPI_MakeBox(gp_Pnt(50, 50, 50), 100, 100, 100).Shape());
        _Intersections.SetArguments(_Arguments); _Intersections.SetNonDestructive(true);
        std::atomic_uint _Checks = 0;
        {
            COpenCascadeCancellationScope _Cancellation([&] { return ++_Checks >= 3; });
            EXPECT_THROW(COpenCascadeCancellationScope::Perform(_Intersections), iCAX::Tasks::TaskCanceledException);
            EXPECT_GE(_Checks.load(), 3u);
        }
        EXPECT_FALSE(static_cast<bool>(COpenCascadeCancellationScope::Capture()));
        EXPECT_NO_THROW(COpenCascadeCancellationScope::Perform(_Intersections));
        EXPECT_FALSE(_Intersections.HasErrors());
    }

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

    TopoDS_Shape MakeShortXAngleBlank()
    {
        BRepBuilderAPI_MakePolygon _Polygon;
        for (const auto& _Point : {
            gp_Pnt(0.0, 0.0, 0.0), gp_Pnt(0.0, 50.0, 0.0),
            gp_Pnt(0.0, 50.0, 5.0), gp_Pnt(0.0, 5.0, 5.0),
            gp_Pnt(0.0, 5.0, 40.0), gp_Pnt(0.0, 0.0, 40.0) })
        {
            _Polygon.Add(_Point);
        }
        _Polygon.Close();
        return BRepPrimAPI_MakePrism(
            BRepBuilderAPI_MakeFace(_Polygon.Wire()).Face(),
            gp_Vec(12.0, 0.0, 0.0)).Shape();
    }

    TopoDS_Shape CutRoundTubeWithSteppedWrappedEnd(double Shift_ = 0.0)
    {
        const auto _Tube = MakeRoundTube();
        const auto _PositiveXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(0.0, 0.0, 160.0 + Shift_),
            60.0,
            60.0,
            40.0).Shape();
        const auto _NegativeXRemoval = BRepPrimAPI_MakeBox(
            gp_Pnt(-60.0, 0.0, 140.0 + Shift_),
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

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankRetainsShortExtrusionAxisAndReplaysHole)
{
    const auto _Blank = MakeShortXAngleBlank();
    const auto _Hole = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(6.0, 30.0, -1.0), gp_Dir(0.0, 0.0, 1.0)),
        2.0, 7.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Blank, _Hole);
    ASSERT_TRUE(_Cut.IsDone());
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Cut.Shape(), _Blank, 12.0);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent)
        << (_Result.ReplayValidation.Diagnostics.empty()
            ? "" : _Result.ReplayValidation.Diagnostics.front());
    EXPECT_EQ("verified-known-extrusion", _Result.Geometry.Metadata.at("baseSource"));
    EXPECT_EQ("1", _Result.Geometry.Metadata.at("baseSectionSampleCount"));
    const auto& _Base = GetBaseNode(_Result.Geometry);
    EXPECT_NEAR(1.0, _Base.Frame.ZDirection.X, 1.0e-12);
    EXPECT_NEAR(0.0, _Base.First, 1.0e-12);
    EXPECT_NEAR(12.0, _Base.Last, 1.0e-12);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
    EXPECT_GT(ShapeVolume(_Blank) - ShapeVolume(_Cut.Shape()), 0.0);
    EXPECT_GT(std::stod(_Result.Geometry.Metadata.at("removalVolume")), 0.0);

    auto _Tampered = _Result.Geometry;
    for (auto& _Node : _Tampered.SolidNodes)
    {
        if (_Node.ID == _Tampered.BaseNodeID)
        {
            std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(
                _Node.Data).Frame.Location.Y += 1.0;
        }
    }
    EXPECT_FALSE(iCAX::OpenCascade::ValidateTubeCSGReplay(
        _Cut.Shape(), _Tampered).bEquivalent);
}

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankReplaysManufacturedChannelWithReflectedArcsAndFourHoles)
{
    auto _Root = std::filesystem::path(__FILE__).parent_path();
    for (int _Index = 0; _Index < 6; ++_Index)
        _Root = _Root.parent_path();
    const auto _Path = _Root
        / "samples/structural-steel/vendors-angle-channel/unistrut-P1044-c-fitting.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Path)) << _Path.string();
    STEPControl_Reader _Reader;
    ASSERT_EQ(IFSelect_RetDone, _Reader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_Reader.TransferRoots(), 0);
    const auto _Source = _Reader.OneShape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Source).IsValid());

    // The original manufacturing model has five coplanar, unperforated end
    // faces. Extrude every piece to preserve its exact bend radii and thickness.
    TopoDS_Shape _Blank;
    std::size_t _EndFaceCount = 0;
    for (TopExp_Explorer _Explorer(_Source, TopAbs_FACE);
        _Explorer.More(); _Explorer.Next())
    {
        const auto _Face = TopoDS::Face(_Explorer.Current());
        const BRepAdaptor_Surface _Surface(_Face, true);
        if (_Surface.GetType() != GeomAbs_Plane
            || std::abs(_Surface.Plane().Axis().Direction().X()) < 0.999999
            || std::abs(_Surface.Plane().Location().X()) > 1.0e-5)
            continue;
        ++_EndFaceCount;
        BRepPrimAPI_MakePrism _Prism(_Face, gp_Vec(40.0, 0.0, 0.0));
        ASSERT_TRUE(_Prism.IsDone());
        if (_Blank.IsNull())
            _Blank = _Prism.Shape();
        else
        {
            BRepAlgoAPI_Fuse _Fuse(_Blank, _Prism.Shape());
            ASSERT_TRUE(_Fuse.IsDone());
            _Blank = _Fuse.Shape();
        }
    }
    ASSERT_EQ(5u, _EndFaceCount);
    ShapeUpgrade_UnifySameDomain _Unify(_Blank, true, true, true);
    _Unify.Build();
    _Blank = _Unify.Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Blank).IsValid());
    // Four diameter-14 holes cross 5 mm of material each.
    EXPECT_NEAR(4.0 * std::acos(-1.0) * 7.0 * 7.0 * 5.0,
        ShapeVolume(_Blank) - ShapeVolume(_Source), 1.0e-5);

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.Tolerance = 1.0e-6;
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Source, _Blank, 40.0, _Options);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent)
        << (_Result.ReplayValidation.Diagnostics.empty()
            ? "" : _Result.ReplayValidation.Diagnostics.front());
    EXPECT_LT(_Result.ReplayValidation.RelativeVolumeError, 1.0e-10);
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-10);
    EXPECT_LT(std::stod(_Result.Geometry.Metadata.at("additionVolume")), 1.0e-8);
    EXPECT_NEAR(4.0 * std::acos(-1.0) * 7.0 * 7.0 * 5.0,
        std::stod(_Result.Geometry.Metadata.at("removalVolume")), 1.0e-5);

    std::size_t _ReflectedArcs = 0;
    for (const auto& _Loop : GetBaseNode(_Result.Geometry).Section.Boundaries)
        for (const auto& _Curve : _Loop.Curves)
            if (const auto* _Circle = std::get_if<iCAX::GeometryData::Circle2>(
                &_Curve.Segment.Curve))
            {
                const auto& _Placement = _Circle->Placement;
                if (_Placement.XDirection.X * _Placement.YDirection.Y
                    - _Placement.XDirection.Y * _Placement.YDirection.X < 0.0)
                    ++_ReflectedArcs;
            }
    EXPECT_EQ(4u, _ReflectedArcs);
}

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankRecoversOriginalP1026LetteringWithExactCurvesAndIslands)
{
    namespace Tube = iCAX::GeometryData::Tube;
    auto _Root = std::filesystem::path(__FILE__).parent_path();
    for (int _Index = 0; _Index < 6; ++_Index)
        _Root = _Root.parent_path();
    const auto _Path = _Root
        / "samples/structural-steel/vendors-angle-channel/unistrut-P1026-two-hole-angle.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Path)) << _Path.string();
    STEPControl_Reader _Reader;
    ASSERT_EQ(IFSelect_RetDone, _Reader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_Reader.TransferRoots(), 0);
    const auto _Original = _Reader.OneShape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Original).IsValid());

    // Original end faces, including the bend, directly define the uncut blank.
    // Rotate the supplied Y extrusion into the editor's +X convention.
    TopoDS_Shape _OriginalBlank;
    for (TopExp_Explorer _Explorer(_Original, TopAbs_FACE);
        _Explorer.More(); _Explorer.Next())
    {
        const auto _Face = TopoDS::Face(_Explorer.Current());
        const BRepAdaptor_Surface _Surface(_Face, true);
        if (_Surface.GetType() != GeomAbs_Plane
            || std::abs(_Surface.Plane().Axis().Direction().Y()) < 0.999999
            || std::abs(_Surface.Plane().Location().Y()) > 1.0e-5)
            continue;
        BRepPrimAPI_MakePrism _Prism(_Face, gp_Vec(0.0, 40.0, 0.0));
        ASSERT_TRUE(_Prism.IsDone());
        if (_OriginalBlank.IsNull())
            _OriginalBlank = _Prism.Shape();
        else
        {
            BRepAlgoAPI_Fuse _Fuse(_OriginalBlank, _Prism.Shape());
            ASSERT_TRUE(_Fuse.IsDone());
            _OriginalBlank = _Fuse.Shape();
        }
    }
    ASSERT_FALSE(_OriginalBlank.IsNull());
    ShapeUpgrade_UnifySameDomain _Unify(_OriginalBlank, true, true, true);
    _Unify.Build();
    gp_Trsf _ToEditor;
    _ToEditor.SetValues(0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    const auto _Source = BRepBuilderAPI_Transform(_Original, _ToEditor, true).Shape();
    const auto _Blank = BRepBuilderAPI_Transform(_Unify.Shape(), _ToEditor, true).Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Blank).IsValid());
    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.Tolerance = 1.0e-6;
    _Options.SourceBRepResourceID = "test://original-p1026";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Source, _Blank, 40.0, _Options);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent)
        << (_Result.ReplayValidation.Diagnostics.empty()
            ? "" : _Result.ReplayValidation.Diagnostics.front());
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LT(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-10);
    EXPECT_LT(_Result.ReplayValidation.RelativeVolumeError, 1.0e-10);

    std::size_t _Pockets = 0, _Islands = 0, _Splines = 0, _Lines = 0;
    auto _Edited = _Result.Geometry;
    std::string _EditedID;
    for (auto& _Node : _Edited.SolidNodes)
    {
        const auto _SourceTag = _Node.Metadata.find("source");
        if (_SourceTag == _Node.Metadata.end()
            || _SourceTag->second != "occ.planar-cap-extrusion")
            continue;
        ++_Pockets;
        auto& _Extrusion = std::get<Tube::SExtrudedRegionNode>(_Node.Data);
        EXPECT_NEAR(0.2, _Extrusion.Last - _Extrusion.First, 1.0e-8);
        _Islands += _Extrusion.Section.Boundaries.size() - 1;
        for (const auto& _Loop : _Extrusion.Section.Boundaries)
            for (const auto& _Curve : _Loop.Curves)
            {
                const auto& _Data = _Curve.Segment.Curve;
                _Splines += std::holds_alternative<iCAX::GeometryData::BSpline2>(_Data)
                    || std::holds_alternative<iCAX::GeometryData::NURBS2>(_Data);
                _Lines += std::holds_alternative<iCAX::GeometryData::Segment2>(_Data);
                EXPECT_FALSE(std::holds_alternative<iCAX::GeometryData::Polyline2>(_Data));
            }
        ASSERT_TRUE(_Node.Relations.has_value());
        ASSERT_EQ(1u, _Node.Relations->MaterialSpans.size());
        const auto& _Span = _Node.Relations->MaterialSpans.front();
        EXPECT_EQ(Tube::EMaterialSpanEndpointKind::HostBoundary, _Span.Start.Kind);
        EXPECT_EQ(Tube::EMaterialSpanEndpointKind::FeatureTermination, _Span.End.Kind);
        EXPECT_NEAR(0.2, _Span.End.Station.Value - _Span.Start.Station.Value, 1.0e-8);
        EXPECT_TRUE(std::any_of(_Node.Relations->SurfaceEvidence.begin(),
            _Node.Relations->SurfaceEvidence.end(), [](const auto& _Evidence) {
                return _Evidence.Role == Tube::ESurfaceEvidenceRole::GeneratedTermination
                    && _Evidence.SourceResourceID == "test://original-p1026"
                    && _Evidence.SourceShapeID != 0;
            }));
        if (_EditedID.empty())
        {
            _EditedID = _Node.ID;
            _Extrusion.Last += 0.1;
        }
    }
    EXPECT_EQ(5u, _Pockets);
    EXPECT_EQ(3u, _Islands);
    EXPECT_EQ(51u, _Splines);
    EXPECT_EQ(26u, _Lines);
    ASSERT_FALSE(_EditedID.empty());
    iCAX::OpenCascade::STubeCSGEvaluationOptions _ReplayOptions;
    _ReplayOptions.Tolerance = _Options.Tolerance;
    const auto _Changed = iCAX::OpenCascade::EvaluateTubeCSG(_Edited, _ReplayOptions);
    ASSERT_TRUE(_Changed.bOK);
    EXPECT_TRUE(BRepCheck_Analyzer(_Changed.Shape).IsValid());
    const auto _Difference = iCAX::OpenCascade::ValidateTubeCSGReplay(
        _Source, _Edited, _ReplayOptions);
    EXPECT_TRUE(_Difference.bEvaluated);
    EXPECT_FALSE(_Difference.bEquivalent);
    EXPECT_GT(_Difference.MissingVolume, 0.01);
    for (auto& _Node : _Edited.SolidNodes)
        if (_Node.ID == _EditedID)
            std::get<Tube::SExtrudedRegionNode>(_Node.Data).Last -= 0.1;
    EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(
        _Source, _Edited, _ReplayOptions).bEquivalent);
}

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankRecoversOriginalTekSpan60235ResidualEndTriangles)
{
    namespace Tube = iCAX::GeometryData::Tube;
    auto _Root = std::filesystem::path(__FILE__).parent_path();
    for (int _Index = 0; _Index < 6; ++_Index)
        _Root = _Root.parent_path();
    const auto _Path = _Root / "samples/structural-steel/recovery/report/solids"
        / "vendors-angle-channel__tekspan-60235-saddle-bracket.step-85659452/solid-1.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Path)) << _Path.string();
    STEPControl_Reader _Reader;
    ASSERT_EQ(IFSelect_RetDone, _Reader.ReadFile(_Path.string().c_str()));
    ASSERT_GT(_Reader.TransferRoots(), 0);
    const auto _Original = _Reader.OneShape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Original).IsValid());

    // Measured supplier-to-editor rigid placement. The source end is cut,
    // so its end face cannot define the blank. Prepare the independently
    // verified channel: 6 mm wall, 12/6 mm outer/inner bends, 230 mm length.
    gp_Trsf _ToEditor;
    _ToEditor.SetValues(0.0, 1.0, -4.6888593636700982e-16, -736.6740053122594,
        0.0, 4.6888593636700982e-16, 1.0, -1261.3618555210098,
        1.0, 0.0, 0.0, -695.66891869732001);
    const auto _Source = BRepBuilderAPI_Transform(_Original, _ToEditor, true).Shape();
    BRepBuilderAPI_MakeWire _Wire;
    const auto _Line = [&](double Y0_, double Z0_, double Y1_, double Z1_) {
        _Wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0.0, Y0_, Z0_), gp_Pnt(0.0, Y1_, Z1_)).Edge());
    };
    const auto _Arc = [&](double Y0_, double Z0_, double YM_, double ZM_, double Y1_, double Z1_) {
        const auto _Curve = GC_MakeArcOfCircle(gp_Pnt(0.0, Y0_, Z0_),
            gp_Pnt(0.0, YM_, ZM_), gp_Pnt(0.0, Y1_, Z1_)).Value();
        _Wire.Add(BRepBuilderAPI_MakeEdge(_Curve).Edge());
    };
    const double _OuterDiagonal = 12.0 / std::sqrt(2.0);
    const double _InnerDiagonal = 6.0 / std::sqrt(2.0);
    _Line(78.0, -51.0, 78.0, -57.0);
    _Line(78.0, -57.0, -66.0, -57.0);
    _Arc(-66.0, -57.0, -66.0 - _OuterDiagonal, -45.0 - _OuterDiagonal, -78.0, -45.0);
    _Line(-78.0, -45.0, -78.0, 45.0);
    _Arc(-78.0, 45.0, -66.0 - _OuterDiagonal, 45.0 + _OuterDiagonal, -66.0, 57.0);
    _Line(-66.0, 57.0, 78.0, 57.0);
    _Line(78.0, 57.0, 78.0, 51.0);
    _Line(78.0, 51.0, -66.0, 51.0);
    _Arc(-66.0, 51.0, -66.0 - _InnerDiagonal, 45.0 + _InnerDiagonal, -72.0, 45.0);
    _Line(-72.0, 45.0, -72.0, -45.0);
    _Arc(-72.0, -45.0, -66.0 - _InnerDiagonal, -45.0 - _InnerDiagonal, -66.0, -51.0);
    _Line(-66.0, -51.0, 78.0, -51.0);
    ASSERT_TRUE(_Wire.IsDone());
    BRepPrimAPI_MakePrism _Prism(BRepBuilderAPI_MakeFace(_Wire.Wire()).Face(), gp_Vec(230.0, 0.0, 0.0));
    ASSERT_TRUE(_Prism.IsDone());
    const auto _Blank = _Prism.Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Blank).IsValid());
    EXPECT_NEAR(ShapeVolume(_Blank), 560658.5807575857, 1.0e-5);
    EXPECT_NEAR(ShapeVolume(_Source), 407163.45024704293, 1.0e-5);

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.SourceBRepResourceID = "test://original-tekspan60235";
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Source, _Blank, 230.0, _Options);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent)
        << (_Result.ReplayValidation.Diagnostics.empty() ? "" : _Result.ReplayValidation.Diagnostics.front());
    EXPECT_LE(_Result.ReplayValidation.RelativeVolumeError, 1.0e-8);
    EXPECT_LE(_Result.Geometry.RelativeUnparameterizedVolume, 1.0e-8);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    EXPECT_LE(std::stod(_Result.Geometry.Metadata.at("additionVolume")), 1.0e-8);
    EXPECT_FALSE(std::any_of(_Result.Geometry.SolidNodes.begin(), _Result.Geometry.SolidNodes.end(),
        [](const auto& Node_) {
            return std::holds_alternative<Tube::SCompositeVolumeNode>(Node_.Data)
                || std::holds_alternative<Tube::SResidualBRepNode>(Node_.Data);
        }));

    std::vector<std::string> _Triangles;
    bool _TopTriangle = false, _BottomTriangle = false;
    for (const auto& _Node : _Result.Geometry.SolidNodes)
    {
        const auto _Mode = _Node.Metadata.find("recognitionMode");
        if (_Mode == _Node.Metadata.end() || _Mode->second != "exact-residual-section")
            continue;
        _Triangles.push_back(_Node.ID);
        ASSERT_TRUE(std::holds_alternative<Tube::SExtrudedRegionNode>(_Node.Data));
        const auto& _Cut = std::get<Tube::SExtrudedRegionNode>(_Node.Data);
        EXPECT_EQ(_Node.Metadata.at("source"), "occ.removal-extrusion");
        EXPECT_EQ(_Node.Metadata.at("independentReplay"), "complete-removal");
        ASSERT_TRUE(_Node.RemovalSemantics.has_value());
        EXPECT_EQ(_Node.RemovalSemantics->Extent, Tube::ERemovalExtentKind::Through);
        ASSERT_TRUE(_Node.Relations.has_value());
        ASSERT_TRUE(_Node.Relations->MaterialEffect.has_value());
        EXPECT_EQ(_Node.Relations->MaterialEffect->Role, Tube::EFeatureMaterialRole::Penetration);
        EXPECT_TRUE(_Node.Relations->MaterialSpans.empty());
        EXPECT_NEAR(_Cut.Last - _Cut.First, 6.0, 1.0e-7);
        EXPECT_NEAR(std::abs(_Cut.Frame.ZDirection.Z), 1.0, 1.0e-8);
        ASSERT_EQ(_Cut.Section.Boundaries.size(), 1u);
        ASSERT_EQ(_Cut.Section.Boundaries.front().Curves.size(), 3u);
        for (const auto& _Edge : _Cut.Section.Boundaries.front().Curves)
            EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Segment2>(_Edge.Segment.Curve));
        const auto _Preview = _Result.PreviewShapes.find(_Node.ID);
        ASSERT_NE(_Preview, _Result.PreviewShapes.end());
        ASSERT_TRUE(BRepCheck_Analyzer(_Preview->second).IsValid());
        EXPECT_NEAR(ShapeVolume(_Preview->second), 300.0, 1.0e-5);
        Bnd_Box _Bounds;
        BRepBndLib::AddOptimal(_Preview->second, _Bounds, false, false);
        double _X0, _Y0, _Z0, _X1, _Y1, _Z1;
        _Bounds.Get(_X0, _Y0, _Z0, _X1, _Y1, _Z1);
        EXPECT_NEAR(_X0, 220.0, 1.0e-6);
        EXPECT_NEAR(_X1, 230.0, 1.0e-6);
        EXPECT_NEAR(_Y0, -62.0, 1.0e-6);
        EXPECT_NEAR(_Y1, -52.0, 1.0e-6);
        EXPECT_NEAR(_Z1 - _Z0, 6.0, 1.0e-6);
        _TopTriangle |= std::abs(_Z0 - 51.0) <= 1.0e-6 && std::abs(_Z1 - 57.0) <= 1.0e-6;
        _BottomTriangle |= std::abs(_Z0 + 57.0) <= 1.0e-6 && std::abs(_Z1 + 51.0) <= 1.0e-6;
    }
    ASSERT_EQ(_Triangles.size(), 2u);
    EXPECT_TRUE(_TopTriangle);
    EXPECT_TRUE(_BottomTriangle);

    // Reevaluate with no external BRep map: saved residual geometry cannot
    // masquerade as an editable construction, even if its volume is small.
    iCAX::OpenCascade::STubeCSGEvaluationOptions _ReplayOptions;
    _ReplayOptions.Tolerance = 1.0e-6;
    _ReplayOptions.EvaluateAllConstructionBodies = false;
    const auto _Independent = iCAX::OpenCascade::ValidateTubeCSGReplay(_Source, _Result.Geometry, _ReplayOptions);
    ASSERT_TRUE(_Independent.bEvaluated);
    ASSERT_TRUE(_Independent.bEquivalent);
    EXPECT_LE(_Independent.RelativeVolumeError, 1.0e-8);
    auto _Edited = _Result.Geometry;
    auto _Triangle = std::find_if(_Edited.SolidNodes.begin(), _Edited.SolidNodes.end(),
        [&](const auto& Node_) { return Node_.ID == _Triangles.front(); });
    ASSERT_NE(_Triangle, _Edited.SolidNodes.end());
    auto& _Cut = std::get<Tube::SExtrudedRegionNode>(_Triangle->Data);
    _Cut.Frame.Location.X -= 1.0;
    const auto _Changed = iCAX::OpenCascade::EvaluateTubeCSG(_Edited, _ReplayOptions);
    ASSERT_TRUE(_Changed.bOK);
    ASSERT_TRUE(BRepCheck_Analyzer(_Changed.Shape).IsValid());
    const auto _ChangedDifference = iCAX::OpenCascade::ValidateTubeCSGReplay(_Source, _Edited, _ReplayOptions);
    EXPECT_TRUE(_ChangedDifference.bEvaluated);
    EXPECT_FALSE(_ChangedDifference.bEquivalent);
    EXPECT_GT(_ChangedDifference.MissingVolume + _ChangedDifference.UnexpectedVolume, 0.01);
    _Cut.Frame.Location.X += 1.0;
    const auto _Restored = iCAX::OpenCascade::ValidateTubeCSGReplay(_Source, _Edited, _ReplayOptions);
    EXPECT_TRUE(_Restored.bEvaluated);
    EXPECT_TRUE(_Restored.bEquivalent);
    EXPECT_LE(_Restored.RelativeVolumeError, 1.0e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, RetainsIndustryGeometryAfterPeriodicEdgeRepairDespitePCurveWarnings)
{
    auto root = std::filesystem::path(__FILE__).parent_path();
    for (int index = 0; index < 6; ++index) root = root.parent_path();
    const auto path = root / "samples/structural-steel/vendors-angle-channel/unistrut-P1033-three-hole-t-angle.step";
    STEPControl_Reader reader;
    ASSERT_EQ(IFSelect_RetDone, reader.ReadFile(path.string().c_str()));
    ASSERT_GT(reader.TransferRoots(), 0);
    const auto source = reader.OneShape();
    ASSERT_TRUE(BRepCheck_Analyzer(source).IsValid());
    ASSERT_FALSE(BRepCheck_Analyzer(source, true, false, true).IsValid());
    std::stringstream before,after; BRepTools::Write(source,before);
    iCAX::OpenCascade::STubeCSGConversionOptions options;
    options.SourceBRepResourceID="test://source";
    options.RemovalBRepResourceID="test://removal";
    options.AdditionBRepResourceID="test://addition";
    const auto result = iCAX::OpenCascade::ConvertBRepToTubeCSG(source,options);
    ASSERT_TRUE(result.ReplayValidation.bEquivalent);
    EXPECT_LE(result.ReplayValidation.RelativeVolumeError,1e-8);
    EXPECT_FALSE(result.Geometry.SolidNodes.empty());
    EXPECT_NE(iCAX::GeometryData::Tube::ERecognitionStatus::Failed,result.Geometry.RecognitionStatus);
    iCAX::OpenCascade::STubeCSGEvaluationOptions replay;
    replay.ExternalBRepShapes.emplace(options.SourceBRepResourceID,source);
    if(!result.RemovalShape.IsNull())replay.ExternalBRepShapes.emplace(options.RemovalBRepResourceID,result.RemovalShape);
    if(!result.AdditionShape.IsNull())replay.ExternalBRepShapes.emplace(options.AdditionBRepResourceID,result.AdditionShape);
    const auto evaluated=iCAX::OpenCascade::EvaluateTubeCSG(result.Geometry,replay);
    ASSERT_TRUE(evaluated.bOK);
    ASSERT_TRUE(BRepCheck_Analyzer(evaluated.Shape).IsValid());
    const auto proof=iCAX::OpenCascade::ValidateTubeCSGReplay(source,result.Geometry,replay);
    EXPECT_TRUE(proof.bEquivalent);
    BRepTools::Write(source,after); EXPECT_EQ(before.str(),after.str());
}

TEST(OpenCascadeTubeCSGConverterTest, ReplaysImportedHoleIntersectionsDespiteExactPCurveWarnings)
{
    auto root = std::filesystem::path(__FILE__).parent_path();
    for (int index = 0; index < 6; ++index) root = root.parent_path();
    for (const auto* file : { "03_round_tube_through_hole.step", "04_round_tube_oblique_hole.step" })
    {
        SCOPED_TRACE(file);
        STEPControl_Reader reader;
        ASSERT_EQ(IFSelect_RetDone, reader.ReadFile((root / "samples/tube-geometry" / file).string().c_str()));
        ASSERT_GT(reader.TransferRoots(), 0);
        const auto source = reader.OneShape();
        ASSERT_TRUE(BRepCheck_Analyzer(source).IsValid());
        ASSERT_FALSE(BRepCheck_Analyzer(source, true, false, true).IsValid());
        const auto result = iCAX::OpenCascade::ConvertBRepToTubeCSG(source);
        ASSERT_TRUE(result.ReplayValidation.bEquivalent);
        EXPECT_LE(result.ReplayValidation.RelativeVolumeError, 1.0e-8);
        ASSERT_GE(result.Geometry.SolidNodes.size(), 2u);
        EXPECT_EQ("independent-copy-equivalent", result.Geometry.Metadata.at("sourceBoundaryCheck"));
    }
}

TEST(OpenCascadeTubeCSGConverterTest, ExactPlanarCapPreservesRationalCurvesAndInnerIsland)
{
    namespace Tube = iCAX::GeometryData::Tube;
    const auto _Blank = MakeShortXAngleBlank();
    const gp_Ax2 _Placement(gp_Pnt(6.0, 30.0, 4.8), gp_Dir(0.0, 0.0, 1.0));
    const auto _MakeRationalWire = [&_Placement](double Radius_) {
        const auto _Wire = BRepBuilderAPI_MakeWire(
            BRepBuilderAPI_MakeEdge(gp_Circ(_Placement, Radius_)).Edge()).Wire();
        BRepBuilderAPI_NurbsConvert _Convert(_Wire, true);
        EXPECT_TRUE(_Convert.IsDone());
        return TopoDS::Wire(_Convert.Shape());
    };
    auto _Face = BRepBuilderAPI_MakeFace(gp_Pln(_Placement), _MakeRationalWire(3.0), true);
    auto _Inner = _MakeRationalWire(1.0);
    _Inner.Reverse();
    _Face.Add(_Inner);
    ASSERT_TRUE(_Face.IsDone());
    const auto _Cutter = BRepPrimAPI_MakePrism(_Face.Face(), gp_Vec(0.0, 0.0, 0.4)).Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Cutter).IsValid());
    BRepAlgoAPI_Cut _Cut(_Blank, _Cutter);
    ASSERT_TRUE(_Cut.IsDone());
    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.Tolerance = 1.0e-6;
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Cut.Shape(), _Blank, 12.0, _Options);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent);
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    const auto _Pocket = std::find_if(_Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(), [](const auto& _Node) {
            const auto _Source = _Node.Metadata.find("source");
            return _Source != _Node.Metadata.end()
                && _Source->second == "occ.planar-cap-extrusion";
        });
    ASSERT_NE(_Result.Geometry.SolidNodes.end(), _Pocket);
    const auto& _Extrusion = std::get<Tube::SExtrudedRegionNode>(_Pocket->Data);
    ASSERT_EQ(2u, _Extrusion.Section.Boundaries.size());
    for (const auto& _Loop : _Extrusion.Section.Boundaries)
    {
        ASSERT_FALSE(_Loop.Curves.empty());
        for (const auto& _Curve : _Loop.Curves)
            EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::NURBS2>(
                _Curve.Segment.Curve));
    }
    EXPECT_NEAR(0.2, _Extrusion.Last - _Extrusion.First, 1.0e-8);
    const auto _PreciseVolume = [](const TopoDS_Shape& Shape_) {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties, 1.0e-10);
        return std::abs(_Properties.Mass());
    };
    // OCC mass integration on the NURBS annulus differs slightly from its analytic area;
    // the replay equivalence above is the strict geometry check.
    EXPECT_NEAR(8.0 * std::acos(-1.0) * 0.2,
        _PreciseVolume(_Blank) - _PreciseVolume(_Cut.Shape()), 1.0e-2);
}

TEST(OpenCascadeTubeCSGConverterTest, DoesNotMisclassifyTaperedPocketAsExactPlanarCapExtrusion)
{
    const auto _Blank = MakeShortXAngleBlank();
    const auto _Cutter = BRepPrimAPI_MakeCone(
        gp_Ax2(gp_Pnt(6.0, 30.0, 4.0), gp_Dir(0.0, 0.0, 1.0)),
        2.0, 3.0, 1.5).Shape();
    BRepAlgoAPI_Cut _Cut(_Blank, _Cutter);
    ASSERT_TRUE(_Cut.IsDone());
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Cut.Shape(), _Blank, 12.0);
    EXPECT_TRUE(std::none_of(_Result.Geometry.SolidNodes.begin(),
        _Result.Geometry.SolidNodes.end(), [](const auto& _Node) {
            const auto _Source = _Node.Metadata.find("source");
            return _Source != _Node.Metadata.end()
                && _Source->second == "occ.planar-cap-extrusion";
        }));
}

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankRejectsIncorrectAxialExtent)
{
    const auto _Blank = MakeShortXAngleBlank();
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Blank, _Blank, 13.0);
    EXPECT_EQ(iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Result.Geometry.RecognitionStatus);
    EXPECT_FALSE(_Result.ReplayValidation.bEquivalent);
    ASSERT_FALSE(_Result.Geometry.Diagnostics.empty());
    EXPECT_EQ("known-blank-not-extrusion", _Result.Geometry.Diagnostics.front().Code);
}

TEST(OpenCascadeTubeCSGConverterTest, KnownBlankRejectsLocalCutAwayFromCentralSection)
{
    const auto _Blank = MakeShortXAngleBlank();
    const auto _Notch = BRepPrimAPI_MakeBox(
        gp_Pnt(1.0, 20.0, -1.0), 2.0, 5.0, 7.0).Shape();
    BRepAlgoAPI_Cut _Cut(_Blank, _Notch);
    ASSERT_TRUE(_Cut.IsDone());
    const auto _Result = iCAX::OpenCascade::ConvertBRepToTubeCSGFromExtrudedBlank(
        _Cut.Shape(), _Cut.Shape(), 12.0);
    EXPECT_EQ(iCAX::GeometryData::Tube::ERecognitionStatus::Failed,
        _Result.Geometry.RecognitionStatus);
    EXPECT_FALSE(_Result.ReplayValidation.bEquivalent);
    ASSERT_FALSE(_Result.Geometry.Diagnostics.empty());
    EXPECT_EQ("known-blank-not-extrusion", _Result.Geometry.Diagnostics.front().Code);
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

TEST(OpenCascadeTubeCSGConverterTest, ReplayRejectsShiftedCavityDespiteEqualVolume)
{
    const auto _Source = MakeDoubleCavityExtrusion();
    auto _Conversion = iCAX::OpenCascade::ConvertBRepToTubeCSG(_Source);
    ASSERT_TRUE(_Conversion.ReplayValidation.bEquivalent)
        << (_Conversion.ReplayValidation.Diagnostics.empty()
            ? "" : _Conversion.ReplayValidation.Diagnostics.front());

    auto& _Geometry = _Conversion.Geometry;
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
        _pSegment->Start.X += 3.0;
        _pSegment->End.X += 3.0;
    }

    const auto _Evaluation = iCAX::OpenCascade::EvaluateTubeCSG(_Geometry);
    ASSERT_TRUE(_Evaluation.bOK);
    EXPECT_NEAR(ShapeVolume(_Source), ShapeVolume(_Evaluation.Shape), 1.0e-4);

    const auto _Replay = iCAX::OpenCascade::ValidateTubeCSGReplay(
        _Source, _Geometry);
    ASSERT_TRUE(_Replay.bEvaluated)
        << (_Replay.Diagnostics.empty() ? "" : _Replay.Diagnostics.front());
    EXPECT_FALSE(_Replay.bEquivalent);
    EXPECT_GT(_Replay.MissingVolume, 0.0);
    EXPECT_GT(_Replay.UnexpectedVolume, 0.0);
    EXPECT_GT(_Replay.RelativeVolumeError, 1.0e-4);
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

TEST(OpenCascadeTubeCSGConverterTest, ReplaysAndEditsSteppedWrappedEndExactly)
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
    EXPECT_TRUE(_Result.ReplayValidation.bEvaluated);
    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent);
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-10);
    const auto _Reevaluation = iCAX::OpenCascade::EvaluateTubeCSG(
        _Result.Geometry);
    ASSERT_TRUE(_Reevaluation.bOK)
        << (_Reevaluation.Diagnostics.empty()
            ? ""
            : _Reevaluation.Diagnostics.front());
    EXPECT_NEAR(
        ShapeVolume(_Shape),
        ShapeVolume(_Reevaluation.Shape),
        1.0e-7);
    auto _Edited = _Result.Geometry;
    auto& _EditedWrapped = std::get<iCAX::GeometryData::Tube::SWrappedVolumeNode>(
        std::find_if(_Edited.SolidNodes.begin(), _Edited.SolidNodes.end(), [&](const auto& node) {
            return node.ID == _Wrapped->ID;
        })->Data);
    for (auto& _Loop : _EditedWrapped.UVRegion.Boundaries)
        for (auto& _Curve : _Loop.Curves) {
            auto* _Polyline = std::get_if<iCAX::GeometryData::Polyline2>(&_Curve.Segment.Curve);
            ASSERT_NE(nullptr, _Polyline);
            for (auto& _Point : _Polyline->Points) _Point.Y += 5.0;
        }
    EXPECT_FALSE(iCAX::OpenCascade::ValidateTubeCSGReplay(_Shape, _Edited).bEquivalent);
    const auto _Moved = iCAX::OpenCascade::ValidateTubeCSGReplay(CutRoundTubeWithSteppedWrappedEnd(5.0), _Edited);
    EXPECT_TRUE(_Moved.bEquivalent);
    EXPECT_LT(_Moved.RelativeVolumeError, 1.0e-10);
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

TEST(OpenCascadeTubeCSGConverterTest, ReplayComparisonDoesNotModifySourceTolerances)
{
    const auto source = BRepPrimAPI_MakeBox(100.0, 60.0, 200.0).Shape();
    auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(source);
    ASSERT_TRUE(converted.ReplayValidation.bEquivalent);
    auto base = std::find_if(converted.Geometry.SolidNodes.begin(),
        converted.Geometry.SolidNodes.end(), [&](const auto& node) {
            return node.ID == converted.Geometry.BaseNodeID;
        });
    ASSERT_NE(converted.Geometry.SolidNodes.end(), base);
    // Coincident faces inside the fuzzy tolerance grow vertex/edge tolerances
    // when the comparison operates directly on its caller's shapes.
    std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(base->Data).Frame.Location.X += 0.0002;
    const auto bytes = [&] {
        std::ostringstream stream;
        BRepTools::Write(source, stream);
        return stream.str();
    };
    const auto before = bytes();
    const auto first = iCAX::OpenCascade::ValidateTubeCSGReplay(source, converted.Geometry);
    ASSERT_TRUE(first.bEvaluated);
    EXPECT_TRUE(before == bytes()) << "Replay validation modified the original BRep";
    const auto second = iCAX::OpenCascade::ValidateTubeCSGReplay(source, converted.Geometry);
    EXPECT_EQ(first.bEquivalent, second.bEquivalent);
    EXPECT_DOUBLE_EQ(first.MissingVolume, second.MissingVolume);
    EXPECT_DOUBLE_EQ(first.UnexpectedVolume, second.UnexpectedVolume);
    EXPECT_TRUE(before == bytes());
}

TEST(OpenCascadeTubeCSGConverterTest, ParallelFeatureRecognitionPreservesGeometryAndIdentity)
{
    auto part = BRepPrimAPI_MakeBox(100.0, 60.0, 200.0).Shape();
    for (const auto& center : std::array<gp_Pnt, 4>{gp_Pnt(20, 70, 80), gp_Pnt(22, 70, 80),
        gp_Pnt(50, 70, 100), gp_Pnt(70, 70, 120)}) {
        BRepAlgoAPI_Cut cut(part, BRepPrimAPI_MakeCylinder(
            gp_Ax2(center, gp_Dir(0, -1, 0)), 3, 80).Shape());
        ASSERT_TRUE(cut.IsDone()); part = cut.Shape();
    }
    const auto sourceBytes = [&] { std::ostringstream stream; BRepTools::Write(part, stream); return stream.str(); };
    const auto before = sourceBytes();
    iCAX::OpenCascade::STubeCSGConversionOptions options; options.FeatureWorkerCount = 1;
    auto serial = iCAX::OpenCascade::ConvertBRepToTubeCSG(part, options);
    ASSERT_TRUE(serial.ReplayValidation.bEquivalent);
    options.FeatureWorkerCount = 4;
    auto parallel = iCAX::OpenCascade::ConvertBRepToTubeCSG(part, options);
    ASSERT_TRUE(parallel.ReplayValidation.bEquivalent);
    std::erase_if(serial.Geometry.Metadata, [](const auto& item) { return item.first.starts_with("elapsedMs."); });
    std::erase_if(parallel.Geometry.Metadata, [](const auto& item) { return item.first.starts_with("elapsedMs."); });
    EXPECT_TRUE(iCAX::GeometryData::Tube::Persistence::Serialize(serial.Geometry)
        == iCAX::GeometryData::Tube::Persistence::Serialize(parallel.Geometry));
    EXPECT_EQ(before, sourceBytes());
}

namespace {
iCAX::GeometryData::Tube::SRegionLoop2 NeutralRectangle(
    const std::string& id, double x, double y, double width, double height, bool ccw = true)
{
    namespace G = iCAX::GeometryData; namespace T = G::Tube;
    std::vector<G::Point2> points{{x,y},{x+width,y},{x+width,y+height},{x,y+height}};
    if (!ccw) std::reverse(points.begin(), points.end());
    T::SRegionLoop2 loop; loop.ID = id;
    for (std::size_t i = 0; i < points.size(); ++i) {
        T::SRegionCurve2 curve; curve.ID = id + "/" + std::to_string(i);
        G::Segment2 line; line.Start = points[i]; line.End = points[(i+1)%points.size()];
        curve.Segment.Curve = line; loop.Curves.push_back(curve);
    }
    return loop;
}
}

TEST(OpenCascadeTubeCSGConverterTest, LargeReplayPreservesSourceAndRejectsEqualVolumeHoleShift)
{
    namespace T = iCAX::GeometryData::Tube;
    T::SExtrudedRegionNode data; data.First = 0; data.Last = 5;
    data.Section.FillRule = T::EPlanarFillRule::EvenOdd;
    data.Section.Boundaries.push_back(NeutralRectangle("outer",0,0,34,22));
    BRep_Builder builder; TopoDS_Compound cuts; builder.MakeCompound(cuts);
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 10; ++column) {
            const double x = 2 + 3 * column, y = 2 + 3 * row;
            builder.Add(cuts, BRepPrimAPI_MakeBox(gp_Pnt(x,y,-1),1,1,7).Shape());
            auto loop = NeutralRectangle("hole-" + std::to_string(row * 10 + column),x,y,1,1,false);
            data.Section.Boundaries.push_back(std::move(loop));
        }
    }
    BRepAlgoAPI_Cut native(BRepPrimAPI_MakeBox(34,22,5).Shape(),cuts);
    ASSERT_TRUE(native.IsDone());
    const auto source = native.Shape(); ASSERT_TRUE(BRepCheck_Analyzer(source).IsValid());
    const auto bytes = [&] { std::ostringstream stream; BRepTools::Write(source,stream); return stream.str(); };
    const auto before = bytes();
    T::SSolidNode node; node.ID = "base"; node.Data = data;
    T::CTubeNeutralGeometry expression; expression.BaseNodeID = expression.RootNodeID = node.ID;
    expression.SolidNodes.push_back(node);
    const auto matching = iCAX::OpenCascade::ValidateTubeCSGReplay(source,expression);
    ASSERT_TRUE(matching.bEquivalent);
    EXPECT_EQ(before,bytes());
    auto& edited = std::get<T::SExtrudedRegionNode>(expression.SolidNodes.front().Data);
    for (auto& curve : edited.Section.Boundaries[1].Curves) {
        auto& line = std::get<iCAX::GeometryData::Segment2>(curve.Segment.Curve);
        line.Start.X += 0.5; line.End.X += 0.5;
    }
    const auto shifted = iCAX::OpenCascade::ValidateTubeCSGReplay(source,expression);
    ASSERT_TRUE(shifted.bEvaluated); EXPECT_FALSE(shifted.bEquivalent);
    EXPECT_GT(shifted.MissingVolume,0); EXPECT_GT(shifted.UnexpectedVolume,0);
    EXPECT_EQ(before,bytes());
}

TEST(OpenCascadeTubeCSGConverterTest, PlanarRegionPreservesIslandsHolesAndDirectedFill)
{
    namespace T = iCAX::GeometryData::Tube;
    const auto outer = BRepPrimAPI_MakeBox(10,10,10).Shape();
    const auto hole = BRepPrimAPI_MakeBox(gp_Pnt(2,2,0),6,6,10).Shape();
    const auto island = BRepPrimAPI_MakeBox(gp_Pnt(3,3,0),4,4,10).Shape();
    const auto other = BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),10,10,10).Shape();
    const auto expected = BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(BRepAlgoAPI_Cut(outer,hole).Shape(),island).Shape(),other).Shape();
    for (const auto rule : {T::EPlanarFillRule::EvenOdd, T::EPlanarFillRule::NonZero}) {
        T::SExtrudedRegionNode data; data.First = 0; data.Last = 10; data.Section.FillRule = rule;
        data.Section.Boundaries = {NeutralRectangle("hole",2,2,6,6,rule == T::EPlanarFillRule::EvenOdd),
            NeutralRectangle("other",20,0,10,10), NeutralRectangle("island",3,3,4,4),
            NeutralRectangle("outer",0,0,10,10)};
        T::SSolidNode node; node.ID = "base"; node.Data = data;
        T::CTubeNeutralGeometry expression; expression.BaseNodeID = expression.RootNodeID = "base";
        expression.SolidNodes.push_back(node);
        const auto replay = iCAX::OpenCascade::ValidateTubeCSGReplay(expected, expression);
        EXPECT_TRUE(replay.bEquivalent);
        EXPECT_LT(replay.RelativeVolumeError, 1e-8);
    }
    T::SExtrudedRegionNode data; data.First = 0; data.Last = 10;
    data.Section.Boundaries = {NeutralRectangle("outer",0,0,10,10),NeutralRectangle("same-winding",2,2,6,6)};
    T::SSolidNode node; node.ID = "base"; node.Data = data;
    T::CTubeNeutralGeometry expression; expression.BaseNodeID = expression.RootNodeID = "base";
    expression.SolidNodes.push_back(node);
    EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(outer, expression).bEquivalent);
}

TEST(OpenCascadeTubeCSGConverterTest, DisconnectedWrappedCirclesOnPlanarWallsRemainExact)
{
    namespace G = iCAX::GeometryData; namespace T = G::Tube;
    T::SExtrudedRegionNode base; base.First = 0; base.Last = 200;
    base.Section.Boundaries = {NeutralRectangle("outer",0,0,100,60)};
    T::SSolidNode baseNode; baseNode.ID = "base"; baseNode.Data = base;
    T::SWrappedVolumeNode cut; cut.Support.ExtrusionNodeID = "base"; cut.Support.LoopID = "outer";
    cut.LowerNormalOffset = -8.0; cut.UpperNormalOffset = 0.0;
    for (const auto center : {G::Point2{25,100},G::Point2{210,120}}) {
        T::SRegionLoop2 loop; loop.ID = "circle-" + std::to_string(cut.UVRegion.Boundaries.size());
        T::SRegionCurve2 curve; curve.ID = loop.ID + "/curve";
        G::Circle2 circle; circle.Placement.Location = center; circle.Radius = 3;
        curve.Segment.Curve = circle; curve.Segment.Range.Last = 2 * std::acos(-1.0);
        loop.Curves.push_back(curve); cut.UVRegion.Boundaries.push_back(loop);
    }
    T::SSolidNode cutNode; cutNode.ID = "cut"; cutNode.Data = cut;
    T::SBooleanNode difference; difference.Operation = T::EBooleanOperation::Difference;
    difference.Children = {"base","cut"};
    T::SSolidNode root; root.ID = "root"; root.Data = difference;
    T::CTubeNeutralGeometry expression; expression.BaseNodeID = "base"; expression.RootNodeID = "root";
    expression.SolidNodes = {baseNode,cutNode,root};
    const auto lower = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25,0,100),gp_Dir(0,1,0)),3,8).Shape();
    const auto upper = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50,60,120),gp_Dir(0,-1,0)),3,8).Shape();
    const auto expected = BRepAlgoAPI_Cut(BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(100,60,200).Shape(),lower).Shape(),upper).Shape();
    const auto replay = iCAX::OpenCascade::ValidateTubeCSGReplay(expected, expression);
    EXPECT_TRUE(replay.bEquivalent);
    EXPECT_LT(replay.RelativeVolumeError, 1e-8);
}

TEST(OpenCascadeTubeCSGConverterTest, RejectsUnverifiedWrappedInterpretationWithoutLosingIndustryPart)
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    const auto path = root / "samples/structural-steel/recovery/report/solids/ship-beams__ic-B-1100.stp-db64888c/solid-30.step";
    ASSERT_TRUE(std::filesystem::exists(path));
    STEPControl_Reader reader; ASSERT_EQ(IFSelect_RetDone,reader.ReadFile(path.string().c_str()));
    ASSERT_GT(reader.TransferRoots(),0); const auto source = reader.OneShape();
    iCAX::OpenCascade::STubeCSGConversionOptions options;
    options.SourceBRepResourceID = "test://original-geometry";
    options.RemovalBRepResourceID = "test://unresolved-geometry";
    options.AdditionBRepResourceID = "test://additional-geometry";
    const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(source,options);
    std::string diagnostics;
    for (const auto& message : converted.ReplayValidation.Diagnostics) diagnostics += message + "; ";
    ASSERT_TRUE(converted.ReplayValidation.bEquivalent) << diagnostics;
    EXPECT_TRUE(std::any_of(converted.Geometry.Metadata.begin(),converted.Geometry.Metadata.end(),
        [](const auto& item) { return item.first.starts_with("construction.") && item.first.ends_with(".replayRejection"); }));
    EXPECT_FALSE(std::any_of(converted.Geometry.SolidNodes.begin(),converted.Geometry.SolidNodes.end(),
        [](const auto& node) { return std::holds_alternative<iCAX::GeometryData::Tube::SWrappedVolumeNode>(node.Data); }));
}

TEST(OpenCascadeTubeCSGConverterTest, IndustrySectionWiresAndCoincidentSeamsReplayExactly)
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    for (const auto& name : {
        "vendors-angle-channel__tekspan-60240-two-hole-angle.step-e419352c/solid-3.step",
        "ship-beams__ic-BO-225-Block-out-box-225-300.stp-0cf860ff/solid-5.step",
        "ship-beams__ic-SF-700-Column-unit.stp-a4bc055f/solid-1.step"})
    {
        SCOPED_TRACE(name);
        const auto path = root / "samples/structural-steel/recovery/report/solids" / name;
        ASSERT_TRUE(std::filesystem::exists(path));
        STEPControl_Reader reader;
        ASSERT_EQ(IFSelect_RetDone, reader.ReadFile(path.string().c_str()));
        ASSERT_GT(reader.TransferRoots(), 0);
        const auto source = reader.OneShape();
        iCAX::OpenCascade::STubeCSGConversionOptions options;
        options.SourceBRepResourceID = "test://source";
        options.RemovalBRepResourceID = "test://removal";
        options.AdditionBRepResourceID = "test://addition";
        const auto result = iCAX::OpenCascade::ConvertBRepToTubeCSG(source, options);
        std::string diagnostics;
        for (const auto& message : result.ReplayValidation.Diagnostics) diagnostics += message + "; ";
        ASSERT_TRUE(result.ReplayValidation.bEquivalent) << diagnostics;
        EXPECT_LE(result.ReplayValidation.RelativeVolumeError, 1.0e-8);
        iCAX::OpenCascade::STubeCSGEvaluationOptions replay;
        replay.ExternalBRepShapes.emplace(options.SourceBRepResourceID, source);
        if (!result.RemovalShape.IsNull()) replay.ExternalBRepShapes.emplace(options.RemovalBRepResourceID, result.RemovalShape);
        if (!result.AdditionShape.IsNull()) replay.ExternalBRepShapes.emplace(options.AdditionBRepResourceID, result.AdditionShape);
        const auto rebuilt = iCAX::OpenCascade::EvaluateTubeCSG(result.Geometry, replay);
        ASSERT_TRUE(rebuilt.bOK);
        ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
    }
}

TEST(OpenCascadeTubeCSGConverterTest, ShiftedPeriodicIndustryEdgesRemainExactAndSourceIsImmutable)
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    for (const auto* name : {"solid-13.step", "solid-15.step"}) {
        SCOPED_TRACE(name);
        const auto path = root / "samples/structural-steel/recovery/report/solids/ship-beams__ic-SF-225-Column-unit.stp-af56533c" / name;
        STEPControl_Reader reader;
        ASSERT_EQ(IFSelect_RetDone, reader.ReadFile(path.string().c_str()));
        ASSERT_GT(reader.TransferRoots(), 0);
        const auto source = reader.OneShape();
        ASSERT_TRUE(BRepCheck_Analyzer(source, true, false, true).IsValid());
        std::stringstream before, after;
        BRepTools::Write(source, before);
        const auto result = iCAX::OpenCascade::ConvertBRepToTubeCSG(source);
        ASSERT_TRUE(result.ReplayValidation.bEquivalent);
        EXPECT_LE(result.ReplayValidation.RelativeVolumeError, 1e-8);
        const auto evaluated = iCAX::OpenCascade::EvaluateTubeCSG(result.Geometry);
        ASSERT_TRUE(evaluated.bOK);
        ASSERT_TRUE(BRepCheck_Analyzer(evaluated.Shape).IsValid());
        GProp_GProps a,b;
        BRepGProp::VolumeProperties(source,a);
        BRepGProp::VolumeProperties(evaluated.Shape,b);
        EXPECT_NEAR(a.Mass(),b.Mass(),std::abs(a.Mass())*1e-8);
        BRepTools::Write(source, after);
        EXPECT_EQ(before.str(),after.str());
    }
}

TEST(OpenCascadeTubeCSGConverterTest, ShortTurnedIndustryPartsKeepTheirCoaxialAxis)
{
    namespace T = iCAX::GeometryData::Tube;
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    for (const auto* name : {
        "ship-beams__ic-BO-700-Block-out-box.stp-df346a92/solid-10.step",
        "ship-beams__ic-BO-225-Block-out-box-225-300.stp-0cf860ff/solid-2.step",
        "ship-beams__ic-BO-225-Block-out-box-225-300.stp-0cf860ff/solid-3.step"})
    {
        SCOPED_TRACE(name);
        const auto path = root / "samples/structural-steel/recovery/report/solids" / name;
        STEPControl_Reader reader;
        ASSERT_EQ(IFSelect_RetDone, reader.ReadFile(path.string().c_str()));
        ASSERT_GT(reader.TransferRoots(), 0);
        const auto source = reader.OneShape();
        std::stringstream before, after;
        BRepTools::Write(source, before);
        iCAX::OpenCascade::STubeCSGConversionOptions options;
        options.SourceBRepResourceID = "test://source";
        options.RemovalBRepResourceID = "test://removal";
        options.AdditionBRepResourceID = "test://addition";
        const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(source, options);
        ASSERT_TRUE(converted.ReplayValidation.bEquivalent);
        const auto base = std::find_if(converted.Geometry.SolidNodes.begin(),
            converted.Geometry.SolidNodes.end(), [&](const auto& node) {
                return node.ID == converted.Geometry.BaseNodeID;
            });
        ASSERT_NE(converted.Geometry.SolidNodes.end(), base);
        const auto* extrusion = std::get_if<T::SExtrudedRegionNode>(&base->Data);
        ASSERT_NE(nullptr, extrusion);
        std::optional<gp_Dir> sourceAxis;
        double outerRadius = 0.0;
        for (TopExp_Explorer face(source, TopAbs_FACE); face.More(); face.Next()) {
            BRepAdaptor_Surface surface(TopoDS::Face(face.Current()));
            if (surface.GetType() == GeomAbs_Cylinder && surface.Cylinder().Radius() > outerRadius) {
                outerRadius = surface.Cylinder().Radius();
                sourceAxis = surface.Cylinder().Axis().Direction();
            }
        }
        ASSERT_TRUE(sourceAxis);
        const auto& direction = extrusion->Frame.ZDirection;
        EXPECT_NEAR(1.0, std::abs(sourceAxis->Dot(gp_Dir(direction.X, direction.Y, direction.Z))), 1e-10);
        iCAX::OpenCascade::STubeCSGEvaluationOptions replay;
        replay.ExternalBRepShapes.emplace(options.SourceBRepResourceID, source);
        if (!converted.RemovalShape.IsNull()) replay.ExternalBRepShapes.emplace(options.RemovalBRepResourceID, converted.RemovalShape);
        if (!converted.AdditionShape.IsNull()) replay.ExternalBRepShapes.emplace(options.AdditionBRepResourceID, converted.AdditionShape);
        const auto rebuilt = iCAX::OpenCascade::EvaluateTubeCSG(converted.Geometry, replay);
        ASSERT_TRUE(rebuilt.bOK);
        ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
        EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(source, converted.Geometry, replay).bEquivalent);
        BRepTools::Write(source, after);
        EXPECT_EQ(before.str(), after.str());
    }
}

TEST(OpenCascadeTubeCSGConverterTest, OriginalAssemblyCutsRequireCompleteCompositionProof)
{
    namespace G = iCAX::GeometryData;
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    for (const auto& [name, index] : std::vector<std::pair<std::string, int>>{
        {"ic-BO-225-Block-out-box-225-300.stp", 2}, {"ic-BO-225-Block-out-box-225-300.stp", 3},
        {"ic-BO-450-Block-out-box.stp", 2}, {"ic-BO-450-Block-out-box.stp", 3},
        {"ic-BO-700-Block-out-box.stp", 10}}) {
        SCOPED_TRACE(name + " solid " + std::to_string(index));
        const auto read = iCAX::OpenCascade::ReadBRepFile((root / "samples/structural-steel/ship-beams" / name).string());
        ASSERT_TRUE(read.bOK);
        const auto assembly = iCAX::OpenCascade::BuildOpenCascadeShape(
            G::Persistence::Deserialize(G::Persistence::Serialize(read.Geometry)));
        ASSERT_TRUE(assembly.bOK);
        TopoDS_Shape selected; int ordinal = 0;
        for (TopExp_Explorer it(assembly.Shape, TopAbs_SOLID); it.More(); it.Next())
            if (++ordinal == index) { selected = it.Current(); break; }
        ASSERT_FALSE(selected.IsNull());
        const auto part = G::Persistence::Deserialize(G::Persistence::Serialize(
            iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(selected, "", "", 0.025)));
        const auto built = iCAX::OpenCascade::BuildOpenCascadeShape(part);
        ASSERT_TRUE(built.bOK);
        const auto source = built.Shape;
        std::stringstream before, after; BRepTools::Write(source, before);
        iCAX::OpenCascade::STubeCSGConversionOptions options;
        options.SourceBRepResourceID = "test://source";
        options.RemovalBRepResourceID = "test://removal";
        options.AdditionBRepResourceID = "test://addition";
        const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(source, options);
        ASSERT_TRUE(converted.ReplayValidation.bEquivalent);
        iCAX::OpenCascade::STubeCSGEvaluationOptions replay;
        replay.ExternalBRepShapes.emplace(options.SourceBRepResourceID, source);
        if (!converted.RemovalShape.IsNull()) replay.ExternalBRepShapes.emplace(options.RemovalBRepResourceID, converted.RemovalShape);
        if (!converted.AdditionShape.IsNull()) replay.ExternalBRepShapes.emplace(options.AdditionBRepResourceID, converted.AdditionShape);
        const auto rebuilt = iCAX::OpenCascade::EvaluateTubeCSG(converted.Geometry, replay);
        ASSERT_TRUE(rebuilt.bOK);
        ASSERT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
        EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(source, converted.Geometry, replay).bEquivalent);
        BRepTools::Write(source, after); EXPECT_EQ(before.str(), after.str());
    }
}

TEST(OpenCascadeTubeCSGConverterTest, DisconnectedSectionIslandsRemainMaterial)
{
    namespace T = iCAX::GeometryData::Tube;
    auto source = BRepPrimAPI_MakeBox(4, 10, 100).Shape();
    for (const auto& member : {
        BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 4, 10, 100).Shape(),
        BRepPrimAPI_MakeBox(24, 10, 2).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 98), 24, 10, 2).Shape()}) {
        BRepAlgoAPI_Fuse fuse(source, member);
        ASSERT_TRUE(fuse.IsDone()); source = fuse.Shape();
    }
    ASSERT_TRUE(BRepCheck_Analyzer(source).IsValid());
    std::stringstream before, after; BRepTools::Write(source, before);
    iCAX::OpenCascade::STubeCSGConversionOptions options;
    options.SourceBRepResourceID = "test://source";
    options.RemovalBRepResourceID = "test://removal";
    options.AdditionBRepResourceID = "test://addition";
    const auto converted = iCAX::OpenCascade::ConvertBRepToTubeCSG(source, options);
    ASSERT_TRUE(converted.ReplayValidation.bEquivalent);
    const auto base = std::find_if(converted.Geometry.SolidNodes.begin(), converted.Geometry.SolidNodes.end(),
        [&](const auto& node) { return node.ID == converted.Geometry.BaseNodeID; });
    ASSERT_NE(converted.Geometry.SolidNodes.end(), base);
    const auto* extrusion = std::get_if<T::SExtrudedRegionNode>(&base->Data);
    ASSERT_NE(nullptr, extrusion);
    ASSERT_EQ(2u, extrusion->Section.Boundaries.size());
    ASSERT_EQ(2u, extrusion->SectionPrimitives.size());
    for (const auto& primitive : extrusion->SectionPrimitives)
        EXPECT_EQ(T::ESectionPrimitiveRole::OuterBoundary, primitive.Role);
    iCAX::OpenCascade::STubeCSGEvaluationOptions replay;
    replay.ExternalBRepShapes.emplace(options.SourceBRepResourceID, source);
    if (!converted.RemovalShape.IsNull()) replay.ExternalBRepShapes.emplace(options.RemovalBRepResourceID, converted.RemovalShape);
    if (!converted.AdditionShape.IsNull()) replay.ExternalBRepShapes.emplace(options.AdditionBRepResourceID, converted.AdditionShape);
    EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(source, converted.Geometry, replay).bEquivalent);
    BRepTools::Write(source, after); EXPECT_EQ(before.str(), after.str());
}

TEST(OpenCascadeTubeCSGConverterTest, RejectsAnOpenSolidBeforeRecognition)
{
    const auto box = BRepPrimAPI_MakeBox(20,30,40).Shape();
    TopExp_Explorer faces(box,TopAbs_FACE);
    TopoDS_Shell shell; BRep_Builder builder; builder.MakeShell(shell);
    faces.Next();
    for (;faces.More();faces.Next()) builder.Add(shell,faces.Current());
    const auto openSolid = BRepBuilderAPI_MakeSolid(shell).Solid();
    ASSERT_FALSE(BRepCheck_Analyzer(openSolid).IsValid());
    const auto result = iCAX::OpenCascade::ConvertBRepToTubeCSG(openSolid);
    EXPECT_FALSE(result.ReplayValidation.bEquivalent);
    EXPECT_TRUE(result.Geometry.SolidNodes.empty());
    ASSERT_FALSE(result.ReplayValidation.Diagnostics.empty());
    EXPECT_EQ("Source BRep has an invalid boundary", result.ReplayValidation.Diagnostics.front());
}

TEST(OpenCascadeTubeCSGConverterTest, StockClippingRetainsCutIdentityForMachining)
{
    namespace T = iCAX::GeometryData::Tube;
    namespace M = iCAX::CAM::Intent::Tube;
    T::CTubeNeutralGeometry geometry;
    geometry.BaseNodeID = "stock-with-custom-id";
    geometry.RootNodeID = "finished";
    T::SSolidNode stock; stock.ID = geometry.BaseNodeID; stock.Data = T::SExtrudedRegionNode{};
    T::SSolidNode cut; cut.ID = "editable-hole"; cut.Data = T::SExtrudedRegionNode{};
    T::SSolidNode clipped; clipped.ID = "clipped-hole";
    clipped.Data = T::SBooleanNode{T::EBooleanOperation::Intersection, {stock.ID, cut.ID}};
    T::SSolidNode root; root.ID = geometry.RootNodeID;
    root.Data = T::SBooleanNode{T::EBooleanOperation::Difference, {stock.ID, clipped.ID}};
    geometry.SolidNodes = {stock, cut, clipped, root};
    const auto intent = M::CompileTubeMachiningIntent(geometry);
    const auto feature = std::find_if(intent.Features.begin(), intent.Features.end(),
        [&](const auto& value) { return value.ID == "cut/" + cut.ID; });
    ASSERT_NE(intent.Features.end(), feature);
    const auto* extrusion = std::get_if<M::SExtrudedCutIntent>(&feature->Data);
    ASSERT_NE(nullptr, extrusion);
    EXPECT_EQ(cut.ID, extrusion->RemovalNodeID);
    EXPECT_TRUE(std::none_of(intent.Features.begin(), intent.Features.end(),
        [&](const auto& value) { return value.ID == "cut/" + clipped.ID; }));
}

TEST(OpenCascadeTubeCSGConverterTest, MaterialWrappersRetainCutIdentityWithoutClaimingUnsupportedMachining)
{
    namespace T = iCAX::GeometryData::Tube;
    namespace M = iCAX::CAM::Intent::Tube;
    T::CTubeNeutralGeometry geometry;
    geometry.BaseNodeID = "stock";
    geometry.RootNodeID = "selected-piece";
    T::SSolidNode stock; stock.ID = geometry.BaseNodeID;
    T::SExtrudedRegionNode extrusion; extrusion.Last = 100;
    extrusion.SideAtlases.push_back({"stock-side", "outer", T::ESideAtlasSeamRule::MinimumXThenY, 200, 0, 100});
    stock.Data = extrusion;
    T::SSolidNode cut; cut.ID = "ordinary-hole";
    T::SWrappedVolumeNode opening; opening.Support = {stock.ID, "outer", ""};
    T::SRegionLoop2 loop; loop.ID = "opening-boundary";
    const std::vector<iCAX::GeometryData::Point2> points{{20, 30}, {30, 30}, {30, 40}, {20, 40}};
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        iCAX::GeometryData::CurveSegment2 edge;
        edge.Curve = iCAX::GeometryData::Segment2{points[index], points[(index + 1) % points.size()]};
        edge.Range = {0, 1, false};
        loop.Curves.push_back({"opening-edge-" + std::to_string(index), edge});
    }
    opening.UVRegion.Boundaries.push_back(loop);
    opening.LowerNormalOffset = -3.0; opening.UpperNormalOffset = 0.2;
    cut.Data = opening;
    T::SSolidNode material; material.ID = "material-before-selection";
    material.Data = T::SBooleanNode{T::EBooleanOperation::Difference, {stock.ID, cut.ID}};
    T::SSolidNode selected; selected.ID = geometry.RootNodeID;
    selected.Data = T::SConnectedComponentNode{material.ID, {1, 2, 3}};
    geometry.SolidNodes = {stock, cut, material, selected};
    const auto assertHole = [&](const M::CTubeMachiningIntent& intent) {
        const auto feature = std::find_if(intent.Features.begin(), intent.Features.end(),
            [&](const auto& value) { return value.ID == "cut/" + cut.ID; });
        ASSERT_NE(intent.Features.end(), feature);
        const auto* wrapped = std::get_if<M::SWrappedCutIntent>(&feature->Data);
        ASSERT_NE(nullptr, wrapped);
        EXPECT_EQ(cut.ID, wrapped->RemovalNodeID);
        ASSERT_EQ(1u, feature->BoundaryTraces.size());
        const auto& trace = feature->BoundaryTraces.front();
        EXPECT_EQ(stock.ID, trace.Support.ExtrusionNodeID);
        EXPECT_EQ("outer", trace.Support.LoopID);
        EXPECT_TRUE(trace.UVPath.Closed);
        ASSERT_EQ(4u, trace.UVPath.Segments.size());
        for (const auto& edge : trace.UVPath.Segments) EXPECT_TRUE(edge.Range.Reversed);
    };
    const auto selectedIntent = M::CompileTubeMachiningIntent(geometry);
    assertHole(selectedIntent);
    EXPECT_EQ(M::ECompilationStatus::RequiresGeometricIntersection, selectedIntent.Status);
    EXPECT_TRUE(std::any_of(selectedIntent.Diagnostics.begin(), selectedIntent.Diagnostics.end(),
        [&](const auto& diagnostic) {
            return diagnostic.Code == "connected-component-requires-intersection"
                && diagnostic.SourceID == selected.ID;
        }));

    T::SSolidNode bevel; bevel.ID = "topology-bevel";
    T::STopologyBevelNode modifier; modifier.Child = selected.ID;
    T::STopologyBevelSelection boundary; boundary.EdgeID = 1; boundary.ReferenceFaceID = 2;
    boundary.AngleDegrees = 30; modifier.Selections.push_back(boundary);
    bevel.Data = modifier; geometry.SolidNodes.push_back(bevel); geometry.RootNodeID = bevel.ID;
    const auto bevelIntent = M::CompileTubeMachiningIntent(geometry);
    assertHole(bevelIntent);
    EXPECT_EQ(M::ECompilationStatus::Invalid, bevelIntent.Status);
    EXPECT_TRUE(std::any_of(bevelIntent.Diagnostics.begin(), bevelIntent.Diagnostics.end(),
        [&](const auto& diagnostic) {
            return diagnostic.Code == "topology-bevel-machining-unsupported"
                && diagnostic.SourceID == bevel.ID;
        }));

    std::get<T::STopologyBevelNode>(geometry.SolidNodes.back().Data).Child = "missing-material";
    const auto missingChildIntent = M::CompileTubeMachiningIntent(geometry);
    EXPECT_EQ(M::ECompilationStatus::Invalid, missingChildIntent.Status);
    EXPECT_TRUE(std::any_of(missingChildIntent.Diagnostics.begin(), missingChildIntent.Diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.Code == "material-wrapper-child-invalid"; }));
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
    const auto _SourceBytes = [&] {
        std::ostringstream stream; BRepTools::Write(_NurbsPart, stream); return stream.str();
    };
    const auto _Before = _SourceBytes();

    iCAX::OpenCascade::STubeCSGConversionOptions _Options;
    _Options.FeatureWorkerCount = 1;
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
            const auto* nurbs = std::get_if<iCAX::GeometryData::NURBS2>(&Curve_.Segment.Curve);
            const auto* spline = std::get_if<iCAX::GeometryData::BSpline2>(&Curve_.Segment.Curve);
            // The editable section keeps the exact curved iso-curve, not the
            // degree-one walking polygon produced by a generic intersection.
            return (nurbs && nurbs->Degree > 1) || (spline && spline->Degree > 1);
        }));
    EXPECT_TRUE(_Result.RemovalShape.IsNull());
    std::string diagnostics;
    for (const auto& message : _Result.ReplayValidation.Diagnostics) diagnostics += message + "\n";
    EXPECT_LT(_Result.Geometry.RelativeVolumeError, 1.0e-5) << diagnostics;
    _Options.FeatureWorkerCount = 4;
    const auto _Parallel = iCAX::OpenCascade::ConvertBRepToTubeCSG(_NurbsPart, _Options);
    ASSERT_TRUE(_Parallel.ReplayValidation.bEquivalent);
    auto _SerialGeometry = _Result.Geometry;
    auto _ParallelGeometry = _Parallel.Geometry;
    const auto _Timing = [](const auto& item) { return item.first.starts_with("elapsedMs."); };
    std::erase_if(_SerialGeometry.Metadata, _Timing);
    std::erase_if(_ParallelGeometry.Metadata, _Timing);
    EXPECT_EQ(iCAX::GeometryData::Tube::Persistence::Serialize(_SerialGeometry),
        iCAX::GeometryData::Tube::Persistence::Serialize(_ParallelGeometry));
    EXPECT_EQ(_Before, _SourceBytes());
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
    ASSERT_TRUE(_ExplicitRoot->Relations.has_value());
    EXPECT_FALSE(_ExplicitRoot->Relations->Extent.has_value());
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

    // Through-cut overrun is an extent change, not a displacement of the
    // opening profile in its source construction frame.
    auto _ProfileOnly = _Result.Geometry;
    _ProfileOnly.RootNodeID = _BevelIter->ID;
    iCAX::OpenCascade::STubeCSGEvaluationOptions _ProfileOptions;
    _ProfileOptions.EvaluateAllConstructionBodies = false;
    const auto _OriginalProfile = iCAX::OpenCascade::EvaluateTubeCSG(_ProfileOnly, _ProfileOptions);
    ASSERT_TRUE(_OriginalProfile.bOK);
    const auto _SourceNode = std::find_if(_ProfileOnly.SolidNodes.begin(), _ProfileOnly.SolidNodes.end(),
        [&](const auto& Node_) { return Node_.ID == _Bevel.SourceCutNodeID; });
    ASSERT_NE(_SourceNode, _ProfileOnly.SolidNodes.end());
    auto& _SourceExtrusion = std::get<iCAX::GeometryData::Tube::SExtrudedRegionNode>(_SourceNode->Data);
    _SourceExtrusion.First -= 3.0;
    _SourceExtrusion.Last += 3.0;
    const auto _PaddingCheck = iCAX::OpenCascade::ValidateTubeCSGReplay(
        _OriginalProfile.Shape, _ProfileOnly, _ProfileOptions);
    EXPECT_TRUE(_PaddingCheck.bEquivalent);
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
    EXPECT_TRUE(_Result.ReplayValidation.bEvaluated);
    EXPECT_TRUE(_Result.ReplayValidation.bEquivalent);
    EXPECT_LE(_Result.Geometry.RelativeVolumeError, 1.0e-8);
    EXPECT_NE(iCAX::GeometryData::Tube::ERecognitionStatus::Failed, _Result.Geometry.RecognitionStatus);
    EXPECT_TRUE(iCAX::OpenCascade::ValidateTubeCSGReplay(_FinalCut.Shape(), _Result.Geometry).bEquivalent);
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
    ASSERT_TRUE(_GroupingAlternative->Relations.has_value());
    EXPECT_FALSE(_GroupingAlternative->Relations->Extent.has_value());
    EXPECT_FALSE(_GroupingAlternative->Relations->Placement.has_value());
    EXPECT_TRUE(_GroupingAlternative->Relations->MaterialSpans.empty());
    ASSERT_TRUE(_GroupingAlternative->Relations->MaterialEffect.has_value());
    EXPECT_EQ(2u, _GroupingAlternative->Relations->MaterialEffect->SourceNodeIDs.size());
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

    ASSERT_TRUE(_Result.ReplayValidation.bEquivalent)
        << (_Result.ReplayValidation.Diagnostics.empty()
            ? "" : _Result.ReplayValidation.Diagnostics.front());
    ASSERT_FALSE(_Result.RemovalShape.IsNull());
    auto _MissingResourceGeometry = _Result.Geometry;
    auto _MutableComposite = std::find_if(
        _MissingResourceGeometry.SolidNodes.begin(),
        _MissingResourceGeometry.SolidNodes.end(),
        [](IN const auto& Node_) {
            return std::holds_alternative<iCAX::GeometryData::Tube::SCompositeVolumeNode>(
                Node_.Data);
        });
    ASSERT_NE(_MissingResourceGeometry.SolidNodes.end(), _MutableComposite);
    std::get<iCAX::GeometryData::Tube::SCompositeVolumeNode>(
        _MutableComposite->Data).BRepResourceID.clear();
    iCAX::OpenCascade::STubeCSGEvaluationOptions _ReplayOptions;
    _ReplayOptions.ExternalBRepShapes.emplace(
        _Options.RemovalBRepResourceID, _Result.RemovalShape);
    const auto _MissingResourceReplay = iCAX::OpenCascade::ValidateTubeCSGReplay(
        _Cut.Shape(), _MissingResourceGeometry, _ReplayOptions);
    EXPECT_FALSE(_MissingResourceReplay.bEquivalent);
    EXPECT_FALSE(_MissingResourceReplay.bEvaluated);
    EXPECT_FALSE(_MissingResourceReplay.Diagnostics.empty());

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
        / ("tube-geometry-preview-" +
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
        "tube-geometry-test",
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
