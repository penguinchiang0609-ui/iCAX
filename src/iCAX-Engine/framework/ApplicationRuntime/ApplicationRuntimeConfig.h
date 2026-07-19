#pragma once
#include "ApplicationRuntimeExport.h"
#include "ApplicationContext/ApplicationDescriptor.h"
#include "ApplicationContext/ApplicationPaths.h"
#include "ProductContext/ProductDefinition.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 配置文件
        * @details ApplicationRuntimeConfig 是运行时启动参数，必须在 Start 前设置。
        */
        struct _APPLICATION_RUNTIME_EXP ApplicationRuntimeConfig
        {
            /*
            * @brief 应用程序设置路径
            */
            std::string strApplicationSettingsPath;

            /*
            * @brief 应用描述
            */
            iCAX::Application::CApplicationDescriptor Descriptor;

            /*
            * @brief 应用路径
            */
            iCAX::Application::CApplicationPaths Paths;

            /*
            * @brief 运行时支持的产品定义
            */
            std::vector<iCAX::Product::CProductDefinition> Products;

            /*
            * @brief 运行时启动时自动启动的产品。为空表示只启动应用运行时。
            */
            std::optional<std::string> StartupProductID;

            /*
            * @brief 后台工作线程帧间隔，单位毫秒
            */
            uint32_t nFrameIntervalMilliseconds = 16;
        };
    }
}
