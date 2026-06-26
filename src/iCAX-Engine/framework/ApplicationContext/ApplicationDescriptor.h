#pragma once

#include "ApplicationContextExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用描述。
        * @details
        *   这里保存应用级只读信息和可识别项目文件的基本能力。
        *   产品级 magic 由 ProductDefinition 描述；ApplicationDescriptor 只表达应用整体支持范围。
        */
        struct _APPLICATION_CONTEXT_EXP CApplicationDescriptor final
        {
            std::string AppID; //!< 应用 ID。
            std::string AppName; //!< 应用展示名称。
            std::string AppVersion; //!< 应用版本。
            std::vector<std::string> SupportedProjectMagics; //!< 应用可识别的项目 magic 集合。
            std::vector<uint32_t> SupportedProjectVersions; //!< 应用可识别的项目格式版本集合。
            std::string DefaultProjectExtension; //!< 默认项目扩展名。

            /*
            * @brief 判断应用是否支持指定项目 magic。
            * @param [in] strMagic_ 项目文件 magic。
            * @return true 表示支持。
            */
            bool SupportsProjectMagic(IN const std::string& strMagic_) const;

            /*
            * @brief 判断应用是否支持指定项目版本。
            * @param [in] nVersion_ 项目格式版本。
            * @return true 表示支持。
            */
            bool SupportsProjectVersion(IN uint32_t nVersion_) const;
        };
    }
}
