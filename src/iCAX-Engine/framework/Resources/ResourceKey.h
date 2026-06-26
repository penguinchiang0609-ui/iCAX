#pragma once

#include "ResourcesExport.h"

#include <string>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 资源唯一键。
        * @details
        *   当前框架直接使用 Source 作为 key，通常就是文件路径、URI 或调用方约定的稳定字符串。
        *   ResourceKey 不额外生成 uuid，避免同一资源出现多个身份。
        */
        struct _RESOURCES_EXP CResourceKey final
        {
            std::string Source; //!< 资源来源，也是资源池中的唯一键。

            /*
            * @brief 判断 key 是否可用。
            * @return true 表示 Source 非空。
            */
            bool IsValid() const;
        };

        /*
        * @brief 从来源字符串创建资源键。
        * @param [in] strSource_ 资源来源。
        * @return 以 Source 为唯一键的 CResourceKey。
        */
        inline CResourceKey MakeResourceKeyFromSource(IN const std::string& strSource_)
        {
            return CResourceKey{ strSource_ };
        }

        /*
        * @brief 判断两个资源键是否相等。
        */
        _RESOURCES_EXP bool operator==(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        /*
        * @brief 判断两个资源键是否不等。
        */
        _RESOURCES_EXP bool operator!=(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        /*
        * @brief 资源键排序，用于 std::map。
        */
        _RESOURCES_EXP bool operator<(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;

        /*
        * @brief 转为可读字符串。
        * @param [in] Key_ 资源键。
        * @return 当前实现返回 Key_.Source。
        */
        _RESOURCES_EXP std::string ToString(IN const CResourceKey& Key_);
    }
}
