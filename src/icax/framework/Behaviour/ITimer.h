#pragma once
#include <chrono>
#include "System.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 时间服务
        * @remark Timer 由 UniverseContext 持有，用于行为系统获取帧间隔和累计时间。
        */
        class _SYSTEM_EXP ITimer
        {
        public:
            /*
            * @brief 构造函数
            */
            ITimer() = default;

            /*
            * @brief 析构函数
            */
            virtual ~ITimer() = default;

        public:
            /*
            * @brief 当前帧耗时
            * @return 距离上一帧 Tick 的秒数。
            */
            virtual double GetDeltaTime() const = 0;

            /*
            * @brief 累计运行时间
            * @return 从 Timer 创建或重置以来累计的秒数。
            */
            virtual double GetTime() const = 0;
        };
    }
} // namespace iCAX
