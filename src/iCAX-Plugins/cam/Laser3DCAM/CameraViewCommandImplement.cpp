#include "pch.h"
#include "CommandInternal.h"
#include <algorithm>
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
            std::sin(nPitchRadians_)
        };
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
            static_cast<iCAX::Render::TransformID>(Instance_.nObjectID));

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
        for (const auto& _Instance : Snapshot_.Instances)
        {
            _ExtendRenderableBounds(_Bounds, Snapshot_, _Instance);
        }
        return _Bounds;
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

    std::shared_ptr<iCAX::RenderInteraction::CRenderTransformComponent> _GetOrAddRenderTransform(
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        auto _pTransform = _GetComponent<iCAX::RenderInteraction::CRenderTransformComponent>(pEntity_);
        if (_pTransform)
        {
            return _pTransform;
        }
        return _AddComponent<iCAX::RenderInteraction::CRenderTransformComponent>(pEntity_);
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
            auto _Machines = _CollectEntitiesWithComponent<iCAX::CAM::CMachineComponent>(_Repository);
            auto _Workpieces = _CollectEntitiesWithComponent<iCAX::CAM::CWorkpieceComponent>(_Repository);
            auto _Cameras = _CollectEntitiesWithComponent<iCAX::RenderInteraction::CCameraComponent>(_Repository);
            auto _RenderInstances = _CollectEntitiesWithComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_Repository);
            size_t _MachineVisualCount = 0;
            size_t _MachineCollisionCount = 0;
            size_t _MachineIncludeCount = 0;
            for (const auto& [_pEntity, _pMachine] : _Machines)
            {
                (void)_pEntity;
                if (_pMachine)
                {
                    _MachineVisualCount += _pMachine->GetVisuals().size();
                    _MachineCollisionCount += _pMachine->GetCollisions().size();
                    _MachineIncludeCount += _pMachine->GetIncludes().size();
                }
            }

            _Diagnostics["machineCount"] = static_cast<unsigned long long>(_Machines.size());
            _Diagnostics["machineVisualCount"] = static_cast<unsigned long long>(_MachineVisualCount);
            _Diagnostics["machineCollisionCount"] = static_cast<unsigned long long>(_MachineCollisionCount);
            _Diagnostics["machineIncludeCount"] = static_cast<unsigned long long>(_MachineIncludeCount);
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
            _Result["renderInstanceCount"] = static_cast<unsigned long long>(_Snapshot.Instances.size());
            _Result["renderTransformCount"] = static_cast<unsigned long long>(_Snapshot.Transforms.size());
            _Result["renderCameraCount"] = static_cast<unsigned long long>(_Snapshot.Cameras.size());
            _Result["reason"] = std::string("render scene is empty");
            return _Result;
        }

        auto& _Repository = SceneContext_.Database();
        auto [_pCameraEntity, _pCamera] = _FindActiveCamera(_Repository);
        auto _pTransform = _GetOrAddRenderTransform(_pCameraEntity);
        auto _pNavigation = _GetOrAddEntityComponent<iCAX::CameraNavigation::CCameraNavigationComponent>(_pCameraEntity);

        const auto _Center = _Scale(_Add(_Bounds.Min, _Bounds.Max), 0.5);
        const auto _Extent = _Sub(_Bounds.Max, _Bounds.Min);
        const auto _Radius = (std::max)(1.0, _Length(_Extent) * 0.5);
        const auto _Fov = (std::max)(0.1, _pCamera->GetVerticalFovRadians());
        const auto _Distance = (std::max)(10.0, _Radius / std::sin(_Fov * 0.5) * 1.35);
        const auto _Forward = _NormalizeOr(
            _ForwardFromYawPitch(
                iCAX::RenderInteraction::kDefaultCameraYawRadians,
                iCAX::RenderInteraction::kDefaultCameraPitchRadians),
            { -0.55, 0.68, -0.46 });
        const auto _Position = _Sub(_Center, _Scale(_Forward, _Distance));
        const auto _MoveSpeed = (std::max)(20.0, _Radius * 0.85);
        const auto _FarPlane = (std::max)(1000.0, _Distance + _Radius * 6.0);
        const auto _NearPlane = (std::max)(0.01, (std::min)(10.0, _Radius / 5000.0));

        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionX, _Position.x);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionY, _Position.y);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PositionZ, _Position.z);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_YawRadians, iCAX::RenderInteraction::kDefaultCameraYawRadians);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_PitchRadians, iCAX::RenderInteraction::kDefaultCameraPitchRadians);
        _SetDoubleProperty(_pTransform, iCAX::RenderInteraction::CRenderTransformComponent::PropertyName_RollRadians, iCAX::RenderInteraction::kDefaultCameraRollRadians);

        _SetDoubleProperty(_pNavigation, iCAX::CameraNavigation::CCameraNavigationComponent::PropertyName_MoveSpeed, _MoveSpeed);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_NearPlane, _NearPlane);
        _SetDoubleProperty(_pCamera, iCAX::RenderInteraction::CCameraComponent::PropertyName_FarPlane, _FarPlane);

        auto _Result = _MakeDiagnostics();
        _Result["fitted"] = true;
        _Result["renderSceneExists"] = true;
        _Result["renderMeshCount"] = static_cast<unsigned long long>(_Snapshot.Meshes.size());
        _Result["renderPolylineCount"] = static_cast<unsigned long long>(_Snapshot.Polylines.size());
        _Result["renderToolpathCount"] = static_cast<unsigned long long>(_Snapshot.Toolpaths.size());
        _Result["renderInstanceCount"] = static_cast<unsigned long long>(_Snapshot.Instances.size());
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

}
