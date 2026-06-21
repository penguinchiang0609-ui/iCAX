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
        /*
        * @brief 资源加载结果状态。
        */
        enum class EResourceLoadStatus : uint8_t
        {
            Ok = 0,
            NoLoader = 1,
            UnsupportedSource = 2,
            InvalidResult = 3,
            Failed = 4
        };

        /*
        * @brief 资源加载结果。
        * @details
        *   成功结果可以只登记 Info，不一定要立刻返回对象。
        *   如果 pResource 非空，RuntimeType 必须填写，ResourcePool 会据此做 typeid 精确匹配。
        */
        struct _RESOURCES_EXP CResourceLoadResult final
        {
            EResourceLoadStatus Status = EResourceLoadStatus::Failed;
            CResourceInfo Info;
            std::shared_ptr<void> pResource;
            std::optional<std::type_index> RuntimeType;
            std::string Error;

            /*
            * @brief 判断加载是否成功。
            * @return true 表示 Status 为 Ok。
            */
            bool IsOK() const noexcept
            {
                return Status == EResourceLoadStatus::Ok;
            }

            /*
            * @brief 创建仅登记资源信息的成功结果。
            * @param [in] Info_ 资源信息。
            * @return 成功结果，不包含资源对象。
            */
            static CResourceLoadResult Succeeded(IN const CResourceInfo& Info_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::Ok;
                _Result.Info = Info_;
                return _Result;
            }

            /*
            * @brief 创建包含资源对象的成功结果。
            * @param [in] Info_ 资源信息。
            * @param [in] pResource_ 已加载对象。
            * @return 成功结果，RuntimeType 自动记录为 T。
            */
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

            /*
            * @brief 创建未找到加载器的结果。
            */
            static CResourceLoadResult NoLoader(IN const CResourceKey& Key_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::NoLoader;
                _Result.Info.Key = Key_;
                _Result.Error = "Resource loader not found";
                return _Result;
            }

            /*
            * @brief 创建来源不受支持的结果。
            */
            static CResourceLoadResult Unsupported(IN const CResourceKey& Key_, IN const std::string& strMessage_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::UnsupportedSource;
                _Result.Info.Key = Key_;
                _Result.Error = strMessage_;
                return _Result;
            }

            /*
            * @brief 创建加载失败结果。
            */
            static CResourceLoadResult Failed(IN const CResourceKey& Key_, IN const std::string& strMessage_)
            {
                CResourceLoadResult _Result;
                _Result.Status = EResourceLoadStatus::Failed;
                _Result.Info.Key = Key_;
                _Result.Error = strMessage_;
                return _Result;
            }

            /*
            * @brief 创建加载器返回值非法的结果。
            */
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
