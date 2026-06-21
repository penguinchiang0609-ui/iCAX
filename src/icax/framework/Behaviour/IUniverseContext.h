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
        * @remark
        *   Context 是 Project 每帧传入 Universe 的运行环境。
        *   Behaviour 通过它访问当前项目的 Repository、计时器和应用设置，避免 Behaviour 持有项目状态。
        */
        class _SYSTEM_EXP IUniverseContext
        {
        public:
            /*
            * @brief 析构函数
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
            * @param [in] pData_ 裸字节数组指针；Context 接管所有权，析构或 Remove 时 delete[]。
            * @details 该接口用于少量运行期临时数据。优先使用类型明确的 Component/Service。
            */
            virtual void SetData(IN const uint32_t& nKey_, IN uint8_t* pData_) = 0;

            /*
            * @brief 获取数据
            * @param [in] nKey_ 项 ID。
            * @return 临时数据指针；不存在时返回 nullptr。
            */
            virtual uint8_t* GetData(IN const uint32_t& nKey_) = 0;

            /*
            * @brief 移除
            * @param [in] nKey_ 项 ID。
            * @details 如果存在对应数据，会 delete[] 释放。
            */
            virtual void Remove(IN const uint32_t& nKey_) = 0;
        };
    }
}
