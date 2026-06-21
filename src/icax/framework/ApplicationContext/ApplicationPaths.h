#pragma once

#include "ApplicationContextExport.h"

#include <string>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用目录集合。
        * @details
        *   这些路径由宿主启动时注入，供配置、缓存、日志等框架模块使用。
        */
        struct _APPLICATION_CONTEXT_EXP CApplicationPaths final
        {
            std::string InstallDirectory; //!< 安装目录。
            std::string UserConfigDirectory; //!< 用户配置目录。
            std::string CacheDirectory; //!< 缓存目录。
            std::string TempDirectory; //!< 临时目录。
            std::string LogDirectory; //!< 日志目录。
        };
    }
}
