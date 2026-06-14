#include "pch.h"
#include "TaskService.h"

iCAX::Core::TaskService::TaskService(IN const std::shared_ptr<ILogService>& pLogService_)
    : m_pLogService(pLogService_)
{
}

//! 加载
void iCAX::Core::TaskService::OnLoad()
{
    m_pool = std::make_unique<BS::thread_pool<>>(8);
}

//! 卸载
void iCAX::Core::TaskService::OnUnload()
{
    m_pool.reset();
    m_pLogService = nullptr;
}

