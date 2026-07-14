#include <gtest/gtest.h>

#include <Laser3DCAM/FeatureRecognitionService.h>
#include <Laser3DCAM/JobComponents.h>
#include <Laser3DCAM/Laser3DCAM.h>
#include <Laser3DCAM/MachineDefinitionComponents.h>
#include <Laser3DCAM/MachineInstanceComponents.h>
#include <Laser3DCAM/MachineResources.h>
#include <Laser3DCAM/SceneComponents.h>
#include <Laser3DCAM/Targets.h>
#include <Laser3DCAM/ToolpathComponents.h>
#include <Laser3DCAM/ToolpathOrderService.h>
#include <Laser3DCAM/ToolpathResources.h>

#include <ApplicationContext/IApplicationContext.h>
#include <CommandTargets/CommandRoute.h>
#include <CommandTargets/CommandMessage.h>
#include <Data/Variant.h>
#include <Data/VariantSerializer.h>
#include <Database/IEntity.h>
#include <Database/IMetaRegistry.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <GeometryData/GeometryData.h>
#include <Mailbox/MailChannel.h>
#include <PDO/IPDOHub.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <ProjectContext/ISceneContext.h>
#include <RenderData/RenderData.h>
#include <RenderInteraction/RenderInteraction.h>
#include <Resources/ResourceInfo.h>
#include <Resources/ResourceLoaderRegistry.h>
#include <Resources/ResourceLibrary.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Transform/Transform.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

    class CTestApplicationContext final : public iCAX::Application::IApplicationContext
    {
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override
        {
            throw std::logic_error("Application descriptor is not used by Laser3DCAM tests");
        }

        const iCAX::Application::CApplicationPaths& GetPaths() const override
        {
            throw std::logic_error("Application paths are not used by Laser3DCAM tests");
        }

        iCAX::Data::PropertyBag GetSettings() const override
        {
            return {};
        }
    };

    class CTestProductContext final : public iCAX::Product::IProductContext
    {
    public:
        const iCAX::Product::CProductDefinition& GetDefinition() const override
        {
            throw std::logic_error("Product definition is not used by Laser3DCAM tests");
        }

        const std::string& GetProductID() const override
        {
            return m_ProductID;
        }

        iCAX::Product::CProductData GetProductData() const override
        {
            return {};
        }

        iCAX::Services::CServiceProvider& GetServiceProvider() const override
        {
            return m_ServiceProvider;
        }

        iCAX::Database::IMetaRegistry& GetMetaRegistry() const override
        {
            throw std::logic_error("Product meta registry is not used by Laser3DCAM command tests");
        }

        iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override
        {
            throw std::logic_error("Product behaviour registry is not used by Laser3DCAM command tests");
        }

        iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override
        {
            return m_ResourceLoaderRegistry;
        }

        iCAX::Command::CCommandRegistry& GetCommandRegistry() const override
        {
            throw std::logic_error("Product command registry is not used by Laser3DCAM command tests");
        }

    private:
        std::string m_ProductID = "laser-3d-cam-test";
        mutable iCAX::Services::CServiceProvider m_ServiceProvider;
        mutable iCAX::Resource::CResourceLoaderRegistry m_ResourceLoaderRegistry;
    };

    class CTestProjectContext final : public iCAX::Project::IProjectContext
    {
    public:
        CTestProjectContext()
            : m_ProjectID(iCAX::Data::GenerateNewUUID())
        {
        }

        const iCAX::Data::uuid& GetProjectID() const override
        {
            return m_ProjectID;
        }

        const std::string& GetProjectName() const override
        {
            return m_ProjectName;
        }

        const std::string& GetProjectPath() const override
        {
            return m_ProjectPath;
        }

        iCAX::Data::PropertyBag& Settings() override
        {
            return m_Settings;
        }

        const iCAX::Data::PropertyBag& Settings() const override
        {
            return m_Settings;
        }

    private:
        iCAX::Data::uuid m_ProjectID;
        std::string m_ProjectName = "Laser3DCAM Test Project";
        std::string m_ProjectPath;
        iCAX::Data::PropertyBag m_Settings;
    };

    class CTestSceneContext final : public iCAX::Project::ISceneContext
    {
    public:
        CTestSceneContext()
            : m_SceneID(iCAX::Data::GenerateNewUUID())
            , m_SceneChannelID(iCAX::Data::GenerateNewUUID())
            , m_pMetaRegistry(iCAX::Database::CreateMetaRegistry())
            , m_pRepository(iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(), m_pMetaRegistry))
            , m_pChannel(std::make_shared<iCAX::Mail::CMailChannel>())
        {
            iCAX::Database::CMetaRegistrationCatalog::ReplayAll(*m_pMetaRegistry);
        }

        const iCAX::Data::uuid& GetSceneID() const override
        {
            return m_SceneID;
        }

        const iCAX::Data::uuid& GetSceneChannelID() const override
        {
            return m_SceneChannelID;
        }

        const std::string& GetSceneName() const override
        {
            return m_SceneName;
        }

        iCAX::Mail::CMailPostOffice GetBackendPostOffice() const override
        {
            return m_pChannel->GetEndAPostOffice();
        }

        iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const override
        {
            return m_pChannel->GetEndBPostOffice();
        }

        bool IsMainScene() const override
        {
            return true;
        }

        bool IsTransientScene() const override
        {
            return false;
        }

        iCAX::Database::IRepository& Database() override
        {
            return *m_pRepository;
        }

        const iCAX::Database::IRepository& Database() const override
        {
            return *m_pRepository;
        }

        iCAX::Resource::CResourceLibrary& Resources() override
        {
            return m_Resources;
        }

        const iCAX::Resource::CResourceLibrary& Resources() const override
        {
            return m_Resources;
        }

        bool HasPDOHub() const override
        {
            return false;
        }

        iCAX::PDO::IPDOHub& PDOHub() override
        {
            throw std::logic_error("PDO hub is not used by Laser3DCAM tests");
        }

        const iCAX::PDO::IPDOHub& PDOHub() const override
        {
            throw std::logic_error("PDO hub is not used by Laser3DCAM tests");
        }

        iCAX::Services::CServiceProvider& Services() const override
        {
            return m_ServiceProvider;
        }

    private:
        iCAX::Data::uuid m_SceneID;
        iCAX::Data::uuid m_SceneChannelID;
        std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
        std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
        iCAX::Resource::CResourceLibrary m_Resources;
        mutable iCAX::Services::CServiceProvider m_ServiceProvider;
        std::shared_ptr<iCAX::Mail::CMailChannel> m_pChannel;
        std::string m_SceneName = "Laser3DCAM Test Scene";
    };

    std::vector<uint8_t> EncodeVariant(IN const iCAX::Data::Variant& Payload_)
    {
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Payload_);
        return std::vector<uint8_t>(_Text.begin(), _Text.end());
    }

    ObjectMap DecodeObjectPayload(IN const std::vector<uint8_t>& Payload_)
    {
        if (Payload_.empty())
        {
            return {};
        }

        const std::string _Text(Payload_.begin(), Payload_.end());
        auto _Variant = iCAX::Data::VariantSerializer::Deserialize(_Text);
        if (!_Variant.Is<ObjectMap>())
        {
            throw std::runtime_error("Expected object command payload");
        }
        return _Variant.To<ObjectMap>();
    }

    iCAX::Command::CCommandRequest MakeCommandRequest(
        IN const std::string& strMainName_,
        IN const std::string& strSubName_,
        IN const ObjectMap& Payload_)
    {
        iCAX::Command::CCommandRequest _Request;
        _Request.nCommandID = 1;
        _Request.Route = iCAX::Command::MakeCommandRoute(strMainName_, strSubName_);
        _Request.Payload = EncodeVariant(iCAX::Data::Variant(Payload_));
        return _Request;
    }

    ObjectMap DecodeResponseObject(IN const iCAX::Command::CCommandResponse& Response_)
    {
        if (!Response_.IsOK())
        {
            throw std::runtime_error(Response_.strError.empty() ? "Command response failed" : Response_.strError);
        }
        return DecodeObjectPayload(Response_.Payload);
    }

    void WriteTextFile(IN const std::filesystem::path& Path_, IN const std::string& Content_)
    {
        std::ofstream _Stream(Path_, std::ios::binary);
        if (!_Stream)
        {
            throw std::runtime_error("Cannot write file: " + Path_.string());
        }
        _Stream << Content_;
    }

    template <typename TComponent>
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> CollectEntitiesWithComponent(
        IN iCAX::Database::IRepository& Repository_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
        for (auto& _pEntity : Repository_.FilterEntities([](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
            return pEntity_ && pEntity_->HasComponent<TComponent>();
        }))
        {
            _Result.emplace_back(_pEntity, _pEntity->GetComponent<TComponent>());
        }
        return _Result;
    }

    bool HasChild(
        IN const std::vector<iCAX::Data::uuid>& Children_,
        IN const iCAX::Data::uuid& ChildID_)
    {
        return std::find(Children_.begin(), Children_.end(), ChildID_) != Children_.end();
    }

    ObjectMap MakePoint(IN double dX_, IN double dY_, IN double dZ_ = 0.0)
    {
        ObjectMap _Point;
        _Point["x"] = dX_;
        _Point["y"] = dY_;
        _Point["z"] = dZ_;
        return _Point;
    }

    ObjectMap MakeChild(IN const std::string& strKind_, IN const iCAX::Data::uuid& EntityID_)
    {
        ObjectMap _Child;
        _Child["kind"] = strKind_;
        _Child["entityId"] = iCAX::Data::to_string(EntityID_);
        return _Child;
    }

    iCAX::Resource::CResourceInfo MakeResourceInfo(IN const std::string& strID_, IN uint64_t nVersion_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = strID_;
        _Info.Name = strID_;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        _Info.nVersion = nVersion_;
        return _Info;
    }

    std::string ReadTextFile(IN const std::filesystem::path& Path_)
    {
        std::ifstream _Stream(Path_, std::ios::binary);
        if (!_Stream)
        {
            throw std::runtime_error("Cannot open file: " + Path_.string());
        }

        std::ostringstream _Content;
        _Content << _Stream.rdbuf();
        return _Content.str();
    }

    std::filesystem::path GetSourceRoot()
    {
        auto _Path = std::filesystem::path(__FILE__).parent_path();
        for (int _Index = 0; _Index < 5; ++_Index)
        {
            _Path = _Path.parent_path();
        }
        return _Path;
    }

    iCAX::GeometryData::BRepVertex MakeVertex(IN uint64_t nID_, IN double dX_, IN double dY_)
    {
        iCAX::GeometryData::BRepVertex _Vertex;
        _Vertex.Id = nID_;
        _Vertex.Position = { dX_, dY_, 0.0 };
        return _Vertex;
    }

    iCAX::GeometryData::BRepEdge MakeEdge(IN uint64_t nID_, IN uint64_t nStartVertexID_, IN uint64_t nEndVertexID_)
    {
        iCAX::GeometryData::BRepEdge _Edge;
        _Edge.Id = nID_;
        _Edge.StartVertexId = nStartVertexID_;
        _Edge.EndVertexId = nEndVertexID_;
        return _Edge;
    }

    iCAX::GeometryData::BRepCoedge MakeCoedge(IN uint64_t nEdgeID_, IN uint64_t nStartVertexID_, IN uint64_t nEndVertexID_)
    {
        iCAX::GeometryData::BRepCoedge _Coedge;
        _Coedge.EdgeId = nEdgeID_;
        _Coedge.StartVertexId = nStartVertexID_;
        _Coedge.EndVertexId = nEndVertexID_;
        return _Coedge;
    }

    iCAX::GeometryData::BRepModel MakeFaceWithInnerHoleBRep()
    {
        iCAX::GeometryData::BRepModel _Model;
        _Model.Vertices = {
            MakeVertex(1, 0.0, 0.0),
            MakeVertex(2, 10.0, 0.0),
            MakeVertex(3, 10.0, 10.0),
            MakeVertex(4, 0.0, 10.0),
            MakeVertex(5, 3.0, 3.0),
            MakeVertex(6, 7.0, 3.0),
            MakeVertex(7, 7.0, 7.0),
            MakeVertex(8, 3.0, 7.0),
        };
        _Model.Edges = {
            MakeEdge(1, 1, 2),
            MakeEdge(2, 2, 3),
            MakeEdge(3, 3, 4),
            MakeEdge(4, 4, 1),
            MakeEdge(5, 5, 6),
            MakeEdge(6, 6, 7),
            MakeEdge(7, 7, 8),
            MakeEdge(8, 8, 5),
        };

        iCAX::GeometryData::BRepWire _OuterWire;
        _OuterWire.Id = 10;
        _OuterWire.Closed = true;
        _OuterWire.Coedges = {
            MakeCoedge(1, 1, 2),
            MakeCoedge(2, 2, 3),
            MakeCoedge(3, 3, 4),
            MakeCoedge(4, 4, 1),
        };

        iCAX::GeometryData::BRepWire _InnerWire;
        _InnerWire.Id = 20;
        _InnerWire.Closed = true;
        _InnerWire.Coedges = {
            MakeCoedge(5, 5, 6),
            MakeCoedge(6, 6, 7),
            MakeCoedge(7, 7, 8),
            MakeCoedge(8, 8, 5),
        };

        iCAX::GeometryData::BRepFace _Face;
        _Face.Id = 30;
        _Face.WireIds = { _OuterWire.Id, _InnerWire.Id };

        _Model.Wires = { _OuterWire, _InnerWire };
        _Model.Faces = { _Face };
        return _Model;
    }

    std::shared_ptr<iCAX::CAM::CPathComponent> AddPath(
        IN CTestSceneContext& Scene_,
        IN const iCAX::Data::uuid& PathID_,
        IN const std::string& strCurveResourceID_,
        IN double dStartX_,
        IN double dEndX_)
    {
        auto _pEntity = Scene_.Database().CreateEntity(PathID_);
        auto _pPath = _pEntity->AddComponent<iCAX::CAM::CPathComponent>();
        std::string _strError;
        EXPECT_TRUE(_pPath->SetCurveResourceID(strCurveResourceID_, _strError)) << _strError;

        auto _pCurve = std::make_shared<iCAX::CAM::CPathCurveResource>();
        _pCurve->nVersion = 1;
        _pCurve->SourceTopology["points"] = VariantArray{
            MakePoint(dStartX_, 0.0),
            MakePoint(dEndX_, 0.0)
        };
        Scene_.Resources().Set<iCAX::CAM::CPathCurveResource>(
            strCurveResourceID_,
            _pCurve,
            MakeResourceInfo(strCurveResourceID_, _pCurve->nVersion));
        return _pPath;
    }

    void RegisterLaserServices(IN iCAX::Services::CServiceProvider& Provider_)
    {
        EXPECT_EQ(1u, iCAX::CAM::GetLaser3DCAMContractVersion());
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(Provider_);
    }
}

TEST(Laser3DCAMFeatureRecognitionTest, RecognizesInnerWireAsHoleFeature)
{
    CTestSceneContext _Scene;
    iCAX::Services::CServiceProvider _Provider;
    RegisterLaserServices(_Provider);
    auto _pService = _Provider.Resolve<iCAX::CAM::IFeatureRecognitionService>();
    ASSERT_NE(nullptr, _pService);

    constexpr const char* _BRepID = "test.brep";
    auto _pBRep = std::make_shared<iCAX::GeometryData::BRepModel>(MakeFaceWithInnerHoleBRep());
    _Scene.Resources().Set<iCAX::GeometryData::BRepModel>(_BRepID, _pBRep, MakeResourceInfo(_BRepID, 7));

    iCAX::CAM::SFeatureRecognitionRequest _Request;
    _Request.BRep.BRepResourceID = _BRepID;
    _Request.BRep.nExpectedDataVersion = 7;
    iCAX::CAM::SFeatureDefinition _Definition;
    _Definition.Kind = "Hole";
    _Request.Definitions.push_back(_Definition);

    const auto _Result = _pService->Recognize(_Scene, _Request);
    ASSERT_EQ(1u, _Result.Features.size());
    EXPECT_EQ(7u, _Result.nDataVersion);
    EXPECT_EQ("hole", _Result.Features.front().Role);
    EXPECT_EQ(20ull, _Result.Features.front().Parameters.at("wireId").To<unsigned long long>());
    ASSERT_EQ(1u, _Result.Features.front().PreviewCurves.size());
}

TEST(Laser3DCAMManifestTest, StartupComponentMatchesRegisteredSceneBootstrapComponent)
{
    const auto _ManifestPath = GetSourceRoot() / "apps" / "laser-3d-cam" / "product.manifest.json";
    const auto _Manifest = ReadTextFile(_ManifestPath);
    const std::string _Expected = std::string("\"startupComponent\": \"") + iCAX::CAM::CSceneBootstrapComponent::S_ClassName + "\"";

    EXPECT_NE(std::string::npos, _Manifest.find(_Expected));
    EXPECT_EQ(std::string::npos, _Manifest.find("CLaserCamSceneBootstrapComponent"));
}

TEST(Laser3DCAMMachineDefinitionTest, MachineImportCommandBuildsRenderableTransformTree)
{
    CTestApplicationContext _Application;
    CTestProductContext _Product;
    CTestProjectContext _Project;
    CTestSceneContext _Scene;

    const auto _TempDir = std::filesystem::temp_directory_path()
        / ("icax_laser_cam_machine_" + iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    std::filesystem::create_directories(_TempDir);
    std::filesystem::create_directories(_TempDir / "textures");
    const auto _TexturePath = _TempDir / "textures" / "base.png";
    WriteTextFile(_TexturePath, "fake png bytes are sufficient for resource URI wiring test");
    const auto _SDFPath = _TempDir / "machine_with_visuals.sdf";
    WriteTextFile(
        _SDFPath,
        R"(<?xml version="1.0"?>
<sdf version="1.7">
  <model name="unit_machine">
    <plugin name="icax_machine_control" filename="icax.machine.control">
      <linear_jog_step_mm>12.5</linear_jog_step_mm>
      <angular_jog_step_deg>3.0</angular_jog_step_deg>
    </plugin>
    <link name="base">
      <pose>0 0 0 0 0 0</pose>
      <visual name="base_box">
        <pose>0.1 0.2 0.3 0 0 0</pose>
        <geometry>
          <box>
            <size>0.4 0.2 0.1</size>
          </box>
        </geometry>
        <material>
          <diffuse>0.7 0.8 0.9 1.0</diffuse>
          <pbr>
            <metal>
              <albedo_map>textures/base.png</albedo_map>
            </metal>
          </pbr>
        </material>
      </visual>
      <collision name="base_collision">
        <pose>0.1 0.2 0.3 0 0 0</pose>
        <geometry>
          <box>
            <size>0.5 0.25 0.12</size>
          </box>
        </geometry>
      </collision>
    </link>
    <link name="head">
      <pose>0 0 0 0 0 0</pose>
      <plugin name="icax_tool_tcp" filename="icax.machine.tool">
        <tool_name>Laser Head TCP</tool_name>
        <tool_type>laser_cutting_head</tool_type>
        <tcp_position_mm>0 0 -30</tcp_position_mm>
        <beam_direction>0 0 -1</beam_direction>
      </plugin>
      <visual name="head_sphere">
        <pose>0 0 0.05 0 0 0</pose>
        <geometry>
          <sphere>
            <radius>0.05</radius>
          </sphere>
        </geometry>
      </visual>
    </link>
    <joint name="lift" type="fixed">
      <pose>0 0 0.5 0 0 0</pose>
      <parent>base</parent>
      <child>head</child>
    </joint>
  </model>
</sdf>)");

    ObjectMap _ImportPayload;
    _ImportPayload["sourcePath"] = _SDFPath.string();
    _ImportPayload["name"] = std::string("Unit Machine");
    const auto _ImportRequest = MakeCommandRequest("MachineDefinition", "Import", _ImportPayload);
    ObjectMap _ImportResult;
    {
        SCOPED_TRACE("import machine definition");
        _ImportResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleImportMachineDefinition(_ImportRequest, _Application, &_Product, &_Project, &_Scene));
    }

    ASSERT_TRUE(_ImportResult.contains("machineDefinitionId"));
    const auto _DefinitionIDText = _ImportResult.at("machineDefinitionId").To<std::string>();
    ASSERT_FALSE(_DefinitionIDText.empty());
    EXPECT_EQ(2ull, _ImportResult.at("linkCount").To<unsigned long long>());
    EXPECT_EQ(1ull, _ImportResult.at("jointCount").To<unsigned long long>());
    EXPECT_EQ(2ull, _ImportResult.at("visualCount").To<unsigned long long>());
    EXPECT_EQ(1ull, _ImportResult.at("collisionCount").To<unsigned long long>());

    {
        SCOPED_TRACE("verify neutral machine description tree");
        const auto _ResourceID = _ImportResult.at("machineResourceId").To<std::string>();
        auto _pDescription = _Scene.Resources().Get<iCAX::CAM::CMachineDescriptionResource>(_ResourceID);
        ASSERT_NE(nullptr, _pDescription);
        EXPECT_EQ("SDF", _pDescription->SourceFormat);
        EXPECT_EQ(3u, _pDescription->Elements.size());
        EXPECT_EQ(2ull, iCAX::CAM::CountMachinePartElements(*_pDescription));
        EXPECT_EQ(1ull, iCAX::CAM::CountMachineJoints(*_pDescription));
        EXPECT_EQ(2ull, iCAX::CAM::CountMachineVisuals(*_pDescription));
        EXPECT_EQ(1ull, iCAX::CAM::CountMachineCollisions(*_pDescription));
        ASSERT_EQ(1u, _pDescription->Plugins.size());
        const auto _Plugin = _pDescription->Plugins.front().To<ObjectMap>();
        EXPECT_EQ("icax_machine_control", _Plugin.at("name").To<std::string>());
        EXPECT_NEAR(12.5, _Plugin.at("linear_jog_step_mm").To<double>(), 0.0001);
        EXPECT_NEAR(3.0, _Plugin.at("angular_jog_step_deg").To<double>(), 0.0001);

        const auto _FindElementByOriginalName = [&](const std::string& OriginalName_) -> const iCAX::CAM::SMachineElementData* {
            auto _Iter = std::find_if(
                _pDescription->Elements.begin(),
                _pDescription->Elements.end(),
                [&OriginalName_](const iCAX::CAM::SMachineElementData& Element_) {
                    return Element_.OriginalName == OriginalName_;
                });
            return _Iter == _pDescription->Elements.end() ? nullptr : &*_Iter;
        };

        const auto* _pBaseElement = _FindElementByOriginalName("base");
        const auto* _pHeadElement = _FindElementByOriginalName("head");
        ASSERT_NE(nullptr, _pBaseElement);
        ASSERT_NE(nullptr, _pHeadElement);
        EXPECT_EQ("part", _pBaseElement->ElementKind);
        EXPECT_EQ(_pDescription->RootElementID, _pBaseElement->ParentElementID);
        ASSERT_EQ(1u, _pBaseElement->Visuals.size());
        EXPECT_EQ("base_box", _pBaseElement->Visuals.front().Name);
        ASSERT_EQ(1u, _pBaseElement->Collisions.size());
        EXPECT_EQ("base_collision", _pBaseElement->Collisions.front().Name);

        EXPECT_EQ("part", _pHeadElement->ElementKind);
        EXPECT_EQ(_pBaseElement->ElementID, _pHeadElement->ParentElementID);
        EXPECT_TRUE(_pHeadElement->JointToParent.bValid);
        EXPECT_EQ("lift", _pHeadElement->JointToParent.Name);
        EXPECT_EQ("fixed", _pHeadElement->JointToParent.JointType);
        EXPECT_TRUE(_pHeadElement->Tool.bValid);
        EXPECT_EQ("Laser Head TCP", _pHeadElement->Tool.Name);
        EXPECT_EQ("laser_cutting_head", _pHeadElement->Tool.ToolType);
        ASSERT_EQ(3u, _pHeadElement->Tool.TcpLocalPosition.size());
        EXPECT_NEAR(-30.0, _pHeadElement->Tool.TcpLocalPosition[2].To<double>(), 0.0001);
        ASSERT_EQ(1u, _pHeadElement->Visuals.size());
        EXPECT_EQ("head_sphere", _pHeadElement->Visuals.front().Name);
    }

    ObjectMap _InstantiatePayload;
    _InstantiatePayload["machineDefinitionId"] = _DefinitionIDText;
    _InstantiatePayload["name"] = std::string("Unit Machine Instance");
    const auto _InstantiateRequest = MakeCommandRequest("Machine", "Instantiate", _InstantiatePayload);
    ObjectMap _MachineList;
    {
        SCOPED_TRACE("instantiate machine");
        _MachineList = DecodeResponseObject(
            iCAX::CAM::Commands::HandleInstantiateMachine(_InstantiateRequest, _Application, &_Product, &_Project, &_Scene));
    }

    ASSERT_TRUE(_MachineList.contains("machines"));
    const auto _MachinePayloads = _MachineList.at("machines").To<VariantArray>();
    ASSERT_EQ(1u, _MachinePayloads.size());
    const auto _MachinePayload = _MachinePayloads.front().To<ObjectMap>();
    EXPECT_EQ("Unit Machine Instance", _MachinePayload.at("name").To<std::string>());
    ASSERT_TRUE(_MachinePayload.contains("links"));
    ASSERT_TRUE(_MachinePayload.contains("joints"));
    ASSERT_TRUE(_MachinePayload.contains("tools"));
    ASSERT_TRUE(_MachinePayload.contains("visuals"));
    ASSERT_TRUE(_MachinePayload.contains("collisions"));
    ASSERT_TRUE(_MachinePayload.contains("materials"));
    EXPECT_EQ(2u, _MachinePayload.at("links").To<VariantArray>().size());
    EXPECT_EQ(1u, _MachinePayload.at("joints").To<VariantArray>().size());
    EXPECT_EQ(1u, _MachinePayload.at("tools").To<VariantArray>().size());
    EXPECT_EQ(2u, _MachinePayload.at("visuals").To<VariantArray>().size());
    EXPECT_EQ(1u, _MachinePayload.at("collisions").To<VariantArray>().size());
    EXPECT_EQ(2u, _MachinePayload.at("materials").To<VariantArray>().size());
    EXPECT_NEAR(12.5, _MachinePayload.at("linearJogStep").To<double>(), 0.0001);
    EXPECT_NEAR(3.0, _MachinePayload.at("angularJogStep").To<double>(), 0.0001);
    ASSERT_TRUE(_MachinePayload.contains("elements"));
    const auto _MachineElements = _MachinePayload.at("elements").To<VariantArray>();
    ASSERT_EQ(3u, _MachineElements.size());
    for (const auto& _ElementVariant : _MachineElements)
    {
        const auto _ElementPayload = _ElementVariant.To<ObjectMap>();
        const auto _Kind = _ElementPayload.at("kind").To<std::string>();
        EXPECT_NE("visual", _Kind);
        EXPECT_NE("collision", _Kind);
    }

    bool _bSawTextureMaterialResource = false;
    for (const auto& _MaterialVariant : _MachinePayload.at("materials").To<VariantArray>())
    {
        const auto _MaterialPayload = _MaterialVariant.To<ObjectMap>();
        ASSERT_TRUE(_MaterialPayload.contains("materialResourceId"));
        const auto _MaterialResourceID = _MaterialPayload.at("materialResourceId").To<std::string>();
        auto _pMaterial = _Scene.Resources().Get<iCAX::Render::SRenderMaterialData>(_MaterialResourceID);
        ASSERT_NE(nullptr, _pMaterial);
        if (_pMaterial->strBaseColorTextureResourceID.empty())
        {
            continue;
        }

        _bSawTextureMaterialResource = true;
        const auto _TextureResourceID = _pMaterial->strBaseColorTextureResourceID;
        ASSERT_FALSE(_TextureResourceID.empty());
        auto _pTexture = _Scene.Resources().Get<iCAX::Render::SRenderTextureData>(_TextureResourceID);
        ASSERT_NE(nullptr, _pTexture);
        EXPECT_NE(std::string::npos, _pTexture->strSourceURI.find("base.png"));
    }
    EXPECT_TRUE(_bSawTextureMaterialResource);

    auto& _Repository = _Scene.Database();
    const auto _Machines = CollectEntitiesWithComponent<iCAX::CAM::CMachineInstanceComponent>(_Repository);
    ASSERT_EQ(1u, _Machines.size());
    const auto _MachineID = _Machines.front().first->GetID();
    const auto _pMachineTransform = _Machines.front().first->GetComponent<iCAX::Transform::CTransformComponent>();
    ASSERT_NE(nullptr, _pMachineTransform);

    const auto _Links = CollectEntitiesWithComponent<iCAX::CAM::CMachineLinkComponent>(_Repository);
    const auto _Joints = CollectEntitiesWithComponent<iCAX::CAM::CMachineJointComponent>(_Repository);
    const auto _Tools = CollectEntitiesWithComponent<iCAX::CAM::CMachineToolComponent>(_Repository);
    const auto _Visuals = CollectEntitiesWithComponent<iCAX::CAM::CMachineVisualComponent>(_Repository);
    const auto _Collisions = CollectEntitiesWithComponent<iCAX::CAM::CMachineCollisionComponent>(_Repository);
    ASSERT_EQ(2u, _Links.size());
    ASSERT_EQ(1u, _Joints.size());
    ASSERT_EQ(1u, _Tools.size());
    ASSERT_EQ(2u, _Visuals.size());
    ASSERT_EQ(1u, _Collisions.size());

    const auto _MachineChildren = iCAX::Transform::GetTransformChildEntityIDs(_Repository, _MachineID);
    ASSERT_FALSE(_MachineChildren.empty());

    iCAX::Data::uuid _BaseLinkID;
    iCAX::Data::uuid _HeadLinkID;
    for (const auto& [_pLinkEntity, _pLink] : _Links)
    {
        ASSERT_NE(nullptr, _pLinkEntity);
        ASSERT_NE(nullptr, _pLink);
        ASSERT_NE(nullptr, _pLinkEntity->GetComponent<iCAX::Transform::CTransformComponent>());
        if (_pLink->GetLinkName() == "base")
        {
            _BaseLinkID = _pLinkEntity->GetID();
        }
        if (_pLink->GetLinkName() == "head")
        {
            _HeadLinkID = _pLinkEntity->GetID();
        }
    }
    ASSERT_FALSE(_BaseLinkID.is_nil());
    ASSERT_FALSE(_HeadLinkID.is_nil());
    EXPECT_TRUE(HasChild(_MachineChildren, _BaseLinkID));
    ASSERT_EQ(_BaseLinkID, _Collisions.front().first->GetID());
    ASSERT_EQ(1u, _Collisions.front().second->GetCollisions().size());

    const auto _pJointEntity = _Joints.front().first;
    const auto _pJoint = _Joints.front().second;
    ASSERT_NE(nullptr, _pJointEntity);
    ASSERT_NE(nullptr, _pJoint);
    const auto _pJointTransform = _pJointEntity->GetComponent<iCAX::Transform::CTransformComponent>();
    ASSERT_NE(nullptr, _pJointTransform);
    EXPECT_EQ("base", _pJoint->GetParentLinkName());
    EXPECT_EQ("head", _pJoint->GetChildLinkName());
    EXPECT_EQ(_HeadLinkID, _pJointEntity->GetID());
    EXPECT_EQ(_BaseLinkID, _pJointTransform->GetParentEntityID());
    const auto _HeadWorld = _pJointTransform->GetLocalToWorldMatrix();
    EXPECT_NEAR(0.0, _HeadWorld(0, 3), 0.0001);
    EXPECT_NEAR(0.0, _HeadWorld(1, 3), 0.0001);
    EXPECT_NEAR(500.0, _HeadWorld(2, 3), 0.0001);
    EXPECT_TRUE(HasChild(iCAX::Transform::GetTransformChildEntityIDs(_Repository, _BaseLinkID), _pJointEntity->GetID()));

    {
        SCOPED_TRACE("machine tool component is attached to the tool endpoint element");
        ASSERT_EQ(_HeadLinkID, _Tools.front().first->GetID());
        const auto _pTool = _Tools.front().second;
        ASSERT_NE(nullptr, _pTool);
        EXPECT_EQ("Laser Head TCP", _pTool->GetToolName());
        EXPECT_EQ("laser_cutting_head", _pTool->GetToolType());
        const auto _Tcp = _pTool->GetTcpLocalPosition();
        const auto _Beam = _pTool->GetBeamLocalDirection();
        ASSERT_EQ(3u, _Tcp.size());
        ASSERT_EQ(3u, _Beam.size());
        EXPECT_NEAR(-30.0, _Tcp[2].To<double>(), 0.0001);
        EXPECT_NEAR(-1.0, _Beam[2].To<double>(), 0.0001);

        ObjectMap _GetPayload;
        _GetPayload["entityId"] = iCAX::Data::to_string(_HeadLinkID);
        const auto _GetRequest = MakeCommandRequest("Machine", "GetElement", _GetPayload);
        const auto _GetResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleGetMachineElement(_GetRequest, _Application, &_Product, &_Project, &_Scene));
        const auto _ToolPayload = _GetResult.at("machineElement").To<ObjectMap>().at("tool").To<ObjectMap>();
        ASSERT_TRUE(_ToolPayload.contains("tcpWorldPosition"));
        ASSERT_TRUE(_ToolPayload.contains("beamWorldDirection"));
        const auto _WorldTCP = _ToolPayload.at("tcpWorldPosition").To<VariantArray>();
        const auto _WorldBeam = _ToolPayload.at("beamWorldDirection").To<VariantArray>();
        ASSERT_EQ(3u, _WorldTCP.size());
        ASSERT_EQ(3u, _WorldBeam.size());
        EXPECT_NEAR(470.0, _WorldTCP[2].To<double>(), 0.0001);
        EXPECT_NEAR(-1.0, _WorldBeam[2].To<double>(), 0.0001);

        ObjectMap _SetPayload;
        _SetPayload["entityId"] = iCAX::Data::to_string(_HeadLinkID);
        _SetPayload["tcpLocalPosition"] = VariantArray{ 1.0, 2.0, -40.0 };
        _SetPayload["beamLocalDirection"] = VariantArray{ 0.0, 0.0, -2.0 };
        const auto _SetRequest = MakeCommandRequest("Machine", "SetToolTCP", _SetPayload);
        const auto _SetResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleSetMachineToolTCP(_SetRequest, _Application, &_Product, &_Project, &_Scene));
        const auto _EditedTool = _SetResult.at("machineElement").To<ObjectMap>().at("tool").To<ObjectMap>();
        const auto _EditedTCP = _EditedTool.at("tcpLocalPosition").To<VariantArray>();
        const auto _EditedBeam = _EditedTool.at("beamLocalDirection").To<VariantArray>();
        const auto _EditedWorldTCP = _EditedTool.at("tcpWorldPosition").To<VariantArray>();
        EXPECT_NEAR(1.0, _EditedTCP[0].To<double>(), 0.0001);
        EXPECT_NEAR(2.0, _EditedTCP[1].To<double>(), 0.0001);
        EXPECT_NEAR(-40.0, _EditedTCP[2].To<double>(), 0.0001);
        EXPECT_NEAR(-1.0, _EditedBeam[2].To<double>(), 0.0001);
        EXPECT_NEAR(460.0, _EditedWorldTCP[2].To<double>(), 0.0001);
        EXPECT_NEAR(-40.0, _pTool->GetTcpLocalPosition()[2].To<double>(), 0.0001);
        EXPECT_NEAR(-1.0, _pTool->GetBeamLocalDirection()[2].To<double>(), 0.0001);
    }

    bool _bSawBaseVisual = false;
    bool _bSawHeadVisual = false;
    for (const auto& [_pVisualEntity, _pVisual] : _Visuals)
    {
        ASSERT_NE(nullptr, _pVisualEntity);
        ASSERT_NE(nullptr, _pVisual);
        const auto _pTransform = _pVisualEntity->GetComponent<iCAX::Transform::CTransformComponent>();
        const auto _pRender = _pVisualEntity->GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>();
        ASSERT_NE(nullptr, _pTransform);
        ASSERT_NE(nullptr, _pRender);
        EXPECT_FALSE(_pRender->GetGeometryResourceID().empty());
        EXPECT_TRUE(_Scene.Resources().Contains(_pRender->GetGeometryResourceID()));
        EXPECT_EQ(std::string(), _pRender->GetMaterialResourceID());
        auto _pAggregateMesh = _Scene.Resources().Get<iCAX::Render::SRenderMeshData>(_pRender->GetGeometryResourceID());
        ASSERT_NE(nullptr, _pAggregateMesh);
        EXPECT_FALSE(_pAggregateMesh->Positions.empty());
        EXPECT_EQ(_pAggregateMesh->Positions.size(), _pAggregateMesh->VertexColorsRGBA.size());

        const auto _VisualItems = _pVisual->GetVisuals();
        ASSERT_EQ(1u, _VisualItems.size());
        ASSERT_TRUE(_VisualItems.front().Is<ObjectMap>());
        const auto _VisualPayload = _VisualItems.front().To<ObjectMap>();
        ASSERT_TRUE(_VisualPayload.contains("name"));
        ASSERT_TRUE(_VisualPayload.contains("geometryResourceId"));
        ASSERT_TRUE(_VisualPayload.contains("materialResourceId"));
        EXPECT_TRUE(_Scene.Resources().Contains(_VisualPayload.at("geometryResourceId").To<std::string>()));
        const auto _MaterialResourceID = _VisualPayload.at("materialResourceId").To<std::string>();
        EXPECT_TRUE(_Scene.Resources().Contains(_MaterialResourceID));
        EXPECT_NE(nullptr, _Scene.Resources().Get<iCAX::Render::SRenderMaterialData>(_MaterialResourceID));

        if (_VisualPayload.at("name").To<std::string>() == "base_box")
        {
            _bSawBaseVisual = true;
            EXPECT_EQ(_BaseLinkID, _pVisualEntity->GetID());
            EXPECT_EQ(_MachineID, _pTransform->GetParentEntityID());
        }
        if (_VisualPayload.at("name").To<std::string>() == "head_sphere")
        {
            _bSawHeadVisual = true;
            EXPECT_EQ(_HeadLinkID, _pVisualEntity->GetID());
            EXPECT_EQ(_BaseLinkID, _pTransform->GetParentEntityID());
        }
    }
    EXPECT_TRUE(_bSawBaseVisual);
    EXPECT_TRUE(_bSawHeadVisual);

    {
        SCOPED_TRACE("machine element appearance override rebuilds the element render resource");
        auto _pBaseEntity = _Repository.GetEntity(_BaseLinkID);
        ASSERT_NE(nullptr, _pBaseEntity);
        auto _pBaseRender = _pBaseEntity->GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>();
        ASSERT_NE(nullptr, _pBaseRender);
        const auto _BaseRenderResourceID = _pBaseRender->GetGeometryResourceID();
        auto _pBaseMeshBefore = _Scene.Resources().Get<iCAX::Render::SRenderMeshData>(_BaseRenderResourceID);
        ASSERT_NE(nullptr, _pBaseMeshBefore);
        const auto _VersionBefore = _pBaseMeshBefore->nDataVersion;
        const auto _VertexCountBefore = _pBaseMeshBefore->Positions.size();

        ObjectMap _AppearancePayload;
        _AppearancePayload["entityId"] = iCAX::Data::to_string(_BaseLinkID);
        _AppearancePayload["colorHex"] = std::string("#3366CC");
        _AppearancePayload["showCollision"] = false;
        const auto _AppearanceRequest = MakeCommandRequest("Machine", "SetElementAppearance", _AppearancePayload);
        const auto _AppearanceResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleSetMachineElementAppearance(_AppearanceRequest, _Application, &_Product, &_Project, &_Scene));

        ASSERT_TRUE(_AppearanceResult.contains("machineElement"));
        const auto _Element = _AppearanceResult.at("machineElement").To<ObjectMap>();
        ASSERT_TRUE(_Element.contains("appearance"));
        const auto _Appearance = _Element.at("appearance").To<ObjectMap>();
        EXPECT_TRUE(_Appearance.at("useColorOverride").To<bool>());
        EXPECT_EQ(0x3366CCFFull, _Appearance.at("colorRGBA").To<unsigned long long>());
        EXPECT_FALSE(_Appearance.at("showCollision").To<bool>());

        auto _pAppearance = _pBaseEntity->GetComponent<iCAX::CAM::CMachineAppearanceComponent>();
        ASSERT_NE(nullptr, _pAppearance);
        EXPECT_TRUE(_pAppearance->GetUseColorOverride());
        EXPECT_EQ(0x3366CCFFull, _pAppearance->GetColorRGBA());
        EXPECT_FALSE(_pAppearance->GetShowCollision());

        _pBaseRender = _pBaseEntity->GetComponent<iCAX::RenderInteraction::CRenderInstanceComponent>();
        ASSERT_NE(nullptr, _pBaseRender);
        EXPECT_EQ(_BaseRenderResourceID, _pBaseRender->GetGeometryResourceID());
        auto _pBaseMeshAfter = _Scene.Resources().Get<iCAX::Render::SRenderMeshData>(_BaseRenderResourceID);
        ASSERT_NE(nullptr, _pBaseMeshAfter);
        EXPECT_GT(_pBaseMeshAfter->nDataVersion, _VersionBefore);
        EXPECT_LT(_pBaseMeshAfter->Positions.size(), _VertexCountBefore);
        ASSERT_FALSE(_pBaseMeshAfter->VertexColorsRGBA.empty());
        for (const auto _Color : _pBaseMeshAfter->VertexColorsRGBA)
        {
            EXPECT_EQ(0x3366CCFFu, _Color);
        }
    }

    {
        SCOPED_TRACE("machine transform policy allows root placement and locks base link");
        ObjectMap _RootTransformPayload;
        _RootTransformPayload["entityId"] = iCAX::Data::to_string(_MachineID);
        _RootTransformPayload["position"] = VariantArray{ 1.0, 2.0, 3.0 };
        _RootTransformPayload["rotationRadians"] = VariantArray{ 0.1, 0.2, 0.3 };
        const auto _RootTransformRequest = MakeCommandRequest("Machine", "SetElementTransform", _RootTransformPayload);
        const auto _RootTransformResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleSetMachineElementTransform(_RootTransformRequest, _Application, &_Product, &_Project, &_Scene));

        ASSERT_TRUE(_RootTransformResult.contains("machineElement"));
        const auto _RootPayload = _RootTransformResult.at("machineElement").To<ObjectMap>();
        ASSERT_TRUE(_RootPayload.contains("transform"));
        ASSERT_TRUE(_RootPayload.contains("transformEditPolicy"));
        const auto _RootTransform = _RootPayload.at("transform").To<ObjectMap>();
        const auto _RootPolicy = _RootPayload.at("transformEditPolicy").To<ObjectMap>();
        const auto _RootPositionPolicy = _RootPolicy.at("position").To<VariantArray>();
        const auto _RootRotationPolicy = _RootPolicy.at("rotationRadians").To<VariantArray>();
        const auto _RootScalePolicy = _RootPolicy.at("scale").To<VariantArray>();
        ASSERT_EQ(3u, _RootPositionPolicy.size());
        ASSERT_EQ(3u, _RootRotationPolicy.size());
        ASSERT_EQ(3u, _RootScalePolicy.size());
        for (size_t _Index = 0; _Index < 3; ++_Index)
        {
            EXPECT_TRUE(_RootPositionPolicy[_Index].To<ObjectMap>().at("editable").To<bool>());
            EXPECT_TRUE(_RootRotationPolicy[_Index].To<ObjectMap>().at("editable").To<bool>());
            EXPECT_FALSE(_RootScalePolicy[_Index].To<ObjectMap>().at("editable").To<bool>());
        }

        const auto _RootPosition = _RootTransform.at("position").To<VariantArray>();
        const auto _RootRotation = _RootTransform.at("rotationRadians").To<VariantArray>();
        ASSERT_EQ(3u, _RootPosition.size());
        ASSERT_EQ(3u, _RootRotation.size());
        EXPECT_NEAR(1.0, _RootPosition[0].To<double>(), 0.0001);
        EXPECT_NEAR(2.0, _RootPosition[1].To<double>(), 0.0001);
        EXPECT_NEAR(3.0, _RootPosition[2].To<double>(), 0.0001);
        EXPECT_NEAR(0.1, _RootRotation[0].To<double>(), 0.0001);
        EXPECT_NEAR(0.2, _RootRotation[1].To<double>(), 0.0001);
        EXPECT_NEAR(0.3, _RootRotation[2].To<double>(), 0.0001);

        const auto _pEditedRootTransform = _Repository.GetEntity(_MachineID)->GetComponent<iCAX::Transform::CTransformComponent>();
        ASSERT_NE(nullptr, _pEditedRootTransform);
        EXPECT_NEAR(1.0, _pEditedRootTransform->GetPositionX(), 0.0001);
        EXPECT_NEAR(2.0, _pEditedRootTransform->GetPositionY(), 0.0001);
        EXPECT_NEAR(3.0, _pEditedRootTransform->GetPositionZ(), 0.0001);
        EXPECT_NEAR(0.1, _pEditedRootTransform->GetYawRadians(), 0.0001);
        EXPECT_NEAR(0.2, _pEditedRootTransform->GetPitchRadians(), 0.0001);
        EXPECT_NEAR(0.3, _pEditedRootTransform->GetRollRadians(), 0.0001);

        ObjectMap _BasePayload;
        _BasePayload["entityId"] = iCAX::Data::to_string(_BaseLinkID);
        _BasePayload["position"] = VariantArray{ 4.0, 5.0, 6.0 };
        const auto _BaseRequest = MakeCommandRequest("Machine", "SetElementTransform", _BasePayload);
        EXPECT_THROW(
            (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_BaseRequest, _Application, &_Product, &_Project, &_Scene),
            std::invalid_argument);

        ObjectMap _ScalePayload;
        _ScalePayload["entityId"] = iCAX::Data::to_string(_MachineID);
        _ScalePayload["scale"] = VariantArray{ 1.1, 1.0, 1.0 };
        const auto _ScaleRequest = MakeCommandRequest("Machine", "SetElementTransform", _ScalePayload);
        EXPECT_THROW(
            (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_ScaleRequest, _Application, &_Product, &_Project, &_Scene),
            std::invalid_argument);

        ObjectMap _FixedPayload;
        _FixedPayload["entityId"] = iCAX::Data::to_string(_HeadLinkID);
        _FixedPayload["position"] = VariantArray{ 4.0, 5.0, 6.0 };
        const auto _FixedRequest = MakeCommandRequest("Machine", "SetElementTransform", _FixedPayload);
        EXPECT_THROW(
            (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_FixedRequest, _Application, &_Product, &_Project, &_Scene),
            std::invalid_argument);

        const auto _AssertLockedPolicy = [&](const iCAX::Data::uuid& EntityID_) {
            ObjectMap _GetPayload;
            _GetPayload["entityId"] = iCAX::Data::to_string(EntityID_);
            const auto _GetRequest = MakeCommandRequest("Machine", "GetElement", _GetPayload);
            const auto _GetResult = DecodeResponseObject(
                iCAX::CAM::Commands::HandleGetMachineElement(_GetRequest, _Application, &_Product, &_Project, &_Scene));
            const auto _Policy = _GetResult.at("machineElement").To<ObjectMap>().at("transformEditPolicy").To<ObjectMap>();
            const auto _PositionPolicy = _Policy.at("position").To<VariantArray>();
            const auto _RotationPolicy = _Policy.at("rotationRadians").To<VariantArray>();
            ASSERT_EQ(3u, _PositionPolicy.size());
            ASSERT_EQ(3u, _RotationPolicy.size());
            for (size_t _Index = 0; _Index < 3; ++_Index)
            {
                EXPECT_FALSE(_PositionPolicy[_Index].To<ObjectMap>().at("editable").To<bool>());
                EXPECT_FALSE(_RotationPolicy[_Index].To<ObjectMap>().at("editable").To<bool>());
            }
        };
        _AssertLockedPolicy(_BaseLinkID);
        _AssertLockedPolicy(_HeadLinkID);
    }

    {
        SCOPED_TRACE("machine object selection carries owning machine id for UI tree sync");
        ObjectMap _PickPayload;
        _PickPayload["entityId"] = iCAX::Data::to_string(_HeadLinkID);
        _PickPayload["objectId"] = iCAX::Data::to_string(_HeadLinkID);
        const auto _PickRequest = MakeCommandRequest("Selection", "PickMachineObject", _PickPayload);
        const auto _PickResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandlePickMachineObject(_PickRequest, _Application, &_Product, &_Project, &_Scene));

        ASSERT_TRUE(_PickResult.contains("selection"));
        const auto _Selection = _PickResult.at("selection").To<ObjectMap>();
        EXPECT_EQ("machine.part", _Selection.at("kind").To<std::string>());
        EXPECT_EQ(iCAX::Data::to_string(_HeadLinkID), _Selection.at("entityId").To<std::string>());
        ASSERT_TRUE(_Selection.contains("machineId"));
        EXPECT_EQ(iCAX::Data::to_string(_MachineID), _Selection.at("machineId").To<std::string>());
    }
}

TEST(Laser3DCAMMachineDefinitionTest, PrismaticMachineElementUsesInstanceFieldPolicy)
{
    CTestApplicationContext _Application;
    CTestProductContext _Product;
    CTestProjectContext _Project;
    CTestSceneContext _Scene;
    auto& _Repository = _Scene.Database();
    std::string _strError;

    const auto _MachineID = iCAX::Data::GenerateNewUUID();
    auto _pMachineEntity = _Repository.CreateEntity(_MachineID);
    auto _pMachine = _pMachineEntity->AddComponent<iCAX::CAM::CMachineInstanceComponent>();
    auto _pKinematics = _pMachineEntity->AddComponent<iCAX::CAM::CMachineKinematicsComponent>();
    ASSERT_NE(nullptr, _pMachine);
    ASSERT_NE(nullptr, _pKinematics);
    ASSERT_TRUE(_pKinematics->SetLinearJogStep(25.0, _strError)) << _strError;
    ASSERT_TRUE(_pKinematics->SetAngularJogStep(2.0, _strError)) << _strError;

    const auto _AxisElementID = iCAX::Data::GenerateNewUUID();
    auto _pEntity = _Repository.CreateEntity(_AxisElementID);
    auto _pTransform = _pEntity->AddComponent<iCAX::Transform::CTransformComponent>();
    auto _pElement = _pEntity->AddComponent<iCAX::CAM::CMachineElementComponent>();
    auto _pJoint = _pEntity->AddComponent<iCAX::CAM::CMachineJointComponent>();
    ASSERT_NE(nullptr, _pTransform);
    ASSERT_NE(nullptr, _pElement);
    ASSERT_NE(nullptr, _pJoint);

    ASSERT_TRUE(_pElement->SetMachineID(_MachineID, _strError)) << _strError;
    ASSERT_TRUE(_pElement->SetElementKind("part", _strError)) << _strError;
    ASSERT_TRUE(_pElement->SetName("x_axis", _strError)) << _strError;
    ASSERT_TRUE(_pJoint->SetJointName("x_slide", _strError)) << _strError;
    ASSERT_TRUE(_pJoint->SetJointType("prismatic", _strError)) << _strError;
    ASSERT_TRUE(_pJoint->SetAxis(VariantArray{ 1.0, 0.0, 0.0 }, _strError)) << _strError;
    ASSERT_TRUE(_pJoint->SetLowerLimit(0.0, _strError)) << _strError;
    ASSERT_TRUE(_pJoint->SetUpperLimit(10.0, _strError)) << _strError;

    ObjectMap _ValidPayload;
    _ValidPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _ValidPayload["position"] = VariantArray{ 5.0, 0.0, 0.0 };
    const auto _ValidRequest = MakeCommandRequest("Machine", "SetElementTransform", _ValidPayload);
    const auto _ValidResult = DecodeResponseObject(
        iCAX::CAM::Commands::HandleSetMachineElementTransform(_ValidRequest, _Application, &_Product, &_Project, &_Scene));
    const auto _Policy = _ValidResult.at("machineElement").To<ObjectMap>().at("transformEditPolicy").To<ObjectMap>();
    const auto _PositionPolicy = _Policy.at("position").To<VariantArray>();
    const auto _RotationPolicy = _Policy.at("rotationRadians").To<VariantArray>();
    ASSERT_EQ(3u, _PositionPolicy.size());
    ASSERT_EQ(3u, _RotationPolicy.size());
    EXPECT_TRUE(_PositionPolicy[0].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_NEAR(0.0, _PositionPolicy[0].To<ObjectMap>().at("min").To<double>(), 0.0001);
    EXPECT_NEAR(10.0, _PositionPolicy[0].To<ObjectMap>().at("max").To<double>(), 0.0001);
    EXPECT_NEAR(25.0, _PositionPolicy[0].To<ObjectMap>().at("step").To<double>(), 0.0001);
    EXPECT_FALSE(_PositionPolicy[1].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_FALSE(_PositionPolicy[2].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_FALSE(_RotationPolicy[0].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_FALSE(_RotationPolicy[1].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_FALSE(_RotationPolicy[2].To<ObjectMap>().at("editable").To<bool>());
    EXPECT_NEAR(2.0 * 3.14159265358979323846 / 180.0, _RotationPolicy[0].To<ObjectMap>().at("step").To<double>(), 0.0001);

    ObjectMap _LimitPayload;
    _LimitPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _LimitPayload["lower"] = -5.0;
    _LimitPayload["upper"] = 15.0;
    const auto _LimitRequest = MakeCommandRequest("Machine", "SetJointLimits", _LimitPayload);
    const auto _LimitResult = DecodeResponseObject(
        iCAX::CAM::Commands::HandleSetMachineJointLimits(_LimitRequest, _Application, &_Product, &_Project, &_Scene));
    ASSERT_TRUE(_LimitResult.contains("machineElement"));
    const auto _JointPayload = _LimitResult.at("machineElement").To<ObjectMap>().at("joint").To<ObjectMap>();
    EXPECT_NEAR(-5.0, _JointPayload.at("lower").To<double>(), 0.0001);
    EXPECT_NEAR(15.0, _JointPayload.at("upper").To<double>(), 0.0001);
    EXPECT_NEAR(-5.0, _pJoint->GetLowerLimit(), 0.0001);
    EXPECT_NEAR(15.0, _pJoint->GetUpperLimit(), 0.0001);

    ObjectMap _InvalidLimitPayload;
    _InvalidLimitPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _InvalidLimitPayload["lower"] = 16.0;
    _InvalidLimitPayload["upper"] = 15.0;
    const auto _InvalidLimitRequest = MakeCommandRequest("Machine", "SetJointLimits", _InvalidLimitPayload);
    EXPECT_THROW(
        (void)iCAX::CAM::Commands::HandleSetMachineJointLimits(_InvalidLimitRequest, _Application, &_Product, &_Project, &_Scene),
        std::invalid_argument);

    ObjectMap _CurrentPositionOutsideLimitPayload;
    _CurrentPositionOutsideLimitPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _CurrentPositionOutsideLimitPayload["lower"] = 1.0;
    _CurrentPositionOutsideLimitPayload["upper"] = 15.0;
    const auto _CurrentPositionOutsideLimitRequest = MakeCommandRequest("Machine", "SetJointLimits", _CurrentPositionOutsideLimitPayload);
    EXPECT_THROW(
        (void)iCAX::CAM::Commands::HandleSetMachineJointLimits(_CurrentPositionOutsideLimitRequest, _Application, &_Product, &_Project, &_Scene),
        std::invalid_argument);

    ObjectMap _OutOfRangePayload;
    _OutOfRangePayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _OutOfRangePayload["position"] = VariantArray{ 16.0, 0.0, 0.0 };
    const auto _OutOfRangeRequest = MakeCommandRequest("Machine", "SetElementTransform", _OutOfRangePayload);
    EXPECT_THROW(
        (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_OutOfRangeRequest, _Application, &_Product, &_Project, &_Scene),
        std::invalid_argument);

    ObjectMap _OffAxisPayload;
    _OffAxisPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _OffAxisPayload["position"] = VariantArray{ 5.0, 1.0, 0.0 };
    const auto _OffAxisRequest = MakeCommandRequest("Machine", "SetElementTransform", _OffAxisPayload);
    EXPECT_THROW(
        (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_OffAxisRequest, _Application, &_Product, &_Project, &_Scene),
        std::invalid_argument);

    ObjectMap _RotationPayload;
    _RotationPayload["entityId"] = iCAX::Data::to_string(_AxisElementID);
    _RotationPayload["rotationRadians"] = VariantArray{ 0.1, 0.0, 0.0 };
    const auto _RotationRequest = MakeCommandRequest("Machine", "SetElementTransform", _RotationPayload);
    EXPECT_THROW(
        (void)iCAX::CAM::Commands::HandleSetMachineElementTransform(_RotationRequest, _Application, &_Product, &_Project, &_Scene),
        std::invalid_argument);
}

TEST(Laser3DCAMMachineDefinitionTest, CatalogOwnsDefinitionEntitiesAndInstanceReferencesDefinition)
{
    CTestApplicationContext _Application;
    CTestProductContext _Product;
    CTestProjectContext _Project;
    CTestSceneContext _Scene;
    auto& _Repository = _Scene.Database();
    const auto _DefinitionID = iCAX::Data::GenerateNewUUID();
    const auto _InstanceID = iCAX::Data::GenerateNewUUID();
    const std::string _MachineResourceID = "machine/definition/test#description";
    std::string _strError;

    std::shared_ptr<iCAX::CAM::CMachineDefinitionCatalogComponent> _pCatalog;
    {
        SCOPED_TRACE("create machine definition catalog");
        _pCatalog = _Repository.GetMetaEntity()->AddComponent<iCAX::CAM::CMachineDefinitionCatalogComponent>();
        ASSERT_NE(nullptr, _pCatalog);
        ASSERT_TRUE(_pCatalog->SetMachineDefinitionIDs(VariantArray{ iCAX::Data::to_string(_DefinitionID) }, _strError)) << _strError;
    }

    auto _pDefinitionEntity = _Repository.CreateEntity(_DefinitionID);
    auto _pDefinition = _pDefinitionEntity->AddComponent<iCAX::CAM::CMachineDefinitionComponent>();
    {
        SCOPED_TRACE("write machine definition summary component");
        ASSERT_NE(nullptr, _pDefinition);
        ASSERT_TRUE(_pDefinition->SetName("Five Axis Laser Head", _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetSourcePath("D:/test/machine.sdf", _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetMachineResourceID(_MachineResourceID, _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetDescriptionFormat("sdf", _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetModelName("head", _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetLinkCount(2, _strError)) << _strError;
        ASSERT_TRUE(_pDefinition->SetJointCount(1, _strError)) << _strError;
        EXPECT_TRUE(_pDefinition->GetEnabled());
        ASSERT_TRUE(_pDefinition->SetEnabled(false, _strError)) << _strError;
        EXPECT_FALSE(_pDefinition->GetEnabled());
        ASSERT_TRUE(_pDefinition->SetEnabled(true, _strError)) << _strError;
    }

    {
        SCOPED_TRACE("verify machine definition catalog");
        ASSERT_EQ(1u, _pCatalog->GetMachineDefinitionIDs().size());
        EXPECT_EQ(iCAX::Data::to_string(_DefinitionID), _pCatalog->GetMachineDefinitionIDs().front().To<std::string>());
        EXPECT_EQ(_MachineResourceID, _pDefinition->GetMachineResourceID());
    }

    auto _pInstanceEntity = _Repository.CreateEntity(_InstanceID);
    auto _pInstance = _pInstanceEntity->AddComponent<iCAX::CAM::CMachineInstanceComponent>();
    {
        SCOPED_TRACE("write machine instance component");
        ASSERT_NE(nullptr, _pInstance);
        ASSERT_TRUE(_pInstance->SetMachineDefinitionID(iCAX::Data::to_string(_DefinitionID), _strError)) << _strError;
        ASSERT_TRUE(_pInstance->SetMachineResourceID(_MachineResourceID, _strError)) << _strError;
    }

    {
        SCOPED_TRACE("verify machine instance reference");
        EXPECT_EQ(iCAX::Data::to_string(_DefinitionID), _pInstance->GetMachineDefinitionID());
        EXPECT_EQ(_MachineResourceID, _pInstance->GetMachineResourceID());
    }

    auto _pDescription = std::make_shared<iCAX::CAM::CMachineDescriptionResource>();
    _pDescription->SourceFormat = "SDF";
    _pDescription->ModelName = "head";
    _Scene.Resources().Set<iCAX::CAM::CMachineDescriptionResource>(_MachineResourceID, _pDescription, MakeResourceInfo(_MachineResourceID, 1));
    ASSERT_TRUE(_Scene.Resources().Contains(_MachineResourceID));

    ObjectMap _DeletePayload;
    _DeletePayload["machineDefinitionId"] = iCAX::Data::to_string(_DefinitionID);
    const auto _DeleteRequest = MakeCommandRequest("MachineDefinition", "Delete", _DeletePayload);
    {
        SCOPED_TRACE("definition cannot be deleted while an instance references it");
        EXPECT_THROW(
            (void)iCAX::CAM::Commands::HandleDeleteMachineDefinition(_DeleteRequest, _Application, &_Product, &_Project, &_Scene),
            std::runtime_error);
        EXPECT_NE(nullptr, _Repository.GetEntity(_DefinitionID));
        EXPECT_TRUE(_Scene.Resources().Contains(_MachineResourceID));
    }

    {
        SCOPED_TRACE("definition deletion removes catalog entry and resource after references are gone");
        ASSERT_TRUE(_Repository.DeleteEntity(_InstanceID, _strError)) << _strError;
        const auto _DeleteResult = DecodeResponseObject(
            iCAX::CAM::Commands::HandleDeleteMachineDefinition(_DeleteRequest, _Application, &_Product, &_Project, &_Scene));

        EXPECT_EQ(iCAX::Data::to_string(_DefinitionID), _DeleteResult.at("deletedMachineDefinitionId").To<std::string>());
        EXPECT_TRUE(_DeleteResult.at("definitions").To<VariantArray>().empty());
        EXPECT_EQ(nullptr, _Repository.GetEntity(_DefinitionID));
        EXPECT_TRUE(_pCatalog->GetMachineDefinitionIDs().empty());
        EXPECT_FALSE(_Scene.Resources().Contains(_MachineResourceID));
    }
}

TEST(Laser3DCAMJobTest, JobOwnsSelectedMachineInstance)
{
    CTestSceneContext _Scene;
    auto& _Repository = _Scene.Database();
    const auto _MachineA = iCAX::Data::GenerateNewUUID();
    const auto _MachineB = iCAX::Data::GenerateNewUUID();
    std::string _strError;

    auto _pMachineAEntity = _Repository.CreateEntity(_MachineA);
    auto _pMachineA = _pMachineAEntity->AddComponent<iCAX::CAM::CMachineInstanceComponent>();
    ASSERT_NE(nullptr, _pMachineA);
    ASSERT_TRUE(_pMachineA->SetName("Machine A", _strError)) << _strError;

    auto _pMachineBEntity = _Repository.CreateEntity(_MachineB);
    auto _pMachineB = _pMachineBEntity->AddComponent<iCAX::CAM::CMachineInstanceComponent>();
    ASSERT_NE(nullptr, _pMachineB);
    ASSERT_TRUE(_pMachineB->SetName("Machine B", _strError)) << _strError;

    auto _pJobEntity = _Repository.CreateEntity(iCAX::Data::GenerateNewUUID());
    auto _pJob = _pJobEntity->AddComponent<iCAX::CAM::CJobComponent>();
    ASSERT_NE(nullptr, _pJob);
    ASSERT_TRUE(_pJob->SetName("Cut Job", _strError)) << _strError;

    ASSERT_TRUE(_pJob->SetMachineEntityID(_MachineA, _strError)) << _strError;
    EXPECT_EQ(_MachineA, _pJob->GetMachineEntityID());

    ASSERT_TRUE(_pJob->SetMachineEntityID(_MachineB, _strError)) << _strError;
    EXPECT_EQ(_MachineB, _pJob->GetMachineEntityID());
}

TEST(Laser3DCAMToolpathOrderTest, NearestNeighborReordersConsecutivePathRun)
{
    CTestSceneContext _Scene;
    iCAX::Services::CServiceProvider _Provider;
    RegisterLaserServices(_Provider);
    auto _pService = _Provider.Resolve<iCAX::CAM::IToolpathOrderService>();
    ASSERT_NE(nullptr, _pService);

    auto& _Repository = _Scene.Database();
    const auto _BlockID = iCAX::Data::GenerateNewUUID();
    const auto _PathA = iCAX::Data::GenerateNewUUID();
    const auto _PathB = iCAX::Data::GenerateNewUUID();
    const auto _PathC = iCAX::Data::GenerateNewUUID();

    auto _pRoot = _Repository.GetMetaEntity()->AddComponent<iCAX::CAM::CRootComponent>();
    std::string _strError;
    ASSERT_TRUE(_pRoot->SetProgramRootBlockID(_BlockID, _strError)) << _strError;

    auto _pBlockEntity = _Repository.CreateEntity(_BlockID);
    auto _pBlock = _pBlockEntity->AddComponent<iCAX::CAM::CBlockComponent>();
    AddPath(_Scene, _PathA, "curve.a", 100.0, 101.0);
    AddPath(_Scene, _PathB, "curve.b", 0.0, 1.0);
    AddPath(_Scene, _PathC, "curve.c", 2.0, 3.0);

    ASSERT_TRUE(_pBlock->SetChildren(
        VariantArray{
            MakeChild("path", _PathA),
            MakeChild("path", _PathB),
            MakeChild("path", _PathC)
        },
        _strError)) << _strError;

    iCAX::CAM::SToolpathOrderRequest _Request;
    _Request.BlockEntityID = _BlockID;
    _Request.Strategy = "NearestNeighbor";
    const auto _Plan = _pService->BuildOrderPlan(_Scene, _Request);

    ASSERT_EQ(3u, _Plan.OrderedChildren.size());
    EXPECT_EQ(_PathA, _Plan.OrderedChildren[0].EntityID);
    EXPECT_EQ(_PathC, _Plan.OrderedChildren[1].EntityID);
    EXPECT_EQ(_PathB, _Plan.OrderedChildren[2].EntityID);
    EXPECT_GT(_Plan.dEstimatedCost, 0.0);

    ASSERT_TRUE(_pService->ApplyOrderPlan(_Scene, _Plan, _strError)) << _strError;
    const auto _Children = _pBlock->GetChildren();
    ASSERT_EQ(3u, _Children.size());
    EXPECT_EQ(iCAX::Data::to_string(_PathC), _Children[1].To<ObjectMap>().at("entityId").To<std::string>());
}
