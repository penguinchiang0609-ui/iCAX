#pragma once

#include "ResourceKey.h"

#include <cstdint>
#include <map>
#include <string>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 资源持久化方式。
        * @details
        *   资源池同时保存运行期资源和项目资源。保存项目时只应导出非 RuntimeOnly 的资源清单。
        */
        enum class EResourcePersistenceMode : uint8_t
        {
            RuntimeOnly = 0, //!< 仅运行期缓存，不写入项目。
            Embedded = 1,    //!< 保存项目时需要嵌入项目文件。
            External = 2     //!< 保存项目时以外部引用方式持久化。
        };

        /*
        * @brief 资源描述信息。
        * @details
        *   CResourceInfo 是资源清单数据，不一定意味着对象已经加载到内存。
        *   ResourcePool 可以只登记 Info，也可以同时保存 pResource。
        */
        struct _RESOURCES_EXP CResourceInfo final
        {
            CResourceKey Key;                 //!< 资源唯一键。
            std::string Name;                 //!< 展示名称。
            std::string Source;               //!< 原始来源；为空时通常用 Key.Source 补齐。
            std::string ContentHash;          //!< 内容哈希，可用于增量保存或缓存校验。
            uint64_t nSize = 0;               //!< 资源大小，单位字节；未知时为 0。
            uint32_t nFlags = 0;              //!< 调用方自定义标志位。
            EResourcePersistenceMode Persistence = EResourcePersistenceMode::RuntimeOnly; //!< 持久化语义。
            std::map<std::string, std::string> Metadata; //!< 扩展元数据。

            /*
            * @brief 是否仅运行期存在。
            */
            bool IsRuntimeOnly() const noexcept
            {
                return Persistence == EResourcePersistenceMode::RuntimeOnly;
            }

            /*
            * @brief 是否应嵌入项目文件。
            */
            bool IsEmbedded() const noexcept
            {
                return Persistence == EResourcePersistenceMode::Embedded;
            }

            /*
            * @brief 是否为外部引用资源。
            */
            bool IsExternal() const noexcept
            {
                return Persistence == EResourcePersistenceMode::External;
            }

            /*
            * @brief 是否需要进入项目资源清单。
            */
            bool IsPersistent() const noexcept
            {
                return Persistence != EResourcePersistenceMode::RuntimeOnly;
            }
        };
    }
}
