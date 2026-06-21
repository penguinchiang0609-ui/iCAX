#pragma once
#include "MailExport.h"
#include "Mail.h"
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 单向邮件队列
        * @remark
        *   1、MailQueue 只负责一个方向上的邮件暂存。
        *   2、它不表达双向收发语义。
        *   3、Enqueue 会复制 MailHeader，并深拷贝 Payload。
        *   4、调用方仍拥有传入 Mail 的 Payload，需要按自己的所有权规则释放。
        *   5、Drain 会把队列内邮件整体转交给调用方，调用方负责释放返回邮件的 Payload。
        *   6、Clear 会释放当前仍滞留在队列中的 Payload。
        */
        class _MAIL_EXP CMailQueue final
        {
        public:
            CMailQueue();
            ~CMailQueue();
            CMailQueue(const CMailQueue&) = delete;
            CMailQueue& operator=(const CMailQueue&) = delete;
            CMailQueue(CMailQueue&& Other_) noexcept;
            CMailQueue& operator=(CMailQueue&& Other_) noexcept;

        public:
            /*
            * @brief 入队邮件
            * @param [in] Mail_ 待写入队列的邮件。
            * @details
            *   会复制 MailHeader 并深拷贝 Payload。
            *   如果 nSize 大于 0 但 pData 为空，会抛出 std::invalid_argument。
            */
            void Enqueue(const Mail& Mail_);

            /*
            * @brief 取出当前全部邮件
            * @return 邮件列表
            * @details 取出后队列清空，返回邮件的 Payload 内存由调用方负责释放。
            */
            std::vector<Mail> Drain();

            /*
            * @brief 清空队列
            * @details 清空时会释放仍滞留在队列中的 Payload。
            */
            void Clear();

        private:
            /*
            * @brief 释放一组邮件的 Payload。
            * @param [in,out] Mails_ 待释放负载的邮件数组；释放后 pData 置空，nSize 置 0。
            */
            static void ReleasePayloads(std::vector<Mail>& Mails_) noexcept;

        private:
            mutable std::mutex m_Mutex;
            std::vector<Mail> m_vecMails;
        };
    }
}
