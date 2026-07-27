#include "pch.h"
#include "SDOQueue.h"

iCAX::Interaction::CSDOQueue::CSDOQueue()
    : CSDOQueue(CSDOQueueCreateInfo{})
{
}

iCAX::Interaction::CSDOQueue::CSDOQueue(IN const CSDOQueueCreateInfo& CreateInfo_)
    : m_CreateInfo(CreateInfo_)
{
    if (m_CreateInfo.nMaxFrameCount == 0)
    {
        throw std::invalid_argument("SDO queue max frame count cannot be zero");
    }
    if (m_CreateInfo.nMaxPayloadBytesPerFrame > m_CreateInfo.nMaxQueuedPayloadBytes
        && m_CreateInfo.nMaxQueuedPayloadBytes != 0)
    {
        throw std::invalid_argument("SDO queue max frame payload exceeds queued payload limit");
    }
}

void iCAX::Interaction::CSDOQueue::Enqueue(IN const CSDOFrame& Frame_)
{
    if (!TryEnqueue(Frame_))
    {
        throw std::runtime_error("SDO queue capacity is exhausted");
    }
}

void iCAX::Interaction::CSDOQueue::Enqueue(IN CSDOFrame&& Frame_)
{
    if (!TryEnqueue(std::move(Frame_)))
    {
        throw std::runtime_error("SDO queue capacity is exhausted");
    }
}

bool iCAX::Interaction::CSDOQueue::TryEnqueue(IN const CSDOFrame& Frame_)
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

bool iCAX::Interaction::CSDOQueue::TryEnqueue(IN CSDOFrame&& Frame_)
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

std::vector<iCAX::Interaction::CSDOFrame> iCAX::Interaction::CSDOQueue::Drain()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<CSDOFrame> _Frames;
    _Frames.reserve(m_Frames.size());
    while (!m_Frames.empty())
    {
        _Frames.push_back(std::move(m_Frames.front()));
        m_Frames.pop_front();
    }
    m_nQueuedPayloadBytes = 0;
    return _Frames;
}

void iCAX::Interaction::CSDOQueue::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Frames.clear();
    m_nQueuedPayloadBytes = 0;
}

size_t iCAX::Interaction::CSDOQueue::GetPendingCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Frames.size();
}

size_t iCAX::Interaction::CSDOQueue::GetFreeFrameCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CreateInfo.nMaxFrameCount - m_Frames.size();
}

size_t iCAX::Interaction::CSDOQueue::GetFreePayloadBytes() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CreateInfo.nMaxQueuedPayloadBytes - m_nQueuedPayloadBytes;
}

bool iCAX::Interaction::CSDOQueue::CanEnqueueNoLock(IN const CSDOFrame& Frame_) const noexcept
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
