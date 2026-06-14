#include "pch.h"
#include "IBehaviourRegistry.h"
#include "BehaviourRegistry.h"

//! 获取全局行为注册表
std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> iCAX::Behaviour::GetGlobalBehaviourRegistry()
{
    static std::shared_ptr<IBehaviourRegistry> s_pBehaviourRegistry = std::make_shared<CBehaviourRegistry>();
    return s_pBehaviourRegistry;
}
