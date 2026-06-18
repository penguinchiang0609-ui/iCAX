#pragma once

#include "ResourceInfo.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeindex>

namespace iCAX
{
    namespace Resource
    {
        enum class EResourceLoadStatus : uint8_t
        {
            Ok = 0,
            NoLoader = 1,
            UnsupportedSource = 2,
            InvalidResult = 3,
            Failed = 4
        };

        struct _RESOURCES_EXP CResourceLoadResult final
        {
            EResourceLoadStatus Status = EResourceLoadStatus::Failed;
            CResourceInfo Info;
            std::shared_ptr<void> pResource;
            std::optional<std::type_index> RuntimeType;
            std::string Error;

            bool IsOK() const noexcept
            {
                return Status == EResourceLoadStatus::Ok;
            }

            static CResourceLoadResult Succeeded(IN const CResourceInfo& Info_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::Ok;
                _Result.Info = Info_;
                return _Result;
            }

            template <typename T>
            static CResourceLoadResult Succeeded(IN const CResourceInfo& Info_, IN std::shared_ptr<T> pResource_)
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;

                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::Ok;
                _Result.Info = Info_;
                _Result.pResource = std::static_pointer_cast<void>(pResource_);
                _Result.RuntimeType = std::type_index(typeid(TResource));
                return _Result;
            }

            static CResourceLoadResult NoLoader(IN const CResourceKey& Key_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::NoLoader;
                _Result.Info.Key = Key_;
                _Result.Error = "Resource loader not found";
                return _Result;
            }

            static CResourceLoadResult Unsupported(IN const CResourceKey& Key_, IN const std::string& strMessage_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::UnsupportedSource;
                _Result.Info.Key = Key_;
                _Result.Error = strMessage_;
                return _Result;
            }

            static CResourceLoadResult Failed(IN const CResourceKey& Key_, IN const std::string& strMessage_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::Failed;
                _Result.Info.Key = Key_;
                _Result.Error = strMessage_;
                return _Result;
            }

            static CResourceLoadResult Invalid(IN const CResourceKey& Key_, IN const std::string& strMessage_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::InvalidResult;
                _Result.Info.Key = Key_;
                _Result.Error = strMessage_;
                return _Result;
            }
        };
    }
}
