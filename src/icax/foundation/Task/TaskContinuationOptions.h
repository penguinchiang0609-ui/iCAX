#pragma once

#include "TaskStatus.h"

#include <cstdint>

namespace iCAX::Tasks
{
    enum class TaskContinuationOptions : std::uint32_t
    {
        None = 0,
        OnlyOnRanToCompletion = 1u << 0,
        OnlyOnFaulted = 1u << 1,
        OnlyOnCanceled = 1u << 2,
        NotOnRanToCompletion = 1u << 3,
        NotOnFaulted = 1u << 4,
        NotOnCanceled = 1u << 5,
        ExecuteSynchronously = 1u << 6,
        RunContinuationsAsynchronously = 1u << 7,
        LazyCancellation = 1u << 8,
        DenyChildAttach = 1u << 9,
        HideScheduler = 1u << 10
    };

    inline TaskContinuationOptions operator|(TaskContinuationOptions lhs_, TaskContinuationOptions rhs_) noexcept
    {
        return static_cast<TaskContinuationOptions>(
            static_cast<std::uint32_t>(lhs_) | static_cast<std::uint32_t>(rhs_));
    }

    inline TaskContinuationOptions operator&(TaskContinuationOptions lhs_, TaskContinuationOptions rhs_) noexcept
    {
        return static_cast<TaskContinuationOptions>(
            static_cast<std::uint32_t>(lhs_) & static_cast<std::uint32_t>(rhs_));
    }

    inline TaskContinuationOptions& operator|=(TaskContinuationOptions& lhs_, TaskContinuationOptions rhs_) noexcept
    {
        lhs_ = lhs_ | rhs_;
        return lhs_;
    }

    inline bool HasFlag(TaskContinuationOptions options_, TaskContinuationOptions flag_) noexcept
    {
        return (static_cast<std::uint32_t>(options_ & flag_) != 0);
    }

    inline bool ShouldRunContinuation(TaskStatus status_, TaskContinuationOptions options_) noexcept
    {
        const bool hasOnly =
            HasFlag(options_, TaskContinuationOptions::OnlyOnRanToCompletion) ||
            HasFlag(options_, TaskContinuationOptions::OnlyOnFaulted) ||
            HasFlag(options_, TaskContinuationOptions::OnlyOnCanceled);

        if (hasOnly)
        {
            return
                (status_ == TaskStatus::RanToCompletion &&
                    HasFlag(options_, TaskContinuationOptions::OnlyOnRanToCompletion)) ||
                (status_ == TaskStatus::Faulted &&
                    HasFlag(options_, TaskContinuationOptions::OnlyOnFaulted)) ||
                (status_ == TaskStatus::Canceled &&
                    HasFlag(options_, TaskContinuationOptions::OnlyOnCanceled));
        }

        if (status_ == TaskStatus::RanToCompletion &&
            HasFlag(options_, TaskContinuationOptions::NotOnRanToCompletion))
        {
            return false;
        }

        if (status_ == TaskStatus::Faulted &&
            HasFlag(options_, TaskContinuationOptions::NotOnFaulted))
        {
            return false;
        }

        if (status_ == TaskStatus::Canceled &&
            HasFlag(options_, TaskContinuationOptions::NotOnCanceled))
        {
            return false;
        }

        return true;
    }
}
