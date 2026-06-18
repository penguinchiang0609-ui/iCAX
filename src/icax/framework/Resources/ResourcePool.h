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
        class _RESOURCES_EXP CResourcePool final
        {
        public:
            CResourcePool() = default;
            ~CResourcePool() = default;

            CResourcePool(IN const CResourcePool&) = delete;
            CResourcePool& operator=(IN const CResourcePool&) = delete;

        public:
            void Register(IN const CResourceInfo& Info_);
            bool TryRegister(IN const CResourceInfo& Info_);
            void SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            void SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_ = CResourceInfo());
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_) const;
            std::shared_ptr<void> GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const;

            bool Contains(IN const CResourceKey& Key_) const;
            bool HasObject(IN const CResourceKey& Key_) const;
            bool Unload(IN const CResourceKey& Key_);
            bool Remove(IN const CResourceKey& Key_);
            void Clear();
            size_t Count() const;

            std::optional<CResourceInfo> GetInfo(IN const CResourceKey& Key_) const;
            bool UpdateInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_);
            std::vector<CResourceKey> GetKeys() const;
            std::vector<CResourceInfo> GetInfos() const;
            std::vector<CResourceInfo> GetManifest(IN bool bIncludeRuntimeOnly_ = false) const;
            std::string GetRuntimeTypeName(IN const CResourceKey& Key_) const;

            template <typename T>
            void Set(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                SetUntyped(CResourceKey{ strSource_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
            bool TryAdd(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                return TryAddUntyped(CResourceKey{ strSource_ }, std::static_pointer_cast<void>(pResource_), typeid(TResource), Info_);
            }

            template <typename T>
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

        private:
            struct CResourceRecord final
            {
                CResourceInfo Info;
                std::optional<std::type_index> RuntimeType;
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
