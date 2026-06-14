#pragma once
#include <boost/asio.hpp>
#include <memory>
#include "Threading.h"

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 进度
        */
        template<typename T>
        class CProgress final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] Handler_
            */
            explicit CProgress(IN std::function<void(const T&)> Handler_) 
                : m_Handler(std::move(Handler_))
            {
            }

            /*
            * @brief 析构函数
            */
            ~CProgress() = default;

        public:
            /*
            * @brief 上报
            */
            void Report(IN const T& value) 
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (m_Handler)
                {
                    m_Handler(value);
                }
            }

        private:
            std::function<void(const T&)> m_Handler;                    //!< 上报行为
            std::mutex m_Mutex;                                         //!< 锁
        };
    }
}