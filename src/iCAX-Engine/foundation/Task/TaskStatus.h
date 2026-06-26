#pragma once

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace iCAX::Tasks
{
    enum class TaskStatus
    {
        Created,
        Running,
        RanToCompletion,
        Faulted,
        Canceled
    };

    class TaskCanceledException final : public std::runtime_error
    {
    public:
        TaskCanceledException()
            : std::runtime_error("Task was canceled.")
        {
        }
    };

    class TaskTimeoutException final : public std::runtime_error
    {
    public:
        TaskTimeoutException()
            : std::runtime_error("Task timed out.")
        {
        }
    };

    class TaskAggregateException final : public std::runtime_error
    {
    public:
        explicit TaskAggregateException(std::vector<std::exception_ptr> exceptions_)
            : std::runtime_error(BuildMessage(exceptions_.size()))
            , m_exceptions(std::move(exceptions_))
        {
        }

        const std::vector<std::exception_ptr>& Exceptions() const noexcept
        {
            return m_exceptions;
        }

    private:
        static std::string BuildMessage(std::size_t count_)
        {
            return "One or more task exceptions occurred. Count: " + std::to_string(count_);
        }

    private:
        std::vector<std::exception_ptr> m_exceptions;
    };

    inline bool IsTerminalStatus(TaskStatus status_) noexcept
    {
        return status_ == TaskStatus::RanToCompletion ||
            status_ == TaskStatus::Faulted ||
            status_ == TaskStatus::Canceled;
    }

    inline const char* ToString(TaskStatus status_) noexcept
    {
        switch (status_)
        {
        case TaskStatus::Created:
            return "Created";
        case TaskStatus::Running:
            return "Running";
        case TaskStatus::RanToCompletion:
            return "RanToCompletion";
        case TaskStatus::Faulted:
            return "Faulted";
        case TaskStatus::Canceled:
            return "Canceled";
        default:
            return "Unknown";
        }
    }
}
