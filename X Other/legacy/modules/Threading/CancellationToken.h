#pragma once
#include <atomic>
#include <memory>
#include "CancellationTokenSource.h"
#include "Threading.h"

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 取消令牌
        */
        class _THREADING_EXP CCancellationToken final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pSource_
            */
            CCancellationToken(IN std::shared_ptr<CCancellationTokenSource> pSource_);

            /*
            * @brief 析构函数
            */
            ~CCancellationToken();

        public:
            /*
            * @brief 是否取消
            * @return bool
            */
            bool IsCanceled() const;

        public:
            /*
            * @brief 获取空令牌
            */
            static CCancellationToken None();

        private:
            std::shared_ptr<CCancellationTokenSource> m_pSource;
        };
    }
}