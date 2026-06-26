#include "pch.h"
#include "BehaviourBase.h"
#include "IUniverse.h"

void iCAX::Behaviour::CBehaviourBase::Awake(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnAwake(Component_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Start(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnStart(Component_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Enable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnEnable(Component_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::PreUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnPreUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::Update(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::PostUpdate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    OnPostUpdate(Component_, ApplicationContext_, ProductContext_, ProjectContext_, nDeltaTime_, nTotalTime_);
}

void iCAX::Behaviour::CBehaviourBase::Disable(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnDisable(Component_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Destroy(
    IN const CComponentDestroyInfo& DestroyInfo_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnDestroy(DestroyInfo_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::DestroyImmediate(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnDestroyImmediate(Component_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Modifying(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnModifying(Component_, NewValues_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Modified(
    IN iCAX::Database::CComponentBase& Component_,
    IN const iCAX::Data::PropertySet& NewValues_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_)
{
    OnModified(Component_, NewValues_, ApplicationContext_, ProductContext_, ProjectContext_);
}

void iCAX::Behaviour::CBehaviourBase::Detach()
{
    OnDetach();
}
