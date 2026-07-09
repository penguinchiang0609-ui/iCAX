#include "pch.h"
#include "RenderInteraction.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/SceneObjectRegistry.h"
#include "RenderData/RenderData.h"
#include "RenderService/RenderService.h"
#include "Services/ServiceProvider.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr double kEpsilon = 0.000001;

    struct Vec3 final
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    uint64_t NextRenderVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t MakeNonZeroVersion(IN uint64_t nVersion_) noexcept
    {
        return nVersion_ == 0 ? NextRenderVersion() : nVersion_;
    }

    Vec3 Add(IN const Vec3& A_, IN const Vec3& B_) noexcept
    {
        return { A_.x + B_.x, A_.y + B_.y, A_.z + B_.z };
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

    iCAX::Render::SMatrix4 MakeLocalToWorld(IN const iCAX::RenderInteraction::CRenderTransformComponent& Transform_) noexcept
    {
        const auto _Forward = NormalizeOr(
            ForwardFromYawPitch(Transform_.GetYawRadians(), Transform_.GetPitchRadians()),
            { 1.0, 0.0, 0.0 });
        const auto _Back = Vec3{ -_Forward.x, -_Forward.y, -_Forward.z };
        const auto _WorldUp = Vec3{ 0.0, 0.0, 1.0 };
        const auto _BaseRight = NormalizeOr(Cross(_WorldUp, _Back), { 1.0, 0.0, 0.0 });
        const auto _BaseUp = NormalizeOr(Cross(_Back, _BaseRight), _WorldUp);

        const auto _CosRoll = std::cos(Transform_.GetRollRadians());
        const auto _SinRoll = std::sin(Transform_.GetRollRadians());
        const auto _Right = Add(Scale(_BaseRight, _CosRoll), Scale(_BaseUp, _SinRoll));
        const auto _Up = Add(Scale(_BaseRight, -_SinRoll), Scale(_BaseUp, _CosRoll));

        auto _Matrix = iCAX::Render::SMatrix4::Identity();
        _Matrix.Values[0] = static_cast<float>(_Right.x * Transform_.GetScaleX());
        _Matrix.Values[1] = static_cast<float>(_Right.y * Transform_.GetScaleX());
        _Matrix.Values[2] = static_cast<float>(_Right.z * Transform_.GetScaleX());
        _Matrix.Values[4] = static_cast<float>(_Up.x * Transform_.GetScaleY());
        _Matrix.Values[5] = static_cast<float>(_Up.y * Transform_.GetScaleY());
        _Matrix.Values[6] = static_cast<float>(_Up.z * Transform_.GetScaleY());
        _Matrix.Values[8] = static_cast<float>(_Back.x * Transform_.GetScaleZ());
        _Matrix.Values[9] = static_cast<float>(_Back.y * Transform_.GetScaleZ());
        _Matrix.Values[10] = static_cast<float>(_Back.z * Transform_.GetScaleZ());
        _Matrix.Values[12] = static_cast<float>(Transform_.GetPositionX());
        _Matrix.Values[13] = static_cast<float>(Transform_.GetPositionY());
        _Matrix.Values[14] = static_cast<float>(Transform_.GetPositionZ());
        return _Matrix;
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

    iCAX::Render::STransformData MakeTransformData(
        IN const iCAX::RenderInteraction::CRenderTransformComponent& Transform_,
        IN iCAX::Render::TransformID nTransformID_,
        IN bool bEnabled_)
    {
        iCAX::Render::STransformData _Data;
        _Data.nTransformID = nTransformID_;
        _Data.nDataVersion = MakeNonZeroVersion(Transform_.Version());
        _Data.nFlags = bEnabled_ ? 0 : iCAX::Render::kRenderFlagDisabled;
        _Data.LocalToWorld = MakeLocalToWorld(Transform_);
        return _Data;
    }

    bool SameTransform(
        IN const iCAX::Render::STransformData& Left_,
        IN const iCAX::Render::STransformData& Right_) noexcept
    {
        if (Left_.nTransformID != Right_.nTransformID
            || Left_.nDataVersion != Right_.nDataVersion
            || Left_.nFlags != Right_.nFlags)
        {
            return false;
        }
        return Left_.LocalToWorld.Values == Right_.LocalToWorld.Values;
    }

    bool UpsertTransform(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN const iCAX::Render::STransformData& Transform_)
    {
        auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nSceneID_);
        auto _Transforms = _Snapshot.Transforms;
        auto _Iter = std::find_if(
            _Transforms.begin(),
            _Transforms.end(),
            [&Transform_](IN const iCAX::Render::STransformData& Item_)
            {
                return Item_.nTransformID == Transform_.nTransformID;
            });

        if (_Iter == _Transforms.end())
        {
            _Transforms.push_back(Transform_);
        }
        else if (SameTransform(*_Iter, Transform_))
        {
            return false;
        }
        else
        {
            *_Iter = Transform_;
        }

        if (!RenderService_.SetTransforms(ProjectID_, nSceneID_, _Transforms, Transform_.nDataVersion))
        {
            throw std::runtime_error("RenderService rejected transform update");
        }
        return true;
    }

    bool RemoveTransform(
        IN iCAX::Render::IRenderService& RenderService_,
        IN const iCAX::Data::uuid& ProjectID_,
        IN iCAX::Render::RenderSceneID nSceneID_,
        IN iCAX::Render::TransformID nTransformID_)
    {
        if (!RenderService_.HasScene(ProjectID_, nSceneID_))
        {
            return false;
        }

        auto _Snapshot = RenderService_.GetSceneSnapshot(ProjectID_, nSceneID_);
        auto _Transforms = _Snapshot.Transforms;
        const auto _OriginalSize = _Transforms.size();
        std::erase_if(_Transforms, [nTransformID_](IN const iCAX::Render::STransformData& Item_)
        {
            return Item_.nTransformID == nTransformID_;
        });
        if (_Transforms.size() == _OriginalSize)
        {
            return false;
        }

        if (!RenderService_.SetTransforms(ProjectID_, nSceneID_, _Transforms, NextRenderVersion()))
        {
            throw std::runtime_error("RenderService rejected transform removal");
        }
        return true;
    }

    class CRenderTransformBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CRenderTransformBehaviour)

    public:
        std::string GetComponentClass() const override
        {
            return iCAX::RenderInteraction::CRenderTransformComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule _Schedule;
            _Schedule.ExecutionOrder = 3000;
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
                const auto _TransformID = static_cast<iCAX::Render::TransformID>(
                    SceneContext_.Objects().GetTransformID(*_ObjectID));
                (void)RemoveTransform(*_pRenderService, ProjectContext_.GetProjectID(), _RenderSceneID, _TransformID);
            }
        }

        void Publish(
            IN iCAX::Database::CComponentBase& Component_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN bool bEnabled_)
        {
            auto& _Transform = static_cast<iCAX::RenderInteraction::CRenderTransformComponent&>(Component_);
            auto _pEntity = _Transform.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("RenderTransformComponent is detached from entity");
            }

            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
            EnsureRenderScene(*_pRenderService, _ProjectID, _RenderSceneID);

            const auto _ObjectID = static_cast<iCAX::Render::SceneObjectID>(
                SceneContext_.Objects().GetOrCreateEntityObject(_pEntity->GetID()));
            const auto _TransformID = static_cast<iCAX::Render::TransformID>(
                SceneContext_.Objects().GetTransformID(_ObjectID));
            const auto _Data = MakeTransformData(_Transform, _TransformID, bEnabled_);
            (void)UpsertTransform(*_pRenderService, _ProjectID, _RenderSceneID, _Data);
        }
    };
}
