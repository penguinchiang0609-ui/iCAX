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
            std::string UserDataDirectory; //!< 用户业务数据根目录。
            std::string BrowserDataDirectory; //!< 浏览器运行数据目录，不承载业务数据。
            std::string CacheDirectory; //!< 缓存目录。
            std::string TempDirectory; //!< 临时目录。
            std::string ResourceVersionDirectory; //!< 资源历史版本临时根目录；为空时使用 TempDirectory/ResourceVersions。
            std::string LogDirectory; //!< 日志目录。
        };

        /*
        * @brief 解析当前用户的 iCAX 数据根目录。
        * @details 优先使用 ICAX_USER_DATA_ROOT；否则使用 %LOCALAPPDATA%/iCAX。
        */
        _APPLICATION_CONTEXT_EXP std::string ResolveDefaultUserDataDirectory();
    }
}
