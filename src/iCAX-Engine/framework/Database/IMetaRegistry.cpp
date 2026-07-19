#include "pch.h"
#include "IMetaRegistry.h"
#include "MetaRegistry.h"
#include "IRepository.h"

//! 创建独立元数据注册表实例
std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::CreateMetaRegistry()
{
    return std::make_shared<CMetaRegistry>();
}

std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::ResolveMetaRegistryForComponent(IN const CComponentBase& Component_)
{
    auto _pEntity = Component_.GetEntity();
    if (!_pEntity)
    {
        throw std::runtime_error("Component has no entity: " + Component_.GetComponentClass());
    }

    auto _pRepository = _pEntity->GetRepository();
    if (!_pRepository)
    {
        throw std::runtime_error("Component entity has no repository: " + Component_.GetComponentClass());
    }

    auto _pMeta = _pRepository->GetMetaRegistry();
    if (!_pMeta)
    {
        throw std::runtime_error("Repository has no meta registry: " + Component_.GetComponentClass());
    }
    return _pMeta;
}
