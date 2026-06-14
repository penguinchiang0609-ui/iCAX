#pragma once
#include "System.h"
#include "Database/IRepository.h"
#include "Data/PropertyBag.h"
#include "ITimer.h"
#include "Services/ServiceProvider.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 宇宙上下文
        */
        class _SYSTEM_EXP IUniverseContext
        {
        public:
            /*
            * @brief
            */
            virtual ~IUniverseContext() = default;

        public:
            /*
            * @brief 获取应用程序设置
            * @return CSetting&
            */
            virtual iCAX::Data::PropertyBag& GetApplicationSettings() const = 0;

            /*
            * @brief 获取数据仓储
            * @return iCAX::Database::IRepository&
            */
            virtual iCAX::Database::IRepository& GetDatabase() const = 0;

            /*
            * @brief 获取计时器
            * @return iCAX::Behaviour::ITimer&
            */
            virtual iCAX::Behaviour::ITimer& GetTimer() const = 0;

        public:
            /*
            * @brief 设置数据
            * @param [in] nKey_ 项ID
            * @param [in] pData_
            */
            virtual void SetData(IN const uint32_t& nKey_, IN uint8_t* pData_) = 0;

            /*
            * @brief 获取数据
            * @param [in] nKey_
            * @return uint8_t*
            */
            virtual uint8_t* GetData(IN const uint32_t& nKey_) = 0;

            /*
            * @brief 移除
            * @param [in] nKey_
            */
            virtual void Remove(IN const uint32_t& nKey_) = 0;
        };
    }
}
