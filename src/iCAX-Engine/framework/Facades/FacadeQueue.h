#pragma once

#include "FacadeFrame.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace iCAX::Interaction
{
    struct _FACADES_EXP CFacadeQueueCreateInfo final
    {
        size_t nMaxFrameCount = 4096;
        size_t nMaxQueuedPayloadBytes = 4ull * 1024ull * 1024ull;
        size_t nMaxPayloadBytesPerFrame = 4ull * 1024ull * 1024ull;
    };

    /*
    * @brief Facade 通道单方向的有界帧队列。
    * @details CFacadeFrame 自己持有 Payload，Drain 后不需要调用额外释放函数。
    */
    class _FACADES_EXP CFacadeQueue final
    {
    public:
        CFacadeQueue();
        explicit CFacadeQueue(IN const CFacadeQueueCreateInfo& CreateInfo_);
        ~CFacadeQueue() = default;

        CFacadeQueue(IN const CFacadeQueue&) = delete;
        CFacadeQueue& operator=(IN const CFacadeQueue&) = delete;
        CFacadeQueue(CFacadeQueue&&) = delete;
        CFacadeQueue& operator=(CFacadeQueue&&) = delete;

        void Enqueue(IN const CFacadeFrame& Frame_);
        void Enqueue(IN CFacadeFrame&& Frame_);
        bool TryEnqueue(IN const CFacadeFrame& Frame_);
        bool TryEnqueue(IN CFacadeFrame&& Frame_);
        std::vector<CFacadeFrame> Drain();
        void Clear();

        size_t GetPendingCount() const;
        size_t GetFreeFrameCount() const;
        size_t GetFreePayloadBytes() const;

    private:
        bool CanEnqueueNoLock(IN const CFacadeFrame& Frame_) const noexcept;

    private:
        mutable std::mutex m_Mutex;
        CFacadeQueueCreateInfo m_CreateInfo;
        std::deque<CFacadeFrame> m_Frames;
        size_t m_nQueuedPayloadBytes = 0;
    };
}
