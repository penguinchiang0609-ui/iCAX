#pragma once

#include "PDOLease.h"

#ifdef min
#pragma push_macro("min")
#undef min
#define ICAX_PDO_RESTORE_MIN_MACRO
#endif
#ifdef max
#pragma push_macro("max")
#undef max
#define ICAX_PDO_RESTORE_MAX_MACRO
#endif

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#ifdef ICAX_PDO_RESTORE_MAX_MACRO
#pragma pop_macro("max")
#undef ICAX_PDO_RESTORE_MAX_MACRO
#endif
#ifdef ICAX_PDO_RESTORE_MIN_MACRO
#pragma pop_macro("min")
#undef ICAX_PDO_RESTORE_MIN_MACRO
#endif

#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace iCAX::PDO
{
    /*
    * @brief 声明一个直接承载 Google FlatBuffer 的 PDO 槽。
    * @param nMaxBufferSize_ FlatBuffer 最大总字节数。
    * @details
    *   Slot 声明保存容量，每次发布由 CPDOWriteLease 单独记录实际字节数，
    *   因而 builder 直接使用生成代码的 FinishXxxBuffer() 即可。
    *   nVersion_ 是业务 schema 协议版本。
    */
    inline PDODecl MakeFlatBufferPDODecl(
        const PDOID nID_,
        const PDODirection eDirection_,
        const uint32_t nVersion_,
        const uint64_t nMaxBufferSize_)
    {
        if (nVersion_ == 0)
        {
            throw std::invalid_argument(
                "FlatBuffer PDO schema version cannot be zero");
        }
        if (nMaxBufferSize_ == 0 ||
            nMaxBufferSize_ >
                static_cast<uint64_t>((std::numeric_limits<int>::max)()))
        {
            throw std::invalid_argument(
                "FlatBuffer PDO capacity is outside the supported range");
        }

        return PDODecl{
            nVersion_,
            nID_,
            eDirection_,
            static_cast<int>(nMaxBufferSize_)
        };
    }

    /*
    * @brief 把一个完整的 FlatBuffer 写入当前 PDO 写租约。
    * @details
    *   本函数同时设置租约的实际 payload size；调用方随后调用 Commit()。
    */
    inline void WriteFlatBuffer(
        CPDOWriteLease& Lease_,
        const std::span<const uint8_t> Buffer_)
    {
        if (!Lease_.IsActive() || Lease_.Data() == nullptr)
        {
            throw std::logic_error("PDO write lease is not active");
        }

        if (Buffer_.empty())
        {
            throw std::invalid_argument(
                "PDO FlatBuffer cannot be empty");
        }
        if (Buffer_.size() > Lease_.PayloadCapacity())
        {
            throw std::length_error(
                "PDO FlatBuffer exceeds the declared slot capacity");
        }

        std::memcpy(Lease_.Data(), Buffer_.data(), Buffer_.size());
        Lease_.SetPayloadSize(static_cast<uint64_t>(Buffer_.size()));
    }

    inline void WriteFlatBuffer(
        CPDOWriteLease& Lease_,
        const flatbuffers::FlatBufferBuilder& Builder_)
    {
        WriteFlatBuffer(
            Lease_,
            std::span<const uint8_t>(
                Builder_.GetBufferPointer(),
                Builder_.GetSize()));
    }

    /*
    * @brief 从 PDO 读租约取得当前 FlatBuffer 的精确字节范围。
    * @details 返回的 span 只在 Lease 保持 active 时有效。
    */
    inline std::optional<std::span<const uint8_t>>
    TryGetFlatBufferBytes(
        const CPDOReadLease& Lease_) noexcept
    {
        if (!Lease_.IsActive() || Lease_.Data() == nullptr)
        {
            return std::nullopt;
        }

        const auto _PayloadSize = Lease_.PayloadSize();
        if (_PayloadSize == 0
            || _PayloadSize > Lease_.PayloadCapacity()
            || _PayloadSize >
                static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
        {
            return std::nullopt;
        }

        return std::span<const uint8_t>(
            static_cast<const uint8_t*>(Lease_.Data()),
            static_cast<size_t>(_PayloadSize));
    }

    template<typename TRoot>
    bool VerifyPDOFlatBuffer(
        const CPDOReadLease& Lease_,
        const char* pFileIdentifier_)
    {
        if (pFileIdentifier_ == nullptr)
        {
            return false;
        }
        const auto _Bytes =
            TryGetFlatBufferBytes(Lease_);
        if (!_Bytes)
        {
            return false;
        }

        flatbuffers::Verifier _Verifier(
            _Bytes->data(),
            _Bytes->size());
        return _Verifier.VerifyBuffer<TRoot>(
            pFileIdentifier_);
    }

    /*
    * @brief 校验并零拷贝读取 PDO FlatBuffer root。
    * @return 校验失败时返回 nullptr；返回的 View 不得超过读租约生命周期。
    */
    template<typename TRoot>
    const TRoot* TryGetPDOFlatBufferRoot(
        const CPDOReadLease& Lease_,
        const char* pFileIdentifier_)
    {
        if (pFileIdentifier_ == nullptr)
        {
            return nullptr;
        }
        const auto _Bytes =
            TryGetFlatBufferBytes(Lease_);
        if (!_Bytes)
        {
            return nullptr;
        }

        flatbuffers::Verifier _Verifier(
            _Bytes->data(),
            _Bytes->size());
        if (!_Verifier.VerifyBuffer<TRoot>(
                pFileIdentifier_))
        {
            return nullptr;
        }

        return flatbuffers::GetRoot<TRoot>(
            _Bytes->data());
    }
}
