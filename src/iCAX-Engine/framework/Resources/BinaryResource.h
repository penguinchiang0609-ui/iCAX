#pragma once

#include "ResourcesExport.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 通用二进制资源。
        * @details
        *   用于把外部文件按项目资源嵌入，例如 CAD 原文件、图片、文本原始内容等。
        *   它只保存数据和元数据，不提供解析行为。
        */
        struct _RESOURCES_EXP CBinaryResource final
        {
            inline static constexpr const char* kResourceTypeName = "resource.binary";

            CBinaryResource();
            ~CBinaryResource();

            std::string SourcePath; //!< 原始来源路径或 URI。
            std::string DisplayName; //!< UI 展示名。
            std::string FileExtension; //!< 规范化扩展名。
            std::vector<uint8_t> Content; //!< 原始二进制内容。
            std::map<std::string, std::string> Metadata; //!< 格式、导入器、单位等元数据。
            uint64_t nVersion = 0; //!< 资源内容版本。
        };
    }
}
