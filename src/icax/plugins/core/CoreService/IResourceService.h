#pragma once
#include "Core.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <typeindex>
#include "Data/uuid.h"
#include "Services/IService.h"

using namespace iCAX::Services;

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 资源池
        */
        class _CORE_EXP IResourceService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IResourceService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IResourceService() = default;

        public:
            // 获取资源
            template<typename T>
            std::shared_ptr<T> Get(IN const std::string& strKey_)
            {
                return std::static_pointer_cast<T>(GetResource(typeid(T), strKey_));
            }

            // 获取资源
            template<typename T>
            void Set(IN const std::string& strKey_, IN const std::shared_ptr<T>& pData_)
            {
                SetResource(typeid(T), strKey_, std::static_pointer_cast<void>(pData_));
            }

            // 卸载资源
            template<typename T>
            void Delete(IN const std::string& strKey_)
            {
                DeleteResource(typeid(T), strKey_);
            }

        public:
            /*
            * @brief 根据类型以及资源的KEY获取资源
            * @param [in] nType_
            * @param [in] strKey_
            * @return std::shared_ptr<void>
            */
            virtual std::shared_ptr<void> GetResource(IN const std::type_index& nType_, IN const std::string& strKey_) = 0;

            /*
            * @brief 根据类型以及资源的KEY设置资源
            * @param [in] nType_
            * @param [in] strKey_
            * @param [in] pResource_
            */
            virtual void SetResource(IN const std::type_index& nType_, IN const std::string& strKey_, IN const std::shared_ptr<void>& pResource_) = 0;

            /*
            * @brief 释放资源
            * @param [in] nType_
            * @param [in] strKey_
            */
            virtual void DeleteResource(IN const std::type_index& nType_, IN const std::string& strKey_) = 0;

        };
    }
}