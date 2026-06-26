#pragma once

#include "ResourceInfo.h"

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
        /*
        * @brief 项目级资源池。
        * @details
        *   ResourcePool 保存资源清单和已加载对象。它不负责加载逻辑，加载由 ResourceLibrary 或 Resource::Load 完成。
        *   同一个 Project 应持有自己的 ResourcePool/ResourceLibrary，以隔离不同项目的资源缓存和持久化清单。
        */
        class _RESOURCES_EXP CResourcePool final
        {
        public:
            CResourcePool() = default;
            ~CResourcePool() = default;

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
            * @brief 判断资源记录是否存在。
            */
            bool Contains(IN const CResourceKey& Key_) const;

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
            * @brief 移除资源记录和对象。
            * @return true 表示移除了已有记录。
            */
            bool Remove(IN const CResourceKey& Key_);

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

            /*
            * @brief 标记指定来源资源内容发生变化。
            * @return 递增后的版本；资源不存在时返回 0。
            */
            uint64_t Touch(IN const std::string& strSource_)
            {
                return Touch(CResourceKey{ strSource_ });
            }

        private:
            struct CResourceRecord final
            {
                CResourceInfo Info;
                std::optional<std::type_index> RuntimeType;
                std::shared_ptr<void> pResource;
            };

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

        private:
            mutable std::shared_mutex m_Mutex;
            std::map<CResourceKey, CResourceRecord> m_mapResources;
        };
    }
}
