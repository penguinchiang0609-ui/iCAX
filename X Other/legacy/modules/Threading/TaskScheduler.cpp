#include "pch.h"
#include "TaskScheduler.h"
#include <boost/asio/post.hpp>

//!< 构造函数
iCAX::Threading::CTaskScheduler::CTaskScheduler()
    : m_Pool(std::thread::hardware_concurrency()) 
{
}

//!< 析构函数
iCAX::Threading::CTaskScheduler::~CTaskScheduler()
{
    m_Pool.join();
}


//!< 调度
void iCAX::Threading::CTaskScheduler::Schedule(IN std::function<void()> Func_)
{
    boost::asio::post(m_Pool, std::move(Func_));
}

//!< 阻塞
void iCAX::Threading::CTaskScheduler::Join()
{
}

iCAX::Threading::CTaskScheduler& iCAX::Threading::CTaskScheduler::Instance()
{
    static CTaskScheduler instance;
    return instance;
}
