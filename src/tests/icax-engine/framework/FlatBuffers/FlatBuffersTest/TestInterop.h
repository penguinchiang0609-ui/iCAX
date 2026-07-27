#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace iCAX::FlatBuffersPolicyTest
{
    struct SEnvelopeV1View final
    {
        uint32_t SchemaVersion = 0;
        std::string Name;
        std::vector<uint32_t> Values;
    };

    std::vector<uint8_t> BuildEnvelopeV2(
        uint32_t SchemaVersion_,
        const std::string& Name_,
        std::span<const uint32_t> Values_,
        const std::string& ContentHash_);

    bool VerifyEnvelopeV2(std::span<const uint8_t> Bytes_);

    bool TryReadEnvelopeV1(
        std::span<const uint8_t> Bytes_,
        SEnvelopeV1View& View_);
}
