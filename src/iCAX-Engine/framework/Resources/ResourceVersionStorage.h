#pragma once

#include "ResourcesExport.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace iCAX::Resource
{
    enum class EResourceVersionCondition : uint8_t
    {
        None = 0,
        MustExist = 1,
        MustNotExist = 2,
        VersionMatches = 3
    };

    enum class EResourceMutationResult : uint8_t
    {
        Created = 0,
        Replaced = 1,
        Removed = 2,
        NotFound = 3,
        PreconditionFailed = 4
    };

    /*
    * @brief 历史资源版本的冷存储编解码器。
    * @details
    *   ResourcePool 不解释 BREP 等业务对象。业务插件可以按运行期类型注册编解码器，
    *   让过期版本释放内存并写入临时目录。序列化失败时资源池会回退到内存保留，
    *   不会为了满足缓存预算而丢弃版本。
    */
    struct _RESOURCES_EXP CResourceVersionCodec final
    {
        using CSerialize = std::function<
            std::optional<std::vector<uint8_t>>(
                const std::shared_ptr<void>&)>;
        using CDeserialize = std::function<
            std::shared_ptr<void>(
                std::span<const uint8_t>)>;

        CSerialize Serialize;
        CDeserialize Deserialize;

        bool IsValid() const noexcept
        {
            return static_cast<bool>(Serialize) &&
                static_cast<bool>(Deserialize);
        }
    };

    /*
    * @brief 历史版本冷存储配置。
    * @details
    *   TemporaryRootDirectory 为空时使用系统 temp 目录。ResourcePool 始终在该根目录
    *   下创建自己独占的会话子目录，析构时只清理这个子目录。
    */
    struct _RESOURCES_EXP CResourceVersionStorageOptions final
    {
        std::filesystem::path TemporaryRootDirectory;
        bool bCleanupOnDestroy = true;
    };

    /*
    * @brief 历史版本存储统计。
    * @details ResidentBytes 使用 CResourceInfo::nSize 估算，仅统计因为无法冷存而由池强持有的对象。
    */
    struct _RESOURCES_EXP CResourceVersionStorageStats final
    {
        size_t nArchivedVersionCount = 0;
        size_t nColdVersionCount = 0;
        size_t nResidentVersionCount = 0;
        uint64_t nColdBytes = 0;
        uint64_t nResidentBytes = 0;
    };
}
