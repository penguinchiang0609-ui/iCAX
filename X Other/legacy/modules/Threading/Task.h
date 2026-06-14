#pragma once
#include "TaskScheduler.h"
#include <mutex>
#include <condition_variable>
#include <exception>
#include <atomic>
#include <vector>
#include <memory>
#include <type_traits>
#include <functional>
#include "Threading.h"
#include "TaskCompletionSource.h"
#include "TaskStatus.h"

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 任务
        * @remark 为简化代码，未提供CTask<void>特例的实现，如有void需要，直接使用CTask<int>代而用之即可
        */
        template<typename T>
        class CTask final 
        {
        public:
            /*
            * @brief 构造函数
            */
            CTask() : m_nStatus(kTaskPending), m_State(std::make_shared<InternalState>()) {}

            /*
            * @brief 析构函数
            */
            ~CTask() = default;

        public:
            /*
            * @brief 是否完成
            * @return bool
            */
            bool IsCompleted() const 
            { 
                return m_nStatus == TaskStatus::kTaskCompleted;
            }

            /*
            * @brief 是否失败
            * @return bool
            */
            bool IsFaulted() const 
            { 
                return m_nStatus == TaskStatus::kTaskFaulted;
            }

            /*
            * @brief 是否取消
            * @return bool
            */
            bool IsCanceled() const 
            { 
                return m_nStatus == TaskStatus::kTaskCanceled;
            }

            /*
            * @brief 获取当前状态
            */
            TaskStatus GetStatus() const 
            { 
                return m_nStatus;
            }

            /*
            * @brief 获取结果
            */
            T& GetResult() const 
            {
                if (IsFaulted())
                    std::rethrow_exception(m_State->m_Exception);
                if (IsCanceled())
                    throw std::runtime_error("Task canceled");

                return m_State->m_Result;
            }

            /*
            * @brief 等待
            */
            void Wait() const
            {
                std::unique_lock<std::mutex> lock(m_State->m_Mutex);
                m_State->m_Condition.wait(lock, [&] { return m_nStatus != TaskStatus::kTaskPending; });
            }

            /*
            * @brief 设置值
            * @param [in] result
            */
            void SetResult(IN T result) 
            {
                {
                    std::lock_guard<std::mutex> lock(m_State->m_Mutex);
                    if (m_nStatus != TaskStatus::kTaskPending) return;
                    m_State->m_Result = std::move(result);
                    m_nStatus = TaskStatus::kTaskCompleted;
                }
                Notify();
            }

            /*
            * @brief 设置异常
            */
            void SetException(std::exception_ptr ex)
            {
                {
                    std::lock_guard<std::mutex> lock(m_State->m_Mutex);
                    if (m_nStatus != TaskStatus::kTaskPending) return;
                    m_State->m_Exception = ex;
                    m_nStatus = TaskStatus::kTaskFaulted;
                }
                Notify();
            }

            /*
            * @brief 设置取消
            */
            void SetCanceled()
            {
                {
                    std::lock_guard<std::mutex> lock(m_State->m_Mutex);
                    if (m_nStatus != TaskStatus::kTaskPending) return;
                    m_nStatus = TaskStatus::kTaskCanceled;
                }
                Notify();
            }

            /*
            * @brief 继续
            * @param [in] cont
            */
            template<typename Func>
            auto ContinueWith(IN Func&& cont) -> CTask<decltype(cont(*this))>
            {
                using R = decltype(cont(*this));
                CTaskCompletionSource<R> tcs;
                OnCompleted([tcs, cont = std::forward<Func>(cont), self = *this]() mutable
                    {
                        try 
                        {
                            //!< 对于取消、异常的处理交由cont处理
                            tcs.SetResult(cont(self));
                            //if (self.IsCanceled()) 
                            //{
                            //    tcs.SetCanceled();
                            //}
                            //else if (self.IsFaulted()) 
                            //{
                            //    tcs.SetException(self.m_State->m_Exception);
                            //}
                            //else 
                            //{
                            //    tcs.SetResult(cont(self));
                            //}
                        }
                        catch (...) 
                        {
                            tcs.SetException(std::current_exception());
                        }
                    });
                return tcs.GetTask();
            }

        private:
            struct InternalState 
            {
                mutable std::mutex m_Mutex;
                std::condition_variable m_Condition;
                std::vector<std::function<void()>> m_Continuations;
                T m_Result;
                std::exception_ptr m_Exception;
            };

            /*
            * @brief 当完成
            * @param [in] ContFunc_
            */
            void OnCompleted(IN std::function<void()> ContFunc_) const
            {
                bool runNow = false;
                {
                    std::lock_guard<std::mutex> lock(m_State->m_Mutex);
                    if (m_nStatus == TaskStatus::kTaskPending) 
                    {
                        m_State->m_Continuations.push_back(std::move(ContFunc_));
                    }
                    else 
                    {
                        runNow = true;
                    }
                }
                if (runNow)
                {
                    CTaskScheduler::Instance().Schedule(std::move(ContFunc_));
                }
            }

            /*
            * @brief 通知
            */
            void Notify() const 
            {
                std::vector<std::function<void()>> conts;
                {
                    std::lock_guard<std::mutex> lock(m_State->m_Mutex);
                    conts.swap(m_State->m_Continuations);
                }
                m_State->m_Condition.notify_all();
                for (auto& cont : conts) 
                {
                    CTaskScheduler::Instance().Schedule(std::move(cont));
                }
            }

            TaskStatus m_nStatus;
            std::shared_ptr<InternalState> m_State;
        };
    }
}