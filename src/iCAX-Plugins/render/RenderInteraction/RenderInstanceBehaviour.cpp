#include "pch.h"
#include "RenderInteraction.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "GeometryData/GeometryData.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/SceneObjectRegistry.h"
#include "RenderData/RenderData.h"
#include "RenderService/RenderService.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServiceProvider.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr double kEpsilon = 0.000001;

    uint64_t NextRenderVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t MakeNonZeroVersion(IN uint64_t nVersion_) noexcept
    {
        return nVersion_ == 0 ? NextRenderVersion() : nVersion_;
    }

    void EnsureRenderScene(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_)
    {
        if (!RenderService_.HasScene(ProjectID_, nSceneID_))
        {
            (void)RenderService_.CreateScene(ProjectID_, nSceneID_);
        }
    }

    uint32_t MakeRenderFlags(
        IN const iCAX::RenderInteraction::CRenderInstanceComponent& Component_,
        IN bool bEnabled_) noexcept
    {
        if (!bEnabled_)
        {
            return iCAX::Render::kRenderFlagDisabled;
        }

        uint32_t _Flags = 0;
        if (Component_.GetVisible())
        {
            _Flags |= iCAX::Render::kRenderFlagVisible;
        }
        if (Component_.GetSelectable())
        {
            _Flags |= iCAX::Render::kRenderFlagSelectable;
        }
        if (Component_.GetHighlighted())
        {
            _Flags |= iCAX::Render::kRenderFlagHighlighted;
        }
        if (Component_.GetSelected())
        {
            _Flags |= iCAX::Render::kRenderFlagSelected;
        }
        return _Flags;
    }

    iCAX::Render::ERenderGeometryKind ToGeometryKind(IN unsigned long long nValue_)
    {
        switch (nValue_)
        {
        case 1ull:
            return iCAX::Render::ERenderGeometryKind::Mesh;
        case 2ull:
            return iCAX::Render::ERenderGeometryKind::Polyline;
        case 3ull:
            return iCAX::Render::ERenderGeometryKind::Toolpath;
        default:
            throw std::invalid_argument("RenderInstanceComponent has unsupported geometry kind");
        }
    }

    iCAX::Render::ERenderClass ToRenderClass(IN unsigned long long nValue_)
    {
        switch (nValue_)
        {
        case 1ull:
            return iCAX::Render::ERenderClass::Model;
        case 2ull:
            return iCAX::Render::ERenderClass::Stock;
        case 3ull:
            return iCAX::Render::ERenderClass::Fixture;
        case 4ull:
            return iCAX::Render::ERenderClass::Tool;
        case 5ull:
            return iCAX::Render::ERenderClass::ToolpathRapid;
        case 6ull:
            return iCAX::Render::ERenderClass::ToolpathCutting;
        case 7ull:
            return iCAX::Render::ERenderClass::Selection;
        case 8ull:
            return iCAX::Render::ERenderClass::Highlight;
        default:
            throw std::invalid_argument("RenderInstanceComponent has unsupported render class");
        }
    }

    iCAX::Render::SRenderStyleData MakeStyle(
        IN const iCAX::RenderInteraction::CRenderInstanceComponent& Component_,
        IN bool bEnabled_) noexcept
    {
        iCAX::Render::SRenderStyleData _Style;
        _Style.nStyleID = static_cast<iCAX::Render::RenderStyleID>(Component_.GetStyleID());
        _Style.nColorRGBA = static_cast<uint32_t>(Component_.GetColorRGBA());
        _Style.nLineWidth = static_cast<float>(Component_.GetLineWidth());
        _Style.nFlags = MakeRenderFlags(Component_, bEnabled_);
        return _Style;
    }

    bool SameStyle(
        IN const iCAX::Render::SRenderStyleData& Left_,
        IN const iCAX::Render::SRenderStyleData& Right_) noexcept
    {
        return Left_.nStyleID == Right_.nStyleID
            && Left_.nColorRGBA == Right_.nColorRGBA
            && std::abs(Left_.nLineWidth - Right_.nLineWidth) <= static_cast<float>(kEpsilon)
            && Left_.nFlags == Right_.nFlags;
    }

    bool SameInstance(
        IN const iCAX::Render::SRenderInstanceData& Left_,
        IN const iCAX::Render::SRenderInstanceData& Right_) noexcept
    {
        return Left_.nObjectID == Right_.nObjectID
            && Left_.nGeometryID == Right_.nGeometryID
            && Left_.eGeometryKind == Right_.eGeometryKind
            && Left_.eRenderClass == Right_.eRenderClass
            && Left_.nStyleID == Right_.nStyleID
            && Left_.nFlags == Right_.nFlags;
    }

    iCAX::Render::SRenderInstanceData MakeInstance(
        IN const iCAX::RenderInteraction::CRenderInstanceComponent& Component_,
        IN iCAX::Render::SceneObjectID nObjectID_,
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN bool bEnabled_)
    {
        iCAX::Render::SRenderInstanceData _Instance;
        _Instance.nObjectID = nObjectID_;
        _Instance.nGeometryID = nGeometryID_;
        _Instance.eGeometryKind = ToGeometryKind(Component_.GetGeometryKind());
        _Instance.eRenderClass = ToRenderClass(Component_.GetRenderClass());
        _Instance.nStyleID = static_cast<iCAX::Render::RenderStyleID>(Component_.GetStyleID());
        _Instance.nFlags = MakeRenderFlags(Component_, bEnabled_);
        return _Instance;
    }

    iCAX::Render::SFloat3 ToRenderFloat3(IN const iCAX::GeometryData::Point3& Point_) noexcept
    {
        return {
            static_cast<float>(Point_.X),
            static_cast<float>(Point_.Y),
            static_cast<float>(Point_.Z)
        };
    }

    iCAX::Render::SFloat3 ToRenderFloat3(IN const iCAX::GeometryData::Vector3& Vector_) noexcept
    {
        return {
            static_cast<float>(Vector_.X),
            static_cast<float>(Vector_.Y),
            static_cast<float>(Vector_.Z)
        };
    }

    iCAX::Render::SRenderMeshData MakeRenderMeshFromBRep(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN uint64_t nDataVersion_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nDataVersion = MakeNonZeroVersion(nDataVersion_);
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;

        bool _bAllTriangulationsHaveNormals = true;
        bool _bAnyTriangulationHasNormals = false;
        for (const auto& _TriangulationRecord : Model_.Triangulations3)
        {
            const auto& _Geometry = _TriangulationRecord.Geometry;
            if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
            {
                continue;
            }
            if (_Mesh.Positions.size() + _Geometry.Vertices.size() > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
            {
                throw std::runtime_error("BRep render mesh has too many vertices for uint32 indices");
            }

            const auto _BaseVertex = static_cast<uint32_t>(_Mesh.Positions.size());
            for (const auto& _Point : _Geometry.Vertices)
            {
                _Mesh.Positions.push_back(ToRenderFloat3(_Point));
            }

            if (_Geometry.Normals.size() == _Geometry.Vertices.size())
            {
                _bAnyTriangulationHasNormals = true;
                for (const auto& _Normal : _Geometry.Normals)
                {
                    _Mesh.Normals.push_back(ToRenderFloat3(_Normal));
                }
            }
            else
            {
                _bAllTriangulationsHaveNormals = false;
            }

            for (const auto& _Triangle : _Geometry.Triangles)
            {
                for (const auto _Index : _Triangle)
                {
                    if (_Index >= _Geometry.Vertices.size())
                    {
                        throw std::runtime_error("BRep render mesh triangulation index is out of range");
                    }
                    _Mesh.Indices.push_back(_BaseVertex + _Index);
                }
            }
        }

        if (_Mesh.Positions.empty() || _Mesh.Indices.empty())
        {
            throw std::runtime_error("BRep model does not have display triangulation");
        }

        if (_bAnyTriangulationHasNormals && _bAllTriangulationsHaveNormals && _Mesh.Normals.size() == _Mesh.Positions.size())
        {
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasNormals;
        }
        else
        {
            _Mesh.Normals.clear();
        }
        return _Mesh;
    }

    bool HasCurrentMeshGeometry(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN uint64_t nResourceVersion_)
    {
        const auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nSceneID_);
        const auto _Iter = _Snapshot.Meshes.find(nGeometryID_);
        return _Iter != _Snapshot.Meshes.end()
            && _Iter->second.nDataVersion >= MakeNonZeroVersion(nResourceVersion_);
    }

    void UpsertGeometryResource(
        IN iCAX::Render::IRenderService& RenderService_,
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN const std::string& strGeometryResourceID_,
        IN iCAX::Render::RenderGeometryID nGeometryID_,
        IN iCAX::Render::ERenderGeometryKind eGeometryKind_)
    {
        if (strGeometryResourceID_.empty())
        {
            throw std::invalid_argument("RenderInstanceComponent requires GeometryResourceID");
        }

        const auto _ResourceVersion = Resources_.GetVersion(strGeometryResourceID_);
        if (_ResourceVersion == 0)
        {
            throw std::runtime_error("Render geometry resource does not exist: " + strGeometryResourceID_);
        }

        if (eGeometryKind_ == iCAX::Render::ERenderGeometryKind::Mesh)
        {
            auto _pMesh = Resources_.Get<iCAX::Render::SRenderMeshData>(strGeometryResourceID_);
            if (_pMesh)
            {
                auto _Mesh = *_pMesh;
                _Mesh.nGeometryID = nGeometryID_;
                _Mesh.nDataVersion = MakeNonZeroVersion((std::max)(_Mesh.nDataVersion, _ResourceVersion));
                if (!RenderService_.UpsertMesh(ProjectID_, nSceneID_, _Mesh))
                {
                    throw std::runtime_error("RenderService rejected mesh resource: " + strGeometryResourceID_);
                }
                return;
            }

            auto _pBRep = Resources_.Get<iCAX::GeometryData::BRepModel>(strGeometryResourceID_);
            if (!_pBRep)
            {
                throw std::runtime_error("Render geometry resource is not mesh or BRep data: " + strGeometryResourceID_);
            }
            if (HasCurrentMeshGeometry(RenderService_, ProjectID_, nSceneID_, nGeometryID_, _ResourceVersion))
            {
                return;
            }

            auto _Mesh = MakeRenderMeshFromBRep(*_pBRep, _ResourceVersion);
            _Mesh.nGeometryID = nGeometryID_;
            if (!RenderService_.UpsertMesh(ProjectID_, nSceneID_, _Mesh))
            {
                throw std::runtime_error("RenderService rejected BRep mesh resource: " + strGeometryResourceID_);
            }
            return;
        }

        if (eGeometryKind_ == iCAX::Render::ERenderGeometryKind::Polyline)
        {
            auto _pPolyline = Resources_.Get<iCAX::Render::SRenderPolylineData>(strGeometryResourceID_);
            if (!_pPolyline)
            {
                throw std::runtime_error("Render geometry resource is not polyline data: " + strGeometryResourceID_);
            }

            auto _Polyline = *_pPolyline;
            _Polyline.nGeometryID = nGeometryID_;
            _Polyline.nDataVersion = MakeNonZeroVersion((std::max)(_Polyline.nDataVersion, _ResourceVersion));
            if (!RenderService_.UpsertPolyline(ProjectID_, nSceneID_, _Polyline))
            {
                throw std::runtime_error("RenderService rejected polyline resource: " + strGeometryResourceID_);
            }
            return;
        }

        if (eGeometryKind_ == iCAX::Render::ERenderGeometryKind::Toolpath)
        {
            auto _pToolpath = Resources_.Get<iCAX::Render::SRenderToolpathData>(strGeometryResourceID_);
            if (!_pToolpath)
            {
                throw std::runtime_error("Render geometry resource is not toolpath data: " + strGeometryResourceID_);
            }

            auto _Toolpath = *_pToolpath;
            _Toolpath.nGeometryID = nGeometryID_;
            _Toolpath.nDataVersion = MakeNonZeroVersion((std::max)(_Toolpath.nDataVersion, _ResourceVersion));
            if (!RenderService_.UpsertToolpath(ProjectID_, nSceneID_, _Toolpath))
            {
                throw std::runtime_error("RenderService rejected toolpath resource: " + strGeometryResourceID_);
            }
            return;
        }

        throw std::invalid_argument("RenderInstanceComponent has unsupported geometry kind");
    }

    bool UpsertInstanceAndStyle(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN const iCAX::Render::SRenderInstanceData& Instance_,
        IN const iCAX::Render::SRenderStyleData& Style_,
        IN uint64_t nVersion_)
    {
        auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nSceneID_);
        auto _Instances = _Snapshot.Instances;
        auto _Styles = _Snapshot.Styles;
        bool _bDirty = false;

        auto _InstanceIter = std::find_if(
            _Instances.begin(),
            _Instances.end(),
            [&Instance_](IN const iCAX::Render::SRenderInstanceData& Item_)
            {
                return Item_.nObjectID == Instance_.nObjectID;
            });
        if (_InstanceIter == _Instances.end())
        {
            _Instances.push_back(Instance_);
            _bDirty = true;
        }
        else if (!SameInstance(*_InstanceIter, Instance_))
        {
            *_InstanceIter = Instance_;
            _bDirty = true;
        }

        auto _StyleIter = std::find_if(
            _Styles.begin(),
            _Styles.end(),
            [&Style_](IN const iCAX::Render::SRenderStyleData& Item_)
            {
                return Item_.nStyleID == Style_.nStyleID;
            });
        if (_StyleIter == _Styles.end())
        {
            _Styles.push_back(Style_);
            _bDirty = true;
        }
        else if (!SameStyle(*_StyleIter, Style_))
        {
            *_StyleIter = Style_;
            _bDirty = true;
        }

        if (!_bDirty)
        {
            return false;
        }

        if (!RenderService_.SetInstances(ProjectID_, nSceneID_, _Instances, _Styles, MakeNonZeroVersion(nVersion_)))
        {
            throw std::runtime_error("RenderService rejected render instance update");
        }
        return true;
    }

    std::optional<iCAX::Render::RenderGeometryID> RemoveInstance(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN iCAX::Render::SceneObjectID nObjectID_)
    {
        if (!RenderService_.HasScene(ProjectID_, nSceneID_))
        {
            return std::nullopt;
        }

        auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nSceneID_);
        std::optional<iCAX::Render::RenderGeometryID> _RemovedGeometryID;
        for (const auto& _Instance : _Snapshot.Instances)
        {
            if (_Instance.nObjectID == nObjectID_)
            {
                _RemovedGeometryID = _Instance.nGeometryID;
                break;
            }
        }
        if (!_RemovedGeometryID)
        {
            return std::nullopt;
        }

        auto _Instances = _Snapshot.Instances;
        const auto _OriginalSize = _Instances.size();
        std::erase_if(_Instances, [nObjectID_](IN const iCAX::Render::SRenderInstanceData& Item_)
        {
            return Item_.nObjectID == nObjectID_;
        });
        if (_Instances.size() == _OriginalSize)
        {
            return std::nullopt;
        }

        if (!RenderService_.SetInstances(ProjectID_, nSceneID_, _Instances, _Snapshot.Styles, NextRenderVersion()))
        {
            throw std::runtime_error("RenderService rejected render instance removal");
        }

        const auto _StillReferenced = std::any_of(
            _Instances.begin(),
            _Instances.end(),
            [_RemovedGeometryID](IN const iCAX::Render::SRenderInstanceData& Item_)
            {
                return Item_.nGeometryID == *_RemovedGeometryID;
            });
        if (!_StillReferenced)
        {
            (void)RenderService_.RemoveGeometry(ProjectID_, nSceneID_, *_RemovedGeometryID);
        }
        return _RemovedGeometryID;
    }

    class CRenderInstanceBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CRenderInstanceBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 1000;
            return _Schedule;
        }

    protected:
        void OnAwake(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnStart(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnUpdate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double&,
            IN const double&) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnEnable(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, true);
        }

        void OnDisable(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            Publish(Component_, ProjectContext_, SceneContext_, false);
        }

        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            auto _pEntity = Component_.GetEntity();
            if (_pEntity)
            {
                RemoveByEntityID(_pEntity->GetID(), ProjectContext_, SceneContext_);
            }
        }

        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            RemoveByEntityID(DestroyInfo_.EntityID, ProjectContext_, SceneContext_);
        }

    private:
        void RemoveByEntityID(
            IN const iCAX::Data::uuid& EntityID_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_)
        {
            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
            if (const auto _ObjectID = SceneContext_.Objects().FindObjectByEntity(EntityID_))
            {
                (void)RemoveInstance(*_pRenderService, ProjectContext_.GetProjectID(), _RenderSceneID, *_ObjectID);
            }
        }

        void Publish(
            IN iCAX::Database::CComponentBase& Component_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN bool bEnabled_)
        {
            auto& _Component = static_cast<iCAX::RenderInteraction::CRenderInstanceComponent&>(Component_);
            if (_Component.GetGeometryResourceID().empty())
            {
                return;
            }

            auto _pEntity = _Component.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("RenderInstanceComponent is detached from entity");
            }
            if (!_pEntity->GetComponent<iCAX::RenderInteraction::CRenderTransformComponent>())
            {
                (void)_pEntity->AddComponent<iCAX::RenderInteraction::CRenderTransformComponent>();
            }

            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
            EnsureRenderScene(*_pRenderService, _ProjectID, _RenderSceneID);

            const auto _ObjectID = static_cast<iCAX::Render::SceneObjectID>(
                SceneContext_.Objects().GetOrCreateEntityObject(_pEntity->GetID()));
            const auto _GeometryID = static_cast<iCAX::Render::RenderGeometryID>(
                SceneContext_.Objects().GetOrCreateGeometry(_Component.GetGeometryResourceID(), "render.geometry"));
            const auto _GeometryKind = ToGeometryKind(_Component.GetGeometryKind());
            UpsertGeometryResource(
                *_pRenderService,
                SceneContext_.Resources(),
                _ProjectID,
                _RenderSceneID,
                _Component.GetGeometryResourceID(),
                _GeometryID,
                _GeometryKind);

            const auto _Instance = MakeInstance(_Component, _ObjectID, _GeometryID, bEnabled_);
            const auto _Style = MakeStyle(_Component, bEnabled_);
            (void)UpsertInstanceAndStyle(*_pRenderService, _ProjectID, _RenderSceneID, _Instance, _Style, _Component.Version());
        }
    };

}
