#pragma once

#include "System.h"
#include "Coroutine.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 协程调度者
        */
        class CCoroutineDispatcher final
        {
        public:
            /*
            * @brief 构造函数
            */
            CCoroutineDispatcher();

            /*
            * @brief 析构函数
            */
            ~CCoroutineDispatcher();

        public:
            /*
            * @brief 启动协程
            * @param [in] Coroutine_
            * @return size_t
            */
            size_t Start(IN CoroutineFunc Coroutine_);

            /*
            * @brief 取消协程
            * @param [in] nID_
            */
            void Cancel(IN const size_t nID_);

            /*
            * @brief TICK
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            */
            void Tick(IN const double& nDeltaTime_, IN const double& nTotalTime_);

        private:
            struct CoroutineState
            {
                CoroutineFunc coroutine;
                std::shared_ptr<IYield> currentYield;
            };

            size_t m_NextID;
            std::unordered_map<size_t, CoroutineState> m_Coroutines;
        };
    }
}