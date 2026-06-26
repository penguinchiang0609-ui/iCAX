#pragma once
#include "Core.h"
#include "ISettingService.h"
#include "Services/ServicesHelper.h"

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 配置
        */
        class _CORE_EXP CSettingService : public ISettingService
        {
        public:
            /*
            * @brief 构造函数
            */
            CSettingService();

            /*
            * @brief 析构函数
            */
            virtual ~CSettingService();

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

        public:
            /*
            * @brief 设置
            * @param [in] pApplication_
            * @param [in] pUsr_
            */
            virtual void SetSetting(IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplication_, IN const std::shared_ptr<iCAX::Data::PropertyBag>& pUsr_) override;

            /*
            * @brief 获取应用程序配置
            * @return iCAX::Data::PropertyBag&
            */
            virtual const iCAX::Data::PropertyBag& GetApplicationSetting() const override;

            /*
            * @brief 获取用户配置
            * @return iCAX::Data::PropertyBag&
            */
            virtual const iCAX::Data::PropertyBag& GetUsrSetting() const override;

        private:
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplication;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pUsr;
            iCAX::Data::PropertyBag m_EmptyApplication;
            iCAX::Data::PropertyBag m_EmptyUsr;

            AUTO_REGIST_SERVICE(ISettingService, CSettingService);
        };
    }
}
