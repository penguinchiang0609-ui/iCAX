#include "pch.h"
#include "Coroutine.h"

//!< 构造函数
iCAX::Behaviour::CYieldWaitForSeconds::CYieldWaitForSeconds(IN const double& nSeconds_)
    : m_Remaining(nSeconds_)
{
}

//!< 析构函数
iCAX::Behaviour::CYieldWaitForSeconds::~CYieldWaitForSeconds()
{
}

//!< 移动到下一个动作
bool iCAX::Behaviour::CYieldWaitForSeconds::MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    m_Remaining -= nDeltaTime_;
    return m_Remaining > 0.0;
}

//!< 构造函数
iCAX::Behaviour::CYieldFrame::CYieldFrame()
{
}

//!< 析构函数
iCAX::Behaviour::CYieldFrame::~CYieldFrame()
{
}

//!< 移动到下一个动作
bool iCAX::Behaviour::CYieldFrame::MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    return false;
}

//!< 构造函数
iCAX::Behaviour::CYieldUntil::CYieldUntil(IN std::function<bool()> Condition_)
    : m_Condition(std::move(Condition_))
{
}

//!< 析构函数
iCAX::Behaviour::CYieldUntil::~CYieldUntil()
{
}

//!< 移动到下一个动作
bool iCAX::Behaviour::CYieldUntil::MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    return m_Condition || !m_Condition();;
}

//!< 构造函数
iCAX::Behaviour::CYieldCoroutine::CYieldCoroutine(IN std::function<std::shared_ptr<IYield>()> Nested_)
    : m_Coroutine(std::move(Nested_)), m_CurrentYield(m_Coroutine())
{
}

//!< 析构函数
iCAX::Behaviour::CYieldCoroutine::~CYieldCoroutine()
{
}

//!< 移动到下一个动作
bool iCAX::Behaviour::CYieldCoroutine::MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    if (!m_CurrentYield)
    {
        return false;
    }
    //!< 检查嵌套的协程是否有下一个动作
    if (!m_CurrentYield->MoveNext(nDeltaTime_, nTotalTime_))
    {
        m_CurrentYield = m_Coroutine();
        if (m_CurrentYield == nullptr)
        {
            return false;//!< 表示任务已经完成了，没有下一个任务
        }
        return true; //!< 工作还未完成，等待下一次查询
    }

    return true;
}
