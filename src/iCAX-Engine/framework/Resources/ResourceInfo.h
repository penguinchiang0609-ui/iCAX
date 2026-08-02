#pragma once

#include "ResourceKey.h"
#include "Data/uuid.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 对一个不可变资源版本的精确引用。
        * @details
        *   URL 表示稳定资源身份，nVersion 锁定具体内容版本。
        *   资源依赖禁止使用“最新版本”语义，避免子资源更新时隐式改变父资源。
        */
        struct _RESOURCES_EXP CResourceReference final
        {
            std::string URL;
            uint64_t nVersion = 0;

            bool IsValid() const noexcept
            {
                return !URL.empty() && nVersion != 0;
            }
        };

        inline bool operator==(
            IN const CResourceReference& Left_,
            IN const CResourceReference& Right_) noexcept
        {
            return Left_.URL == Right_.URL &&
                Left_.nVersion == Right_.nVersion;
        }

        inline bool operator!=(
            IN const CResourceReference& Left_,
            IN const CResourceReference& Right_) noexcept
        {
            return !(Left_ == Right_);
        }

        inline bool operator<(
            IN const CResourceReference& Left_,
            IN const CResourceReference& Right_) noexcept
        {
            if (Left_.URL != Right_.URL)
            {
                return Left_.URL < Right_.URL;
            }
            return Left_.nVersion < Right_.nVersion;
        }

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
            iCAX::Data::uuid ResourceID;       //!< URL 最后一段的稳定 GUID；旧格式资源可为 nil。
            std::string Name;                 //!< 展示名称。
            std::string Source;               //!< 原始文件或外部 URI；不参与资源身份。
            std::string MediaType;            //!< 规范资源表示的媒体类型。
            std::string ResourceTypeID;       //!< 跨语言稳定资源类型 ID。
            std::string FlatBufferIdentifier; //!< Google FlatBuffers 四字节 file_identifier；非 FlatBuffer 时为空。
            std::string ContentHash;          //!< 内容哈希，可用于增量保存或缓存校验。
            uint64_t nVersion = 0;            //!< 资源内容版本；运行期生成资源可递增该值作为 PDO dataVersion。
            uint64_t nSize = 0;               //!< 资源大小，单位字节；未知时为 0。
            uint32_t nSchemaVersion = 0;       //!< 业务 Schema/Layout 版本；未知时为 0。
            uint32_t nMinimumReaderVersion = 0; //!< 能正确解释资源所需的最低读取器版本。
            uint32_t nFlags = 0;              //!< 调用方自定义标志位。
            EResourcePersistenceMode Persistence = EResourcePersistenceMode::RuntimeOnly; //!< 持久化语义。
            std::map<std::string, std::string> Metadata; //!< 扩展元数据。
            std::vector<CResourceReference> Dependencies; //!< 本版本锁定的直接依赖；资源版本创建后不可修改。

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

        /*
        * @brief 从一组组件/文档根引用收集资源版本闭包的结果。
        * @details Resources 按依赖优先顺序排列，同一 {URL, version} 只出现一次。
        */
        struct _RESOURCES_EXP CResourceReachabilityResult final
        {
            std::vector<CResourceInfo> Resources;
            std::vector<CResourceReference> Missing;

            bool IsComplete() const noexcept
            {
                return Missing.empty();
            }
        };

        /*
        * @brief 一个可直接写入项目文件或从项目文件恢复的精确资源版本。
        * @details External 资源的 Body 必须为空；Embedded 资源的 Body 由稳定
        *   ResourceTypeID 对应的持久化编解码器生成。
        */
        struct _RESOURCES_EXP CResourcePersistentPayload final
        {
            CResourceInfo Info;
            std::vector<uint8_t> Body;
        };
    }
}
