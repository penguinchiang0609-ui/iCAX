#pragma once
#include "System.h"
#include <memory>
#include <functional>

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 延时重复调度者
        */
        class CRepeatDispatcher final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] Timer_
            */
            CRepeatDispatcher();

            /*
            * @brief 析构函数
            */
            ~CRepeatDispatcher();

        public:
            /*
            * @brief 重复定时器
            * @param [in] Callback_
            * @param [in] nDelay_ 首次延时，如果为0则表示第一次立马执行
            * @param [in] nInterval_ 时间间隔
            * @return size_t
            */
            size_t Repeat(IN std::function<bool()> Callback_, IN const double& nDelay_, IN const double& nInterval_);

            /*
            * @brief 取消任务
            * @param [in] nID_
            */
            void Cancel(IN const size_t& nID_);

            /*
            * @brief TICK
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            void Tick(IN const double& nDeltaTime_, IN const double& nTotalTime_);

        private:
            struct Task
            {
                double nTimeRemaining = 0.0;
                double nInterval = 0.0;
                bool bRepeat = false;
                std::function<bool()> Callback;
            };

            std::unordered_map<size_t, Task> m_Tasks;
            size_t m_NextID = 1;
        };
    }
} // namespace iCAX