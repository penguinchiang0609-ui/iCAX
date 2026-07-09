#include "pch.h"
#include "CameraNavigation.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "InputService/InputService.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderInteraction/RenderInteraction.h"
#include "Services/ServiceProvider.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr double kMinPitch = -1.483529806;
    constexpr double kMaxPitch = 1.483529806;
    constexpr double kEpsilon = 0.000001;

    struct Vec3 final
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    Vec3 Add(IN const Vec3& A_, IN const Vec3& B_) noexcept
    {
        return { A_.x + B_.x, A_.y + B_.y, A_.z + B_.z };
    }

    Vec3 Sub(IN const Vec3& A_, IN const Vec3& B_) noexcept
    {
        return { A_.x - B_.x, A_.y - B_.y, A_.z - B_.z };
    }

    Vec3 Scale(IN const Vec3& Value_, IN const double nScale_) noexcept
    {
        return { Value_.x * nScale_, Value_.y * nScale_, Value_.z * nScale_ };
    }

    Vec3 Cross(IN const Vec3& A_, IN const Vec3& B_) noexcept
    {
        return {
            A_.y * B_.z - A_.z * B_.y,
            A_.z * B_.x - A_.x * B_.z,
            A_.x * B_.y - A_.y * B_.x
        };
    }

    double Length(IN const Vec3& Value_) noexcept
    {
        return std::sqrt(Value_.x * Value_.x + Value_.y * Value_.y + Value_.z * Value_.z);
    }

    Vec3 NormalizeOr(IN const Vec3& Value_, IN const Vec3& Fallback_) noexcept
    {
        const auto _Length = Length(Value_);
        if (_Length <= kEpsilon)
        {
            return Fallback_;
        }
        return { Value_.x / _Length, Value_.y / _Length, Value_.z / _Length };
    }

    Vec3 ForwardFromYawPitch(IN const double nYaw_, IN const double nPitch_) noexcept
    {
        const auto _CosPitch = std::cos(nPitch_);
        return {
            _CosPitch * std::cos(nYaw_),
            _CosPitch * std::sin(nYaw_),
            std::sin(nPitch_)
        };
    }

    bool IsDown(
        IN const iCAX::Input::IInputService& Input_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN iCAX::InputPDO::InputViewportID nViewportID_,
        IN iCAX::InputPDO::EInputKeyCode eKey_)
    {
        return Input_.GetKey(ProjectID_, SceneID_, nViewportID_, static_cast<iCAX::InputPDO::InputKeyCode>(eKey_));
    }

    bool IsEitherDown(
        IN const iCAX::Input::IInputService& Input_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN iCAX::InputPDO::InputViewportID nViewportID_,
        IN iCAX::InputPDO::EInputKeyCode eFirstKey_,
        IN iCAX::InputPDO::EInputKeyCode eSecondKey_)
    {
        return IsDown(Input_, ProjectID_, SceneID_, nViewportID_, eFirstKey_)
            || IsDown(Input_, ProjectID_, SceneID_, nViewportID_, eSecondKey_);
    }

    iCAX::InputPDO::InputViewportID ResolveViewportID(
        IN const iCAX::CameraNavigation::CCameraNavigationComponent& Navigation_,
        IN const iCAX::RenderInteraction::CCameraComponent& Camera_) noexcept
    {
        const auto _ExplicitID = static_cast<iCAX::InputPDO::InputViewportID>(Navigation_.GetViewportID());
        if (_ExplicitID != iCAX::InputPDO::kInvalidInputViewportID)
        {
            return _ExplicitID;
        }
        return static_cast<iCAX::InputPDO::InputViewportID>(Camera_.GetViewportID());
    }

    bool ApplyNavigationInput(
        IN const iCAX::Input::IInputService& InputService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN iCAX::InputPDO::InputViewportID nViewportID_,
        IN const iCAX::CameraNavigation::CCameraNavigationComponent& Navigation_,
        IN const double nDeltaTime_,
        IN OUT iCAX::RenderInteraction::CRenderTransformComponent& Transform_)
    {
        const auto _InputFrame = InputService_.GetFrameSnapshot(ProjectID_, SceneID_, nViewportID_);
        if (!_InputFrame.bHasState)
        {
            return false;
        }

        auto _Position = Vec3{ Transform_.GetPositionX(), Transform_.GetPositionY(), Transform_.GetPositionZ() };
        auto _Yaw = Transform_.GetYawRadians();
        auto _Pitch = Transform_.GetPitchRadians();
        const auto _Roll = Transform_.GetRollRadians();

        const bool _bFast = ((_InputFrame.nModifierMask & iCAX::InputPDO::kInputModifierShift) != 0)
            || IsDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::Shift);
        const auto _Speed = Navigation_.GetMoveSpeed()
            * (_bFast ? Navigation_.GetFastMultiplier() : 1.0)
            * (std::max)(0.0, nDeltaTime_);

        bool _bChanged = false;
        Vec3 _Move;
        const auto _Forward = NormalizeOr(ForwardFromYawPitch(_Yaw, _Pitch), { 1.0, 0.0, 0.0 });
        const auto _Right = NormalizeOr(Cross(_Forward, Vec3{ 0.0, 0.0, 1.0 }), { 1.0, 0.0, 0.0 });

        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyW, iCAX::InputPDO::EInputKeyCode::ArrowUp))
        {
            _Move = Add(_Move, _Forward);
        }
        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyS, iCAX::InputPDO::EInputKeyCode::ArrowDown))
        {
            _Move = Sub(_Move, _Forward);
        }
        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyD, iCAX::InputPDO::EInputKeyCode::ArrowRight))
        {
            _Move = Add(_Move, _Right);
        }
        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyA, iCAX::InputPDO::EInputKeyCode::ArrowLeft))
        {
            _Move = Sub(_Move, _Right);
        }
        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyE, iCAX::InputPDO::EInputKeyCode::PageUp))
        {
            _Move.z += 1.0;
        }
        if (IsEitherDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::KeyQ, iCAX::InputPDO::EInputKeyCode::PageDown))
        {
            _Move.z -= 1.0;
        }

        if (Length(_Move) > kEpsilon)
        {
            _Position = Add(_Position, Scale(NormalizeOr(_Move, {}), _Speed));
            _bChanged = true;
        }

        const auto& _Pointer = _InputFrame.Pointer;
        const bool _bRotate = (_Pointer.nButtonMask & iCAX::InputPDO::kInputMouseButtonRight) != 0;
        if (_bRotate && (std::abs(_Pointer.nDeltaX) > kEpsilon || std::abs(_Pointer.nDeltaY) > kEpsilon))
        {
            const auto _Sensitivity = Navigation_.GetMouseSensitivity();
            _Yaw += _Pointer.nDeltaX * _Sensitivity;
            _Pitch = std::clamp(_Pitch - _Pointer.nDeltaY * _Sensitivity, kMinPitch, kMaxPitch);
            _bChanged = true;
        }

        if (std::abs(_Pointer.nWheelY) > kEpsilon)
        {
            _Position = Add(_Position, Scale(_Forward, -_Pointer.nWheelY * _Speed * Navigation_.GetWheelSpeedScale()));
            _bChanged = true;
        }

        if (!_bChanged)
        {
            return false;
        }

        iCAX::Data::PropertySet _Properties;
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionX] =
            iCAX::Data::PropertyValue(_Position.x);
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionY] =
            iCAX::Data::PropertyValue(_Position.y);
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionZ] =
            iCAX::Data::PropertyValue(_Position.z);
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_YawRadians] =
            iCAX::Data::PropertyValue(_Yaw);
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PitchRadians] =
            iCAX::Data::PropertyValue(_Pitch);
        _Properties[iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_RollRadians] =
            iCAX::Data::PropertyValue(_Roll);

        std::string _strError;
        if (!Transform_.SetProperties(_Properties, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Camera navigation cannot update transform component" : _strError);
        }
        return true;
    }

    class CCameraNavigationBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CCameraNavigationBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::CameraNavigation::CCameraNavigationComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 1000;
            return _Schedule;
        }

    protected:
        void OnUpdate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double& nDeltaTime_,
            IN const double&) override
        {
            auto& _Navigation = static_cast<iCAX::CameraNavigation::CCameraNavigationComponent&>(Component_);
            if (!_Navigation.GetEnabled())
            {
                return;
            }

            auto _pEntity = _Navigation.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("CameraNavigationComponent is detached from entity");
            }

            auto _pCamera = _pEntity->GetComponent<iCAX::RenderInteraction::CCameraComponent>();
            if (!_pCamera)
            {
                throw std::runtime_error("CameraNavigationComponent requires CCameraComponent on the same entity");
            }

            auto _pTransform = _pEntity->GetComponent<iCAX::RenderInteraction::CRenderTransformComponent>();
            if (!_pTransform)
            {
                _pTransform = _pEntity->AddComponent<iCAX::RenderInteraction::CRenderTransformComponent>();
            }

            auto _pInputService = SceneContext_.Services().Resolve<iCAX::Input::IInputService>();
            _pInputService->UpdateFromPDO(ProjectContext_, SceneContext_);

            (void)ApplyNavigationInput(
                *_pInputService,
                ProjectContext_.GetProjectID(),
                SceneContext_.GetSceneID(),
                ResolveViewportID(_Navigation, *_pCamera),
                _Navigation,
                nDeltaTime_,
                *_pTransform);
        }
    };
}

uint32_t iCAX::CameraNavigation::GetCameraNavigationContractVersion() noexcept
{
    return 1;
}

