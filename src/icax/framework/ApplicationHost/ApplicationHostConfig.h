#pragma once
#include "ApplicationHostExport.h"
#include "ApplicationContext/ApplicationDescriptor.h"
#include "ApplicationContext/ApplicationPaths.h"
#include <string>


namespace iCAX
{
    namespace ApplicationHost
    {
        /*
        * @brief 配置文件
        */
        struct _APPLICATION_HOST_EXP ApplicationHostConfig
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
            * @brief 插件清单路径
            */
            std::string strPluginManifestPath;

            /*
            * @brief 启动项配置信息
            */
            std::string strStartupPath;
        };
    }
}
