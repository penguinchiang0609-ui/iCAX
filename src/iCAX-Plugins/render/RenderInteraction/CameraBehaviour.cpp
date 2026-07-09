#include "pch.h"
#include "RenderInteraction.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/SceneObjectRegistry.h"
#include "RenderData/RenderDataTypes.h"
#include "RenderService/RenderService.h"
#include "Services/ServiceProvider.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
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

    iCAX::Render::SRenderCameraData MakeCameraData(
        IN const iCAX::RenderInteraction::CCameraComponent& Camera_,
        IN const iCAX::Render::RenderCameraID nCameraID_) noexcept
    {
        iCAX::Render::SRenderCameraData _Data;
        _Data.nCameraID = nCameraID_;
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
        if (bActive_ || _ActiveCameraID == iCAX::Render::kInvalidRenderCameraID)
        {
            if (_ActiveCameraID != Camera_.nCameraID)
            {
                _ActiveCameraID = Camera_.nCameraID;
                _bChanged = true;
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

        if (!RenderService_.SetCameras(ProjectID_, nRenderSceneID_, _Cameras, _ActiveCameraID, MakeNonZeroVersion(nCameraVersion_)))
        {
            throw std::runtime_error("Camera behaviour cannot publish camera component to render service");
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
        void OnUpdate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double&,
            IN const double&) override
        {
            auto& _Camera = static_cast<iCAX::RenderInteraction::CCameraComponent&>(Component_);
            if (!_Camera.GetEnabled())
            {
                return;
            }

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

            const auto _ObjectID = static_cast<iCAX::Render::SceneObjectID>(
                SceneContext_.Objects().GetOrCreateEntityObject(_pEntity->GetID()));
            const auto _TransformID = static_cast<iCAX::Render::TransformID>(
                SceneContext_.Objects().GetTransformID(_ObjectID));
            const auto _CameraID = static_cast<iCAX::Render::RenderCameraID>(_TransformID);

            auto _Snapshot = _pRenderService->GetSceneSnapshot(_ProjectID, _RenderSceneID);
            const auto _CameraData = MakeCameraData(_Camera, _CameraID);
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
