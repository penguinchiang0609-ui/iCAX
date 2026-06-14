#pragma once
#include "WindowService.h"
#include "IWindowService.h"
#include <unordered_map>

namespace iCAX
{
    namespace Services
    {
        class _WINDOWSERVICE_EXP WinWindowService : public IWindowService
        {
        public:
            /*
            * @brief 构造函数
            */
            WinWindowService();

            /*
            * @brief 析构函数
            */
            virtual ~WinWindowService();

        public:
            /*
            * @brief 注册窗体
            * @param [in] pHwnd_
            */
            virtual void RegistWindow(IN const HWND& pHwnd_) override;

            /*
            * @brief 注销窗体
            * @param [in] pHwnd_
            */
            virtual void DeregistWindow(IN const HWND& pHwnd_) override;

            /*
            * @brief 获取当前活动窗口的句柄
            * @return HWND
            */
            virtual HWND GetActiveWindowHandle() const override;

        public:
            /*
            * @brief 每帧后触发
            */
            virtual void UpdateAvtive() override;

        private:
            HWND m_hwndActive;                              //!< 当前焦点窗口的句柄
            std::unordered_map<HWND, HWND> m_mapHwnd;       //!< 管理多个窗口句柄
        };
    }
}