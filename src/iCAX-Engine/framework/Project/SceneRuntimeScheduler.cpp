#include "pch.h"
#include "SceneRuntimeScheduler.h"

namespace
{
    std::chrono::milliseconds NormalizeFrameInterval(IN uint32_t nFrameIntervalMilliseconds_)
    {
        return std::chrono::milliseconds(nFrameIntervalMilliseconds_ == 0 ? 1 : nFrameIntervalMilliseconds_);
    }
}

iCAX::Project::CSceneRuntimeScheduler::CSceneRuntimeScheduler(IN uint32_t nFrameIntervalMilliseconds_)
{
    Reset(nFrameIntervalMilliseconds_);
}

void iCAX::Project::CSceneRuntimeScheduler::Reset(IN uint32_t nFrameIntervalMilliseconds_)
{
    m_FrameInterval = NormalizeFrameInterval(nFrameIntervalMilliseconds_);
    m_LastTimePoint = Clock::now();
    m_NextFrameTime = m_LastTimePoint;
    m_DeltaTime = 0.0;
    m_TotalTime = 0.0;
}

iCAX::Project::CProjectFrameTime iCAX::Project::CSceneRuntimeScheduler::Tick()
{
    const auto _Now = Clock::now();
    m_DeltaTime = std::chrono::duration<double>(_Now - m_LastTimePoint).count();
    m_TotalTime += m_DeltaTime;
    m_LastTimePoint = _Now;
    return { m_DeltaTime, m_TotalTime };
}

void iCAX::Project::CSceneRuntimeScheduler::AdvanceFrameDeadline()
{
    m_NextFrameTime += m_FrameInterval;
}

void iCAX::Project::CSceneRuntimeScheduler::DropBacklogIfNeeded()
{
    const auto _Now = Clock::now();
    if (m_NextFrameTime < _Now - m_FrameInterval)
    {
        m_NextFrameTime = _Now;
    }
}

std::chrono::steady_clock::time_point iCAX::Project::CSceneRuntimeScheduler::GetNextFrameTime() const
{
    return m_NextFrameTime;
}

std::chrono::milliseconds iCAX::Project::CSceneRuntimeScheduler::GetFrameInterval() const
{
    return m_FrameInterval;
}

double iCAX::Project::CSceneRuntimeScheduler::GetDeltaTime() const
{
    return m_DeltaTime;
}

double iCAX::Project::CSceneRuntimeScheduler::GetTotalTime() const
{
    return m_TotalTime;
}
