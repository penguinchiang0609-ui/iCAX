#pragma once
#include "Core.h"
#include "TaskService.h"

namespace iCAX
{
    namespace Core
    {
        /*
        * @brief 执行结果
        */
        template<typename T>
        struct TaskResult 
        {
            bool success;
            T value;
            std::exception_ptr ex;
        };

        /*
        * @brief 任务
        */
        template<typename T>
        class Task 
        {
        public:
            Task(std::shared_future<T> fut, TaskService* svc)
                : m_future(fut)
                , m_service(svc) 
            {
            }

            //! 禁用赋值，保留拷贝
            Task(const Task&) = default;//! 拷贝构造：允许，共享任务状态
            Task& operator=(const Task&) = delete;//! 赋值：禁止，避免覆盖原任务
            Task(Task&&) noexcept = default;//! 移动构造：允许
            Task& operator=(Task&&) noexcept = default;//! 移动赋值：允许

            template<typename F>
            auto ContinueWith(F&& func)
            {
                using RetType = decltype(func(std::declval<TaskResult<T>>()));
                std::shared_ptr<std::promise<RetType>> prom = std::make_shared<std::promise<RetType>>();
                std::shared_future<RetType> fut = prom->get_future().share();

                m_service->m_pool->submit([prev = m_future, prom, func = std::forward<F>(func)]() mutable
                {
                    TaskResult<T> result{};
                    try
                    {
                        result.value = prev.get();
                        result.success = true;
                    }
                    catch (...) 
                    {
                        result.success = false;
                        result.ex = std::current_exception();
                    }

                    try
                    {
                        if constexpr (std::is_void_v<RetType>)
                        {
                            func(result);
                            prom->set_value();
                        }
                        else
                        {
                            prom->set_value(func(result));
                        }
                    }
                    catch (...)
                    {
                        prom->set_exception(std::current_exception());
                    }
                });

                return Task<RetType>(fut, m_service);
            }

            std::shared_future<T>& GetFuture() { return m_future; }

        private:
            std::shared_future<T> m_future;
            TaskService* m_service;
        };
    }
}
