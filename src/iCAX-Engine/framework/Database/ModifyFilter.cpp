#include "pch.h"
#include "ModifyFilter.h"

#include "ComponentBase.h"
#include "IEntity.h"
#include "IRepository.h"
#include "IMetaRegistry.h"

namespace
{
    std::shared_ptr<iCAX::Database::IMetaRegistry> ResolveMetaRegistryForEntity(IN const iCAX::Database::IEntity& Entity_)
    {
        auto _pRepository = Entity_.GetRepository();
        if (!_pRepository)
        {
            return nullptr;
        }
        return _pRepository->GetMetaRegistry();
    }
}

bool iCAX::Database::CModifyFilter::AllowAttachComponent(IN const IEntity& Entity_, IN const std::string& strComponentClass_, OUT std::string& strError_)
{
    strError_.clear();
    auto _pMeta = ResolveMetaRegistryForEntity(Entity_);
    if (!_pMeta)
    {
        strError_ = "Entity repository has no meta registry";
        return false;
    }
    return _pMeta->AllowAttachByName(Entity_, strComponentClass_, strError_);
}

bool iCAX::Database::CModifyFilter::AllowRemoveComponent(IN const IEntity& Entity_, IN const std::string& strComponentClass_, OUT std::string& strError_)
{
    strError_.clear();
    auto _pMeta = ResolveMetaRegistryForEntity(Entity_);
    if (!_pMeta)
    {
        strError_ = "Entity repository has no meta registry";
        return false;
    }
    return _pMeta->AllowRemoveByName(Entity_, strComponentClass_, strError_);
}

bool iCAX::Database::CModifyFilter::AllowModifyComponent(IN const CComponentBase& Component_, IN const iCAX::Data::PropertySet& Properties_, OUT std::string& strError_)
{
    strError_.clear();
    if (Properties_.empty())
    {
        return true;
    }

    auto _pMeta = ResolveMetaRegistryForComponent(Component_);
    return _pMeta->AllowModify(Component_, Properties_, strError_);
}
