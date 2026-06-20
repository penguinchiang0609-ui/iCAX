#include "pch.h"
#include "IMetaRegistry.h"
#include "MetaRegistry.h"
#include "IRepository.h"
#include "MetaRegistrationCatalog.h"

//! 获取全局元数据注册表
std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::CreateMetaRegistry()
{
    return std::make_shared<CMetaRegistry>();
}

std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::GetGlobalMetaRegistry()
{
    static std::shared_ptr<IMetaRegistry> _pMetaRegistry = CreateMetaRegistry();
    static size_t _nReplayedRegistrationCount = 0;
    _nReplayedRegistrationCount = CMetaRegistrationCatalog::ReplayFrom(_nReplayedRegistrationCount, *_pMetaRegistry);
    return _pMetaRegistry;
}

std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::ResolveMetaRegistryForComponent(IN const CComponentBase& Component_)
{
    auto _pEntity = Component_.GetEntity();
    if (_pEntity)
    {
        auto _pRepository = _pEntity->GetRepository();
        if (_pRepository)
        {
            auto _pMeta = _pRepository->GetMetaRegistry();
            if (_pMeta)
            {
                return _pMeta;
            }
        }
    }

    return GetGlobalMetaRegistry();
}
