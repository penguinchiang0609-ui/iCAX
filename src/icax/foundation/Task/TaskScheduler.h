#pragma once

#include "TaskDefines.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

namespace iCAX::Tasks
{
    class _TASK_EXP ITaskScheduler
    {
    public:
        virtual ~ITaskScheduler() = default;
        virtual void Schedule(std::function<void()> action_) = 0;
    };

    using TaskSchedulerPtr = std::shared_ptr<ITaskScheduler>;

    _TASK_EXP TaskSchedulerPtr InlineScheduler();
    _TASK_EXP TaskSchedulerPtr ThreadPoolScheduler();
    _TASK_EXP TaskSchedulerPtr BackgroundScheduler();
    _TASK_EXP TaskSchedulerPtr DefaultScheduler();
    _TASK_EXP TaskSchedulerPtr CurrentScheduler();
    _TASK_EXP TaskSchedulerPtr FromCurrentSynchronizationContext();
    _TASK_EXP void SetCurrentSchedulerForThread(TaskSchedulerPtr scheduler_);
    _TASK_EXP void ScheduleTimer(std::chrono::milliseconds delay_, std::function<void()> action_);

    class _TASK_EXP CurrentTaskSchedulerScope final
    {
    public:
        explicit CurrentTaskSchedulerScope(TaskSchedulerPtr scheduler_);
        ~CurrentTaskSchedulerScope();

        CurrentTaskSchedulerScope(const CurrentTaskSchedulerScope&) = delete;
        CurrentTaskSchedulerScope& operator=(const CurrentTaskSchedulerScope&) = delete;

    private:
        TaskSchedulerPtr m_previous;
    };

    class _TASK_EXP ThreadPoolTaskScheduler final : public ITaskScheduler
    {
    public:
        explicit ThreadPoolTaskScheduler(std::size_t workerCount_ = 0);
        ~ThreadPoolTaskScheduler() override;

        ThreadPoolTaskScheduler(const ThreadPoolTaskScheduler&) = delete;
        ThreadPoolTaskScheduler& operator=(const ThreadPoolTaskScheduler&) = delete;

        void Schedule(std::function<void()> action_) override;
        void Shutdown();
        std::size_t WorkerCount() const;
        std::size_t PendingCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    class ManualTaskScheduler final : public ITaskScheduler
    {
    public:
        void Schedule(std::function<void()> action_) override
        {
            if (!action_)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_actions.push(std::move(action_));
        }

        bool RunOne()
        {
            std::function<void()> action;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_actions.empty())
                {
                    return false;
                }

                action = std::move(m_actions.front());
                m_actions.pop();
            }

            action();
            return true;
        }

        std::size_t RunAll()
        {
            std::size_t count = 0;
            while (RunOne())
            {
                ++count;
            }

            return count;
        }

        std::size_t PendingCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_actions.size();
        }

    private:
        mutable std::mutex m_mutex;
        std::queue<std::function<void()>> m_actions;
    };
}
