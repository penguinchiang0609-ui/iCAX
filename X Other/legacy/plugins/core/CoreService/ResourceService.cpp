#include "pch.h"
#include "ResourceService.h"
#include <string>
#include <format>

//! 构造函数
iCAX::Core::CResourceService::CResourceService(IN const std::shared_ptr<ILogService>& pLogService_)
    : m_mapResources()
    , m_pLogService(pLogService_)
{
}

//! 析构函数
iCAX::Core::CResourceService::~CResourceService()
{
}

//! 加载
void iCAX::Core::CResourceService::OnLoad()
{
}

//! 卸载
void iCAX::Core::CResourceService::OnUnload()
{
    m_pLogService = nullptr;
}

//! 根据类型以及资源的KEY获取资源
std::shared_ptr<void> iCAX::Core::CResourceService::GetResource(IN const std::type_index& nType_, IN const std::string& strKey_)
{
    if (m_mapResources.contains(nType_) && m_mapResources[nType_].contains(strKey_))
    {
        return m_mapResources[nType_][strKey_];
    }
    m_pLogService->Warn(std::format("Resource not exist, type={}, key={}", nType_.name(), strKey_));
    return nullptr;
}

//! 根据类型以及资源的KEY设置资源
void iCAX::Core::CResourceService::SetResource(IN const std::type_index& nType_, IN const std::string& strKey_, IN const std::shared_ptr<void>& pResource_)
{
    if (!m_mapResources.contains(nType_))
    {
        m_mapResources[nType_] = std::unordered_map<std::string, std::shared_ptr<void>>();
    }
    m_mapResources[nType_][strKey_] = pResource_;
    m_pLogService->Trace(std::format("Resource set, type={}, key={}", nType_.name(), strKey_));
}

//! 根据类型以及资源的KEY卸载资源
void iCAX::Core::CResourceService::DeleteResource(IN const std::type_index& nType_, IN const std::string& strKey_)
{
    if (m_mapResources.contains(nType_))
    {
        m_mapResources[nType_].erase(strKey_);
    }
    m_pLogService->Trace(std::format("Resource delete, type={}, key={}", nType_.name(), strKey_));
}
