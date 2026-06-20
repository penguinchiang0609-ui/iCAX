#pragma once

#include "ProductExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        /*
        * @brief 产品模块组
        * @details 一组通常共同发布的组件、行为、服务和命令模块。
        */
        struct _PRODUCT_EXP CProductModuleGroup final
        {
            std::string strComponentPath;
            std::string strBehaviourPath;
            std::string strServicePath;
            std::string strCommandPath;
        };

        /*
        * @brief 产品需要加载的模块
        */
        struct _PRODUCT_EXP CProductModules final
        {
            std::vector<std::string> ComponentModules;
            std::vector<std::string> BehaviourModules;
            std::vector<std::string> ServiceModules;
            std::vector<std::string> CommandModules;
            std::vector<CProductModuleGroup> ModuleGroups;
        };

        /*
        * @brief 产品项目文件定义
        * @details 描述产品可识别的项目文件格式，是产品静态能力的一部分。
        */
        struct _PRODUCT_EXP CProductFileDefinition final
        {
            std::string Magic;
            std::string FormatVersion;
            std::vector<std::string> FileExtensions;
            uint64_t MagicOffset = 0;
            uint64_t ProbeBytes = 256;
        };

        /*
        * @brief 产品定义
        * @details ApplicationHost 只根据产品定义启动产品运行时，不直接打开项目。
        */
        struct _PRODUCT_EXP CProductDefinition final
        {
            std::string ProductID;
            std::string ProductName;
            std::string ProductVersion;
            std::string FrontendEntry;
            std::string DefaultProjectStartupComponent;
            CProductFileDefinition ProjectFile;
            CProductModules Modules;
        };
    }
}
