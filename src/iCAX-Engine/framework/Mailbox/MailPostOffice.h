#pragma once
#include "MailExport.h"
#include "Mail.h"
#include "MailQueue.h"
#include <memory>
#include <vector>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮局
        * @remark
        *   1、MailPostOffice 是某一端看到的轻量收发视图。
        *   2、一个 MailPostOffice 只保存一个收件队列弱引用和一个发件队列弱引用。
        *   3、Send 会把邮件写入对端的收件队列。
        *   4、Receive 会从本端的收件队列取出邮件。
        *   5、MailPostOffice 不拥有队列，不分配内存，不释放队列。
        *   6、底层通道释放后，旧 MailPostOffice 会失效并在使用时抛出异常。
        */
        class _MAIL_EXP CMailPostOffice final
        {
        public:
            CMailPostOffice() = default;
            /*
            * @brief 构造绑定到指定收/发队列的邮局。
            * @param [in] pIncomingQueue_ 本端收件队列。
            * @param [in] pOutgoingQueue_ 本端发件队列，也就是对端收件队列。
            */
            CMailPostOffice(
                std::shared_ptr<CMailQueue> pIncomingQueue_,
                std::shared_ptr<CMailQueue> pOutgoingQueue_) noexcept;
            ~CMailPostOffice() = default;
            CMailPostOffice(const CMailPostOffice&) = default;
            CMailPostOffice& operator=(const CMailPostOffice&) = default;
            CMailPostOffice(CMailPostOffice&& Other_) noexcept = default;
            CMailPostOffice& operator=(CMailPostOffice&& Other_) noexcept = default;

        public:
            /*
            * @brief 当前邮局是否已经绑定队列
            * @return true 表示收件队列和发件队列都仍然存在。
            */
            bool IsValid() const noexcept;

            /*
            * @brief 发送邮件
            * @param [in] Mail_ 待发送邮件。
            * @details
            *   Send 会深拷贝 Payload，调用方仍拥有传入 Mail 的 Payload。
            *   如果底层 Channel 被 Reset 或释放，发送会抛出 std::logic_error。
            */
            void Send(const Mail& Mail_) const;

            /*
            * @brief 接收当前全部邮件
            * @return 邮件列表，顺序与对端 Send 顺序一致。
            * @details
            *   接收后收件队列清空，返回邮件的 Payload 内存由调用方负责释放。
            *   如果底层 Channel 被 Reset 或释放，接收会抛出 std::logic_error。
            */
            std::vector<Mail> Receive() const;

            /*
            * @brief 清空收件队列
            * @details 会释放本端尚未 Receive 的邮件 Payload。
            */
            void ClearIncoming() const;

            /*
            * @brief 清空发件队列
            * @details 会释放本端已 Send、但对端尚未 Receive 的邮件 Payload。
            */
            void ClearOutgoing() const;

        private:
            /*
            * @brief 获取有效收件队列。
            * @return 绑定的收件队列。
            * @throws std::logic_error 邮局未绑定或队列已经失效。
            */
            std::shared_ptr<CMailQueue> RequireIncomingQueue() const;

            /*
            * @brief 获取有效发件队列。
            * @return 绑定的发件队列。
            * @throws std::logic_error 邮局未绑定或队列已经失效。
            */
            std::shared_ptr<CMailQueue> RequireOutgoingQueue() const;

        private:
            std::weak_ptr<CMailQueue> m_pIncomingQueue;
            std::weak_ptr<CMailQueue> m_pOutgoingQueue;
        };
    }
}
