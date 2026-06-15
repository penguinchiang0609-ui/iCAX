#pragma once
#include "MailExport.h"
#include "Mail.h"
#include "MailQueue.h"
#include <vector>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮局
        * @remark
        *   1、MailPostOffice 是某一端看到的轻量收发视图。
        *   2、一个 MailPostOffice 只保存一个收件队列指针和一个发件队列指针。
        *   3、Send 会把邮件写入对端的收件队列。
        *   4、Receive 会从本端的收件队列取出邮件。
        *   5、MailPostOffice 不拥有队列，不分配内存，不释放队列。
        *   6、MailPostOffice 可以随时从 MailChannel 获取，成本等同于复制两个指针。
        */
        class _MAIL_EXP CMailPostOffice final
        {
        public:
            CMailPostOffice() = default;
            CMailPostOffice(CMailQueue& IncomingQueue_, CMailQueue& OutgoingQueue_) noexcept;
            ~CMailPostOffice() = default;
            CMailPostOffice(const CMailPostOffice&) = default;
            CMailPostOffice& operator=(const CMailPostOffice&) = default;
            CMailPostOffice(CMailPostOffice&& Other_) noexcept = default;
            CMailPostOffice& operator=(CMailPostOffice&& Other_) noexcept = default;

        public:
            /*
            * @brief 当前邮局是否已经绑定队列
            */
            bool IsValid() const noexcept;

            /*
            * @brief 发送邮件
            * @param Mail_ 邮件
            */
            void Send(const Mail& Mail_) const;

            /*
            * @brief 接收当前全部邮件
            * @return 邮件列表
            * @details 接收后收件队列清空，返回邮件的 Payload 内存由调用方负责释放。
            */
            std::vector<Mail> Receive() const;

            /*
            * @brief 清空收件队列
            */
            void ClearIncoming() const;

            /*
            * @brief 清空发件队列
            */
            void ClearOutgoing() const;

        private:
            CMailQueue& RequireIncomingQueue() const;
            CMailQueue& RequireOutgoingQueue() const;

        private:
            CMailQueue* m_pIncomingQueue = nullptr;
            CMailQueue* m_pOutgoingQueue = nullptr;
        };
    }
}
