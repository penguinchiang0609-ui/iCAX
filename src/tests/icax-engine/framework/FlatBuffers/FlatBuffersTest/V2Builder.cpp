#include "pch.h"

#include "TestInterop.h"
#include "Generated/V2/TestEnvelopeV2_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

namespace iCAX::FlatBuffersPolicyTest
{
    std::vector<uint8_t> BuildEnvelopeV2(
        const uint32_t SchemaVersion_,
        const std::string& Name_,
        const std::span<const uint32_t> Values_,
        const std::string& ContentHash_)
    {
        flatbuffers::FlatBufferBuilder _Builder;
        const auto _Name = _Builder.CreateString(Name_);
        const auto _Values = _Builder.CreateVector(
            Values_.data(),
            Values_.size());
        const auto _ContentHash = _Builder.CreateString(ContentHash_);

        const auto _Envelope =
            ::iCAX::FlatBuffersPolicyTest::CreateTestEnvelope(
                _Builder,
                SchemaVersion_,
                _Name,
                _Values,
                _ContentHash);
        ::iCAX::FlatBuffersPolicyTest::FinishTestEnvelopeBuffer(
            _Builder,
            _Envelope);

        return {
            _Builder.GetBufferPointer(),
            _Builder.GetBufferPointer() + _Builder.GetSize()
        };
    }

    bool VerifyEnvelopeV2(const std::span<const uint8_t> Bytes_)
    {
        flatbuffers::Verifier _Verifier(Bytes_.data(), Bytes_.size());
        return ::iCAX::FlatBuffersPolicyTest::VerifyTestEnvelopeBuffer(
            _Verifier);
    }
}
