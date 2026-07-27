#include "pch.h"

#include "TestInterop.h"

#include <array>
#include <cstdint>
#include <vector>

using namespace iCAX::FlatBuffersPolicyTest;

TEST(FlatBuffersTest, V1ReaderReadsV2TableAndIgnoresAppendedField)
{
    const std::array<uint32_t, 3> _Values{ 11, 22, 33 };
    const auto _Bytes = BuildEnvelopeV2(
        2,
        "mesh",
        _Values,
        "sha256:0123456789");

    ASSERT_TRUE(VerifyEnvelopeV2(_Bytes));

    SEnvelopeV1View _View;
    ASSERT_TRUE(TryReadEnvelopeV1(_Bytes, _View));
    EXPECT_EQ(2u, _View.SchemaVersion);
    EXPECT_EQ("mesh", _View.Name);
    EXPECT_EQ(
        (std::vector<uint32_t>{ 11, 22, 33 }),
        _View.Values);
}

TEST(FlatBuffersTest, VerifierRejectsTruncatedBuffer)
{
    const std::array<uint32_t, 1> _Values{ 7 };
    auto _Bytes = BuildEnvelopeV2(
        2,
        "brep",
        _Values,
        "sha256:abcdef");
    ASSERT_GT(_Bytes.size(), 8u);

    _Bytes.resize(_Bytes.size() / 2);

    EXPECT_FALSE(VerifyEnvelopeV2(_Bytes));

    SEnvelopeV1View _View;
    EXPECT_FALSE(TryReadEnvelopeV1(_Bytes, _View));
}

TEST(FlatBuffersTest, VerifierRejectsWrongFileIdentifier)
{
    const std::array<uint32_t, 1> _Values{ 9 };
    auto _Bytes = BuildEnvelopeV2(
        2,
        "mesh",
        _Values,
        "sha256:fedcba");
    ASSERT_GE(_Bytes.size(), 8u);

    _Bytes[4] = static_cast<uint8_t>('X');

    EXPECT_FALSE(VerifyEnvelopeV2(_Bytes));

    SEnvelopeV1View _View;
    EXPECT_FALSE(TryReadEnvelopeV1(_Bytes, _View));
}
