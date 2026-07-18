#pragma once

#include "ApplicationHostExport.h"
#include "Facades/FacadeMethod.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace ApplicationHost
    {
        inline constexpr const char* kAppFacadeName = "App";
        inline constexpr uint32_t kAppFacadeCode = iCAX::Interaction::InteractionNameHash32(kAppFacadeName);

        inline constexpr const char* kAppGetStateName = "GetState";
        inline constexpr const char* kAppListProductsName = "ListProducts";
        inline constexpr const char* kAppStartProductName = "StartProduct";
        inline constexpr const char* kAppStopProductName = "StopProduct";
        inline constexpr const char* kAppResolveProjectFileName = "ResolveProjectFile";
        inline constexpr const char* kAppOpenProjectFileName = "OpenProjectFile";

        inline constexpr uint64_t kAppGetStateMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppGetStateName);
        inline constexpr uint64_t kAppListProductsMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppListProductsName);
        inline constexpr uint64_t kAppStartProductMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppStartProductName);
        inline constexpr uint64_t kAppStopProductMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppStopProductName);
        inline constexpr uint64_t kAppResolveProjectFileMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppResolveProjectFileName);
        inline constexpr uint64_t kAppOpenProjectFileMethodCode = iCAX::Interaction::MakeFacadeMethodCode(kAppFacadeName, kAppOpenProjectFileName);

        /*
        * @brief 编码 ApplicationHost Facade 调用负载。
        * @param [in] Payload_ Variant 负载。
        * @return UTF-8 文本字节数组。
        * @details 使用 Data::VariantSerializer，不引入 flatbuffer/interop。
        */
        _APPLICATION_HOST_EXP std::vector<uint8_t> EncodeApplicationHostPayload(IN const iCAX::Data::Variant& Payload_);

        /*
        * @brief 解码 ApplicationHost Facade 调用负载。
        * @param [in] Payload_ UTF-8 文本字节数组。
        * @return Variant 负载；空负载返回空对象。
        */
        _APPLICATION_HOST_EXP iCAX::Data::Variant DecodeApplicationHostPayload(IN const std::vector<uint8_t>& Payload_);
    }
}
