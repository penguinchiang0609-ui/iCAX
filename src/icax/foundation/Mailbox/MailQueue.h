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
        *   3、Enqueue 对 Mail 做浅拷贝，Payload 所有权进入队列。
        *   4、Drain 会把队列内邮件整体转交给调用方，调用方负责释放 Payload。
        *   5、Clear 会释放当前仍滞留在队列中的 Payload。
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
            * @param Mail_ 邮件
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
            */
            void Clear();

        private:
            static void ReleasePayloads(std::vector<Mail>& Mails_) noexcept;

        private:
            mutable std::mutex m_Mutex;
            std::vector<Mail> m_vecMails;
        };
    }
}
