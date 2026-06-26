#pragma once

#include "CancellationToken.h"
#include "TaskCreationOptions.h"
#include "TaskContinuationOptions.h"
#include "TaskScheduler.h"
#include "TaskStatus.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace iCAX::Tasks
{
    template<typename T>
    class Task;

    template<typename T>
    class TaskCompletionSource;

    namespace detail
    {
        inline std::exception_ptr NormalizeException(std::exception_ptr exception_)
        {
            if (exception_)
            {
                return exception_;
            }

            return std::make_exception_ptr(std::runtime_error("Task faulted without an exception."));
        }

        inline TaskSchedulerPtr RequireScheduler(TaskSchedulerPtr scheduler_)
        {
            if (!scheduler_)
            {
                throw std::invalid_argument("scheduler cannot be null.");
            }

            return scheduler_;
        }

        inline void ThrowAlreadyCompleted()
        {
            throw std::logic_error("Task is already completed.");
        }

        inline std::size_t NextTaskId()
        {
            static std::atomic_size_t nextId = 1;
            return nextId.fetch_add(1);
        }

        inline void RunContinuation(
            TaskCreationOptions creationOptions_,
            std::function<void()> continuation_)
        {
            if (!continuation_)
            {
                return;
            }

            if (HasFlag(creationOptions_, TaskCreationOptions::RunContinuationsAsynchronously))
            {
                DefaultScheduler()->Schedule(std::move(continuation_));
                return;
            }

            continuation_();
        }

        inline void RunContinuations(
            TaskCreationOptions creationOptions_,
            std::vector<std::function<void()>>& continuations_)
        {
            for (auto& continuation : continuations_)
            {
                RunContinuation(creationOptions_, std::move(continuation));
            }
        }

        inline thread_local std::size_t CurrentTaskIdValue = 0;

        class CurrentTaskIdScope final
        {
        public:
            explicit CurrentTaskIdScope(std::size_t id_)
                : m_previous(CurrentTaskIdValue)
            {
                CurrentTaskIdValue = id_;
            }

            ~CurrentTaskIdScope()
            {
                CurrentTaskIdValue = m_previous;
            }

        private:
            std::size_t m_previous = 0;
        };

        template<typename Work, bool TakesToken>
        struct RunReturnType;

        template<typename Work>
        struct RunReturnType<Work, true>
        {
            using Type = std::invoke_result_t<Work, CancellationToken>;
        };

        template<typename Work>
        struct RunReturnType<Work, false>
        {
            using Type = std::invoke_result_t<Work>;
        };

        template<typename Work>
        using RunReturnT = typename RunReturnType<Work, std::is_invocable_v<Work, CancellationToken>>::Type;

        template<typename Work>
        decltype(auto) InvokeWork(Work& work_, const CancellationToken& token_)
        {
            if constexpr (std::is_invocable_v<Work, CancellationToken>)
            {
                return std::invoke(work_, token_);
            }
            else
            {
                return std::invoke(work_);
            }
        }

        template<typename T>
        class TaskState final
        {
        public:
            TaskState(
                std::shared_ptr<void> asyncState_ = nullptr,
                TaskCreationOptions creationOptions_ = TaskCreationOptions::None)
                : m_id(NextTaskId())
                , m_asyncState(std::move(asyncState_))
                , m_creationOptions(creationOptions_)
            {
            }

            std::size_t Id() const noexcept
            {
                return m_id;
            }

            std::shared_ptr<void> AsyncState() const noexcept
            {
                return m_asyncState;
            }

            TaskCreationOptions CreationOptions() const noexcept
            {
                return m_creationOptions;
            }

            TaskStatus Status() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_status;
            }

            bool IsCompleted() const
            {
                return IsTerminalStatus(Status());
            }

            bool IsFaulted() const
            {
                return Status() == TaskStatus::Faulted;
            }

            bool IsCanceled() const
            {
                return Status() == TaskStatus::Canceled;
            }

            bool IsCompletedSuccessfully() const
            {
                return Status() == TaskStatus::RanToCompletion;
            }

            void MarkRunning()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_status == TaskStatus::Created)
                {
                    m_status = TaskStatus::Running;
                }
            }

            bool TrySetResult(T value_)
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_value = std::move(value_);
                    m_status = TaskStatus::RanToCompletion;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            bool TrySetException(std::exception_ptr exception_)
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_exception = NormalizeException(exception_);
                    m_status = TaskStatus::Faulted;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            bool TrySetCanceled()
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_status = TaskStatus::Canceled;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            void Wait() const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return IsTerminalStatus(m_status); });
            }

            template<typename Rep, typename Period>
            bool WaitFor(const std::chrono::duration<Rep, Period>& timeout_) const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                return m_cv.wait_for(lock, timeout_, [this] { return IsTerminalStatus(m_status); });
            }

            template<typename Clock, typename Duration>
            bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline_) const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                return m_cv.wait_until(lock, deadline_, [this] { return IsTerminalStatus(m_status); });
            }

            const T& ResultRef() const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return IsTerminalStatus(m_status); });

                if (m_status == TaskStatus::Canceled)
                {
                    throw TaskCanceledException();
                }

                if (m_status == TaskStatus::Faulted)
                {
                    std::rethrow_exception(m_exception);
                }

                return *m_value;
            }

            T Result() const
            {
                return ResultRef();
            }

            T TakeResult()
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return IsTerminalStatus(m_status); });

                if (m_status == TaskStatus::Canceled)
                {
                    throw TaskCanceledException();
                }

                if (m_status == TaskStatus::Faulted)
                {
                    std::rethrow_exception(m_exception);
                }

                return std::move(*m_value);
            }

            std::exception_ptr Exception() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_exception;
            }

            void AddContinuation(std::function<void()> continuation_)
            {
                if (!continuation_)
                {
                    return;
                }

                bool runNow = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        runNow = true;
                    }
                    else
                    {
                        m_continuations.push_back(std::move(continuation_));
                    }
                }

                if (runNow)
                {
                    RunContinuation(m_creationOptions, std::move(continuation_));
                }
            }

        private:
            mutable std::mutex m_mutex;
            mutable std::condition_variable m_cv;
            std::size_t m_id = 0;
            std::shared_ptr<void> m_asyncState;
            TaskCreationOptions m_creationOptions = TaskCreationOptions::None;
            TaskStatus m_status = TaskStatus::Created;
            std::optional<T> m_value;
            std::exception_ptr m_exception;
            std::vector<std::function<void()>> m_continuations;
        };

        template<>
        class TaskState<void> final
        {
        public:
            TaskState(
                std::shared_ptr<void> asyncState_ = nullptr,
                TaskCreationOptions creationOptions_ = TaskCreationOptions::None)
                : m_id(NextTaskId())
                , m_asyncState(std::move(asyncState_))
                , m_creationOptions(creationOptions_)
            {
            }

            std::size_t Id() const noexcept
            {
                return m_id;
            }

            std::shared_ptr<void> AsyncState() const noexcept
            {
                return m_asyncState;
            }

            TaskCreationOptions CreationOptions() const noexcept
            {
                return m_creationOptions;
            }

            TaskStatus Status() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_status;
            }

            bool IsCompleted() const
            {
                return IsTerminalStatus(Status());
            }

            bool IsFaulted() const
            {
                return Status() == TaskStatus::Faulted;
            }

            bool IsCanceled() const
            {
                return Status() == TaskStatus::Canceled;
            }

            bool IsCompletedSuccessfully() const
            {
                return Status() == TaskStatus::RanToCompletion;
            }

            void MarkRunning()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_status == TaskStatus::Created)
                {
                    m_status = TaskStatus::Running;
                }
            }

            bool TrySetResult()
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_status = TaskStatus::RanToCompletion;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            bool TrySetException(std::exception_ptr exception_)
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_exception = NormalizeException(exception_);
                    m_status = TaskStatus::Faulted;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            bool TrySetCanceled()
            {
                std::vector<std::function<void()>> continuations;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        return false;
                    }

                    m_status = TaskStatus::Canceled;
                    continuations.swap(m_continuations);
                }

                m_cv.notify_all();
                RunContinuations(m_creationOptions, continuations);
                return true;
            }

            void Wait() const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return IsTerminalStatus(m_status); });
            }

            template<typename Rep, typename Period>
            bool WaitFor(const std::chrono::duration<Rep, Period>& timeout_) const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                return m_cv.wait_for(lock, timeout_, [this] { return IsTerminalStatus(m_status); });
            }

            template<typename Clock, typename Duration>
            bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline_) const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                return m_cv.wait_until(lock, deadline_, [this] { return IsTerminalStatus(m_status); });
            }

            void Result() const
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return IsTerminalStatus(m_status); });

                if (m_status == TaskStatus::Canceled)
                {
                    throw TaskCanceledException();
                }

                if (m_status == TaskStatus::Faulted)
                {
                    std::rethrow_exception(m_exception);
                }
            }

            std::exception_ptr Exception() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_exception;
            }

            void AddContinuation(std::function<void()> continuation_)
            {
                if (!continuation_)
                {
                    return;
                }

                bool runNow = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (IsTerminalStatus(m_status))
                    {
                        runNow = true;
                    }
                    else
                    {
                        m_continuations.push_back(std::move(continuation_));
                    }
                }

                if (runNow)
                {
                    RunContinuation(m_creationOptions, std::move(continuation_));
                }
            }

        private:
            mutable std::mutex m_mutex;
            mutable std::condition_variable m_cv;
            std::size_t m_id = 0;
            std::shared_ptr<void> m_asyncState;
            TaskCreationOptions m_creationOptions = TaskCreationOptions::None;
            TaskStatus m_status = TaskStatus::Created;
            std::exception_ptr m_exception;
            std::vector<std::function<void()>> m_continuations;
        };
    }

    template<typename T>
    class Task
    {
    public:
        using ValueType = T;

        Task() = default;

        bool Valid() const noexcept
        {
            return m_state != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return Valid();
        }

        TaskStatus Status() const
        {
            return State().Status();
        }

        std::size_t Id() const
        {
            return State().Id();
        }

        std::shared_ptr<void> AsyncState() const
        {
            return State().AsyncState();
        }

        TaskCreationOptions CreationOptions() const
        {
            return State().CreationOptions();
        }

        bool IsCompleted() const
        {
            return State().IsCompleted();
        }

        bool IsFaulted() const
        {
            return State().IsFaulted();
        }

        bool IsCanceled() const
        {
            return State().IsCanceled();
        }

        bool IsCompletedSuccessfully() const
        {
            return State().IsCompletedSuccessfully();
        }

        void Wait() const
        {
            State().Wait();
        }

        template<typename Rep, typename Period>
        bool WaitFor(const std::chrono::duration<Rep, Period>& timeout_) const
        {
            return State().WaitFor(timeout_);
        }

        template<typename Clock, typename Duration>
        bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline_) const
        {
            return State().WaitUntil(deadline_);
        }

        T Result() const
        {
            return State().Result();
        }

        const T& ResultRef() const
        {
            return State().ResultRef();
        }

        T TakeResult()
        {
            return State().TakeResult();
        }

        std::exception_ptr Exception() const
        {
            return State().Exception();
        }

        template<typename F>
        auto ContinueWith(F&& continuation_, TaskSchedulerPtr scheduler_ = InlineScheduler()) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>;

        template<typename F>
        auto ContinueWith(
            F&& continuation_,
            TaskContinuationOptions options_,
            TaskSchedulerPtr scheduler_ = InlineScheduler()) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>;

        template<typename F>
        auto ContinueWith(
            F&& continuation_,
            TaskContinuationOptions options_,
            TaskSchedulerPtr scheduler_,
            CancellationToken cancellationToken_) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>;

        static Task<T> FromResult(T value_);
        static Task<T> FromException(std::exception_ptr exception_);
        static Task<T> FromException(std::vector<std::exception_ptr> exceptions_);
        static Task<T> FromCanceled();
        static Task<T> FromCanceled(CancellationToken cancellationToken_);
        static std::size_t CurrentId() noexcept;

    private:
        explicit Task(std::shared_ptr<detail::TaskState<T>> state_)
            : m_state(std::move(state_))
        {
        }

        detail::TaskState<T>& State() const
        {
            if (!m_state)
            {
                throw std::logic_error("Task does not have state.");
            }

            return *m_state;
        }

    private:
        std::shared_ptr<detail::TaskState<T>> m_state;

        friend class TaskCompletionSource<T>;
        template<typename>
        friend class Task;
    };

    template<>
    class Task<void>
    {
    public:
        using ValueType = void;

        Task() = default;

        bool Valid() const noexcept
        {
            return m_state != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return Valid();
        }

        TaskStatus Status() const
        {
            return State().Status();
        }

        std::size_t Id() const
        {
            return State().Id();
        }

        std::shared_ptr<void> AsyncState() const
        {
            return State().AsyncState();
        }

        TaskCreationOptions CreationOptions() const
        {
            return State().CreationOptions();
        }

        bool IsCompleted() const
        {
            return State().IsCompleted();
        }

        bool IsFaulted() const
        {
            return State().IsFaulted();
        }

        bool IsCanceled() const
        {
            return State().IsCanceled();
        }

        bool IsCompletedSuccessfully() const
        {
            return State().IsCompletedSuccessfully();
        }

        void Wait() const
        {
            State().Wait();
        }

        template<typename Rep, typename Period>
        bool WaitFor(const std::chrono::duration<Rep, Period>& timeout_) const
        {
            return State().WaitFor(timeout_);
        }

        template<typename Clock, typename Duration>
        bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline_) const
        {
            return State().WaitUntil(deadline_);
        }

        void Result() const
        {
            State().Result();
        }

        std::exception_ptr Exception() const
        {
            return State().Exception();
        }

        template<typename F>
        auto ContinueWith(F&& continuation_, TaskSchedulerPtr scheduler_ = InlineScheduler()) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>;

        template<typename F>
        auto ContinueWith(
            F&& continuation_,
            TaskContinuationOptions options_,
            TaskSchedulerPtr scheduler_ = InlineScheduler()) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>;

        template<typename F>
        auto ContinueWith(
            F&& continuation_,
            TaskContinuationOptions options_,
            TaskSchedulerPtr scheduler_,
            CancellationToken cancellationToken_) const
            -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>;

        static Task<void> CompletedTask();
        static Task<void> FromException(std::exception_ptr exception_);
        static Task<void> FromException(std::vector<std::exception_ptr> exceptions_);
        static Task<void> FromCanceled();
        static Task<void> FromCanceled(CancellationToken cancellationToken_);
        static std::size_t CurrentId() noexcept;

    private:
        explicit Task(std::shared_ptr<detail::TaskState<void>> state_)
            : m_state(std::move(state_))
        {
        }

        detail::TaskState<void>& State() const
        {
            if (!m_state)
            {
                throw std::logic_error("Task does not have state.");
            }

            return *m_state;
        }

    private:
        std::shared_ptr<detail::TaskState<void>> m_state;

        friend class TaskCompletionSource<void>;
        template<typename>
        friend class Task;
    };

    template<typename T>
    class TaskCompletionSource
    {
    public:
        explicit TaskCompletionSource(
            std::shared_ptr<void> asyncState_ = nullptr,
            TaskCreationOptions creationOptions_ = TaskCreationOptions::None)
            : m_state(std::make_shared<detail::TaskState<T>>(
                std::move(asyncState_),
                creationOptions_))
        {
        }

        Task<T> GetTask() const
        {
            return Task<T>(m_state);
        }

        std::size_t Id() const
        {
            return m_state->Id();
        }

        void MarkRunning() const
        {
            m_state->MarkRunning();
        }

        bool TrySetResult(T value_) const
        {
            return m_state->TrySetResult(std::move(value_));
        }

        void SetResult(T value_) const
        {
            if (!TrySetResult(std::move(value_)))
            {
                detail::ThrowAlreadyCompleted();
            }
        }

        bool TrySetException(std::exception_ptr exception_) const
        {
            return m_state->TrySetException(exception_);
        }

        void SetException(std::exception_ptr exception_) const
        {
            if (!TrySetException(exception_))
            {
                detail::ThrowAlreadyCompleted();
            }
        }

        bool TrySetCanceled() const
        {
            return m_state->TrySetCanceled();
        }

        void SetCanceled() const
        {
            if (!TrySetCanceled())
            {
                detail::ThrowAlreadyCompleted();
            }
        }

    private:
        std::shared_ptr<detail::TaskState<T>> m_state;
    };

    template<>
    class TaskCompletionSource<void>
    {
    public:
        explicit TaskCompletionSource(
            std::shared_ptr<void> asyncState_ = nullptr,
            TaskCreationOptions creationOptions_ = TaskCreationOptions::None)
            : m_state(std::make_shared<detail::TaskState<void>>(
                std::move(asyncState_),
                creationOptions_))
        {
        }

        Task<void> GetTask() const
        {
            return Task<void>(m_state);
        }

        std::size_t Id() const
        {
            return m_state->Id();
        }

        void MarkRunning() const
        {
            m_state->MarkRunning();
        }

        bool TrySetResult() const
        {
            return m_state->TrySetResult();
        }

        void SetResult() const
        {
            if (!TrySetResult())
            {
                detail::ThrowAlreadyCompleted();
            }
        }

        bool TrySetException(std::exception_ptr exception_) const
        {
            return m_state->TrySetException(exception_);
        }

        void SetException(std::exception_ptr exception_) const
        {
            if (!TrySetException(exception_))
            {
                detail::ThrowAlreadyCompleted();
            }
        }

        bool TrySetCanceled() const
        {
            return m_state->TrySetCanceled();
        }

        void SetCanceled() const
        {
            if (!TrySetCanceled())
            {
                detail::ThrowAlreadyCompleted();
            }
        }

    private:
        std::shared_ptr<detail::TaskState<void>> m_state;
    };

    template<typename T>
    Task<T> Task<T>::FromResult(T value_)
    {
        TaskCompletionSource<T> source;
        source.SetResult(std::move(value_));
        return source.GetTask();
    }

    template<typename T>
    Task<T> Task<T>::FromException(std::exception_ptr exception_)
    {
        TaskCompletionSource<T> source;
        source.SetException(exception_);
        return source.GetTask();
    }

    template<typename T>
    Task<T> Task<T>::FromException(std::vector<std::exception_ptr> exceptions_)
    {
        return FromException(std::make_exception_ptr(TaskAggregateException(std::move(exceptions_))));
    }

    template<typename T>
    Task<T> Task<T>::FromCanceled()
    {
        TaskCompletionSource<T> source;
        source.SetCanceled();
        return source.GetTask();
    }

    template<typename T>
    Task<T> Task<T>::FromCanceled(CancellationToken)
    {
        return FromCanceled();
    }

    template<typename T>
    std::size_t Task<T>::CurrentId() noexcept
    {
        return detail::CurrentTaskIdValue;
    }

    inline Task<void> Task<void>::CompletedTask()
    {
        TaskCompletionSource<void> source;
        source.SetResult();
        return source.GetTask();
    }

    inline Task<void> Task<void>::FromException(std::exception_ptr exception_)
    {
        TaskCompletionSource<void> source;
        source.SetException(exception_);
        return source.GetTask();
    }

    inline Task<void> Task<void>::FromException(std::vector<std::exception_ptr> exceptions_)
    {
        return FromException(std::make_exception_ptr(TaskAggregateException(std::move(exceptions_))));
    }

    inline Task<void> Task<void>::FromCanceled()
    {
        TaskCompletionSource<void> source;
        source.SetCanceled();
        return source.GetTask();
    }

    inline Task<void> Task<void>::FromCanceled(CancellationToken)
    {
        return FromCanceled();
    }

    inline std::size_t Task<void>::CurrentId() noexcept
    {
        return detail::CurrentTaskIdValue;
    }

    template<typename T>
    template<typename F>
    auto Task<T>::ContinueWith(F&& continuation_, TaskSchedulerPtr scheduler_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>
    {
        return ContinueWith(
            std::forward<F>(continuation_),
            TaskContinuationOptions::None,
            std::move(scheduler_));
    }

    template<typename T>
    template<typename F>
    auto Task<T>::ContinueWith(
        F&& continuation_,
        TaskContinuationOptions options_,
        TaskSchedulerPtr scheduler_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>
    {
        return ContinueWith(
            std::forward<F>(continuation_),
            options_,
            std::move(scheduler_),
            CancellationToken::None());
    }

    template<typename T>
    template<typename F>
    auto Task<T>::ContinueWith(
        F&& continuation_,
        TaskContinuationOptions options_,
        TaskSchedulerPtr scheduler_,
        CancellationToken cancellationToken_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<T>>>
    {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, Task<T>>;

        TaskCompletionSource<ReturnType> source;
        auto resultTask = source.GetTask();

        if (!m_state)
        {
            source.SetException(std::make_exception_ptr(std::logic_error("Task does not have state.")));
            return resultTask;
        }

        const bool lazyCancellation = HasFlag(options_, TaskContinuationOptions::LazyCancellation);
        if (cancellationToken_.IsCancellationRequested() && !lazyCancellation)
        {
            source.SetCanceled();
            return resultTask;
        }

        auto scheduler = detail::RequireScheduler(std::move(scheduler_));
        auto callback = std::make_shared<std::decay_t<F>>(std::forward<F>(continuation_));
        auto sourceState = m_state;
        auto registration = std::make_shared<CancellationTokenRegistration>();
        if (cancellationToken_.CanBeCanceled() && !lazyCancellation)
        {
            *registration = cancellationToken_.Register([source]() mutable
            {
                source.TrySetCanceled();
            });
        }

        sourceState->AddContinuation([sourceState, source, scheduler, callback, options_, cancellationToken_, registration]() mutable
        {
            if (cancellationToken_.IsCancellationRequested())
            {
                source.TrySetCanceled();
                registration->Unregister();
                return;
            }

            if (!ShouldRunContinuation(sourceState->Status(), options_))
            {
                source.TrySetCanceled();
                registration->Unregister();
                return;
            }

            try
            {
                auto targetScheduler = HasFlag(options_, TaskContinuationOptions::ExecuteSynchronously)
                    ? InlineScheduler()
                    : (HasFlag(options_, TaskContinuationOptions::RunContinuationsAsynchronously)
                        ? DefaultScheduler()
                        : scheduler);
                auto visibleScheduler = HasFlag(options_, TaskContinuationOptions::HideScheduler)
                    ? DefaultScheduler()
                    : targetScheduler;

                targetScheduler->Schedule([sourceState, source, callback, cancellationToken_, registration, visibleScheduler]() mutable
                {
                    registration->Unregister();
                    source.MarkRunning();
                    if (cancellationToken_.IsCancellationRequested())
                    {
                        source.TrySetCanceled();
                        return;
                    }

                    try
                    {
                        detail::CurrentTaskIdScope currentId(source.Id());
                        CurrentTaskSchedulerScope schedulerScope(visibleScheduler);
                        Task<T> completedTask(sourceState);
                        if constexpr (std::is_void_v<ReturnType>)
                        {
                            std::invoke(*callback, completedTask);
                            source.TrySetResult();
                        }
                        else
                        {
                            source.TrySetResult(std::invoke(*callback, completedTask));
                        }
                    }
                    catch (const TaskCanceledException&)
                    {
                        source.TrySetCanceled();
                    }
                    catch (...)
                    {
                        source.TrySetException(std::current_exception());
                    }
                });
            }
            catch (...)
            {
                source.TrySetException(std::current_exception());
            }
        });

        return resultTask;
    }

    template<typename F>
    auto Run(F&& work_, TaskSchedulerPtr scheduler_, CancellationToken cancellationToken_)
        -> Task<detail::RunReturnT<std::decay_t<F>>>
    {
        using WorkType = std::decay_t<F>;
        using ReturnType = detail::RunReturnT<WorkType>;

        TaskCompletionSource<ReturnType> source;
        auto resultTask = source.GetTask();

        if (cancellationToken_.IsCancellationRequested())
        {
            source.SetCanceled();
            return resultTask;
        }

        auto scheduler = detail::RequireScheduler(std::move(scheduler_));
        auto work = std::make_shared<WorkType>(std::forward<F>(work_));

        try
        {
            auto runningScheduler = scheduler;
            scheduler->Schedule([source, work, cancellationToken_, runningScheduler]() mutable
            {
                detail::CurrentTaskIdScope currentId(source.Id());
                CurrentTaskSchedulerScope schedulerScope(runningScheduler);
                source.MarkRunning();

                if (cancellationToken_.IsCancellationRequested())
                {
                    source.TrySetCanceled();
                    return;
                }

                try
                {
                    if constexpr (std::is_void_v<ReturnType>)
                    {
                        detail::InvokeWork(*work, cancellationToken_);
                        source.TrySetResult();
                    }
                    else
                    {
                        source.TrySetResult(detail::InvokeWork(*work, cancellationToken_));
                    }
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            });
        }
        catch (const TaskCanceledException&)
        {
            source.TrySetCanceled();
        }
        catch (...)
        {
            source.TrySetException(std::current_exception());
        }

        return resultTask;
    }

    template<typename F>
    auto Run(F&& work_, TaskSchedulerPtr scheduler_ = DefaultScheduler())
        -> Task<detail::RunReturnT<std::decay_t<F>>>
    {
        return Run(
            std::forward<F>(work_),
            std::move(scheduler_),
            CancellationToken::None());
    }

    template<typename F>
    auto Run(F&& work_, CancellationToken cancellationToken_)
        -> Task<detail::RunReturnT<std::decay_t<F>>>
    {
        return Run(
            std::forward<F>(work_),
            DefaultScheduler(),
            std::move(cancellationToken_));
    }

    template<typename F>
    auto StartNew(
        F&& work_,
        CancellationToken cancellationToken_ = CancellationToken::None(),
        TaskCreationOptions creationOptions_ = TaskCreationOptions::None,
        TaskSchedulerPtr scheduler_ = CurrentScheduler(),
        std::shared_ptr<void> asyncState_ = nullptr)
        -> Task<detail::RunReturnT<std::decay_t<F>>>
    {
        using WorkType = std::decay_t<F>;
        using ReturnType = detail::RunReturnT<WorkType>;

        TaskCompletionSource<ReturnType> source(std::move(asyncState_), creationOptions_);
        auto resultTask = source.GetTask();

        if (cancellationToken_.IsCancellationRequested())
        {
            source.SetCanceled();
            return resultTask;
        }

        auto scheduler = detail::RequireScheduler(std::move(scheduler_));
        auto work = std::make_shared<WorkType>(std::forward<F>(work_));
        auto action = [source, work, cancellationToken_, scheduler, creationOptions_]() mutable
        {
            detail::CurrentTaskIdScope currentId(source.Id());
            const auto visibleScheduler = HasFlag(creationOptions_, TaskCreationOptions::HideScheduler)
                ? DefaultScheduler()
                : scheduler;
            CurrentTaskSchedulerScope schedulerScope(visibleScheduler);
            source.MarkRunning();

            if (cancellationToken_.IsCancellationRequested())
            {
                source.TrySetCanceled();
                return;
            }

            try
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    detail::InvokeWork(*work, cancellationToken_);
                    source.TrySetResult();
                }
                else
                {
                    source.TrySetResult(detail::InvokeWork(*work, cancellationToken_));
                }
            }
            catch (const TaskCanceledException&)
            {
                source.TrySetCanceled();
            }
            catch (...)
            {
                source.TrySetException(std::current_exception());
            }
        };

        try
        {
            if (HasFlag(creationOptions_, TaskCreationOptions::LongRunning))
            {
                std::thread(std::move(action)).detach();
            }
            else
            {
                scheduler->Schedule(std::move(action));
            }
        }
        catch (const TaskCanceledException&)
        {
            source.TrySetCanceled();
        }
        catch (...)
        {
            source.TrySetException(std::current_exception());
        }

        return resultTask;
    }

    class TaskFactory final
    {
    public:
        explicit TaskFactory(
            CancellationToken cancellationToken_ = CancellationToken::None(),
            TaskCreationOptions creationOptions_ = TaskCreationOptions::None,
            TaskContinuationOptions continuationOptions_ = TaskContinuationOptions::None,
            TaskSchedulerPtr scheduler_ = DefaultScheduler())
            : m_cancellationToken(std::move(cancellationToken_))
            , m_creationOptions(creationOptions_)
            , m_continuationOptions(continuationOptions_)
            , m_scheduler(detail::RequireScheduler(std::move(scheduler_)))
        {
        }

        const CancellationToken& CancellationTokenValue() const noexcept
        {
            return m_cancellationToken;
        }

        TaskCreationOptions CreationOptions() const noexcept
        {
            return m_creationOptions;
        }

        TaskContinuationOptions ContinuationOptions() const noexcept
        {
            return m_continuationOptions;
        }

        TaskSchedulerPtr Scheduler() const noexcept
        {
            return m_scheduler;
        }

        template<typename F>
        auto StartNew(F&& work_) const
            -> Task<detail::RunReturnT<std::decay_t<F>>>
        {
            return iCAX::Tasks::StartNew(
                std::forward<F>(work_),
                m_cancellationToken,
                m_creationOptions,
                m_scheduler);
        }

        template<typename F>
        auto StartNew(F&& work_, std::shared_ptr<void> asyncState_) const
            -> Task<detail::RunReturnT<std::decay_t<F>>>
        {
            return iCAX::Tasks::StartNew(
                std::forward<F>(work_),
                m_cancellationToken,
                m_creationOptions,
                m_scheduler,
                std::move(asyncState_));
        }

    private:
        CancellationToken m_cancellationToken;
        TaskCreationOptions m_creationOptions = TaskCreationOptions::None;
        TaskContinuationOptions m_continuationOptions = TaskContinuationOptions::None;
        TaskSchedulerPtr m_scheduler;
    };

    template<typename Rep, typename Period>
    Task<void> Delay(
        const std::chrono::duration<Rep, Period>& timeout_,
        CancellationToken cancellationToken_ = CancellationToken::None())
    {
        TaskCompletionSource<void> source;
        auto resultTask = source.GetTask();

        if (cancellationToken_.IsCancellationRequested())
        {
            source.SetCanceled();
            return resultTask;
        }

        auto registration = std::make_shared<CancellationTokenRegistration>();
        if (cancellationToken_.CanBeCanceled())
        {
            *registration = cancellationToken_.Register([source]() mutable
            {
                source.TrySetCanceled();
            });
        }

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_);
        ScheduleTimer(delay, [source, cancellationToken_, registration]() mutable
        {
            if (cancellationToken_.IsCancellationRequested())
            {
                source.TrySetCanceled();
            }
            else
            {
                source.TrySetResult();
            }

            registration->Unregister();
        });

        return resultTask;
    }

    template<typename T, typename Rep, typename Period>
    Task<T> TimeoutAfter(Task<T> task_, const std::chrono::duration<Rep, Period>& timeout_)
    {
        if (!task_)
        {
            return Task<T>::FromException(std::make_exception_ptr(
                std::logic_error("Task does not have state.")));
        }

        TaskCompletionSource<T> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        task_.ContinueWith(
            [source, done](Task<T> completed_) mutable
            {
                bool expected = false;
                if (!done->compare_exchange_strong(expected, true))
                {
                    return;
                }

                try
                {
                    source.TrySetResult(completed_.Result());
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_);
        ScheduleTimer(delay, [source, done]() mutable
        {
            bool expected = false;
            if (done->compare_exchange_strong(expected, true))
            {
                source.TrySetException(std::make_exception_ptr(TaskTimeoutException()));
            }
        });

        return resultTask;
    }

    template<typename Rep, typename Period>
    Task<void> TimeoutAfter(Task<void> task_, const std::chrono::duration<Rep, Period>& timeout_)
    {
        if (!task_)
        {
            return Task<void>::FromException(std::make_exception_ptr(
                std::logic_error("Task does not have state.")));
        }

        TaskCompletionSource<void> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        task_.ContinueWith(
            [source, done](Task<void> completed_) mutable
            {
                bool expected = false;
                if (!done->compare_exchange_strong(expected, true))
                {
                    return;
                }

                try
                {
                    completed_.Result();
                    source.TrySetResult();
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_);
        ScheduleTimer(delay, [source, done]() mutable
        {
            bool expected = false;
            if (done->compare_exchange_strong(expected, true))
            {
                source.TrySetException(std::make_exception_ptr(TaskTimeoutException()));
            }
        });

        return resultTask;
    }

    template<typename T>
    Task<T> WaitAsync(Task<T> task_, CancellationToken cancellationToken_)
    {
        if (!task_)
        {
            return Task<T>::FromException(std::make_exception_ptr(
                std::logic_error("Task does not have state.")));
        }

        TaskCompletionSource<T> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        if (cancellationToken_.IsCancellationRequested())
        {
            source.SetCanceled();
            return resultTask;
        }

        auto registration = std::make_shared<CancellationTokenRegistration>();
        if (cancellationToken_.CanBeCanceled())
        {
            *registration = cancellationToken_.Register([source, done]() mutable
            {
                bool expected = false;
                if (done->compare_exchange_strong(expected, true))
                {
                    source.TrySetCanceled();
                }
            });
        }

        task_.ContinueWith(
            [source, done, registration](Task<T> completed_) mutable
            {
                bool expected = false;
                if (!done->compare_exchange_strong(expected, true))
                {
                    return;
                }

                registration->Unregister();

                try
                {
                    source.TrySetResult(completed_.Result());
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        return resultTask;
    }

    inline Task<void> WaitAsync(Task<void> task_, CancellationToken cancellationToken_)
    {
        if (!task_)
        {
            return Task<void>::FromException(std::make_exception_ptr(
                std::logic_error("Task does not have state.")));
        }

        TaskCompletionSource<void> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        if (cancellationToken_.IsCancellationRequested())
        {
            source.SetCanceled();
            return resultTask;
        }

        auto registration = std::make_shared<CancellationTokenRegistration>();
        if (cancellationToken_.CanBeCanceled())
        {
            *registration = cancellationToken_.Register([source, done]() mutable
            {
                bool expected = false;
                if (done->compare_exchange_strong(expected, true))
                {
                    source.TrySetCanceled();
                }
            });
        }

        task_.ContinueWith(
            [source, done, registration](Task<void> completed_) mutable
            {
                bool expected = false;
                if (!done->compare_exchange_strong(expected, true))
                {
                    return;
                }

                registration->Unregister();

                try
                {
                    completed_.Result();
                    source.TrySetResult();
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        return resultTask;
    }

    template<typename T, typename Rep, typename Period>
    Task<T> WaitAsync(
        Task<T> task_,
        const std::chrono::duration<Rep, Period>& timeout_,
        CancellationToken cancellationToken_ = CancellationToken::None())
    {
        return WaitAsync(TimeoutAfter(std::move(task_), timeout_), std::move(cancellationToken_));
    }

    template<typename Rep, typename Period>
    Task<void> WaitAsync(
        Task<void> task_,
        const std::chrono::duration<Rep, Period>& timeout_,
        CancellationToken cancellationToken_ = CancellationToken::None())
    {
        return WaitAsync(TimeoutAfter(std::move(task_), timeout_), std::move(cancellationToken_));
    }

    template<typename T>
    Task<std::vector<T>> WhenAll(std::vector<Task<T>> tasks_)
    {
        if (tasks_.empty())
        {
            return Task<std::vector<T>>::FromResult({});
        }

        TaskCompletionSource<std::vector<T>> source;
        auto resultTask = source.GetTask();
        auto results = std::make_shared<std::vector<std::optional<T>>>(tasks_.size());
        auto remaining = std::make_shared<std::atomic_size_t>(tasks_.size());
        auto gate = std::make_shared<std::mutex>();
        auto exceptions = std::make_shared<std::vector<std::exception_ptr>>();
        auto anyCanceled = std::make_shared<std::atomic_bool>(false);

        for (std::size_t index = 0; index < tasks_.size(); ++index)
        {
            tasks_[index].ContinueWith(
                [index, results, remaining, gate, exceptions, anyCanceled, source](Task<T> completed_) mutable
                {
                    try
                    {
                        (*results)[index] = completed_.Result();
                    }
                    catch (const TaskCanceledException&)
                    {
                        anyCanceled->store(true);
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> lock(*gate);
                        exceptions->push_back(std::current_exception());
                    }

                    if (remaining->fetch_sub(1) == 1)
                    {
                        std::vector<std::exception_ptr> exceptionList;
                        {
                            std::lock_guard<std::mutex> lock(*gate);
                            exceptionList = *exceptions;
                        }

                        if (!exceptionList.empty())
                        {
                            source.TrySetException(std::make_exception_ptr(
                                TaskAggregateException(std::move(exceptionList))));
                            return;
                        }

                        if (anyCanceled->load())
                        {
                            source.TrySetCanceled();
                            return;
                        }

                        std::vector<T> values;
                        values.reserve(results->size());
                        for (const auto& item : *results)
                        {
                            values.push_back(*item);
                        }

                        source.TrySetResult(std::move(values));
                    }
                },
                InlineScheduler());
        }

        return resultTask;
    }

    inline Task<void> WhenAll(std::vector<Task<void>> tasks_)
    {
        if (tasks_.empty())
        {
            return Task<void>::CompletedTask();
        }

        TaskCompletionSource<void> source;
        auto resultTask = source.GetTask();
        auto remaining = std::make_shared<std::atomic_size_t>(tasks_.size());
        auto gate = std::make_shared<std::mutex>();
        auto exceptions = std::make_shared<std::vector<std::exception_ptr>>();
        auto anyCanceled = std::make_shared<std::atomic_bool>(false);

        for (auto& task : tasks_)
        {
            task.ContinueWith(
                [remaining, gate, exceptions, anyCanceled, source](Task<void> completed_) mutable
                {
                    try
                    {
                        completed_.Result();
                    }
                    catch (const TaskCanceledException&)
                    {
                        anyCanceled->store(true);
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> lock(*gate);
                        exceptions->push_back(std::current_exception());
                    }

                    if (remaining->fetch_sub(1) == 1)
                    {
                        std::vector<std::exception_ptr> exceptionList;
                        {
                            std::lock_guard<std::mutex> lock(*gate);
                            exceptionList = *exceptions;
                        }

                        if (!exceptionList.empty())
                        {
                            source.TrySetException(std::make_exception_ptr(
                                TaskAggregateException(std::move(exceptionList))));
                            return;
                        }

                        if (anyCanceled->load())
                        {
                            source.TrySetCanceled();
                            return;
                        }

                        source.TrySetResult();
                    }
                },
                InlineScheduler());
        }

        return resultTask;
    }

    template<typename T>
    Task<Task<T>> WhenAny(std::vector<Task<T>> tasks_)
    {
        if (tasks_.empty())
        {
            return Task<Task<T>>::FromException(
                std::make_exception_ptr(std::invalid_argument("WhenAny requires at least one task.")));
        }

        TaskCompletionSource<Task<T>> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        for (auto& task : tasks_)
        {
            task.ContinueWith(
                [source, done](Task<T> completed_) mutable
                {
                    bool expected = false;
                    if (done->compare_exchange_strong(expected, true))
                    {
                        source.TrySetResult(completed_);
                    }
                },
                InlineScheduler());
        }

        return resultTask;
    }

    inline Task<Task<void>> WhenAny(std::vector<Task<void>> tasks_)
    {
        if (tasks_.empty())
        {
            return Task<Task<void>>::FromException(
                std::make_exception_ptr(std::invalid_argument("WhenAny requires at least one task.")));
        }

        TaskCompletionSource<Task<void>> source;
        auto resultTask = source.GetTask();
        auto done = std::make_shared<std::atomic_bool>(false);

        for (auto& task : tasks_)
        {
            task.ContinueWith(
                [source, done](Task<void> completed_) mutable
                {
                    bool expected = false;
                    if (done->compare_exchange_strong(expected, true))
                    {
                        source.TrySetResult(completed_);
                    }
                },
                InlineScheduler());
        }

        return resultTask;
    }

    template<typename T>
    Task<std::vector<T>> WhenAll(std::initializer_list<Task<T>> tasks_)
    {
        return WhenAll<T>(std::vector<Task<T>>(tasks_));
    }

    inline Task<void> WhenAll(std::initializer_list<Task<void>> tasks_)
    {
        return WhenAll(std::vector<Task<void>>(tasks_));
    }

    template<typename T>
    Task<Task<T>> WhenAny(std::initializer_list<Task<T>> tasks_)
    {
        return WhenAny<T>(std::vector<Task<T>>(tasks_));
    }

    inline Task<Task<void>> WhenAny(std::initializer_list<Task<void>> tasks_)
    {
        return WhenAny(std::vector<Task<void>>(tasks_));
    }

    template<typename T>
    void WaitAll(std::vector<Task<T>> tasks_)
    {
        WhenAll<T>(std::move(tasks_)).Result();
    }

    inline void WaitAll(std::vector<Task<void>> tasks_)
    {
        WhenAll(std::move(tasks_)).Result();
    }

    template<typename T>
    void WaitAll(std::initializer_list<Task<T>> tasks_)
    {
        WaitAll<T>(std::vector<Task<T>>(tasks_));
    }

    inline void WaitAll(std::initializer_list<Task<void>> tasks_)
    {
        WaitAll(std::vector<Task<void>>(tasks_));
    }

    template<typename T>
    std::size_t WaitAny(std::vector<Task<T>> tasks_)
    {
        if (tasks_.empty())
        {
            throw std::invalid_argument("WaitAny requires at least one task.");
        }

        const auto completed = WhenAny<T>(tasks_).Result();
        for (std::size_t index = 0; index < tasks_.size(); ++index)
        {
            if (tasks_[index].Id() == completed.Id())
            {
                return index;
            }
        }

        throw std::logic_error("Completed task was not found.");
    }

    inline std::size_t WaitAny(std::vector<Task<void>> tasks_)
    {
        if (tasks_.empty())
        {
            throw std::invalid_argument("WaitAny requires at least one task.");
        }

        const auto completed = WhenAny(tasks_).Result();
        for (std::size_t index = 0; index < tasks_.size(); ++index)
        {
            if (tasks_[index].Id() == completed.Id())
            {
                return index;
            }
        }

        throw std::logic_error("Completed task was not found.");
    }

    template<typename T>
    std::size_t WaitAny(std::initializer_list<Task<T>> tasks_)
    {
        return WaitAny<T>(std::vector<Task<T>>(tasks_));
    }

    inline std::size_t WaitAny(std::initializer_list<Task<void>> tasks_)
    {
        return WaitAny(std::vector<Task<void>>(tasks_));
    }

    template<typename T>
    Task<T> Unwrap(Task<Task<T>> task_)
    {
        TaskCompletionSource<T> source;
        auto resultTask = source.GetTask();

        task_.ContinueWith(
            [source](Task<Task<T>> outer_) mutable
            {
                try
                {
                    auto inner = outer_.Result();
                    inner.ContinueWith(
                        [source](Task<T> completed_) mutable
                        {
                            try
                            {
                                source.TrySetResult(completed_.Result());
                            }
                            catch (const TaskCanceledException&)
                            {
                                source.TrySetCanceled();
                            }
                            catch (...)
                            {
                                source.TrySetException(std::current_exception());
                            }
                        },
                        InlineScheduler());
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        return resultTask;
    }

    inline Task<void> Unwrap(Task<Task<void>> task_)
    {
        TaskCompletionSource<void> source;
        auto resultTask = source.GetTask();

        task_.ContinueWith(
            [source](Task<Task<void>> outer_) mutable
            {
                try
                {
                    auto inner = outer_.Result();
                    inner.ContinueWith(
                        [source](Task<void> completed_) mutable
                        {
                            try
                            {
                                completed_.Result();
                                source.TrySetResult();
                            }
                            catch (const TaskCanceledException&)
                            {
                                source.TrySetCanceled();
                            }
                            catch (...)
                            {
                                source.TrySetException(std::current_exception());
                            }
                        },
                        InlineScheduler());
                }
                catch (const TaskCanceledException&)
                {
                    source.TrySetCanceled();
                }
                catch (...)
                {
                    source.TrySetException(std::current_exception());
                }
            },
            InlineScheduler());

        return resultTask;
    }

    template<typename F>
    auto Task<void>::ContinueWith(F&& continuation_, TaskSchedulerPtr scheduler_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>
    {
        return ContinueWith(
            std::forward<F>(continuation_),
            TaskContinuationOptions::None,
            std::move(scheduler_));
    }

    template<typename F>
    auto Task<void>::ContinueWith(
        F&& continuation_,
        TaskContinuationOptions options_,
        TaskSchedulerPtr scheduler_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>
    {
        return ContinueWith(
            std::forward<F>(continuation_),
            options_,
            std::move(scheduler_),
            CancellationToken::None());
    }

    template<typename F>
    auto Task<void>::ContinueWith(
        F&& continuation_,
        TaskContinuationOptions options_,
        TaskSchedulerPtr scheduler_,
        CancellationToken cancellationToken_) const
        -> Task<std::invoke_result_t<std::decay_t<F>, Task<void>>>
    {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, Task<void>>;

        TaskCompletionSource<ReturnType> source;
        auto resultTask = source.GetTask();

        if (!m_state)
        {
            source.SetException(std::make_exception_ptr(std::logic_error("Task does not have state.")));
            return resultTask;
        }

        const bool lazyCancellation = HasFlag(options_, TaskContinuationOptions::LazyCancellation);
        if (cancellationToken_.IsCancellationRequested() && !lazyCancellation)
        {
            source.SetCanceled();
            return resultTask;
        }

        auto scheduler = detail::RequireScheduler(std::move(scheduler_));
        auto callback = std::make_shared<std::decay_t<F>>(std::forward<F>(continuation_));
        auto sourceState = m_state;
        auto registration = std::make_shared<CancellationTokenRegistration>();
        if (cancellationToken_.CanBeCanceled() && !lazyCancellation)
        {
            *registration = cancellationToken_.Register([source]() mutable
            {
                source.TrySetCanceled();
            });
        }

        sourceState->AddContinuation([sourceState, source, scheduler, callback, options_, cancellationToken_, registration]() mutable
        {
            if (cancellationToken_.IsCancellationRequested())
            {
                source.TrySetCanceled();
                registration->Unregister();
                return;
            }

            if (!ShouldRunContinuation(sourceState->Status(), options_))
            {
                source.TrySetCanceled();
                registration->Unregister();
                return;
            }

            try
            {
                auto targetScheduler = HasFlag(options_, TaskContinuationOptions::ExecuteSynchronously)
                    ? InlineScheduler()
                    : (HasFlag(options_, TaskContinuationOptions::RunContinuationsAsynchronously)
                        ? DefaultScheduler()
                        : scheduler);
                auto visibleScheduler = HasFlag(options_, TaskContinuationOptions::HideScheduler)
                    ? DefaultScheduler()
                    : targetScheduler;

                targetScheduler->Schedule([sourceState, source, callback, cancellationToken_, registration, visibleScheduler]() mutable
                {
                    registration->Unregister();
                    source.MarkRunning();
                    if (cancellationToken_.IsCancellationRequested())
                    {
                        source.TrySetCanceled();
                        return;
                    }

                    try
                    {
                        detail::CurrentTaskIdScope currentId(source.Id());
                        CurrentTaskSchedulerScope schedulerScope(visibleScheduler);
                        Task<void> completedTask(sourceState);
                        if constexpr (std::is_void_v<ReturnType>)
                        {
                            std::invoke(*callback, completedTask);
                            source.TrySetResult();
                        }
                        else
                        {
                            source.TrySetResult(std::invoke(*callback, completedTask));
                        }
                    }
                    catch (const TaskCanceledException&)
                    {
                        source.TrySetCanceled();
                    }
                    catch (...)
                    {
                        source.TrySetException(std::current_exception());
                    }
                });
            }
            catch (...)
            {
                source.TrySetException(std::current_exception());
            }
        });

        return resultTask;
    }
}
