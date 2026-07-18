#pragma once

#include "FacadesExport.h"

#include <cstdint>
#include <string>

namespace iCAX::Interaction
{
    /*
    * @brief 计算交互名称的 32 位稳定码。
    * @details 名称是公开身份，稳定码只用于 Mail 中的紧凑表示和进程内快速查找。
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
    * @brief 把 Facade 名称码和 Method 名称码组合为 Mail 使用的 64 位类型码。
    */
    inline constexpr uint64_t MakeFacadeMethodCode(IN uint32_t nFacadeCode_, IN uint32_t nMethodCode_) noexcept
    {
        return (static_cast<uint64_t>(nFacadeCode_) << 32) | static_cast<uint64_t>(nMethodCode_);
    }

    inline constexpr uint64_t MakeFacadeMethodCode(IN const char* pFacadeName_, IN const char* pMethodName_) noexcept
    {
        return MakeFacadeMethodCode(InteractionNameHash32(pFacadeName_), InteractionNameHash32(pMethodName_));
    }

    inline constexpr uint32_t GetFacadeCode(IN uint64_t nMethodCode_) noexcept
    {
        return static_cast<uint32_t>(nMethodCode_ >> 32);
    }

    inline constexpr uint32_t GetMethodCode(IN uint64_t nMethodCode_) noexcept
    {
        return static_cast<uint32_t>(nMethodCode_ & 0xFFFFFFFFull);
    }

    /*
    * @brief 一个可调用的 Facade 方法。
    * @details 对外完整名称固定为 FacadeName.MethodName，两段名称均为单段标识符。
    */
    struct _FACADES_EXP CFacadeMethod final
    {
        std::string strFacadeName;
        std::string strMethodName;
        uint32_t nFacadeCode = 0;
        uint32_t nMethodCode = 0;

        uint64_t GetCode() const noexcept;
        bool IsValid() const noexcept;
    };

    _FACADES_EXP uint32_t InteractionNameHash32(IN const std::string& strText_);
    _FACADES_EXP bool IsValidFacadeName(IN const std::string& strName_);
    _FACADES_EXP bool IsValidMethodName(IN const std::string& strName_);

    _FACADES_EXP CFacadeMethod MakeFacadeMethod(
        IN const std::string& strFacadeName_,
        IN const std::string& strMethodName_);

    _FACADES_EXP CFacadeMethod MakeFacadeMethod(IN uint64_t nMethodCode_);
    _FACADES_EXP std::string FormatFacadeMethod(IN const CFacadeMethod& Method_);
}
