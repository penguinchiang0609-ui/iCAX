#pragma once
#include "Services.h"
#include "IMailBoxService.h"
#include "ServicesHelper.h"
#include <unordered_map>

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 邮箱服务
        */
        class CMailBoxService : public IMailBoxService
        {
        public:
            /*
            * @brief 构造函数
            */
            CMailBoxService();

            /*
            * @brief 析构函数
            */
            virtual ~CMailBoxService();

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
            * @brief 获取前端邮件柜
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮件柜引用
            */
            virtual iCAX::Mailbox::CMailBox& GetOutBox(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 获取后端邮件柜
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮件柜引用
            */
            virtual iCAX::Mailbox::CMailBox& GetInBox(IN const iCAX::Data::uuid& ID_) override;

        private:
            std::unordered_map<iCAX::Data::uuid, iCAX::Mailbox::CMailBox> m_FrontendMailBoxes; //!< 前端邮件柜
            std::unordered_map<iCAX::Data::uuid, iCAX::Mailbox::CMailBox> m_BackendMailBoxes;  //!< 后端邮件柜

            AUTO_REGIST_SERVICE(IMailBoxService, CMailBoxService);
        };
    }
}
