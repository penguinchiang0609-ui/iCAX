#include "pch.h"
#include "IUniverse.h"
#include "Universe.h"

#include <utility>

//!< 生成宇宙
std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::GenerateUniverse()
{
    return GenerateUniverse(nullptr);
}

std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::GenerateUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_)
{
    auto _pUniverse = std::make_shared<CUniverse>(std::move(pRegistry_));
    _pUniverse->Initialize();
    return _pUniverse;
}
