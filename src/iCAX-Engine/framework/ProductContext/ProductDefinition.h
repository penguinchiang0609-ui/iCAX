#pragma once

#include "ProductContextExport.h"
#include "PDO/IPDOHub.h"

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
        struct _PRODUCT_CONTEXT_EXP CProductModuleGroup final
        {
            std::string strComponentPath; //!< 组件模块路径。
            std::string strBehaviourPath; //!< 行为模块路径。
            std::string strServicePath;   //!< 服务模块路径。
            std::string strCommandPath;   //!< 命令模块路径。
        };

        /*
        * @brief 产品需要加载的模块
        */
        struct _PRODUCT_CONTEXT_EXP CProductModules final
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
        struct _PRODUCT_CONTEXT_EXP CProductFileDefinition final
        {
            std::string Magic; //!< 项目文件 magic，必须存在且在 ApplicationHost 内唯一。
            std::string FormatVersion; //!< 项目文件格式版本文本。
            std::string QuickSaveLogMagic; //!< 快速保存日志 magic；为空时由产品运行时基于 Magic 派生。
            uint32_t QuickSaveLogVersion = 1; //!< 快速保存日志格式版本，只用于严格匹配，不做迁移。
            std::vector<std::string> FileExtensions; //!< 建议扩展名，仅用于展示或文件选择，不作为唯一判定依据。
            uint64_t MagicOffset = 0; //!< magic 在文件中的偏移。
            uint64_t ProbeBytes = 256; //!< 为识别 magic 读取的探测字节数。
        };

        /*
        * @brief 产品定义
        * @details ApplicationHost 只根据产品定义启动产品运行时，不直接打开项目。
        */
        struct _PRODUCT_CONTEXT_EXP CProductDefinition final
        {
            std::string ProductID; //!< 产品唯一 ID。
            std::string ProductName; //!< 产品展示名称。
            std::string ProductVersion; //!< 产品版本。
            std::string FrontendEntry; //!< 前端入口路径或标识。
            std::string DefaultProjectStartupComponent; //!< 打开项目后默认绑定的启动组件。
            CProductFileDefinition ProjectFile; //!< 产品项目文件识别规则。
            CProductModules Modules; //!< 产品需要加载的模块定义。
            bool bEnablePDOHub = false; //!< true 表示项目默认创建一块可动态分配 slot 的 PDO Arena。
            iCAX::PDO::CPDOHubCreateInfo PDOHubCreateInfo; //!< 默认项目 PDOHub 创建参数。
        };

        /*
        * @brief 判断 ProductID 是否可安全用于内存映射名、日志上下文和产品数据目录。
        * @details 允许字母、数字、点、下划线和短横线；禁止空、"."、".." 和路径穿越片段。
        */
        inline bool IsValidProductID(IN const std::string& strProductID_)
        {
            if (strProductID_.empty() || strProductID_ == "." || strProductID_ == "..")
            {
                return false;
            }
            if (strProductID_.find("..") != std::string::npos)
            {
                return false;
            }

            for (const auto _Char : strProductID_)
            {
                const bool _bDigit = _Char >= '0' && _Char <= '9';
                const bool _bLower = _Char >= 'a' && _Char <= 'z';
                const bool _bUpper = _Char >= 'A' && _Char <= 'Z';
                const bool _bSymbol = _Char == '.' || _Char == '_' || _Char == '-';
                if (!_bDigit && !_bLower && !_bUpper && !_bSymbol)
                {
                    return false;
                }
            }
            return true;
        }
    }
}
