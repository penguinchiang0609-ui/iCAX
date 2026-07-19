#include "pch.h"
#include "FacadeMethod.h"


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

uint64_t iCAX::Interaction::CFacadeMethod::GetCode() const noexcept
{
    return MakeFacadeMethodCode(nFacadeCode, nMethodCode);
}

bool iCAX::Interaction::CFacadeMethod::IsValid() const noexcept
{
    return nFacadeCode != 0 && nMethodCode != 0;
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

bool iCAX::Interaction::IsValidFacadeName(IN const std::string& strName_)
{
    return IsValidName(strName_);
}

bool iCAX::Interaction::IsValidMethodName(IN const std::string& strName_)
{
    return IsValidName(strName_);
}

iCAX::Interaction::CFacadeMethod iCAX::Interaction::MakeFacadeMethod(
    IN const std::string& strFacadeName_,
    IN const std::string& strMethodName_)
{
    CFacadeMethod _Method;
    _Method.strFacadeName = strFacadeName_;
    _Method.strMethodName = strMethodName_;
    _Method.nFacadeCode = RequireNameCode("Facade", strFacadeName_, IsValidFacadeName(strFacadeName_));
    _Method.nMethodCode = RequireNameCode("Method", strMethodName_, IsValidMethodName(strMethodName_));
    return _Method;
}

iCAX::Interaction::CFacadeMethod iCAX::Interaction::MakeFacadeMethod(IN uint64_t nMethodCode_)
{
    CFacadeMethod _Method;
    _Method.nFacadeCode = GetFacadeCode(nMethodCode_);
    _Method.nMethodCode = GetMethodCode(nMethodCode_);
    return _Method;
}

std::string iCAX::Interaction::FormatFacadeMethod(IN const CFacadeMethod& Method_)
{
    if (!Method_.strFacadeName.empty() && !Method_.strMethodName.empty())
    {
        return Method_.strFacadeName + "." + Method_.strMethodName;
    }
    return std::format("0x{:08X}.0x{:08X}", Method_.nFacadeCode, Method_.nMethodCode);
}
