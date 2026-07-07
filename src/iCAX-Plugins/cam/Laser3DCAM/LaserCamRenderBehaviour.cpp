#include "pch.h"
#include "ToolpathComponents.h"
#include "ToolpathResourceKeys.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Data/uuid.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderData/RenderDataTypes.h"
#include "RenderService/RenderService.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServiceProvider.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<TComponent>(pEntity_->GetComponent(TComponent::S_ClassName));
    }

    template <typename TComponent>
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _CollectEntitiesWithComponent(
        IN iCAX::Database::IRepository& Repository_)
    {
        std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
        auto _Entities = Repository_.FilterEntities([](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
            return pEntity_ && pEntity_->HasComponent(TComponent::S_ClassName);
        });

        _Result.reserve(_Entities.size());
        for (auto& _pEntity : _Entities)
        {
            auto _pComponent = _GetComponent<TComponent>(_pEntity);
            if (_pComponent)
            {
                _Result.emplace_back(_pEntity, _pComponent);
            }
        }
        return _Result;
    }

    uint64_t _MakeStableRenderID(IN const std::string& strText_) noexcept
    {
        uint64_t _Hash = 14695981039346656037ull;
        for (const auto _Char : strText_)
        {
            _Hash ^= static_cast<unsigned char>(_Char);
            _Hash *= 1099511628211ull;
        }
        return _Hash == 0 ? 1 : _Hash;
    }

    iCAX::Render::RenderSceneID _MakeRenderSceneID(IN const iCAX::Data::uuid& SceneID_) noexcept
    {
        return static_cast<iCAX::Render::RenderSceneID>(_MakeStableRenderID("scene:" + iCAX::Data::to_string(SceneID_)));
    }

    iCAX::Render::RenderGeometryID _MakeRenderGeometryID(IN const std::string& strDisplayResourceID_) noexcept
    {
        return static_cast<iCAX::Render::RenderGeometryID>(_MakeStableRenderID("mesh:" + strDisplayResourceID_));
    }

    iCAX::Render::RenderObjectID _MakeRenderObjectID(IN const iCAX::Data::uuid& EntityID_) noexcept
    {
        return static_cast<iCAX::Render::RenderObjectID>(_MakeStableRenderID("object:" + iCAX::Data::to_string(EntityID_)));
    }

    iCAX::Render::RenderCameraID _MakeDefaultRenderCameraID() noexcept
    {
        return static_cast<iCAX::Render::RenderCameraID>(_MakeStableRenderID("camera:laser-3d-cam.default"));
    }

    uint64_t _NextRenderDataVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    iCAX::Render::SFloat3 _ToRenderFloat3(IN const iCAX::GeometryData::Point3& Point_) noexcept
    {
        return {
            static_cast<float>(Point_.X),
            static_cast<float>(Point_.Y),
            static_cast<float>(Point_.Z)
        };
    }

    iCAX::Render::SFloat3 _ToRenderFloat3(IN const iCAX::GeometryData::Vector3& Vector_) noexcept
    {
        return {
            static_cast<float>(Vector_.X),
            static_cast<float>(Vector_.Y),
            static_cast<float>(Vector_.Z)
        };
    }

    iCAX::Render::SFloat3 _Sub(
        IN const iCAX::Render::SFloat3& Left_,
        IN const iCAX::Render::SFloat3& Right_) noexcept
    {
        return { Left_.x - Right_.x, Left_.y - Right_.y, Left_.z - Right_.z };
    }

    iCAX::Render::SFloat3 _Cross(
        IN const iCAX::Render::SFloat3& Left_,
        IN const iCAX::Render::SFloat3& Right_) noexcept
    {
        return {
            Left_.y * Right_.z - Left_.z * Right_.y,
            Left_.z * Right_.x - Left_.x * Right_.z,
            Left_.x * Right_.y - Left_.y * Right_.x
        };
    }

    float _Length(IN const iCAX::Render::SFloat3& Value_) noexcept
    {
        return std::sqrt(Value_.x * Value_.x + Value_.y * Value_.y + Value_.z * Value_.z);
    }

    iCAX::Render::SFloat3 _NormalizeOr(
        IN const iCAX::Render::SFloat3& Value_,
        IN const iCAX::Render::SFloat3& Fallback_) noexcept
    {
        const auto _LengthValue = _Length(Value_);
        if (_LengthValue <= 0.000001f)
        {
            return Fallback_;
        }
        return { Value_.x / _LengthValue, Value_.y / _LengthValue, Value_.z / _LengthValue };
    }

    iCAX::Render::SMatrix4 _MakeCameraLocalToWorld(
        IN const iCAX::Render::SFloat3& CameraPosition_,
        IN const iCAX::Render::SFloat3& LookAtPoint_,
        IN const iCAX::Render::SFloat3& Up_) noexcept
    {
        const auto _Forward = _NormalizeOr(_Sub(LookAtPoint_, CameraPosition_), { 0.0f, 0.0f, -1.0f });
        const auto _Back = iCAX::Render::SFloat3{ -_Forward.x, -_Forward.y, -_Forward.z };
        const auto _Right = _NormalizeOr(_Cross(Up_, _Back), { 1.0f, 0.0f, 0.0f });
        const auto _Up = _NormalizeOr(_Cross(_Back, _Right), { 0.0f, 0.0f, 1.0f });

        auto _Matrix = iCAX::Render::SMatrix4::Identity();
        _Matrix.Values[0] = _Right.x;
        _Matrix.Values[1] = _Right.y;
        _Matrix.Values[2] = _Right.z;
        _Matrix.Values[4] = _Up.x;
        _Matrix.Values[5] = _Up.y;
        _Matrix.Values[6] = _Up.z;
        _Matrix.Values[8] = _Back.x;
        _Matrix.Values[9] = _Back.y;
        _Matrix.Values[10] = _Back.z;
        _Matrix.Values[12] = CameraPosition_.x;
        _Matrix.Values[13] = CameraPosition_.y;
        _Matrix.Values[14] = CameraPosition_.z;
        return _Matrix;
    }

    void _AddBoundsPoint(
        IN OUT iCAX::Render::SFloat3& Min_,
        IN OUT iCAX::Render::SFloat3& Max_,
        IN const iCAX::Render::SFloat3& Point_,
        IN OUT bool& bEmpty_) noexcept
    {
        if (bEmpty_)
        {
            Min_ = Point_;
            Max_ = Point_;
            bEmpty_ = false;
            return;
        }

        Min_.x = std::min(Min_.x, Point_.x);
        Min_.y = std::min(Min_.y, Point_.y);
        Min_.z = std::min(Min_.z, Point_.z);
        Max_.x = std::max(Max_.x, Point_.x);
        Max_.y = std::max(Max_.y, Point_.y);
        Max_.z = std::max(Max_.z, Point_.z);
    }

    void _MergeBounds(
        IN OUT iCAX::Render::SFloat3& Min_,
        IN OUT iCAX::Render::SFloat3& Max_,
        IN const iCAX::Render::SRenderAABB& Bounds_,
        IN OUT bool& bEmpty_) noexcept
    {
        _AddBoundsPoint(Min_, Max_, Bounds_.Min, bEmpty_);
        _AddBoundsPoint(Min_, Max_, Bounds_.Max, bEmpty_);
    }

    iCAX::Render::SRenderMeshData _MakeRenderMeshFromBRep(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const std::string& strDisplayResourceID_,
        IN uint64_t nDataVersion_)
    {
        if (strDisplayResourceID_.empty())
        {
            throw std::runtime_error("CAM display resource id cannot be empty");
        }

        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nGeometryID = _MakeRenderGeometryID(strDisplayResourceID_);
        _Mesh.nDataVersion = std::max<uint64_t>(1, nDataVersion_);
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;

        iCAX::Render::SFloat3 _Min;
        iCAX::Render::SFloat3 _Max;
        bool _bBoundsEmpty = true;
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
                throw std::runtime_error("CAM display mesh has too many vertices for uint32 indices");
            }

            const auto _BaseVertex = static_cast<uint32_t>(_Mesh.Positions.size());
            for (const auto& _Point : _Geometry.Vertices)
            {
                const auto _RenderPoint = _ToRenderFloat3(_Point);
                _Mesh.Positions.push_back(_RenderPoint);
                _AddBoundsPoint(_Min, _Max, _RenderPoint, _bBoundsEmpty);
            }

            if (_Geometry.Normals.size() == _Geometry.Vertices.size())
            {
                _bAnyTriangulationHasNormals = true;
                for (const auto& _Normal : _Geometry.Normals)
                {
                    _Mesh.Normals.push_back(_ToRenderFloat3(_Normal));
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
                        throw std::runtime_error("CAM display mesh triangulation index is out of range");
                    }
                    _Mesh.Indices.push_back(_BaseVertex + _Index);
                }
            }
        }

        if (_Mesh.Positions.empty() || _Mesh.Indices.empty())
        {
            throw std::runtime_error("CAD model does not have display triangulation");
        }

        if (_bAnyTriangulationHasNormals && _bAllTriangulationsHaveNormals && _Mesh.Normals.size() == _Mesh.Positions.size())
        {
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasNormals;
        }
        else
        {
            _Mesh.Normals.clear();
        }

        _Mesh.Bounds.Min = _Min;
        _Mesh.Bounds.Max = _Max;
        return _Mesh;
    }

    iCAX::Render::SRenderStyleData _MakeDefaultModelStyle() noexcept
    {
        iCAX::Render::SRenderStyleData _Style;
        _Style.nStyleID = 1;
        _Style.nColorRGBA = 0x8FB8C9FFu;
        _Style.nLineWidth = 1.0f;
        _Style.nFlags = iCAX::Render::kRenderFlagVisible | iCAX::Render::kRenderFlagSelectable;
        return _Style;
    }

    iCAX::Render::SRenderInstanceData _MakeWorkpieceRenderInstance(
        IN const iCAX::Data::uuid& WorkpieceEntityID_,
        IN const std::string& strDisplayResourceID_) noexcept
    {
        iCAX::Render::SRenderInstanceData _Instance;
        _Instance.nObjectID = _MakeRenderObjectID(WorkpieceEntityID_);
        _Instance.nGeometryID = _MakeRenderGeometryID(strDisplayResourceID_);
        _Instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
        _Instance.eRenderClass = iCAX::Render::ERenderClass::Model;
        _Instance.nStyleID = 1;
        _Instance.nFlags = iCAX::Render::kRenderFlagVisible | iCAX::Render::kRenderFlagSelectable;
        return _Instance;
    }

    iCAX::Render::SRenderTransformData _MakeIdentityTransform(
        IN iCAX::Render::RenderTransformID nTransformID_,
        IN iCAX::Render::RenderDataVersion nVersion_) noexcept
    {
        iCAX::Render::SRenderTransformData _Transform;
        _Transform.nTransformID = nTransformID_;
        _Transform.nDataVersion = nVersion_;
        _Transform.LocalToWorld = iCAX::Render::SMatrix4::Identity();
        return _Transform;
    }

    iCAX::Render::SRenderCameraData _MakeDefaultCamera(
        IN iCAX::Render::RenderCameraID nCameraID_,
        IN const iCAX::Render::SRenderAABB& Bounds_) noexcept
    {
        const auto _SizeX = std::max(1.0f, Bounds_.Max.x - Bounds_.Min.x);
        const auto _SizeY = std::max(1.0f, Bounds_.Max.y - Bounds_.Min.y);
        const auto _SizeZ = std::max(1.0f, Bounds_.Max.z - Bounds_.Min.z);
        const auto _Radius = std::max({ _SizeX, _SizeY, _SizeZ }) * 1.8f;

        iCAX::Render::SRenderCameraData _Camera;
        _Camera.nCameraID = nCameraID_;
        _Camera.nFlags = iCAX::Render::kRenderCameraFlagPerspective;
        _Camera.nNearPlane = 0.1f;
        _Camera.nFarPlane = std::max(100000.0f, _Radius * 20.0f);
        _Camera.nVerticalFovRadians = 0.785398185f;
        return _Camera;
    }

    iCAX::Render::SRenderTransformData _MakeDefaultCameraTransform(
        IN iCAX::Render::RenderCameraID nCameraID_,
        IN const iCAX::Render::SRenderAABB& Bounds_,
        IN iCAX::Render::RenderDataVersion nVersion_) noexcept
    {
        const auto _CenterX = (Bounds_.Min.x + Bounds_.Max.x) * 0.5f;
        const auto _CenterY = (Bounds_.Min.y + Bounds_.Max.y) * 0.5f;
        const auto _CenterZ = (Bounds_.Min.z + Bounds_.Max.z) * 0.5f;
        const auto _SizeX = std::max(1.0f, Bounds_.Max.x - Bounds_.Min.x);
        const auto _SizeY = std::max(1.0f, Bounds_.Max.y - Bounds_.Min.y);
        const auto _SizeZ = std::max(1.0f, Bounds_.Max.z - Bounds_.Min.z);
        const auto _Radius = std::max({ _SizeX, _SizeY, _SizeZ }) * 1.8f;

        const iCAX::Render::SFloat3 _CameraPosition{ _CenterX + _Radius, _CenterY - _Radius, _CenterZ + _Radius * 0.75f };
        const iCAX::Render::SFloat3 _LookAtPoint{ _CenterX, _CenterY, _CenterZ };
        const iCAX::Render::SFloat3 _Up{ 0.0f, 0.0f, 1.0f };

        iCAX::Render::SRenderTransformData _Transform;
        _Transform.nTransformID = nCameraID_;
        _Transform.nDataVersion = nVersion_;
        _Transform.LocalToWorld = _MakeCameraLocalToWorld(_CameraPosition, _LookAtPoint, _Up);
        return _Transform;
    }

    struct SWorkpieceRenderState final
    {
        std::string BRepResourceID;
        std::string DisplayResourceID;
        uint64_t nBRepVersion = 0;
        iCAX::Render::RenderGeometryID nGeometryID = iCAX::Render::kInvalidRenderGeometryID;
        iCAX::Render::SRenderAABB Bounds;
    };

    std::string _ResolveDisplayResourceID(IN const iCAX::CAM::CLaserWorkpieceComponent& Workpiece_)
    {
        if (!Workpiece_.GetDisplayResourceID().empty())
        {
            return Workpiece_.GetDisplayResourceID();
        }
        return iCAX::CAM::MakeCAMDisplayResourceID(Workpiece_.GetModelResourceID());
    }

    class CLaserCamRenderBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CLaserCamRenderBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::CAM::CLaserCamRootComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 1000;
            return _Schedule;
        }

    protected:
        void OnUpdate(
            IN iCAX::Database::CComponentBase&,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN const iCAX::Product::IProductContext& ProductContext_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_) override
        {
            PublishScene(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
        }

    private:
        void PublishScene(
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN const iCAX::Product::IProductContext& ProductContext_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_)
        {
            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto _RenderSceneID = _MakeRenderSceneID(SceneContext_.GetSceneID());

            if (!_pRenderService->HasScene(_ProjectID, _RenderSceneID))
            {
                (void)_pRenderService->CreateScene(_ProjectID, _RenderSceneID);
                m_bSceneDirty = true;
            }

            auto& _Repository = SceneContext_.Database();
            auto _Workpieces = _CollectEntitiesWithComponent<iCAX::CAM::CLaserWorkpieceComponent>(_Repository);
            std::unordered_set<std::string> _SeenKeys;
            bool _bDirty = m_bSceneDirty;

            iCAX::Render::SFloat3 _SceneMin;
            iCAX::Render::SFloat3 _SceneMax;
            bool _bSceneBoundsEmpty = true;
            std::vector<iCAX::Render::SRenderInstanceData> _Instances;
            std::vector<iCAX::Render::SRenderTransformData> _Transforms;
            _Instances.reserve(_Workpieces.size());
            _Transforms.reserve(_Workpieces.size() + 1);

            for (const auto& [_pEntity, _pWorkpiece] : _Workpieces)
            {
                if (!_pEntity || !_pWorkpiece || _pWorkpiece->GetBRepResourceID().empty())
                {
                    continue;
                }

                const auto _DisplayResourceID = _ResolveDisplayResourceID(*_pWorkpiece);
                if (_DisplayResourceID.empty())
                {
                    continue;
                }

                const auto _Key = iCAX::Data::to_string(_pEntity->GetID());
                const auto _BRepVersion = SceneContext_.Resources().GetVersion(_pWorkpiece->GetBRepResourceID());
                if (_BRepVersion == 0)
                {
                    continue;
                }

                auto& _State = m_Workpieces[_Key];
                const bool _bGeometryChanged =
                    _State.BRepResourceID != _pWorkpiece->GetBRepResourceID()
                    || _State.DisplayResourceID != _DisplayResourceID
                    || _State.nBRepVersion != _BRepVersion;

                if (_bGeometryChanged)
                {
                    auto _pBRep = SceneContext_.Resources().Get<iCAX::GeometryData::BRepModel>(_pWorkpiece->GetBRepResourceID());
                    if (!_pBRep)
                    {
                        throw std::runtime_error("CAM workpiece BRep resource has unexpected runtime type: " + _pWorkpiece->GetBRepResourceID());
                    }

                    auto _Mesh = _MakeRenderMeshFromBRep(*_pBRep, _DisplayResourceID, _NextRenderDataVersion());
                    if (!_pRenderService->UpsertMesh(_ProjectID, _RenderSceneID, _Mesh))
                    {
                        throw std::runtime_error("Render scene is not available for CAM workpiece mesh");
                    }

                    if (_State.nGeometryID != iCAX::Render::kInvalidRenderGeometryID && _State.nGeometryID != _Mesh.nGeometryID)
                    {
                        (void)_pRenderService->RemoveGeometry(_ProjectID, _RenderSceneID, _State.nGeometryID);
                    }

                    _State.BRepResourceID = _pWorkpiece->GetBRepResourceID();
                    _State.DisplayResourceID = _DisplayResourceID;
                    _State.nBRepVersion = _BRepVersion;
                    _State.nGeometryID = _Mesh.nGeometryID;
                    _State.Bounds = _Mesh.Bounds;
                    _bDirty = true;
                }

                _SeenKeys.insert(_Key);
                _MergeBounds(_SceneMin, _SceneMax, _State.Bounds, _bSceneBoundsEmpty);
                const auto _Instance = _MakeWorkpieceRenderInstance(_pEntity->GetID(), _DisplayResourceID);
                _Instances.push_back(_Instance);
                _Transforms.push_back(_MakeIdentityTransform(_Instance.nObjectID, _NextRenderDataVersion()));
            }

            for (auto _Ite = m_Workpieces.begin(); _Ite != m_Workpieces.end();)
            {
                if (_SeenKeys.contains(_Ite->first))
                {
                    ++_Ite;
                    continue;
                }

                if (_Ite->second.nGeometryID != iCAX::Render::kInvalidRenderGeometryID)
                {
                    (void)_pRenderService->RemoveGeometry(_ProjectID, _RenderSceneID, _Ite->second.nGeometryID);
                }
                _Ite = m_Workpieces.erase(_Ite);
                _bDirty = true;
            }

            if (_bDirty)
            {
                const auto _SceneVersion = _NextRenderDataVersion();
                const std::vector<iCAX::Render::SRenderStyleData> _Styles{ _MakeDefaultModelStyle() };
                if (!_pRenderService->SetInstances(_ProjectID, _RenderSceneID, _Instances, _Styles, _SceneVersion))
                {
                    throw std::runtime_error("Render scene is not available for CAM workpiece instances");
                }

                if (!_bSceneBoundsEmpty)
                {
                    iCAX::Render::SRenderAABB _Bounds;
                    _Bounds.Min = _SceneMin;
                    _Bounds.Max = _SceneMax;
                    const auto _CameraID = _MakeDefaultRenderCameraID();
                    _Transforms.push_back(_MakeDefaultCameraTransform(_CameraID, _Bounds, _SceneVersion));
                    if (!_pRenderService->SetTransforms(_ProjectID, _RenderSceneID, _Transforms, _SceneVersion))
                    {
                        throw std::runtime_error("Render scene is not available for CAM camera transform");
                    }

                    const std::vector<iCAX::Render::SRenderCameraData> _Cameras{ _MakeDefaultCamera(_CameraID, _Bounds) };
                    if (!_pRenderService->SetCameras(_ProjectID, _RenderSceneID, _Cameras, _CameraID, _SceneVersion))
                    {
                        throw std::runtime_error("Render scene is not available for CAM camera");
                    }
                }
                else if (!_pRenderService->SetTransforms(_ProjectID, _RenderSceneID, _Transforms, _SceneVersion))
                {
                    throw std::runtime_error("Render scene is not available for CAM workpiece transforms");
                }

                _pRenderService->Update(ApplicationContext_, ProductContext_, ProjectContext_, SceneContext_, nDeltaTime_, nTotalTime_);
                m_bSceneDirty = false;
            }
        }

    private:
        std::unordered_map<std::string, SWorkpieceRenderState> m_Workpieces;
        bool m_bSceneDirty = false;
    };
}
