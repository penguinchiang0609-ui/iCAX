#include "pch.h"

#include "TestInterop.h"
#include "Generated/V1/TestEnvelopeV1_generated.h"

#include <flatbuffers/verifier.h>

namespace iCAX::FlatBuffersPolicyTest
{
    bool TryReadEnvelopeV1(
        const std::span<const uint8_t> Bytes_,
        SEnvelopeV1View& View_)
    {
        flatbuffers::Verifier _Verifier(Bytes_.data(), Bytes_.size());
        if (!::iCAX::FlatBuffersPolicyTest::VerifyTestEnvelopeBuffer(
                _Verifier))
        {
            return false;
        }

        const auto* _Envelope =
            ::iCAX::FlatBuffersPolicyTest::GetTestEnvelope(Bytes_.data());
        if (_Envelope == nullptr)
        {
            return false;
        }

        View_.SchemaVersion = _Envelope->schema_version();
        View_.Name = _Envelope->name() == nullptr
            ? std::string{}
            : _Envelope->name()->str();

        View_.Values.clear();
        if (const auto* _Values = _Envelope->values();
            _Values != nullptr)
        {
            View_.Values.assign(_Values->begin(), _Values->end());
        }

        return true;
    }
}
