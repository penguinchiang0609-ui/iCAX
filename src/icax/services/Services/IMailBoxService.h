#pragma once
#include "Services.h"
#include <string>
#include "IService.h"
#include "Mailbox/MailBox.h"
#include <vector>
#include "Data/uuid.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 邮箱服务接口
        */
        class _SERVICE_EXP IMailBoxService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IMailBoxService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IMailBoxService() = default;

        public:
            //! 发件和收件是相对引擎而言!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

            /*
            * @brief 获取发件箱
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮件柜引用
            */
            virtual iCAX::Mailbox::CMailBox& GetOutBox(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取收件箱
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮件柜引用
            */
            virtual iCAX::Mailbox::CMailBox& GetInBox(IN const iCAX::Data::uuid& ID_) = 0;
        };
    }
}
