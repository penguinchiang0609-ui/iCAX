#pragma once

#include "ResourceInfo.h"
#include "ResourceVersionStorage.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourceAccessService;

        struct CResourceSnapshot final
        {
            CResourceInfo Info;
            std::optional<std::type_index> RuntimeType;
            std::shared_ptr<void> pResource;
        };

        /*
        * @brief Scene 级资源池。
        * @details
        *   ResourcePool 保存资源清单和已加载对象。它不负责加载逻辑，加载由 ResourceLibrary 或 Resource::Load 完成。
        *   同一个 Scene 应持有自己的 ResourcePool/ResourceLibrary，以隔离不同 Scene 的资源缓存和持久化清单。
        */
        class _RESOURCES_EXP CResourcePool final
        {
        public:
            CResourcePool();
            explicit CResourcePool(
                IN const CResourceVersionStorageOptions& VersionStorageOptions_);
            ~CResourcePool();

            CResourcePool(IN const CResourcePool&) = delete;
            CResourcePool& operator=(IN const CResourcePool&) = delete;

        public:
            /*
            * @brief 登记或更新资源信息。
            * @param [in] Info_ 资源信息，Info_.Key 必须有效。
            * @details 如果资源已存在，只更新 Info，不影响已加载对象。
            */
            void Register(IN const CResourceInfo& Info_);

            /*
            * @brief 尝试登记资源信息。
            * @param [in] Info_ 资源信息，Info_.Key 必须有效。
            * @return true 表示新增成功；false 表示同 Key 已存在。
            */
            bool TryRegister(IN const CResourceInfo& Info_);

            /*
            * @brief 设置无类型资源对象。
            * @param [in] Key_ 资源键。
            * @param [in] pResource_ 资源对象，不能为空。
            * @param [in] RuntimeType_ 资源真实 C++ 类型。
            * @param [in] Info_ 资源信息，可为空；Key 和 Source 会自动补齐。
            * @details 同 Key 已存在时会整体覆盖记录。
            */
            void SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            void SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());

            /*
            * @brief 尝试新增无类型资源对象。
            * @return true 表示新增成功；false 表示同 Key 已存在。
            */
            bool TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());

            /*
            * @brief 获取无类型资源对象。
            * @param [in] Key_ 资源键。
            * @return 已加载对象；不存在或尚未加载时返回 nullptr。
            */
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_) const;

            /*
            * @brief 按期望类型获取无类型资源对象。
            * @param [in] Key_ 资源键。
            * @param [in] ExpectedRuntimeType_ 期望的 C++ 类型。
            * @return 类型匹配的资源对象；不存在、未加载或类型不匹配时返回 nullptr。
            */
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const;

            /*
            * @brief 获取指定历史版本的无类型资源对象。
            * @details 冷版本会从临时目录按需反序列化；池只保存弱缓存，不因此永久增加内存占用。
            */
            std::shared_ptr<void> GetUntyped(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_) const;
            std::shared_ptr<void> GetUntyped(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_,
                IN const std::type_info& ExpectedRuntimeType_) const;

            /*
            * @brief 判断资源记录是否存在。
            */
            bool Contains(IN const CResourceKey& Key_) const;

            /*
            * @brief 判断指定版本是否仍可用于撤销/还原。
            */
            bool ContainsVersion(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_) const;

            /*
            * @brief 判断资源对象是否已加载。
            */
            bool HasObject(IN const CResourceKey& Key_) const;

            /*
            * @brief 卸载资源对象但保留资源信息。
            * @return true 表示找到了资源记录。
            */
            bool Unload(IN const CResourceKey& Key_);

            /*
            * @brief 清空资源池。
            */
            void Clear();

            /*
            * @brief 获取资源记录数量。
            */
            size_t Count() const;

            /*
            * @brief 获取资源信息。
            * @return 找到时返回 CResourceInfo，否则返回 std::nullopt。
            */
            std::optional<CResourceInfo> GetInfo(IN const CResourceKey& Key_) const;

            /*
            * @brief 获取指定当前或历史版本的资源信息。
            */
            std::optional<CResourceInfo> GetInfo(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_) const;

            /*
            * @brief 获取资源内容版本。
            * @return 资源存在时返回 Info.nVersion，否则返回 0。
            * @details
            *   该版本可作为 PDO dataVersion 使用；0 表示尚无有效内容版本。
            */
            uint64_t GetVersion(IN const CResourceKey& Key_) const;

            /*
            * @brief 标记资源内容发生变化并递增版本。
            * @return 递增后的版本；资源不存在时返回 0。
            * @details 只应在资源内容变化时调用，单纯修改 Name、Persistence、Metadata 不应调用。
            */
            uint64_t Touch(IN const CResourceKey& Key_);

            /*
            * @brief 更新资源信息。
            * @return true 表示资源记录存在并已更新。
            */
            bool UpdateInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_);

            /*
            * @brief 获取全部资源键。
            */
            std::vector<CResourceKey> GetKeys() const;

            /*
            * @brief 获取全部资源信息。
            */
            std::vector<CResourceInfo> GetInfos() const;

            /*
            * @brief 获取可保存资源清单。
            * @param [in] bIncludeRuntimeOnly_ true 表示连运行期资源也纳入结果。
            * @return 资源信息数组。
            */
            std::vector<CResourceInfo> GetManifest(IN bool bIncludeRuntimeOnly_ = false) const;

            /*
            * @brief 获取已加载对象的运行期类型名。
            * @return 类型名；资源不存在或未加载时返回空字符串。
            */
            std::string GetRuntimeTypeName(IN const CResourceKey& Key_) const;

            /*
            * @brief 原子获取资源信息和对象快照。
            */
            std::optional<CResourceSnapshot> GetSnapshot(
                IN const CResourceKey& Key_) const;

            /*
            * @brief 原子获取指定当前或历史版本快照。
            * @details 历史版本位于冷存储时会按需加载；失败时仍返回 Info，但 pResource 为空。
            */
            std::optional<CResourceSnapshot> GetSnapshot(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_) const;

            /*
            * @brief 获取某个 URL 当前仍保留的全部版本号。
            */
            std::vector<uint64_t> GetVersions(
                IN const CResourceKey& Key_) const;

            /*
            * @brief 获取直接引用指定资源版本的全部资源版本。
            * @details 返回的是精确版本引用；当前版本和历史版本都会参与查询。
            */
            std::vector<CResourceReference> GetDependents(
                IN const CResourceReference& Target_) const;

            /*
            * @brief 从组件/文档根引用收集完整资源版本闭包。
            * @details
            *   结果按依赖优先顺序排列并自动去重；只读取版本元数据，不加载冷存储正文。
            *   Missing 非空表示根引用或依赖已经损坏，保存方不应提交不完整项目文件。
            */
            CResourceReachabilityResult CollectReachable(
                IN const std::vector<CResourceReference>& Roots_) const;

            /*
            * @brief 注册业务资源历史版本编解码器。
            * @param [in] bReplaceExisting_ true 表示替换同运行期类型的现有编解码器。
            */
            bool RegisterVersionCodec(
                IN const std::type_info& RuntimeType_,
                IN CResourceVersionCodec Codec_,
                IN bool bReplaceExisting_ = false);

            /*
            * @brief 查询版本是否已经从池持有的内存对象转为磁盘冷版本。
            */
            bool IsVersionCold(
                IN const CResourceKey& Key_,
                IN uint64_t nVersion_) const;

            /*
            * @brief 获取历史版本冷存储统计和本池独占的临时目录。
            */
            CResourceVersionStorageStats GetVersionStorageStats() const;
            std::filesystem::path GetVersionStorageDirectory() const;

            /*
            * @brief 按资源版本条件原子创建或替换对象。
            * @details 成功写入时由池生成严格递增的资源版本。
            */
            EResourceMutationResult PutUntypedVersioned(
                IN const CResourceKey& Key_,
                IN std::shared_ptr<void> pResource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_,
                IN EResourceVersionCondition Condition_,
                IN uint64_t nExpectedVersion_,
                OUT CResourceInfo* pStoredInfo_ = nullptr);
            EResourceMutationResult PutUntypedVersioned(
                IN const CResourceKey& Key_,
                IN std::shared_ptr<void> pResource_,
                IN std::type_index RuntimeType_,
                IN const CResourceInfo& Info_,
                IN EResourceVersionCondition Condition_,
                IN uint64_t nExpectedVersion_,
                OUT CResourceInfo* pStoredInfo_ = nullptr);

            /*
            * @brief 复用父资源不可变正文，仅重绑定一个直接依赖并生成父资源新版本。
            * @details Parent_ 必须是当前版本；并发变化时返回 PreconditionFailed。
            */
            EResourceMutationResult RebindDependencyVersioned(
                IN const CResourceReference& Parent_,
                IN const CResourceReference& OldDependency_,
                IN const CResourceReference& NewDependency_,
                OUT CResourceInfo* pStoredInfo_ = nullptr);

            template <typename T>
            /*
            * @brief 设置指定类型资源对象。
            * @param [in] strSource_ 资源来源，同时作为 key。
            * @param [in] pResource_ 资源对象。
            * @param [in] Info_ 资源信息。
            */
            void Set(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                SetUntyped(CResourceKey{ strSource_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
            /*
            * @brief 尝试新增指定类型资源对象。
            * @return true 表示新增成功。
            */
            bool TryAdd(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                return TryAddUntyped(CResourceKey{ strSource_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
            /*
            * @brief 获取指定类型资源对象。
            * @return 类型匹配的资源对象；未加载、未找到或类型不匹配时返回 nullptr。
            */
            std::shared_ptr<T> Get(IN const std::string& strSource_) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = GetUntyped(CResourceKey{ strSource_ }, typeid(TResource));
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            template <typename T>
            /*
            * @brief 获取指定资源版本；冷版本会从磁盘按需加载。
            */
            std::shared_ptr<T> Get(
                IN const std::string& strSource_,
                IN uint64_t nVersion_) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = GetUntyped(
                    CResourceKey{ strSource_ },
                    nVersion_,
                    typeid(TResource));
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            /*
            * @brief 标记指定来源资源内容发生变化。
            * @return 递增后的版本；资源不存在时返回 0。
            */
            uint64_t Touch(IN const std::string& strSource_)
            {
                return Touch(CResourceKey{ strSource_ });
            }

        private:
            friend class CResourceAccessService;

            struct CResourceRecord final
            {
                CResourceInfo Info;
                std::optional<std::type_index> RuntimeType;
                std::shared_ptr<void> pResource;
            };

            struct CArchivedResourceRecord final
            {
                CResourceInfo Info;
                std::optional<std::type_index> RuntimeType;
                std::weak_ptr<void> CachedResource;
                std::shared_ptr<void> pResidentResource;
                std::filesystem::path ColdStoragePath;
                uint64_t nStoredSize = 0;
            };

            using CArchivedVersionMap =
                std::map<uint64_t, CArchivedResourceRecord>;

            /*
            * @brief 校验资源键。
            * @throws std::invalid_argument Key 无效时抛出。
            */
            static void ValidateKey(IN const CResourceKey& Key_);

            /*
            * @brief 规整资源信息。
            * @return 补齐 Key 和 Source 后的资源信息。
            */
            static CResourceInfo NormalizeInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_);

            /*
            * @brief 在已持锁时查找精确版本元数据。
            */
            const CResourceInfo* FindInfoLocked(
                IN const CResourceReference& Reference_) const noexcept;

            /*
            * @brief 校验新资源版本的依赖都存在且满足持久化约束。
            */
            void ValidateDependenciesLocked(
                IN const CResourceInfo& Info_) const;

            /*
            * @brief 在持有 m_Mutex 独占锁时归档一个即将过期的当前版本。
            */
            void ArchiveRecordLocked(
                IN const CResourceKey& Key_,
                IN const CResourceRecord& Record_);

            /*
            * @brief DELETE 的内部实现：移除当前入口并把当前版本归档。
            */
            EResourceMutationResult DeleteCurrentVersioned(
                IN const CResourceKey& Key_,
                IN EResourceVersionCondition Condition_,
                IN uint64_t nExpectedVersion_);

            /*
            * @brief 初始化内置编解码器和本池独占的临时目录。
            */
            void InitializeVersionStorage();

        private:
            mutable std::shared_mutex m_Mutex;
            std::map<CResourceKey, CResourceRecord> m_mapResources;
            mutable std::map<CResourceKey, CArchivedVersionMap>
                m_mapArchivedVersions;
            std::map<CResourceKey, uint64_t> m_mapVersionHighWaterMarks;
            std::map<std::type_index, CResourceVersionCodec>
                m_mapVersionCodecs;
            CResourceVersionStorageOptions m_VersionStorageOptions;
            std::filesystem::path m_VersionStorageDirectory;
            uint64_t m_nArchiveFileSequence = 0;
        };
    }
}
