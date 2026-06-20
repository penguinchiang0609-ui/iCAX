#pragma once

#include "ProductExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        namespace Detail
        {
            inline constexpr uint32_t FNV1a32(IN const char* pText_) noexcept
            {
                uint32_t _Hash = 2166136261u;
                while (*pText_)
                {
                    _Hash ^= static_cast<uint8_t>(*pText_);
                    _Hash *= 16777619u;
                    ++pText_;
                }
                return _Hash;
            }
        }

        inline constexpr uint64_t MakeProductCommandCode(IN uint32_t nCommandNameID_) noexcept
        {
            return (static_cast<uint64_t>(Detail::FNV1a32("iCAX.Product")) << 32)
                | static_cast<uint64_t>(nCommandNameID_);
        }

        inline constexpr uint64_t kProductGetStateCommand = MakeProductCommandCode(Detail::FNV1a32("Product.GetState"));
        inline constexpr uint64_t kProductListProjectCatalogsCommand = MakeProductCommandCode(Detail::FNV1a32("Product.ListProjectCatalogs"));
        inline constexpr uint64_t kProductOpenProjectCatalogCommand = MakeProductCommandCode(Detail::FNV1a32("Product.OpenProjectCatalog"));
        inline constexpr uint64_t kProductCloseProjectCatalogCommand = MakeProductCommandCode(Detail::FNV1a32("Product.CloseProjectCatalog"));

        _PRODUCT_EXP std::vector<uint8_t> EncodeProductPayload(IN const iCAX::Data::Variant& Payload_);
        _PRODUCT_EXP iCAX::Data::Variant DecodeProductPayload(IN const std::vector<uint8_t>& Payload_);
    }
}
