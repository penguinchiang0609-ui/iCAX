#include "pch.h"
#include "IBehaviourRegistry.h"
#include "BehaviourRegistry.h"
#include "BehaviourRegistrationCatalog.h"

std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> iCAX::Behaviour::CreateBehaviourRegistry()
{
    return std::make_shared<CBehaviourRegistry>();
}

//! 获取全局行为注册表
std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> iCAX::Behaviour::GetGlobalBehaviourRegistry()
{
    static std::shared_ptr<IBehaviourRegistry> s_pBehaviourRegistry = CreateBehaviourRegistry();
    static size_t s_nReplayedRegistrationCount = 0;
    s_nReplayedRegistrationCount = CBehaviourRegistrationCatalog::ReplayFrom(s_nReplayedRegistrationCount, *s_pBehaviourRegistry);
    return s_pBehaviourRegistry;
}
