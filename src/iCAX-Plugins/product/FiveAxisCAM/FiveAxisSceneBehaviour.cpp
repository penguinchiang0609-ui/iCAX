#include "FiveAxisCAM.h"
#include "FiveAxisComponents.h"
#include "Behaviour/BehaviourBase.h"
#include "Behaviour/IBehaviourRegistry.h"

namespace
{
    class CFiveAxisSceneBehaviour final : public iCAX::Behaviour::CBehaviourBase
    {
        AUTO_REGIST_BEHAVIOUR(CFiveAxisSceneBehaviour)
    public:
        std::string GetComponentClass() const override { return iCAX::FiveAxisCAM::CFiveAxisSceneComponent::S_ClassName; }
        iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
        { iCAX::Behaviour::CBehaviourSchedule schedule; schedule.ExecutionOrder = -9000; return schedule; }
    protected:
        void OnAwake(iCAX::Database::CComponentBase&, const iCAX::Application::IApplicationContext&,
            const iCAX::Product::IProductContext&, iCAX::Project::IProjectContext&, iCAX::Project::ISceneContext& scene) override
        { iCAX::FiveAxisCAM::InitializeScene(scene); }
        void OnUpdate(iCAX::Database::CComponentBase&, const iCAX::Application::IApplicationContext&,
            const iCAX::Product::IProductContext&, iCAX::Project::IProjectContext&, iCAX::Project::ISceneContext& scene,
            const double& delta, const double&) override
        { if (!scene.IsStopRequested()) { iCAX::FiveAxisCAM::PumpMachinePoseEdit(scene); iCAX::FiveAxisCAM::PumpTasks(scene); iCAX::FiveAxisCAM::PumpTaskProgressPDO(scene); iCAX::FiveAxisCAM::PumpMachineJog(scene,delta); } }
        void OnDisable(iCAX::Database::CComponentBase& component,const iCAX::Application::IApplicationContext&,
            const iCAX::Product::IProductContext&,iCAX::Project::IProjectContext&,iCAX::Project::ISceneContext& scene) override
        { const auto& runtime=static_cast<iCAX::FiveAxisCAM::CFiveAxisSceneComponent&>(component).Runtime;iCAX::FiveAxisCAM::ReleaseMachineJog(scene,runtime);iCAX::FiveAxisCAM::ReleaseMachinePoseEdit(scene,runtime);iCAX::FiveAxisCAM::ReleaseTaskProgressPDO(scene,runtime);iCAX::FiveAxisCAM::ReleaseIntentToolpaths(scene,runtime); }
        void OnDestroyImmediate(iCAX::Database::CComponentBase& component,const iCAX::Application::IApplicationContext&,
            const iCAX::Product::IProductContext&,iCAX::Project::IProjectContext&,iCAX::Project::ISceneContext& scene) override
        { const auto& runtime=static_cast<iCAX::FiveAxisCAM::CFiveAxisSceneComponent&>(component).Runtime;iCAX::FiveAxisCAM::ReleaseMachineJog(scene,runtime);iCAX::FiveAxisCAM::ReleaseMachinePoseEdit(scene,runtime);iCAX::FiveAxisCAM::ReleaseTaskProgressPDO(scene,runtime);iCAX::FiveAxisCAM::ReleaseIntentToolpaths(scene,runtime); }
    };
}
