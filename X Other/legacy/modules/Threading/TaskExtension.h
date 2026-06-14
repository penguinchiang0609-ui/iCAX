#pragma once

#include "Threading.h"
#include "Task.h"
#include "Progress.h"
#include "CancellationToken.h"

namespace iCAX
{
    namespace Threading
    {
        /*
        * @brief 启动新任务
        */
        template<typename Func, typename ResultType = std::invoke_result_t<Func>>
        CTask<ResultType> StartNew(IN Func&& func, IN CCancellationToken token, IN CProgress<int> progress = {}) 
        {
            CTaskCompletionSource<ResultType> tcs;

            CTaskScheduler::Instance().Schedule([func = std::forward<Func>(func), tcs, token, progress]() mutable 
                {
                    try 
                    {
                        if (token.IsCanceled()) 
                        {
                            tcs.SetCanceled();
                            return;
                        }
                        auto result = func();
                        tcs.SetResult(std::move(result));
                    }
                    catch (...) 
                    {
                        tcs.SetException(std::current_exception());
                    }
                });

            return tcs.GetTask();
        }

        template<typename T>
        CTask<std::vector<T>> WhenAll(IN const std::vector<CTask<T>>& tasks) 
        {
            CTaskCompletionSource<std::vector<T>> tcs;
            auto results = std::make_shared<std::vector<T>>(tasks.size());
            auto remaining = std::make_shared<std::atomic<size_t>>(tasks.size());

            for (size_t i = 0; i < tasks.size(); ++i) 
            {
                tasks[i].ContinueWith([i, tcs, results, remaining](const Task<T>& t) mutable 
                {
                    try 
                    {
                        (*results)[i] = t.GetResult();
                    }
                    catch (...) 
                    {
                        tcs.SetException(std::current_exception());
                        return;
                    }
                    if (--(*remaining) == 0)
                    {
                        tcs.SetResult(*results);
                    }
                });
            }

            return tcs.GetTask();
        }

        /*
        * @brief 任意完成
        * @param [in] tasks
        * @return CTask<CTask<T>>
        */
        template<typename T>
        CTask<CTask<T>> WhenAny(IN const std::vector<CTask<T>>& tasks)
        {
            CTaskCompletionSource<CTask<T>> tcs;
            auto triggered = std::make_shared<std::atomic<bool>>(false);

            for (const auto& task : tasks)
            {
                task.ContinueWith([tcs, task, triggered](const Task<T>& t) mutable
                {
                    if (!triggered->exchange(true))
                    {
                        tcs.SetResult(task);
                    }
                });
            }

            return tcs.GetTask();
        }

        /*
        * @brief 展开
        * @param [in] IN CTask<CTask<T>> outer
        * @return CTask<T>
        */
        template<typename T>
        CTask<T> Unwrap(IN CTask<CTask<T>> outer) 
        {
            CTaskCompletionSource<T> tcs;

            outer.ContinueWith([tcs](const CTask<CTask<T>>& outerTask) mutable
                {
                    try 
                    {
                        if (outerTask.IsCanceled()) 
                        {
                            tcs.SetCanceled();
                            return;
                        }
                        if (outerTask.IsFaulted()) 
                        {
                            tcs.SetException(outerTask.GetResult().GetException());
                            return;
                        }

                        CTask<T> inner = outerTask.GetResult();
                        inner.ContinueWith([tcs](const CTask<T>& t) mutable 
                            {
                                if (t.IsCanceled()) tcs.SetCanceled();
                                else if (t.IsFaulted()) tcs.SetException(t.GetException());
                                else tcs.SetResult(t.GetResult());
                            });
                    }
                    catch (...) 
                    {
                        tcs.SetException(std::current_exception());
                    }
                });

            return tcs.GetTask();
        }
    }
}

//CTask<CTask<int>> nested = WhenAny(...);
//CTask<int> flat = Unwrap(nested);

//!< 简单任务 + 链式调用
//void Example1() {
//    auto task = StartNew([] 
//   {
//        std::this_thread::sleep_for(std::chrono::milliseconds(500));
//        return 42;
//    });
//
//    task.ContinueWith([](const Task<int>& t) 
//    {
//          std::cout << "Result = " << t.GetResult() << "\n";
//          return t.GetResult() * 2;
//     }).ContinueWith([](const Task<int>& t) 
//     {
//          std::cout << "Chained Result = " << t.GetResult() << "\n";
//     });
//
//     task.Wait(); // 等待主任务完成（非必须）
//   }

//!< 取消任务
//void Example2() {
//    CancellationTokenSource cts;
//    auto token = cts.GetToken();
//
//    auto task = StartNew([=] {
//        for (int i = 0; i < 10; ++i) {
//            if (token.IsCanceled()) {
//                std::cout << "kTaskCanceled\n";
//                return -1;
//            }
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//        }
//        return 123;
//        }, token);
//
//    // 模拟提前取消
//    std::this_thread::sleep_for(std::chrono::milliseconds(300));
//    cts.Cancel();
//
//    try {
//        task.Wait();
//        std::cout << "Result: " << task.GetResult() << "\n";
//    }
//    catch (const std::exception& e) {
//        std::cout << "Exception: " << e.what() << "\n";
//    }
//}

//!< 进度报告
//void Example3() {
//    Progress<int> progress([](int percent) {
//        std::cout << "Progress: " << percent << "%\n";
//        });
//
//    auto task = StartNew([=] {
//        for (int i = 0; i <= 10; ++i) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(100));
//            progress.Report(i * 10);
//        }
//        return true;
//        }, CCancellationToken::None(), progress);
//
//    task.Wait();
//    std::cout << "Task kTaskCompleted: " << task.GetResult() << "\n";
//}