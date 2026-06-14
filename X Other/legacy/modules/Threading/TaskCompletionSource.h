#pragma once
#include "CancellationToken.h"
#include "Progress.h"
#include "Threading.h"

namespace iCAX
{
    namespace Threading
    {
        template<typename T>
        class CTask;

        template<typename T>
        class CTaskCompletionSource final
        {
        public:
            /*
            * @brief 构造函数
            */
            CTaskCompletionSource() 
                : m_pTask(std::make_shared<CTask<T>>()) 
            {
            }

            /*
            * @brief 析构函数
            */
            ~CTaskCompletionSource()
            {
            }

        public:
            /*
            * @brief 获取Task<T>
            */
            std::shared_ptr<CTask<T>> GetTask() const
            {
                return m_pTask;
            }

            /*
            * @brief 设置值
            * @param [in] Value_
            */
            void SetResult(IN const T& Value_) 
            {
                m_pTask->SetResult(Value_);
            }

            /*
            * @brief 设置异常
            * @param [in] EX_
            */
            void SetException(IN std::exception_ptr EX_)
            {
                m_pTask->SetException(EX_);
            }

            /*
            * @brief 取消
            */
            void SetCanceled() 
            {
                m_pTask->SetCanceled();
            }

        private:
            std::shared_ptr<CTask<T>> m_pTask;
        };
    }
}
//
//CCancellationToken token;
//CProgress<int> progress([](int p) {
//    std::cout << "进度: " << p << "%\n";
//    });
//
//auto task = StartNew([=]() {
//    for (int i = 0; i <= 100; i += 20) {
//        if (token.IsCanceled()) throw std::runtime_error("任务已取消");
//        progress.Report(i);
//        std::this_thread::sleep_for(std::chrono::milliseconds(100));
//    }
//    return 42;
//    }, token, progress);
//
//auto result = task.GetResult();