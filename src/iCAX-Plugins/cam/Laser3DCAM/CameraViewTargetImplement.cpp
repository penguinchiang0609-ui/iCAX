#include "pch.h"
#include "CameraViewTargetImplement.h"

#include "CameraNavigation/CameraNavigation.h"
#include "Laser3DCAM.h"
#include "MachineInstanceComponents.h"
#include "RenderInteraction/RenderInteraction.h"
#include "RenderService/RenderService.h"
#include "Services/ServiceProvider.h"
#include "ToolpathTargetImplement.h"
#include "Transform/Transform.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace iCAX::CAM::Commands::Internal
{
    struct SFitVec3 final
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct SFitBounds final
    {
        bool bValid = false;
        SFitVec3 Min;
        SFitVec3 Max;
    };

    SFitVec3 _Add(IN const SFitVec3& Left_, IN const SFitVec3& Right_) noexcept
    {
        return { Left_.x + Right_.x, Left_.y + Right_.y, Left_.z + Right_.z };
    }

    SFitVec3 _Sub(IN const SFitVec3& Left_, IN const SFitVec3& Right_) noexcept
    {
        return { Left_.x - Right_.x, Left_.y - Right_.y, Left_.z - Right_.z };
    }

    SFitVec3 _Scale(IN const SFitVec3& Value_, IN double nScale_) noexcept
    {
        return { Value_.x * nScale_, Value_.y * nScale_, Value_.z * nScale_ };
    }

    double _Length(IN const SFitVec3& Value_) noexcept
    {
        return std::sqrt(Value_.x * Value_.x + Value_.y * Value_.y + Value_.z * Value_.z);
    }

    SFitVec3 _NormalizeOr(IN const SFitVec3& Value_, IN const SFitVec3& Fallback_) noexcept
    {
        const auto _Len = _Length(Value_);
        if (_Len <= 0.0000001)
        {
            return Fallback_;
        }
        return { Value_.x / _Len, Value_.y / _Len, Value_.z / _Len };
    }

    SFitVec3 _ForwardFromYawPitch(IN double nYawRadians_, IN double nPitchRadians_) noexcept
    {
        const auto _CosPitch = std::cos(nPitchRadians_);
        return {
            _CosPitch * std::cos(nYawRadians_),
            _CosPitch * std::sin(nYawRadians_),
            -std::sin(nPitchRadians_)
        };
    }

    std::pair<double, double> _YawPitchFromForward(IN const SFitVec3& Forward_) noexcept
    {
        const auto _Forward = _NormalizeOr(Forward_, _ForwardFromYawPitch(
            iCAX::CAM::kDefaultCameraYawRadians,
            iCAX::CAM::kDefaultCameraPitchRadians));
        const auto _Yaw = std::atan2(_Forward.y, _Forward.x);
        const auto _Pitch = std::asin(std::clamp(-_Forward.z, -1.0, 1.0));
        return { _Yaw, _Pitch };
    }

    void _ExtendBounds(IN OUT SFitBounds& Bounds_, IN const SFitVec3& Point_) noexcept
    {
        if (!Bounds_.bValid)
        {
            Bounds_.bValid = true;
            Bounds_.Min = Point_;
            Bounds_.Max = Point_;
            return;
        }

        Bounds_.Min.x = (std::min)(Bounds_.Min.x, Point_.x);
        Bounds_.Min.y = (std::min)(Bounds_.Min.y, Point_.y);
        Bounds_.Min.z = (std::min)(Bounds_.Min.z, Point_.z);
        Bounds_.Max.x = (std::max)(Bounds_.Max.x, Point_.x);
        Bounds_.Max.y = (std::max)(Bounds_.Max.y, Point_.y);
        Bounds_.Max.z = (std::max)(Bounds_.Max.z, Point_.z);
    }

    SFitVec3 _TransformPoint(
        IN const iCAX::Render::SMatrix4& Matrix_,
        IN const iCAX::Render::SFloat3& Point_) noexcept
    {
        const auto& _M = Matrix_.Values;
        return {
            static_cast<double>(_M[0]) * Point_.x + static_cast<double>(_M[4]) * Point_.y + static_cast<double>(_M[8]) * Point_.z + static_cast<double>(_M[12]),
            static_cast<double>(_M[1]) * Point_.x + static_cast<double>(_M[5]) * Point_.y + static_cast<double>(_M[9]) * Point_.z + static_cast<double>(_M[13]),
            static_cast<double>(_M[2]) * Point_.x + static_cast<double>(_M[6]) * Point_.y + static_cast<double>(_M[10]) * Point_.z + static_cast<double>(_M[14])
        };
    }

    const iCAX::Render::SMatrix4& _FindTransformOrIdentity(
        IN const iCAX::Render::SRenderSceneSnapshot& Snapshot_,
        IN iCAX::Render::TransformID nTransformID_)
    {
        static const auto _Identity = iCAX::Render::SMatrix4::Identity();
        auto _Iter = std::find_if(
            Snapshot_.Transforms.begin(),
            Snapshot_.Transforms.end(),
            [nTransformID_](IN const iCAX::Render::STransformData& Item_)
            {
                return Item_.nTransformID == nTransformID_;
            });
        return _Iter == Snapshot_.Transforms.end() ? _Identity : _Iter->LocalToWorld;
    }

    void _ExtendRenderableBounds(
        IN OUT SFitBounds& Bounds_,
        IN const iCAX::Render::SRenderSceneSnapshot& Snapshot_,
        IN const iCAX::Render::SRenderInstanceData& Instance_)
    {
        if ((Instance_.nFlags & iCAX::Render::kRenderFlagVisible) == 0)
        {
            return;
        }

        const auto& _Transform = _FindTransformOrIdentity(
            Snapshot_,
            Instance_.nObjectID);

        if (Instance_.eGeometryKind == iCAX::Render::ERenderGeometryKind::Mesh)
        {
            auto _Iter = Snapshot_.Meshes.find(Instance_.nGeometryID);
            if (_Iter == Snapshot_.Meshes.end())
            {
                return;
            }
            for (const auto& _Point : _Iter->second.Positions)
            {
                _ExtendBounds(Bounds_, _TransformPoint(_Transform, _Point));
            }
            return;
        }

        if (Instance_.eGeometryKind == iCAX::Render::ERenderGeometryKind::Polyline)
        {
            auto _Iter = Snapshot_.Polylines.find(Instance_.nGeometryID);
            if (_Iter == Snapshot_.Polylines.end())
            {
                return;
            }
            for (const auto& _Point : _Iter->second.Points)
            {
                _ExtendBounds(Bounds_, _TransformPoint(_Transform, _Point));
            }
            return;
        }

        if (Instance_.eGeometryKind == iCAX::Render::ERenderGeometryKind::Toolpath)
        {
            auto _Iter = Snapshot_.Toolpaths.find(Instance_.nGeometryID);
            if (_Iter == Snapshot_.Toolpaths.end())
            {
                return;
            }
            for (const auto& _Point : _Iter->second.Points)
            {
                _ExtendBounds(Bounds_, _TransformPoint(_Transform, _Point.Position));
            }
        }
    }

    SFitBounds _ComputeRenderableBounds(IN const iCAX::Render::SRenderSceneSnapshot& Snapshot_)
    {
        SFitBounds _Bounds;
        for (const auto& _Instance : Snapshot_.Objects)
        {
            _ExtendRenderableBounds(_Bounds, Snapshot_, _Instance);
        }
        return _Bounds;
    }

    SFitVec3 _ForwardForStandardView(IN std::string strViewName_)
    {
        std::transform(strViewName_.begin(), strViewName_.end(), strViewName_.begin(), [](IN unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        auto _AppendPart = [](IN const std::string& strPart_, IN OUT SFitVec3& Forward_) -> bool
        {
            if (strPart_ == "front")
            {
                Forward_.y += 1.0;
                return true;
            }
            if (strPart_ == "back")
            {
                Forward_.y -= 1.0;
                return true;
            }
            if (strPart_ == "right")
            {
                Forward_.x -= 1.0;
                return true;
            }
            if (strPart_ == "left")
            {
                Forward_.x += 1.0;
                return true;
            }
            if (strPart_ == "top")
            {
                Forward_.z -= 1.0;
                return true;
            }
            if (strPart_ == "bottom")
            {
                Forward_.z += 1.0;
                return true;
            }
            return false;
        };

        if (strViewName_.find('-') != std::string::npos)
        {
            SFitVec3 _Forward;
            bool _HasPart = false;
            std::string _Part;
            for (const auto _Char : strViewName_)
            {
                if (_Char == '-')
                {
                    _HasPart = _AppendPart(_Part, _Forward) || _HasPart;
                    _Part.clear();
                    continue;
                }
                _Part.push_back(_Char);
            }
            _HasPart = _AppendPart(_Part, _Forward) || _HasPart;
            if (_HasPart)
            {
                return _Forward;
            }
        }

        if (strViewName_ == "front")
        {
            return { 0.0, 1.0, 0.0 };
        }
        if (strViewName_ == "back")
        {
            return { 0.0, -1.0, 0.0 };
        }
        if (strViewName_ == "right")
        {
            return { -1.0, 0.0, 0.0 };
        }
        if (strViewName_ == "left")
        {
            return { 1.0, 0.0, 0.0 };
        }
        if (strViewName_ == "top")
        {
            return { 0.0, 0.0, -1.0 };
        }
        if (strViewName_ == "bottom")
        {
            return { 0.0, 0.0, 1.0 };
        }
        return { -0.55, 0.68, -0.46 };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::RenderInteraction::CCameraComponent>>
        _FindActiveCamera(IN iCAX::Database::IRepository& Repository_)
    {
        auto _Cameras = _CollectEntitiesWithComponent<iCAX::RenderInteraction::CCameraComponent>(Repository_);
        if (_Cameras.empty())
        {
            throw std::runtime_error("Fit view requires a render camera component");
        }

        auto _Iter = std::find_if(
            _Cameras.begin(),
            _Cameras.end(),
            [](IN const auto& Item_)
            {
                return Item_.second && Item_.second->GetActive();
            });
        return _Iter == _Cameras.end() ? _Cameras.front() : *_Iter;
    }

    std::shared_ptr<iCAX::Transform::CTransformComponent> _GetOrAddTransform(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        auto _pTransform = _GetComponent<iCAX::Transform::CTransformComponent>(pEntity_);
        if (_pTransform)
        {
            return _pTransform;
        }
        return _AddComponent<iCAX::Transform::CTransformComponent>(pEntity_);
    }

    ObjectMap _FitActiveCameraToRenderableBounds(
        IN iCAX::Project::IProjectContext& ProjectContext_,
        IN iCAX::Project::ISceneContext& SceneContext_)
    {
        auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
        const auto& _ProjectID = ProjectContext_.GetProjectID();
        const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());

        auto _MakeDiagnostics = [&]() {
            ObjectMap _Diagnostics;
            auto& _Repository = SceneContext_.Database();
            auto _Machines = _CollectEntitiesWithComponent<iCAX::CAM::CMachineInstanceComponent>(_Repository);
            auto _Workpieces = _CollectEntitiesWithComponent<iCAX::CAM::CWorkpieceComponent>(_Repository);
            auto _Cameras = _CollectEntitiesWithComponent<iCAX::RenderInteraction::CCameraComponent>(_Repository);
            auto _RenderInstances = _CollectEntitiesWithComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_Repository);
            auto _MachineVisuals = _CollectEntitiesWithComponent<iCAX::CAM::CMachineVisualComponent>(_Repository);
            auto _MachineCollisions = _CollectEntitiesWithComponent<iCAX::CAM::CMachineCollisionComponent>(_Repository);

            _Diagnostics["machineCount"] = static_cast<unsigned long long>(_Machines.size());
            _Diagnostics["machineVisualCount"] = static_cast<unsigned long long>(_MachineVisuals.size());
            _Diagnostics["machineCollisionCount"] = static_cast<unsigned long long>(_MachineCollisions.size());
            _Diagnostics["machineIncludeCount"] = 0ull;
            _Diagnostics["workpieceCount"] = static_cast<unsigned long long>(_Workpieces.size());
            _Diagnostics["cameraComponentCount"] = static_cast<unsigned long long>(_Cameras.size());
            _Diagnostics["renderInstanceComponentCount"] = static_cast<unsigned long long>(_RenderInstances.size());
            _Diagnostics["renderSceneId"] = static_cast<unsigned long long>(_RenderSceneID);
            return _Diagnostics;
        };

        if (!_pRenderService->HasScene(_ProjectID, _RenderSceneID))
        {
            auto _Result = _MakeDiagnostics();
            _Result["fitted"] = false;
            _Result["renderSceneExists"] = false;
            _Result["reason"] = std::string("render scene does not exist");
            return _Result;
        }

        const auto _Snapshot = _pRenderService->GetSceneSnapshot(_ProjectID, _RenderSceneID);
        const auto _Bounds = _ComputeRenderableBounds(_Snapshot);
        if (!_Bounds.bValid)
        {
            auto _Result = _MakeDiagnostics();
            _Result["fitted"] = false;
            _Result["renderSceneExists"] = true;
            _Result["renderMeshCount"] = static_cast<unsigned long long>(_Snapshot.Meshes.size());
            _Result["renderPolylineCount"] = static_cast<unsigned long long>(_Snapshot.Polylines.size());
            _Result["renderToolpathCount"] = static_cast<unsigned long long>(_Snapshot.Toolpaths.size());
            _Result["renderObjectCount"] = static_cast<unsigned long long>(_Snapshot.Objects.size());
            _Result["renderTransformCount"] = static_cast<unsigned long long>(_Snapshot.Transforms.size());
            _Result["renderCameraCount"] = static_cast<unsigned long long>(_Snapshot.Cameras.size());
            _Result["reason"] = std::string("render scene is empty");
            return _Result;
        }

        auto& _Repository = SceneContext_.Database();
        auto [_pCameraEntity, _pCamera] = _FindActiveCamera(_Repository);
        auto _pTransform = _GetOrAddTransform(_pCameraEntity);
        auto _pNavigation = _GetOrAddEntityComponent<iCAX::CameraNavigation::CCameraNavigationComponent>(_pCameraEntity);

        const auto _Center = _Scale(_Add(_Bounds.Min, _Bounds.Max), 0.5);
        const auto _Extent = _Sub(_Bounds.Max, _Bounds.Min);
        const auto _Radius = (std::max)(1.0, _Length(_Extent) * 0.5);
        const auto _Fov = (std::max)(0.1, _pCamera->GetVerticalFovRadians());
        const auto _Distance = (std::max)(10.0, _Radius / std::sin(_Fov * 0.5) * 1.35);
        const auto _Forward = _NormalizeOr({ -0.55, 0.68, -0.46 }, _ForwardFromYawPitch(
            iCAX::CAM::kDefaultCameraYawRadians,
            iCAX::CAM::kDefaultCameraPitchRadians));
        const auto [_CameraYaw, _CameraPitch] = _YawPitchFromForward(_Forward);
        const auto _Position = _Sub(_Center, _Scale(_Forward, _Distance));
        const auto _MoveSpeed = (std::max)(20.0, _Radius * 0.85);
        const auto _FarPlane = (std::max)(1000.0, _Distance + _Radius * 6.0);
        const auto _NearPlane = (std::max)(0.01, (std::min)(10.0, _Radius / 5000.0));

        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionX, _Position.x);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionY, _Position.y);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionZ, _Position.z);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_YawRadians, _CameraYaw);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PitchRadians, _CameraPitch);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_RollRadians, iCAX::CAM::kDefaultCameraRollRadians);

        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_MoveSpeed, _MoveSpeed);
        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetX, _Center.x);
        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetY, _Center.y);
        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_OrbitTargetZ, _Center.z);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_NearPlane, _NearPlane);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_FarPlane, _FarPlane);

        auto _Result = _MakeDiagnostics();
        _Result["fitted"] = true;
        _Result["renderSceneExists"] = true;
        _Result["renderMeshCount"] = static_cast<unsigned long long>(_Snapshot.Meshes.size());
        _Result["renderPolylineCount"] = static_cast<unsigned long long>(_Snapshot.Polylines.size());
        _Result["renderToolpathCount"] = static_cast<unsigned long long>(_Snapshot.Toolpaths.size());
        _Result["renderObjectCount"] = static_cast<unsigned long long>(_Snapshot.Objects.size());
        _Result["renderTransformCount"] = static_cast<unsigned long long>(_Snapshot.Transforms.size());
        _Result["renderCameraCount"] = static_cast<unsigned long long>(_Snapshot.Cameras.size());
        _Result["centerX"] = _Center.x;
        _Result["centerY"] = _Center.y;
        _Result["centerZ"] = _Center.z;
        _Result["radius"] = _Radius;
        _Result["distance"] = _Distance;
        _Result["moveSpeed"] = _MoveSpeed;
        return _Result;
    }

    ObjectMap _SetActiveCameraToStandardView(
        IN iCAX::Project::IProjectContext& ProjectContext_,
        IN iCAX::Project::ISceneContext& SceneContext_,
        IN const std::string& strViewName_)
    {
        auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
        const auto& _ProjectID = ProjectContext_.GetProjectID();
        const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());

        ObjectMap _Result;
        _Result["view"] = strViewName_.empty() ? std::string("iso") : strViewName_;
        _Result["renderSceneId"] = static_cast<unsigned long long>(_RenderSceneID);

        if (!_pRenderService->HasScene(_ProjectID, _RenderSceneID))
        {
            _Result["applied"] = false;
            _Result["reason"] = std::string("render scene does not exist");
            return _Result;
        }

        const auto _Snapshot = _pRenderService->GetSceneSnapshot(_ProjectID, _RenderSceneID);
        const auto _Bounds = _ComputeRenderableBounds(_Snapshot);
        if (!_Bounds.bValid)
        {
            _Result["applied"] = false;
            _Result["reason"] = std::string("render scene is empty");
            _Result["renderObjectCount"] = static_cast<unsigned long long>(_Snapshot.Objects.size());
            _Result["renderTransformCount"] = static_cast<unsigned long long>(_Snapshot.Transforms.size());
            _Result["renderCameraCount"] = static_cast<unsigned long long>(_Snapshot.Cameras.size());
            return _Result;
        }

        auto& _Repository = SceneContext_.Database();
        auto [_pCameraEntity, _pCamera] = _FindActiveCamera(_Repository);
        auto _pTransform = _GetOrAddTransform(_pCameraEntity);
        auto _pNavigation = _GetOrAddEntityComponent<iCAX::CameraNavigation::CCameraNavigationComponent>(_pCameraEntity);

        const auto _BoundsCenter = _Scale(_Add(_Bounds.Min, _Bounds.Max), 0.5);
        const auto _Extent = _Sub(_Bounds.Max, _Bounds.Min);
        const auto _Radius = (std::max)(1.0, _Length(_Extent) * 0.5);
        const auto _Fov = (std::max)(0.1, _pCamera->GetVerticalFovRadians());
        const auto _FitDistance = (std::max)(10.0, _Radius / std::sin(_Fov * 0.5) * 1.25);
        const auto _Target = SFitVec3{
            _pNavigation->GetOrbitTargetX(),
            _pNavigation->GetOrbitTargetY(),
            _pNavigation->GetOrbitTargetZ()
        };
        const auto _CurrentPosition = SFitVec3{
            _pTransform->GetPositionX(),
            _pTransform->GetPositionY(),
            _pTransform->GetPositionZ()
        };
        const auto _CurrentDistance = _Length(_Sub(_CurrentPosition, _Target));
        const auto _Distance = _CurrentDistance > 1.0 ? _CurrentDistance : _FitDistance;
        const auto _Forward = _NormalizeOr(
            _ForwardForStandardView(strViewName_),
            _ForwardFromYawPitch(iCAX::CAM::kDefaultCameraYawRadians, iCAX::CAM::kDefaultCameraPitchRadians));
        const auto [_CameraYaw, _CameraPitch] = _YawPitchFromForward(_Forward);
        const auto _Position = _Sub(_Target, _Scale(_Forward, _Distance));
        const auto _MoveSpeed = (std::max)(20.0, _Radius * 0.85);
        const auto _FarPlane = (std::max)(1000.0, _Distance + _Radius * 6.0);
        const auto _NearPlane = (std::max)(0.01, (std::min)(10.0, _Radius / 5000.0));

        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionX, _Position.x);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionY, _Position.y);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PositionZ, _Position.z);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_YawRadians, _CameraYaw);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_PitchRadians, _CameraPitch);
        _SetDoubleProperty(_pTransform, iCAX::Transform::CTransformComponent::PropertyName_RollRadians, iCAX::CAM::kDefaultCameraRollRadians);

        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_MoveSpeed, _MoveSpeed);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_NearPlane, _NearPlane);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_FarPlane, _FarPlane);

        _Result["applied"] = true;
        _Result["targetX"] = _Target.x;
        _Result["targetY"] = _Target.y;
        _Result["targetZ"] = _Target.z;
        _Result["boundsCenterX"] = _BoundsCenter.x;
        _Result["boundsCenterY"] = _BoundsCenter.y;
        _Result["boundsCenterZ"] = _BoundsCenter.z;
        _Result["radius"] = _Radius;
        _Result["distance"] = _Distance;
        _Result["moveSpeed"] = _MoveSpeed;
        return _Result;
    }

}

