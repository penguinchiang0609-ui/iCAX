#pragma once

#include "ResourceKey.h"

#include <cstdint>
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
        struct _RESOURCE_POOL_EXP CResourceInfo final
        {
            CResourceKey Key;
            std::string Name;
            std::string Source;
            uint64_t nSize = 0;
            uint32_t nFlags = 0;
            std::map<std::string, std::string> Metadata;
        };

        template <typename T>
        std::string GetDefaultResourceType()
        {
            using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
            return typeid(TResource).name();
        }

        template <typename T>
        CResourceKey MakeResourceKey(IN const iCAX::Data::uuid& ID_, IN const std::string& strType_ = GetDefaultResourceType<T>())
        {
            return CResourceKey{ strType_, ID_ };
        }

        class _RESOURCE_POOL_EXP CResourcePool final
        {
        public:
            CResourcePool() = default;
            ~CResourcePool() = default;

            CResourcePool(IN const CResourcePool&) = delete;
            CResourcePool& operator=(IN const CResourcePool&) = delete;

        public:
            void SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_) const;
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const;

            bool Contains(IN const CResourceKey& Key_) const;
            bool Remove(IN const CResourceKey& Key_);
            void Clear();
            size_t Count() const;

            std::optional<CResourceInfo> GetInfo(IN const CResourceKey& Key_) const;
            bool UpdateInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_);
            std::vector<CResourceKey> GetKeys(IN const std::string& strType_ = std::string()) const;
            std::vector<CResourceInfo> GetInfos(IN const std::string& strType_ = std::string()) const;
            std::string GetRuntimeTypeName(IN const CResourceKey& Key_) const;

            template <typename T>
            void Set(IN const iCAX::Data::uuid& ID_, IN std::shared_ptr<T> pResource_, IN const std::string& strType_ = GetDefaultResourceType<T>(), IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                SetUntyped(CResourceKey{ strType_, ID_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
            bool TryAdd(IN const iCAX::Data::uuid& ID_, IN std::shared_ptr<T> pResource_, IN const std::string& strType_ = GetDefaultResourceType<T>(), IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                return TryAddUntyped(CResourceKey{ strType_, ID_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
            std::shared_ptr<T> Get(IN const iCAX::Data::uuid& ID_, IN const std::string& strType_ = GetDefaultResourceType<T>()) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = GetUntyped(CResourceKey{ strType_, ID_ }, typeid(TResource));
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

        private:
            struct CResourceRecord final
            {
                CResourceInfo Info;
                std::type_index RuntimeType = std::type_index(typeid(void));
                std::shared_ptr<void> pResource;
            };

            static void ValidateKey(IN const CResourceKey& Key_);
            static CResourceInfo NormalizeInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_);

        private:
            mutable std::shared_mutex m_Mutex;
            std::map<CResourceKey, CResourceRecord> m_mapResources;
        };
    }
}
