#pragma once
#include "WindowService.h"
#include <windows.h>
namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 窗口服务
        */
        class _WINDOWSERVICE_EXP IWindowService
        {
        public:
            /*
            * @brief 构造函数
            */
            IWindowService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IWindowService() = default;

        public:
            /*
            * @brief 注册窗体
            * @param [in] pHwnd_
            */
            virtual void RegistWindow(IN const HWND& pHwnd_) = 0;

            /*
            * @brief 注销窗体
            * @param [in] pHwnd_
            */
            virtual void DeregistWindow(IN const HWND& pHwnd_) = 0;

            /*
            * @brief 获取当前活动窗口的句柄
            * @return HWND
            */
            virtual HWND GetActiveWindowHandle() const = 0;

            /*
            * @brief 每帧后触发
            */
            virtual void UpdateAvtive()  = 0;
        };
    }
}