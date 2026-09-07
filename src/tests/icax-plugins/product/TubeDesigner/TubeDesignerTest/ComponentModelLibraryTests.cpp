#include "pch.h"

#include <TubeDesigner/ComponentModelLibrary.h>
#include <OpenCascadeResourceImport/OpenCascadeNeutralModelEvaluator.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <TemplateRuntime/TemplateCodec.h>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace iCAX::TubeDesigner;
    using namespace iCAX::Application;
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;
    using iCAX::TemplateRuntime::CStandardJsonCodec;
    using iCAX::TemplateRuntime::CTemplateCodec;

    std::filesystem::path ComponentTestRoot()
    {
        const auto _Path = std::filesystem::temp_directory_path()
            / ("icax-component-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(_Path))
            throw std::runtime_error("could not create isolated component test directory");
        return _Path;
    }

    std::string ComponentPathText(const std::filesystem::path& Path_)
    {
        const auto _Utf8 = Path_.u8string();
        return { reinterpret_cast<const char*>(_Utf8.data()), _Utf8.size() };
    }

    void WriteComponentBytes(const std::filesystem::path& Path_, const std::string& Bytes_)
    {
        std::filesystem::create_directories(Path_.parent_path());
        std::ofstream _Stream(Path_, std::ios::binary | std::ios::trunc);
        if (!_Stream || !_Stream.write(Bytes_.data(), static_cast<std::streamsize>(Bytes_.size())))
            throw std::runtime_error("could not write component test fixture");
    }

    void WriteComponentBRep(const std::filesystem::path& Path_, const TopoDS_Shape& Shape_)
    {
        std::ostringstream _Stream;
        BRepTools::Write(Shape_, _Stream);
        WriteComponentBytes(Path_, _Stream.str());
    }

    void WriteComponentJson(const std::filesystem::path& Path_, const ObjectMap& Values_)
    {
        WriteComponentBytes(Path_, CStandardJsonCodec::Serialize(Values_));
    }

    double ComponentVolume(const TopoDS_Shape& Shape_)
    {
        GProp_GProps _Properties;
        BRepGProp::VolumeProperties(Shape_, _Properties);
        return _Properties.Mass();
    }

    std::array<double, 6> ComponentBounds(const TopoDS_Shape& Shape_)
    {
        Bnd_Box _Bounds;
        BRepBndLib::Add(Shape_, _Bounds, false);
        _Bounds.SetGap(0.0);
        std::array<double, 6> _Result;
        _Bounds.Get(_Result[0], _Result[1], _Result[2], _Result[3], _Result[4], _Result[5]);
        return _Result;
    }

    std::size_t ComponentFaceCount(const TopoDS_Shape& Shape_)
    {
        std::size_t _Count = 0;
        for (TopExp_Explorer _Face(Shape_, TopAbs_FACE); _Face.More(); _Face.Next()) ++_Count;
        return _Count;
    }

    TopoDS_Shape OffsetDrilledComponent()
    {
        const auto _Box = BRepPrimAPI_MakeBox(gp_Pnt(100, -80, 50), 40, 30, 20).Shape();
        const auto _Hole = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(110, -65, 40), gp_Dir(0, 0, 1)), 3, 40).Shape();
        BRepAlgoAPI_Cut _Cut(_Box, _Hole);
        if (!_Cut.IsDone() || _Cut.Shape().IsNull()) throw std::runtime_error("component fixture cut failed");
        return _Cut.Shape();
    }

    ObjectMap ComponentMetadata(const std::string& Name_ = "定位柱帽")
    {
        return { { "name", Name_ }, { "category", std::string("柱帽") },
            { "sourcing", std::string("purchased") }, { "material", std::string("304") },
            { "description", std::string("真实钻孔配件") } };
    }

    ObjectMap ComponentDocument(const std::string& Reference_)
    {
        return {
            { "schema", std::string("icax.neutral-model") }, { "schemaVersion", 1 },
            { "template", ObjectMap{ { "id", std::string("test.component-library") }, { "version", std::string("1.0.0") } } },
            { "geometry", VariantArray{ ObjectMap{
                { "key", std::string("cap.geometry") }, { "operator", std::string("resource") },
                { "arguments", ObjectMap{ { "reference", Reference_ } } }
            } } },
            { "items", VariantArray{ ObjectMap{
                { "key", std::string("cap") }, { "displayName", std::string("柱帽") },
                { "representations", ObjectMap{ { "result", std::string("cap.geometry") } } },
                { "properties", ObjectMap{
                    { "manufacturing.modelReference", Reference_ },
                    { "manufacturing.partKind", std::string("tube") },
                    { "manufacturing.plate", ObjectMap{ { "width", 999.0 } } },
                    { "tubeDesigner.profile", ObjectMap{ { "id", std::string("rect") } } }, { "length", 999.0 }
                } }
            } } },
            { "outputs", VariantArray{ ObjectMap{
                { "key", std::string("manufacturing") }, { "purpose", std::string("result") },
                { "items", VariantArray{ std::string("cap") } }
            } } }
        };
    }

    ObjectMap ComponentResourceExtensions(const std::string& Relative_)
    {
        auto _Metadata = ComponentMetadata();
        _Metadata["path"] = Relative_;
        return { { "modelResources", ObjectMap{ { "cap", _Metadata } } } };
    }

    ObjectMap FrozenComponents(const ObjectMap& Document_)
    {
        return Document_.at("extensions").To<ObjectMap>().at(kResolvedComponentModels).To<ObjectMap>();
    }

    TopoDS_Shape ResolvedComponentShape(const ObjectMap& Document_)
    {
        const auto _Model = CTemplateCodec::ParseNeutralModel(Document_);
        return iCAX::OpenCascade::EvaluateNeutralModel(_Model, { "cap.geometry" }).At("cap.geometry");
    }

    ObjectMap ComponentItemProperties(const ObjectMap& Document_)
    {
        return Document_.at("items").To<VariantArray>().front().To<ObjectMap>().at("properties").To<ObjectMap>();
    }

    void WriteSystemComponent(const std::filesystem::path& Root_, const std::string& ID_,
        const TopoDS_Shape& Shape_)
    {
        auto _Metadata = ComponentMetadata();
        _Metadata["schema"] = std::string("icax.component-model");
        _Metadata["schemaVersion"] = 1;
        _Metadata["id"] = ID_;
        _Metadata["geometryFile"] = std::string("shape.brep");
        WriteComponentBRep(Root_ / ID_ / "shape.brep", Shape_);
        WriteComponentJson(Root_ / ID_ / "model.json", _Metadata);
    }

    class CComponentMemoryStore final : public IProductUserDataStore
    {
    public:
        const std::string& GetProductID() const noexcept override { return ProductID; }
        std::vector<CProductUserDataRecord> List(const CProductUserDataQuery& Query_) override
        {
            std::vector<CProductUserDataRecord> _Result;
            for (const auto& [_Key, _Record] : Records)
                if ((Query_.FeatureID.empty() || Query_.FeatureID == _Record.FeatureID)
                    && (Query_.RecordType.empty() || Query_.RecordType == _Record.RecordType))
                    _Result.push_back(_Record);
            return _Result;
        }
        std::optional<CProductUserDataRecord> Get(const std::string& Feature_, const std::string& Type_,
            const std::string& ID_, bool = false) override
        {
            ++GetCalls;
            const auto _It = Records.find({ Feature_, Type_, ID_ });
            return _It == Records.end() ? std::nullopt : std::optional(_It->second);
        }
        CProductUserDataRecord Put(const CProductUserDataRecord& Record_,
            std::optional<std::uint64_t> = std::nullopt) override
        {
            const auto _Key = std::make_tuple(Record_.FeatureID, Record_.RecordType, Record_.RecordID);
            auto _Record = Record_;
            const auto _It = Records.find(_Key);
            _Record.Revision = _It == Records.end() ? 1 : _It->second.Revision + 1;
            Records[_Key] = _Record;
            return _Record;
        }
        bool Delete(const std::string& Feature_, const std::string& Type_, const std::string& ID_,
            std::optional<std::uint64_t> = std::nullopt) override
        {
            return Records.erase({ Feature_, Type_, ID_ }) != 0;
        }
        void Save(const std::string& ID_, const ObjectMap& Snapshot_)
        {
            CProductUserDataRecord _Record;
            _Record.FeatureID = kComponentModelFeature;
            _Record.RecordType = kComponentModelRecordType;
            _Record.RecordID = ID_;
            _Record.Payload = Snapshot_;
            (void)Put(_Record);
        }
        std::size_t GetCalls = 0;
    private:
        const std::string ProductID = "tube-designer";
        std::map<std::tuple<std::string, std::string, std::string>, CProductUserDataRecord> Records;
    };
}

TEST(ComponentModelLibrary, ImportsStepAndBRepWithoutLosingHolesAndNormalizesTheAnchor)
{
    const auto _Root = ComponentTestRoot();
    const auto _Original = OffsetDrilledComponent();
    WriteComponentBRep(_Root / "drilled.brep", _Original);
    STEPControl_Writer _Writer;
    ASSERT_EQ(IFSelect_RetDone, _Writer.Transfer(_Original, STEPControl_AsIs));
    ASSERT_EQ(IFSelect_RetDone, _Writer.Write(ComponentPathText(_Root / "drilled.step").c_str()));
    for (const auto& _Name : { "drilled.brep", "drilled.step" })
    {
        SCOPED_TRACE(_Name);
        const auto _Snapshot = ImportComponentModelFile(_Root / _Name, ComponentMetadata());
        const auto _Shape = ComponentModelShape(_Snapshot);
        EXPECT_TRUE(BRepCheck_Analyzer(_Shape).IsValid());
        EXPECT_NEAR(ComponentVolume(_Original), ComponentVolume(_Shape), 1e-4);
        EXPECT_EQ(ComponentFaceCount(_Original), ComponentFaceCount(_Shape));
        const auto _Bounds = ComponentBounds(_Shape);
        const std::array<double, 6> _Expected{ -20, -15, 0, 20, 15, 20 };
        for (std::size_t _Index = 0; _Index < _Bounds.size(); ++_Index)
            EXPECT_NEAR(_Expected[_Index], _Bounds[_Index], 1e-6);
        const auto _Translation = _Snapshot.at("importTranslation").To<VariantArray>();
        ASSERT_EQ(3u, _Translation.size());
        EXPECT_NEAR(-120.0, _Translation[0].To<double>(), 1e-6);
        EXPECT_NEAR(65.0, _Translation[1].To<double>(), 1e-6);
        EXPECT_NEAR(-50.0, _Translation[2].To<double>(), 1e-6);
        EXPECT_EQ("mm", _Snapshot.at("unit").To<std::string>());
        EXPECT_FALSE(_Snapshot.at("geometryDigest").To<std::string>().empty());
        EXPECT_EQ("定位柱帽", _Snapshot.at("name").To<std::string>());
        EXPECT_TRUE(std::filesystem::remove(_Root / _Name));
        EXPECT_NEAR(ComponentVolume(_Original), ComponentVolume(ComponentModelShape(_Snapshot)), 1e-4);
    }
}

TEST(ComponentModelLibrary, LoadsSystemGeometryWithoutChangingItsDeclaredAnchor)
{
    const auto _Root = ComponentTestRoot();
    const auto _Shape = OffsetDrilledComponent();
    WriteSystemComponent(_Root, "test-cap", _Shape);
    const auto _Snapshot = LoadSystemComponentModel(_Root, "test-cap");
    EXPECT_EQ("system:test-cap", _Snapshot.at("sourceReference").To<std::string>());
    const auto _Actual = ComponentBounds(ComponentModelShape(_Snapshot));
    const auto _Expected = ComponentBounds(_Shape);
    for (std::size_t _Index = 0; _Index < _Actual.size(); ++_Index)
        EXPECT_NEAR(_Expected[_Index], _Actual[_Index], 1e-6);
    auto _Document = ComponentDocument("system:test-cap");
    ResolveTemplateComponentResources(_Document, _Root, {}, _Root, nullptr);
    EXPECT_NEAR(ComponentVolume(_Shape), ComponentVolume(ResolvedComponentShape(_Document)), 1e-6);
    const auto _Properties = ComponentItemProperties(_Document);
    EXPECT_EQ("accessory", _Properties.at("manufacturing.partKind").To<std::string>());
    EXPECT_EQ("accessory", _Properties.at("manufacturing.materialCategory").To<std::string>());
    EXPECT_EQ("purchased", _Properties.at("manufacturing.sourcing").To<std::string>());
    EXPECT_DOUBLE_EQ(0, _Properties.at("length").To<double>());
    EXPECT_FALSE(_Properties.contains("tubeDesigner.profile"));
    EXPECT_FALSE(_Properties.contains("manufacturing.plate"));
    const auto _Summary = ComponentModelSummary(_Snapshot, "system", "test-cap", 7);
    EXPECT_FALSE(_Summary.contains("brep"));
    EXPECT_EQ(7u, _Summary.at("revision").To<unsigned long long>());
    EXPECT_EQ(1u, ListComponentModelSummaries(_Root, nullptr).size());
}

TEST(ComponentModelLibrary, RejectsSystemAndTemplatePathEscapesAndMismatchedIdentity)
{
    const auto _Root = ComponentTestRoot();
    const auto _Package = _Root / "package";
    std::filesystem::create_directory(_Package);
    WriteComponentBRep(_Root / "outside.brep", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    for (const auto& _ID : { "", "..", "../outside", "a/b", "a\\b", "C:cap" })
        EXPECT_THROW(LoadSystemComponentModel(_Root, _ID), std::invalid_argument);
    for (const auto& _Path : { "../outside.brep", "models/../../outside.brep", "..\\outside.brep",
            "C:/outside.brep", "/outside.brep", "https://example.invalid/model.brep" })
    {
        SCOPED_TRACE(_Path);
        auto _Document = ComponentDocument("template:cap");
        EXPECT_THROW(ResolveTemplateComponentResources(_Document, _Package,
            ComponentResourceExtensions(_Path), _Root, nullptr), std::invalid_argument);
    }
    WriteSystemComponent(_Root, "test-cap", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    auto _Metadata = ComponentMetadata();
    _Metadata["schema"] = std::string("icax.component-model");
    _Metadata["schemaVersion"] = 1;
    _Metadata["id"] = std::string("test-cap");
    _Metadata["geometryFile"] = std::string("../outside.brep");
    WriteComponentJson(_Root / "test-cap/model.json", _Metadata);
    EXPECT_THROW(LoadSystemComponentModel(_Root, "test-cap"), std::invalid_argument);
    _Metadata["geometryFile"] = std::string("shape.brep");
    _Metadata["id"] = std::string("another-cap");
    WriteComponentJson(_Root / "test-cap/model.json", _Metadata);
    EXPECT_THROW(LoadSystemComponentModel(_Root, "test-cap"), std::invalid_argument);
    _Metadata["id"] = std::string("test-cap");
    _Metadata["schema"] = std::string("unexpected-schema");
    WriteComponentJson(_Root / "test-cap/model.json", _Metadata);
    EXPECT_THROW(LoadSystemComponentModel(_Root, "test-cap"), std::invalid_argument);
    _Metadata["schema"] = std::string("icax.component-model");
    _Metadata["schemaVersion"] = 2;
    WriteComponentJson(_Root / "test-cap/model.json", _Metadata);
    EXPECT_THROW(LoadSystemComponentModel(_Root, "test-cap"), std::invalid_argument);
}

TEST(ComponentModelLibrary, RejectsTemplateSymlinkEscapesWhenThePlatformAllowsSymlinks)
{
    const auto _Root = ComponentTestRoot();
    const auto _Package = _Root / "package";
    const auto _Outside = _Root / "outside";
    std::filesystem::create_directory(_Package);
    WriteComponentBRep(_Outside / "shape.brep", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    std::error_code _Error;
    std::filesystem::create_directory_symlink(_Outside, _Package / "linked", _Error);
    if (_Error)
    {
        SUCCEED() << "directory symlinks are unavailable on this host: " << _Error.message();
        return;
    }
    auto _Document = ComponentDocument("template:cap");
    EXPECT_THROW(ResolveTemplateComponentResources(_Document, _Package,
        ComponentResourceExtensions("linked/shape.brep"), _Root, nullptr), std::invalid_argument);
}

TEST(ComponentModelLibrary, SystemGeometryJsonRequiresMillimetresAndUsesItsDeclaredOutput)
{
    const auto _Root = ComponentTestRoot();
    WriteSystemComponent(_Root, "test-cap", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    auto _Metadata = ComponentMetadata();
    _Metadata["schema"] = std::string("icax.component-model");
    _Metadata["schemaVersion"] = 1;
    _Metadata["id"] = std::string("test-cap");
    _Metadata["geometryFile"] = std::string("shape.json");
    WriteComponentJson(_Root / "test-cap/model.json", _Metadata);
    auto _Geometry = CStandardJsonCodec::Parse(R"json({
        "schema":"icax.neutral-model", "schemaVersion":1,
        "template":{"id":"test.component-json","version":"1.0.0"}, "lengthUnit":"inch",
        "geometry":[
            {"key":"profile","operator":"profile2d","arguments":{
                "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},"contours":[
                {"kind":"roundedRectangle","width":10,"height":20,"radius":0}]}},
            {"key":"solid","operator":"extrude","inputs":["profile"],"arguments":{"vector":[0,0,30]}},
            {"key":"unused","operator":"resource","arguments":{"reference":"system:not-loaded"}}
        ],
        "items":[{"key":"cap","displayName":"cap","representations":{"result":"solid"}}],
        "outputs":[{"key":"result","purpose":"result","items":["cap"]}]
    })json").To<ObjectMap>();
    WriteComponentJson(_Root / "test-cap/shape.json", _Geometry);
    EXPECT_THROW(LoadSystemComponentModel(_Root, "test-cap"), std::invalid_argument);
    _Geometry["lengthUnit"] = std::string("mm");
    WriteComponentJson(_Root / "test-cap/shape.json", _Geometry);
    const auto _Snapshot = LoadSystemComponentModel(_Root, "test-cap");
    EXPECT_NEAR(6000.0, ComponentVolume(ComponentModelShape(_Snapshot)), 1e-6);
}

TEST(ComponentModelLibrary, InfersAccessoryProvenanceThroughTransformsAndExplicitBooleanArguments)
{
    const auto _Root = ComponentTestRoot();
    WriteComponentBRep(_Root / "cap.brep", BRepPrimAPI_MakeBox(gp_Pnt(-20, -15, 0), 40, 30, 20).Shape());
    const auto _Extensions = ComponentResourceExtensions("cap.brep");
    auto _Base = ComponentDocument("template:cap");
    auto _Items = _Base.at("items").To<VariantArray>();
    auto _Item = _Items.front().To<ObjectMap>();
    auto _Properties = _Item.at("properties").To<ObjectMap>();
    _Properties.erase("manufacturing.modelReference");
    _Item["properties"] = _Properties;
    _Items.front() = _Item;
    _Base["items"] = _Items;
    auto _Nodes = _Base.at("geometry").To<VariantArray>();
    auto _Prototype = _Nodes.front().To<ObjectMap>();
    _Prototype["key"] = std::string("cap.prototype");
    _Nodes.front() = _Prototype;

    auto _TransformNodes = _Nodes;
    _TransformNodes.emplace_back(ObjectMap{
        { "key", std::string("cap.geometry") }, { "operator", std::string("transform") },
        { "inputs", VariantArray{ std::string("cap.prototype") } },
        { "arguments", ObjectMap{ { "placement", ObjectMap{
            { "origin", VariantArray{ 100.0, 0.0, 0.0 } },
            { "xAxis", VariantArray{ 1.0, 0.0, 0.0 } }, { "yAxis", VariantArray{ 0.0, 1.0, 0.0 } },
            { "zAxis", VariantArray{ 0.0, 0.0, 1.0 } }
        } } } }
    });
    auto _Transformed = _Base;
    _Transformed["geometry"] = _TransformNodes;
    ResolveTemplateComponentResources(_Transformed, _Root, _Extensions, _Root, nullptr);
    const auto _TransformedProperties = ComponentItemProperties(_Transformed);
    EXPECT_EQ("accessory", _TransformedProperties.at("manufacturing.partKind").To<std::string>());
    EXPECT_EQ("template:cap", _TransformedProperties.at("manufacturing.modelReference").To<std::string>());
    EXPECT_FALSE(_TransformedProperties.contains("tubeDesigner.profile"));
    EXPECT_FALSE(_TransformedProperties.contains("manufacturing.plate"));
    EXPECT_DOUBLE_EQ(0.0, _TransformedProperties.at("length").To<double>());
    EXPECT_NEAR(24000.0, ComponentVolume(ResolvedComponentShape(_Transformed)), 1e-6);

    const auto _CutterNodes = CStandardJsonCodec::Parse(R"json([
        {"key":"cut.profile","operator":"profile2d","arguments":{
            "placement":{"origin":[0,0,-5],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[{"kind":"roundedRectangle","width":5,"height":5,"radius":0}]}},
        {"key":"cut.solid","operator":"extrude","inputs":["cut.profile"],"arguments":{"vector":[0,0,30]}}
    ])json").To<VariantArray>();
    _Nodes.insert(_Nodes.end(), _CutterNodes.begin(), _CutterNodes.end());
    for (bool _ResourceIsTarget : { true, false })
    {
        SCOPED_TRACE(_ResourceIsTarget);
        auto _BooleanNodes = _Nodes;
        _BooleanNodes.emplace_back(ObjectMap{
            { "key", std::string("cap.geometry") }, { "operator", std::string("boolean") },
            // Deliberately no inputs: these are valid effective dependencies.
            { "arguments", ObjectMap{
                { "operation", std::string("subtract") },
                { "target", std::string(_ResourceIsTarget ? "cap.prototype" : "cut.solid") },
                { "tools", VariantArray{ std::string(_ResourceIsTarget ? "cut.solid" : "cap.prototype") } }
            } }
        });
        auto _Boolean = _Base;
        _Boolean["geometry"] = _BooleanNodes;
        ResolveTemplateComponentResources(_Boolean, _Root, _Extensions, _Root, nullptr);
        const auto _Actual = ComponentItemProperties(_Boolean);
        EXPECT_EQ("accessory", _Actual.at("manufacturing.partKind").To<std::string>());
        EXPECT_EQ("template:cap", _Actual.at("manufacturing.modelReference").To<std::string>());
        EXPECT_FALSE(_Actual.contains("tubeDesigner.profile"));
        EXPECT_FALSE(_Actual.contains("manufacturing.plate"));
        EXPECT_DOUBLE_EQ(0.0, _Actual.at("length").To<double>());
        EXPECT_NEAR(_ResourceIsTarget ? 23500.0 : 250.0, ComponentVolume(ResolvedComponentShape(_Boolean)), 1e-6);
    }
    auto _Mismatched = _Base;
    _Mismatched["geometry"] = _TransformNodes;
    auto _MismatchedItems = _Mismatched.at("items").To<VariantArray>();
    auto _MismatchedItem = _MismatchedItems.front().To<ObjectMap>();
    _Properties["manufacturing.modelReference"] = std::string("template:some-other-model");
    _MismatchedItem["properties"] = _Properties;
    _MismatchedItems.front() = _MismatchedItem;
    _Mismatched["items"] = _MismatchedItems;
    EXPECT_THROW(ResolveTemplateComponentResources(_Mismatched, _Root, _Extensions, _Root, nullptr), std::invalid_argument);
}

TEST(ComponentModelLibrary, FrozenTemplateResourcesSurviveEditsDeletionAndManufacturingOnlyReferences)
{
    const auto _Root = ComponentTestRoot();
    const auto _Package = _Root / "package";
    const auto _Original = BRepPrimAPI_MakeBox(gp_Pnt(-20, -15, 0), 40, 30, 20).Shape();
    const auto _Spare = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    WriteComponentBRep(_Package / "models/cap.brep", _Original);
    WriteComponentBRep(_Package / "models/spare.brep", _Spare);
    auto _Extensions = ComponentResourceExtensions("models/cap.brep");
    auto _Resources = _Extensions.at("modelResources").To<ObjectMap>();
    auto _SpareMetadata = ComponentMetadata("制造专用配件");
    _SpareMetadata["path"] = std::string("models/spare.brep");
    _Resources["spare"] = _SpareMetadata;
    _Extensions["modelResources"] = _Resources;
    auto _Preview = ComponentDocument("template:cap");
    ResolveTemplateComponentResources(_Preview, _Package, _Extensions, _Root, nullptr);
    const auto _Frozen = FrozenComponents(_Preview);
    ASSERT_EQ(2u, _Frozen.size());
    const auto _OriginalBRep = _Frozen.at("template:cap").To<ObjectMap>().at("brep").To<std::string>();

    WriteComponentBRep(_Package / "models/cap.brep", BRepPrimAPI_MakeBox(100, 100, 100).Shape());
    auto _Manufacturing = ComponentDocument("template:cap");
    ResolveTemplateComponentResources(_Manufacturing, _Package, _Extensions, _Root, nullptr, &_Frozen);
    EXPECT_NEAR(ComponentVolume(_Original), ComponentVolume(ResolvedComponentShape(_Manufacturing)), 1e-6);
    EXPECT_EQ(_OriginalBRep, FrozenComponents(_Manufacturing).at("template:cap").To<ObjectMap>().at("brep").To<std::string>());
    EXPECT_TRUE(std::filesystem::remove(_Package / "models/cap.brep"));
    EXPECT_TRUE(std::filesystem::remove(_Package / "models/spare.brep"));
    auto _ManufacturingOnly = ComponentDocument("template:spare");
    ResolveTemplateComponentResources(_ManufacturingOnly, _Package / "missing", {}, _Root / "missing", nullptr, &_Frozen);
    EXPECT_NEAR(ComponentVolume(_Spare), ComponentVolume(ResolvedComponentShape(_ManufacturingOnly)), 1e-6);
    auto _MissingSnapshot = ComponentDocument("template:unknown");
    EXPECT_THROW(ResolveTemplateComponentResources(_MissingSnapshot, _Package, _Extensions,
        _Root, nullptr, &_Frozen), std::invalid_argument);
    auto _Tampered = _Frozen;
    auto _Snapshot = _Tampered.at("template:cap").To<ObjectMap>();
    _Snapshot["brep"] = _OriginalBRep + " ";
    _Tampered["template:cap"] = _Snapshot;
    auto _TamperedDocument = ComponentDocument("template:cap");
    EXPECT_THROW(ResolveTemplateComponentResources(_TamperedDocument, _Package, _Extensions,
        _Root, nullptr, &_Tampered), std::invalid_argument);
}

TEST(ComponentModelLibrary, FrozenLibraryResourcesDoNotReadChangedOrDeletedRecords)
{
    const auto _Root = ComponentTestRoot();
    WriteComponentBRep(_Root / "first.brep", BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    WriteComponentBRep(_Root / "second.brep", BRepPrimAPI_MakeBox(40, 50, 60).Shape());
    CComponentMemoryStore _Store;
    _Store.Save("my-cap", ImportComponentModelFile(_Root / "first.brep", ComponentMetadata()));
    auto _Preview = ComponentDocument("library:my-cap");
    ResolveTemplateComponentResources(_Preview, _Root, {}, _Root, &_Store);
    const auto _Frozen = FrozenComponents(_Preview);
    const auto _ReadCount = _Store.GetCalls;
    _Store.Save("my-cap", ImportComponentModelFile(_Root / "second.brep", ComponentMetadata("新配件")));
    auto _Manufacturing = ComponentDocument("library:my-cap");
    ResolveTemplateComponentResources(_Manufacturing, _Root, {}, _Root, &_Store, &_Frozen);
    EXPECT_EQ(_ReadCount, _Store.GetCalls);
    EXPECT_NEAR(6000.0, ComponentVolume(ResolvedComponentShape(_Manufacturing)), 1e-6);
    EXPECT_EQ("定位柱帽", ComponentItemProperties(_Manufacturing).at("manufacturing.modelName").To<std::string>());
    EXPECT_TRUE(_Store.Delete(kComponentModelFeature, kComponentModelRecordType, "my-cap"));
    _Manufacturing = ComponentDocument("library:my-cap");
    ResolveTemplateComponentResources(_Manufacturing, _Root, {}, _Root, &_Store, &_Frozen);
    EXPECT_EQ(_ReadCount, _Store.GetCalls);
    EXPECT_NEAR(6000.0, ComponentVolume(ResolvedComponentShape(_Manufacturing)), 1e-6);
    auto _Live = ComponentDocument("library:my-cap");
    EXPECT_THROW(ResolveTemplateComponentResources(_Live, _Root, {}, _Root, &_Store), std::invalid_argument);
    EXPECT_THROW(ResolveComponentModelSnapshot(_Root, nullptr, "user", "my-cap"), std::invalid_argument);
    EXPECT_THROW(ResolveComponentModelSnapshot(_Root, &_Store, "unknown", "my-cap"), std::invalid_argument);
}

TEST(ComponentModelLibrary, MetadataUpdatesCannotOverwriteGeometryAndInvalidValuesAreRejected)
{
    const auto _Root = ComponentTestRoot();
    WriteComponentBRep(_Root / "cap.brep", OffsetDrilledComponent());
    auto _Snapshot = ImportComponentModelFile(_Root / "cap.brep", ComponentMetadata());
    const auto _BRep = _Snapshot.at("brep").To<std::string>();
    const auto _Digest = _Snapshot.at("geometryDigest").To<std::string>();
    const auto _Bounds = CStandardJsonCodec::Serialize(_Snapshot.at("bounds"));
    UpdateComponentModelMetadata(_Snapshot, {
        { "name", std::string("自制柱帽") }, { "sourcing", std::string("made") },
        { "material", std::string("Q235B") }, { "brep", std::string("forged geometry") },
        { "geometryDigest", std::string("forged digest") }, { "bounds", ObjectMap{} }
    });
    EXPECT_EQ("自制柱帽", _Snapshot.at("name").To<std::string>());
    EXPECT_EQ("made", _Snapshot.at("sourcing").To<std::string>());
    EXPECT_EQ(_BRep, _Snapshot.at("brep").To<std::string>());
    EXPECT_EQ(_Digest, _Snapshot.at("geometryDigest").To<std::string>());
    EXPECT_EQ(_Bounds, CStandardJsonCodec::Serialize(_Snapshot.at("bounds")));
    for (const auto& _Bad : std::vector<ObjectMap>{
        { { "name", std::string(" \t") } }, { { "category", std::string() } },
        { { "sourcing", std::string("unknown") } }, { { "material", 42 } },
        { { "description", std::string(4001, 'x') } }, { { "name", std::string(241, 'x') } },
        { { "name", std::string("a\0b", 3) } }
    })
    {
        auto _Copy = _Snapshot;
        EXPECT_THROW(UpdateComponentModelMetadata(_Copy, _Bad), std::invalid_argument);
        EXPECT_EQ(_BRep, _Copy.at("brep").To<std::string>());
    }
    const auto _Before = CStandardJsonCodec::Serialize(_Snapshot);
    EXPECT_THROW(UpdateComponentModelMetadata(_Snapshot, {
        { "name", std::string("must not partially apply") }, { "sourcing", std::string("invalid") }
    }), std::invalid_argument);
    EXPECT_EQ(_Before, CStandardJsonCodec::Serialize(_Snapshot));
    EXPECT_THROW(ImportComponentModelFile(_Root / "cap.brep", { { "sourcing", std::string("invalid") } }), std::invalid_argument);
    WriteComponentBytes(_Root / "broken.brep", "not a solid BRep");
    EXPECT_THROW(ImportComponentModelFile(_Root / "broken.brep", {}), std::invalid_argument);
    WriteComponentBytes(_Root / "unaccepted.json", "{}");
    EXPECT_THROW(ImportComponentModelFile(_Root / "unaccepted.json", {}), std::invalid_argument);
}
