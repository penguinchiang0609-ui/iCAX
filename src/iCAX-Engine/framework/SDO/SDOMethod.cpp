#include "pch.h"
#include "SDOMethod.h"


namespace
{
    bool IsAsciiUpper(IN const char Value_) noexcept
    {
        return Value_ >= 'A' && Value_ <= 'Z';
    }

    bool IsAsciiAlpha(IN const char Value_) noexcept
    {
        return (Value_ >= 'A' && Value_ <= 'Z') || (Value_ >= 'a' && Value_ <= 'z');
    }

    bool IsAsciiDigit(IN const char Value_) noexcept
    {
        return Value_ >= '0' && Value_ <= '9';
    }

    bool IsValidName(IN const std::string& strName_)
    {
        if (strName_.empty() || !IsAsciiUpper(strName_.front()))
        {
            return false;
        }

        for (const auto _Char : strName_)
        {
            if (!IsAsciiAlpha(_Char) && !IsAsciiDigit(_Char) && _Char != '_')
            {
                return false;
            }
        }
        return true;
    }

    uint32_t RequireNameCode(
        IN const char* pKind_,
        IN const std::string& strName_,
        IN const bool bValid_)
    {
        if (!bValid_)
        {
            throw std::invalid_argument(
                std::string(pKind_) + " name must match [A-Z][A-Za-z0-9_]*: " + strName_);
        }

        const auto _Code = iCAX::Interaction::InteractionNameHash32(strName_);
        if (_Code == 0)
        {
            throw std::invalid_argument(std::string(pKind_) + " code cannot be zero: " + strName_);
        }
        return _Code;
    }
}

uint64_t iCAX::Interaction::CSDOMethod::GetCode() const noexcept
{
    return MakeSDOMethodCode(nSDOCode, nMethodCode);
}

bool iCAX::Interaction::CSDOMethod::IsValid() const noexcept
{
    return nSDOCode != 0 && nMethodCode != 0;
}

uint32_t iCAX::Interaction::InteractionNameHash32(IN const std::string& strText_)
{
    uint32_t _Hash = 2166136261u;
    for (const auto _Char : strText_)
    {
        _Hash ^= static_cast<uint8_t>(_Char);
        _Hash *= 16777619u;
    }
    return _Hash;
}

bool iCAX::Interaction::IsValidSDOName(IN const std::string& strName_)
{
    return IsValidName(strName_);
}

bool iCAX::Interaction::IsValidMethodName(IN const std::string& strName_)
{
    return IsValidName(strName_);
}

iCAX::Interaction::CSDOMethod iCAX::Interaction::MakeSDOMethod(
    IN const std::string& strSDOName_,
    IN const std::string& strMethodName_)
{
    CSDOMethod _Method;
    _Method.strSDOName = strSDOName_;
    _Method.strMethodName = strMethodName_;
    _Method.nSDOCode = RequireNameCode("SDO", strSDOName_, IsValidSDOName(strSDOName_));
    _Method.nMethodCode = RequireNameCode("Method", strMethodName_, IsValidMethodName(strMethodName_));
    return _Method;
}

iCAX::Interaction::CSDOMethod iCAX::Interaction::MakeSDOMethod(IN uint64_t nMethodCode_)
{
    CSDOMethod _Method;
    _Method.nSDOCode = GetSDOCode(nMethodCode_);
    _Method.nMethodCode = GetMethodCode(nMethodCode_);
    return _Method;
}

std::string iCAX::Interaction::FormatSDOMethod(IN const CSDOMethod& Method_)
{
    if (!Method_.strSDOName.empty() && !Method_.strMethodName.empty())
    {
        return Method_.strSDOName + "." + Method_.strMethodName;
    }
    return std::format("0x{:08X}.0x{:08X}", Method_.nSDOCode, Method_.nMethodCode);
}
