#pragma once
#include <memory>
#include "Threading.h"
#include <functional>
#include <boost/asio/thread_pool.hpp>

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 任务调度者
        */
        class _THREADING_EXP CTaskScheduler
        {
        private:
            /*
            * @brief 构造函数
            */
            CTaskScheduler();

        public:
            /*
            * @brief 析构函数
            */
            ~CTaskScheduler();

        public:
            /*
            * @brief 调度
            * @param [in] Func_
            */
            void Schedule(IN std::function<void()> Func_);

            /*
            * @brief 阻塞
            * @remark 等待线程池中所有任务都完成
            */
            void Join();

        public:
            /*
            * @brief 获取单例
            * @return CTaskScheduler&
            */
            static CTaskScheduler& Instance();

        private:
            boost::asio::thread_pool m_Pool;
        };
    }
}