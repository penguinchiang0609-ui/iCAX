#include "pch.h"
#include "SceneBootstrapComponents.h"

namespace
{
    class CSceneBootstrapBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CSceneBootstrapBehaviour)
    public:
        std::string GetComponentClass() const override
        {
            return iCAX::Common::CSceneBootstrapComponent::S_ClassName;
        }

        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        {
            iCAX::Behaviour::CBehaviourSchedule schedule;
            schedule.ExecutionOrder = -10000;
            return schedule;
        }

    protected:
        void OnAwake(
            iCAX::Database::CComponentBase&,
            const iCAX::Application::IApplicationContext&,
            const iCAX::Product::IProductContext&,
            iCAX::Project::IProjectContext&,
            iCAX::Project::ISceneContext& scene) override
        {
            if (!scene.Database().GetMetaEntity())
                throw std::runtime_error("generic scene bootstrap requires repository meta entity");
        }
    };
}
