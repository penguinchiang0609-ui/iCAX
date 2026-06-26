#pragma once

#include "ApplicationHostExport.h"
#include "CommandHandler/CommandRoute.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace ApplicationHost
    {
        inline constexpr const char* kAppCommandMainName = "App";
        inline constexpr uint32_t kAppCommandMainCode = iCAX::Command::CommandHash32(kAppCommandMainName);

        inline constexpr const char* kAppGetStateName = "GetState";
        inline constexpr const char* kAppListProductsName = "ListProducts";
        inline constexpr const char* kAppStartProductName = "StartProduct";
        inline constexpr const char* kAppStopProductName = "StopProduct";
        inline constexpr const char* kAppResolveProjectFileName = "ResolveProjectFile";
        inline constexpr const char* kAppOpenProjectFileName = "OpenProjectFile";

        inline constexpr uint64_t kAppGetStateCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppGetStateName);
        inline constexpr uint64_t kAppListProductsCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppListProductsName);
        inline constexpr uint64_t kAppStartProductCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppStartProductName);
        inline constexpr uint64_t kAppStopProductCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppStopProductName);
        inline constexpr uint64_t kAppResolveProjectFileCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppResolveProjectFileName);
        inline constexpr uint64_t kAppOpenProjectFileCommand = iCAX::Command::MakeCommandCode(kAppCommandMainName, kAppOpenProjectFileName);

        /*
        * @brief 编码 ApplicationHost 命令负载。
        * @param [in] Payload_ Variant 负载。
        * @return UTF-8 文本字节数组。
        * @details 使用 Data::VariantSerializer，不引入 flatbuffer/interop。
        */
        _APPLICATION_HOST_EXP std::vector<uint8_t> EncodeApplicationHostPayload(IN const iCAX::Data::Variant& Payload_);

        /*
        * @brief 解码 ApplicationHost 命令负载。
        * @param [in] Payload_ UTF-8 文本字节数组。
        * @return Variant 负载；空负载返回空对象。
        */
        _APPLICATION_HOST_EXP iCAX::Data::Variant DecodeApplicationHostPayload(IN const std::vector<uint8_t>& Payload_);
    }
}
