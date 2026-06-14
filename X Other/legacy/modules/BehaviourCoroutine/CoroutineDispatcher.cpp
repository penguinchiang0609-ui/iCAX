#include "pch.h"
#include "CoroutineDispatcher.h"


//! 构造函数
iCAX::Behaviour::CCoroutineDispatcher::CCoroutineDispatcher()
    : m_NextID(0)
    , m_Coroutines()
{
}

//!< 析构函数
iCAX::Behaviour::CCoroutineDispatcher::~CCoroutineDispatcher()
{
}

//!< 启动协程
size_t iCAX::Behaviour::CCoroutineDispatcher::Start(IN CoroutineFunc Coroutine_)
{
    size_t id = m_NextID++;
    CoroutineState state;
    state.coroutine = std::move(Coroutine_);
    state.currentYield = state.coroutine();
    m_Coroutines.emplace(id, std::move(state));
    return id;
}

//!< 取消协程
void iCAX::Behaviour::CCoroutineDispatcher::Cancel(IN const size_t nID_)
{
    m_Coroutines.erase(nID_);
}

//!< tick
void iCAX::Behaviour::CCoroutineDispatcher::Tick(IN const double& nDeltaTime_, IN const double& nTotalTime_)
{
    std::vector<size_t> toRemove;

    for (auto& [id, state] : m_Coroutines)
    {
        if (!state.currentYield)
        {
            toRemove.push_back(id);
            continue;
        }

        if (!state.currentYield->MoveNext(nDeltaTime_, nTotalTime_))
        {
            state.currentYield = state.coroutine();
            if (!state.currentYield)
            {
                toRemove.push_back(id);
            }
        }
        else
        {
            toRemove.push_back(id);
        }
    }

    for (size_t id : toRemove)
    {
        m_Coroutines.erase(id);
    }
}
