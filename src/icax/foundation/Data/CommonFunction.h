#pragma once
#include "Data.h"
#include <cmath>
#include <math.h>
#include <memory>
#include <unordered_set>
#include <string>
#include <locale>
#include <codecvt>
#include <vector>
#include <stdexcept>
#include <Windows.h>
#include <cstring>

template<typename T>
T wrap(T value, T minVal, T maxVal)
{
    static_assert(std::is_arithmetic_v<T>, "wrap only supports arithmetic types");
    T range = maxVal - minVal;
    if (range <= 0)
        return minVal; // 防护：无效区间
    if (ZERO_EQL(range))
        return minVal;

    if constexpr (std::is_integral_v<T>)
    {
        // 整数模运算
        T result = (value - minVal) % range;
        if (result < 0) result += range;
        return result + minVal;
    }
    else
    {
        // 浮点数模运算
        T result = std::fmod(value - minVal, range);
        if (result < 0) result += range;
        return result + minVal;
    }
}

template <class T>
struct WeakPtrHash {
    size_t operator()(std::weak_ptr<T> const& w) const noexcept {
        auto sp = w.lock();
        return std::hash<T*>()(sp.get());   // expired → get() = nullptr
    }
};

template <class T>
struct WeakPtrEqual {
    bool operator()(std::weak_ptr<T> const& a, std::weak_ptr<T> const& b) const noexcept {
        return a.lock() == b.lock();
    }
};

template <typename T>
bool EqualWeakPtrSets(
    const std::unordered_set<std::weak_ptr<T>, WeakPtrHash<T>, WeakPtrEqual<T>>& a,
    const std::unordered_set<std::weak_ptr<T>, WeakPtrHash<T>, WeakPtrEqual<T>>& b)
{
    if (a.size() != b.size()) return false;

    for (auto& wa : a) {
        bool found = false;
        for (auto& wb : b) {
            if (WeakPtrEqual<T>()(wa, wb)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    return true;
}

_DATA_EXP std::size_t HashCombine(std::size_t seed, std::size_t value);

// ANSI (GBK/系统编码) -> UTF-8
inline std::string LocalToUTF8(const std::string& ansiStr)
{
    if (ansiStr.empty())
        return {};

    // 1. ANSI -> wchar_t
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansiStr.data(), (int)ansiStr.size(), nullptr, 0);
    if (wlen == 0)
        throw std::runtime_error("MultiByteToWideChar failed");

    std::wstring wstr(wlen, 0);
    if (MultiByteToWideChar(CP_ACP, 0, ansiStr.data(), (int)ansiStr.size(), wstr.data(), wlen) == 0)
        throw std::runtime_error("MultiByteToWideChar failed");

    // 2. wchar_t -> UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len == 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    std::string utf8Str(u8len, 0);
    if (WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, utf8Str.data(), u8len, nullptr, nullptr) == 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    return utf8Str;
}

// UTF-8 -> ANSI (系统默认代码页)
inline std::string UTF8ToLocal(const std::string& utf8Str)
{
    if (utf8Str.empty())
        return {};

    // 1. UTF-8 -> wchar_t
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), nullptr, 0);
    if (wlen == 0)
        throw std::runtime_error("MultiByteToWideChar failed");

    std::wstring wstr(wlen, 0);
    if (MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), wstr.data(), wlen) == 0)
        throw std::runtime_error("MultiByteToWideChar failed");

    // 2. wchar_t -> ANSI
    int alen = WideCharToMultiByte(CP_ACP, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (alen == 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    std::string localStr(alen, 0);
    if (WideCharToMultiByte(CP_ACP, 0, wstr.data(), wlen, localStr.data(), alen, nullptr, nullptr) == 0)
        throw std::runtime_error("WideCharToMultiByte failed");

    return localStr;
}

//! HASH计算
inline constexpr uint32_t FNV1a32(const char* s)
{
    uint32_t h = 2166136261u;
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s);
        h *= 16777619u;
        ++s;
    }
    return h;
}