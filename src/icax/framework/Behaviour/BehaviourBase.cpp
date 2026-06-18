#include "pch.h"
#include "BehaviourBase.h"
#include "IUniverse.h"

void iCAX::Behaviour::CBehaviourBase::Awake(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    Context_.GetDatabase().GetView().BuildCache(Component_.GetComponentClass(), false);
    OnAwake(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Start(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnStart(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Enable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnEnable(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::PreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnPreUpdate(Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Update(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnUpdate(Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::PostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnPostUpdate(Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Disable(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnDisable(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Destory(IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnDestroy(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Modifing(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
    OnModifing(Component_, NewValues_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Modified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
    OnModified(Component_, NewValues_, Context_);
}
