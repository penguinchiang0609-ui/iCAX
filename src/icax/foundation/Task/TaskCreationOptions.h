#pragma once

#include <cstdint>

namespace iCAX::Tasks
{
    enum class TaskCreationOptions : std::uint32_t
    {
        None = 0,
        LongRunning = 1u << 0,
        PreferFairness = 1u << 1,
        RunContinuationsAsynchronously = 1u << 2,
        DenyChildAttach = 1u << 3,
        HideScheduler = 1u << 4
    };

    inline TaskCreationOptions operator|(TaskCreationOptions lhs_, TaskCreationOptions rhs_) noexcept
    {
        return static_cast<TaskCreationOptions>(
            static_cast<std::uint32_t>(lhs_) | static_cast<std::uint32_t>(rhs_));
    }

    inline TaskCreationOptions operator&(TaskCreationOptions lhs_, TaskCreationOptions rhs_) noexcept
    {
        return static_cast<TaskCreationOptions>(
            static_cast<std::uint32_t>(lhs_) & static_cast<std::uint32_t>(rhs_));
    }

    inline TaskCreationOptions& operator|=(TaskCreationOptions& lhs_, TaskCreationOptions rhs_) noexcept
    {
        lhs_ = lhs_ | rhs_;
        return lhs_;
    }

    inline bool HasFlag(TaskCreationOptions options_, TaskCreationOptions flag_) noexcept
    {
        return (static_cast<std::uint32_t>(options_ & flag_) != 0);
    }
}

