#include "pch.h"
#include "BehaviourBase.h"
#include "IWorld.h"
#include "IUniverse.h"

void iCAX::Behaviour::CBehaviourBase::Awake(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    Context_.GetDatabase().GetDomain(World_.GetID())->GetView().BuildCache(Component_.GetComponentClass(), false);
    OnAwake(Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Start(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnStart(World_, Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Enable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnEnable(World_, Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::PreUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnPreUpdate(World_, Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Update(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnUpdate(World_, Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::PostUpdate(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const double& nDeltaTime_, IN const double& nTotalTime_, IN const IUniverseContext& Context_)
{
    OnPostUpdate(World_, Component_, nDeltaTime_, nTotalTime_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Disable(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnDisable(World_, Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Destory(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const IUniverseContext& Context_)
{
    OnDestroy(World_, Component_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Modifing(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
    OnModifing(World_, Component_, NewValues_, Context_);
}

void iCAX::Behaviour::CBehaviourBase::Modified(IN IWorld& World_, IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const IUniverseContext& Context_)
{
    OnModified(World_, Component_, NewValues_, Context_);
}