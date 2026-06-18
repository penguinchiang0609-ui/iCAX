#pragma once
#include "ApplicationHostExport.h"
#include "ApplicationContext/ApplicationDescriptor.h"
#include "ApplicationContext/ApplicationPaths.h"
#include <cstdint>
#include <string>
#include <vector>


namespace iCAX
{
    namespace ApplicationHost
    {
        /*
        * @brief 宿主启动时自动打开的项目
        */
        struct _APPLICATION_HOST_EXP ApplicationHostStartupProject final
        {
            std::string strProductID;
            std::string strProjectName;
            std::string strProjectPath;
        };

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
            * @brief 产品目录路径，目录下每个产品目录可包含 manifest.json
            */
            std::string strProductCatalogPath;

            /*
            * @brief 显式产品清单路径列表
            */
            std::vector<std::string> ProductManifestPaths;

            /*
            * @brief 宿主启动时自动打开的项目列表。为空表示只启动工作区，不打开项目。
            */
            std::vector<ApplicationHostStartupProject> StartupProjects;

            /*
            * @brief 后台工作线程帧间隔，单位毫秒
            */
            uint32_t nFrameIntervalMilliseconds = 16;
        };
    }
}
