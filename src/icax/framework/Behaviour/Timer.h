#pragma once
#include "ITimer.h"
#include <chrono>

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 时间服务
        * @remark 使用 steady_clock 计算帧间隔，避免系统时间调整影响行为 Tick。
        */
        class CTimer final : public ITimer
        {
        public:
            /*
            * @brief 构造函数
            */
            CTimer();

            /*
            * @brief 析构函数
            */
            virtual ~CTimer();

        public:
            /*
            * @brief 每帧调用
            * @details 更新 DeltaTime 和 TotalTime，通常由 Universe::Tick 调用。
            */
            void Tick();

            /*
            * @brief 当前帧耗时（可缩放）
            * @return double
            */
            virtual double GetDeltaTime() const override;

            /*
            * @brief 累计运行时间（可缩放）
            * @return double
            */
            virtual double GetTime() const override;

        private:
            using Clock = std::chrono::steady_clock;

            Clock::time_point m_LastTimePoint;
            double m_DeltaTime = 0.0;
            double m_TotalTime = 0.0;
        };
    }

} // namespace iCAX
