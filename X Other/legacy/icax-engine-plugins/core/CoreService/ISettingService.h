#pragma once
#include "Core.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <typeindex>
#include "Data/uuid.h"
#include "Services/IService.h"
#include <Data/PropertyBag.h>

using namespace iCAX::Services;

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 配置
        */
        class _CORE_EXP ISettingService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            ISettingService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~ISettingService() = default;

        public:
            /*
            * @brief 设置
            * @param [in] pApplication_
            * @param [in] pUsr_
            */
            virtual void SetSetting(IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplication_, IN const std::shared_ptr<iCAX::Data::PropertyBag>& pUsr_) = 0;

            /*
            * @brief 获取应用程序配置
            * @return iCAX::Data::PropertyBag&
            */
            virtual const iCAX::Data::PropertyBag& GetApplicationSetting() const = 0;

            /*
            * @brief 获取用户配置
            * @return iCAX::Data::PropertyBag&
            */
            virtual const iCAX::Data::PropertyBag& GetUsrSetting() const = 0;

        };
    }
}