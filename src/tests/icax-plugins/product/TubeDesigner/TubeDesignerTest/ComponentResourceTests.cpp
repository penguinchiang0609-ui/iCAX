#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <Task/Task.h>
#include <GeometryData/BRepPersistence.h>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Elips.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <variant>

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

    std::array<double, 6> MeshBounds(const iCAX::GeometryData::CTriangleMeshResource& Mesh_)
    {
        const auto& _Vertices = Mesh_.Mesh.Vertices;
        if (_Vertices.empty()) throw std::invalid_argument("triangle mesh has no vertices");
        std::array<double, 6> _Bounds{
            _Vertices.front().X, _Vertices.front().Y, _Vertices.front().Z,
            _Vertices.front().X, _Vertices.front().Y, _Vertices.front().Z
        };
        for (const auto& _Vertex : _Vertices)
        {
            _Bounds[0] = std::min(_Bounds[0], _Vertex.X);
            _Bounds[1] = std::min(_Bounds[1], _Vertex.Y);
            _Bounds[2] = std::min(_Bounds[2], _Vertex.Z);
            _Bounds[3] = std::max(_Bounds[3], _Vertex.X);
            _Bounds[4] = std::max(_Bounds[4], _Vertex.Y);
            _Bounds[5] = std::max(_Bounds[5], _Vertex.Z);
        }
        return _Bounds;
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

TEST(ComponentResource, DependencyReadyBranchesAndDuplicateInputsMatchSerial)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    _Model.Geometry.push_back({ "tool", EGeometryOperator::Transform,
        { "component.prototype" }, ResourcePlacement(5, 0, 0) });
    std::vector<std::string> _Roots;
    for (int _Branch = 0; _Branch < 13; ++_Branch)
    {
        std::string _Previous = "component.prototype";
        for (int _Depth = 0; _Depth <= _Branch % 4; ++_Depth)
        {
            const auto _Key = "branch." + std::to_string(_Branch) + "." + std::to_string(_Depth);
            _Model.Geometry.push_back({ _Key, EGeometryOperator::Boolean,
                { _Previous, "tool" }, { { "operation", std::string("subtract") } } });
            _Previous = _Key;
        }
        const auto _Placed = "placed." + std::to_string(_Branch);
        _Model.Geometry.push_back({ _Placed, EGeometryOperator::Transform,
            { _Previous }, ResourcePlacement(20.0 * _Branch, 0, 0) });
        _Roots.push_back(_Placed);
    }
    // Repeated references are one dependency but remain two compound members.
    auto _Children = _Roots;
    _Children.push_back(_Roots.front());
    _Model.Geometry.push_back({ "assembly", EGeometryOperator::Compound, _Children, {} });
    _Roots.push_back("assembly");
    const auto _Serial = EvaluateNeutralModel(_Model, _Roots, { 1, true });
    for (const auto _Concurrency : { 2u, 4u, 8u })
    {
        const auto _Parallel = EvaluateNeutralModel(_Model, _Roots, { _Concurrency, true });
        ASSERT_EQ(_Serial.Geometry.size(), _Parallel.Geometry.size());
        EXPECT_TRUE(_Parallel.At("component.prototype").IsPartner(_Parallel.At("tool")));
        for (const auto& _Key : _Roots)
        {
            SCOPED_TRACE(_Key);
            EXPECT_NEAR(ResourceVolume(_Serial.At(_Key)), ResourceVolume(_Parallel.At(_Key)), 1e-6);
            ExpectResourceBounds(_Parallel.At(_Key), ResourceBounds(_Serial.At(_Key)));
        }
        EXPECT_NEAR(6000.0, ResourceVolume(_Parallel.At("component.prototype")), 1e-6);
    }
}

TEST(ComponentResource, CoordinatorFailureDrainsWorkersBeforeReturning)
{
    auto _Model = ResourceModel(ResourceBRep(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    std::vector<std::string> _Roots;
    for (int _Index = 0; _Index < 12; ++_Index)
    {
        const auto _Key = "independent." + std::to_string(_Index);
        _Model.Geometry.push_back({ _Key, EGeometryOperator::Resource, {},
            { { "brep", ResourceBRep(BRepPrimAPI_MakeBox(20, 20, 30).Shape()) } } });
        _Roots.push_back(_Key);
    }
    _Model.Geometry.push_back({ "bad.placement", EGeometryOperator::Transform,
        { "component.prototype" }, {} });
    _Roots.push_back("bad.placement");
    EXPECT_THROW(EvaluateNeutralModel(_Model, _Roots, { 4, true }), std::invalid_argument);
    const auto _Valid = EvaluateNeutralModel(_Model, { "component.prototype" }, { 4, true });
    EXPECT_NEAR(6000.0, ResourceVolume(_Valid.At("component.prototype")), 1e-6);
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
    EXPECT_TRUE(std::any_of(_Neutral.Surfaces3.begin(), _Neutral.Surfaces3.end(), [](const auto& Surface_) {
        return std::holds_alternative<iCAX::GeometryData::CylindricalSurface3>(Surface_.Geometry);
    }));
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

namespace
{
    TopoDS_Wire ResourceEllipseWire(double Major_, double Minor_, const gp_Ax2& Frame_)
    {
        BRepBuilderAPI_MakeWire _Wire;
        const gp_Elips _Ellipse(Frame_, Major_, Minor_);
        // Match the exact quadrant arcs emitted by the actual tube templates.
        for (int _Index = 0; _Index < 4; ++_Index)
            _Wire.Add(BRepBuilderAPI_MakeEdge(_Ellipse, _Index * std::acos(-1.) / 2,
                (_Index + 1) * std::acos(-1.) / 2).Edge());
        return _Wire.Wire();
    }

    TopoDS_Shape ResourceEllipticalTube()
    {
        const gp_Ax2 _Frame(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
        BRepBuilderAPI_MakeFace _Face(ResourceEllipseWire(50, 10, _Frame));
        _Face.Add(TopoDS::Wire(ResourceEllipseWire(48.5, 8.5, _Frame).Reversed()));
        if (!_Face.IsDone()) throw std::runtime_error("ellipse test face failed");
        return BRepPrimAPI_MakePrism(_Face.Face(), gp_Vec(0, 0, 970)).Shape();
    }

    bool ResourceHasSurface(const TopoDS_Shape& Shape_, GeomAbs_SurfaceType Type_)
    {
        for (TopExp_Explorer _Face(Shape_, TopAbs_FACE); _Face.More(); _Face.Next())
            if (BRepAdaptor_Surface(TopoDS::Face(_Face.Current()), true).GetType() == Type_) return true;
        return false;
    }

    double ResourceAdaptiveVolume(const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        const auto _Error = BRepGProp::VolumeProperties(Shape_, _Properties, 1e-10, false, false);
        EXPECT_LT(_Error, 1e-7);
        return _Properties.Mass();
    }

    // Orientation, closed flag and all four edge occurrence orientations.
    // INTERNAL edges are exact topology even though a connection-order explorer
    // cannot visit them. Sorting removes irrelevant wire enumeration order.
    using ResourceWireSignature = std::array<std::size_t, 6>;

    std::vector<ResourceWireSignature> ResourceWireSignatures(const TopoDS_Shape& Shape_)
    {
        std::vector<ResourceWireSignature> _Signatures;
        for (TopExp_Explorer _Faces(Shape_, TopAbs_FACE); _Faces.More(); _Faces.Next())
        {
            const auto _Face = _Faces.Current().Oriented(TopAbs_FORWARD);
            for (TopoDS_Iterator _Wires(_Face, false, true); _Wires.More(); _Wires.Next())
            {
                const auto _Occurrence = _Wires.Value();
                if (_Occurrence.ShapeType() != TopAbs_WIRE) continue;
                const auto _Wire = _Occurrence.Oriented(TopAbs_FORWARD);
                ResourceWireSignature _Signature{
                    static_cast<std::size_t>(_Occurrence.Orientation()),
                    static_cast<std::size_t>(_Wire.Closed()), 0, 0, 0, 0 };
                for (TopoDS_Iterator _Edges(_Wire, false, true); _Edges.More(); _Edges.Next())
                    ++_Signature[2 + static_cast<std::size_t>(_Edges.Value().Orientation())];
                _Signatures.push_back(_Signature);
            }
        }
        std::sort(_Signatures.begin(), _Signatures.end());
        return _Signatures;
    }

    std::vector<ResourceWireSignature> ResourceWireSignatures(const iCAX::GeometryData::BRepModel& Model_)
    {
        std::vector<ResourceWireSignature> _Signatures;
        for (const auto& _Face : Model_.Faces)
            for (std::size_t _Index = 0; _Index < _Face.WireIds.size(); ++_Index)
            {
                const auto _Wire = std::find_if(Model_.Wires.begin(), Model_.Wires.end(),
                    [&](const auto& _Candidate) { return _Candidate.Id == _Face.WireIds[_Index]; });
                if (_Wire == Model_.Wires.end()) throw std::runtime_error("test face references missing wire");
                ResourceWireSignature _Signature{
                    static_cast<std::size_t>(_Face.WireOrientations.at(_Index)),
                    static_cast<std::size_t>(_Wire->Closed), 0, 0, 0, 0 };
                for (const auto& _Coedge : _Wire->Coedges)
                    ++_Signature[2 + static_cast<std::size_t>(_Coedge.Orientation)];
                _Signatures.push_back(_Signature);
            }
        std::sort(_Signatures.begin(), _Signatures.end());
        return _Signatures;
    }

    void ExpectResourceSweptRoundTrip(const TopoDS_Shape& Source_, double Tolerance_ = 0.001)
    {
        ASSERT_TRUE(BRepCheck_Analyzer(Source_).IsValid());
        const auto _Volume = ResourceAdaptiveVolume(Source_);
        ASSERT_GT(_Volume, 0);
        const auto _WireSignatures = ResourceWireSignatures(Source_);
        auto _Neutral = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Source_, "swept", "swept", Tolerance_);
        EXPECT_EQ(ResourceWireSignatures(_Neutral), _WireSignatures);
        // Exercise the persisted model, not just the in-memory conversion.
        _Neutral = iCAX::GeometryData::Persistence::Deserialize(iCAX::GeometryData::Persistence::Serialize(_Neutral));
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_Neutral);
        ASSERT_TRUE(_Rebuilt.bOK);
        ASSERT_FALSE(_Rebuilt.Shape.IsNull());
        ASSERT_TRUE(BRepCheck_Analyzer(_Rebuilt.Shape).IsValid());
        EXPECT_EQ(ResourceWireSignatures(_Rebuilt.Shape), _WireSignatures);
        int _Solids = 0;
        for (TopExp_Explorer _Solid(_Rebuilt.Shape, TopAbs_SOLID); _Solid.More(); _Solid.Next()) ++_Solids;
        EXPECT_EQ(_Solids, 1);
        EXPECT_NEAR(ResourceAdaptiveVolume(_Rebuilt.Shape), _Volume, _Volume * 1e-7);
        Bnd_Box _Before, _After;
        BRepBndLib::AddOptimal(Source_, _Before, false, false);
        BRepBndLib::AddOptimal(_Rebuilt.Shape, _After, false, false);
        std::array<double, 6> _Expected{}, _Actual{};
        _Before.Get(_Expected[0], _Expected[1], _Expected[2], _Expected[3], _Expected[4], _Expected[5]);
        _After.Get(_Actual[0], _Actual[1], _Actual[2], _Actual[3], _Actual[4], _Actual[5]);
        for (std::size_t _Index = 0; _Index < _Expected.size(); ++_Index)
            EXPECT_NEAR(_Actual[_Index], _Expected[_Index], 1e-5) << "bound " << _Index;
        // Face and edge validity alone can miss an inconsistent UV frame.
        // Check that each saved pcurve evaluates onto its actual 3D edge.
        for (TopExp_Explorer _Face(_Rebuilt.Shape, TopAbs_FACE); _Face.More(); _Face.Next())
        {
            const auto _ActualFace = TopoDS::Face(_Face.Current());
            BRepAdaptor_Surface _Surface(_ActualFace, true);
            for (TopExp_Explorer _Edge(_ActualFace, TopAbs_EDGE); _Edge.More(); _Edge.Next())
            {
                const auto _ActualEdge = TopoDS::Edge(_Edge.Current());
                if (BRep_Tool::Degenerated(_ActualEdge)) continue;
                BRepAdaptor_Curve _Curve(_ActualEdge);
                BRepAdaptor_Curve2d _PCurve(_ActualEdge, _ActualFace);
                const auto _Tolerance = std::max(1e-6, 2 * (BRep_Tool::Tolerance(_ActualEdge) + BRep_Tool::Tolerance(_ActualFace)));
                for (int _Sample = 0; _Sample <= 10; ++_Sample)
                {
                    const auto _T = _PCurve.FirstParameter() + (_PCurve.LastParameter() - _PCurve.FirstParameter()) * _Sample / 10.;
                    const auto _UV = _PCurve.Value(_T);
                    EXPECT_LE(_Curve.Value(_T).Distance(_Surface.Value(_UV.X(), _UV.Y())), _Tolerance);
                }
            }
        }
    }
}

TEST(ComponentResource, EllipticalExtrusionBRepRoundTripPreservesParametersAndMaterial)
{
    const auto _Tube = ResourceEllipticalTube();
    ASSERT_TRUE(ResourceHasSurface(_Tube, GeomAbs_SurfaceOfExtrusion));
    const auto _AnalyticVolume = std::acos(-1.) * (50 * 10 - 48.5 * 8.5) * 970;
    EXPECT_NEAR(ResourceAdaptiveVolume(_Tube), _AnalyticVolume, _AnalyticVolume * 1e-7);
    ExpectResourceSweptRoundTrip(_Tube);
    gp_Trsf _Placement;
    _Placement.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), 0.37);
    _Placement.SetTranslationPart(gp_Vec(600, 1700, 520));
    ExpectResourceSweptRoundTrip(_Tube.Moved(TopLoc_Location(_Placement)));
}

TEST(ComponentResource, EllipticalBooleanCutBRepRoundTripKeepsTrimmedPcurves)
{
    const auto _Tube = ResourceEllipticalTube();
    const auto _Tool = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15, -30, 400), gp_Dir(0, 1, 0)), 4, 60).Shape();
    BRepAlgoAPI_Cut _Cut(_Tube, _Tool);
    ASSERT_TRUE(_Cut.IsDone());
    ASSERT_TRUE(ResourceHasSurface(_Cut.Shape(), GeomAbs_SurfaceOfExtrusion));
    EXPECT_LT(ResourceAdaptiveVolume(_Cut.Shape()), ResourceAdaptiveVolume(_Tube));
    ExpectResourceSweptRoundTrip(_Cut.Shape());
}

TEST(ComponentResource, EllipticalRevolutionBRepRoundTripKeepsItsAngularParameterFrame)
{
    const gp_Ax2 _Frame(gp_Pnt(30, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    const auto _Face = BRepBuilderAPI_MakeFace(ResourceEllipseWire(8, 5, _Frame)).Face();
    const auto _Ring = BRepPrimAPI_MakeRevol(_Face, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1))).Shape();
    ASSERT_TRUE(ResourceHasSurface(_Ring, GeomAbs_SurfaceOfRevolution));
    const auto _AnalyticVolume = 2 * std::acos(-1.) * 30 * std::acos(-1.) * 8 * 5;
    EXPECT_NEAR(ResourceAdaptiveVolume(_Ring), _AnalyticVolume, _AnalyticVolume * 1e-7);
    ExpectResourceSweptRoundTrip(_Ring);
}

TEST(ComponentResource, ActualEllipticalPostCopeBRepRoundTripPreservesInternalWireOccurrences)
{
    const auto _Model = CTemplateCodec::ParseNeutralModel(CStandardJsonCodec::Parse(
#include "AssemblyInfillEllipseCopeFixture.inc"
    ));
    ASSERT_EQ(_Model.Geometry.size(), 15u);
    const auto _Geometry = EvaluateNeutralModel(_Model,
        { "process.83fa65dc210548e1aeb74668.operation.0" }, { 1, false });
    const auto& _Coped = _Geometry.At("process.83fa65dc210548e1aeb74668.operation.0");
    ASSERT_TRUE(BRepCheck_Analyzer(_Coped).IsValid());
    const auto _Signatures = ResourceWireSignatures(_Coped);
    EXPECT_TRUE(std::any_of(_Signatures.begin(), _Signatures.end(),
        [](const auto& _Signature) { return _Signature[4] != 0; }))
        << "actual cope must retain its internal boolean edge occurrences";
    // Native disassembly uses this tolerance. Validate the original boolean
    // topology and the whole-shape exact parameter conversion independently.
    ExpectResourceSweptRoundTrip(_Coped, 0.025);
    BRepBuilderAPI_NurbsConvert _Conversion(_Coped, true);
    ASSERT_TRUE(_Conversion.IsDone());
    ASSERT_TRUE(BRepCheck_Analyzer(_Conversion.Shape()).IsValid());
    ExpectResourceSweptRoundTrip(_Conversion.Shape(), 0.025);
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

TEST(ComponentResource, RepeatedBRepShapesKeepIndependentResourceMetadata)
{
    using namespace iCAX::OpenCascade;
    const auto _Shape = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const auto _Results = ConvertOpenCascadeShapesToBRep({
        { _Shape, "first", "resource/first" },
        { _Shape, "second", "resource/second" },
    }, 0.025, 2);
    ASSERT_EQ(2u, _Results.size());
    for (std::size_t _Index = 0; _Index < _Results.size(); ++_Index)
    {
        const auto _Name = _Index == 0 ? "first" : "second";
        const auto _ID = _Index == 0 ? "resource/first" : "resource/second";
        SCOPED_TRACE(_ID);
        EXPECT_EQ(_Name, _Results[_Index].Metadata.Name);
        EXPECT_EQ(_ID, _Results[_Index].Metadata.SourceId);
        const auto _CheckRows = [&](const auto& Rows_) {
            for (const auto& _Row : Rows_) EXPECT_EQ(_ID, _Row.Metadata.SourceId);
        };
        _CheckRows(_Results[_Index].Curves2); _CheckRows(_Results[_Index].Curves3);
        _CheckRows(_Results[_Index].Surfaces3); _CheckRows(_Results[_Index].Triangulations3);
        _CheckRows(_Results[_Index].Vertices); _CheckRows(_Results[_Index].Edges);
        _CheckRows(_Results[_Index].Wires); _CheckRows(_Results[_Index].Faces);
        _CheckRows(_Results[_Index].Shells); _CheckRows(_Results[_Index].Solids);
        _CheckRows(_Results[_Index].CompSolids); _CheckRows(_Results[_Index].Compounds);
        const auto _Rebuilt = BuildOpenCascadeShape(_Results[_Index]);
        ASSERT_TRUE(_Rebuilt.bOK);
        EXPECT_NEAR(ResourceVolume(_Shape), ResourceVolume(_Rebuilt.Shape), 1e-6);
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

TEST(ComponentResource, ParallelDisplayMeshTranslationPreservesOrderAndGeometry)
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
    const auto _Serial = ConvertOpenCascadeShapesToTriangleMeshes(_Inputs, 0.1, 1);
    const auto _Parallel = ConvertOpenCascadeShapesToTriangleMeshes(_Inputs, 0.1, 4);
    ASSERT_EQ(_Inputs.size(), _Parallel.size());
    for (std::size_t _Index = 0; _Index < _Inputs.size(); ++_Index)
    {
        SCOPED_TRACE(_Index);
        const auto& _Actual = _Parallel[_Index];
        EXPECT_EQ(_Inputs[_Index].SourceID, _Actual.Metadata.SourceId);
        EXPECT_EQ(_Inputs[_Index].DisplayName, _Actual.Metadata.Name);
        EXPECT_FALSE(_Actual.Mesh.Vertices.empty());
        EXPECT_FALSE(_Actual.Mesh.Triangles.empty());
        EXPECT_EQ(_Serial[_Index].Mesh.Vertices.size(), _Actual.Mesh.Vertices.size());
        EXPECT_EQ(_Serial[_Index].Mesh.Triangles, _Actual.Mesh.Triangles);
        EXPECT_EQ(_Actual.Mesh.Triangles.size(), _Actual.Mesh.TriangleFaceIds.size());
        const auto _ExpectedBounds = ResourceBounds(_Inputs[_Index].Shape);
        const auto _MeshBounds = MeshBounds(_Actual);
        const auto _Tolerance = _Index % 2 ? 1e-6 : 1.0;
        for (std::size_t _Axis = 0; _Axis < _ExpectedBounds.size(); ++_Axis)
            EXPECT_NEAR(_ExpectedBounds[_Axis], _MeshBounds[_Axis], _Tolerance) << _Axis;
    }
    EXPECT_TRUE(ConvertOpenCascadeShapesToTriangleMeshes({}).empty());
    EXPECT_THROW(ConvertOpenCascadeShapesToTriangleMeshes(
        { _Inputs.front(), { {}, "null", "bad" } }), std::invalid_argument);
}
