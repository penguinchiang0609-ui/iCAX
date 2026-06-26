#pragma once

#include "PDODecl.h"

#include <type_traits>

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief 声明一个类型可以作为 PDO payload。
        * @details
        *   PDO payload 必须是固定二进制布局，C++ 与前端 wrapper 需要从同一份结构定义生成。
        *   使用该宏后，可通过 MakeTypedPDODecl<T>() 生成带版本和大小的 PDODecl。
        */
#define ICAX_DECLARE_PDO_PAYLOAD(Version_) \
        inline static constexpr uint32_t S_PDOLayoutVersion = Version_

        template<typename T>
        struct TPDOLayout final
        {
            static_assert(std::is_standard_layout_v<T>, "PDO payload must be standard-layout");
            static_assert(std::is_trivially_copyable_v<T>, "PDO payload must be trivially copyable");

            inline static constexpr uint32_t Version = T::S_PDOLayoutVersion;
            inline static constexpr int PayloadSize = static_cast<int>(sizeof(T));
        };

        template<typename T>
        PDODecl MakeTypedPDODecl(
            IN const PDOID& nID_,
            IN const PDODirection& eDirection_)
        {
            static_assert(std::is_standard_layout_v<T>, "PDO payload must be standard-layout");
            static_assert(std::is_trivially_copyable_v<T>, "PDO payload must be trivially copyable");
            static_assert(T::S_PDOLayoutVersion > 0, "PDO payload layout version cannot be zero");

            return PDODecl{
                TPDOLayout<T>::Version,
                nID_,
                eDirection_,
                TPDOLayout<T>::PayloadSize
            };
        }
    }
}
