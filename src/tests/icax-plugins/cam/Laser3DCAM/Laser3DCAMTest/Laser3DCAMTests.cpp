#include <gtest/gtest.h>

#include <Laser3DCAM/FeatureRecognitionService.h>
#include <Laser3DCAM/Laser3DCAM.h>
#include <Laser3DCAM/ToolpathComponents.h>
#include <Laser3DCAM/ToolpathOrderService.h>
#include <Laser3DCAM/ToolpathResources.h>

#include <Data/Variant.h>
#include <Database/IEntity.h>
#include <Database/IMetaRegistry.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <GeometryData/GeometryData.h>
#include <Mailbox/MailChannel.h>
#include <PDO/IPDOHub.h>
#include <ProjectContext/ISceneContext.h>
#include <ProjectContext/SceneObjectRegistry.h>
#include <Resources/ResourceInfo.h>
#include <Resources/ResourceLibrary.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

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

        iCAX::Project::CSceneObjectRegistry& Objects() override
        {
            return m_Objects;
        }

        const iCAX::Project::CSceneObjectRegistry& Objects() const override
        {
            return m_Objects;
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
        iCAX::Project::CSceneObjectRegistry m_Objects;
        mutable iCAX::Services::CServiceProvider m_ServiceProvider;
        std::shared_ptr<iCAX::Mail::CMailChannel> m_pChannel;
        std::string m_SceneName = "Laser3DCAM Test Scene";
    };

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
