#include "pch.h"
#include "CommandRoute.h"

#include <format>
#include <stdexcept>
#include <string_view>

namespace
{
    bool IsAsciiUpper(IN const char Char_) noexcept
    {
        return Char_ >= 'A' && Char_ <= 'Z';
    }

    bool IsAsciiAlpha(IN const char Char_) noexcept
    {
        return (Char_ >= 'A' && Char_ <= 'Z') || (Char_ >= 'a' && Char_ <= 'z');
    }

    bool IsAsciiDigit(IN const char Char_) noexcept
    {
        return Char_ >= '0' && Char_ <= '9';
    }

    void RequireCommandName(IN const char* pKind_, IN const std::string& strName_)
    {
        if (!iCAX::Command::IsValidCommandName(strName_))
        {
            throw std::invalid_argument(
                std::string(pKind_) +
                " command name must match [A-Z][A-Za-z0-9_]*: " +
                strName_);
        }
    }

    void RequireCommandMainName(IN const std::string& strName_)
    {
        if (!iCAX::Command::IsValidCommandMainName(strName_))
        {
            throw std::invalid_argument(
                "Main command name must be one or more [A-Z][A-Za-z0-9_]* parts separated by '.': " +
                strName_);
        }
    }

    uint32_t MakeValidatedCommandCode(IN const char* pKind_, IN const std::string& strName_)
    {
        if (std::string_view(pKind_) == "Main")
        {
            RequireCommandMainName(strName_);
        }
        else
        {
            RequireCommandName(pKind_, strName_);
        }
        const auto _Code = iCAX::Command::CommandHash32(strName_);
        if (_Code == 0)
        {
            throw std::invalid_argument(std::string(pKind_) + " command code cannot be zero: " + strName_);
        }
        return _Code;
    }
}

uint64_t iCAX::Command::CCommandRoute::GetRouteCode() const noexcept
{
    return MakeCommandCode(nMainCode, nSubCode);
}

bool iCAX::Command::CCommandRoute::IsValid() const noexcept
{
    return nMainCode != 0 && nSubCode != 0;
}

uint32_t iCAX::Command::CommandHash32(IN const std::string& strText_)
{
    uint32_t _Hash = 2166136261u;
    for (const auto _Char : strText_)
    {
        _Hash ^= static_cast<uint8_t>(_Char);
        _Hash *= 16777619u;
    }
    return _Hash;
}

bool iCAX::Command::IsValidCommandName(IN const std::string& strName_)
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

bool iCAX::Command::IsValidCommandMainName(IN const std::string& strName_)
{
    if (strName_.empty())
    {
        return false;
    }

    size_t _Start = 0;
    while (_Start < strName_.size())
    {
        const auto _Dot = strName_.find('.', _Start);
        const auto _Count = (_Dot == std::string::npos) ? std::string::npos : _Dot - _Start;
        if (!IsValidCommandName(strName_.substr(_Start, _Count)))
        {
            return false;
        }
        if (_Dot == std::string::npos)
        {
            return true;
        }
        _Start = _Dot + 1;
    }
    return false;
}

iCAX::Command::CCommandRoute iCAX::Command::MakeCommandRoute(
    IN const std::string& strMainName_,
    IN const std::string& strSubName_)
{
    CCommandRoute _Route;
    _Route.strMainName = strMainName_;
    _Route.strSubName = strSubName_;
    _Route.nMainCode = MakeValidatedCommandCode("Main", strMainName_);
    _Route.nSubCode = MakeValidatedCommandCode("Sub", strSubName_);
    return _Route;
}

iCAX::Command::CCommandRoute iCAX::Command::MakeCommandRoute(IN uint64_t nRouteCode_)
{
    CCommandRoute _Route;
    _Route.nMainCode = GetCommandMainCode(nRouteCode_);
    _Route.nSubCode = GetCommandSubCode(nRouteCode_);
    return _Route;
}

std::string iCAX::Command::FormatCommandRoute(IN const CCommandRoute& Route_)
{
    if (!Route_.strMainName.empty() && !Route_.strSubName.empty())
    {
        return Route_.strMainName + "." + Route_.strSubName;
    }
    return std::format("0x{:08X}.0x{:08X}", Route_.nMainCode, Route_.nSubCode);
}
