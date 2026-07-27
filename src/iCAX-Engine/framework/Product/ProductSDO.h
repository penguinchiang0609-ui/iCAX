#pragma once

#include "SDO/SDOMethod.h"
#include "ProductExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        inline constexpr const char* kProductSDOName = "Product";
        inline constexpr uint32_t kProductSDOCode = iCAX::Interaction::InteractionNameHash32(kProductSDOName);

        inline constexpr const char* kProductGetStateName = "GetState";
        inline constexpr const char* kProductListProjectCatalogsName = "ListProjectCatalogs";
        inline constexpr const char* kProductOpenProjectCatalogName = "OpenProjectCatalog";
        inline constexpr const char* kProductCloseProjectCatalogName = "CloseProjectCatalog";

        inline constexpr uint64_t kProductGetStateMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProductSDOName, kProductGetStateName);
        inline constexpr uint64_t kProductListProjectCatalogsMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProductSDOName, kProductListProjectCatalogsName);
        inline constexpr uint64_t kProductOpenProjectCatalogMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProductSDOName, kProductOpenProjectCatalogName);
        inline constexpr uint64_t kProductCloseProjectCatalogMethodCode = iCAX::Interaction::MakeSDOMethodCode(kProductSDOName, kProductCloseProjectCatalogName);

        /*
        * @brief 编码产品 SDO 调用负载。
        * @param [in] Payload_ Variant 负载。
        * @return UTF-8 文本字节数组。
        * @details 使用 Data::VariantSerializer，不引入 flatbuffer/interop。
        */
        _PRODUCT_EXP std::vector<uint8_t> EncodeProductPayload(IN const iCAX::Data::Variant& Payload_);

        /*
        * @brief 解码产品 SDO 调用负载。
        * @param [in] Payload_ UTF-8 文本字节数组。
        * @return Variant 负载；空负载返回空对象。
        * @throws std::invalid_argument 负载不是合法 Variant 文本时抛出。
        */
        _PRODUCT_EXP iCAX::Data::Variant DecodeProductPayload(IN const std::vector<uint8_t>& Payload_);
    }
}
