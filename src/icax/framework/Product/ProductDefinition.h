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
            std::string strComponentPath; //!< 组件模块路径。
            std::string strBehaviourPath; //!< 行为模块路径。
            std::string strServicePath;   //!< 服务模块路径。
            std::string strCommandPath;   //!< 命令模块路径。
        };

        /*
        * @brief 产品需要加载的模块
        */
        struct _PRODUCT_EXP CProductModules final
        {
            std::vector<std::string> ComponentModules; //!< 单独组件模块列表。
            std::vector<std::string> BehaviourModules; //!< 单独行为模块列表。
            std::vector<std::string> ServiceModules;   //!< 单独服务模块列表。
            std::vector<std::string> CommandModules;   //!< 单独命令模块列表。
            std::vector<CProductModuleGroup> ModuleGroups; //!< 成组发布的模块列表。
        };

        /*
        * @brief 产品项目文件定义
        * @details 描述产品可识别的项目文件格式，是产品静态能力的一部分。
        */
        struct _PRODUCT_EXP CProductFileDefinition final
        {
            std::string Magic; //!< 项目文件 magic，必须存在且在 ApplicationHost 内唯一。
            std::string FormatVersion; //!< 项目文件格式版本文本。
            std::vector<std::string> FileExtensions; //!< 建议扩展名，仅用于展示或文件选择，不作为唯一判定依据。
            uint64_t MagicOffset = 0; //!< magic 在文件中的偏移。
            uint64_t ProbeBytes = 256; //!< 为识别 magic 读取的探测字节数。
        };

        /*
        * @brief 产品定义
        * @details ApplicationHost 只根据产品定义启动产品运行时，不直接打开项目。
        */
        struct _PRODUCT_EXP CProductDefinition final
        {
            std::string ProductID; //!< 产品唯一 ID。
            std::string ProductName; //!< 产品展示名称。
            std::string ProductVersion; //!< 产品版本。
            std::string FrontendEntry; //!< 前端入口路径或标识。
            std::string DefaultProjectStartupComponent; //!< 打开项目后默认绑定的启动组件。
            CProductFileDefinition ProjectFile; //!< 产品项目文件识别规则。
            CProductModules Modules; //!< 产品需要加载的模块定义。
        };
    }
}
