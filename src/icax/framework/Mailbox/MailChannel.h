#pragma once
#include "MailExport.h"
#include "MailPostOffice.h"
#include "MailQueue.h"
#include <cstdint>
#include <memory>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮件通道端点
        */
        enum MailChannelEnd : uint8_t
        {
            kMailEndA = 0,
            kMailEndB = 1,
        };

        /*
        * @brief 双向邮件通道
        * @remark
        *   一个 MailChannel 内部包含两个 MailQueue：
        *   1、EndA -> EndB
        *   2、EndB -> EndA
        */
        class _MAIL_EXP CMailChannel final
        {
        public:
            CMailChannel() = default;
            ~CMailChannel() = default;
            CMailChannel(const CMailChannel&) = delete;
            CMailChannel& operator=(const CMailChannel&) = delete;
            CMailChannel(CMailChannel&& Other_) noexcept = delete;
            CMailChannel& operator=(CMailChannel&& Other_) noexcept = delete;

        public:
            /*
            * @brief 获取指定端点视角的邮局
            * @param End_ 端点
            * @details 返回的是轻量非拥有弱引用视图，不分配内存。
            */
            CMailPostOffice GetPostOffice(IN MailChannelEnd End_) noexcept;

            /*
            * @brief 获取 EndA 视角的邮局
            * @details 返回的是轻量非拥有弱引用视图，不分配内存。
            */
            CMailPostOffice GetEndAPostOffice() noexcept;

            /*
            * @brief 获取 EndB 视角的邮局
            * @details 返回的是轻量非拥有弱引用视图，不分配内存。
            */
            CMailPostOffice GetEndBPostOffice() noexcept;

            /*
            * @brief 清空两个方向上的待处理邮件
            */
            void Clear();

            /*
            * @brief 重置通道
            * @details 丢弃当前两个方向的队列，并让已经发出去的邮局视图失效。
            */
            void Reset();

            /*
            * @brief 获取 EndA 到 EndB 的底层队列
            */
            CMailQueue& GetAToBQueue() noexcept;

            /*
            * @brief 获取 EndB 到 EndA 的底层队列
            */
            CMailQueue& GetBToAQueue() noexcept;

        private:
            std::shared_ptr<CMailQueue> m_AToB = std::make_shared<CMailQueue>();
            std::shared_ptr<CMailQueue> m_BToA = std::make_shared<CMailQueue>();
        };
    }
}
