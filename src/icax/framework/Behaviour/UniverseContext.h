#pragma once
#include "System.h"
#include "IUniverseContext.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 上下文
        * @remark 默认 UniverseContext 保存当前 Project 的 Repository、应用设置和 Timer。
        */
        class _SYSTEM_EXP CUniverseContext : public IUniverseContext
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pRepository_ 当前项目 Repository。
            * @param [in] pApplicationSetting_ 应用设置属性包。
            */
            CUniverseContext(IN const std::shared_ptr<iCAX::Database::IRepository>& pRepository_, 
                IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplicationSetting_);

            /*
            * @brief 析构函数
            * @details 释放 SetData 接管的所有临时裸数据。
            */
            virtual ~CUniverseContext();

        public:
            /*
            * @brief 获取应用程序设置
            * @return CSetting&
            */
            virtual iCAX::Data::PropertyBag& GetApplicationSettings() const override;

            /*
            * @brief 获取数据仓储
            * @return iCAX::Database::IRepository&
            */
            virtual iCAX::Database::IRepository& GetDatabase() const override;

            /*
            * @brief 获取计时器
            * @return iCAX::Behaviour::ITimer&
            */
            virtual iCAX::Behaviour::ITimer& GetTimer() const override;

        public:
            /*
            * @brief 设置数据
            * @param [in] nKey_ 项ID
            * @param [in] pData_
            */
            virtual void SetData(IN const uint32_t& nKey_, IN uint8_t* pData_) override;

            /*
            * @brief 获取数据
            * @param [in] nKey_
            * @return uint8_t*
            */
            virtual uint8_t* GetData(IN const uint32_t& nKey_) override;

            /*
            * @brief 移除
            * @param [in] nKey_
            */
            virtual void Remove(IN const uint32_t& nKey_) override;

        private:
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSetting;
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
            std::shared_ptr<iCAX::Behaviour::ITimer> m_pTimer;
            std::unordered_map<uint32_t, uint8_t*> m_mapContext;
        };
    }
}
