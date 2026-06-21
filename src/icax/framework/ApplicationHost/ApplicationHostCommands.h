#pragma once

#include "ApplicationHostExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace ApplicationHost
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

        inline constexpr uint64_t MakeApplicationCommandCode(IN uint32_t nCommandNameID_) noexcept
        {
            return (static_cast<uint64_t>(Detail::FNV1a32("iCAX.ApplicationHost")) << 32)
                | static_cast<uint64_t>(nCommandNameID_);
        }

        inline constexpr uint64_t kAppGetStateCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.GetState"));
        inline constexpr uint64_t kAppListProductsCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.ListProducts"));
        inline constexpr uint64_t kAppStartProductCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.StartProduct"));
        inline constexpr uint64_t kAppStopProductCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.StopProduct"));
        inline constexpr uint64_t kAppResolveProjectFileCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.ResolveProjectFile"));
        inline constexpr uint64_t kAppOpenProjectFileCommand = MakeApplicationCommandCode(Detail::FNV1a32("App.OpenProjectFile"));

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
