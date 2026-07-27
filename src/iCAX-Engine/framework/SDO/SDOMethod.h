#pragma once

#include "SDOExport.h"

#include <cstdint>
#include <string>

namespace iCAX::Interaction
{
    /*
    * @brief 计算交互名称的 32 位稳定码。
    * @details 名称是公开身份，稳定码只用于 SDOFrame 中的紧凑表示和进程内快速查找。
    */
    inline constexpr uint32_t InteractionNameHash32(IN const char* pText_) noexcept
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

    /*
    * @brief 把 SDO 名称码和 Method 名称码组合为 SDOFrame 使用的 64 位方法码。
    */
    inline constexpr uint64_t MakeSDOMethodCode(IN uint32_t nSDOCode_, IN uint32_t nMethodCode_) noexcept
    {
        return (static_cast<uint64_t>(nSDOCode_) << 32) | static_cast<uint64_t>(nMethodCode_);
    }

    inline constexpr uint64_t MakeSDOMethodCode(IN const char* pSDOName_, IN const char* pMethodName_) noexcept
    {
        return MakeSDOMethodCode(InteractionNameHash32(pSDOName_), InteractionNameHash32(pMethodName_));
    }

    inline constexpr uint32_t GetSDOCode(IN uint64_t nMethodCode_) noexcept
    {
        return static_cast<uint32_t>(nMethodCode_ >> 32);
    }

    inline constexpr uint32_t GetMethodCode(IN uint64_t nMethodCode_) noexcept
    {
        return static_cast<uint32_t>(nMethodCode_ & 0xFFFFFFFFull);
    }

    /*
    * @brief 一个可调用的 SDO 方法。
    * @details 对外完整名称固定为 SDOName.MethodName，两段名称均为单段标识符。
    */
    struct _SDO_EXP CSDOMethod final
    {
        std::string strSDOName;
        std::string strMethodName;
        uint32_t nSDOCode = 0;
        uint32_t nMethodCode = 0;

        uint64_t GetCode() const noexcept;
        bool IsValid() const noexcept;
    };

    _SDO_EXP uint32_t InteractionNameHash32(IN const std::string& strText_);
    _SDO_EXP bool IsValidSDOName(IN const std::string& strName_);
    _SDO_EXP bool IsValidMethodName(IN const std::string& strName_);

    _SDO_EXP CSDOMethod MakeSDOMethod(
        IN const std::string& strSDOName_,
        IN const std::string& strMethodName_);

    _SDO_EXP CSDOMethod MakeSDOMethod(IN uint64_t nMethodCode_);
    _SDO_EXP std::string FormatSDOMethod(IN const CSDOMethod& Method_);
}
