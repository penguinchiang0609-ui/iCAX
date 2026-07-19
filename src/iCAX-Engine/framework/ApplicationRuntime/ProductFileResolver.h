#pragma once

#include "ApplicationRuntimeExport.h"
#include "ProductContext/ProductDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        enum class EProductFileResolveStatus : uint8_t
        {
            NotFound = 0, //!< 未识别出产品。
            Resolved = 1, //!< 已唯一识别出产品。
        };

        /*
        * @brief 项目文件产品识别结果。
        */
        struct _APPLICATION_RUNTIME_EXP CProductFileResolveResult final
        {
            std::string ProjectPath; //!< 项目文件路径。
            EProductFileResolveStatus Status = EProductFileResolveStatus::NotFound; //!< 识别状态。
            std::string ProductID; //!< 唯一识别出的产品 ID。
            std::vector<std::string> CandidateProductIDs; //!< 候选产品 ID 列表。
            bool bMatchedByMagic = false; //!< 是否通过 magic 命中。
        };

        /*
        * @brief 项目文件产品识别器。
        * @details
        *   当前只使用 ProductDefinition.ProjectFile.Magic 识别产品。
        *   扩展名只作为 UI 文件选择参考，不作为唯一判断依据。
        */
        class _APPLICATION_RUNTIME_EXP CProductFileResolver final
        {
        public:
            /*
            * @brief 识别项目文件所属产品。
            * @param [in] strProjectPath_ 项目文件路径，必须存在且是普通文件。
            * @param [in] Products_ 可用产品定义列表。
            * @return 识别结果。
            * @throws std::logic_error 多个产品 magic 同时命中时抛出。
            */
            static CProductFileResolveResult Resolve(
                IN const std::string& strProjectPath_,
                IN const std::vector<iCAX::Product::CProductDefinition>& Products_);
        };
    }
}
