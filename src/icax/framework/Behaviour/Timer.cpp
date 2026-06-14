#include "pch.h"
#include "Timer.h"

//!< 构造函数
iCAX::Behaviour::CTimer::CTimer()
{
    m_LastTimePoint = Clock::now();
}

//!< 析构函数
iCAX::Behaviour::CTimer::~CTimer()
{
}

//!< 每帧调用
void iCAX::Behaviour::CTimer::Tick()
{
    auto now = Clock::now();
    m_DeltaTime = std::chrono::duration<double>(now - m_LastTimePoint).count();
    m_TotalTime += m_DeltaTime;
    m_LastTimePoint = now;
}

//!< 当前帧耗时（可缩放）
double iCAX::Behaviour::CTimer::GetDeltaTime() const
{
    return m_DeltaTime;
}

//!< 累计运行时间（可缩放）
double iCAX::Behaviour::CTimer::GetTime() const
{
    return m_TotalTime;
}
