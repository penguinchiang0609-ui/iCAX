#include "pch.h"

// The workbook implementation is linked directly into this test executable.
#define _TUBE_DESIGNER
#include <TubeDesigner/PartListXlsxExporter.h>
#undef _TUBE_DESIGNER
#include <TubeDesigner/ComponentModelLibrary.h>
#include <TubeDesigner/FinalGeometryMeasurement.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>
#include <Data/VariantSerializer.h>

#include <chrono>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Iterator.hxx>

namespace
{
    using namespace iCAX::TubeDesigner;

    iCAX::Data::Variant NumberArray(std::initializer_list<double> Values_)
    {
        iCAX::Data::VariantArray _Values;
        for (const auto _Value : Values_) _Values.emplace_back(_Value);
        return _Values;
    }

    iCAX::Data::ObjectMap RoundedProfile(
        double Width_, double Height_, double Radius_)
    {
        return {
            { "placement", iCAX::Data::ObjectMap{
                { "origin", NumberArray({ 0.0, 0.0, 0.0 }) },
                { "xAxis", NumberArray({ 1.0, 0.0, 0.0 }) },
                { "yAxis", NumberArray({ 0.0, 1.0, 0.0 }) }
            } },
            { "contours", iCAX::Data::VariantArray{
                iCAX::Data::ObjectMap{
                    { "kind", std::string("roundedRectangle") },
                    { "width", Width_ }, { "height", Height_ }, { "radius", Radius_ }
                },
                iCAX::Data::ObjectMap{
                    { "kind", std::string("roundedRectangle") },
                    { "width", Width_ - 4.0 }, { "height", Height_ - 4.0 },
                    { "radius", std::max(0.0, Radius_ - 2.0) }
                }
            } }
        };
    }

    iCAX::TemplateRuntime::SPythonTemplateHostOptions EmbeddedPythonHostOptions(
        const std::filesystem::path& RepositoryRoot_)
    {
        const auto _RuntimeLibrary = RepositoryRoot_
            / "src/x64/Debug/runtime/python/python312.dll";
        const auto _Worker = RepositoryRoot_
            / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
        if (!std::filesystem::is_regular_file(_RuntimeLibrary))
            throw std::runtime_error("embedded Python runtime was not prepared for the test");
        return { _RuntimeLibrary, _Worker, _Worker.parent_path() };
    }

    std::filesystem::path Utf8TestPath(const std::string& Value_)
    {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(Value_.data()), Value_.size()));
    }

    std::string Utf8TestPathText(const std::filesystem::path& Value_)
    {
        const auto _Text = Value_.u8string();
        return { reinterpret_cast<const char*>(_Text.data()), _Text.size() };
    }

    iCAX::TemplateRuntime::SNeutralModel RootSelectionTestModel(
        bool IncludeUnsupportedBranch_ = false)
    {
        auto _Document = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
            "schema":"icax.neutral-model", "schemaVersion":1,
            "template":{"id":"test.root-selection","version":"1.0.0","packageDigest":"test"},
            "geometry":[
                {"key":"shared.profile","operator":"profile2d","arguments":{
                    "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                    "contours":[{"kind":"roundedRectangle","width":40,"height":20,"radius":0}]}},
                {"key":"shared.solid","operator":"extrude","inputs":["shared.profile"],
                    "arguments":{"vector":[0,0,100]}},
                {"key":"display.final","operator":"compound","inputs":["shared.solid"]},
                {"key":"export.profile","operator":"profile2d","arguments":{
                    "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                    "contours":[{"kind":"roundedRectangle","width":60,"height":30,"radius":0}]}},
                {"key":"export.solid","operator":"extrude","inputs":["export.profile"],
                    "arguments":{"vector":[0,0,200]}},
                {"key":"export.final","operator":"compound","inputs":["shared.solid","export.solid"]}
            ],
            "items":[
                {"key":"assembled","displayName":"Display item","representations":{"display":"display.final"}},
                {"key":"manufactured","displayName":"Manufacturing item","representations":{"export":"export.final"}}
            ],
            "outputs":[
                {"key":"display.default","purpose":"display","items":["assembled"]},
                {"key":"export.manufacturing","purpose":"export","items":["manufactured"]}
            ]
        })json").To<iCAX::Data::ObjectMap>();
        if (IncludeUnsupportedBranch_)
        {
            auto _Geometry = _Document.at("geometry").To<iCAX::Data::VariantArray>();
            _Geometry.emplace_back(iCAX::Data::ObjectMap{
                { "key", std::string("unused.unsupported") },
                { "operator", std::string("sweep") },
                { "inputs", iCAX::Data::VariantArray{ std::string("shared.profile") } }
            });
            _Document["geometry"] = _Geometry;
        }
        return iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document);
    }

    double RootSelectionShapeVolume(const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return _Properties.Mass();
    }

    std::array<double, 6> RootSelectionShapeBounds(const TopoDS_Shape& Shape_)
    {
        Bnd_Box _Bounds;
        BRepBndLib::Add(Shape_, _Bounds);
        std::array<double, 6> _Values{};
        _Bounds.Get(_Values[0], _Values[1], _Values[2], _Values[3], _Values[4], _Values[5]);
        return _Values;
    }

    void ExpectRootSelectionGeometryMatches(
        const TopoDS_Shape& Selected_, const TopoDS_Shape& Complete_)
    {
        ASSERT_FALSE(Selected_.IsNull());
        ASSERT_FALSE(Complete_.IsNull());
        const auto _ExpectedVolume = RootSelectionShapeVolume(Complete_);
        EXPECT_NEAR(_ExpectedVolume, RootSelectionShapeVolume(Selected_),
            std::max(1.0, std::abs(_ExpectedVolume)) * 1.0e-8);
        const auto _SelectedBounds = RootSelectionShapeBounds(Selected_);
        const auto _CompleteBounds = RootSelectionShapeBounds(Complete_);
        for (std::size_t _Index = 0; _Index < _SelectedBounds.size(); ++_Index)
            EXPECT_NEAR(_CompleteBounds[_Index], _SelectedBounds[_Index], 1.0e-6);
    }

    iCAX::TemplateRuntime::SNeutralModel RigidInstanceTestModel()
    {
        // The non-square profile makes a swapped or mirrored placement observable.
        return iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
                "schema":"icax.neutral-model", "schemaVersion":1,
                "template":{"id":"test.rigid-instances","version":"1.0.0","packageDigest":"test"},
                "geometry":[
                    {"key":"shared.profile","operator":"profile2d","arguments":{
                        "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                        "contours":[{"kind":"roundedRectangle","width":40,"height":20,"radius":0}]}},
                    {"key":"shared.solid","operator":"extrude","inputs":["shared.profile"],
                        "arguments":{"vector":[0,0,100]}},
                    {"key":"display.first","operator":"transform","inputs":["shared.solid"],
                        "arguments":{"placement":{"origin":[125,-80,310],
                            "xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}}},
                    {"key":"export.second","operator":"transform","inputs":["shared.solid"],
                        "arguments":{"placement":{"origin":[-200,50,25],
                            "xAxis":[0,1,0],"yAxis":[0,0,1],"zAxis":[1,0,0]}}}
                ],
                "items":[
                    {"key":"first","displayName":"First instance","representations":{"display":"display.first"}},
                    {"key":"second","displayName":"Second instance","representations":{"export":"export.second"}}
                ],
                "outputs":[
                    {"key":"display.default","purpose":"display","items":["first"]},
                    {"key":"export.manufacturing","purpose":"export","items":["second"]}
                ]
            })json"));
    }

    void ExpectRigidShapeBounds(
        const TopoDS_Shape& Shape_, const std::array<double, 6>& Expected_)
    {
        ASSERT_FALSE(Shape_.IsNull());
        const auto _Bounds = RootSelectionShapeBounds(Shape_);
        for (std::size_t _Index = 0; _Index < Expected_.size(); ++_Index)
            EXPECT_NEAR(Expected_[_Index], _Bounds[_Index], 1.0e-6) << "bound " << _Index;
    }

    void ExpectUnmodifiedBoxPrototype(const TopoDS_Shape& Shape_)
    {
        EXPECT_NEAR(80000.0, RootSelectionShapeVolume(Shape_), 1.0e-6);
        for (const auto& [_Type, _ExpectedCount] : std::array{
            std::pair{ TopAbs_VERTEX, 8 }, std::pair{ TopAbs_EDGE, 12 }, std::pair{ TopAbs_FACE, 6 } })
        {
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> _Subshapes;
            TopExp::MapShapes(Shape_, _Type, _Subshapes);
            EXPECT_EQ(_ExpectedCount, _Subshapes.Extent());
        }
    }

    iCAX::OpenCascade::SNeutralModelEvaluation EvaluateTemplatePurposeGeometry(
        const iCAX::TemplateRuntime::SNeutralModel& Model_, const std::string& Purpose_)
    {
        const auto _Output = std::find_if(Model_.Outputs.begin(), Model_.Outputs.end(),
            [&](const auto& Output_) { return Output_.Purpose == Purpose_; });
        if (_Output == Model_.Outputs.end() || _Output->ItemKeys.empty())
            throw std::runtime_error("test template has no " + Purpose_ + " output items");
        std::vector<std::string> _Roots;
        for (const auto& _ItemKey : _Output->ItemKeys)
        {
            const auto _Item = std::find_if(Model_.Items.begin(), Model_.Items.end(),
                [&](const auto& Item_) { return Item_.Key == _ItemKey; });
            if (_Item == Model_.Items.end())
                throw std::runtime_error("test output references missing item: " + _ItemKey);
            _Roots.push_back(_Item->Representations.at(Purpose_));
        }
        return iCAX::OpenCascade::EvaluateNeutralModel(Model_, _Roots);
    }

    void ExpectUnmachinedTemplateDisplay(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const iCAX::OpenCascade::SNeutralModelEvaluation& Display_)
    {
        ASSERT_FALSE(Display_.Geometry.empty());
        std::size_t _BareInstanceCount = 0;
        for (const auto& _Node : Model_.Geometry)
        {
            if (!Display_.Geometry.contains(_Node.Key)) continue;
            SCOPED_TRACE(_Node.Key);
            EXPECT_NE(iCAX::TemplateRuntime::EGeometryOperator::Boolean, _Node.Operator);
            if (_Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Profile2D
                || _Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Extrude)
            {
                // All displayed primitives must be the shared bare-tube prototype.
                // Per-part through-hole, groove and miter cutters are not shared.
                EXPECT_TRUE(_Node.Key.starts_with("shared.tube."));
            }
            else
            {
                EXPECT_TRUE(_Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Transform
                    || _Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Compound);
            }
            if (_Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Transform)
                ++_BareInstanceCount;
            ASSERT_FALSE(Display_.At(_Node.Key).IsNull());
        }
        EXPECT_GT(_BareInstanceCount, 0u);
        const auto _Output = std::find_if(Model_.Outputs.begin(), Model_.Outputs.end(),
            [](const auto& Output_) { return Output_.Purpose == "display"; });
        ASSERT_NE(_Output, Model_.Outputs.end());
        for (const auto& _ItemKey : _Output->ItemKeys)
        {
            const auto _Item = std::find_if(Model_.Items.begin(), Model_.Items.end(),
                [&](const auto& Item_) { return Item_.Key == _ItemKey; });
            ASSERT_NE(_Item, Model_.Items.end());
            EXPECT_TRUE(Display_.Geometry.contains(_Item->Representations.at("display")));
        }
    }

    void ExpectManufacturingBooleanIsEvaluated(
        const iCAX::TemplateRuntime::SNeutralModel& Model_,
        const iCAX::OpenCascade::SNeutralModelEvaluation& Export_)
    {
        EXPECT_GT(std::count_if(Model_.Geometry.begin(), Model_.Geometry.end(),
            [&](const auto& Node_) {
                return Node_.Operator == iCAX::TemplateRuntime::EGeometryOperator::Boolean
                    && Export_.Geometry.contains(Node_.Key);
            }), 0);
    }

    struct STemplateProtocolFixture final
    {
        iCAX::TemplateRuntime::STemplateDescriptor Descriptor;
        iCAX::Data::ObjectMap Parameters;
        std::filesystem::path TemplatePath;
    };

    STemplateProtocolFixture TemplateProtocolFixture(
        const std::filesystem::path& Root_, const std::string& Directory_)
    {
        const auto _Directory = Root_ / "src/apps/tube-designer/templates" / Directory_;
        std::ifstream _Stream(_Directory / "template.json", std::ios::binary);
        if (!_Stream) throw std::runtime_error("cannot read test template: " + Directory_);
        const std::string _Text{ std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>() };
        STemplateProtocolFixture _Fixture;
        _Fixture.Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_Text));
        _Fixture.Descriptor.PackageDigest = "explicit-purpose-test-" + Directory_;
        for (const auto& _Parameter : _Fixture.Descriptor.Parameters)
            _Fixture.Parameters[_Parameter.Key] = _Parameter.DefaultValue;
        _Fixture.Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Fixture.Descriptor, _Fixture.Parameters);
        _Fixture.TemplatePath = _Directory / "template.py";
        return _Fixture;
    }

    iCAX::Data::ObjectMap InvokeExplicitTemplatePurpose(
        iCAX::TemplateRuntime::CPythonTemplateHost& Host_,
        const STemplateProtocolFixture& Fixture_, const std::string& Purpose_)
    {
        auto _Request = iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            Fixture_.Descriptor, Fixture_.Parameters, Fixture_.TemplatePath.string());
        auto _Context = _Request.at("context").To<iCAX::Data::ObjectMap>();
        _Context["geometryPurpose"] = Purpose_;
        _Request["context"] = _Context;
        return Host_.Invoke(_Request);
    }

    void ExpectSingleResultRawProtocol(
        const iCAX::Data::ObjectMap& Raw_, const std::string& Purpose_,
        std::size_t ManufacturingPartCount_)
    {
        using iCAX::Data::ObjectMap;
        using iCAX::Data::VariantArray;
        const auto _Outputs = Raw_.at("outputs").To<VariantArray>();
        ASSERT_EQ(1u, _Outputs.size());
        const auto _Output = _Outputs.front().To<ObjectMap>();
        EXPECT_EQ("result", _Output.at("key").To<std::string>());
        EXPECT_EQ("result", _Output.at("purpose").To<std::string>());
        const auto _Items = Raw_.at("items").To<VariantArray>();
        ASSERT_FALSE(_Items.empty());
        EXPECT_EQ(_Items.size(), _Output.at("items").To<VariantArray>().size());
        for (const auto& _ItemValue : _Items)
        {
            const auto _Item = _ItemValue.To<ObjectMap>();
            SCOPED_TRACE(_Item.at("key").To<std::string>());
            const auto _Representations = _Item.at("representations").To<ObjectMap>();
            ASSERT_EQ(1u, _Representations.size());
            ASSERT_TRUE(_Representations.contains("result"));
            EXPECT_FALSE(_Representations.at("result").To<std::string>().empty());
        }
        const auto _Extensions = Raw_.at("extensions").To<ObjectMap>();
        EXPECT_EQ(Purpose_, _Extensions.at("tubeDesigner.geometryPurpose").To<std::string>());
        const auto& _Count = _Extensions.at("tubeDesigner.manufacturingPartCount");
        ASSERT_TRUE(_Count.Is<long long>() || _Count.Is<unsigned long long>());
        const auto _ActualCount = _Count.Is<long long>()
            ? static_cast<unsigned long long>(_Count.To<long long>())
            : _Count.To<unsigned long long>();
        EXPECT_EQ(static_cast<unsigned long long>(ManufacturingPartCount_), _ActualCount);
    }

    void ExpectWindowRawGraphIsPurposeSpecific(
        const iCAX::Data::ObjectMap& Raw_, const std::string& Purpose_)
    {
        const auto _Nodes = Raw_.at("geometry").To<iCAX::Data::VariantArray>();
        ASSERT_FALSE(_Nodes.empty());
        std::size_t _BooleanCount = 0;
        for (const auto& _Value : _Nodes)
        {
            const auto _Node = _Value.To<iCAX::Data::ObjectMap>();
            const auto _Key = _Node.at("key").To<std::string>();
            const auto _Operator = _Node.at("operator").To<std::string>();
            SCOPED_TRACE(_Key);
            if (_Operator == "boolean") ++_BooleanCount;
            if (Purpose_ == "display")
            {
                EXPECT_NE("boolean", _Operator);
                EXPECT_EQ(std::string::npos, _Key.find(".through."));
                EXPECT_EQ(std::string::npos, _Key.find(".miter."));
                EXPECT_EQ(std::string::npos, _Key.find(".export."));
                if (_Operator == "profile2d" || _Operator == "extrude")
                    EXPECT_TRUE(_Key.starts_with("shared.tube."));
                else
                    EXPECT_TRUE(_Operator == "transform" || _Operator == "compound");
            }
            else
            {
                EXPECT_EQ(std::string::npos, _Key.find(".display."));
            }
        }
        if (Purpose_ == "manufacturing") EXPECT_GT(_BooleanCount, 0u);
    }

    void ExpectExplicitResultMatchesLegacyItems(
        const iCAX::TemplateRuntime::SNeutralModel& Explicit_,
        const iCAX::TemplateRuntime::SNeutralModel& Legacy_, const std::string& Purpose_)
    {
        ASSERT_EQ(Legacy_.Items.size(), Explicit_.Items.size());
        const auto _LegacyPurpose = Purpose_ == "display" ? "display" : "export";
        for (std::size_t _Index = 0; _Index < Legacy_.Items.size(); ++_Index)
        {
            EXPECT_EQ(Legacy_.Items[_Index].Key, Explicit_.Items[_Index].Key);
            EXPECT_EQ(Legacy_.Items[_Index].Representations.at(_LegacyPurpose),
                Explicit_.Items[_Index].Representations.at("result"));
        }
    }
}

TEST(TubeDesignerFinalGeometryMeasurementTest, ReadsDimensionsFromFinalBRepOnly)
{
    const auto _Outer = BRepPrimAPI_MakeBox(
        gp_Pnt(-20.0, -10.0, 0.0), 40.0, 20.0, 1000.0).Shape();
    const auto _Inner = BRepPrimAPI_MakeBox(
        gp_Pnt(-18.5, -8.5, -1.0), 37.0, 17.0, 1002.0).Shape();
    TopoDS_Shape _FinalShape = BRepAlgoAPI_Cut(_Outer, _Inner).Shape();
    for (const auto _Station : { 150.0, 350.0 })
    {
        const auto _Opening = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(0.0, -20.0, _Station), gp_Dir(0.0, 1.0, 0.0)),
            5.0, 40.0).Shape();
        _FinalShape = BRepAlgoAPI_Cut(_FinalShape, _Opening).Shape();
    }

    const auto _Measurement = MeasureFinalPartGeometry(
        _FinalShape, "geometry-only-test", 7);
    ASSERT_TRUE(_Measurement.at("available").To<bool>());
    EXPECT_EQ("final-brep", _Measurement.at("source").To<std::string>());
    EXPECT_NEAR(1000.0, _Measurement.at("length").To<double>(), 0.01);

    const auto _Features = _Measurement.at("features").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(2u, _Features.size());
    const auto _First = _Features[0].To<iCAX::Data::ObjectMap>();
    const auto _Second = _Features[1].To<iCAX::Data::ObjectMap>();
    EXPECT_NEAR(
        200.0,
        _Second.at("station").To<double>() - _First.at("station").To<double>(),
        0.01);
    EXPECT_NEAR(10.0, _First.at("diameter").To<double>(), 0.01);
    EXPECT_NEAR(10.0, _Second.at("diameter").To<double>(), 0.01);
}

TEST(TubeDesignerFinalGeometryMeasurementTest, ReadsRectangularOpeningFromFinalBRepTopology)
{
    const auto _Outer = BRepPrimAPI_MakeBox(
        gp_Pnt(-20.0, -10.0, 0.0), 40.0, 20.0, 1000.0).Shape();
    const auto _Inner = BRepPrimAPI_MakeBox(
        gp_Pnt(-18.5, -8.5, -1.0), 37.0, 17.0, 1002.0).Shape();
    const auto _Opening = BRepPrimAPI_MakeBox(
        gp_Pnt(-4.0, -20.0, 494.0), 8.0, 40.0, 12.0).Shape();
    const auto _FinalShape = BRepAlgoAPI_Cut(
        BRepAlgoAPI_Cut(_Outer, _Inner).Shape(), _Opening).Shape();

    const auto _Measurement = MeasureFinalPartGeometry(
        _FinalShape, "rectangular-opening-test", 8);
    ASSERT_TRUE(_Measurement.at("available").To<bool>());
    EXPECT_NEAR(1000.0, _Measurement.at("length").To<double>(), 0.01);

    const auto _Features = _Measurement.at("features").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(1u, _Features.size());
    const auto _Feature = _Features[0].To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("rectangle", _Feature.at("shape").To<std::string>());
    EXPECT_NEAR(500.0, _Feature.at("station").To<double>(), 0.01);
    EXPECT_NEAR(12.0, _Feature.at("openingSpanAlong").To<double>(), 0.01);
    EXPECT_NEAR(8.0, _Feature.at("openingSpanAcross").To<double>(), 0.01);
    EXPECT_GE(_Feature.at("evidenceCount").To<unsigned long long>(), 2u);
}

TEST(TubeDesignerFinalGeometryMeasurementTest, NormalizesManufacturingPartAlongPositiveXAxis)
{
    const auto _Outer = BRepPrimAPI_MakeBox(
        gp_Pnt(-20.0, -10.0, 100.0), 40.0, 20.0, 1000.0).Shape();
    const auto _Inner = BRepPrimAPI_MakeBox(
        gp_Pnt(-18.5, -8.5, 99.0), 37.0, 17.0, 1002.0).Shape();
    const auto _Normalized = NormalizeLinearPartForManufacturing(
        BRepAlgoAPI_Cut(_Outer, _Inner).Shape());
    const auto _Measurement = MeasureFinalPartGeometry(
        _Normalized, "normalized-manufacturing-test", 1);
    ASSERT_TRUE(_Measurement.at("available").To<bool>());
    EXPECT_NEAR(1000.0, _Measurement.at("length").To<double>(), 0.01);
    const auto _Reference = _Measurement.at("linearReference").To<iCAX::Data::ObjectMap>();
    const auto _Start = _Reference.at("start").To<iCAX::Data::VariantArray>();
    const auto _End = _Reference.at("end").To<iCAX::Data::VariantArray>();
    EXPECT_NEAR(-500.0, _Start[0].To<double>(), 0.01);
    EXPECT_NEAR(500.0, _End[0].To<double>(), 0.01);
    for (const auto _Coordinate : { 1u, 2u })
    {
        EXPECT_NEAR(0.0, _Start[_Coordinate].To<double>(), 0.01);
        EXPECT_NEAR(0.0, _End[_Coordinate].To<double>(), 0.01);
    }
}

TEST(TubeDesignerFinalGeometryMeasurementTest, ExtractsPlanarTrapezoidEndProjections)
{
    BRepBuilderAPI_MakePolygon _Polygon;
    _Polygon.Add(gp_Pnt(0.0, 0.0, 0.0));
    _Polygon.Add(gp_Pnt(100.0, 0.0, 0.0));
    _Polygon.Add(gp_Pnt(110.0, 20.0, 0.0));
    _Polygon.Add(gp_Pnt(-10.0, 20.0, 0.0));
    _Polygon.Close();
    const auto _Face = BRepBuilderAPI_MakeFace(_Polygon.Wire()).Face();
    const auto _Shape = BRepPrimAPI_MakePrism(_Face, gp_Vec(0.0, 0.0, 10.0)).Shape();

    const auto _Measured = MeasureLinearNestingGeometry(
        NormalizeLinearPartForManufacturing(_Shape));

    ASSERT_TRUE(_Measured.IsReliable);
    EXPECT_TRUE(_Measured.Left.IsPlanar);
    EXPECT_TRUE(_Measured.Right.IsPlanar);
    EXPECT_NEAR(_Measured.EnvelopeLength, 120.0, 0.02);
    EXPECT_NEAR(_Measured.Left.Projection, 10.0, 0.02);
    EXPECT_NEAR(_Measured.Right.Projection, 10.0, 0.02);
    EXPECT_NEAR(_Measured.MaterialEquivalentLength, 110.0, 0.02);
    EXPECT_GT(std::hypot(_Measured.Left.GradientY, _Measured.Left.GradientZ), 0.1);
    EXPECT_GT(std::hypot(_Measured.Right.GradientY, _Measured.Right.GradientZ), 0.1);
}

TEST(TubeDesignerFinalGeometryMeasurementTest, RoundTripsLocatedFinalPartBRep)
{
    gp_Trsf _Placement;
    _Placement.SetTranslation(gp_Vec(125.0, -80.0, 310.0));
    const auto _Located = BRepPrimAPI_MakeBox(40.0, 20.0, 1000.0).Shape().Moved(
        TopLoc_Location(_Placement));
    const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
        _Located, "located-final-part", "located-final-part", 0.001);

    ASSERT_FALSE(_BRep.Wires.empty());
    for (const auto& _Wire : _BRep.Wires)
    {
        ASSERT_FALSE(_Wire.Coedges.empty());
        for (const auto& _Coedge : _Wire.Coedges) EXPECT_NE(0u, _Coedge.EdgeId);
    }

    const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_BRep);
    std::string _Diagnostics;
    for (const auto& _Item : _Rebuilt.Diagnostics) _Diagnostics += _Item + " | ";
    ASSERT_TRUE(_Rebuilt.bOK) << _Diagnostics;
    ASSERT_FALSE(_Rebuilt.Shape.IsNull());

    Bnd_Box _Bounds;
    BRepBndLib::Add(_Rebuilt.Shape, _Bounds);
    double _XMin = 0.0, _YMin = 0.0, _ZMin = 0.0;
    double _XMax = 0.0, _YMax = 0.0, _ZMax = 0.0;
    _Bounds.Get(_XMin, _YMin, _ZMin, _XMax, _YMax, _ZMax);
    EXPECT_NEAR(125.0, _XMin, 0.01);
    EXPECT_NEAR(-80.0, _YMin, 0.01);
    EXPECT_NEAR(310.0, _ZMin, 0.01);
    EXPECT_NEAR(165.0, _XMax, 0.01);
    EXPECT_NEAR(-60.0, _YMax, 0.01);
    EXPECT_NEAR(1310.0, _ZMax, 0.01);
}

TEST(TubeDesignerPartListXlsxExporterTest, WritesAnExcelWorkbookWithSelectedPartRows)
{
    char* _RequestedOutputBuffer = nullptr;
    std::size_t _RequestedOutputLength = 0;
    (void)_dupenv_s(
        &_RequestedOutputBuffer, &_RequestedOutputLength,
        "ICAX_TUBE_DESIGNER_XLSX_TEST_OUTPUT");
    const std::unique_ptr<char, decltype(&std::free)> _RequestedOutput(
        _RequestedOutputBuffer, &std::free);
    const auto _KeepOutput = _RequestedOutput && _RequestedOutputLength > 1;
    const auto _Target = _KeepOutput
        ? std::filesystem::path(_RequestedOutput.get())
        : std::filesystem::temp_directory_path()
            / ("icax-tube-designer-parts-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".xlsx");
    const std::vector<iCAX::TubeDesigner::SPartListRow> _Rows{
        { 1, "单面防盗窗 001", "FD-001", 1, "FD-001-001", "左外框",
            "矩形管", "40 × 20 × R3 × 1.5", 1800.0, 1, "FD-001-001.step",
            "单面防盗窗 001/FD-001-001.step" },
        { 1, "单面防盗窗 001", "FD-001", 2, "FD-001-002", "中间竖管",
            "圆管", "Φ19 × 1", 1740.0, 2, "FD-001-002.step",
            "单面防盗窗 001/FD-001-002.step" },
        { 1, "单面防盗窗 001", "FD-001", 3, "FD-001-003", "中间封板",
            "板件", "300 × 600 × 2 mm", 600.0, 1, "FD-001-003.step",
            "单面防盗窗 001/FD-001-003.step", "plate", 300.0, 600.0, 2.0, "304" }
    };

    ASSERT_NO_THROW(iCAX::TubeDesigner::WritePartListWorkbook(_Target, _Rows));
    std::ifstream _Stream(_Target, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(_Stream));
    const std::string _Content{
        std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>()
    };
    if (!_KeepOutput)
    {
        std::error_code _RemoveError;
        std::filesystem::remove(_Target, _RemoveError);
    }

    ASSERT_GE(_Content.size(), 22u);
    EXPECT_EQ("PK\x03\x04", _Content.substr(0, 4));
    EXPECT_NE(std::string::npos, _Content.find("xl/worksheets/sheet1.xml"));
    EXPECT_NE(std::string::npos, _Content.find("TubeDesigner 零件清单"));
    EXPECT_NE(std::string::npos, _Content.find("FD-001-002.step"));
    EXPECT_NE(std::string::npos, _Content.find("板厚 (mm)"));
    EXPECT_NE(std::string::npos, _Content.find("r=\"N5\" s=\"4\"><v>300</v>"));
    EXPECT_NE(std::string::npos, _Content.find("r=\"P5\" s=\"4\"><v>2</v>"));
    EXPECT_EQ(std::string("PK\x05\x06", 4), _Content.substr(_Content.size() - 22, 4));
}

TEST(TemplateRuntimeTest, NeutralModelCodecAndOpenCascadeAdapterExecuteGenericTubeGraph)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    VariantArray _Geometry{
        ObjectMap{
            { "key", std::string("profile") },
            { "operator", std::string("profile2d") },
            { "inputs", VariantArray{} },
            { "arguments", RoundedProfile(40.0, 20.0, 3.0) }
        },
        ObjectMap{
            { "key", std::string("solid.final") },
            { "operator", std::string("extrude") },
            { "inputs", VariantArray{ std::string("profile") } },
            { "arguments", ObjectMap{ { "vector", NumberArray({ 0.0, 0.0, 100.0 }) } } }
        }
    };
    const ObjectMap _Document{
        { "schema", std::string("icax.neutral-model") },
        { "schemaVersion", 1ull },
        { "template", ObjectMap{
            { "id", std::string("test.generic") },
            { "version", std::string("1.0.0") },
            { "packageDigest", std::string("test") }
        } },
        { "coordinateSystem", std::string("right-handed-x-width-y-depth-z-height") },
        { "lengthUnit", std::string("mm") },
        { "parameters", ObjectMap{} },
        { "geometry", _Geometry },
        { "items", VariantArray{ ObjectMap{
            { "key", std::string("part.0001") },
            { "displayName", std::string("Generic part") },
            { "representations", ObjectMap{
                { "display", std::string("solid.final") },
                { "export", std::string("solid.final") }
            } }
        } } },
        { "outputs", VariantArray{ ObjectMap{
            { "key", std::string("display.default") },
            { "purpose", std::string("display") },
            { "items", VariantArray{ std::string("part.0001") } }
        } } }
    };

    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document);
    const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    EXPECT_EQ(2u, _Evaluation.Geometry.size());
    EXPECT_FALSE(_Evaluation.At("solid.final").IsNull());
    std::size_t _MaximumWireCount = 0;
    for (TopExp_Explorer _Faces(_Evaluation.At("solid.final"), TopAbs_FACE);
        _Faces.More(); _Faces.Next())
    {
        std::size_t _WireCount = 0;
        for (TopExp_Explorer _Wires(_Faces.Current(), TopAbs_WIRE);
            _Wires.More(); _Wires.Next())
        {
            ++_WireCount;
        }
        _MaximumWireCount = std::max(_MaximumWireCount, _WireCount);
    }
    EXPECT_EQ(2u, _MaximumWireCount);
}

TEST(TemplateRuntimeTest, NeutralModelSelectedRootsKeepDisplayAndManufacturingBranchesSeparate)
{
    const auto _Model = RootSelectionTestModel();
    const auto _Display = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "display.final" });
    const auto _Export = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "export.final" });
    const auto _Complete = iCAX::OpenCascade::EvaluateNeutralModel(_Model);

    ASSERT_EQ(3u, _Display.Geometry.size());
    EXPECT_TRUE(_Display.Geometry.contains("shared.profile"));
    EXPECT_TRUE(_Display.Geometry.contains("shared.solid"));
    EXPECT_FALSE(_Display.Geometry.contains("export.profile"));
    EXPECT_FALSE(_Display.Geometry.contains("export.solid"));
    EXPECT_FALSE(_Display.Geometry.contains("export.final"));
    ASSERT_EQ(5u, _Export.Geometry.size());
    EXPECT_FALSE(_Export.Geometry.contains("display.final"));
    EXPECT_TRUE(_Export.Geometry.contains("shared.solid"));
    EXPECT_TRUE(_Export.Geometry.contains("export.profile"));
    EXPECT_EQ(_Model.Geometry.size(), _Complete.Geometry.size());
    ExpectRootSelectionGeometryMatches(_Display.At("display.final"), _Complete.At("display.final"));
    ExpectRootSelectionGeometryMatches(_Export.At("export.final"), _Complete.At("export.final"));
}

TEST(TemplateRuntimeTest, NeutralModelSelectedRootsDoNotExecuteAnUnselectedUnsupportedBranch)
{
    // Sweep is a valid neutral-model operator, but this OCC adapter does not implement it.
    const auto _Model = RootSelectionTestModel(true);
    EXPECT_NO_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "display.final" }));
    EXPECT_NO_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "export.final" }));
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "unused.unsupported" }), std::invalid_argument);
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(_Model), std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelExecutesBooleanWhenExplicitlySelectedByDisplayOutput)
{
    auto _Model = RootSelectionTestModel();
    _Model.Geometry.push_back({ "display.boolean", iCAX::TemplateRuntime::EGeometryOperator::Boolean,
        { "export.solid", "shared.solid" }, {
            { "operation", std::string("subtract") }, { "target", std::string("export.solid") },
            { "tools", iCAX::Data::VariantArray{ std::string("shared.solid") } }
        } });
    _Model.Items[0].Representations["display"] = std::string("display.boolean");
    // Bare display geometry is a product-template decision. The generic C++
    // evaluator must execute any explicitly selected Boolean, including display.
    const auto _Display = EvaluateTemplatePurposeGeometry(_Model, "display");
    ASSERT_EQ(5u, _Display.Geometry.size());
    EXPECT_TRUE(_Display.Geometry.contains("display.boolean"));
    EXPECT_TRUE(_Display.Geometry.contains("export.solid"));
    EXPECT_TRUE(_Display.Geometry.contains("shared.solid"));
    EXPECT_FALSE(_Display.Geometry.contains("display.final"));
    EXPECT_FALSE(_Display.Geometry.contains("export.final"));
    EXPECT_NEAR(280000.0, RootSelectionShapeVolume(_Display.At("display.boolean")), 1.0e-5);
    const auto _Complete = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    ExpectRootSelectionGeometryMatches(_Display.At("display.boolean"), _Complete.At("display.boolean"));
}

TEST(TemplateRuntimeTest, NeutralModelSelectedRootsReuseSharedShapeAndDuplicateRoots)
{
    const auto _Model = RootSelectionTestModel();
    const auto _Result = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "display.final", "export.final", "display.final", "shared.solid" });
    EXPECT_EQ(6u, _Result.Geometry.size());
    TopoDS_Iterator _DisplayChild(_Result.At("display.final"));
    TopoDS_Iterator _ExportChild(_Result.At("export.final"));
    ASSERT_TRUE(_DisplayChild.More());
    ASSERT_TRUE(_ExportChild.More());
    EXPECT_TRUE(_DisplayChild.Value().IsSame(_Result.At("shared.solid")));
    EXPECT_TRUE(_ExportChild.Value().IsSame(_Result.At("shared.solid")));
    EXPECT_TRUE(_DisplayChild.Value().IsSame(_ExportChild.Value()));
}

TEST(TemplateRuntimeTest, NeutralModelSelectedRootsResolveBooleanArgumentOnlyDependencies)
{
    const auto _Document = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
        "schema":"icax.neutral-model", "schemaVersion":1,
        "template":{"id":"test.boolean-roots","version":"1.0.0","packageDigest":"test"},
        "geometry":[
            {"key":"cut.final","operator":"boolean","inputs":[],
                "arguments":{"operation":"subtract","target":"target.solid","tools":["tool.solid"]}},
            {"key":"target.profile","operator":"profile2d","arguments":{
                "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                "contours":[{"kind":"roundedRectangle","width":40,"height":20,"radius":0}]}},
            {"key":"target.solid","operator":"extrude","inputs":["target.profile"],
                "arguments":{"vector":[0,0,100]}},
            {"key":"tool.profile","operator":"profile2d","arguments":{
                "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                "contours":[{"kind":"roundedRectangle","width":80,"height":80,"radius":0}]}},
            {"key":"tool.solid","operator":"extrude","inputs":["tool.profile"],
                "arguments":{"vector":[0,0,40]}},
            {"key":"unrelated.profile","operator":"profile2d","arguments":{
                "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                "contours":[{"kind":"circle","radius":3}]}}
        ]
    })json");
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document);
    const auto _Result = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "cut.final" });
    ASSERT_EQ(5u, _Result.Geometry.size());
    EXPECT_TRUE(_Result.Geometry.contains("target.profile"));
    EXPECT_TRUE(_Result.Geometry.contains("target.solid"));
    EXPECT_TRUE(_Result.Geometry.contains("tool.profile"));
    EXPECT_TRUE(_Result.Geometry.contains("tool.solid"));
    EXPECT_FALSE(_Result.Geometry.contains("unrelated.profile"));
    EXPECT_NEAR(48000.0, RootSelectionShapeVolume(_Result.At("cut.final")), 0.001);
    const auto _Complete = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    ExpectRootSelectionGeometryMatches(_Result.At("cut.final"), _Complete.At("cut.final"));
}

TEST(TemplateRuntimeTest, NeutralModelSelectedEmptyRootsEvaluateNothingAndUnknownRootsFail)
{
    const auto _Model = RootSelectionTestModel(true);
    const auto _Empty = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{});
    EXPECT_TRUE(_Empty.Geometry.empty());
    EXPECT_THROW((void)_Empty.At("display.final"), std::out_of_range);
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "missing.root" }), std::invalid_argument);
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "" }), std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelSelectedRootsRejectBooleanArgumentDependencyCycles)
{
    auto _Model = RootSelectionTestModel();
    // Argument-only edges are resolved by the adapter, not the codec's inputs graph.
    _Model.Geometry.push_back({ "cycle.a", iCAX::TemplateRuntime::EGeometryOperator::Boolean, {},
        { { "operation", std::string("subtract") }, { "target", std::string("cycle.b") },
          { "tools", iCAX::Data::VariantArray{ std::string("shared.solid") } } } });
    _Model.Geometry.push_back({ "cycle.b", iCAX::TemplateRuntime::EGeometryOperator::Boolean, {},
        { { "operation", std::string("subtract") }, { "target", std::string("cycle.a") },
          { "tools", iCAX::Data::VariantArray{ std::string("shared.solid") } } } });
    EXPECT_NO_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "display.final" }));
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, std::vector<std::string>{ "cycle.a" }), std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelRigidInstancesShareOneProfileAndExtrusion)
{
    const auto _Model = RigidInstanceTestModel();
    const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, { "display.first", "export.second", "display.first" });
    ASSERT_EQ(4u, _Evaluation.Geometry.size());
    std::size_t _Profiles = 0, _Extrusions = 0;
    for (const auto& _Node : _Model.Geometry)
    {
        if (!_Evaluation.Geometry.contains(_Node.Key)) continue;
        if (_Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Profile2D) ++_Profiles;
        if (_Node.Operator == iCAX::TemplateRuntime::EGeometryOperator::Extrude) ++_Extrusions;
    }
    EXPECT_EQ(1u, _Profiles);
    EXPECT_EQ(1u, _Extrusions);
    const auto& _Prototype = _Evaluation.At("shared.solid");
    const auto& _First = _Evaluation.At("display.first");
    const auto& _Second = _Evaluation.At("export.second");
    EXPECT_TRUE(_Prototype.IsPartner(_First));
    EXPECT_TRUE(_Prototype.IsPartner(_Second));
    EXPECT_TRUE(_First.IsPartner(_Second));
    EXPECT_FALSE(_First.IsSame(_Second));
    EXPECT_FALSE(_First.Location().IsEqual(_Second.Location()));
    EXPECT_TRUE(_Prototype.Location().IsIdentity());
    ExpectRigidShapeBounds(_Prototype, { -20, -10, 0, 20, 10, 100 });
    ExpectRigidShapeBounds(_First, { 105, -90, 310, 145, -70, 410 });
    ExpectRigidShapeBounds(_Second, { -200, 30, 15, -100, 70, 35 });
    for (const auto* _Shape : { &_Prototype, &_First, &_Second })
        ExpectUnmodifiedBoxPrototype(*_Shape);
}

TEST(TemplateRuntimeTest, NeutralModelRigidTransformsComposeLocationsWithoutMovingPartners)
{
    auto _Model = RigidInstanceTestModel();
    _Model.Geometry.push_back({ "display.chained", iCAX::TemplateRuntime::EGeometryOperator::Transform,
        { "display.first" }, { { "placement", iCAX::Data::ObjectMap{
            { "origin", NumberArray({ 0, 0, 0 }) },
            { "xAxis", NumberArray({ 0, 1, 0 }) },
            { "yAxis", NumberArray({ -1, 0, 0 }) },
            { "zAxis", NumberArray({ 0, 0, 1 }) }
        } } } });
    const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, { "display.chained", "export.second" });
    ASSERT_EQ(5u, _Evaluation.Geometry.size());
    const auto& _Chained = _Evaluation.At("display.chained");
    EXPECT_TRUE(_Chained.IsPartner(_Evaluation.At("shared.solid")));
    ExpectRigidShapeBounds(_Chained, { 70, 105, 310, 90, 145, 410 });
    ExpectRigidShapeBounds(_Evaluation.At("display.first"), { 105, -90, 310, 145, -70, 410 });
    ExpectRigidShapeBounds(_Evaluation.At("export.second"), { -200, 30, 15, -100, 70, 35 });
    ExpectRigidShapeBounds(_Evaluation.At("shared.solid"), { -20, -10, 0, 20, 10, 100 });
}

TEST(TemplateRuntimeTest, NeutralModelRigidInstanceBooleansDoNotAlterSharedPrototypeOrPartner)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    using iCAX::TemplateRuntime::EGeometryOperator;
    for (const auto& [_Operation, _ExpectedVolume] : std::array{
        std::pair{ "subtract", 78080.0 }, std::pair{ "union", 81920.0 }, std::pair{ "intersect", 1920.0 } })
    {
        SCOPED_TRACE(_Operation);
        auto _Model = RigidInstanceTestModel();
        // The tool crosses the first instance only: intersection = 8 * 20 * 12.
        _Model.Geometry.push_back({ "tool.profile", EGeometryOperator::Profile2D, {}, {
            { "placement", ObjectMap{
                { "origin", NumberArray({ 125, -80, 350 }) },
                { "xAxis", NumberArray({ 1, 0, 0 }) }, { "yAxis", NumberArray({ 0, 1, 0 }) }
            } },
            { "contours", VariantArray{ ObjectMap{
                { "kind", std::string("roundedRectangle") },
                { "width", 8.0 }, { "height", 40.0 }, { "radius", 0.0 }
            } } }
        } });
        _Model.Geometry.push_back({ "tool.solid", EGeometryOperator::Extrude, { "tool.profile" },
            { { "vector", NumberArray({ 0, 0, 12 }) } } });
        _Model.Geometry.push_back({ "machined.first", EGeometryOperator::Boolean,
            { "display.first", "tool.solid" }, {
                { "operation", std::string(_Operation) }, { "target", std::string("display.first") },
                { "tools", VariantArray{ std::string("tool.solid") } }
            } });
        const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(
            _Model, { "export.second", "machined.first" });
        const auto& _Prototype = _Evaluation.At("shared.solid");
        const auto& _First = _Evaluation.At("display.first");
        const auto& _Partner = _Evaluation.At("export.second");
        const auto& _Machined = _Evaluation.At("machined.first");
        EXPECT_NEAR(_ExpectedVolume, RootSelectionShapeVolume(_Machined), 1.0e-5);
        EXPECT_FALSE(_Machined.IsPartner(_Prototype));
        EXPECT_TRUE(_Prototype.IsPartner(_First));
        EXPECT_TRUE(_Prototype.IsPartner(_Partner));
        ExpectRigidShapeBounds(_Prototype, { -20, -10, 0, 20, 10, 100 });
        ExpectRigidShapeBounds(_First, { 105, -90, 310, 145, -70, 410 });
        ExpectRigidShapeBounds(_Partner, { -200, 30, 15, -100, 70, 35 });
        for (const auto* _Shape : { &_Prototype, &_First, &_Partner })
            ExpectUnmodifiedBoxPrototype(*_Shape);
    }
}

TEST(TemplateRuntimeTest, NeutralModelRigidTransformRejectsNonRigidOrMalformedPlacements)
{
    using iCAX::Data::ObjectMap;
    const auto _ValidPlacement = RigidInstanceTestModel().Geometry[2].Arguments.at("placement").To<ObjectMap>();
    const auto _ExpectRejected = [](const char* Label_, const ObjectMap& Placement_)
    {
        SCOPED_TRACE(Label_);
        auto _Model = RigidInstanceTestModel();
        _Model.Geometry[2].Arguments["placement"] = Placement_;
        EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
            _Model, { "display.first" }), std::invalid_argument);
    };
    auto _Placement = _ValidPlacement;
    _Placement["xAxis"] = NumberArray({ 2, 0, 0 });
    _ExpectRejected("scaled axis", _Placement);
    _Placement = _ValidPlacement;
    _Placement["zAxis"] = NumberArray({ 0, 0, -1 });
    _ExpectRejected("mirror", _Placement);
    _Placement = _ValidPlacement;
    _Placement["yAxis"] = NumberArray({ 0.6, 0.8, 0 });
    _ExpectRejected("unit but non-orthogonal axes", _Placement);
    _Placement = _ValidPlacement;
    _Placement["xAxis"] = NumberArray({ 0, 0, 0 });
    _ExpectRejected("zero axis", _Placement);
    _Placement = _ValidPlacement;
    _Placement["origin"] = NumberArray({ std::numeric_limits<double>::quiet_NaN(), 0, 0 });
    _ExpectRejected("non-finite origin", _Placement);
    _Placement = _ValidPlacement;
    _Placement["zAxis"] = NumberArray({ 0, 0, std::numeric_limits<double>::infinity() });
    _ExpectRejected("non-finite axis", _Placement);
    _Placement = _ValidPlacement;
    _Placement["origin"] = NumberArray({ 0, 0 });
    _ExpectRejected("short origin", _Placement);
    _Placement = _ValidPlacement;
    _Placement["xAxis"] = NumberArray({ 1, 0, 0, 0 });
    _ExpectRejected("long axis", _Placement);
    for (const auto* _Field : { "origin", "xAxis", "yAxis", "zAxis" })
    {
        _Placement = _ValidPlacement;
        _Placement.erase(_Field);
        _ExpectRejected(_Field, _Placement);
    }
    auto _Model = RigidInstanceTestModel();
    _Model.Geometry[2].Arguments.clear();
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, { "display.first" }), std::invalid_argument);
    _Model = RigidInstanceTestModel();
    _Model.Geometry[2].Inputs.clear();
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, { "display.first" }), std::invalid_argument);
    _Model.Geometry[2].Inputs = { "shared.solid", "shared.solid" };
    EXPECT_THROW((void)iCAX::OpenCascade::EvaluateNeutralModel(
        _Model, { "display.first" }), std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelRigidTransformSelectionKeepsPurposeDependenciesSeparate)
{
    const auto _Model = RigidInstanceTestModel();
    const auto& _DisplayKey = _Model.Items[0].Representations.at("display");
    const auto& _ExportKey = _Model.Items[1].Representations.at("export");
    const auto _Display = iCAX::OpenCascade::EvaluateNeutralModel(_Model, { _DisplayKey });
    const auto _Export = iCAX::OpenCascade::EvaluateNeutralModel(_Model, { _ExportKey });
    ASSERT_EQ(3u, _Display.Geometry.size());
    ASSERT_EQ(3u, _Export.Geometry.size());
    EXPECT_FALSE(_Display.Geometry.contains(_ExportKey));
    EXPECT_FALSE(_Export.Geometry.contains(_DisplayKey));
    for (const auto* _Result : { &_Display, &_Export })
    {
        EXPECT_TRUE(_Result->Geometry.contains("shared.profile"));
        EXPECT_TRUE(_Result->Geometry.contains("shared.solid"));
    }
    const auto _Complete = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    ExpectRootSelectionGeometryMatches(_Display.At(_DisplayKey), _Complete.At(_DisplayKey));
    ExpectRootSelectionGeometryMatches(_Export.At(_ExportKey), _Complete.At(_ExportKey));
}

TEST(TemplateRuntimeTest, NeutralModelRigidInstancesRoundTripBRepInWorldCoordinates)
{
    const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(RigidInstanceTestModel());
    for (const auto* _Key : { "display.first", "export.second" })
    {
        SCOPED_TRACE(_Key);
        const auto& _Located = _Evaluation.At(_Key);
        const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
            _Located, _Key, _Key, 0.001);
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_BRep);
        std::string _Diagnostics;
        for (const auto& _Item : _Rebuilt.Diagnostics) _Diagnostics += _Item + " | ";
        ASSERT_TRUE(_Rebuilt.bOK) << _Diagnostics;
        ExpectRootSelectionGeometryMatches(_Rebuilt.Shape, _Located);
        ExpectUnmodifiedBoxPrototype(_Rebuilt.Shape);
    }
    // Meshing/serialization of one located wrapper must not relocate its partners.
    ExpectRigidShapeBounds(_Evaluation.At("shared.solid"), { -20, -10, 0, 20, 10, 100 });
    ExpectRigidShapeBounds(_Evaluation.At("display.first"), { 105, -90, 310, 145, -70, 410 });
    ExpectRigidShapeBounds(_Evaluation.At("export.second"), { -200, 30, 15, -100, 70, 35 });
}

TEST(TemplateRuntimeTest, PathProfileSupportsAnalyticAndSplineCurveSegments)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Evaluate = [](const ObjectMap& Contour_)
    {
        const ObjectMap _Document{
            { "schema", std::string("icax.neutral-model") },
            { "schemaVersion", 1ull },
            { "template", ObjectMap{
                { "id", std::string("test.path-profile") },
                { "version", std::string("1.0.0") },
                { "packageDigest", std::string("test") }
            } },
            { "geometry", VariantArray{
                ObjectMap{
                    { "key", std::string("profile") },
                    { "operator", std::string("profile2d") },
                    { "arguments", ObjectMap{
                        { "placement", ObjectMap{
                            { "origin", NumberArray({ 0.0, 0.0, 0.0 }) },
                            { "xAxis", NumberArray({ 1.0, 0.0, 0.0 }) },
                            { "yAxis", NumberArray({ 0.0, 1.0, 0.0 }) }
                        } },
                        { "contours", VariantArray{ Contour_ } }
                    } }
                },
                ObjectMap{
                    { "key", std::string("solid") },
                    { "operator", std::string("extrude") },
                    { "inputs", VariantArray{ std::string("profile") } },
                    { "arguments", ObjectMap{
                        { "vector", NumberArray({ 0.0, 0.0, 100.0 }) }
                    } }
                }
            } }
        };
        return iCAX::OpenCascade::EvaluateNeutralModel(
            iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document));
    };
    const auto _VerifyBRepRoundTrip = [](const auto& Evaluation_, const std::string& Label_)
    {
        const auto& _Shape = Evaluation_.At("solid");
        const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
            _Shape, Label_, Label_, 0.001);
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_BRep);
        std::string _Diagnostics;
        for (const auto& _Item : _Rebuilt.Diagnostics) _Diagnostics += _Item + " | ";
        EXPECT_TRUE(_Rebuilt.bOK) << Label_ << ": " << _Diagnostics;
        EXPECT_FALSE(_Rebuilt.Shape.IsNull()) << Label_;
    };

    const ObjectMap _MixedPath{
        { "kind", std::string("path") },
        { "segments", VariantArray{
            ObjectMap{
                { "kind", std::string("line") },
                { "start", NumberArray({ -40.0, -30.0 }) },
                { "end", NumberArray({ 40.0, -30.0 }) }
            },
            ObjectMap{
                { "kind", std::string("arc") },
                { "start", NumberArray({ 40.0, -30.0 }) },
                { "middle", NumberArray({ 50.0, 0.0 }) },
                { "end", NumberArray({ 40.0, 30.0 }) }
            },
            ObjectMap{
                { "kind", std::string("line") },
                { "start", NumberArray({ 40.0, 30.0 }) },
                { "end", NumberArray({ -40.0, 30.0 }) }
            },
            ObjectMap{
                { "kind", std::string("ellipseArc") },
                { "center", NumberArray({ -40.0, 0.0 }) },
                { "majorRadius", 30.0 },
                { "minorRadius", 10.0 },
                { "rotation", std::acos(-1.0) / 2.0 },
                { "startAngle", 0.0 },
                { "endAngle", std::acos(-1.0) }
            }
        } }
    };
    const auto _MixedEvaluation = _Evaluate(_MixedPath);
    EXPECT_FALSE(_MixedEvaluation.At("solid").IsNull());
    _VerifyBRepRoundTrip(_MixedEvaluation, "analytic-path");

    constexpr double _BezierK = 16.5685424949238;
    const ObjectMap _BezierPath{
        { "kind", std::string("path") },
        { "segments", VariantArray{
            ObjectMap{
                { "kind", std::string("bezier") },
                { "controlPoints", VariantArray{
                    NumberArray({ 0.0, -30.0 }), NumberArray({ _BezierK, -30.0 }),
                    NumberArray({ 30.0, -_BezierK }), NumberArray({ 30.0, 0.0 })
                } }
            },
            ObjectMap{
                { "kind", std::string("bezier") },
                { "controlPoints", VariantArray{
                    NumberArray({ 30.0, 0.0 }), NumberArray({ 30.0, _BezierK }),
                    NumberArray({ _BezierK, 30.0 }), NumberArray({ 0.0, 30.0 })
                } }
            },
            ObjectMap{
                { "kind", std::string("bezier") },
                { "controlPoints", VariantArray{
                    NumberArray({ 0.0, 30.0 }), NumberArray({ -_BezierK, 30.0 }),
                    NumberArray({ -30.0, _BezierK }), NumberArray({ -30.0, 0.0 })
                } }
            },
            ObjectMap{
                { "kind", std::string("bezier") },
                { "controlPoints", VariantArray{
                    NumberArray({ -30.0, 0.0 }), NumberArray({ -30.0, -_BezierK }),
                    NumberArray({ -_BezierK, -30.0 }), NumberArray({ 0.0, -30.0 })
                } }
            }
        } }
    };
    const auto _BezierEvaluation = _Evaluate(_BezierPath);
    EXPECT_FALSE(_BezierEvaluation.At("solid").IsNull());
    _VerifyBRepRoundTrip(_BezierEvaluation, "bezier-path");

    const ObjectMap _BSplinePath{
        { "kind", std::string("path") },
        { "segments", VariantArray{ ObjectMap{
            { "kind", std::string("bspline") },
            { "degree", 3 },
            { "controlPoints", VariantArray{
                NumberArray({ 0.0, -30.0 }), NumberArray({ 30.0, -30.0 }),
                NumberArray({ 35.0, 0.0 }), NumberArray({ 0.0, 30.0 }),
                NumberArray({ -35.0, 0.0 }), NumberArray({ -30.0, -30.0 }),
                NumberArray({ 0.0, -30.0 })
            } },
            { "knots", NumberArray({ 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 4.0, 4.0, 4.0 }) }
        } } }
    };
    const auto _BSplineEvaluation = _Evaluate(_BSplinePath);
    EXPECT_FALSE(_BSplineEvaluation.At("solid").IsNull());
    _VerifyBRepRoundTrip(_BSplineEvaluation, "bspline-path");

    const ObjectMap _PeriodicBSplinePath{
        { "kind", std::string("path") },
        { "segments", VariantArray{ ObjectMap{
            { "kind", std::string("bspline") },
            { "degree", 3 },
            { "periodic", true },
            { "controlPoints", VariantArray{
                NumberArray({ 0.0, -35.0 }), NumberArray({ 14.6946, -20.2254 }),
                NumberArray({ 33.2870, -10.8156 }), NumberArray({ 23.7764, 7.7254 }),
                NumberArray({ 20.5725, 28.3156 }), NumberArray({ 0.0, 25.0 }),
                NumberArray({ -20.5725, 28.3156 }), NumberArray({ -23.7764, 7.7254 }),
                NumberArray({ -33.2870, -10.8156 }), NumberArray({ -14.6946, -20.2254 })
            } },
            { "knots", NumberArray({
                0.0, 1.0, 2.0, 3.0, 4.0, 5.0,
                6.0, 7.0, 8.0, 9.0, 10.0
            }) },
            { "multiplicities", NumberArray({
                1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                1.0, 1.0, 1.0, 1.0, 1.0
            }) }
        } } }
    };
    const auto _PeriodicBSplineEvaluation = _Evaluate(_PeriodicBSplinePath);
    EXPECT_FALSE(_PeriodicBSplineEvaluation.At("solid").IsNull());
    _VerifyBRepRoundTrip(_PeriodicBSplineEvaluation, "periodic-bspline-path");

    constexpr double _DiagonalWeight = 0.7071067811865476;
    const ObjectMap _NurbsPath{
        { "kind", std::string("path") },
        { "segments", VariantArray{ ObjectMap{
            { "kind", std::string("nurbs") },
            { "degree", 2 },
            { "controlPoints", VariantArray{
                NumberArray({ 30.0, 0.0 }), NumberArray({ 30.0, 30.0 }),
                NumberArray({ 0.0, 30.0 }), NumberArray({ -30.0, 30.0 }),
                NumberArray({ -30.0, 0.0 }), NumberArray({ -30.0, -30.0 }),
                NumberArray({ 0.0, -30.0 }), NumberArray({ 30.0, -30.0 }),
                NumberArray({ 30.0, 0.0 })
            } },
            { "weights", NumberArray({
                1.0, _DiagonalWeight, 1.0, _DiagonalWeight, 1.0,
                _DiagonalWeight, 1.0, _DiagonalWeight, 1.0
            }) },
            { "knots", NumberArray({ 0.0, 1.0, 2.0, 3.0, 4.0 }) },
            { "multiplicities", NumberArray({ 3.0, 2.0, 2.0, 2.0, 3.0 }) }
        } } }
    };
    const auto _NurbsEvaluation = _Evaluate(_NurbsPath);
    EXPECT_FALSE(_NurbsEvaluation.At("solid").IsNull());
    _VerifyBRepRoundTrip(_NurbsEvaluation, "nurbs-path");

    const ObjectMap _DisconnectedPath{
        { "kind", std::string("path") },
        { "segments", VariantArray{
            ObjectMap{
                { "kind", std::string("line") },
                { "start", NumberArray({ 0.0, 0.0 }) },
                { "end", NumberArray({ 10.0, 0.0 }) }
            },
            ObjectMap{
                { "kind", std::string("line") },
                { "start", NumberArray({ 20.0, 0.0 }) },
                { "end", NumberArray({ 20.0, 10.0 }) }
            }
        } }
    };
    EXPECT_THROW((void)_Evaluate(_DisconnectedPath), std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelRequiresTemplateObject)
{
    const iCAX::Data::ObjectMap _Document{
        { "schema", std::string("icax.neutral-model") },
        { "schemaVersion", 1ull },
        { "geometry", iCAX::Data::VariantArray{} }
    };
    EXPECT_THROW(
        (void)iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document),
        std::invalid_argument);
}

TEST(TemplateRuntimeTest, NeutralModelRejectsKeysThatCouldBecomeResourcePaths)
{
    const iCAX::Data::ObjectMap _Document{
        { "schema", std::string("icax.neutral-model") },
        { "schemaVersion", 1ull },
        { "template", iCAX::Data::ObjectMap{
            { "id", std::string("test.generic") },
            { "version", std::string("1.0.0") }
        } },
        { "geometry", iCAX::Data::VariantArray{ iCAX::Data::ObjectMap{
            { "key", std::string("../outside") },
            { "operator", std::string("compound") },
            { "inputs", iCAX::Data::VariantArray{} }
        } } }
    };
    EXPECT_THROW(
        (void)iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document),
        std::invalid_argument);
}

TEST(TemplateRuntimeTest, DescriptorRejectsAConditionThatReferencesAnUnknownParameter)
{
    constexpr auto _Descriptor = R"json({
        "schema":"icax.template-descriptor",
        "schemaVersion":1,
        "id":"test.condition",
        "version":"1.0.0",
        "displayName":"Condition test",
        "parameters":[{
            "key":"enabled",
            "displayName":"Enabled",
            "valueType":"boolean",
            "defaultValue":true,
            "visibleWhen":{"op":"eq","parameter":"missing","value":true}
        }]
    })json";
    EXPECT_THROW(
        (void)iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_Descriptor)),
        std::invalid_argument);
}

namespace
{
    iCAX::TemplateRuntime::STemplateDescriptor NumericEnumFixture()
    {
        return iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
                "schema":"icax.template-descriptor", "schemaVersion":1,
                "id":"test.numeric-enum", "version":"1.0.0", "displayName":"Numeric enum",
                "parameters":[{
                    "key":"railCount", "displayName":"Rails", "valueType":"enum",
                    "defaultValue":3,
                    "choices":[{"value":3,"displayName":"Three rails"}]
                }]
            })json"));
    }
}

TEST(TemplateRuntimeTest, NumericEnumAcceptsWebBridgeIntAndReturnsDescriptorType)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::TemplateRuntime::CTemplateCodec;
    const auto _Descriptor = NumericEnumFixture();
    ASSERT_TRUE(_Descriptor.Parameters.front().Choices.front().Value.Is<long long>());
    // Exactly the typed wire payload emitted by SDK/SDO/variantSerializer.mjs.
    const auto _BridgeInput = iCAX::Data::VariantSerializer::Deserialize(
        R"({"__variant_type":"Object","value":{"railCount":{"__variant_type":"int","value":3}}})")
        .To<ObjectMap>();
    ASSERT_TRUE(_BridgeInput.at("railCount").Is<int>());
    const auto _Actual = CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor, _BridgeInput);
    ASSERT_TRUE(_Actual.at("railCount").Is<long long>());
    EXPECT_EQ(3LL, _Actual.at("railCount").To<long long>());
    for (const auto& _Input : std::array<Variant, 5>{ 3, 3u, 3ULL, 3.0f, 3.0 })
    {
        const auto _Normalized = CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, ObjectMap{ { "railCount", _Input } });
        EXPECT_EQ(_Descriptor.Parameters.front().Choices.front().Value, _Normalized.at("railCount"));
    }
}

TEST(TemplateRuntimeTest, NumericEnumDoesNotCoerceStringsBooleansOrNearbyValues)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::TemplateRuntime::CTemplateCodec;
    auto _Descriptor = NumericEnumFixture();
    for (const auto& _Input : std::array<Variant, 7>{ std::string("3"), true, false, 3.001, 2,
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
        EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, ObjectMap{ { "railCount", _Input } }), std::invalid_argument);
    _Descriptor.Parameters.front().Choices.front().Value = 1LL;
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, ObjectMap{ { "railCount", true } }), std::invalid_argument);
    _Descriptor.Parameters.front().Choices.front().Value = std::string("3");
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, ObjectMap{ { "railCount", 3 } }), std::invalid_argument);
    EXPECT_EQ(std::string("3"), CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, ObjectMap{ { "railCount", std::string("3") } }).at("railCount").To<std::string>());
}

TEST(TemplateRuntimeTest, NumericEnumKeepsLargeIntegerPrecisionAndSignedBoundaries)
{
    using iCAX::Data::ObjectMap;
    using iCAX::TemplateRuntime::CTemplateCodec;
    auto _Descriptor = NumericEnumFixture();
    auto& _Choice = _Descriptor.Parameters.front().Choices.front().Value;
    _Choice = 9007199254740993LL; // 2^53+1 must not equal rounded double 2^53.
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", 9007199254740992.0 } }), std::invalid_argument);
    EXPECT_EQ(_Choice, CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", 9007199254740993ULL } }).at("railCount"));
    _Choice = std::numeric_limits<unsigned long long>::max();
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", -1LL } }), std::invalid_argument);
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", std::ldexp(1.0, 64) } }), std::invalid_argument);
    _Choice = std::numeric_limits<long long>::min();
    EXPECT_EQ(_Choice, CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", -std::ldexp(1.0, 63) } }).at("railCount"));
    EXPECT_THROW(CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", 1ULL << 63 } }), std::invalid_argument);
    _Choice = 0LL;
    EXPECT_EQ(_Choice, CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", -0.0 } }).at("railCount"));
    _Choice = 0.5;
    EXPECT_EQ(_Choice, CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor,
        ObjectMap{ { "railCount", 0.5f } }).at("railCount"));
}

TEST(TemplateRuntimeTest, NumericEnumRejectsEquivalentDuplicateChoices)
{
    EXPECT_THROW(iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
            "schema":"icax.template-descriptor", "schemaVersion":1,
            "id":"test.duplicate-enum", "version":"1.0.0", "displayName":"Duplicate enum",
            "parameters":[{
                "key":"railCount", "displayName":"Rails", "valueType":"enum", "defaultValue":3,
                "choices":[{"value":3,"displayName":"Integer"},{"value":3.0,"displayName":"Double"}]
            }]
        })json")), std::invalid_argument);
}

TEST(TemplateRuntimeTest, ParameterPresentationPreservesExplicitAndMissingOrder)
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Descriptor = CTemplateCodec::ParseDescriptor(CStandardJsonCodec::Parse(R"json({
        "schema":"icax.template-descriptor", "schemaVersion":1,
        "id":"test.order", "version":"1.0.0", "displayName":"Order",
        "parameters":[
            {"key":"missing","displayName":"Missing","valueType":"string","defaultValue":""},
            {"key":"zero","displayName":"Zero","valueType":"string","defaultValue":"","order":0},
            {"key":"later","displayName":"Later","valueType":"string","defaultValue":"","order":20}
        ]
    })json"));
    EXPECT_FALSE(_Descriptor.Parameters.at(0).Order.has_value());
    EXPECT_EQ(0, _Descriptor.Parameters.at(1).Order.value());
    const auto _Fields = CTemplateCodec::MakePresentationDescriptor(_Descriptor).at("parameters").To<VariantArray>();
    EXPECT_FALSE(_Fields.at(0).To<ObjectMap>().contains("order"));
    EXPECT_EQ(0, _Fields.at(1).To<ObjectMap>().at("order").To<long long>());
    EXPECT_EQ(20, _Fields.at(2).To<ObjectMap>().at("order").To<long long>());
}

TEST(TemplateRuntimeTest, EveryGuardrailCatalogPresetAcceptsBrowserRailCountTypes)
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    const auto _Path = std::filesystem::current_path()
        / "src/apps/tube-designer/templates/modular_guardrail/template.json";
    std::ifstream _Stream(_Path, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(_Stream));
    const std::string _Text{ std::istreambuf_iterator<char>(_Stream), std::istreambuf_iterator<char>() };
    const auto _Descriptor = CTemplateCodec::ParseDescriptor(CStandardJsonCodec::Parse(_Text));
    const auto _Catalog = _Descriptor.Extensions.at("catalog").To<ObjectMap>();
    const auto _Presets = _Catalog.at("presets").To<VariantArray>();
    ASSERT_EQ(20u, _Presets.size());
    for (const auto& _RawPreset : _Presets)
    {
        const auto _Preset = _RawPreset.To<ObjectMap>();
        SCOPED_TRACE(_Preset.at("id").To<std::string>());
        ObjectMap _Parameters;
        for (const auto& _Definition : _Descriptor.Parameters)
            _Parameters[_Definition.Key] = _Definition.DefaultValue;
        for (const auto& [_Key, _Value] : _Preset.at("parameters").To<ObjectMap>())
            _Parameters[_Key] = _Value;
        const auto _Rails = _Parameters.at("railCount").To<long long>();
        _Parameters["railCount"] = static_cast<int>(_Rails);
        auto _Normalized = CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor, _Parameters);
        ASSERT_TRUE(_Normalized.at("railCount").Is<long long>());
        EXPECT_EQ(_Rails, _Normalized.at("railCount").To<long long>());
        _Parameters["railCount"] = static_cast<double>(_Rails);
        _Normalized = CTemplateCodec::ValidateAndNormalizeParameters(_Descriptor, _Parameters);
        EXPECT_EQ(_Rails, _Normalized.at("railCount").To<long long>());
    }
}

TEST(TemplateRuntimeTest, EmbeddedPythonEvaluatesTheRealTemplatePackageWithoutExternalInterpreter)
{
    const auto _Root = std::filesystem::current_path();
    const auto _DescriptorPath = _Root / "src/apps/tube-designer/templates/single_face_security_window/template.json";
    const auto _TemplatePath = _DescriptorPath.parent_path() / "template.py";
    std::ifstream _DescriptorStream(_DescriptorPath, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(_DescriptorStream));
    const std::string _DescriptorText{
        std::istreambuf_iterator<char>(_DescriptorStream), std::istreambuf_iterator<char>()
    };
    auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    _Descriptor.PackageDigest = "test-real-package";
    iCAX::Data::ObjectMap _Parameters;
    for (const auto& _Definition : _Descriptor.Parameters)
        _Parameters[_Definition.Key] = _Definition.DefaultValue;
    EXPECT_TRUE(_Parameters.at("accessDoorEnabled").To<bool>());
    // Fix the legacy two-side-frame/through-hole measurement fixture explicitly;
    // its counts and opening stations must not follow commercial product defaults.
    // Current defaults are exercised with their opening in the purpose-protocol test.
    _Parameters["accessDoorEnabled"] = false;
    _Parameters["frameLayout"] = std::string("left_right");
    _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, _Parameters);

    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    const auto _Response = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
    EXPECT_EQ(_Descriptor.ID, _Model.TemplateID);
    EXPECT_EQ(_Descriptor.Version, _Model.TemplateVersion);
    EXPECT_GT(_Model.Geometry.size(), 35u);
    EXPECT_EQ(15u, _Model.Items.size());
    ASSERT_EQ(2u, _Model.Outputs.size());
    EXPECT_EQ("display", _Model.Outputs.at(0).Purpose);
    EXPECT_EQ(15u, _Model.Outputs.at(0).ItemKeys.size());
    EXPECT_EQ("export", _Model.Outputs.at(1).Purpose);
    EXPECT_EQ(15u, _Model.Outputs.at(1).ItemKeys.size());
    const auto _MainHorizontal = std::find_if(
        _Model.Items.begin(), _Model.Items.end(),
        [](const auto& Item_) { return Item_.Key.starts_with("main_grid.horizontal."); });
    ASSERT_NE(_Model.Items.end(), _MainHorizontal);
    ASSERT_TRUE(_MainHorizontal->Properties.contains("manufacturing.categoryKey"));
    EXPECT_EQ(
        "main_grid.horizontal",
        _MainHorizontal->Properties.at("manufacturing.categoryKey").To<std::string>());
    EXPECT_EQ(
        "主横杆",
        _MainHorizontal->Properties.at("manufacturing.categoryName").To<std::string>());
    ASSERT_TRUE(_MainHorizontal->Properties.contains("tubeDesigner.profile"));
    const auto _Profile = _MainHorizontal->Properties.at("tubeDesigner.profile")
        .To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.tube-profile", _Profile.at("schema").To<std::string>());
    EXPECT_EQ("rect", _Profile.at("id").To<std::string>());
    EXPECT_EQ("2.1.0", _Profile.at("packageVersion").To<std::string>());
    EXPECT_EQ("矩形管", _Profile.at("displayName").To<std::string>());
    EXPECT_FALSE(_Profile.at("specification").To<std::string>().empty());
    const auto _OuterFrameLeft = std::find_if(
        _Model.Items.begin(), _Model.Items.end(),
        [](const auto& Item_) { return Item_.Key.starts_with("outer_frame.left."); });
    ASSERT_NE(_Model.Items.end(), _OuterFrameLeft);
    const auto _OuterFrameGeometryKey = _OuterFrameLeft->Representations.at("export");
    const auto _OuterFrameGeometry = std::find_if(
        _Model.Geometry.begin(), _Model.Geometry.end(),
        [&](const auto& Node_) { return Node_.Key == _OuterFrameGeometryKey; });
    ASSERT_NE(_Model.Geometry.end(), _OuterFrameGeometry);
    EXPECT_EQ(iCAX::TemplateRuntime::EGeometryOperator::Boolean, _OuterFrameGeometry->Operator);
    EXPECT_EQ(5u, _OuterFrameGeometry->Inputs.size());
    const auto _PartsTable = std::find_if(_Model.Tables.begin(), _Model.Tables.end(),
        [](const auto& Table_) { return Table_.Key == "parts"; });
    ASSERT_NE(_Model.Tables.end(), _PartsTable);
    EXPECT_EQ(15u, _PartsTable->Rows.size());
    EXPECT_TRUE(std::any_of(_Model.Tables.begin(), _Model.Tables.end(),
        [](const auto& Table_) { return Table_.Key == "security_window_review"; }));
    const auto _DisplayEvaluation = EvaluateTemplatePurposeGeometry(_Model, "display");
    ExpectUnmachinedTemplateDisplay(_Model, _DisplayEvaluation);
    const auto _Evaluation = EvaluateTemplatePurposeGeometry(_Model, "export");
    ExpectManufacturingBooleanIsEvaluated(_Model, _Evaluation);
    EXPECT_EQ(_Model.Geometry.size(), _Evaluation.Geometry.size());
    for (const auto& _Item : _Model.Items)
        EXPECT_FALSE(_Evaluation.At(_Item.Representations.at("display")).IsNull());
    EXPECT_NE(_OuterFrameLeft->Representations.at("display"), _OuterFrameGeometryKey);
    EXPECT_GT(RootSelectionShapeVolume(
        _DisplayEvaluation.At(_OuterFrameLeft->Representations.at("display"))),
        RootSelectionShapeVolume(_Evaluation.At(_OuterFrameGeometryKey)));
    for (const auto& _Item : _Model.Items)
    {
        if (!_Item.Key.starts_with("main_grid.horizontal.")) continue;
        std::size_t _SolidCount = 0;
        for (TopExp_Explorer _Explorer(
                _Evaluation.At(_Item.Representations.at("export")), TopAbs_SOLID);
            _Explorer.More(); _Explorer.Next())
        {
            ++_SolidCount;
        }
        EXPECT_EQ(1u, _SolidCount) << _Item.Key << " was severed by a through cutter";
    }
    const auto _OuterFrameMeasurement = MeasureFinalPartGeometry(
        NormalizeLinearPartForManufacturing(_Evaluation.At(_OuterFrameGeometryKey)),
        "real-template-outer-frame", 1);
    ASSERT_TRUE(_OuterFrameMeasurement.at("available").To<bool>());
    const auto _OuterFrameFeatures =
        _OuterFrameMeasurement.at("features").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(4u, _OuterFrameFeatures.size());
    EXPECT_NEAR(1800.0, _OuterFrameMeasurement.at("length").To<double>(), 0.01);
    const std::array _ExpectedOpeningStations{ 200.0, 666.6666667, 1133.3333333, 1600.0 };
    for (std::size_t _Index = 0; _Index < _OuterFrameFeatures.size(); ++_Index)
    {
        const auto _Feature = _OuterFrameFeatures[_Index].To<iCAX::Data::ObjectMap>();
        EXPECT_EQ("side-opening", _Feature.at("kind").To<std::string>());
        EXPECT_NEAR(
            _ExpectedOpeningStations[_Index], _Feature.at("station").To<double>(), 0.01);
    }
    const auto _OuterFrameReference =
        _OuterFrameMeasurement.at("linearReference").To<iCAX::Data::ObjectMap>();
    const auto _OuterFrameStart =
        _OuterFrameReference.at("start").To<iCAX::Data::VariantArray>();
    const auto _OuterFrameEnd =
        _OuterFrameReference.at("end").To<iCAX::Data::VariantArray>();
    EXPECT_NEAR(-900.0, _OuterFrameStart[0].To<double>(), 0.01);
    EXPECT_NEAR(900.0, _OuterFrameEnd[0].To<double>(), 0.01);
    EXPECT_NEAR(0.0, _OuterFrameEnd[1].To<double>(), 0.01);
    EXPECT_NEAR(0.0, _OuterFrameEnd[2].To<double>(), 0.01);

    _Parameters["width"] = 1400.0;
    _Parameters["frameLayout"] = std::string("four_sides");
    // This stage verifies a single unfolded frame, regardless of the default joint.
    _Parameters["frameJoinType"] = std::string("v_groove_90:sharp_v");
    const auto _SecondResponse = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _SecondModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_SecondResponse);
    ASSERT_TRUE(_SecondModel.Parameters.at("width").Is<double>());
    EXPECT_DOUBLE_EQ(1400.0, _SecondModel.Parameters.at("width").To<double>());
    ASSERT_EQ(16u, _SecondModel.Items.size());
    const auto& _ContinuousFrame = _SecondModel.Items.front();
    EXPECT_EQ("outer_frame.continuous.0001", _ContinuousFrame.Key);
    EXPECT_NE(
        _ContinuousFrame.Representations.at("display"),
        _ContinuousFrame.Representations.at("export"));
    const auto _SecondDisplay = EvaluateTemplatePurposeGeometry(_SecondModel, "display");
    ExpectUnmachinedTemplateDisplay(_SecondModel, _SecondDisplay);
    const auto _SecondEvaluation = iCAX::OpenCascade::EvaluateNeutralModel(_SecondModel);
    EXPECT_FALSE(_SecondEvaluation.At(
        _ContinuousFrame.Representations.at("display")).IsNull());
    EXPECT_FALSE(_SecondEvaluation.At(
        _ContinuousFrame.Representations.at("export")).IsNull());

    const auto& _FrameDisplayKey = _ContinuousFrame.Representations.at("display");
    const auto& _FrameExportKey = _ContinuousFrame.Representations.at("export");
    const auto _FrameDisplay = iCAX::OpenCascade::EvaluateNeutralModel(
        _SecondModel, std::vector<std::string>{ _FrameDisplayKey });
    const auto _FrameExport = iCAX::OpenCascade::EvaluateNeutralModel(
        _SecondModel, std::vector<std::string>{ _FrameExportKey });
    ExpectManufacturingBooleanIsEvaluated(_SecondModel, _FrameExport);
    EXPECT_FALSE(_FrameDisplay.Geometry.contains(_FrameExportKey));
    EXPECT_FALSE(_FrameExport.Geometry.contains(_FrameDisplayKey));
    for (const auto& [_Key, _Shape] : _FrameDisplay.Geometry)
    {
        EXPECT_TRUE(_Key.starts_with("shared.tube.") || _Key.find(".display.") != std::string::npos) << _Key;
        EXPECT_EQ(std::string::npos, _Key.find(".export.")) << _Key;
    }
    for (const auto& [_Key, _Shape] : _FrameExport.Geometry)
    {
        EXPECT_TRUE(_Key.starts_with("shared.tube.") || _Key.find(".export.") != std::string::npos) << _Key;
        EXPECT_EQ(std::string::npos, _Key.find(".display.")) << _Key;
    }
    ExpectRootSelectionGeometryMatches(_FrameDisplay.At(_FrameDisplayKey),
        _SecondEvaluation.At(_FrameDisplayKey));
    ExpectRootSelectionGeometryMatches(_FrameExport.At(_FrameExportKey),
        _SecondEvaluation.At(_FrameExportKey));
    const auto _FrameDisplayBounds = RootSelectionShapeBounds(_FrameDisplay.At(_FrameDisplayKey));
    const auto _FrameExportBounds = RootSelectionShapeBounds(_FrameExport.At(_FrameExportKey));
    EXPECT_NEAR(1400.0, _FrameDisplayBounds[3] - _FrameDisplayBounds[0], 0.001);
    EXPECT_NEAR(1800.0, _FrameDisplayBounds[5] - _FrameDisplayBounds[2], 0.001);
    EXPECT_NEAR(_ContinuousFrame.Properties.at("length").To<double>(),
        _FrameExportBounds[3] - _FrameExportBounds[0], 0.001);
    EXPECT_LT(_FrameExportBounds[5] - _FrameExportBounds[2], 100.0);

    _Parameters["frameJoinType"] = std::string("miter_45");
    const auto _MiterResponse = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _MiterModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_MiterResponse);
    ASSERT_EQ(19u, _MiterModel.Items.size());
    EXPECT_EQ("outer_frame.left.0001", _MiterModel.Items.front().Key);
    const auto _MiterDisplay = EvaluateTemplatePurposeGeometry(_MiterModel, "display");
    ExpectUnmachinedTemplateDisplay(_MiterModel, _MiterDisplay);
    const auto _MiterEvaluation = EvaluateTemplatePurposeGeometry(_MiterModel, "export");
    ExpectManufacturingBooleanIsEvaluated(_MiterModel, _MiterEvaluation);
    for (std::size_t _Index = 0; _Index < 4; ++_Index)
    {
        const auto& _Item = _MiterModel.Items[_Index];
        EXPECT_NE(_Item.Representations.at("display"), _Item.Representations.at("export"));
        EXPECT_FALSE(_MiterEvaluation.At(
            _Item.Representations.at("export")).IsNull());
    }

    _Parameters["frameLayout"] = std::string("left_right");
    _Parameters["accessDoorEnabled"] = true;
    const auto _DoorResponse = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _DoorModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_DoorResponse);
    const auto _DoorDisplay = EvaluateTemplatePurposeGeometry(_DoorModel, "display");
    ExpectUnmachinedTemplateDisplay(_DoorModel, _DoorDisplay);
    // Grid-end assembly relationships coexist with hinges. Only the opening's
    // hinge count is fixed here; the default tube-grid must not create a plate.
    EXPECT_FALSE(_Parameters.contains("mainInfillMode"));
    EXPECT_FALSE(std::any_of(_DoorModel.Items.begin(), _DoorModel.Items.end(),
        [](const auto& Item_) { return Item_.Key.starts_with("center_plate."); }));
    std::size_t _HingeCount = 0;
    for (const auto& _Relationship : _DoorModel.Relationships)
    {
        if (_Relationship.Kind != "hinge") continue;
        ++_HingeCount;
        ASSERT_EQ(2u, _Relationship.ItemKeys.size());
        EXPECT_TRUE(std::any_of(_Relationship.ItemKeys.begin(), _Relationship.ItemKeys.end(),
            [](const auto& Key_) { return Key_.starts_with("access_door.fixed_frame."); }));
        EXPECT_TRUE(std::any_of(_Relationship.ItemKeys.begin(), _Relationship.ItemKeys.end(),
            [](const auto& Key_) { return Key_.starts_with("access_door.leaf.frame."); }));
    }
    EXPECT_EQ(_Parameters.at("doorHingeCount").To<unsigned long long>(), _HingeCount);
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, MultiFaceInspectionDoorCanBePlacedOnEveryAvailableFace)
{
    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    struct SCase
    {
        const char* Directory;
        const char* Face;
        const char* SidePosition;
        const char* ExpectedFaceName;
        bool EvaluateShape;
    };
    const std::vector<SCase> _Cases{
        { "two_face_security_window", "side", "left", "左侧面", true },
        { "two_face_security_window", "front", "right", "正面", false },
        { "two_face_security_window", "side", "right", "右侧面", false },
        { "three_face_security_window", "left", "", "左侧面", false },
        { "three_face_security_window", "front", "", "正面", true },
        { "three_face_security_window", "right", "", "右侧面", false },
        { "five_face_security_window", "left", "", "左侧面", false },
        { "five_face_security_window", "front", "", "正面", false },
        { "five_face_security_window", "right", "", "右侧面", false },
        { "five_face_security_window", "top", "", "上面", true },
        { "five_face_security_window", "bottom", "", "下面", true },
    };
    for (const auto& _Case : _Cases)
    {
        SCOPED_TRACE(std::string(_Case.Directory) + "/" + _Case.Face);
        const auto _TemplateRoot = _Root / "src/apps/tube-designer/templates" / _Case.Directory;
        const auto _DescriptorPath = _TemplateRoot / "template.json";
        std::ifstream _DescriptorStream(_DescriptorPath, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(_DescriptorStream)) << _DescriptorPath.string();
        const std::string _DescriptorText{
            std::istreambuf_iterator<char>(_DescriptorStream), std::istreambuf_iterator<char>()
        };
        auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
        _Descriptor.PackageDigest = std::string("door-") + _Case.Directory + "-" + _Case.Face;
        iCAX::Data::ObjectMap _Parameters;
        for (const auto& _Definition : _Descriptor.Parameters)
            _Parameters[_Definition.Key] = _Definition.DefaultValue;
        _Parameters["accessDoorEnabled"] = true;
        _Parameters["accessDoorFace"] = std::string(_Case.Face);
        // Small openings on every face are inspection hatches, not escape
        // windows. Explicit offsets also fit the shallow top/bottom surfaces.
        _Parameters["doorUse"] = std::string("maintenance");
        _Parameters["doorClearWidth"] = 300.0;
        _Parameters["doorClearHeight"] = 300.0;
        _Parameters["doorUOffset"] = 80.0;
        _Parameters["doorVOffset"] = 80.0;
        if (_Case.SidePosition[0] != '\0')
            _Parameters["sidePosition"] = std::string(_Case.SidePosition);
        _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, _Parameters);

        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _Parameters, (_TemplateRoot / "template.py").string()));
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
        ASSERT_TRUE(_Model.Extensions.contains("tubeDesigner.securityWindowReview"));
        const auto _Review = _Model.Extensions.at("tubeDesigner.securityWindowReview")
            .To<iCAX::Data::ObjectMap>();
        EXPECT_EQ("maintenance", _Review.at("openingUse").To<std::string>());
        EXPECT_FALSE(_Review.at("complianceCertified").To<bool>());
        EXPECT_NEAR(300.0, _Review.at("designClearWidth").To<double>(), 0.001);
        EXPECT_NEAR(300.0, _Review.at("designClearHeight").To<double>(), 0.001);
        const auto _Display = EvaluateTemplatePurposeGeometry(_Model, "display");
        ExpectUnmachinedTemplateDisplay(_Model, _Display);
        const auto _DoorPartCount = std::count_if(
            _Model.Items.begin(), _Model.Items.end(),
            [](const auto& Item_) { return Item_.Key.starts_with("access_door."); });
        EXPECT_EQ(12, _DoorPartCount) << _Case.Directory << "/" << _Case.Face;
        const auto _HingeCount = std::count_if(
            _Model.Relationships.begin(), _Model.Relationships.end(),
            [](const auto& Relationship_) { return Relationship_.Kind == "hinge"; });
        EXPECT_EQ(2, _HingeCount) << _Case.Directory << "/" << _Case.Face;
        const auto _SplitPartCount = std::count_if(
            _Model.Items.begin(), _Model.Items.end(),
            [](const auto& Item_)
            {
                const auto _IsGrid = Item_.Key.starts_with("main_grid.")
                    || Item_.Key.starts_with("cap_grid.");
                return _IsGrid && (Item_.Key.find(".start.") != std::string::npos
                    || Item_.Key.find(".end.") != std::string::npos
                    || Item_.Key.find(".top.") != std::string::npos
                    || Item_.Key.find(".bottom.") != std::string::npos
                    || Item_.Key.find(".front.") != std::string::npos
                    || Item_.Key.find(".back.") != std::string::npos);
            });
        EXPECT_GT(_SplitPartCount, 0) << _Case.Directory << "/" << _Case.Face;
        for (const auto& _Item : _Model.Items)
        {
            if (!_Item.Key.starts_with("access_door.")) continue;
            ASSERT_TRUE(_Item.Properties.contains("tubeDesigner.faceName"));
            EXPECT_EQ(
                _Case.ExpectedFaceName,
                _Item.Properties.at("tubeDesigner.faceName").To<std::string>());
        }
        if (_Case.EvaluateShape)
        {
            const auto _Evaluation = EvaluateTemplatePurposeGeometry(_Model, "export");
            ExpectManufacturingBooleanIsEvaluated(_Model, _Evaluation);
            for (const auto& _Item : _Model.Items)
            {
                if (!_Item.Key.starts_with("access_door.")) continue;
                EXPECT_FALSE(_Display.At(_Item.Representations.at("display")).IsNull())
                    << _Case.Directory << "/" << _Case.Face << "/" << _Item.Key;
                EXPECT_FALSE(_Evaluation.At(_Item.Representations.at("export")).IsNull())
                    << _Case.Directory << "/" << _Case.Face << "/" << _Item.Key;
            }
        }
    }
}

TEST(TemplateRuntimeTest, StraightStairRailingGeneratesManufacturableTubeParts)
{
    const auto _Root = std::filesystem::current_path();
    const auto _TemplateRoot = _Root
        / "src/apps/tube-designer/templates/straight_stair_railing";
    const auto _DescriptorPath = _TemplateRoot / "template.json";
    std::ifstream _DescriptorStream(_DescriptorPath, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(_DescriptorStream));
    const std::string _DescriptorText{
        std::istreambuf_iterator<char>(_DescriptorStream), std::istreambuf_iterator<char>()
    };
    auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    EXPECT_EQ("straight-stair-railing", _Descriptor.ID);
    _Descriptor.PackageDigest = "straight-stair-railing-test";

    iCAX::Data::ObjectMap _Parameters;
    for (const auto& _Definition : _Descriptor.Parameters)
        _Parameters[_Definition.Key] = _Definition.DefaultValue;
    _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, _Parameters);

    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    const auto _Evaluate = [&](const iCAX::Data::ObjectMap& Parameters_)
    {
        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, Parameters_, (_TemplateRoot / "template.py").string()));
        auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
        auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
        return std::pair{ std::move(_Model), std::move(_Geometry) };
    };

    auto [_VerticalModel, _VerticalGeometry] = _Evaluate(_Parameters);
    ASSERT_EQ(27u, _VerticalModel.Items.size());
    ASSERT_EQ(2u, _VerticalModel.Outputs.size());
    EXPECT_EQ(27u, _VerticalModel.Outputs[0].ItemKeys.size());
    EXPECT_EQ(27u, _VerticalModel.Outputs[1].ItemKeys.size());
    EXPECT_EQ(4u, _VerticalModel.Relationships.size());
    EXPECT_EQ(21, std::count_if(
        _VerticalModel.Items.begin(), _VerticalModel.Items.end(),
        [](const auto& Item_) { return Item_.Key.starts_with("infill.vertical."); }));
    for (const auto& _Item : _VerticalModel.Items)
    {
        ASSERT_TRUE(_Item.Properties.contains("manufacturing.categoryKey"));
        ASSERT_TRUE(_Item.Properties.contains("tubeDesigner.profile"));
        EXPECT_FALSE(_VerticalGeometry.At(_Item.Representations.at("display")).IsNull());
        EXPECT_FALSE(_VerticalGeometry.At(_Item.Representations.at("export")).IsNull());
    }
    const auto& _Handrail = _VerticalModel.Items.front();
    EXPECT_EQ("handrail.0001", _Handrail.Key);
    EXPECT_NEAR(
        std::hypot(3300.0, 1980.0),
        _Handrail.Properties.at("length").To<double>(), 0.01);

    _Parameters["infillType"] = std::string("horizontal");
    auto [_HorizontalModel, _HorizontalGeometry] = _Evaluate(_Parameters);
    ASSERT_EQ(8u, _HorizontalModel.Items.size());
    EXPECT_EQ(3, std::count_if(
        _HorizontalModel.Items.begin(), _HorizontalModel.Items.end(),
        [](const auto& Item_) { return Item_.Key.starts_with("infill.horizontal."); }));
    for (const auto& _Item : _HorizontalModel.Items)
        EXPECT_FALSE(_HorizontalGeometry.At(_Item.Representations.at("export")).IsNull());
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, EveryBuiltInProfilePackageGeneratesAValidExtrusion)
{
    constexpr auto _DescriptorText = R"json({
        "schema":"icax.template-descriptor",
        "schemaVersion":1,
        "id":"profile-catalog-probe",
        "version":"1.0.0",
        "displayName":"Profile catalog probe",
        "parameters":[
            {"key":"probeProfileType","displayName":"Profile","valueType":"string","defaultValue":"rect"},
            {"key":"probeWidth","displayName":"Width","valueType":"number","defaultValue":80.0},
            {"key":"probeDepth","displayName":"Depth","valueType":"number","defaultValue":50.0},
            {"key":"probeWallThickness","displayName":"Thickness","valueType":"number","defaultValue":4.0},
            {"key":"probeCornerRadius","displayName":"Radius","valueType":"number","defaultValue":3.0}
        ]
    })json";
    auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    _Descriptor.PackageDigest = "profile-catalog-probe-test";
    const auto _Root = std::filesystem::current_path();
    const auto _Script = _Root
        / "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/ProfileCatalogProbe.py";
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    constexpr const char* _ProfileIDs[]{
        "rect", "round", "ellipse", "flat-oval", "polygon",
        "angle", "channel", "i-section", "t-section", "z-section",
    };
    for (const auto* _ProfileID : _ProfileIDs)
    {
        SCOPED_TRACE(_ProfileID);
        iCAX::Data::ObjectMap _Parameters{
            { "probeProfileType", std::string(_ProfileID) },
            { "probeWidth", 80.0 },
            { "probeDepth", 50.0 },
            { "probeWallThickness", 4.0 },
            { "probeCornerRadius", 3.0 },
        };
        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _Parameters, _Script.string()));
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
        ASSERT_EQ(2u, _Model.Geometry.size());
        EXPECT_EQ(
            iCAX::TemplateRuntime::EGeometryOperator::Profile2D,
            _Model.Geometry[0].Operator);
        EXPECT_EQ(
            iCAX::TemplateRuntime::EGeometryOperator::Extrude,
            _Model.Geometry[1].Operator);
        const auto _Contours = _Model.Geometry[0].Arguments.at("contours")
            .To<iCAX::Data::VariantArray>();
        const auto _ExpectedContourCount = std::find(
            std::begin(_ProfileIDs), std::begin(_ProfileIDs) + 5, _ProfileID)
            != std::begin(_ProfileIDs) + 5 ? 2u : 1u;
        EXPECT_EQ(_ExpectedContourCount, _Contours.size());
        ASSERT_EQ(1u, _Model.Items.size());
        const auto _Profile = _Model.Items.front().Properties.at("tubeDesigner.profile")
            .To<iCAX::Data::ObjectMap>();
        EXPECT_EQ(_ProfileID, _Profile.at("id").To<std::string>());
        EXPECT_FALSE(_Profile.at("displayName").To<std::string>().empty());
        EXPECT_FALSE(_Profile.at("specification").To<std::string>().empty());
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
        const auto& _Shape = _Geometry.At(
            _Model.Items.front().Representations.at("export"));
        EXPECT_FALSE(_Shape.IsNull());
        const auto _BRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
            _Shape, std::string("profile-preview-") + _ProfileID,
            std::string("profile-preview-") + _ProfileID, 0.001);
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_BRep);
        std::string _Diagnostics;
        for (const auto& _Item : _Rebuilt.Diagnostics) _Diagnostics += _Item + " | ";
        EXPECT_TRUE(_Rebuilt.bOK) << _ProfileID << ": " << _Diagnostics;
        EXPECT_FALSE(_Rebuilt.Shape.IsNull()) << _ProfileID;
    }
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, ImportsFrozenDxfProfileAndUsesItAsAnInstanceOverride)
{
    const auto _Root = std::filesystem::current_path();
    const auto _Importer = _Root
        / "src/apps/tube-designer/templates/_shared/dxf_profile_importer.py";
    const auto _Fixture = _Root
        / "src/tests/icax-plugins/product/TubeDesigner/Fixtures/imported_profile_mm.dxf";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Importer));
    ASSERT_TRUE(std::filesystem::is_regular_file(_Fixture));
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));

    const iCAX::Data::ObjectMap _ImportRequest{
        { "protocol", std::string("icax.template-runtime") },
        { "protocolVersion", 1ull },
        { "operation", std::string("evaluate") },
        { "templatePath", _Importer.string() },
        { "template", iCAX::Data::ObjectMap{
            { "id", std::string("icax.dxf-profile-importer") },
            { "version", std::string("1.0.0") },
            { "packageDigest", std::string("dxf-profile-importer-test") }
        } },
        { "parameters", iCAX::Data::ObjectMap{ { "sourcePath", _Fixture.string() } } },
        { "context", iCAX::Data::ObjectMap{
            { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
            { "lengthUnit", std::string("mm") }
        } }
    };
    const auto _Imported = _Host.Invoke(_ImportRequest);
    ASSERT_TRUE(_Imported.contains("profile"));
    const auto _Profile = _Imported.at("profile").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.imported-tube-profile", _Profile.at("schema").To<std::string>());
    EXPECT_NEAR(50.0, _Profile.at("width").To<double>(), 1.0e-9);
    EXPECT_NEAR(30.0, _Profile.at("depth").To<double>(), 1.0e-9);
    EXPECT_EQ(2u, _Profile.at("contours").To<iCAX::Data::VariantArray>().size());
    EXPECT_TRUE(_Profile.at("hollow").To<bool>());

    constexpr auto _DescriptorText = R"json({
        "schema":"icax.template-descriptor",
        "schemaVersion":1,
        "id":"profile-catalog-probe",
        "version":"1.0.0",
        "displayName":"Profile catalog probe",
        "parameters":[
            {"key":"probeProfileType","displayName":"Profile","valueType":"string","defaultValue":"rect"},
            {"key":"probeWidth","displayName":"Width","valueType":"number","defaultValue":80.0},
            {"key":"probeDepth","displayName":"Depth","valueType":"number","defaultValue":50.0},
            {"key":"probeWallThickness","displayName":"Thickness","valueType":"number","defaultValue":4.0},
            {"key":"probeCornerRadius","displayName":"Radius","valueType":"number","defaultValue":3.0}
        ]
    })json";
    auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    _Descriptor.PackageDigest = "profile-catalog-dxf-override-test";
    iCAX::Data::ObjectMap _Parameters{
        { "probeProfileType", std::string("rect") },
        { "probeWidth", 80.0 }, { "probeDepth", 50.0 },
        { "probeWallThickness", 4.0 }, { "probeCornerRadius", 3.0 },
    };
    _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, _Parameters);
    _Parameters["tubeDesignerProfileOverrides"] = iCAX::Data::ObjectMap{
        { "probe", _Profile }
    };
    const auto _Probe = _Root
        / "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/ProfileCatalogProbe.py";
    const auto _Response = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _Probe.string()));
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
    const auto _Properties = _Model.Items.front().Properties.at("tubeDesigner.profile")
        .To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("imported-dxf", _Properties.at("kind").To<std::string>());
    EXPECT_TRUE(_Properties.at("frozenGeometry").To<bool>());
    const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    const auto& _Shape = _Geometry.At(_Model.Items.front().Representations.at("export"));
    ASSERT_FALSE(_Shape.IsNull());
    Bnd_Box _Bounds;
    BRepBndLib::Add(_Shape, _Bounds);
    double _MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ;
    _Bounds.Get(_MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ);
    EXPECT_NEAR(50.0, _MaxX - _MinX, 1.0e-5);
    EXPECT_NEAR(30.0, _MaxY - _MinY, 1.0e-5);
    EXPECT_NEAR(250.0, _MaxZ - _MinZ, 1.0e-5);

    for (const auto& [_Label, _FileName] : std::array{
        std::pair{ "rectangle", std::string("03_DXF矩形管_50x30x3.dxf") },
        std::pair{ "round", std::string("04_DXF圆管_外径40壁厚2.dxf") },
        std::pair{ "ellipse", std::string("05_DXF椭圆管_60x30壁厚2.dxf") },
        std::pair{ "plum", std::string("06_DXF五瓣梅花管_外径约60.dxf") } })
    {
        SCOPED_TRACE(_Label);
        const auto _SampleStartedAt = std::chrono::steady_clock::now();
        auto _PreviousStageAt = _SampleStartedAt;
        const auto _ReportStage = [&_Label, &_PreviousStageAt](const char* Stage_) {
            const auto _Now = std::chrono::steady_clock::now();
            const auto _Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                _Now - _PreviousStageAt).count();
            std::cout << "[profile-timing] " << _Label << " " << Stage_
                << "=" << _Elapsed << "ms" << std::endl;
            _PreviousStageAt = _Now;
        };
        auto _SampleRequest = _ImportRequest;
        const auto _SamplePath = _Root
            / Utf8TestPath("Temp/管型导入素材")
            / Utf8TestPath(_FileName);
        _SampleRequest["parameters"] = iCAX::Data::ObjectMap{
            { "sourcePath", Utf8TestPathText(_SamplePath) }
        };
        const auto _SampleImported = _Host.Invoke(_SampleRequest);
        _ReportStage("import");
        const auto _SampleProfile = _SampleImported.at("profile")
            .To<iCAX::Data::ObjectMap>();
        auto _SampleParameters = _Parameters;
        _SampleParameters["tubeDesignerProfileOverrides"] = iCAX::Data::ObjectMap{
            { "probe", _SampleProfile }
        };
        const auto _SampleResponse = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _SampleParameters, _Probe.string()));
        _ReportStage("python-model");
        const auto _SampleModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            _SampleResponse);
        const auto _SampleGeometry = iCAX::OpenCascade::EvaluateNeutralModel(_SampleModel);
        _ReportStage("extrude");
        const auto& _SampleShape = _SampleGeometry.At(
            _SampleModel.Items.front().Representations.at("export"));
        ASSERT_FALSE(_SampleShape.IsNull());
        const auto _SampleBRep = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
            _SampleShape, _Label, _Label, 0.025);
        _ReportStage("brep-convert");
        const auto _SampleRebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_SampleBRep);
        _ReportStage("brep-rebuild");
        std::string _SampleDiagnostics;
        for (const auto& _Item : _SampleRebuilt.Diagnostics)
            _SampleDiagnostics += _Item + " | ";
        EXPECT_TRUE(_SampleRebuilt.bOK) << _SampleDiagnostics;
        EXPECT_FALSE(_SampleRebuilt.Shape.IsNull());
    }
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, ImportsAndReevaluatesEditableProfilePackage)
{
    const auto _Root = std::filesystem::current_path();
    const auto _Runtime = _Root
        / "src/apps/tube-designer/templates/_shared/profile_package_runtime.py";
    const auto _Fixture = _Root
        / "src/tests/icax-plugins/product/TubeDesigner/Fixtures/editable_profile_package.icaxprofile";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Runtime));
    ASSERT_TRUE(std::filesystem::is_regular_file(_Fixture));
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));

    const auto _MakeRequest = [&](const iCAX::Data::ObjectMap& Parameters_)
    {
        return iCAX::Data::ObjectMap{
            { "protocol", std::string("icax.template-runtime") },
            { "protocolVersion", 1ull },
            { "operation", std::string("evaluate") },
            { "templatePath", _Runtime.string() },
            { "template", iCAX::Data::ObjectMap{
                { "id", std::string("icax.profile-package-runtime") },
                { "version", std::string("1.0.0") },
                { "packageDigest", std::string("profile-package-runtime-test") }
            } },
            { "parameters", Parameters_ },
            { "context", iCAX::Data::ObjectMap{
                { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
                { "lengthUnit", std::string("mm") }
            } }
        };
    };

    const auto _Imported = _Host.Invoke(_MakeRequest({
        { "action", std::string("inspect") },
        { "sourcePath", _Fixture.string() },
        { "password", std::string() }
    }));
    const auto _Package = _Imported.at("package").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("icax.tube-profile-package-record", _Package.at("schema").To<std::string>());
    EXPECT_EQ("parametric-package", _Package.at("kind").To<std::string>());
    EXPECT_FALSE(_Package.at("passwordProtected").To<bool>());
    EXPECT_FALSE(_Package.at("scriptSource").To<std::string>().empty());
    const auto _Descriptor = _Package.at("descriptor").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("test-editable-rect", _Descriptor.at("id").To<std::string>());

    auto _Values = _Package.at("defaultParameters").To<iCAX::Data::ObjectMap>();
    _Values["width"] = 72.0;
    _Values["depth"] = 44.0;
    const auto _Evaluated = _Host.Invoke(_MakeRequest({
        { "action", std::string("evaluate") },
        { "descriptor", _Descriptor },
        { "scriptSource", _Package.at("scriptSource") },
        { "packageDigest", _Package.at("packageDigest") },
        { "sourceFileName", _Package.at("sourceFileName") },
        { "values", _Values }
    }));
    const auto _Profile = _Evaluated.at("profile").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("parametric-package", _Profile.at("kind").To<std::string>());
    EXPECT_TRUE(_Profile.at("editableParameters").To<bool>());
    EXPECT_FALSE(_Profile.at("frozenGeometry").To<bool>());
    EXPECT_NEAR(72.0, _Profile.at("width").To<double>(), 1.0e-9);
    EXPECT_NEAR(44.0, _Profile.at("depth").To<double>(), 1.0e-9);
    ASSERT_EQ(2u, _Profile.at("contours").To<iCAX::Data::VariantArray>().size());

    constexpr auto _DescriptorText = R"json({
        "schema":"icax.template-descriptor",
        "schemaVersion":1,
        "id":"profile-catalog-probe",
        "version":"1.0.0",
        "displayName":"Profile catalog probe",
        "parameters":[
            {"key":"probeProfileType","displayName":"Profile","valueType":"string","defaultValue":"rect"},
            {"key":"probeWidth","displayName":"Width","valueType":"number","defaultValue":80.0},
            {"key":"probeDepth","displayName":"Depth","valueType":"number","defaultValue":50.0},
            {"key":"probeWallThickness","displayName":"Thickness","valueType":"number","defaultValue":4.0},
            {"key":"probeCornerRadius","displayName":"Radius","valueType":"number","defaultValue":3.0}
        ]
    })json";
    auto _ProbeDescriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    _ProbeDescriptor.PackageDigest = "profile-catalog-parametric-override-test";
    auto _ProbeParameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _ProbeDescriptor,
        {
            { "probeProfileType", std::string("rect") },
            { "probeWidth", 80.0 }, { "probeDepth", 50.0 },
            { "probeWallThickness", 4.0 }, { "probeCornerRadius", 3.0 }
        });
    _ProbeParameters["tubeDesignerProfileOverrides"] = iCAX::Data::ObjectMap{
        { "probe", _Profile }
    };
    const auto _Probe = _Root
        / "src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/ProfileCatalogProbe.py";
    const auto _Response = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _ProbeDescriptor, _ProbeParameters, _Probe.string()));
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
    const auto _Properties = _Model.Items.front().Properties.at("tubeDesigner.profile")
        .To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("parametric-package", _Properties.at("kind").To<std::string>());
    EXPECT_TRUE(_Properties.at("editableParameters").To<bool>());
    const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    const auto& _Shape = _Geometry.At(_Model.Items.front().Representations.at("export"));
    ASSERT_FALSE(_Shape.IsNull());
    Bnd_Box _Bounds;
    BRepBndLib::Add(_Shape, _Bounds);
    double _MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ;
    _Bounds.Get(_MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ);
    EXPECT_NEAR(72.0, _MaxX - _MinX, 1.0e-5);
    EXPECT_NEAR(44.0, _MaxY - _MinY, 1.0e-5);
    EXPECT_NEAR(250.0, _MaxZ - _MinZ, 1.0e-5);
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, ListsAndEvaluatesEverySystemProfileForTheLibrary)
{
    const auto _Root = std::filesystem::current_path();
    const auto _Runtime = _Root
        / "src/apps/tube-designer/templates/_shared/profile_package_runtime.py";
    ASSERT_TRUE(std::filesystem::is_regular_file(_Runtime));
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    const auto _MakeRequest = [&](const iCAX::Data::ObjectMap& Parameters_)
    {
        return iCAX::Data::ObjectMap{
            { "protocol", std::string("icax.template-runtime") },
            { "protocolVersion", 1ull },
            { "operation", std::string("evaluate") },
            { "templatePath", _Runtime.string() },
            { "template", iCAX::Data::ObjectMap{
                { "id", std::string("icax.profile-package-runtime") },
                { "version", std::string("1.0.0") },
                { "packageDigest", std::string("system-profile-library-test") }
            } },
            { "parameters", Parameters_ },
            { "context", iCAX::Data::ObjectMap{
                { "coordinateSystem", std::string("right-handed-x-width-y-depth") },
                { "lengthUnit", std::string("mm") }
            } }
        };
    };

    const auto _Listed = _Host.Invoke(_MakeRequest({
        { "action", std::string("list-system") }
    }));
    const auto _Profiles = _Listed.at("systemProfiles").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(10u, _Profiles.size());
    constexpr const char* _ExpectedIDs[]{
        "angle", "channel", "ellipse", "flat-oval", "i-section", "polygon",
        "rect", "round", "t-section", "z-section",
    };
    for (const auto& _Value : _Profiles)
    {
        const auto _Package = _Value.To<iCAX::Data::ObjectMap>();
        EXPECT_EQ("icax.tube-profile-package-record", _Package.at("schema").To<std::string>());
        EXPECT_EQ("parametric-package", _Package.at("kind").To<std::string>());
        const auto _Descriptor = _Package.at("descriptor").To<iCAX::Data::ObjectMap>();
        const auto _ID = _Descriptor.at("id").To<std::string>();
        EXPECT_NE(std::end(_ExpectedIDs), std::find(
            std::begin(_ExpectedIDs), std::end(_ExpectedIDs), _ID));
        const auto _Defaults = _Package.at("defaultParameters").To<iCAX::Data::ObjectMap>();
        EXPECT_FALSE(_Defaults.empty()) << _ID;
        const auto _Preview = _Package.at("previewProfile").To<iCAX::Data::ObjectMap>();
        EXPECT_FALSE(_Preview.at("contours").To<iCAX::Data::VariantArray>().empty()) << _ID;
        EXPECT_GT(_Preview.at("width").To<double>(), 0.0) << _ID;
        EXPECT_GT(_Preview.at("depth").To<double>(), 0.0) << _ID;
    }

    const auto _Evaluated = _Host.Invoke(_MakeRequest({
        { "action", std::string("evaluate-system") },
        { "systemProfileId", std::string("round") },
        { "values", iCAX::Data::ObjectMap{
            { "width", 76.0 }, { "wallThickness", 3.0 }
        } }
    }));
    const auto _Round = _Evaluated.at("profile").To<iCAX::Data::ObjectMap>();
    EXPECT_NEAR(76.0, _Round.at("width").To<double>(), 1.0e-9);
    EXPECT_NEAR(76.0, _Round.at("depth").To<double>(), 1.0e-9);
    EXPECT_NEAR(3.0, _Round.at("wallThickness").To<double>(), 1.0e-9);
    EXPECT_EQ(2u, _Round.at("contours").To<iCAX::Data::VariantArray>().size());

    const auto _StarEvaluated = _Host.Invoke(_MakeRequest({
        { "action", std::string("evaluate-system") },
        { "systemProfileId", std::string("polygon") },
        { "values", iCAX::Data::ObjectMap{
            { "shapeMode", std::string("star") },
            { "sideCount", 5 },
            { "starInnerRatio", 0.5 },
            { "width", 40.0 },
            { "depth", 40.0 },
            { "wallThickness", 2.0 },
        } }
    }));
    const auto _Star = _StarEvaluated.at("profile").To<iCAX::Data::ObjectMap>();
    EXPECT_EQ("polygon", _Star.at("profileDefinitionId").To<std::string>());
    const auto _StarContours = _Star.at("contours").To<iCAX::Data::VariantArray>();
    ASSERT_EQ(2u, _StarContours.size());
    for (const auto& _ContourValue : _StarContours)
    {
        const auto _Contour = _ContourValue.To<iCAX::Data::ObjectMap>();
        EXPECT_EQ(10u, _Contour.at("points").To<iCAX::Data::VariantArray>().size());
    }
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, MultiStepSteelStaircaseTemplatesGenerateCompleteManufacturingModels)
{
    struct SCase final
    {
        const char* Directory;
        const char* TemplateID;
        bool HasLanding;
    };
    constexpr SCase _Cases[]{
        { "straight_steel_staircase", "straight-steel-staircase", false },
        { "l_turn_steel_staircase", "l-turn-steel-staircase", true },
        { "u_turn_steel_staircase", "u-turn-steel-staircase", true },
    };

    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto& _Case : _Cases)
    {
        SCOPED_TRACE(_Case.TemplateID);
        const auto _TemplateRoot = _Root
            / "src/apps/tube-designer/templates" / _Case.Directory;
        std::ifstream _DescriptorStream(_TemplateRoot / "template.json", std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(_DescriptorStream));
        const std::string _DescriptorText{
            std::istreambuf_iterator<char>(_DescriptorStream), std::istreambuf_iterator<char>()
        };
        auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
            iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
        EXPECT_EQ(_Case.TemplateID, _Descriptor.ID);
        _Descriptor.PackageDigest = std::string(_Case.TemplateID) + "-test";

        iCAX::Data::ObjectMap _Parameters;
        for (const auto& _Definition : _Descriptor.Parameters)
            _Parameters[_Definition.Key] = _Definition.DefaultValue;
        if (!_Case.HasLanding)
        {
            _Parameters["stringerProfileType"] = std::string("i-section");
            _Parameters["treadProfileType"] = std::string("channel");
            _Parameters["handrailProfileType"] = std::string("ellipse");
            _Parameters["postProfileType"] = std::string("angle");
        }
        _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, _Parameters);
        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _Parameters, (_TemplateRoot / "template.py").string()));
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
        EXPECT_GT(_Model.Items.size(), 20u);
        ASSERT_EQ(2u, _Model.Outputs.size());
        EXPECT_EQ(_Model.Items.size(), _Model.Outputs[0].ItemKeys.size());
        EXPECT_EQ(_Model.Items.size(), _Model.Outputs[1].ItemKeys.size());
        EXPECT_EQ(_Case.HasLanding, std::any_of(
            _Model.Items.begin(), _Model.Items.end(),
            [](const auto& Item_) { return Item_.Key.starts_with("landing."); }));
        EXPECT_TRUE(std::any_of(
            _Model.Items.begin(), _Model.Items.end(),
            [](const auto& Item_) { return Item_.Key.find(".tread.") != std::string::npos; }));
        for (const auto& _Item : _Model.Items)
        {
            ASSERT_TRUE(_Item.Properties.contains("manufacturing.categoryKey"));
            ASSERT_TRUE(_Item.Properties.contains("tubeDesigner.profile"));
        }

        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
        for (const auto& _Item : _Model.Items)
            EXPECT_FALSE(_Geometry.At(_Item.Representations.at("export")).IsNull()) << _Item.Key;
    }
}

TEST(TemplateRuntimeTest, TwoFaceDirectionRebuildsMirroredFinalShapes)
{
    const auto _Root = std::filesystem::current_path();
    const auto _TemplateRoot = _Root / "src/apps/tube-designer/templates/two_face_security_window";
    const auto _DescriptorPath = _TemplateRoot / "template.json";
    std::ifstream _DescriptorStream(_DescriptorPath, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(_DescriptorStream)) << _DescriptorPath.string();
    const std::string _DescriptorText{
        std::istreambuf_iterator<char>(_DescriptorStream), std::istreambuf_iterator<char>()
    };
    auto _Descriptor = iCAX::TemplateRuntime::CTemplateCodec::ParseDescriptor(
        iCAX::TemplateRuntime::CStandardJsonCodec::Parse(_DescriptorText));
    _Descriptor.PackageDigest = "two-face-direction-final-shapes";

    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));

    struct SResult final
    {
        iCAX::TemplateRuntime::SNeutralModel Model;
        iCAX::OpenCascade::SNeutralModelEvaluation Evaluation;
        double SideCenterX = 0.0;
    };
    const auto _Generate = [&](const std::string& SidePosition_, const std::string& FaceName_)
    {
        iCAX::Data::ObjectMap _Parameters;
        for (const auto& _Definition : _Descriptor.Parameters)
            _Parameters[_Definition.Key] = _Definition.DefaultValue;
        _Parameters["sidePosition"] = SidePosition_;
        _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, _Parameters);
        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _Parameters, (_TemplateRoot / "template.py").string()));
        auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
        EXPECT_EQ(SidePosition_, _Model.Parameters.at("sidePosition").To<std::string>());
        const auto _Display = EvaluateTemplatePurposeGeometry(_Model, "display");
        ExpectUnmachinedTemplateDisplay(_Model, _Display);
        auto _Evaluation = EvaluateTemplatePurposeGeometry(_Model, "export");
        ExpectManufacturingBooleanIsEvaluated(_Model, _Evaluation);

        const auto _SideItem = std::find_if(
            _Model.Items.begin(), _Model.Items.end(),
            [&](const auto& Item_)
            {
                const auto _Face = Item_.Properties.find("tubeDesigner.faceName");
                return Item_.Key.starts_with("main_grid.horizontal.")
                    && _Face != Item_.Properties.end()
                    && _Face->second.Is<std::string>()
                    && _Face->second.To<std::string>() == FaceName_;
            });
        if (_SideItem == _Model.Items.end())
            throw std::runtime_error("two-face result has no side horizontal item: " + SidePosition_);
        const auto _Representation = _SideItem->Representations.find("export");
        if (_Representation == _SideItem->Representations.end())
            throw std::runtime_error("two-face side item has no export representation: " + SidePosition_);
        const auto& _Shape = _Evaluation.At(_Representation->second);
        if (_Shape.IsNull())
            throw std::runtime_error("two-face side item evaluated to a null shape: " + SidePosition_);
        Bnd_Box _Bounds;
        BRepBndLib::Add(_Shape, _Bounds);
        double _MinX = 0.0, _MinY = 0.0, _MinZ = 0.0;
        double _MaxX = 0.0, _MaxY = 0.0, _MaxZ = 0.0;
        _Bounds.Get(_MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ);
        const auto _DisplayBounds = RootSelectionShapeBounds(
            _Display.At(_SideItem->Representations.at("display")));
        EXPECT_NEAR((_MinX + _MaxX) / 2.0,
            (_DisplayBounds[0] + _DisplayBounds[3]) / 2.0, 1.0e-6);
        return SResult{ std::move(_Model), std::move(_Evaluation), (_MinX + _MaxX) / 2.0 };
    };

    auto _Left = _Generate("left", "左侧面");
    auto _Right = _Generate("right", "右侧面");
    // The front face is at Y=0 and the wall/interior is on -Y.  An observer
    // standing inside therefore sees world -X on the left and +X on the right.
    EXPECT_LT(_Left.SideCenterX, -500.0);
    EXPECT_GT(_Right.SideCenterX, 500.0);
    EXPECT_NEAR(-_Left.SideCenterX, _Right.SideCenterX, 1.0e-6);
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, ExplicitPythonWindowPurposeReturnsPhysicallySeparateRawGraphs)
{
    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto* _Directory : { "single_face_security_window", "two_face_security_window",
        "three_face_security_window", "five_face_security_window" })
    {
        SCOPED_TRACE(_Directory);
        const auto _Fixture = TemplateProtocolFixture(_Root, _Directory);
        EXPECT_TRUE(_Fixture.Parameters.at("accessDoorEnabled").To<bool>());
        EXPECT_EQ("escape", _Fixture.Parameters.at("doorUse").To<std::string>());
        EXPECT_DOUBLE_EQ(800.0, _Fixture.Parameters.at("doorClearWidth").To<double>());
        EXPECT_DOUBLE_EQ(1000.0, _Fixture.Parameters.at("doorClearHeight").To<double>());
        const auto _Legacy = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Fixture.Descriptor, _Fixture.Parameters, _Fixture.TemplatePath.string())));
        ASSERT_EQ(2u, _Legacy.Outputs.size());
        ASSERT_TRUE(_Legacy.Extensions.contains("tubeDesigner.securityWindowReview"));
        const auto _Review = _Legacy.Extensions.at("tubeDesigner.securityWindowReview")
            .To<iCAX::Data::ObjectMap>();
        EXPECT_EQ("outside", _Review.at("dimensions").To<std::string>());
        EXPECT_EQ("escape", _Review.at("openingUse").To<std::string>());
        EXPECT_TRUE(_Review.at("openingEnabled").To<bool>());
        EXPECT_FALSE(_Review.at("complianceCertified").To<bool>());
        EXPECT_NEAR(800.0, _Review.at("designClearWidth").To<double>(), 0.001);
        EXPECT_NEAR(1000.0, _Review.at("designClearHeight").To<double>(), 0.001);
        EXPECT_NEAR(830.0, _Review.at("fixedClearWidth").To<double>(), 0.001);
        EXPECT_NEAR(1000.0, _Review.at("fixedClearHeight").To<double>(), 0.001);
        EXPECT_FALSE(_Legacy.Parameters.contains("doorWidth"));
        EXPECT_FALSE(_Legacy.Parameters.contains("doorHeight"));
        for (const auto* _Purpose : { "display", "manufacturing" })
        {
            SCOPED_TRACE(_Purpose);
            const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, _Purpose);
            // Inspect Python's raw response before parsing or evaluating: a full
            // mixed graph later filtered by C++ cannot satisfy these assertions.
            ExpectSingleResultRawProtocol(_Raw, _Purpose, _Legacy.Items.size());
            ExpectWindowRawGraphIsPurposeSpecific(_Raw, _Purpose);
            const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Raw);
            ExpectExplicitResultMatchesLegacyItems(_Model, _Legacy, _Purpose);
            const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
            EXPECT_EQ(_Raw.at("geometry").To<iCAX::Data::VariantArray>().size(), _Geometry.Geometry.size());
            for (const auto& _Item : _Model.Items)
                EXPECT_FALSE(_Geometry.At(_Item.Representations.at("result")).IsNull()) << _Item.Key;

            if (std::string(_Directory) == "single_face_security_window"
                && std::string(_Purpose) == "manufacturing")
            {
                const auto _Outer = std::find_if(_Model.Items.begin(), _Model.Items.end(),
                    [](const auto& Item_) { return Item_.Key == "outer_frame.left.0001"; });
                ASSERT_NE(_Outer, _Model.Items.end());
                const auto _Measured = MeasureFinalPartGeometry(NormalizeLinearPartForManufacturing(
                    _Geometry.At(_Outer->Representations.at("result"))), "explicit-manufacturing", 1);
                ASSERT_TRUE(_Measured.at("available").To<bool>());
                EXPECT_NEAR(1800.0, _Measured.at("length").To<double>(), 0.01);
                const auto _Features = _Measured.at("features").To<iCAX::Data::VariantArray>();
                ASSERT_EQ(4u, _Features.size());
                constexpr std::array _Stations{ 200.0, 666.6666667, 1133.3333333, 1600.0 };
                for (std::size_t _Index = 0; _Index < _Stations.size(); ++_Index)
                    EXPECT_NEAR(_Stations[_Index],
                        _Features[_Index].To<iCAX::Data::ObjectMap>().at("station").To<double>(), 0.01);
            }
        }
    }
}

TEST(TemplateRuntimeTest, ExplicitPythonContinuousFrameReturnsAssemblyOrGroovedStockNotBoth)
{
    const auto _Root = std::filesystem::current_path();
    auto _Fixture = TemplateProtocolFixture(_Root, "single_face_security_window");
    _Fixture.Parameters["width"] = 1400.0;
    _Fixture.Parameters["frameLayout"] = std::string("four_sides");
    _Fixture.Parameters["frameJoinType"] = std::string("v_groove_90:sharp_v");
    // This fixture isolates the outer continuous frame, not the opening layout.
    _Fixture.Parameters["accessDoorEnabled"] = false;
    _Fixture.Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _Fixture.Descriptor, _Fixture.Parameters);
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    const auto _Legacy = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Fixture.Descriptor, _Fixture.Parameters, _Fixture.TemplatePath.string())));
    ASSERT_EQ(16u, _Legacy.Items.size());
    for (const auto* _Purpose : { "display", "manufacturing" })
    {
        SCOPED_TRACE(_Purpose);
        const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, _Purpose);
        ExpectSingleResultRawProtocol(_Raw, _Purpose, 16u);
        ExpectWindowRawGraphIsPurposeSpecific(_Raw, _Purpose);
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Raw);
        ExpectExplicitResultMatchesLegacyItems(_Model, _Legacy, _Purpose);
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
        EXPECT_EQ(_Model.Geometry.size(), _Geometry.Geometry.size());
        const auto _Frame = std::find_if(_Model.Items.begin(), _Model.Items.end(),
            [](const auto& Item_) { return Item_.Key == "outer_frame.continuous.0001"; });
        ASSERT_NE(_Frame, _Model.Items.end());
        const auto _Bounds = RootSelectionShapeBounds(_Geometry.At(_Frame->Representations.at("result")));
        const auto _GrooveNodeCount = std::count_if(_Model.Geometry.begin(), _Model.Geometry.end(),
            [](const auto& Node_) { return Node_.Key.find(".export.groove.") != std::string::npos; });
        if (std::string(_Purpose) == "display")
        {
            EXPECT_NEAR(1400.0, _Bounds[3] - _Bounds[0], 0.001);
            EXPECT_NEAR(1800.0, _Bounds[5] - _Bounds[2], 0.001);
            EXPECT_EQ(0, _GrooveNodeCount);
            EXPECT_TRUE(_Geometry.Geometry.contains("outer_frame.continuous.0001.display.compound"));
        }
        else
        {
            EXPECT_NEAR(_Frame->Properties.at("length").To<double>(), _Bounds[3] - _Bounds[0], 0.001);
            EXPECT_LT(_Bounds[5] - _Bounds[2], 100.0);
            EXPECT_GT(_GrooveNodeCount, 0);
            EXPECT_FALSE(_Geometry.Geometry.contains("outer_frame.continuous.0001.display.compound"));
            ExpectManufacturingBooleanIsEvaluated(_Model, _Geometry);
        }
    }
}

TEST(TemplateRuntimeTest, ExplicitPythonStairAndRailingPurposesUseTheSingleResultProtocol)
{
    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto* _Directory : { "straight_stair_railing", "straight_steel_staircase",
        "l_turn_steel_staircase", "u_turn_steel_staircase" })
    {
        SCOPED_TRACE(_Directory);
        const auto _Fixture = TemplateProtocolFixture(_Root, _Directory);
        const auto _Legacy = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Fixture.Descriptor, _Fixture.Parameters, _Fixture.TemplatePath.string())));
        ASSERT_EQ(2u, _Legacy.Outputs.size());
        for (const auto* _Purpose : { "display", "manufacturing" })
        {
            SCOPED_TRACE(_Purpose);
            const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, _Purpose);
            ExpectSingleResultRawProtocol(_Raw, _Purpose, _Legacy.Items.size());
            const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Raw);
            ExpectExplicitResultMatchesLegacyItems(_Model, _Legacy, _Purpose);
            // Stair templates do not need the window-specific shared key names.
            const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
            EXPECT_EQ(_Model.Geometry.size(), _Geometry.Geometry.size());
            for (const auto& _Item : _Model.Items)
                EXPECT_FALSE(_Geometry.At(_Item.Representations.at("result")).IsNull()) << _Item.Key;
        }
    }
}

TEST(TemplateRuntimeTest, SecurityWindowSecondRoundDefaultsProduceValidNativeSolids)
{
    using namespace iCAX::TemplateRuntime;
    const auto _Root = std::filesystem::current_path();
    CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto* _Directory : { "single_face_security_window", "two_face_security_window",
        "three_face_security_window", "five_face_security_window" })
    {
        SCOPED_TRACE(_Directory);
        const auto _Fixture = TemplateProtocolFixture(_Root, _Directory);
        EXPECT_EQ("insert", _Fixture.Parameters.at("mainHorizontalConnection").To<std::string>());
        if (std::string(_Directory) == "single_face_security_window")
        {
            EXPECT_EQ("four_sides", _Fixture.Parameters.at("frameLayout").To<std::string>());
            for (const auto* _Key : { "frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType" })
                EXPECT_EQ("miter_45", _Fixture.Parameters.at(_Key).To<std::string>());
        }
        for (const auto* _Purpose : { "display", "manufacturing" })
        {
            SCOPED_TRACE(_Purpose);
            const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, _Purpose);
            ExpectWindowRawGraphIsPurposeSpecific(_Raw, _Purpose);
            const auto _Model = CTemplateCodec::ParseNeutralModel(_Raw);
            EXPECT_EQ(CStandardJsonCodec::Serialize(_Fixture.Parameters),
                CStandardJsonCodec::Serialize(_Model.Parameters));
            const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
            ASSERT_FALSE(_Model.Items.empty());
            for (const auto& _Item : _Model.Items)
            {
                SCOPED_TRACE(_Item.Key);
                const auto& _Shape = _Evaluation.At(_Item.Representations.at("result"));
                ASSERT_FALSE(_Shape.IsNull());
                EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
                EXPECT_GT(RootSelectionShapeVolume(_Shape), 0.01);
            }
        }
    }
}

TEST(TemplateRuntimeTest, SecurityWindowSecondRoundWeldRemovesPostHolesButKeepsGridPiercings)
{
    using namespace iCAX::TemplateRuntime;
    const auto _Root = std::filesystem::current_path();
    CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto* _Directory : { "single_face_security_window", "two_face_security_window",
        "three_face_security_window", "five_face_security_window" })
    {
        SCOPED_TRACE(_Directory);
        auto _Fixture = TemplateProtocolFixture(_Root, _Directory);
        _Fixture.Parameters["accessDoorEnabled"] = false;
        std::array<double, 2> _PostVolume{}, _HorizontalVolume{};
        std::array<std::size_t, 2> _PostHoles{}, _HorizontalHoles{};
        for (std::size_t _Mode = 0; _Mode < 2; ++_Mode)
        {
            SCOPED_TRACE(_Mode == 0 ? "insert" : "weld");
            _Fixture.Parameters["mainHorizontalConnection"] = std::string(_Mode == 0 ? "insert" : "weld");
            const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, "manufacturing");
            const auto _Model = CTemplateCodec::ParseNeutralModel(_Raw);
            std::vector<std::string> _Roots;
            for (const auto& _Item : _Model.Items)
                if (_Item.Key.starts_with("main_grid.horizontal.")
                    || (_Item.Key.starts_with("outer_frame.")
                        && _Item.Properties.at("manufacturing.categoryKey").To<std::string>().find("vertical") != std::string::npos))
                    _Roots.push_back(_Item.Representations.at("result"));
            ASSERT_FALSE(_Roots.empty());
            const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model, _Roots);
            for (const auto& _Item : _Model.Items)
            {
                const auto& _Representation = _Item.Representations.at("result");
                if (!_Evaluation.Geometry.contains(_Representation)) continue;
                const auto& _Shape = _Evaluation.At(_Representation);
                EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid()) << _Item.Key;
                if (_Item.Key.starts_with("main_grid.horizontal."))
                    _HorizontalVolume[_Mode] += RootSelectionShapeVolume(_Shape);
                else if (_Item.Key.starts_with("outer_frame."))
                    _PostVolume[_Mode] += RootSelectionShapeVolume(_Shape);
            }
            for (const auto& _Node : _Model.Geometry)
            {
                if (_Node.Operator != EGeometryOperator::Profile2D || _Node.Key.find(".through.") == std::string::npos)
                    continue;
                if (_Node.Key.starts_with("main_grid.horizontal.")) ++_HorizontalHoles[_Mode];
                if (_Node.Key.starts_with("outer_frame.vertical.") || _Node.Key.starts_with("outer_frame.left.")
                    || _Node.Key.starts_with("outer_frame.right.")) ++_PostHoles[_Mode];
            }
            const auto _Display = InvokeExplicitTemplatePurpose(_Host, _Fixture, "display");
            ExpectWindowRawGraphIsPurposeSpecific(_Display, "display");
        }
        EXPECT_GT(_PostHoles[0], 0u);
        EXPECT_EQ(0u, _PostHoles[1]);
        EXPECT_GT(_HorizontalHoles[0], 0u);
        EXPECT_EQ(_HorizontalHoles[0], _HorizontalHoles[1]);
        EXPECT_GT(_PostVolume[1], _PostVolume[0]); // The missing holes are missing from the real solids.
        EXPECT_GT(_HorizontalVolume[0], _HorizontalVolume[1]); // Welded rails lose their insertion stubs.
    }
}

TEST(TemplateRuntimeTest, SecurityWindowSecondRoundRailMitersHaveCorrectBoundsAndNoCornerOverlap)
{
    using namespace iCAX::TemplateRuntime;
    using iCAX::Data::ObjectMap;
    const auto _Root = std::filesystem::current_path();
    CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    for (const auto* _Directory : { "three_face_security_window", "five_face_security_window" })
    {
        SCOPED_TRACE(_Directory);
        auto _Fixture = TemplateProtocolFixture(_Root, _Directory);
        _Fixture.Parameters["accessDoorEnabled"] = false;
        _Fixture.Parameters["frameCornerJoin"] = std::string("rail_miter");
        for (const auto* _Connection : { "insert", "weld" })
        {
            SCOPED_TRACE(_Connection);
            _Fixture.Parameters["mainHorizontalConnection"] = std::string(_Connection);
            const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, "manufacturing");
            const auto _Model = CTemplateCodec::ParseNeutralModel(_Raw);
            std::vector<std::string> _Roots;
            for (const auto& _Item : _Model.Items)
                if (_Item.Key.starts_with("outer_frame.")) _Roots.push_back(_Item.Representations.at("result"));
            const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model, _Roots);
            Bnd_Box _Envelope;
            std::vector<TopoDS_Shape> _TopRails;
            std::size_t _MiterCount = 0;
            for (const auto& _Item : _Model.Items)
            {
                if (!_Item.Key.starts_with("outer_frame.")) continue;
                SCOPED_TRACE(_Item.Key);
                const auto& _Shape = _Evaluation.At(_Item.Representations.at("result"));
                EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
                EXPECT_GT(RootSelectionShapeVolume(_Shape), 0.01);
                BRepBndLib::Add(_Shape, _Envelope);
                const auto _EndProcess = _Item.Properties.at("tubeDesigner.endProcess").To<ObjectMap>();
                const auto _Planes = _EndProcess.at("cutPlanes").To<iCAX::Data::VariantArray>();
                if (!_Planes.empty())
                {
                    ++_MiterCount;
                    EXPECT_TRUE(_Item.Properties.contains("tubeDesigner.displayApproximation"));
                }
                if (_Item.Key.starts_with("outer_frame.top.")) _TopRails.push_back(_Shape);
                if (_Item.Key.starts_with("outer_frame.vertical."))
                {
                    const auto _Bounds = RootSelectionShapeBounds(_Shape);
                    EXPECT_NEAR(_Fixture.Parameters.at("frameWidth").To<double>(), _Bounds[2], 0.001);
                    EXPECT_NEAR(_Fixture.Parameters.at("height").To<double>()
                        - _Fixture.Parameters.at("frameWidth").To<double>(), _Bounds[5], 0.001);
                }
            }
            EXPECT_GT(_MiterCount, 0u);
            std::array<double, 6> _Bounds{};
            _Envelope.Get(_Bounds[0], _Bounds[1], _Bounds[2], _Bounds[3], _Bounds[4], _Bounds[5]);
            EXPECT_NEAR(_Fixture.Parameters.at("frontWidth").To<double>(), _Bounds[3] - _Bounds[0], 0.001);
            EXPECT_NEAR(_Fixture.Parameters.at("height").To<double>(), _Bounds[5] - _Bounds[2], 0.001);
            const double _Depth = std::string(_Directory) == "five_face_security_window"
                ? _Fixture.Parameters.at("depth").To<double>()
                : std::max(_Fixture.Parameters.at("leftWidth").To<double>(), _Fixture.Parameters.at("rightWidth").To<double>());
            EXPECT_NEAR(_Depth, _Bounds[4] - _Bounds[1], 0.001);
            for (std::size_t _A = 0; _A < _TopRails.size(); ++_A)
                for (std::size_t _B = _A + 1; _B < _TopRails.size(); ++_B)
                {
                    BRepAlgoAPI_Common _Common(_TopRails[_A], _TopRails[_B]);
                    ASSERT_TRUE(_Common.IsDone());
                    EXPECT_LT(std::abs(RootSelectionShapeVolume(_Common.Shape())), 0.01);
                }
            ExpectWindowRawGraphIsPurposeSpecific(InvokeExplicitTemplatePurpose(_Host, _Fixture, "display"), "display");
        }
    }
}

TEST(TemplateRuntimeTest, SecurityWindowPublicStyleContainsOnlyTubeParts)
{
    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    auto _Fixture = TemplateProtocolFixture(_Root, "single_face_security_window");
    EXPECT_FALSE(_Fixture.Parameters.contains("mainInfillMode"));
    EXPECT_FALSE(_Fixture.Parameters.contains("centerPlateWidth"));
    for (const bool _DoorEnabled : { false, true })
    {
        SCOPED_TRACE(_DoorEnabled);
        _Fixture.Parameters["accessDoorEnabled"] = _DoorEnabled;
        const auto _Raw = InvokeExplicitTemplatePurpose(_Host, _Fixture, "manufacturing");
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Raw);
        ASSERT_FALSE(_Model.Items.empty());
        for (const auto& _Item : _Model.Items)
        {
            EXPECT_FALSE(_Item.Key.starts_with("center_plate."));
            EXPECT_TRUE(_Item.Properties.contains("tubeDesigner.profile"));
        }
    }
}

TEST(TemplateRuntimeTest, EveryModularGuardrailPresetProducesValidNonOverlappingSolids)
{
    using iCAX::Data::ObjectMap;
    const auto _Root = std::filesystem::current_path();
    iCAX::TemplateRuntime::CPythonTemplateHost _Host(EmbeddedPythonHostOptions(_Root));
    const auto _Base = TemplateProtocolFixture(_Root, "modular_guardrail");
    const auto _Catalog = _Base.Descriptor.Extensions.at("catalog").To<ObjectMap>();
    const auto _Presets = _Catalog.at("presets").To<iCAX::Data::VariantArray>();
    ASSERT_GE(_Presets.size(), 32u);
    for (const auto& _PresetValue : _Presets)
    {
        const auto _Preset = _PresetValue.To<ObjectMap>();
        SCOPED_TRACE(_Preset.at("id").To<std::string>());
        auto _Fixture = _Base;
        for (const auto& [_Key, _Value] : _Preset.at("parameters").To<ObjectMap>())
            _Fixture.Parameters[_Key] = _Value;
        _Fixture.Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Fixture.Descriptor, _Fixture.Parameters);
        auto _Document = InvokeExplicitTemplatePurpose(_Host, _Fixture, "manufacturing");
        ResolveTemplateComponentResources(_Document,
            _Root / "src/apps/tube-designer/templates/modular_guardrail", _Base.Descriptor.Extensions,
            _Root / "src/apps/tube-designer/models", nullptr);
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document);
        ASSERT_EQ(1u, _Model.Outputs.size());
        ASSERT_EQ("result", _Model.Outputs.front().Purpose);
        ASSERT_GT(_Model.Items.size(), 2u);
        const auto _Geometry = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
        std::vector<TopoDS_Shape> _Shapes;
        std::vector<std::array<double, 6>> _Bounds;
        for (const auto& _Item : _Model.Items)
        {
            SCOPED_TRACE(_Item.Key);
            const auto& _Shape = _Geometry.At(_Item.Representations.at("result"));
            ASSERT_FALSE(_Shape.IsNull());
            EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
            EXPECT_GT(RootSelectionShapeVolume(_Shape), 0.01);
            const auto _Kind = _Item.Properties.at("manufacturing.partKind").To<std::string>();
            const bool _IsTube = _Kind == "tube" || _Kind == "profile" || _Kind == "linear";
            EXPECT_TRUE(_IsTube || _Kind == "plate" || _Kind == "glass" || _Kind == "accessory");
            EXPECT_EQ(_IsTube, _Item.Properties.contains("tubeDesigner.profile"));
            _Shapes.push_back(_Shape);
            _Bounds.push_back(RootSelectionShapeBounds(_Shape));
        }
        // Skip face contact and separated pairs; only positive-volume bounds
        // need an exact common operation. Glass clamps may touch faces, but no
        // tube, plate, glass or accessory pair may overlap by positive volume.
        for (std::size_t _A = 0; _A < _Shapes.size(); ++_A)
        {
            for (std::size_t _B = _A + 1; _B < _Shapes.size(); ++_B)
            {
                bool _Candidate = true;
                for (std::size_t _Axis = 0; _Axis < 3; ++_Axis)
                    _Candidate &= std::min(_Bounds[_A][_Axis + 3], _Bounds[_B][_Axis + 3])
                        - std::max(_Bounds[_A][_Axis], _Bounds[_B][_Axis]) > 1e-4;
                if (!_Candidate) continue;
                SCOPED_TRACE(_Model.Items[_A].Key + " / " + _Model.Items[_B].Key);
                BRepAlgoAPI_Common _Common(_Shapes[_A], _Shapes[_B]);
                ASSERT_TRUE(_Common.IsDone());
                EXPECT_LT(std::abs(RootSelectionShapeVolume(_Common.Shape())), 0.01);
            }
        }
    }
}
