#include "pch.h"
#include "RenderInteraction.h"

#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IEntity.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "RenderData/RenderData.h"
#include "RenderService/RenderService.h"
#include "Services/ServiceProvider.h"
#include "Transform/Transform.h"


namespace
{
    uint64_t NextRenderVersion() noexcept
    {
        static std::atomic_uint64_t _Version{ 1 };
        return _Version.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t MakeNonZeroVersion(IN uint64_t nVersion_) noexcept
    {
        return nVersion_ == 0 ? NextRenderVersion() : nVersion_;
    }

    uint64_t MakeVersionNewerThan(IN uint64_t nPreviousVersion_) noexcept
    {
        const auto _Next = NextRenderVersion();
        return _Next > nPreviousVersion_ ? _Next : nPreviousVersion_ + 1;
    }

    iCAX::Render::SMatrix4 ToRenderMatrix(IN const iCAX::Data::Double4x4& Matrix_) noexcept
    {
        auto _Matrix = iCAX::Render::SMatrix4::Identity();
        for (int _Row = 0; _Row < 4; ++_Row)
        {
            for (int _Column = 0; _Column < 4; ++_Column)
            {
                _Matrix.Values[static_cast<size_t>(_Column) * 4 + static_cast<size_t>(_Row)] =
                    static_cast<float>(Matrix_(_Row, _Column));
            }
        }
        return _Matrix;
    }

    iCAX::Render::SMatrix4 MakeLocalToWorld(IN const iCAX::Transform::CTransformComponent& Transform_) noexcept
    {
        return ToRenderMatrix(Transform_.GetLocalToWorldMatrix());
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
        IN const iCAX::Transform::CTransformComponent& Transform_,
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

        auto _NextTransform = Transform_;
        if (_Iter == _Transforms.end())
        {
            _Transforms.push_back(_NextTransform);
        }
        else if (SameTransform(*_Iter, _NextTransform))
        {
            return false;
        }
        else
        {
            if (_NextTransform.nDataVersion <= _Iter->nDataVersion)
            {
                _NextTransform.nDataVersion = MakeVersionNewerThan(_Iter->nDataVersion);
            }
            *_Iter = _NextTransform;
        }

        if (!RenderService_.SetTransforms(ProjectID_, nSceneID_, _Transforms, _NextTransform.nDataVersion))
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
            return iCAX::Transform::CTransformComponent::S_ClassName;
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
            (void)RemoveTransform(*_pRenderService, ProjectContext_.GetProjectID(), _RenderSceneID, EntityID_);
        }

        void Publish(
            IN iCAX::Database::CComponentBase& Component_,
            IN iCAX::Project::IProjectContext& ProjectContext_,
            IN iCAX::Project::ISceneContext& SceneContext_,
            IN bool bEnabled_)
        {
            auto& _Transform = static_cast<iCAX::Transform::CTransformComponent&>(Component_);
            auto _pEntity = _Transform.GetEntity();
            if (!_pEntity)
            {
                throw std::runtime_error("TransformComponent is detached from entity");
            }

            auto _pRenderService = SceneContext_.Services().Resolve<iCAX::Render::IRenderService>();
            const auto& _ProjectID = ProjectContext_.GetProjectID();
            const auto _RenderSceneID = iCAX::Render::MakeRenderSceneID(SceneContext_.GetSceneID());
            EnsureRenderScene(*_pRenderService, _ProjectID, _RenderSceneID);

            const auto _TransformID = _pEntity->GetID();
            const auto _Data = MakeTransformData(_Transform, _TransformID, bEnabled_);
            (void)UpsertTransform(*_pRenderService, _ProjectID, _RenderSceneID, _Data);
        }
    };
}
