#include "pch.h"
#include "MachineInstanceComponents.h"
#include "MachineTransformConstraints.h"
#include <Behaviour/BehaviourBase.h>
#include <Behaviour/IBehaviourRegistry.h>
#include <ProjectContext/ISceneContext.h>

namespace {
class CMachinePoseBehaviour final:public iCAX::Behaviour::CBehaviourBase {
    AUTO_REGIST_BEHAVIOUR(CMachinePoseBehaviour)
public:
    std::string GetComponentClass() const override{return iCAX::CAM::CMachineElementComponent::S_ClassName;}
    iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override{
        iCAX::Behaviour::CBehaviourSchedule s;s.ExecutionOrder=100;s.RunBefore={"CTransformBehaviour","CRenderInstanceBehaviour","CMachineColliderBehaviour"};return s;
    }
protected:
    void OnAwake(iCAX::Database::CComponentBase& c,const iCAX::Application::IApplicationContext&,
        const iCAX::Product::IProductContext&,iCAX::Project::IProjectContext&,iCAX::Project::ISceneContext&) override{Sync(c);}
    void OnUpdate(iCAX::Database::CComponentBase& c,const iCAX::Application::IApplicationContext&,
        const iCAX::Product::IProductContext&,iCAX::Project::IProjectContext&,iCAX::Project::ISceneContext&,const double&,const double&) override{Sync(c);}
private:
    static void Sync(iCAX::Database::CComponentBase& c){if(auto e=c.GetEntity())iCAX::CAM::SynchronizeMachineElementPose(*e);}
};
}
