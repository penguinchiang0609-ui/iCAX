#pragma once

#include "SDOFrame.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace iCAX::Interaction
{
    struct _SDO_EXP CSDOQueueCreateInfo final
    {
        size_t nMaxFrameCount = 4096;
        size_t nMaxQueuedPayloadBytes = 4ull * 1024ull * 1024ull;
        size_t nMaxPayloadBytesPerFrame = 4ull * 1024ull * 1024ull;
    };

    /*
    * @brief SDO 通道单方向的有界帧队列。
    * @details CSDOFrame 自己持有 Payload，Drain 后不需要调用额外释放函数。
    */
    class _SDO_EXP CSDOQueue final
    {
    public:
        CSDOQueue();
        explicit CSDOQueue(IN const CSDOQueueCreateInfo& CreateInfo_);
        ~CSDOQueue() = default;

        CSDOQueue(IN const CSDOQueue&) = delete;
        CSDOQueue& operator=(IN const CSDOQueue&) = delete;
        CSDOQueue(CSDOQueue&&) = delete;
        CSDOQueue& operator=(CSDOQueue&&) = delete;

        void Enqueue(IN const CSDOFrame& Frame_);
        void Enqueue(IN CSDOFrame&& Frame_);
        bool TryEnqueue(IN const CSDOFrame& Frame_);
        bool TryEnqueue(IN CSDOFrame&& Frame_);
        std::vector<CSDOFrame> Drain();
        void Clear();

        size_t GetPendingCount() const;
        size_t GetFreeFrameCount() const;
        size_t GetFreePayloadBytes() const;

    private:
        bool CanEnqueueNoLock(IN const CSDOFrame& Frame_) const noexcept;

    private:
        mutable std::mutex m_Mutex;
        CSDOQueueCreateInfo m_CreateInfo;
        std::deque<CSDOFrame> m_Frames;
        size_t m_nQueuedPayloadBytes = 0;
    };
}
