#include "pch.h"
#include "IMetaRegistry.h"
#include "MetaRegistry.h"

//! 获取全局元数据注册表
std::shared_ptr<iCAX::Database::IMetaRegistry> iCAX::Database::GetGlobalMetaRegistry()
{
    static std::shared_ptr<IMetaRegistry> _pMetaRegistry = std::make_shared<CMetaRegistry>();
    return _pMetaRegistry;
}
