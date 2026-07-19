#include "pch.h"
#include "RenderInteraction.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderData/RenderDataTypes.h"
#include "RenderService/RenderService.h"
#include "Services/ServiceProvider.h"
#include "Transform/Transform.h"


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

    iCAX::Render::SRenderCameraData MakeCameraData(
        IN const iCAX::RenderInteraction::CCameraComponent& Camera_,
        IN const iCAX::Render::RenderCameraID nCameraID_,
        IN const uint64_t nCameraVersion_) noexcept
    {
        iCAX::Render::SRenderCameraData _Data;
        _Data.nCameraID = nCameraID_;
        _Data.nDataVersion = MakeNonZeroVersion(nCameraVersion_);
        _Data.nFlags = iCAX::Render::kRenderCameraFlagPerspective;
        _Data.nNearPlane = static_cast<float>(Camera_.GetNearPlane());
        _Data.nFarPlane = static_cast<float>(Camera_.GetFarPlane());
        _Data.nVerticalFovRadians = static_cast<float>(Camera_.GetVerticalFovRadians());
        _Data.nOrthographicHeight = static_cast<float>(Camera_.GetOrthographicHeight());
        return _Data;
    }

    bool SameCameraData(
        IN const iCAX::Render::SRenderCameraData& Left_,
        IN const iCAX::Render::SRenderCameraData& Right_) noexcept
    {
        return Left_.nCameraID == Right_.nCameraID
            && Left_.nFlags == Right_.nFlags
            && std::abs(Left_.nNearPlane - Right_.nNearPlane) <= static_cast<float>(kEpsilon)
            && std::abs(Left_.nFarPlane - Right_.nFarPlane) <= static_cast<float>(kEpsilon)
            && std::abs(Left_.nVerticalFovRadians - Right_.nVerticalFovRadians) <= static_cast<float>(kEpsilon)
            && std::abs(Left_.nOrthographicHeight - Right_.nOrthographicHeight) <= static_cast<float>(kEpsilon);
    }

    bool UpsertCamera(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Render::RenderSceneID nRenderSceneID_,
        IN const iCAX::Render::SRenderCameraData& Camera_,
        IN const bool bActive_,
        IN const uint64_t nCameraVersion_,
        IN const iCAX::Render::SRenderSceneSnapshot& Snapshot_)
    {
        auto _Cameras = Snapshot_.Cameras;
        bool _bChanged = false;
        auto _Iter = std::find_if(
            _Cameras.begin(),
            _Cameras.end(),
            [&Camera_](IN const iCAX::Render::SRenderCameraData& Item_)
            {
                return Item_.nCameraID == Camera_.nCameraID;
            });

        if (_Iter == _Cameras.end())
        {
            _Cameras.push_back(Camera_);
            _bChanged = true;
        }
        else if (!SameCameraData(*_Iter, Camera_))
        {
            *_Iter = Camera_;
            _bChanged = true;
        }

        auto _ActiveCameraID = Snapshot_.nActiveCameraID;
        bool _bActiveChanged = false;
        if (bActive_ || _ActiveCameraID == iCAX::Render::kInvalidRenderCameraID)
        {
            if (_ActiveCameraID != Camera_.nCameraID)
            {
                _ActiveCameraID = Camera_.nCameraID;
                _bChanged = true;
                _bActiveChanged = true;
            }
        }

        if (!_bChanged)
        {
            return false;
        }

        if (_Cameras.empty())
        {
            _Cameras.push_back(Camera_);
            _ActiveCameraID = Camera_.nCameraID;
        }

        if (_bActiveChanged)
        {
            const auto _Version = MakeNonZeroVersion(nCameraVersion_);
            for (auto& _Camera : _Cameras)
            {
                _Camera.nDataVersion = _Version;
            }
        }

        if (!RenderService_.SetCameras(ProjectID_, nRenderSceneID_, _Cameras, _ActiveCameraID))
        {
            throw std::runtime_error("Camera behaviour cannot publish camera component to render service");
        }
        return true;
    }

    bool RemoveCamera(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nRenderSceneID_,
        IN iCAX::Render::RenderCameraID nCameraID_)
    {
        if (!RenderService_.HasScene(ProjectID_, nRenderSceneID_))
        {
            return false;
        }

        auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nRenderSceneID_);
        auto _Cameras = _Snapshot.Cameras;
        const auto _OriginalSize = _Cameras.size();
        std::erase_if(
            _Cameras,
            [nCameraID_](IN const iCAX::Render::SRenderCameraData& Camera_)
            {
                return Camera_.nCameraID == nCameraID_;
            });
        if (_Cameras.size() == _OriginalSize)
        {
            return false;
        }

        auto _ActiveCameraID = _Snapshot.nActiveCameraID;
        if (_ActiveCameraID == nCameraID_)
        {
            _ActiveCameraID = _Cameras.empty()
                ? iCAX::Render::kInvalidRenderCameraID
                : _Cameras.front().nCameraID;
            if (!_Cameras.empty())
            {
                _Cameras.front().nDataVersion = NextRenderVersion();
            }
        }
        if (!RenderService_.SetCameras(ProjectID_, nRenderSceneID_, _Cameras, _ActiveCameraID))
        {
            throw std::runtime_error("Camera behaviour cannot remove camera component from render service");
        }
        return true;
    }

    class CCameraBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CCameraBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::RenderInteraction::CCameraComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 2000;
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
            (void)RemoveCamera(*_pRenderService, ProjectContext_.GetProjectID(), _RenderSceneID, EntityID_);
        }

        void Publish(
            IN iCAX::Database::CComponentBase& Component_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN bool bLifecycleEnabled_)
        {
            auto& _Camera = static_cast<iCAX::RenderInteraction::CCameraComponent&>(Component_);
            auto _pEntity = _Camera.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("Camera component is detached from entity");
            }

            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
            if (!_pRenderService->HasScene(_ProjectID, _RenderSceneID))
            {
                (void)_pRenderService->CreateScene(_ProjectID, _RenderSceneID);
            }

            if (!_pEntity->GetComponent<iCAX::Transform::CTransformComponent>())
            {
                (void)_pEntity->AddComponent<iCAX::Transform::CTransformComponent>();
            }

            const auto _CameraID = _pEntity->GetID();

            if (!bLifecycleEnabled_ || !_Camera.GetEnabled())
            {
                (void)RemoveCamera(*_pRenderService, _ProjectID, _RenderSceneID, _CameraID);
                return;
            }

            auto _Snapshot = _pRenderService->GetSceneSnapshot(_ProjectID, _RenderSceneID);
            const auto _CameraData = MakeCameraData(_Camera, _CameraID, _Camera.Version());
            (void)UpsertCamera(
                *_pRenderService,
                _ProjectID,
                _RenderSceneID,
                _CameraData,
                _Camera.GetActive(),
                _Camera.Version(),
                _Snapshot);
        }
    };
}

uint32_t iCAX::RenderInteraction::GetRenderInteractionContractVersion() noexcept
{
    return 2;
}
