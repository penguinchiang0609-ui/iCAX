#include "pch.h"
#include "FacadeQueue.h"

iCAX::Interaction::CFacadeQueue::CFacadeQueue()
    : CFacadeQueue(CFacadeQueueCreateInfo{})
{
}

iCAX::Interaction::CFacadeQueue::CFacadeQueue(IN const CFacadeQueueCreateInfo& CreateInfo_)
    : m_CreateInfo(CreateInfo_)
{
    if (m_CreateInfo.nMaxFrameCount == 0)
    {
        throw std::invalid_argument("Facade queue max frame count cannot be zero");
    }
    if (m_CreateInfo.nMaxPayloadBytesPerFrame > m_CreateInfo.nMaxQueuedPayloadBytes
        && m_CreateInfo.nMaxQueuedPayloadBytes != 0)
    {
        throw std::invalid_argument("Facade queue max frame payload exceeds queued payload limit");
    }
}

void iCAX::Interaction::CFacadeQueue::Enqueue(IN const CFacadeFrame& Frame_)
{
    if (!TryEnqueue(Frame_))
    {
        throw std::runtime_error("Facade queue capacity is exhausted");
    }
}

void iCAX::Interaction::CFacadeQueue::Enqueue(IN CFacadeFrame&& Frame_)
{
    if (!TryEnqueue(std::move(Frame_)))
    {
        throw std::runtime_error("Facade queue capacity is exhausted");
    }
}

bool iCAX::Interaction::CFacadeQueue::TryEnqueue(IN const CFacadeFrame& Frame_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!CanEnqueueNoLock(Frame_))
    {
        return false;
    }
    m_nQueuedPayloadBytes += Frame_.Payload.size();
    m_Frames.push_back(Frame_);
    return true;
}

bool iCAX::Interaction::CFacadeQueue::TryEnqueue(IN CFacadeFrame&& Frame_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!CanEnqueueNoLock(Frame_))
    {
        return false;
    }
    m_nQueuedPayloadBytes += Frame_.Payload.size();
    m_Frames.push_back(std::move(Frame_));
    return true;
}

std::vector<iCAX::Interaction::CFacadeFrame> iCAX::Interaction::CFacadeQueue::Drain()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<CFacadeFrame> _Frames;
    _Frames.reserve(m_Frames.size());
    while (!m_Frames.empty())
    {
        _Frames.push_back(std::move(m_Frames.front()));
        m_Frames.pop_front();
    }
    m_nQueuedPayloadBytes = 0;
    return _Frames;
}

void iCAX::Interaction::CFacadeQueue::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Frames.clear();
    m_nQueuedPayloadBytes = 0;
}

size_t iCAX::Interaction::CFacadeQueue::GetPendingCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Frames.size();
}

size_t iCAX::Interaction::CFacadeQueue::GetFreeFrameCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CreateInfo.nMaxFrameCount - m_Frames.size();
}

size_t iCAX::Interaction::CFacadeQueue::GetFreePayloadBytes() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CreateInfo.nMaxQueuedPayloadBytes - m_nQueuedPayloadBytes;
}

bool iCAX::Interaction::CFacadeQueue::CanEnqueueNoLock(IN const CFacadeFrame& Frame_) const noexcept
{
    if (m_Frames.size() >= m_CreateInfo.nMaxFrameCount)
    {
        return false;
    }
    if (Frame_.Payload.size() > m_CreateInfo.nMaxPayloadBytesPerFrame)
    {
        return false;
    }
    return Frame_.Payload.size() <= m_CreateInfo.nMaxQueuedPayloadBytes - m_nQueuedPayloadBytes;
}
