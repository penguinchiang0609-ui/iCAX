#include "pch.h"
#include "Coroutine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace iCAX::Coroutines::detail
{
    enum class EWaitState
    {
        Ready,
        Running,
        WaitingForTime,
        WaitingForPredicate,
        WaitingForTask,
        CancelRequested,
    };

    struct CCoroutineRecord final
    {
        CCoroutineFrame Frame;
        EWaitState WaitState = EWaitState::Ready;
        std::uint64_t EarliestFrame = 0;
        double WakeTime = 0.0;
        double PausedAtTime = 0.0;
        bool Paused = false;
        std::function<bool()> Predicate;
        std::shared_ptr<CCoroutineStatusState> StatusState =
            std::make_shared<CCoroutineStatusState>();
    };

    class CCoroutineRuntimeState final
        : public ICoroutineRuntime
        , public std::enable_shared_from_this<CCoroutineRuntimeState>
    {
    public:
        explicit CCoroutineRuntimeState(TaskSchedulerPtr scheduler_)
            : m_scheduler(std::move(scheduler_))
        {
            if (!m_scheduler)
            {
                throw std::invalid_argument("Coroutine scheduler cannot be null");
            }
        }

        ~CCoroutineRuntimeState() override
        {
            CancelAll();
        }

        TaskSchedulerPtr Scheduler() const override
        {
            return m_scheduler;
        }

        CCoroutineStartInfo Start(CCoroutineFrame frame_)
        {
            if (!frame_.NativeHandle || !frame_.pPromise
                || !frame_.Complete || !frame_.Cancel || !frame_.Fault)
            {
                throw std::invalid_argument("Coroutine frame is not valid");
            }

            const auto coroutineID = m_nextCoroutineID++;
            auto record = std::make_unique<CCoroutineRecord>();
            record->Frame = std::move(frame_);
            record->WaitState = EWaitState::Ready;
            record->EarliestFrame = m_frameIndex + (m_isTicking ? 1 : 0);
            record->StatusState->Status = ECoroutineStatus::Scheduled;
            record->Frame.pPromise->Bind(
                std::static_pointer_cast<ICoroutineRuntime>(shared_from_this()),
                coroutineID);

            CCoroutineStartInfo result;
            result.ID = coroutineID;
            result.StatusState = record->StatusState;

            m_coroutines.emplace(coroutineID, std::move(record));
            return result;
        }

        void Pause(const CCoroutineHandleBase& handle_)
        {
            auto* record = FindCoroutine(handle_);
            if (!record || record->Paused || record->WaitState == EWaitState::CancelRequested)
            {
                return;
            }
            record->Paused = true;
            record->PausedAtTime = m_totalTime;
            record->StatusState->Status = ECoroutineStatus::Paused;
        }

        void Resume(const CCoroutineHandleBase& handle_)
        {
            auto* record = FindCoroutine(handle_);
            if (!record || !record->Paused || record->WaitState == EWaitState::CancelRequested)
            {
                return;
            }
            if (record->WaitState == EWaitState::WaitingForTime)
            {
                record->WakeTime += (std::max)(0.0, m_totalTime - record->PausedAtTime);
            }
            record->Paused = false;
            record->PausedAtTime = 0.0;
            record->StatusState->Status = StatusForWaitState(record->WaitState);
        }

        void Cancel(const CCoroutineHandleBase& handle_)
        {
            if (!Owns(handle_))
            {
                return;
            }
            CancelCoroutine(handle_.m_id);
        }

        bool IsRunning(const CCoroutineHandleBase& handle_) const
        {
            const auto found = FindCoroutine(handle_);
            return found != nullptr && found->WaitState != EWaitState::CancelRequested;
        }

        ECoroutineStatus Status(const CCoroutineHandleBase& handle_) const
        {
            const auto found = FindCoroutine(handle_);
            if (!found)
            {
                return handle_.m_statusState
                    ? handle_.m_statusState->Status
                    : ECoroutineStatus::Invalid;
            }

            if (found->Paused)
            {
                return ECoroutineStatus::Paused;
            }
            return StatusForWaitState(found->WaitState);
        }

        std::size_t Tick(double deltaTime_, double totalTime_)
        {
            if (!std::isfinite(deltaTime_) || deltaTime_ < 0.0)
            {
                throw std::invalid_argument("Coroutine delta time must be finite and non-negative");
            }
            if (!std::isfinite(totalTime_) || totalTime_ < 0.0)
            {
                throw std::invalid_argument("Coroutine total time must be finite and non-negative");
            }
            if (m_isTicking)
            {
                throw std::logic_error("Coroutine runtime Tick cannot be re-entered");
            }

            ++m_frameIndex;
            m_totalTime = totalTime_;
            m_isTicking = true;
            struct CTickGuard final
            {
                bool& IsTicking;
                ~CTickGuard() { IsTicking = false; }
            } _TickGuard{ m_isTicking };

            std::vector<std::uint64_t> ready;
            ready.reserve(m_coroutines.size());
            std::vector<std::pair<std::uint64_t, std::exception_ptr>> predicateFailures;
            std::vector<std::uint64_t> candidates;
            candidates.reserve(m_coroutines.size());
            for (const auto& [coroutineID, _] : m_coroutines)
            {
                candidates.push_back(coroutineID);
            }

            for (const auto coroutineID : candidates)
            {
                auto found = m_coroutines.find(coroutineID);
                if (found == m_coroutines.end() || !CanRun(*found->second))
                {
                    continue;
                }
                auto* record = found->second.get();

                if (record->WaitState == EWaitState::WaitingForTime
                    && record->EarliestFrame <= m_frameIndex
                    && record->WakeTime <= m_totalTime)
                {
                    record->WaitState = EWaitState::Ready;
                }
                else if (record->WaitState == EWaitState::WaitingForPredicate
                    && record->EarliestFrame <= m_frameIndex)
                {
                    bool predicateReady = false;
                    try
                    {
                        auto predicate = record->Predicate;
                        predicateReady = predicate && predicate();
                    }
                    catch (...)
                    {
                        predicateFailures.emplace_back(coroutineID, std::current_exception());
                        continue;
                    }

                    found = m_coroutines.find(coroutineID);
                    if (found == m_coroutines.end() || !CanRun(*found->second))
                    {
                        continue;
                    }
                    record = found->second.get();
                    if (predicateReady && record->WaitState == EWaitState::WaitingForPredicate)
                    {
                        record->Predicate = {};
                        record->WaitState = EWaitState::Ready;
                    }
                }

                if (record->WaitState == EWaitState::Ready
                    && record->EarliestFrame <= m_frameIndex)
                {
                    ready.push_back(coroutineID);
                }
            }

            for (const auto& [coroutineID, exception] : predicateFailures)
            {
                FinishFaulted(coroutineID, exception);
            }

            std::size_t resumedCount = 0;
            for (const auto coroutineID : ready)
            {
                auto found = m_coroutines.find(coroutineID);
                if (found == m_coroutines.end() || !CanRun(*found->second))
                {
                    continue;
                }
                auto& record = *found->second;
                if (record.WaitState != EWaitState::Ready
                    || record.EarliestFrame > m_frameIndex)
                {
                    continue;
                }

                record.WaitState = EWaitState::Running;
                record.StatusState->Status = ECoroutineStatus::Running;
                m_currentCoroutineID = coroutineID;
                ++resumedCount;
                record.Frame.NativeHandle.resume();
                m_currentCoroutineID = 0;

                found = m_coroutines.find(coroutineID);
                if (found == m_coroutines.end())
                {
                    continue;
                }

                if (found->second->WaitState == EWaitState::CancelRequested)
                {
                    FinishCanceled(coroutineID);
                }
                else if (found->second->Frame.NativeHandle.done())
                {
                    const auto exception = found->second->Frame.pPromise->Exception();
                    if (exception)
                    {
                        FinishFaulted(coroutineID, exception);
                    }
                    else
                    {
                        FinishCompleted(coroutineID);
                    }
                }
                else if (found->second->WaitState == EWaitState::Running)
                {
                    found->second->WaitState = EWaitState::Ready;
                    found->second->EarliestFrame = m_frameIndex + 1;
                    found->second->StatusState->Status = found->second->Paused
                        ? ECoroutineStatus::Paused
                        : ECoroutineStatus::Scheduled;
                }
                else
                {
                    found->second->StatusState->Status = found->second->Paused
                        ? ECoroutineStatus::Paused
                        : ECoroutineStatus::Waiting;
                }
            }

            return resumedCount;
        }

        void CancelAll()
        {
            std::vector<std::uint64_t> coroutineIDs;
            coroutineIDs.reserve(m_coroutines.size());
            for (const auto& [coroutineID, _] : m_coroutines)
            {
                coroutineIDs.push_back(coroutineID);
            }
            for (const auto coroutineID : coroutineIDs)
            {
                auto found = m_coroutines.find(coroutineID);
                if (found != m_coroutines.end())
                {
                    CancelCoroutine(coroutineID);
                }
            }
        }

        std::size_t RunningCount() const
        {
            return m_coroutines.size();
        }

        void SetExceptionHandler(CCoroutineRuntime::ExceptionHandler handler_)
        {
            m_exceptionHandler = std::move(handler_);
        }

        void SuspendUntilNextFrame(std::uint64_t coroutineID_) override
        {
            auto* record = FindCoroutine(coroutineID_);
            if (!record)
            {
                return;
            }
            record->Predicate = {};
            record->WaitState = EWaitState::Ready;
            record->EarliestFrame = m_frameIndex + 1;
        }

        void SuspendForSeconds(
            std::uint64_t coroutineID_,
            double seconds_) override
        {
            if (!std::isfinite(seconds_) || seconds_ < 0.0)
            {
                throw std::invalid_argument("Coroutine wait duration must be finite and non-negative");
            }
            auto* record = FindCoroutine(coroutineID_);
            if (!record)
            {
                return;
            }
            record->Predicate = {};
            record->WaitState = EWaitState::WaitingForTime;
            record->WakeTime = m_totalTime + seconds_;
            record->EarliestFrame = m_frameIndex + 1;
        }

        void SuspendUntil(
            std::uint64_t coroutineID_,
            std::function<bool()> predicate_) override
        {
            if (!predicate_)
            {
                throw std::invalid_argument("Coroutine wait predicate cannot be empty");
            }
            auto* record = FindCoroutine(coroutineID_);
            if (!record)
            {
                return;
            }
            record->Predicate = std::move(predicate_);
            record->WaitState = EWaitState::WaitingForPredicate;
            record->EarliestFrame = m_frameIndex + 1;
        }

        void SuspendForTask(std::uint64_t coroutineID_) override
        {
            auto* record = FindCoroutine(coroutineID_);
            if (!record)
            {
                return;
            }
            record->Predicate = {};
            record->WaitState = EWaitState::WaitingForTask;
        }

        void Wake(std::uint64_t coroutineID_) override
        {
            auto* record = FindCoroutine(coroutineID_);
            if (!record || record->WaitState != EWaitState::WaitingForTask)
            {
                return;
            }
            record->WaitState = EWaitState::Ready;
            record->EarliestFrame = m_frameIndex + (m_isTicking ? 1 : 0);
            record->StatusState->Status = record->Paused
                ? ECoroutineStatus::Paused
                : ECoroutineStatus::Scheduled;
        }

    private:
        static ECoroutineStatus StatusForWaitState(EWaitState waitState_) noexcept
        {
            switch (waitState_)
            {
            case EWaitState::Ready:
                return ECoroutineStatus::Scheduled;
            case EWaitState::Running:
                return ECoroutineStatus::Running;
            case EWaitState::CancelRequested:
                return ECoroutineStatus::Canceled;
            default:
                return ECoroutineStatus::Waiting;
            }
        }

        bool Owns(const CCoroutineHandleBase& handle_) const
        {
            auto runtime = handle_.m_runtime.lock();
            return runtime && runtime.get() == this;
        }

        CCoroutineRecord* FindCoroutine(std::uint64_t coroutineID_)
        {
            auto found = m_coroutines.find(coroutineID_);
            if (found == m_coroutines.end())
            {
                return nullptr;
            }
            return found->second.get();
        }

        const CCoroutineRecord* FindCoroutine(std::uint64_t coroutineID_) const
        {
            auto found = m_coroutines.find(coroutineID_);
            if (found == m_coroutines.end())
            {
                return nullptr;
            }
            return found->second.get();
        }

        CCoroutineRecord* FindCoroutine(const CCoroutineHandleBase& handle_)
        {
            return Owns(handle_) ? FindCoroutine(handle_.m_id) : nullptr;
        }

        const CCoroutineRecord* FindCoroutine(const CCoroutineHandleBase& handle_) const
        {
            return Owns(handle_) ? FindCoroutine(handle_.m_id) : nullptr;
        }

        bool CanRun(const CCoroutineRecord& record_) const
        {
            return !record_.Paused && record_.WaitState != EWaitState::CancelRequested;
        }

        void CancelCoroutine(std::uint64_t coroutineID_)
        {
            auto* record = FindCoroutine(coroutineID_);
            if (!record)
            {
                return;
            }
            if (m_currentCoroutineID == coroutineID_)
            {
                record->WaitState = EWaitState::CancelRequested;
                record->StatusState->Status = ECoroutineStatus::Canceled;
                return;
            }
            FinishCanceled(coroutineID_);
        }

        void FinishCompleted(std::uint64_t coroutineID_)
        {
            Finish(coroutineID_, ECoroutineStatus::Completed, nullptr);
        }

        void FinishCanceled(std::uint64_t coroutineID_)
        {
            Finish(coroutineID_, ECoroutineStatus::Canceled, nullptr);
        }

        void FinishFaulted(std::uint64_t coroutineID_, std::exception_ptr exception_)
        {
            Finish(coroutineID_, ECoroutineStatus::Faulted, exception_);
        }

        void Finish(
            std::uint64_t coroutineID_,
            ECoroutineStatus status_,
            std::exception_ptr exception_)
        {
            auto found = m_coroutines.find(coroutineID_);
            if (found == m_coroutines.end())
            {
                return;
            }

            auto record = std::move(found->second);
            const CCoroutineHandleBase publicHandle(
                coroutineID_,
                record->StatusState,
                std::weak_ptr<ICoroutineRuntime>(shared_from_this()));
            m_coroutines.erase(found);

            record->StatusState->Status = status_;
            if (status_ == ECoroutineStatus::Completed)
            {
                if (auto completionException = record->Frame.Complete())
                {
                    status_ = ECoroutineStatus::Faulted;
                    exception_ = completionException;
                    record->StatusState->Status = status_;
                }
            }
            else if (status_ == ECoroutineStatus::Canceled)
            {
                record->Frame.Cancel();
            }
            else
            {
                record->Frame.Fault(exception_);
            }

            if (status_ == ECoroutineStatus::Faulted && m_exceptionHandler)
            {
                try
                {
                    m_exceptionHandler(publicHandle, exception_);
                }
                catch (...)
                {
                }
            }

        }

    private:
        TaskSchedulerPtr m_scheduler;
        std::unordered_map<std::uint64_t, std::unique_ptr<CCoroutineRecord>> m_coroutines;
        std::uint64_t m_nextCoroutineID = 1;
        std::uint64_t m_frameIndex = 0;
        std::uint64_t m_currentCoroutineID = 0;
        double m_totalTime = 0.0;
        bool m_isTicking = false;
        CCoroutineRuntime::ExceptionHandler m_exceptionHandler;
    };
}

std::suspend_always iCAX::Coroutines::detail::CCoroutinePromiseBase::initial_suspend() const noexcept
{
    return {};
}

std::suspend_always iCAX::Coroutines::detail::CCoroutinePromiseBase::final_suspend() const noexcept
{
    return {};
}

void iCAX::Coroutines::detail::CCoroutinePromiseBase::unhandled_exception() noexcept
{
    m_exception = std::current_exception();
}

std::exception_ptr iCAX::Coroutines::detail::CCoroutinePromiseBase::Exception() const noexcept
{
    return m_exception;
}

std::shared_ptr<iCAX::Coroutines::detail::ICoroutineRuntime>
iCAX::Coroutines::detail::CCoroutinePromiseBase::Runtime() const
{
    auto runtime = m_runtime.lock();
    if (!runtime)
    {
        throw std::logic_error("Coroutine is not attached to an active runtime");
    }
    return runtime;
}

std::uint64_t iCAX::Coroutines::detail::CCoroutinePromiseBase::CoroutineID() const noexcept
{
    return m_coroutineID;
}

void iCAX::Coroutines::detail::CCoroutinePromiseBase::Bind(
    std::weak_ptr<ICoroutineRuntime> runtime_,
    std::uint64_t coroutineID_) noexcept
{
    m_runtime = std::move(runtime_);
    m_coroutineID = coroutineID_;
}

iCAX::Coroutines::CCoroutineHandleBase::CCoroutineHandleBase(
    std::uint64_t id_,
    std::shared_ptr<detail::CCoroutineStatusState> statusState_,
    std::weak_ptr<detail::ICoroutineRuntime> runtime_)
    : m_id(id_)
    , m_statusState(std::move(statusState_))
    , m_runtime(std::move(runtime_))
{
}

bool iCAX::Coroutines::CCoroutineHandleBase::IsValid() const noexcept
{
    return m_id != 0 && m_statusState != nullptr;
}

iCAX::Coroutines::CCoroutineHandleBase::operator bool() const noexcept
{
    return IsValid();
}

std::uint64_t iCAX::Coroutines::CCoroutineHandleBase::ID() const noexcept
{
    return m_id;
}

bool iCAX::Coroutines::CNextFrameAwaitable::await_ready() const noexcept
{
    return false;
}

void iCAX::Coroutines::CNextFrameAwaitable::await_resume() const noexcept
{
}

iCAX::Coroutines::CWaitForSecondsAwaitable::CWaitForSecondsAwaitable(double seconds_) noexcept
    : m_seconds(seconds_)
{
}

bool iCAX::Coroutines::CWaitForSecondsAwaitable::await_ready() const noexcept
{
    return false;
}

void iCAX::Coroutines::CWaitForSecondsAwaitable::await_resume() const noexcept
{
}

iCAX::Coroutines::CWaitUntilAwaitable::CWaitUntilAwaitable(
    std::function<bool()> predicate_)
    : m_predicate(std::move(predicate_))
{
    if (!m_predicate)
    {
        throw std::invalid_argument("Coroutine wait predicate cannot be empty");
    }
}

bool iCAX::Coroutines::CWaitUntilAwaitable::await_ready() const
{
    return false;
}

void iCAX::Coroutines::CWaitUntilAwaitable::await_resume() const noexcept
{
}

iCAX::Coroutines::CCoroutineRuntime::CCoroutineRuntime(TaskSchedulerPtr scheduler_)
    : m_runtime(std::make_shared<detail::CCoroutineRuntimeState>(std::move(scheduler_)))
{
}

iCAX::Coroutines::CCoroutineRuntime::~CCoroutineRuntime()
{
    if (m_runtime)
    {
        std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->CancelAll();
    }
}

iCAX::Coroutines::detail::CCoroutineStartInfo
iCAX::Coroutines::CCoroutineRuntime::StartErased(detail::CCoroutineFrame frame_)
{
    return std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Start(
        std::move(frame_));
}

void iCAX::Coroutines::CCoroutineRuntime::Pause(const CCoroutineHandleBase& handle_)
{
    std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Pause(handle_);
}

void iCAX::Coroutines::CCoroutineRuntime::Resume(const CCoroutineHandleBase& handle_)
{
    std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Resume(handle_);
}

void iCAX::Coroutines::CCoroutineRuntime::Cancel(const CCoroutineHandleBase& handle_)
{
    std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Cancel(handle_);
}

bool iCAX::Coroutines::CCoroutineRuntime::IsRunning(const CCoroutineHandleBase& handle_) const
{
    return std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->IsRunning(handle_);
}

iCAX::Coroutines::ECoroutineStatus iCAX::Coroutines::CCoroutineRuntime::Status(
    const CCoroutineHandleBase& handle_) const
{
    return std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Status(handle_);
}

std::size_t iCAX::Coroutines::CCoroutineRuntime::Tick(double deltaTime_, double totalTime_)
{
    return std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->Tick(
        deltaTime_,
        totalTime_);
}

void iCAX::Coroutines::CCoroutineRuntime::CancelAll()
{
    std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->CancelAll();
}

std::size_t iCAX::Coroutines::CCoroutineRuntime::RunningCount() const
{
    return std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->RunningCount();
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Coroutines::CCoroutineRuntime::Scheduler() const
{
    return m_runtime->Scheduler();
}

void iCAX::Coroutines::CCoroutineRuntime::SetExceptionHandler(ExceptionHandler handler_)
{
    std::static_pointer_cast<detail::CCoroutineRuntimeState>(m_runtime)->SetExceptionHandler(
        std::move(handler_));
}
