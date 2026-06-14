#include "pch.h"
#include "RepeatDispatcher.h"

//! 构造函数
iCAX::Behaviour::CRepeatDispatcher::CRepeatDispatcher()
{
}

//! 析构函数
iCAX::Behaviour::CRepeatDispatcher::~CRepeatDispatcher()
{
}

//!< 重复定时器
size_t iCAX::Behaviour::CRepeatDispatcher::Repeat(IN std::function<bool()> Callback_, IN const double& nDelay_, IN const double& nInterval_)
{
    size_t id = m_NextID++;
    m_Tasks[id] = Task{ nDelay_, nInterval_, true, std::move(Callback_) };
    return id;
}

//!< 取消任务
void iCAX::Behaviour::CRepeatDispatcher::Cancel(IN const size_t& nID_)
{
    m_Tasks.erase(nID_);
}

//!< 每帧执行
void iCAX::Behaviour::CRepeatDispatcher::Tick(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    std::vector<size_t> _vecToRemove;

    for (auto& [_nID, _Task] : m_Tasks)
    {
        _Task.nTimeRemaining -= nDeltaTime_;
        if (_Task.nTimeRemaining <= 0.0)
        {
            bool _bKeep = _Task.Callback ? _Task.Callback() : false;
            if (_bKeep)
                _Task.nTimeRemaining = _Task.nInterval;
            else
                _vecToRemove.push_back(_nID);
        }
    }

    for (auto _nID : _vecToRemove)
        m_Tasks.erase(_nID);
}
