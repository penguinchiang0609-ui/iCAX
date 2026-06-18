#pragma once

#include "ResourceLoaderRegistry.h"
#include "ResourcePool.h"

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace iCAX
{
    namespace Resource
    {
        inline CResourceInfo BuildLoadInfo(
            IN const CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_)
        {
            auto _Info = Info_;

            auto _StoredInfo = Pool_.GetInfo(Key_);
            if (_StoredInfo.has_value())
            {
                auto _Merged = _StoredInfo.value();
                if (!_Info.Name.empty())
                {
                    _Merged.Name = _Info.Name;
                }
                if (!_Info.Source.empty())
                {
                    _Merged.Source = _Info.Source;
                }
                if (!_Info.ContentHash.empty())
                {
                    _Merged.ContentHash = _Info.ContentHash;
                }
                if (_Info.nSize != 0)
                {
                    _Merged.nSize = _Info.nSize;
                }
                if (_Info.nFlags != 0)
                {
                    _Merged.nFlags = _Info.nFlags;
                }
                if (_Info.Persistence != EResourcePersistenceMode::RuntimeOnly)
                {
                    _Merged.Persistence = _Info.Persistence;
                }
                for (const auto& _Pair : _Info.Metadata)
                {
                    _Merged.Metadata[_Pair.first] = _Pair.second;
                }
                _Info = std::move(_Merged);
            }

            _Info.Key = Key_;
            if (!strSource_.empty())
            {
                _Info.Source = strSource_;
            }
            else if (_Info.Source.empty())
            {
                _Info.Source = Key_.Source;
            }
            return _Info;
        }

        inline CResourceLoadContext MakeLoadContext(
            IN CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN std::type_index TargetResourceType_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_,
            IN const std::map<std::string, std::string>& Options_)
        {
            CResourceLoadContext _Context;
            _Context.TargetKey = Key_;
            _Context.TargetResourceType = TargetResourceType_;
            _Context.Info = BuildLoadInfo(Pool_, Key_, strSource_, Info_);
            _Context.Source = strSource_.empty() ? _Context.Info.Source : strSource_;
            _Context.Options = Options_;
            return _Context;
        }

        inline bool StoreLoadResult(IN CResourcePool& Pool_, IN const CResourceLoadResult& Result_)
        {
            if (!Result_.IsOK() || !Result_.Info.Key.IsValid())
            {
                return false;
            }

            if (Result_.pResource)
            {
                if (!Result_.RuntimeType.has_value())
                {
                    return false;
                }
                Pool_.SetUntyped(Result_.Info.Key, Result_.pResource, Result_.RuntimeType.value(), Result_.Info);
            }
            else
            {
                Pool_.Register(Result_.Info);
            }
            return true;
        }

        template <typename T>
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (!Key_.IsValid())
            {
                return nullptr;
            }

            using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
            auto _pExisting = Pool_.GetUntyped(Key_, typeid(TResource));
            if (_pExisting)
            {
                return std::static_pointer_cast<TResource>(_pExisting);
            }

            if (Pool_.HasObject(Key_))
            {
                return nullptr;
            }

            const auto _TargetResourceType = std::type_index(typeid(TResource));
            auto _Context = MakeLoadContext(Pool_, Key_, _TargetResourceType, Key_.Source, Info_, Options_);
            auto _Result = CResourceLoaderRegistry::Load(_Context);
            if (!_Result.IsOK())
            {
                return nullptr;
            }
            if (!StoreLoadResult(Pool_, _Result))
            {
                return nullptr;
            }

            auto _pLoaded = Pool_.GetUntyped(Key_, typeid(TResource));
            if (!_pLoaded)
            {
                return nullptr;
            }
            return std::static_pointer_cast<TResource>(_pLoaded);
        }

        template <typename T>
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (strSource_.empty())
            {
                return nullptr;
            }

            return Load<T>(Pool_, MakeResourceKeyFromSource(strSource_), Info_, Options_);
        }
    }
}
