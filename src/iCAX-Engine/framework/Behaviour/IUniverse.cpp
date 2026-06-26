#include "pch.h"
#include "IUniverse.h"
#include "Universe.h"

#include <utility>

std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::GenerateUniverse(IN std::shared_ptr<IBehaviourRegistry> pRegistry_)
{
    return std::make_shared<CUniverse>(std::move(pRegistry_));
}
