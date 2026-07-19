#pragma once

#include "Task.h"

#include <coroutine>
#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace iCAX::Coroutines
{
    using iCAX::Tasks::Task;
    using iCAX::Tasks::TaskCompletionSource;
    using iCAX::Tasks::TaskSchedulerPtr;

    enum class ECoroutineStatus
    {
        Invalid = 0,
        Scheduled,
        Running,
        Waiting,
        Paused,
        Completed,
        Canceled,
        Faulted,
    };

    template<typename TResult = void>
    class CCoroutine;

    template<typename TResult = void>
    class CCoroutineHandle;

    class CCoroutineRuntime;

    namespace detail
    {
        class ICoroutineRuntime
        {
        public:
            virtual ~ICoroutineRuntime() = default;

            virtual TaskSchedulerPtr Scheduler() const = 0;
            virtual void SuspendUntilNextFrame(std::uint64_t coroutineID_) = 0;
            virtual void SuspendForSeconds(std::uint64_t coroutineID_, double seconds_) = 0;
            virtual void SuspendUntil(
                std::uint64_t coroutineID_,
                std::function<bool()> predicate_) = 0;
            virtual void SuspendForTask(std::uint64_t coroutineID_) = 0;
            virtual void Wake(std::uint64_t coroutineID_) = 0;
        };

        struct CCoroutineStatusState final
        {
            ECoroutineStatus Status = ECoroutineStatus::Scheduled;
        };

        class _TASK_EXP CCoroutinePromiseBase
        {
        public:
            std::suspend_always initial_suspend() const noexcept;
            std::suspend_always final_suspend() const noexcept;
            void unhandled_exception() noexcept;

            std::exception_ptr Exception() const noexcept;
            std::shared_ptr<ICoroutineRuntime> Runtime() const;
            std::uint64_t CoroutineID() const noexcept;

        private:
            void Bind(
                std::weak_ptr<ICoroutineRuntime> runtime_,
                std::uint64_t coroutineID_) noexcept;

        private:
            std::weak_ptr<ICoroutineRuntime> m_runtime;
            std::uint64_t m_coroutineID = 0;
            std::exception_ptr m_exception;

            friend class CCoroutineRuntimeState;
        };

        template<typename TResult>
        struct CCoroutinePromise;

        struct CCoroutineFrame final
        {
            std::coroutine_handle<> NativeHandle;
            CCoroutinePromiseBase* pPromise = nullptr;
            std::function<std::exception_ptr()> Complete;
            std::function<void()> Cancel;
            std::function<void(std::exception_ptr)> Fault;
        };

        struct CCoroutineStartInfo final
        {
            std::uint64_t ID = 0;
            std::shared_ptr<CCoroutineStatusState> StatusState;
        };

        class CCoroutineRuntimeState;
    }

    class _TASK_EXP CCoroutineHandleBase
    {
    public:
        CCoroutineHandleBase() = default;

        bool IsValid() const noexcept;
        explicit operator bool() const noexcept;
        std::uint64_t ID() const noexcept;

    protected:
        CCoroutineHandleBase(
            std::uint64_t id_,
            std::shared_ptr<detail::CCoroutineStatusState> statusState_,
            std::weak_ptr<detail::ICoroutineRuntime> runtime_);

    private:
        std::uint64_t m_id = 0;
        std::shared_ptr<detail::CCoroutineStatusState> m_statusState;
        std::weak_ptr<detail::ICoroutineRuntime> m_runtime;

        friend class CCoroutineRuntime;
        friend class detail::CCoroutineRuntimeState;
    };

    template<typename TResult>
    class CCoroutineHandle final : public CCoroutineHandleBase
    {
    public:
        CCoroutineHandle() = default;

        Task<TResult> Completion() const
        {
            return m_completion;
        }

    private:
        CCoroutineHandle(
            std::uint64_t id_,
            std::shared_ptr<detail::CCoroutineStatusState> statusState_,
            std::weak_ptr<detail::ICoroutineRuntime> runtime_,
            Task<TResult> completion_)
            : CCoroutineHandleBase(id_, std::move(statusState_), std::move(runtime_))
            , m_completion(std::move(completion_))
        {
        }

    private:
        Task<TResult> m_completion;

        friend class CCoroutineRuntime;
    };

    template<typename TResult>
    class CCoroutine final
    {
        static_assert(!std::is_reference_v<TResult>, "Coroutine result cannot be a reference type");

    public:
        using promise_type = detail::CCoroutinePromise<TResult>;
        using NativeHandle = std::coroutine_handle<promise_type>;

        CCoroutine() noexcept = default;
        CCoroutine(const CCoroutine&) = delete;
        CCoroutine& operator=(const CCoroutine&) = delete;

        CCoroutine(CCoroutine&& other_) noexcept
            : m_handle(std::exchange(other_.m_handle, nullptr))
        {
        }

        CCoroutine& operator=(CCoroutine&& other_) noexcept
        {
            if (this != &other_)
            {
                if (m_handle)
                {
                    m_handle.destroy();
                }
                m_handle = std::exchange(other_.m_handle, nullptr);
            }
            return *this;
        }

        ~CCoroutine()
        {
            if (m_handle)
            {
                m_handle.destroy();
            }
        }

        bool IsValid() const noexcept
        {
            return m_handle != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return IsValid();
        }

    private:
        explicit CCoroutine(NativeHandle handle_) noexcept
            : m_handle(handle_)
        {
        }

        NativeHandle Release() noexcept
        {
            return std::exchange(m_handle, nullptr);
        }

    private:
        NativeHandle m_handle = nullptr;

        friend promise_type;
        friend class CCoroutineRuntime;
    };

    namespace detail
    {
        template<typename TResult>
        struct CCoroutinePromise final : CCoroutinePromiseBase
        {
            CCoroutine<TResult> get_return_object() noexcept
            {
                using NativeHandle = typename CCoroutine<TResult>::NativeHandle;
                return CCoroutine<TResult>(NativeHandle::from_promise(*this));
            }

            template<typename TValue>
                requires std::constructible_from<TResult, TValue&&>
            void return_value(TValue&& value_)
            {
                m_result.emplace(std::forward<TValue>(value_));
            }

            TResult TakeResult()
            {
                if (!m_result)
                {
                    throw std::logic_error("Coroutine completed without a result");
                }
                return std::move(*m_result);
            }

        private:
            std::optional<TResult> m_result;
        };

        template<>
        struct CCoroutinePromise<void> final : CCoroutinePromiseBase
        {
            CCoroutine<> get_return_object() noexcept
            {
                using NativeHandle = typename CCoroutine<>::NativeHandle;
                return CCoroutine<>(NativeHandle::from_promise(*this));
            }

            void return_void() const noexcept
            {
            }
        };

        template<typename TResult>
        class CCoroutineFrameOwner final
        {
        public:
            using NativeHandle = typename CCoroutine<TResult>::NativeHandle;

            CCoroutineFrameOwner(NativeHandle handle_, TaskSchedulerPtr scheduler_)
                : m_handle(handle_)
                , m_completionSource(std::move(scheduler_))
            {
            }

            ~CCoroutineFrameOwner()
            {
                DestroyFrame();
            }

            Task<TResult> Completion() const
            {
                return m_completionSource.GetTask();
            }

            std::exception_ptr Complete() noexcept
            {
                try
                {
                    if constexpr (std::is_void_v<TResult>)
                    {
                        DestroyFrame();
                        m_completionSource.TrySetResult();
                    }
                    else
                    {
                        TResult result = m_handle.promise().TakeResult();
                        DestroyFrame();
                        m_completionSource.TrySetResult(std::move(result));
                    }
                    return nullptr;
                }
                catch (...)
                {
                    auto exception = std::current_exception();
                    DestroyFrame();
                    try
                    {
                        m_completionSource.TrySetException(exception);
                    }
                    catch (...)
                    {
                    }
                    return exception;
                }
            }

            void Cancel() noexcept
            {
                DestroyFrame();
                try
                {
                    m_completionSource.TrySetCanceled();
                }
                catch (...)
                {
                }
            }

            void Fault(std::exception_ptr exception_) noexcept
            {
                DestroyFrame();
                try
                {
                    m_completionSource.TrySetException(exception_);
                }
                catch (...)
                {
                }
            }

        private:
            void DestroyFrame() noexcept
            {
                if (m_handle)
                {
                    m_handle.destroy();
                    m_handle = nullptr;
                }
            }

        private:
            NativeHandle m_handle = nullptr;
            TaskCompletionSource<TResult> m_completionSource;
        };
    }

    class _TASK_EXP CNextFrameAwaitable final
    {
    public:
        bool await_ready() const noexcept;

        template<typename TPromise>
        void await_suspend(std::coroutine_handle<TPromise> handle_) const
        {
            auto& promise = static_cast<detail::CCoroutinePromiseBase&>(handle_.promise());
            promise.Runtime()->SuspendUntilNextFrame(promise.CoroutineID());
        }

        void await_resume() const noexcept;
    };

    class _TASK_EXP CWaitForSecondsAwaitable final
    {
    public:
        explicit CWaitForSecondsAwaitable(double seconds_) noexcept;

        bool await_ready() const noexcept;

        template<typename TPromise>
        void await_suspend(std::coroutine_handle<TPromise> handle_) const
        {
            auto& promise = static_cast<detail::CCoroutinePromiseBase&>(handle_.promise());
            promise.Runtime()->SuspendForSeconds(promise.CoroutineID(), m_seconds);
        }

        void await_resume() const noexcept;

    private:
        double m_seconds = 0.0;
    };

    class _TASK_EXP CWaitUntilAwaitable final
    {
    public:
        explicit CWaitUntilAwaitable(std::function<bool()> predicate_);

        bool await_ready() const;

        template<typename TPromise>
        void await_suspend(std::coroutine_handle<TPromise> handle_)
        {
            auto& promise = static_cast<detail::CCoroutinePromiseBase&>(handle_.promise());
            promise.Runtime()->SuspendUntil(
                promise.CoroutineID(),
                std::move(m_predicate));
        }

        void await_resume() const noexcept;

    private:
        std::function<bool()> m_predicate;
    };

    inline CNextFrameAwaitable NextFrame() noexcept
    {
        return {};
    }

    inline CWaitForSecondsAwaitable WaitForSeconds(double seconds_) noexcept
    {
        return CWaitForSecondsAwaitable(seconds_);
    }

    template<typename Predicate>
    CWaitUntilAwaitable WaitUntil(Predicate&& predicate_)
    {
        return CWaitUntilAwaitable(std::function<bool()>(std::forward<Predicate>(predicate_)));
    }

    template<typename T>
    class CTaskAwaitable final
    {
    public:
        explicit CTaskAwaitable(Task<T> task_)
            : m_task(std::move(task_))
        {
        }

        bool await_ready() const
        {
            return !m_task.Valid() || m_task.IsCompleted();
        }

        template<typename TPromise>
        void await_suspend(std::coroutine_handle<TPromise> handle_)
        {
            auto& promise = static_cast<detail::CCoroutinePromiseBase&>(handle_.promise());
            auto runtime = promise.Runtime();
            runtime->SuspendForTask(promise.CoroutineID());

            const auto coroutineID = promise.CoroutineID();
            std::weak_ptr<detail::ICoroutineRuntime> weakRuntime = runtime;
            (void)m_task.ContinueWith(
                [weakRuntime, coroutineID](Task<T>)
                {
                    if (auto lockedRuntime = weakRuntime.lock())
                    {
                        lockedRuntime->Wake(coroutineID);
                    }
                },
                runtime->Scheduler());
        }

        T await_resume()
        {
            if constexpr (std::copy_constructible<T>)
            {
                return m_task.Result();
            }
            else
            {
                return m_task.TakeResult();
            }
        }

    private:
        Task<T> m_task;
    };

    template<>
    class CTaskAwaitable<void> final
    {
    public:
        explicit CTaskAwaitable(Task<void> task_)
            : m_task(std::move(task_))
        {
        }

        bool await_ready() const
        {
            return !m_task.Valid() || m_task.IsCompleted();
        }

        template<typename TPromise>
        void await_suspend(std::coroutine_handle<TPromise> handle_)
        {
            auto& promise = static_cast<detail::CCoroutinePromiseBase&>(handle_.promise());
            auto runtime = promise.Runtime();
            runtime->SuspendForTask(promise.CoroutineID());

            const auto coroutineID = promise.CoroutineID();
            std::weak_ptr<detail::ICoroutineRuntime> weakRuntime = runtime;
            (void)m_task.ContinueWith(
                [weakRuntime, coroutineID](Task<void>)
                {
                    if (auto lockedRuntime = weakRuntime.lock())
                    {
                        lockedRuntime->Wake(coroutineID);
                    }
                },
                runtime->Scheduler());
        }

        void await_resume()
        {
            m_task.Result();
        }

    private:
        Task<void> m_task;
    };

    template<typename T>
    CTaskAwaitable<T> Await(Task<T> task_)
    {
        return CTaskAwaitable<T>(std::move(task_));
    }

    class _TASK_EXP CCoroutineRuntime final
    {
    public:
        using ExceptionHandler = std::function<void(const CCoroutineHandleBase&, std::exception_ptr)>;

        explicit CCoroutineRuntime(TaskSchedulerPtr scheduler_);
        ~CCoroutineRuntime();

        CCoroutineRuntime(const CCoroutineRuntime&) = delete;
        CCoroutineRuntime& operator=(const CCoroutineRuntime&) = delete;

        template<typename TResult>
        CCoroutineHandle<TResult> Start(
            CCoroutine<TResult> coroutine_)
        {
            if (!coroutine_)
            {
                throw std::invalid_argument("Coroutine cannot be empty");
            }

            auto nativeHandle = coroutine_.Release();
            std::shared_ptr<detail::CCoroutineFrameOwner<TResult>> owner;
            try
            {
                owner = std::make_shared<detail::CCoroutineFrameOwner<TResult>>(
                    nativeHandle,
                    Scheduler());
            }
            catch (...)
            {
                nativeHandle.destroy();
                throw;
            }
            auto completion = owner->Completion();

            detail::CCoroutineFrame frame;
            frame.NativeHandle = nativeHandle;
            frame.pPromise = &nativeHandle.promise();
            frame.Complete = [owner]() { return owner->Complete(); };
            frame.Cancel = [owner]() { owner->Cancel(); };
            frame.Fault = [owner](std::exception_ptr exception_) { owner->Fault(exception_); };

            auto started = StartErased(std::move(frame));
            return CCoroutineHandle<TResult>(
                started.ID,
                std::move(started.StatusState),
                std::weak_ptr<detail::ICoroutineRuntime>(m_runtime),
                std::move(completion));
        }

        void Pause(const CCoroutineHandleBase& handle_);
        void Resume(const CCoroutineHandleBase& handle_);
        void Cancel(const CCoroutineHandleBase& handle_);
        bool IsRunning(const CCoroutineHandleBase& handle_) const;
        ECoroutineStatus Status(const CCoroutineHandleBase& handle_) const;

        std::size_t Tick(double deltaTime_, double totalTime_);
        void CancelAll();
        std::size_t RunningCount() const;
        TaskSchedulerPtr Scheduler() const;
        void SetExceptionHandler(ExceptionHandler handler_);

    private:
        detail::CCoroutineStartInfo StartErased(detail::CCoroutineFrame frame_);

    private:
        std::shared_ptr<detail::ICoroutineRuntime> m_runtime;
    };
}
