#pragma once

#include "ApplicationRuntimeExport.h"
#include "SDO/SDOMethod.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        inline constexpr const char* kAppSDOName = "App";
        inline constexpr uint32_t kAppSDOCode = iCAX::Interaction::InteractionNameHash32(kAppSDOName);

        inline constexpr const char* kAppGetStateName = "GetState";
        inline constexpr const char* kAppListProductsName = "ListProducts";
        inline constexpr const char* kAppStartProductName = "StartProduct";
        inline constexpr const char* kAppStopProductName = "StopProduct";
        inline constexpr const char* kAppResolveProjectFileName = "ResolveProjectFile";
        inline constexpr const char* kAppOpenProjectFileName = "OpenProjectFile";

        inline constexpr uint64_t kAppGetStateMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppGetStateName);
        inline constexpr uint64_t kAppListProductsMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppListProductsName);
        inline constexpr uint64_t kAppStartProductMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppStartProductName);
        inline constexpr uint64_t kAppStopProductMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppStopProductName);
        inline constexpr uint64_t kAppResolveProjectFileMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppResolveProjectFileName);
        inline constexpr uint64_t kAppOpenProjectFileMethodCode = iCAX::Interaction::MakeSDOMethodCode(kAppSDOName, kAppOpenProjectFileName);

        /*
        * @brief 编码 ApplicationRuntime SDO 调用负载。
        * @param [in] Payload_ Variant 负载。
        * @return UTF-8 文本字节数组。
        * @details 使用 Data::VariantSerializer，不引入 flatbuffer/interop。
        */
        _APPLICATION_RUNTIME_EXP std::vector<uint8_t> EncodeApplicationRuntimePayload(IN const iCAX::Data::Variant& Payload_);

        /*
        * @brief 解码 ApplicationRuntime SDO 调用负载。
        * @param [in] Payload_ UTF-8 文本字节数组。
        * @return Variant 负载；空负载返回空对象。
        */
        _APPLICATION_RUNTIME_EXP iCAX::Data::Variant DecodeApplicationRuntimePayload(IN const std::vector<uint8_t>& Payload_);
    }
}
