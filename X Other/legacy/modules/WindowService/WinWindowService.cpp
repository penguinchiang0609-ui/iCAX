#include "pch.h"
#include "WinWindowService.h"

//!< 构造函数
iCAX::Services::WinWindowService::WinWindowService()
    : m_hwndActive(nullptr)
    , m_mapHwnd()
{
}

//!< 析构函数
iCAX::Services::WinWindowService::~WinWindowService()
{
}

void iCAX::Services::WinWindowService::RegistWindow(IN const HWND& pHwnd_)
{
    if (pHwnd_ == nullptr || !IsWindow(pHwnd_))
    {
        return;
    }
    m_mapHwnd[pHwnd_] = pHwnd_;  // 将窗口句柄加入窗口池
}

void iCAX::Services::WinWindowService::DeregistWindow(IN const HWND& pHwnd_)
{
    auto it = m_mapHwnd.find(pHwnd_);
    if (it != m_mapHwnd.end())
    {
        m_mapHwnd.erase(it);  // 从窗口池移除
        if (m_hwndActive == pHwnd_)
        {
            m_hwndActive = nullptr;  // 如果移除的是当前活动窗口，重置活动窗口
        }
    }
}

HWND iCAX::Services::WinWindowService::GetActiveWindowHandle() const
{
    return m_hwndActive;
}

void iCAX::Services::WinWindowService::UpdateAvtive()
{
    // 获取当前活动窗口
    HWND hwnd = GetForegroundWindow();  // 获取当前用户操作的窗口句柄
    if (hwnd != nullptr && IsWindow(hwnd) && m_mapHwnd.find(hwnd) != m_mapHwnd.end())
    {
        m_hwndActive = hwnd;  // 更新活动窗口
    }
    else
    {
        m_hwndActive = nullptr;
    }
}