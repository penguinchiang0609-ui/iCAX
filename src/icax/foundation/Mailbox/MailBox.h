#pragma once
#include "MailboxExport.h"
#include "Mail.h"
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Mailbox
    {
        /*
        * @brief 邮件柜
        * @remark
        *   1、邮件柜用于存储和管理邮件。
        *   2、邮件柜提供投递邮件和获取邮件的接口。
        *   3、邮件柜内部不处理邮件的具体业务逻辑，业务逻辑由接收方处理。
        *   4、邮件柜内部使用锁保护队列，支持多线程投递、取出和清空。
        *   5、RetrieveMails 会把邮件所有权交给调用方，调用方负责释放 Payload 内存。
        *   6、ClearMails 会释放当前仍滞留在邮件柜中的 Payload 内存。
        *   7、邮件柜不处理大消息分块问题，由投递方自己处理分块和重组。
        */
        class _MAILBOX_EXP CMailBox final
        {
        public:
            CMailBox();
            ~CMailBox();
            CMailBox(const CMailBox&) = delete;
            CMailBox& operator=(const CMailBox&) = delete;
            CMailBox(CMailBox&& Other_) noexcept;
            CMailBox& operator=(CMailBox&& Other_) noexcept;

        public:
            /*
            * @brief 投递邮件
            * @param Mail_ 邮件
            */
            void DeliverMail(const Mail& Mail_);

            /*
            * @brief 获取所有邮件
            * @return 邮件列表
            * @details 获取后邮件柜清空，返回邮件的 Payload 内存由调用方负责释放。
            */
            std::vector<Mail> RetrieveMails();

            /*
            * @brief 清空邮件
            */
            void ClearMails();

        private:
            static void ReleasePayloads(std::vector<Mail>& Mails_) noexcept;

        private:
            mutable std::mutex m_Mutex;
            std::vector<Mail> m_vecMails;
        };
    }
}
