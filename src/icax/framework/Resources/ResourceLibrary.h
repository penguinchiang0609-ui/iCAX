#pragma once

#include "ResourceInfo.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourcePool;

        class _RESOURCES_EXP CResourceLibrary final
        {
        public:
            CResourceLibrary();
            ~CResourceLibrary();

            CResourceLibrary(IN const CResourceLibrary&) = delete;
            CResourceLibrary& operator=(IN const CResourceLibrary&) = delete;
            CResourceLibrary(IN CResourceLibrary&& Other_) noexcept;
            CResourceLibrary& operator=(IN CResourceLibrary&& Other_) noexcept;

        public:
            template <typename T>
            std::shared_ptr<T> Load(
                IN const std::string& strSource_,
                IN const CResourceInfo& Info_ = CResourceInfo(),
                IN const std::map<std::string, std::string>& Options_ = {})
            {
                if (strSource_.empty())
                {
                    return nullptr;
                }

                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = LoadUntyped(strSource_, typeid(TResource), Info_, Options_);
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            template <typename T>
            void Set(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                SetUntyped(strSource_, std::static_pointer_cast<void>(std::move(pResource_)), typeid(TResource), Info_);
            }

            template <typename T>
            bool TryAdd(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                return TryAddUntyped(strSource_, std::static_pointer_cast<void>(std::move(pResource_)), typeid(TResource), Info_);
            }

            template <typename T>
            std::shared_ptr<T> Get(IN const std::string& strSource_) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = GetUntyped(strSource_, typeid(TResource));
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            void Register(IN const std::string& strSource_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool TryRegister(IN const std::string& strSource_, IN const CResourceInfo& Info_ = CResourceInfo());
            bool Contains(IN const std::string& strSource_) const;
            bool HasObject(IN const std::string& strSource_) const;
            bool Unload(IN const std::string& strSource_);
            bool Remove(IN const std::string& strSource_);
            void Clear();
            size_t Count() const;
            std::optional<CResourceInfo> GetInfo(IN const std::string& strSource_) const;
            bool UpdateInfo(IN const std::string& strSource_, IN const CResourceInfo& Info_);
            std::vector<CResourceKey> GetKeys() const;
            std::vector<CResourceInfo> GetInfos() const;
            std::vector<CResourceInfo> GetManifest(IN bool bIncludeRuntimeOnly_ = false) const;

        private:
            CResourcePool& GetPool();
            const CResourcePool& GetPool() const;
            std::shared_ptr<void> LoadUntyped(
                IN const std::string& strSource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_,
                IN const std::map<std::string, std::string>& Options_);
            void SetUntyped(
                IN const std::string& strSource_,
                IN std::shared_ptr<void> pResource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_);
            bool TryAddUntyped(
                IN const std::string& strSource_,
                IN std::shared_ptr<void> pResource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_);
            std::shared_ptr<void> GetUntyped(
                IN const std::string& strSource_,
                IN const std::type_info& RuntimeType_) const;

        private:
            std::unique_ptr<CResourcePool> m_pPool;
        };
    }
}
