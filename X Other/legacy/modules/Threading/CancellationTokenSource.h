#pragma once
#include <atomic>
#include "Threading.h"

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 取消令牌源
        */
        class _THREADING_EXP CCancellationTokenSource final
        {
        public:
            /*
            * @brief 构造函数
            */
            CCancellationTokenSource();

            /*
            * @brief 析构函数
            */
            ~CCancellationTokenSource();

        public:
            /*
            * @brief 取消
            */
            void Cancel();

            /*
            * @brief 是否取消
            * @return bool
            */
            bool IsCanceled() const;

        private:
            std::atomic<bool> m_bCanceled;
        };
    }
}