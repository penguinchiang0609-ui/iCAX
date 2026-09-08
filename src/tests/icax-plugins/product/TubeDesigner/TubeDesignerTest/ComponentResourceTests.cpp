#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <Task/Task.h>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    using iCAX::OpenCascade::EvaluateNeutralModel;

    std::string ResourceBRep(const TopoDS_Shape& Shape_,
        TopTools_FormatVersion Version_ = TopTools_FormatVersion_CURRENT)
    {
        std::ostringstream _Stream;
        BRepTools::Write(Shape_, _Stream, false, false, Version_);
        return _Stream.str();
    }

    SNeutralModel ResourceModel(std::string BRep_)
    {
        SNeutralModel _Model;
        _Model.Geometry.push_back({ "component.prototype", EGeometryOperator::Resource,
            {}, { { "brep", std::move(BRep_) } } });
        return _Model;
    }

    double ResourceVolume(const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return _Properties.Mass();
    }

    std::array<double, 6> ResourceBounds(const TopoDS_Shape& Shape_)
    {
        Bnd_Box _Bounds;
        BRepBndLib::Add(Shape_, _Bounds, false);
        std::array<double, 6> _Result;
        _Bounds.Get(_Result[0], _Result[1], _Result[2], _Result[3], _Result[4], _Result[5]);
        return _Result;
    }

    void ExpectResourceBounds(const TopoDS_Shape& Shape_, const std::array<double, 6>& Expected_)
    {
        const auto _Bounds = ResourceBounds(Shape_);
        for (std::size_t _Index = 0; _Index < _Bounds.size(); ++_Index)
            EXPECT_NEAR(Expected_[_Index], _Bounds[_Index], 1e-6) << _Index;
    }

    ObjectMap ResourcePlacement(double X_, double Y_, double Z_, bool Rotate_ = false)
    {
        return {
            { "placement", ObjectMap{
                { "origin", VariantArray{ X_, Y_, Z_ } },
                { "xAxis", Rotate_ ? VariantArray{ 0.0, 1.0, 0.0 } : VariantArray{ 1.0, 0.0, 0.0 } },
                { "yAxis", Rotate_ ? VariantArray{ -1.0, 0.0, 0.0 } : VariantArray{ 0.0, 1.0, 0.0 } },
                { "zAxis", VariantArray{ 0.0, 0.0, 1.0 } }
            } }
        };
    }
}

TEST(ComponentResource, CodecPreservesUnresolvedReferencesWithoutReadingPaths)
{
    auto _Model = CTemplateCodec::ParseNeutralModel(CStandardJsonCodec::Parse(R"json({
        "schema":"icax.neutral-model", "schemaVersion":1,
        "template":{"id":"test.components","version":"1.0.0"},
        "geometry":[{"key":"component.prototype","operator":"resource",
            "arguments":{"reference":"template:post-cap"}}],
        "items":[{"key":"cap","displayName":"柱帽",
            "representations":{"display":"component.prototype"}}],
        "outputs":[{"key":"display.default","purpose":"display","items":["cap"]}]
    })json"));
    ASSERT_EQ(1u, _Model.Geometry.size());
    EXPECT_EQ(EGeometryOperator::Resource, _Model.Geometry.front().Operator);
    EXPECT_EQ("template:post-cap", _Model.Geometry.front().Arguments.at("reference").To<std::string>());
    for (const auto& _Reference : { "template:post-cap", "system:cap", "library:cap",
            "C:/not-a-resource.step", "https://example.invalid/part.step" })
    {
        SCOPED_TRACE(_Reference);
        _Model.Geometry.front().Arguments["reference"] = std::string(_Reference);
        try
        {
            (void)EvaluateNeutralModel(_Model);
            FAIL() << "unresolved references must not be evaluated";
        }
        catch (const std::invalid_argument& Error_)
        {
            EXPECT_NE(std::string(Error_.what()).find("requires resolved BRepTools text"), std::string::npos);
        }
    }
    _Model.Geometry.front().Arguments["brep"] = ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    EXPECT_NEAR(6000.0, ResourceVolume(EvaluateNeutralModel(_Model).At("component.prototype")), 1e-6);
}

TEST(ComponentResource, RoundTripsSolidAndCompoundThroughSupportedAsciiVersions)
{
    const auto _Box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    for (const auto _Version : { TopTools_FormatVersion_VERSION_1,
            TopTools_FormatVersion_VERSION_2, TopTools_FormatVersion_VERSION_3 })
    {
        SCOPED_TRACE(static_cast<int>(_Version));
        const auto _Result = EvaluateNeutralModel(ResourceModel(ResourceBRep(_Box, _Version)));
        const auto& _Shape = _Result.At("component.prototype");
        EXPECT_EQ(TopAbs_SOLID, _Shape.ShapeType());
        EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
        EXPECT_NEAR(6000.0, ResourceVolume(_Shape), 1e-6);
        ExpectResourceBounds(_Shape, { 0, 0, 0, 10, 20, 30 });
    }
    gp_Trsf _Translation;
    _Translation.SetTranslation(gp_Vec(100, 0, 0));
    TopoDS_Compound _Compound;
    BRep_Builder _Builder;
    _Builder.MakeCompound(_Compound);
    _Builder.Add(_Compound, _Box);
    _Builder.Add(_Compound, _Box.Moved(TopLoc_Location(_Translation)));
    const auto _Result = EvaluateNeutralModel(ResourceModel(ResourceBRep(_Compound)));
    const auto& _Shape = _Result.At("component.prototype");
    EXPECT_EQ(TopAbs_COMPOUND, _Shape.ShapeType());
    EXPECT_NEAR(12000.0, ResourceVolume(_Shape), 1e-6);
    ExpectResourceBounds(_Shape, { 0, 0, 0, 110, 20, 30 });
    std::size_t _Solids = 0;
    for (TopExp_Explorer _Solid(_Shape, TopAbs_SOLID); _Solid.More(); _Solid.Next()) ++_Solids;
    EXPECT_EQ(2u, _Solids);
}

TEST(ComponentResource, RepeatedTransformsShareAnUnmodifiedPrototype)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    _Model.Geometry.push_back({ "cap.first", EGeometryOperator::Transform,
        { "component.prototype" }, ResourcePlacement(100, 0, 0) });
    _Model.Geometry.push_back({ "cap.second", EGeometryOperator::Transform,
        { "component.prototype" }, ResourcePlacement(0, 200, 0, true) });
    _Model.Geometry.push_back({ "assembly", EGeometryOperator::Compound,
        { "cap.first", "cap.second" }, {} });
    const auto _Result = EvaluateNeutralModel(_Model, { "assembly", "cap.first", "assembly" });
    ASSERT_EQ(4u, _Result.Geometry.size());
    const auto& _Prototype = _Result.At("component.prototype");
    const auto& _First = _Result.At("cap.first");
    const auto& _Second = _Result.At("cap.second");
    EXPECT_TRUE(_Prototype.IsPartner(_First));
    EXPECT_TRUE(_Prototype.IsPartner(_Second));
    EXPECT_FALSE(_First.IsSame(_Second));
    EXPECT_TRUE(_Prototype.Location().IsIdentity());
    ExpectResourceBounds(_Prototype, { 0, 0, 0, 10, 20, 30 });
    ExpectResourceBounds(_First, { 100, 0, 0, 110, 20, 30 });
    ExpectResourceBounds(_Second, { -20, 200, 0, 0, 210, 30 });
    EXPECT_NEAR(6000.0, ResourceVolume(_Prototype), 1e-6);
    EXPECT_NEAR(12000.0, ResourceVolume(_Result.At("assembly")), 1e-6);
}

TEST(ComponentResource, RejectsInvalidMissingBinaryTruncatedAndOversizedBRep)
{
    const auto _Valid = ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    for (const auto& _Text : std::vector<std::string>{ "", " \r\n\t", "C:/part.brep",
            "https://example.invalid/part.brep", "not BRep", "CASCADE Topology V3, broken",
            _Valid.substr(0, _Valid.size() / 2), _Valid + "trailing data", _Valid + std::string(1, '\0') })
    {
        SCOPED_TRACE(_Text.substr(0, 48));
        EXPECT_THROW(EvaluateNeutralModel(ResourceModel(_Text)), std::invalid_argument);
    }
    auto _Model = ResourceModel(_Valid);
    _Model.Geometry.front().Arguments.erase("brep");
    EXPECT_THROW(EvaluateNeutralModel(_Model), std::invalid_argument);
    _Model.Geometry.front().Arguments["brep"] = 42;
    EXPECT_THROW(EvaluateNeutralModel(_Model), std::invalid_argument);
    _Model.Geometry.front().Arguments["brep"] = _Valid;
    _Model.Geometry.front().Inputs.push_back("component.prototype");
    EXPECT_THROW(EvaluateNeutralModel(_Model), std::invalid_argument);
    auto _Oversized = _Valid;
    _Oversized.resize(kMaximumResourceBRepBytes + 1, ' ');
    EXPECT_THROW(EvaluateNeutralModel(ResourceModel(std::move(_Oversized))), std::invalid_argument);
}

TEST(ComponentResource, AcceptsTheExact32MiBBoundary)
{
    auto _Text = ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    _Text.resize(kMaximumResourceBRepBytes, ' ');
    const auto _Result = EvaluateNeutralModel(ResourceModel(std::move(_Text)));
    EXPECT_NEAR(6000.0, ResourceVolume(_Result.At("component.prototype")), 1e-6);
}

TEST(ComponentResource, RejectsEmptyInvalidAndNonSolidTopology)
{
    const auto _Box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const auto _Edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Shape();
    TopExp_Explorer _Face(_Box, TopAbs_FACE);
    ASSERT_TRUE(_Face.More());
    BRep_Builder _Builder;
    TopoDS_Compound _Empty;
    _Builder.MakeCompound(_Empty);
    TopoDS_Compound _Mixed;
    _Builder.MakeCompound(_Mixed);
    _Builder.Add(_Mixed, _Box);
    _Builder.Add(_Mixed, _Edge);
    TopoDS_Solid _EmptySolid;
    _Builder.MakeSolid(_EmptySolid);
    for (const auto& _Shape : std::vector<TopoDS_Shape>{ _Empty, _EmptySolid,
            _Face.Current(), _Edge, _Mixed, _Box.Reversed() })
    {
        SCOPED_TRACE(static_cast<int>(_Shape.ShapeType()));
        EXPECT_THROW(EvaluateNeutralModel(ResourceModel(ResourceBRep(_Shape))), std::invalid_argument);
    }
}

TEST(ComponentResource, OnlySelectedRootsResolveResourcePayloads)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    _Model.Geometry.push_back({ "unused.unresolved", EGeometryOperator::Resource,
        {}, { { "reference", std::string("template:missing-resource") } } });
    _Model.Geometry.push_back({ "unused.malformed", EGeometryOperator::Resource,
        {}, { { "brep", std::string("invalid BRep") } } });
    const auto _None = EvaluateNeutralModel(_Model, std::vector<std::string>{});
    EXPECT_TRUE(_None.Geometry.empty());
    const auto _Selected = EvaluateNeutralModel(_Model, { "component.prototype", "component.prototype" });
    ASSERT_EQ(1u, _Selected.Geometry.size());
    EXPECT_NEAR(6000.0, ResourceVolume(_Selected.At("component.prototype")), 1e-6);
    EXPECT_THROW(EvaluateNeutralModel(_Model, { "unused.unresolved" }), std::invalid_argument);
    EXPECT_THROW(EvaluateNeutralModel(_Model, { "unused.malformed" }), std::invalid_argument);
    EXPECT_THROW(EvaluateNeutralModel(_Model), std::invalid_argument);
}

TEST(ComponentResource, ParallelBooleansPreserveSharedInputsAndMatchSerial)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    _Model.Geometry.push_back({ "tool", EGeometryOperator::Transform,
        { "component.prototype" }, ResourcePlacement(5, 0, 0) });
    std::vector<std::string> _Roots;
    for (int _Index = 0; _Index < 17; ++_Index)
    {
        const auto _Key = "cut." + std::to_string(_Index);
        const std::string _Operation = _Index % 3 == 0 ? "union" : _Index % 3 == 1 ? "intersect" : "subtract";
        _Model.Geometry.push_back({ _Key, EGeometryOperator::Boolean, { "ignored.missing" },
            { { "operation", _Operation },
              { "target", std::string("component.prototype") },
              { "tools", VariantArray{ std::string("tool") } } } });
        _Roots.push_back(_Key);
    }
    const auto _Serial = EvaluateNeutralModel(_Model, _Roots, { 1, false });
    for (int _Repeat = 0; _Repeat < 3; ++_Repeat)
    {
        const auto _Parallel = EvaluateNeutralModel(_Model, _Roots, { 4, true });
        EXPECT_EQ(_Serial.Geometry.size(), _Parallel.Geometry.size());
        EXPECT_TRUE(_Parallel.At("component.prototype").IsPartner(_Parallel.At("tool")));
        EXPECT_NEAR(6000.0, ResourceVolume(_Parallel.At("component.prototype")), 1e-6);
        for (std::size_t _Index = 0; _Index < _Roots.size(); ++_Index)
        {
            const auto& _Key = _Roots[_Index];
            SCOPED_TRACE(_Key);
            const auto& _Shape = _Parallel.At(_Key);
            EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
            EXPECT_NEAR(_Index % 3 == 0 ? 9000.0 : 3000.0, ResourceVolume(_Shape), 1e-6);
            EXPECT_NEAR(ResourceVolume(_Serial.At(_Key)), ResourceVolume(_Shape), 1e-6);
            ExpectResourceBounds(_Shape, ResourceBounds(_Serial.At(_Key)));
        }
    }
}

TEST(ComponentResource, BoundingBoxFilterPreservesDisjointTouchingAndOverlappingOperations)
{
    for (const double _Offset : { 100.0, 10.0, 9.9999999, 5.0, 0.0 })
    {
        for (const std::string _Operation : { "subtract", "union", "intersect" })
        {
            SCOPED_TRACE(_Operation + ":" + std::to_string(_Offset));
            auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
            _Model.Geometry.push_back({ "tool", EGeometryOperator::Transform,
                { "component.prototype" }, ResourcePlacement(_Offset, 0, 0) });
            _Model.Geometry.push_back({ "result", EGeometryOperator::Boolean,
                { "component.prototype", "tool" }, { { "operation", _Operation } } });
            const auto _Baseline = EvaluateNeutralModel(_Model, { "result" }, { 1, false });
            const auto _Filtered = EvaluateNeutralModel(_Model, { "result" }, { 4, true });
            EXPECT_NEAR(ResourceVolume(_Baseline.At("result")),
                ResourceVolume(_Filtered.At("result")), 1e-6);
            if (ResourceVolume(_Baseline.At("result")) > 1e-6)
                ExpectResourceBounds(_Filtered.At("result"), ResourceBounds(_Baseline.At("result")));
            if (_Operation == "subtract" && _Offset == 100.0)
                EXPECT_TRUE(_Filtered.At("result").IsSame(_Filtered.At("component.prototype")));
        }
    }
}

TEST(ComponentResource, ParallelWorkerFailureJoinsAndAllowsSubsequentEvaluation)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    _Model.Geometry.push_back({ "bad", EGeometryOperator::Resource,
        {}, { { "brep", std::string("invalid BRep") } } });
    for (int _Repeat = 0; _Repeat < 3; ++_Repeat)
    {
        EXPECT_THROW(EvaluateNeutralModel(_Model, { "bad", "component.prototype" }, { 4, true }),
            std::invalid_argument);
        const auto _Valid = EvaluateNeutralModel(_Model, { "component.prototype" }, { 4, true });
        EXPECT_NEAR(6000.0, ResourceVolume(_Valid.At("component.prototype")), 1e-6);
    }
}

TEST(ComponentResource, ParallelEvaluationCanRunInsideSingleWorkerTaskScheduler)
{
    auto _Caller = std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(1);
    const auto _Task = iCAX::Tasks::Run([] {
        auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
        _Model.Geometry.push_back({ "second", EGeometryOperator::Resource, {},
            { { "brep", ResourceBRep(BRepPrimAPI_MakeBox(20, 20, 30).Shape()) } } });
        return EvaluateNeutralModel(_Model, { "component.prototype", "second" }, { 4, true });
    }, _Caller);
    ASSERT_TRUE(_Task.WaitFor(std::chrono::seconds(5)));
    const auto _Result = _Task.Result();
    EXPECT_NEAR(6000.0, ResourceVolume(_Result.At("component.prototype")), 1e-6);
    EXPECT_NEAR(12000.0, ResourceVolume(_Result.At("second")), 1e-6);
}

TEST(ComponentResource, NativeCylinderBRepRoundTripPreservesSeamAndVolume)
{
    const auto _Shape = BRepPrimAPI_MakeCylinder(7, 30).Shape();
    const auto _Neutral = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Shape, "cylinder", "cylinder", 0.025);
    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_Neutral);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(BRepCheck_Analyzer(_Rebuilt.Shape).IsValid());
    EXPECT_NEAR(ResourceVolume(_Shape), ResourceVolume(_Rebuilt.Shape), 1e-6);
}

TEST(ComponentResource, MirroredCylinderBRepRoundTripPreservesSurfaceHandedness)
{
    gp_Trsf _Mirror;
    _Mirror.SetMirror(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)));
    const auto _Shape = BRepBuilderAPI_Transform(BRepPrimAPI_MakeCylinder(7, 30).Shape(), _Mirror, true).Shape();
    ASSERT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
    const auto _Neutral = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Shape, "mirrored", "mirrored", 0.025);
    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_Neutral);
    ASSERT_TRUE(_Rebuilt.bOK);
    EXPECT_TRUE(BRepCheck_Analyzer(_Rebuilt.Shape).IsValid());
    EXPECT_NEAR(ResourceVolume(_Shape), ResourceVolume(_Rebuilt.Shape), 1e-6);
}

TEST(ComponentResource, ParallelBRepTranslationPreservesOrderGeometryAndUnmeshedPrototypes)
{
    using namespace iCAX::OpenCascade;
    const auto _Box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const auto _Cylinder = BRepPrimAPI_MakeCylinder(7, 30).Shape();
    std::vector<SBRepConversionInput> _Inputs;
    for (int _Index = 0; _Index < 17; ++_Index)
    {
        gp_Trsf _Translation;
        _Translation.SetTranslation(gp_Vec(_Index * 50, _Index * 7, 0));
        _Inputs.push_back({ (_Index % 2 ? _Box : _Cylinder).Moved(TopLoc_Location(_Translation)),
            "part " + std::to_string(_Index), "resource/" + std::to_string(_Index) });
    }
    const auto _Serial = ConvertOpenCascadeShapesToBRep(_Inputs, 0.025, 1);
    const auto _Parallel = ConvertOpenCascadeShapesToBRep(_Inputs, 0.025, 4);
    ASSERT_EQ(_Inputs.size(), _Parallel.size());
    for (std::size_t _Index = 0; _Index < _Inputs.size(); ++_Index)
    {
        SCOPED_TRACE(_Index);
        const auto& _Actual = _Parallel[_Index];
        EXPECT_EQ(_Inputs[_Index].SourceID, _Actual.Metadata.SourceId);
        EXPECT_EQ(_Inputs[_Index].DisplayName, _Actual.Metadata.Name);
        EXPECT_EQ(_Serial[_Index].Vertices.size(), _Actual.Vertices.size());
        EXPECT_EQ(_Serial[_Index].Edges.size(), _Actual.Edges.size());
        EXPECT_EQ(_Serial[_Index].Faces.size(), _Actual.Faces.size());
        ASSERT_EQ(_Serial[_Index].Triangulations3.size(), _Actual.Triangulations3.size());
        for (std::size_t _Face = 0; _Face < _Actual.Triangulations3.size(); ++_Face)
        {
            const auto& _Mesh = _Actual.Triangulations3[_Face].Geometry;
            EXPECT_FALSE(_Mesh.Triangles.empty());
            EXPECT_EQ(_Serial[_Index].Triangulations3[_Face].Geometry.Triangles, _Mesh.Triangles);
            EXPECT_EQ(_Serial[_Index].Triangulations3[_Face].Geometry.Vertices.size(), _Mesh.Vertices.size());
        }
        const auto _Rebuilt = BuildOpenCascadeShape(_Actual);
        ASSERT_TRUE(_Rebuilt.bOK);
        EXPECT_TRUE(BRepCheck_Analyzer(_Rebuilt.Shape).IsValid());
        EXPECT_NEAR(ResourceVolume(_Inputs[_Index].Shape), ResourceVolume(_Rebuilt.Shape), 1e-6);
        ExpectResourceBounds(_Rebuilt.Shape, ResourceBounds(_Inputs[_Index].Shape));
    }
    for (const auto& _Prototype : { _Box, _Cylinder })
        for (TopExp_Explorer _Face(_Prototype, TopAbs_FACE); _Face.More(); _Face.Next())
        {
            TopLoc_Location _Location;
            EXPECT_TRUE(BRep_Tool::Triangulation(TopoDS::Face(_Face.Current()), _Location).IsNull());
        }
}

TEST(ComponentResource, BRepTranslationRejectsInvalidBatchAndCanRunAgain)
{
    using namespace iCAX::OpenCascade;
    EXPECT_TRUE(ConvertOpenCascadeShapesToBRep({}).empty());
    const SBRepConversionInput _Valid{ BRepPrimAPI_MakeBox(10, 20, 30).Shape(), "box", "box" };
    EXPECT_THROW(ConvertOpenCascadeShapesToBRep({ _Valid, { {}, "null", "bad" } }), std::invalid_argument);
    auto _Pool = std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(1);
    const auto _Task = iCAX::Tasks::Run([_Valid] {
        return ConvertOpenCascadeShapesToBRep({ _Valid, _Valid }, 0.025, 4);
    }, _Pool);
    ASSERT_TRUE(_Task.WaitFor(std::chrono::seconds(5)));
    EXPECT_EQ(2u, _Task.Result().size());
}
