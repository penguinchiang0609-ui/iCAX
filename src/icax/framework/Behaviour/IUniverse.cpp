#include "pch.h"
#include "IUniverse.h"
#include "Universe.h"
#include "UniverseContext.h"

//!< 生成宇宙
std::shared_ptr<iCAX::Behaviour::IUniverse> iCAX::Behaviour::GenerateUniverse(IN const std::shared_ptr<iCAX::Database::IRepository>& pRepository_,
    IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplicationSetting_)
{
    auto _pUniverse = std::make_shared<CUniverse>(std::make_shared<CUniverseContext>(pRepository_, pApplicationSetting_));
    _pUniverse->Initialize();
    return _pUniverse;
}
