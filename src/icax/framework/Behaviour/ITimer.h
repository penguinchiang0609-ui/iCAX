#pragma once
#include <chrono>
#include "System.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 时间服务
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
            * @return double
            */
            virtual double GetDeltaTime() const = 0;

            /*
            * @brief 累计运行时间
            * @return double
            */
            virtual double GetTime() const = 0;
        };
    }
} // namespace iCAX