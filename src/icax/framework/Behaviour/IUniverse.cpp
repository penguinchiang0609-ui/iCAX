#include "pch.h"
#include "IUniverse.h"
#include "Universe.h"

//!< 生成宇宙
std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::GenerateUniverse()
{
    auto _pUniverse = std::make_shared<CUniverse>();
    _pUniverse->Initialize();
    return _pUniverse;
}
