#include "pch.h"

#include <TubeDesigner/SecurityWindow1Template.h>
#include <TubeDesigner/SecurityWindow2Template.h>
#include <TubeDesigner/SecurityWindow3Template.h>
#include <TubeDesigner/SingleFaceSecurityWindowTemplate.h>
#include <TubeDesigner/PartListXlsxExporter.h>
#include <TubeDesigner/FinalGeometryMeasurement.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <TemplateRuntime/PythonTemplateHost.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TopExp_Explorer.hxx>

namespace
{
    using namespace iCAX::TubeDesigner;

    const SGeneratedPart& PartAt(const SGeneratedProduct& Product_, std::size_t Index_)
    {
        EXPECT_LT(Index_, Product_.Parts.size());
        return Product_.Parts.at(Index_);
    }

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
            { "contour", iCAX::Data::ObjectMap{
                { "kind", std::string("roundedRectangle") },
                { "width", Width_ }, { "height", Height_ }, { "radius", Radius_ }
            } }
        };
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
        { 1, "单面防盗窗 001", "FD-001", 1, "FD-001-001", "左外框", "rect",
            40.0, 20.0, 1.5, 3.0, 1800.0, 1, "FD-001-001.step",
            "单面防盗窗 001/FD-001-001.step" },
        { 1, "单面防盗窗 001", "FD-001", 2, "FD-001-002", "中间竖管", "round",
            19.0, 19.0, 1.0, 0.0, 1740.0, 2, "FD-001-002.step",
            "单面防盗窗 001/FD-001-002.step" }
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
    EXPECT_EQ(std::string("PK\x05\x06", 4), _Content.substr(_Content.size() - 22, 4));
}

TEST(TemplateRuntimeTest, NeutralModelCodecAndOpenCascadeAdapterExecuteGenericTubeGraph)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    VariantArray _Geometry{
        ObjectMap{
            { "key", std::string("profile.outer") },
            { "operator", std::string("profile2d") },
            { "inputs", VariantArray{} },
            { "arguments", RoundedProfile(40.0, 20.0, 3.0) }
        },
        ObjectMap{
            { "key", std::string("solid.outer") },
            { "operator", std::string("extrude") },
            { "inputs", VariantArray{ std::string("profile.outer") } },
            { "arguments", ObjectMap{ { "vector", NumberArray({ 0.0, 0.0, 100.0 }) } } }
        },
        ObjectMap{
            { "key", std::string("profile.inner") },
            { "operator", std::string("profile2d") },
            { "inputs", VariantArray{} },
            { "arguments", ObjectMap{
                { "placement", ObjectMap{
                    { "origin", NumberArray({ 0.0, 0.0, 0.0 }) },
                    { "xAxis", NumberArray({ 1.0, 0.0, 0.0 }) },
                    { "yAxis", NumberArray({ 0.0, 1.0, 0.0 }) }
                } },
                { "contour", ObjectMap{
                    { "kind", std::string("polygon") },
                    { "points", VariantArray{
                        NumberArray({ -18.0, -8.0 }), NumberArray({ 18.0, -8.0 }),
                        NumberArray({ 18.0, 8.0 }), NumberArray({ -18.0, 8.0 })
                    } }
                } }
            } }
        },
        ObjectMap{
            { "key", std::string("solid.inner") },
            { "operator", std::string("extrude") },
            { "inputs", VariantArray{ std::string("profile.inner") } },
            { "arguments", ObjectMap{
                { "vector", NumberArray({ 0.0, 0.0, 100.0 }) },
                { "extendStart", 1.0 }, { "extendEnd", 1.0 }
            } }
        },
        ObjectMap{
            { "key", std::string("solid.final") },
            { "operator", std::string("boolean") },
            { "inputs", VariantArray{ std::string("solid.outer"), std::string("solid.inner") } },
            { "arguments", ObjectMap{
                { "operation", std::string("subtract") },
                { "target", std::string("solid.outer") },
                { "tools", VariantArray{ std::string("solid.inner") } }
            } }
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
    EXPECT_EQ(5u, _Evaluation.Geometry.size());
    EXPECT_FALSE(_Evaluation.At("solid.final").IsNull());
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

TEST(TemplateRuntimeTest, PythonWorkerEvaluatesTheRealTemplatePackageWhenConfigured)
{
    char* _PythonBuffer = nullptr;
    std::size_t _PythonLength = 0;
    if (_dupenv_s(&_PythonBuffer, &_PythonLength, "ICAX_PYTHON_EXECUTABLE") != 0
        || !_PythonBuffer || _PythonLength <= 1)
    {
        std::free(_PythonBuffer);
        return;
    }
    const std::unique_ptr<char, decltype(&std::free)> _PythonValue(_PythonBuffer, &std::free);
    const auto _Root = std::filesystem::current_path();
    const auto _DescriptorPath = _Root / "src/apps/tube-designer/templates/single_face_security_window/template.json";
    const auto _TemplatePath = _DescriptorPath.parent_path() / "template.py";
    const auto _WorkerPath = _Root / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
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
    _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
        _Descriptor, _Parameters);

    iCAX::TemplateRuntime::CPythonTemplateHost _Host({
        std::filesystem::path(_PythonValue.get()), _WorkerPath, _WorkerPath.parent_path()
    });
    const auto _Response = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
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
    ASSERT_EQ(1u, _Model.Tables.size());
    EXPECT_EQ(15u, _Model.Tables.front().Rows.size());
    const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
    EXPECT_EQ(_Model.Geometry.size(), _Evaluation.Geometry.size());
    for (const auto& _Item : _Model.Items)
        EXPECT_FALSE(_Evaluation.At(_Item.Representations.at("display")).IsNull());
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

    _Parameters["width"] = 1400.0;
    _Parameters["frameLayout"] = std::string("four_sides");
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
    const auto _SecondEvaluation = iCAX::OpenCascade::EvaluateNeutralModel(_SecondModel);
    EXPECT_FALSE(_SecondEvaluation.At(
        _ContinuousFrame.Representations.at("display")).IsNull());
    EXPECT_FALSE(_SecondEvaluation.At(
        _ContinuousFrame.Representations.at("export")).IsNull());

    _Parameters["frameJoinType"] = std::string("miter_45");
    const auto _MiterResponse = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _MiterModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_MiterResponse);
    ASSERT_EQ(19u, _MiterModel.Items.size());
    EXPECT_EQ("outer_frame.left.0001", _MiterModel.Items.front().Key);
    const auto _MiterEvaluation = iCAX::OpenCascade::EvaluateNeutralModel(_MiterModel);
    for (std::size_t _Index = 0; _Index < 4; ++_Index)
    {
        const auto& _Item = _MiterModel.Items[_Index];
        EXPECT_FALSE(_MiterEvaluation.At(
            _Item.Representations.at("export")).IsNull());
    }

    _Parameters["frameLayout"] = std::string("left_right");
    _Parameters["accessDoorEnabled"] = true;
    const auto _DoorResponse = _Host.Invoke(
        iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
            _Descriptor, _Parameters, _TemplatePath.string()));
    const auto _DoorModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_DoorResponse);
    ASSERT_EQ(2u, _DoorModel.Relationships.size());
    EXPECT_EQ("hinge", _DoorModel.Relationships.front().Kind);
    EXPECT_EQ(2u, _DoorModel.Relationships.front().ItemKeys.size());
    EXPECT_TRUE(_Host.IsRunning());
}

TEST(TemplateRuntimeTest, MultiFaceInspectionDoorCanBePlacedOnEveryAvailableFace)
{
    char* _PythonBuffer = nullptr;
    std::size_t _PythonLength = 0;
    if (_dupenv_s(&_PythonBuffer, &_PythonLength, "ICAX_PYTHON_EXECUTABLE") != 0
        || !_PythonBuffer || _PythonLength <= 1)
    {
        std::free(_PythonBuffer);
        return;
    }
    const std::unique_ptr<char, decltype(&std::free)> _PythonValue(_PythonBuffer, &std::free);
    const auto _Root = std::filesystem::current_path();
    const auto _WorkerPath = _Root / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
    iCAX::TemplateRuntime::CPythonTemplateHost _Host({
        std::filesystem::path(_PythonValue.get()), _WorkerPath, _WorkerPath.parent_path()
    });
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
        if (_Case.SidePosition[0] != '\0')
            _Parameters["sidePosition"] = std::string(_Case.SidePosition);
        _Parameters = iCAX::TemplateRuntime::CTemplateCodec::ValidateAndNormalizeParameters(
            _Descriptor, _Parameters);

        const auto _Response = _Host.Invoke(
            iCAX::TemplateRuntime::CTemplateCodec::MakeEvaluationRequest(
                _Descriptor, _Parameters, (_TemplateRoot / "template.py").string()));
        const auto _Model = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Response);
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
            const auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model);
            for (const auto& _Item : _Model.Items)
            {
                if (!_Item.Key.starts_with("access_door.")) continue;
                EXPECT_FALSE(_Evaluation.At(_Item.Representations.at("display")).IsNull())
                    << _Case.Directory << "/" << _Case.Face << "/" << _Item.Key;
            }
        }
    }
}

TEST(TemplateRuntimeTest, TwoFaceDirectionRebuildsMirroredFinalShapes)
{
    char* _PythonBuffer = nullptr;
    std::size_t _PythonLength = 0;
    if (_dupenv_s(&_PythonBuffer, &_PythonLength, "ICAX_PYTHON_EXECUTABLE") != 0
        || !_PythonBuffer || _PythonLength <= 1)
    {
        std::free(_PythonBuffer);
        return;
    }
    const std::unique_ptr<char, decltype(&std::free)> _PythonValue(_PythonBuffer, &std::free);
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

    const auto _WorkerPath = _Root / "src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_worker.py";
    iCAX::TemplateRuntime::CPythonTemplateHost _Host({
        std::filesystem::path(_PythonValue.get()), _WorkerPath, _WorkerPath.parent_path()
    });

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
        auto _Evaluation = iCAX::OpenCascade::EvaluateNeutralModel(_Model);

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
        const auto _Representation = _SideItem->Representations.find("display");
        if (_Representation == _SideItem->Representations.end())
            throw std::runtime_error("two-face side item has no display representation: " + SidePosition_);
        const auto& _Shape = _Evaluation.At(_Representation->second);
        if (_Shape.IsNull())
            throw std::runtime_error("two-face side item evaluated to a null shape: " + SidePosition_);
        Bnd_Box _Bounds;
        BRepBndLib::Add(_Shape, _Bounds);
        double _MinX = 0.0, _MinY = 0.0, _MinZ = 0.0;
        double _MaxX = 0.0, _MaxY = 0.0, _MaxZ = 0.0;
        _Bounds.Get(_MinX, _MinY, _MinZ, _MaxX, _MaxY, _MaxZ);
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

TEST(TubeDesignerSecurityWindow1Test, DefaultParametersGenerateStableSevenPartOrder)
{
    const auto _Product = GenerateSecurityWindow1(SSecurityWindow1Parameters{});

    EXPECT_EQ("TD-001", _Product.ProductCode);
    EXPECT_EQ("security-window-1", _Product.TemplateID);
    EXPECT_EQ("1.0.0", _Product.TemplateVersion);
    ASSERT_EQ(7u, _Product.Parts.size());

    EXPECT_EQ("TD-001-001", PartAt(_Product, 0).PartNumber);
    EXPECT_EQ("left-frame", PartAt(_Product, 0).Role);
    EXPECT_NEAR(1800.0, PartAt(_Product, 0).Length, 1.0e-9);

    EXPECT_EQ("TD-001-002", PartAt(_Product, 1).PartNumber);
    EXPECT_EQ("right-frame", PartAt(_Product, 1).Role);
    EXPECT_NEAR(1800.0, PartAt(_Product, 1).Length, 1.0e-9);

    EXPECT_EQ("TD-001-003", PartAt(_Product, 2).PartNumber);
    EXPECT_EQ("middle-vertical", PartAt(_Product, 2).Role);
    EXPECT_NEAR(1750.0, PartAt(_Product, 2).Length, 1.0e-9);

    for (std::size_t _Index = 3; _Index < _Product.Parts.size(); ++_Index)
    {
        EXPECT_EQ("horizontal", PartAt(_Product, _Index).Role);
        EXPECT_NEAR(1150.0, PartAt(_Product, _Index).Length, 1.0e-9);
    }
    EXPECT_EQ("TD-001-007", PartAt(_Product, 6).PartNumber);
}

TEST(TubeDesignerSecurityWindow1Test, DefaultConnectionsKeepExplicitTargetAndInsertedRoles)
{
    const auto _Product = GenerateSecurityWindow1(SSecurityWindow1Parameters{});

    ASSERT_EQ(12u, _Product.Joints.size());
    std::size_t _FrameCuts = 0;
    std::size_t _HorizontalCuts = 0;
    for (const auto& _Joint : _Product.Joints)
    {
        EXPECT_EQ("through", _Joint.Mode);
        EXPECT_NEAR(0.1, _Joint.Clearance, 1.0e-9);
        if (_Joint.TargetPartIndex == 1 || _Joint.TargetPartIndex == 2)
        {
            EXPECT_GE(_Joint.InsertedPartIndex, 4u);
            EXPECT_LE(_Joint.InsertedPartIndex, 7u);
            ++_FrameCuts;
        }
        else
        {
            EXPECT_GE(_Joint.TargetPartIndex, 4u);
            EXPECT_LE(_Joint.TargetPartIndex, 7u);
            EXPECT_EQ(3u, _Joint.InsertedPartIndex);
            ++_HorizontalCuts;
        }
    }
    EXPECT_EQ(8u, _FrameCuts);
    EXPECT_EQ(4u, _HorizontalCuts);
}

TEST(TubeDesignerSecurityWindow1Test, CountsDrivePartAndJointTotals)
{
    SSecurityWindow1Parameters _Parameters;
    _Parameters.ProductCode = "ORDER-42";
    _Parameters.HorizontalCount = 3;
    _Parameters.MiddleVerticalCount = 2;

    const auto _Product = GenerateSecurityWindow1(_Parameters);

    ASSERT_EQ(7u, _Product.Parts.size());
    EXPECT_EQ(12u, _Product.Joints.size());
    EXPECT_EQ("ORDER-42-007", _Product.Parts.back().PartNumber);
}

TEST(TubeDesignerSecurityWindow1Test, RejectsUnsupportedOrUnsafeParameters)
{
    SSecurityWindow1Parameters _Parameters;
    _Parameters.MiddleVerticalCount = 11;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Frame.Type = "round";
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalCount = 2;
    _Parameters.FirstHorizontalTopOffset = 1700.0;
    _Parameters.LastHorizontalBottomOffset = 200.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalCount = 101;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.HorizontalBranchReserve = -2.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Width = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);

    _Parameters = {};
    _Parameters.Height = 60.0;
    _Parameters.VerticalBranchReserve = 0.0;
    EXPECT_THROW((void)GenerateSecurityWindow1(_Parameters), std::invalid_argument);
}

TEST(TubeDesignerSecurityWindow2Test, DefaultHandleTemplateGeneratesIndependentRegions)
{
    const auto _Product = GenerateSecurityWindow2(SSecurityWindow2Parameters{});

    EXPECT_EQ("security-window-2", _Product.TemplateID);
    ASSERT_EQ(11u, _Product.Parts.size());
    EXPECT_EQ("handle-left-vertical", PartAt(_Product, 2).Role);
    EXPECT_EQ("handle-right-vertical", PartAt(_Product, 3).Role);
    EXPECT_EQ("handle-top-horizontal", PartAt(_Product, 4).Role);
    EXPECT_EQ("handle-bottom-horizontal", PartAt(_Product, 5).Role);
    EXPECT_EQ("TD-002-011", _Product.Parts.back().PartNumber);
    EXPECT_EQ(8u, _Product.Joints.size());
}

TEST(TubeDesignerSecurityWindow3Test, DefaultVGrooveFrameSeparatesFoldedPreviewFromUnfoldedManufacturing)
{
    const SSecurityWindow3Parameters _Parameters;
    const auto _Product = GenerateSecurityWindow3(_Parameters);

    EXPECT_EQ("security-window-3", _Product.TemplateID);
    ASSERT_EQ(6u, _Product.Parts.size());
    const auto& _Frame = PartAt(_Product, 0);
    EXPECT_EQ("outer-frame", _Frame.Role);
    ASSERT_TRUE(_Frame.PreviewGeometry.has_value());
    ASSERT_TRUE(_Frame.ManufacturingGeometry.has_value());
    EXPECT_EQ("folded-rect-frame", _Frame.PreviewGeometry->Kind);
    EXPECT_NEAR(1200.0, _Frame.PreviewGeometry->OuterMaxX, 1.0e-9);
    EXPECT_NEAR(1800.0, _Frame.PreviewGeometry->OuterMaxY, 1.0e-9);
    EXPECT_EQ(4u, _Frame.ManufacturingGeometry->BendCount);
    EXPECT_EQ(4u, _Frame.ManufacturingGeometry->Cutters.size());

    const auto _BendAllowance = 3.14159265358979323846 * 0.62 * 2.0 / 2.0;
    const auto _ExpectedLength = 2.0 * (1200.0 - 4.0)
        + 2.0 * (1800.0 - 4.0) + 4.0 * _BendAllowance;
    EXPECT_NEAR(_ExpectedLength, _Frame.Length, 1.0e-9);
    EXPECT_NEAR(_ExpectedLength, _Frame.ManufacturingGeometry->BaseLength, 1.0e-9);
    EXPECT_EQ(9u, _Product.Joints.size());
}

TEST(TubeDesignerSecurityWindow3Test, FlatJoinModesProduceFourIndependentFrameParts)
{
    SSecurityWindow3Parameters _Parameters;
    _Parameters.FrameJoinType = "miter_45";
    auto _Product = GenerateSecurityWindow3(_Parameters);
    ASSERT_EQ(9u, _Product.Parts.size());
    EXPECT_EQ("left-frame", PartAt(_Product, 0).Role);
    EXPECT_EQ("miter-45", PartAt(_Product, 0).StartCut);
    EXPECT_FALSE(PartAt(_Product, 0).ManufacturingGeometry.has_value());

    _Parameters.FrameJoinType = "butt_90";
    _Parameters.FrameButtWrapMode = "side_wraps_horizontal";
    _Product = GenerateSecurityWindow3(_Parameters);
    ASSERT_EQ(9u, _Product.Parts.size());
    EXPECT_NEAR(1100.0, PartAt(_Product, 2).Length, 1.0e-9);

    _Parameters.FrameButtWrapMode = "horizontal_wraps_side";
    _Product = GenerateSecurityWindow3(_Parameters);
    EXPECT_NEAR(1700.0, PartAt(_Product, 0).Length, 1.0e-9);
}

TEST(TubeDesignerSecurityWindow3Test, VGrooveOptionsCreateExplicitManufacturingCutters)
{
    SSecurityWindow3Parameters _Parameters;
    _Parameters.VGrooveStyle = "rounded_v";
    auto _Product = GenerateSecurityWindow3(_Parameters);
    ASSERT_TRUE(PartAt(_Product, 0).ManufacturingGeometry.has_value());
    EXPECT_EQ("xy-profile-prism", PartAt(_Product, 0).ManufacturingGeometry->Cutters.front().Kind);

    _Parameters.VGrooveStyle = "sharp_v";
    _Parameters.VGrooveBottomCut = true;
    _Parameters.VGrooveReliefHole = true;
    _Parameters.VGrooveWallOvercut = true;
    _Product = GenerateSecurityWindow3(_Parameters);
    ASSERT_TRUE(PartAt(_Product, 0).ManufacturingGeometry.has_value());
    EXPECT_EQ(12u, PartAt(_Product, 0).ManufacturingGeometry->Cutters.size());

    _Parameters.VGrooveStyle = "left_arc";
    EXPECT_THROW((void)GenerateSecurityWindow3(_Parameters), std::invalid_argument);
}

TEST(TubeDesignerSingleFaceSecurityWindowTest, FrameLayoutsShareOneStableTemplateIdentity)
{
    SSingleFaceSecurityWindowParameters _Parameters;

    auto _Product = GenerateSingleFaceSecurityWindow(_Parameters);
    EXPECT_EQ("single-face-security-window", _Product.TemplateID);
    EXPECT_EQ("1.0.0", _Product.TemplateVersion);
    ASSERT_EQ(7u, _Product.Parts.size());
    EXPECT_EQ("left-frame", PartAt(_Product, 0).Role);
    EXPECT_EQ("main-vertical", _Product.Parts.back().Role);
    EXPECT_EQ("round", _Product.Parts.back().Profile.Type);

    _Parameters.FrameLayout = "top_bottom";
    _Product = GenerateSingleFaceSecurityWindow(_Parameters);
    EXPECT_EQ("single-face-security-window", _Product.TemplateID);
    ASSERT_EQ(7u, _Product.Parts.size());
    EXPECT_EQ("bottom-frame", PartAt(_Product, 0).Role);
    EXPECT_EQ("top-frame", PartAt(_Product, 1).Role);

    _Parameters.FrameLayout = "four_sides";
    _Product = GenerateSingleFaceSecurityWindow(_Parameters);
    EXPECT_EQ("single-face-security-window", _Product.TemplateID);
    ASSERT_EQ(6u, _Product.Parts.size());
    EXPECT_EQ("outer-frame", PartAt(_Product, 0).Role);
    EXPECT_TRUE(PartAt(_Product, 0).PreviewGeometry.has_value());
    EXPECT_TRUE(PartAt(_Product, 0).ManufacturingGeometry.has_value());
}

TEST(TubeDesignerSingleFaceSecurityWindowTest, AccessDoorIsNestedFrameLeafAndHingeAssembly)
{
    SSingleFaceSecurityWindowParameters _Parameters;
    _Parameters.AccessDoorEnabled = true;
    const auto _Product = GenerateSingleFaceSecurityWindow(_Parameters);

    EXPECT_EQ("single-face-security-window", _Product.TemplateID);
    ASSERT_EQ(12u, _Product.Parts.size());
    EXPECT_EQ("access-door-fixed-frame", PartAt(_Product, 8).Role);
    EXPECT_EQ("access-door-leaf-frame", PartAt(_Product, 9).Role);
    EXPECT_EQ("rect", PartAt(_Product, 10).Profile.Type);
    EXPECT_EQ("round", PartAt(_Product, 11).Profile.Type);
    EXPECT_EQ("access-door-fixed-frame", PartAt(_Product, 8).PreviewAssemblyKey);
    EXPECT_EQ("固定门框", PartAt(_Product, 8).PreviewAssemblyName);
    for (std::size_t _Index = 9; _Index < 12; ++_Index)
    {
        EXPECT_EQ("access-door-leaf", PartAt(_Product, _Index).PreviewAssemblyKey);
        EXPECT_EQ("活动门扇", PartAt(_Product, _Index).PreviewAssemblyName);
    }
    const auto _HingeCount = std::count_if(
        _Product.Joints.begin(), _Product.Joints.end(),
        [](const auto& Joint_) { return Joint_.Mode == "hinge"; });
    EXPECT_EQ(2, _HingeCount);
}

TEST(TubeDesignerSingleFaceSecurityWindowTest, DoorFramesChooseTheirConnectionProcessesIndependently)
{
    SSingleFaceSecurityWindowParameters _Parameters;
    _Parameters.AccessDoorEnabled = true;
    _Parameters.DoorFrameProcess.JoinType = "miter_45";
    _Parameters.DoorLeafFrameProcess.JoinType = "butt_90";
    _Parameters.DoorLeafFrameProcess.ButtWrapMode = "horizontal_wraps_side";
    const auto _Product = GenerateSingleFaceSecurityWindow(_Parameters);

    ASSERT_EQ(18u, _Product.Parts.size());
    for (std::size_t _Index = 8; _Index < 12; ++_Index)
    {
        EXPECT_TRUE(PartAt(_Product, _Index).Role.starts_with("access-door-fixed-"));
        EXPECT_EQ("access-door-fixed-frame", PartAt(_Product, _Index).PreviewAssemblyKey);
        EXPECT_EQ("miter-45", PartAt(_Product, _Index).StartCut);
    }
    for (std::size_t _Index = 12; _Index < 18; ++_Index)
    {
        EXPECT_TRUE(PartAt(_Product, _Index).Role.starts_with("access-door-leaf-"));
        EXPECT_EQ("access-door-leaf", PartAt(_Product, _Index).PreviewAssemblyKey);
    }
    EXPECT_EQ("square", PartAt(_Product, 12).StartCut);
    EXPECT_NEAR(238.0, PartAt(_Product, 12).Length, 1.0e-9);
}

TEST(TubeDesignerSingleFaceSecurityWindowTest, RejectsUnknownFrameLayout)
{
    SSingleFaceSecurityWindowParameters _Parameters;
    _Parameters.FrameLayout = "unknown";
    EXPECT_THROW((void)GenerateSingleFaceSecurityWindow(_Parameters), std::invalid_argument);
}
