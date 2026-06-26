#include "pch.h"
#include "IBehaviourRegistry.h"
#include "BehaviourRegistry.h"

iCAX::Behaviour::CBehaviourDescriptor::CBehaviourDescriptor()
    : Type(typeid(void))
    , BehaviourClass()
    , ComponentClass()
    , Factory()
{
}

std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> iCAX::Behaviour::CreateBehaviourRegistry()
{
    return std::make_shared<CBehaviourRegistry>();
}
