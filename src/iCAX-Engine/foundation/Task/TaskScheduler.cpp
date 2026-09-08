#include "pch.h"
#include "TaskScheduler.h"


namespace
{
    thread_local iCAX::Tasks::TaskSchedulerPtr g_currentScheduler;

    std::size_t NormalizeWorkerCount(std::size_t workerCount_)
    {
        if (workerCount_ > 0)
        {
            return workerCount_;
        }

        const auto hardwareCount = std::thread::hardware_concurrency();
        return hardwareCount > 0 ? hardwareCount : 1;
    }

    class InlineTaskScheduler final : public iCAX::Tasks::ITaskScheduler
    {
    public:
        void Schedule(std::function<void()> action_) override
        {
            if (action_)
            {
                action_();
            }
        }
    };

    class TimerQueue final
    {
    public:
        TimerQueue()
            : m_worker([this] { WorkerLoop(); })
        {
        }

        ~TimerQueue()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }

            m_cv.notify_all();
            if (m_worker.joinable())
            {
                m_worker.join();
            }
        }

        void Schedule(std::chrono::milliseconds delay_, std::function<void()> action_)
        {
            if (!action_)
            {
                return;
            }

            auto due = std::chrono::steady_clock::now();
            if (delay_ > std::chrono::milliseconds::zero())
            {
                due += delay_;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_actions.emplace(due, std::move(action_));
            }

            m_cv.notify_one();
        }

    private:
        void WorkerLoop()
        {
            while (true)
            {
                std::vector<std::function<void()>> ready;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    while (!m_stopping && m_actions.empty())
                    {
                        m_cv.wait(lock);
                    }

                    if (m_stopping)
                    {
                        m_actions.clear();
                        return;
                    }

                    while (!m_stopping && !m_actions.empty())
                    {
                        const auto nextDue = m_actions.begin()->first;
                        if (m_cv.wait_until(lock, nextDue, [this, nextDue]
                        {
                            return m_stopping || m_actions.empty() || m_actions.begin()->first < nextDue;
                        }))
                        {
                            break;
                        }

                        const auto now = std::chrono::steady_clock::now();
                        auto end = m_actions.upper_bound(now);
                        for (auto it = m_actions.begin(); it != end; )
                        {
                            ready.push_back(std::move(it->second));
                            it = m_actions.erase(it);
                        }

                        break;
                    }
                }

                for (auto& action : ready)
                {
                    try
                    {
                        action();
                    }
                    catch (...)
                    {
                    }
                }
            }
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> m_actions;
        std::thread m_worker;
        bool m_stopping = false;
    };

    TimerQueue& SharedTimerQueue()
    {
        static TimerQueue queue;
        return queue;
    }
}

struct iCAX::Tasks::ThreadPoolTaskScheduler::Impl final
{
    // Workers own the queue state, not the scheduler object. The last scheduler
    // reference can legitimately be released by a completed task on a worker.
    struct State final
    {
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::queue<std::function<void()>> actions;
        bool stopping = false;
    };

    inline static thread_local const State* currentWorkerState = nullptr;

    explicit Impl(std::size_t workerCount_)
        : workerCount(NormalizeWorkerCount(workerCount_))
    {
        workers.reserve(workerCount);
        try
        {
            for (std::size_t index = 0; index < workerCount; ++index)
                workers.emplace_back([queue = state] { WorkerLoop(std::move(queue)); });
        }
        catch (...)
        {
            // A partially constructed vector of joinable threads would terminate
            // the process if thread creation failed without first draining it.
            Shutdown();
            throw;
        }
    }

    ~Impl()
    {
        Shutdown();
        if (currentWorkerState == state.get())
        {
            // Never join peers from a worker: they may be waiting for this task.
            // Detached workers retain State and drain already accepted actions.
            for (auto& worker : workers) if (worker.joinable()) worker.detach();
        }
    }

    void Schedule(std::function<void()> action_)
    {
        if (!action_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopping)
            {
                throw std::runtime_error("ThreadPoolTaskScheduler has been shut down.");
            }

            state->actions.push(std::move(action_));
        }

        state->cv.notify_one();
    }

    std::size_t PendingCount() const
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        return state->actions.size();
    }

    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stopping = true;
        }

        state->cv.notify_all();

        // Worker calls request shutdown without waiting. External callers all
        // synchronize on joining, including concurrent/repeated Shutdown calls.
        if (currentWorkerState == state.get()) return;
        std::lock_guard<std::mutex> joinLock(joinMutex);
        for (auto& worker : workers)
        {
            if (worker.joinable()) worker.join();
        }
    }

    static void WorkerLoop(std::shared_ptr<State> state_)
    {
        currentWorkerState = state_.get();
        while (true)
        {
            std::function<void()> action;
            {
                std::unique_lock<std::mutex> lock(state_->mutex);
                state_->cv.wait(lock, [&] { return state_->stopping || !state_->actions.empty(); });

                if (state_->stopping && state_->actions.empty())
                {
                    currentWorkerState = nullptr;
                    return;
                }

                action = std::move(state_->actions.front());
                state_->actions.pop();
            }

            try
            {
                action();
            }
            catch (...)
            {
                // Scheduled task wrappers are expected to catch their own exceptions.
            }
        }
    }

    std::size_t workerCount;
    std::shared_ptr<State> state = std::make_shared<State>();
    std::mutex joinMutex;
    std::vector<std::thread> workers;
};

iCAX::Tasks::ThreadPoolTaskScheduler::ThreadPoolTaskScheduler(std::size_t workerCount_)
    : m_impl(std::make_unique<Impl>(workerCount_))
{
}

iCAX::Tasks::ThreadPoolTaskScheduler::~ThreadPoolTaskScheduler() = default;

void iCAX::Tasks::ThreadPoolTaskScheduler::Schedule(std::function<void()> action_)
{
    m_impl->Schedule(std::move(action_));
}

void iCAX::Tasks::ThreadPoolTaskScheduler::Shutdown()
{
    m_impl->Shutdown();
}

std::size_t iCAX::Tasks::ThreadPoolTaskScheduler::WorkerCount() const
{
    return m_impl->workerCount;
}

std::size_t iCAX::Tasks::ThreadPoolTaskScheduler::PendingCount() const
{
    return m_impl->PendingCount();
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::InlineScheduler()
{
    static TaskSchedulerPtr scheduler = std::make_shared<InlineTaskScheduler>();
    return scheduler;
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::ThreadPoolScheduler()
{
    static TaskSchedulerPtr scheduler = std::make_shared<ThreadPoolTaskScheduler>();
    return scheduler;
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::BackgroundScheduler()
{
    return ThreadPoolScheduler();
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::DefaultScheduler()
{
    return ThreadPoolScheduler();
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::CurrentScheduler()
{
    return g_currentScheduler ? g_currentScheduler : DefaultScheduler();
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Tasks::FromCurrentSynchronizationContext()
{
    return CurrentScheduler();
}

void iCAX::Tasks::SetCurrentSchedulerForThread(TaskSchedulerPtr scheduler_)
{
    g_currentScheduler = std::move(scheduler_);
}

iCAX::Tasks::CurrentTaskSchedulerScope::CurrentTaskSchedulerScope(TaskSchedulerPtr scheduler_)
    : m_previous(g_currentScheduler)
{
    g_currentScheduler = std::move(scheduler_);
}

iCAX::Tasks::CurrentTaskSchedulerScope::~CurrentTaskSchedulerScope()
{
    g_currentScheduler = std::move(m_previous);
}

void iCAX::Tasks::ScheduleTimer(std::chrono::milliseconds delay_, std::function<void()> action_)
{
    SharedTimerQueue().Schedule(delay_, std::move(action_));
}
