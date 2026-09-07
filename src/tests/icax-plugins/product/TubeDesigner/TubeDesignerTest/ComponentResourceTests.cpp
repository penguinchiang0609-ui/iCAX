#include "pch.h"

#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>

#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
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
