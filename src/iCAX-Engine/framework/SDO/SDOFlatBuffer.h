#pragma once

#include "SDOPayload.h"

#ifdef min
#pragma push_macro("min")
#undef min
#define ICAX_SDO_RESTORE_MIN_MACRO
#endif
#ifdef max
#pragma push_macro("max")
#undef max
#define ICAX_SDO_RESTORE_MAX_MACRO
#endif

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#ifdef ICAX_SDO_RESTORE_MAX_MACRO
#pragma pop_macro("max")
#undef ICAX_SDO_RESTORE_MAX_MACRO
#endif
#ifdef ICAX_SDO_RESTORE_MIN_MACRO
#pragma pop_macro("min")
#undef ICAX_SDO_RESTORE_MIN_MACRO
#endif

#include <span>

namespace iCAX::Interaction
{
    /*
    * @brief 复制已经 Finish 的普通 Google FlatBuffer，作为 SDO 负载。
    * @details
    *   SDO 本身已经有明确的消息边界，因此使用普通 FlatBuffer，不增加
    *   size prefix 或 iCAX 自定义 envelope。
    */
    inline SDOPayload MakeFlatBufferSDO(
        const flatbuffers::FlatBufferBuilder& Builder_)
    {
        const auto* _Begin = Builder_.GetBufferPointer();
        return SDOPayload(_Begin, _Begin + Builder_.GetSize());
    }

    inline std::span<const uint8_t> GetSDOBytes(
        const SDOPayload& Payload_) noexcept
    {
        return { Payload_.data(), Payload_.size() };
    }

    template<typename TRoot>
    bool VerifyFlatBufferSDO(
        const std::span<const uint8_t> Bytes_,
        const char* pFileIdentifier_)
    {
        if (Bytes_.empty() || pFileIdentifier_ == nullptr)
        {
            return false;
        }

        flatbuffers::Verifier _Verifier(Bytes_.data(), Bytes_.size());
        return _Verifier.VerifyBuffer<TRoot>(pFileIdentifier_);
    }

    template<typename TRoot>
    bool VerifyFlatBufferSDO(
        const SDOPayload& Payload_,
        const char* pFileIdentifier_)
    {
        return VerifyFlatBufferSDO<TRoot>(
            GetSDOBytes(Payload_),
            pFileIdentifier_);
    }

    /*
    * @brief 校验并零拷贝读取 SDO 的 FlatBuffer root。
    * @return 校验失败时返回 nullptr；返回的 View 不得超过 Payload 生命周期。
    */
    template<typename TRoot>
    const TRoot* TryGetFlatBufferSDORoot(
        const std::span<const uint8_t> Bytes_,
        const char* pFileIdentifier_)
    {
        return VerifyFlatBufferSDO<TRoot>(Bytes_, pFileIdentifier_)
            ? flatbuffers::GetRoot<TRoot>(Bytes_.data())
            : nullptr;
    }

    template<typename TRoot>
    const TRoot* TryGetFlatBufferSDORoot(
        const SDOPayload& Payload_,
        const char* pFileIdentifier_)
    {
        return TryGetFlatBufferSDORoot<TRoot>(
            GetSDOBytes(Payload_),
            pFileIdentifier_);
    }
}
