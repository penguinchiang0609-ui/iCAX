#pragma once
#include "MailExport.h"
#include "Mail.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 单向邮件队列的预分配参数。
        * @details
        *   队列创建时一次性分配 record 池和 payload 池。后续 Send 只从池中切分空间，
        *   避免每封邮件在队列内部频繁 new/delete。
        */
        struct _MAIL_EXP CMailQueueCreateInfo final
        {
            size_t nMaxMailCount = 4096; //!< 队列中 pending + leased 邮件的最大数量。
            size_t nPayloadPoolBytes = 4ull * 1024ull * 1024ull; //!< Payload 预分配池大小。
            size_t nMaxPayloadBytesPerMail = 4ull * 1024ull * 1024ull; //!< 单封邮件允许的最大 payload。
        };

        /*
        * @brief 单向邮件队列
        * @remark
        *   1、MailQueue 只负责一个方向上的邮件暂存。
        *   2、它不表达双向收发语义。
        *   3、Enqueue 会复制 MailHeader，并把 Payload 拷贝到队列预分配 PayloadPool。
        *   4、调用方仍拥有传入 Mail 的 Payload，需要按自己的所有权规则释放。
        *   5、Drain 会把队列内 pending 邮件整体转交给调用方，调用方负责调用 ReleaseMailPayload 归还租约。
        *   6、Clear 会释放当前仍滞留在队列中的 pending Payload，不会影响已经 Drain 出去的 leased 邮件。
        */
        class _MAIL_EXP CMailQueue final
            : public IMailPayloadLease
            , public std::enable_shared_from_this<CMailQueue>
        {
        private:
            enum class ERecordState : uint8_t
            {
                Free,
                Pending,
                Leased
            };

            struct CRecord final
            {
                MailHeader Header;
                size_t nPayloadOffset = 0;
                size_t nPayloadSize = 0;
                size_t nPayloadBlockSize = 0;
                uint32_t nGeneration = 0;
                ERecordState eState = ERecordState::Free;
            };

            struct CFreeBlock final
            {
                size_t nOffset = 0;
                size_t nSize = 0;
            };

        public:
            CMailQueue();
            explicit CMailQueue(IN const CMailQueueCreateInfo& CreateInfo_);
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
            * @brief 尝试入队邮件。
            * @return true 表示写入成功；false 表示 record 或 payload 预分配空间不足。
            */
            bool TryEnqueue(const Mail& Mail_);

            /*
            * @brief 直接把 header + bytes 写入队列。
            * @details 用于避免调用方先构造临时 Mail payload 再发送。
            */
            void EnqueuePayload(
                IN const MailHeader& Header_,
                IN const void* pPayload_,
                IN size_t nPayloadSize_);

            /*
            * @brief 尝试直接把 header + bytes 写入队列。
            */
            bool TryEnqueuePayload(
                IN const MailHeader& Header_,
                IN const void* pPayload_,
                IN size_t nPayloadSize_);

            /*
            * @brief 取出当前全部邮件
            * @return 邮件列表
            * @details
            *   取出后 pending 队列清空，返回邮件的 Payload 仍引用队列预分配池。
            *   调用方处理完后必须调用 ReleaseMailPayload 归还空间。
            */
            std::vector<Mail> Drain();

            /*
            * @brief 清空队列
            * @details 清空时会释放仍滞留在队列中的 Payload。
            */
            void Clear();

            /*
            * @brief 获取当前 pending 邮件数量。
            */
            size_t GetPendingCount() const;

            /*
            * @brief 获取当前可用 record 数量。
            */
            size_t GetFreeRecordCount() const;

            /*
            * @brief 获取当前 payload pool 剩余总字节数。
            */
            size_t GetFreePayloadBytes() const;

        public:
            void ReleaseMailPayloadLease(
                IN size_t nLeaseIndex_,
                IN uint32_t nLeaseGeneration_) noexcept override;

        private:
            void InitializeStorage(IN const CMailQueueCreateInfo& CreateInfo_);
            bool TryEnqueuePayloadNoLock(
                IN const MailHeader& Header_,
                IN const void* pPayload_,
                IN size_t nPayloadSize_);
            std::vector<Mail> DrainNoLock();
            std::shared_ptr<IMailPayloadLease> TryGetLeaseOwnerNoLock() noexcept;
            bool TryAllocatePayloadBlockNoLock(IN size_t nSize_, OUT CFreeBlock& Block_);
            void ReturnPayloadBlockNoLock(IN CFreeBlock Block_);
            void ReleaseRecordNoLock(IN size_t nRecordIndex_) noexcept;
            static size_t AlignPayloadSize(IN size_t nSize_) noexcept;

        private:
            mutable std::mutex m_Mutex;
            CMailQueueCreateInfo m_CreateInfo;
            std::vector<CRecord> m_Records;
            std::vector<uint32_t> m_FreeRecordIndices;
            std::vector<uint32_t> m_PendingRecordIndices;
            std::vector<uint8_t> m_PayloadStorage;
            std::vector<CFreeBlock> m_FreePayloadBlocks;
        };
    }
}
