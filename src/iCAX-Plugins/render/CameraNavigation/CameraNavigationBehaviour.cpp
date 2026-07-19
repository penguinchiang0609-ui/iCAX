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
#include "Transform/Transform.h"


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

    struct PointerDelta final
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct WheelRuntimeState final
    {
        double nLastWheelTime = -1.0;
        double nMultiplier = 1.0;
        int nLastDirection = 0;
    };

    struct NavigationRuntimeKey final
    {
        iCAX::Data::uuid ProjectID;
        iCAX::Data::uuid SceneID;
        iCAX::Data::uuid EntityID;
        iCAX::InputPDO::InputViewportID nViewportID = iCAX::InputPDO::kInvalidInputViewportID;

        bool operator==(IN const NavigationRuntimeKey& Other_) const noexcept
        {
            return ProjectID == Other_.ProjectID
                && SceneID == Other_.SceneID
                && EntityID == Other_.EntityID
                && nViewportID == Other_.nViewportID;
        }
    };

    struct NavigationRuntimeKeyHasher final
    {
        size_t operator()(IN const NavigationRuntimeKey& Key_) const noexcept
        {
            auto _Hash = std::hash<iCAX::Data::uuid>{}(Key_.ProjectID);
            const auto _Mix = [](IN OUT size_t& Hash_, IN const size_t Value_) noexcept
            {
                Hash_ ^= Value_ + 0x9e3779b97f4a7c15ull + (Hash_ << 6) + (Hash_ >> 2);
            };
            _Mix(_Hash, std::hash<iCAX::Data::uuid>{}(Key_.SceneID));
            _Mix(_Hash, std::hash<iCAX::Data::uuid>{}(Key_.EntityID));
            _Mix(_Hash, std::hash<iCAX::InputPDO::InputViewportID>{}(Key_.nViewportID));
            return _Hash;
        }
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
            -std::sin(nPitch_)
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

    PointerDelta FilterOrbitDelta(
        IN const double nDeltaX_,
        IN const double nDeltaY_,
        IN const double nDeadZonePixels_,
        IN const double nAxisLockRatio_) noexcept
    {
        PointerDelta _Delta{ nDeltaX_, nDeltaY_ };
        const auto _DeadZone = (std::max)(0.0, nDeadZonePixels_);
        if (std::abs(_Delta.x) <= _DeadZone)
        {
            _Delta.x = 0.0;
        }
        if (std::abs(_Delta.y) <= _DeadZone)
        {
            _Delta.y = 0.0;
        }

        const auto _AbsX = std::abs(_Delta.x);
        const auto _AbsY = std::abs(_Delta.y);
        if (_AbsX <= kEpsilon || _AbsY <= kEpsilon)
        {
            return _Delta;
        }

        const auto _AxisLockRatio = (std::max)(1.0, nAxisLockRatio_);
        if (_AbsX >= _AbsY * _AxisLockRatio)
        {
            _Delta.y = 0.0;
        }
        else if (_AbsY >= _AbsX * _AxisLockRatio)
        {
            _Delta.x = 0.0;
        }
        return _Delta;
    }

    double ResolveWheelAccelerationMultiplier(
        IN const double nWheelY_,
        IN const double nTotalTime_,
        IN const iCAX::CameraNavigation::CCameraNavigationComponent& Navigation_,
        IN OUT WheelRuntimeState& State_) noexcept
    {
        if (std::abs(nWheelY_) <= kEpsilon)
        {
            return 1.0;
        }

        const int _Direction = nWheelY_ > 0.0 ? 1 : -1;
        const auto _ResetSeconds = (std::max)(0.02, Navigation_.GetWheelAccelerationResetSeconds());
        const auto _Gain = (std::max)(0.0, Navigation_.GetWheelAccelerationGain());
        const auto _MaxMultiplier = (std::max)(1.0, Navigation_.GetWheelAccelerationMaxMultiplier());
        const bool _bContinuous =
            State_.nLastDirection == _Direction
            && State_.nLastWheelTime >= 0.0
            && nTotalTime_ >= State_.nLastWheelTime
            && nTotalTime_ - State_.nLastWheelTime <= _ResetSeconds;

        if (!_bContinuous)
        {
            State_.nMultiplier = 1.0;
        }
        else
        {
            const auto _WheelStep = std::clamp(std::abs(nWheelY_) / 120.0, 0.25, 2.0);
            State_.nMultiplier = (std::min)(_MaxMultiplier, State_.nMultiplier + _Gain * _WheelStep);
        }

        State_.nLastDirection = _Direction;
        State_.nLastWheelTime = nTotalTime_;
        return State_.nMultiplier;
    }

    bool ApplyNavigationInput(
        IN const iCAX::Input::IInputService& InputService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN const iCAX::Data::uuid& SceneID_,
        IN iCAX::InputPDO::InputViewportID nViewportID_,
        IN OUT iCAX::CameraNavigation::CCameraNavigationComponent& Navigation_,
        IN const double nDeltaTime_,
        IN const double nTotalTime_,
        IN OUT WheelRuntimeState& WheelState_,
        IN OUT iCAX::Transform::CTransformComponent& Transform_)
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
        auto _OrbitTarget = Vec3{
            Navigation_.GetOrbitTargetX(),
            Navigation_.GetOrbitTargetY(),
            Navigation_.GetOrbitTargetZ()
        };

        const bool _bFast = ((_InputFrame.nModifierMask & iCAX::InputPDO::kInputModifierShift) != 0)
            || IsDown(InputService_, ProjectID_, SceneID_, nViewportID_, iCAX::InputPDO::EInputKeyCode::Shift);
        const auto _Speed = Navigation_.GetMoveSpeed()
            * (_bFast ? Navigation_.GetFastMultiplier() : 1.0)
            * (std::max)(0.0, nDeltaTime_);

        bool _bChanged = false;
        bool _bOrbitTargetChanged = false;
        Vec3 _Move;
        const auto _Forward = NormalizeOr(ForwardFromYawPitch(_Yaw, _Pitch), { 1.0, 0.0, 0.0 });
        const auto _Right = NormalizeOr(Cross(_Forward, Vec3{ 0.0, 0.0, 1.0 }), { 1.0, 0.0, 0.0 });
        const auto _CameraUp = NormalizeOr(Cross(_Right, _Forward), { 0.0, 0.0, 1.0 });

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
        const bool _bPan = (_Pointer.nButtonMask & iCAX::InputPDO::kInputMouseButtonMiddle) != 0;
        if (_bPan && (std::abs(_Pointer.nDeltaX) > kEpsilon || std::abs(_Pointer.nDeltaY) > kEpsilon))
        {
            auto _PanDelta = PointerDelta{ _Pointer.nDeltaX, _Pointer.nDeltaY };
            const auto _PanDeadZone = (std::max)(0.0, Navigation_.GetPanDeadZonePixels());
            if (std::abs(_PanDelta.x) <= _PanDeadZone)
            {
                _PanDelta.x = 0.0;
            }
            if (std::abs(_PanDelta.y) <= _PanDeadZone)
            {
                _PanDelta.y = 0.0;
            }
            if (std::abs(_PanDelta.x) > kEpsilon || std::abs(_PanDelta.y) > kEpsilon)
            {
                const auto _OrbitDistance = (std::max)(kEpsilon, Length(Sub(_Position, _OrbitTarget)));
                const auto _PanScale = _OrbitDistance * (std::max)(0.0, Navigation_.GetPanSpeedScale());
                const auto _PanMove = Add(
                    Scale(_Right, -_PanDelta.x * _PanScale),
                    Scale(_CameraUp, _PanDelta.y * _PanScale));
                _Position = Add(_Position, _PanMove);
                _OrbitTarget = Add(_OrbitTarget, _PanMove);
                _bChanged = true;
                _bOrbitTargetChanged = true;
            }
        }

        const bool _bRotate = (_Pointer.nButtonMask & iCAX::InputPDO::kInputMouseButtonRight) != 0;
        const auto _OrbitDelta = FilterOrbitDelta(
            _Pointer.nDeltaX,
            _Pointer.nDeltaY,
            Navigation_.GetOrbitDeadZonePixels(),
            Navigation_.GetOrbitAxisLockRatio());
        if (_bRotate && (std::abs(_OrbitDelta.x) > kEpsilon || std::abs(_OrbitDelta.y) > kEpsilon))
        {
            const auto _Sensitivity = Navigation_.GetMouseSensitivity();
            const auto _OrbitDistance = (std::max)(kEpsilon, Length(Sub(_Position, _OrbitTarget)));
            _Yaw += _OrbitDelta.x * _Sensitivity;
            _Pitch = std::clamp(_Pitch - _OrbitDelta.y * _Sensitivity, kMinPitch, kMaxPitch);
            const auto _OrbitForward = NormalizeOr(ForwardFromYawPitch(_Yaw, _Pitch), { 1.0, 0.0, 0.0 });
            _Position = Sub(_OrbitTarget, Scale(_OrbitForward, _OrbitDistance));
            _bChanged = true;
        }

        if (std::abs(_Pointer.nWheelY) > kEpsilon)
        {
            const auto _WheelMultiplier = ResolveWheelAccelerationMultiplier(
                _Pointer.nWheelY,
                nTotalTime_,
                Navigation_,
                WheelState_);
            _Position = Add(_Position, Scale(_Forward, -_Pointer.nWheelY * _Speed * Navigation_.GetWheelSpeedScale() * _WheelMultiplier));
            _bChanged = true;
        }

        if (!_bChanged)
        {
            return false;
        }

        iCAX::Data::PropertySet _Properties;
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionX] =
            iCAX::Data::PropertyValue(_Position.x);
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionY] =
            iCAX::Data::PropertyValue(_Position.y);
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionZ] =
            iCAX::Data::PropertyValue(_Position.z);
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_YawRadians] =
            iCAX::Data::PropertyValue(_Yaw);
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PitchRadians] =
            iCAX::Data::PropertyValue(_Pitch);
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_RollRadians] =
            iCAX::Data::PropertyValue(_Roll);

        std::string _strError;
        if (!Transform_.SetProperties(_Properties, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Camera navigation cannot update transform component" : _strError);
        }
        if (_bOrbitTargetChanged)
        {
            iCAX::Data::PropertySet _NavigationProperties;
            _NavigationProperties[iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetX] =
                iCAX::Data::PropertyValue(_OrbitTarget.x);
            _NavigationProperties[iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetY] =
                iCAX::Data::PropertyValue(_OrbitTarget.y);
            _NavigationProperties[iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetZ] =
                iCAX::Data::PropertyValue(_OrbitTarget.z);
            if (!Navigation_.SetProperties(_NavigationProperties, _strError))
            {
                throw std::runtime_error(_strError.empty() ? "Camera navigation cannot update orbit target" : _strError);
            }
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
        void OnDestroyImmediate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            if (auto _pEntity = Component_.GetEntity())
            {
                EraseWheelState(ProjectContext_.GetProjectID(), SceneContext_.GetSceneID(), _pEntity->GetID());
            }
        }

        void OnDestroy(
            IN const iCAX::Behaviour::CComponentDestroyInfo& DestroyInfo_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_) override
        {
            EraseWheelState(ProjectContext_.GetProjectID(), SceneContext_.GetSceneID(), DestroyInfo_.EntityID);
        }

        void OnUpdate(
            IN iCAX::Database::CComponentBase& Component_,
            IN const iCAX::Application::IApplicationContext&,
            IN const iCAX::Product::IProductContext&,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN const double& nDeltaTime_,
            IN const double& nTotalTime_) override
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

            auto _pTransform = _pEntity->GetComponent<iCAX::Transform::CTransformComponent>();
            if (!_pTransform)
            {
                _pTransform = _pEntity->AddComponent<iCAX::Transform::CTransformComponent>();
            }

            auto _pInputService = SceneContext_.Services().Resolve<iCAX::Input::IInputService>();
            _pInputService->UpdateFromPDO(ProjectContext_, SceneContext_);
            const auto _ViewportID = ResolveViewportID(_Navigation, *_pCamera);
            auto& _WheelState = m_WheelStates[NavigationRuntimeKey{
                ProjectContext_.GetProjectID(),
                SceneContext_.GetSceneID(),
                _pEntity->GetID(),
                _ViewportID
            }];

            (void)ApplyNavigationInput(
                *_pInputService,
                ProjectContext_.GetProjectID(),
                SceneContext_.GetSceneID(),
                _ViewportID,
                _Navigation,
                nDeltaTime_,
                nTotalTime_,
                _WheelState,
                *_pTransform);
        }

    private:
        void EraseWheelState(
            IN const iCAX::Data::uuid& ProjectID_,
            IN const iCAX::Data::uuid& SceneID_,
            IN const iCAX::Data::uuid& EntityID_)
        {
            std::erase_if(
                m_WheelStates,
                [&ProjectID_, &SceneID_, &EntityID_](IN const auto& Item_)
                {
                    return Item_.first.ProjectID == ProjectID_
                        && Item_.first.SceneID == SceneID_
                        && Item_.first.EntityID == EntityID_;
                });
        }

        std::unordered_map<NavigationRuntimeKey, WheelRuntimeState, NavigationRuntimeKeyHasher> m_WheelStates;
    };
}

uint32_t iCAX::CameraNavigation::GetCameraNavigationContractVersion() noexcept
{
    return 1;
}

