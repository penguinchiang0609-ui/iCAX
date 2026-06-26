#pragma once

#include "ProjectExport.h"

#include <chrono>
#include <cstdint>

namespace iCAX
{
    namespace Project
    {
        /*
        * @brief 单帧时间信息。
        */
        struct _PROJECT_EXP CProjectFrameTime final
        {
            double DeltaTime = 0.0; //!< 当前帧距离上一帧的秒数。
            double TotalTime = 0.0; //!< Project 调度器启动或重置后的累计秒数。
        };

        /*
        * @brief Project 运行时调度器。
        * @details
        *   Project 是 Universe Tick 的驱动方，因此帧时间统计和下一帧唤醒时间也属于 Project。
        *   Universe 只消费 Project 传入的 DeltaTime/TotalTime，不拥有计时器。
        */
        class _PROJECT_EXP CProjectRuntimeScheduler final
        {
        public:
            /*
            * @brief 构造运行时调度器。
            * @param [in] nFrameIntervalMilliseconds_ 目标帧间隔，0 会被规整为 1ms。
            */
            explicit CProjectRuntimeScheduler(IN uint32_t nFrameIntervalMilliseconds_ = 16);

            /*
            * @brief 重置调度状态。
            * @param [in] nFrameIntervalMilliseconds_ 目标帧间隔，0 会被规整为 1ms。
            * @details 重置后 DeltaTime/TotalTime 清零，下一帧基准从当前时间开始。
            */
            void Reset(IN uint32_t nFrameIntervalMilliseconds_);

            /*
            * @brief 推进一帧时间。
            * @return 当前帧时间信息。
            */
            CProjectFrameTime Tick();

            /*
            * @brief 将下一帧唤醒时间向后推进一个目标帧间隔。
            */
            void AdvanceFrameDeadline();

            /*
            * @brief 如果当前线程已经明显落后目标帧时间，则丢弃积压帧。
            * @details 避免项目线程在长耗时后忙追历史时间点。
            */
            void DropBacklogIfNeeded();

            /*
            * @brief 获取下一帧唤醒时间。
            */
            std::chrono::steady_clock::time_point GetNextFrameTime() const;

            /*
            * @brief 获取目标帧间隔。
            */
            std::chrono::milliseconds GetFrameInterval() const;

            /*
            * @brief 获取最近一帧间隔秒数。
            */
            double GetDeltaTime() const;

            /*
            * @brief 获取累计运行秒数。
            */
            double GetTotalTime() const;

        private:
            using Clock = std::chrono::steady_clock;

            Clock::time_point m_LastTimePoint;
            Clock::time_point m_NextFrameTime;
            std::chrono::milliseconds m_FrameInterval;
            double m_DeltaTime = 0.0;
            double m_TotalTime = 0.0;
        };
    }
}
