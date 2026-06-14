#pragma once
#include "Core.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <typeindex>
#include "Data/uuid.h"
#include "Services/IService.h"
#include <Data/PropertyBag.h>
#include "BS_thread_pool.h"
#include <memory>
#include "ILogService.h"
#include "Services/ServicesHelper.h"

using namespace iCAX::Services;

namespace iCAX
{
    namespace Core
    {
        template<typename T>
        class Task;

        class _CORE_EXP TaskService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            TaskService(IN const std::shared_ptr<ILogService>& pLogService_);

            /*
            * @brief 析构函数
            */
            virtual ~TaskService() = default;

        public://! IService 成员
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

            /*
            * @brief 提交任务，返回 Task<T>
            */
            template<typename F, typename... Args>
            auto RunAsync(F&& f, Args&&... args)
            {
                using RetType = decltype(f(args...));
                if (!m_pool)
                {
                    m_pLogService->Error("TaskService not powered on");
                    throw std::runtime_error("TaskService not powered on");
                }
                std::shared_ptr<std::promise<RetType>> prom = std::make_shared<std::promise<RetType>>();
                std::shared_future<RetType> fut = prom->get_future().share();

                m_pool->submit_task([prom, f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable
                {
                    try
                    {
                        if constexpr (std::is_void_v<RetType>)
                        {
                            std::apply(f, tup);
                            prom->set_value();
                        }
                        else
                        {
                            prom->set_value(std::apply(f, tup));
                        }
                    }
                    catch (...)
                    {
                        prom->set_exception(std::current_exception());
                    }
                });

                return Task<RetType>(fut, this);
            }

        private:
            std::unique_ptr<BS::thread_pool<>> m_pool;//! 初始化为8哥线程的线程池
            std::shared_ptr<ILogService> m_pLogService;
            template<typename T>
            friend class Task;

        public:
            template<typename T>
            static Task<std::vector<T>> WhenAll(std::vector<Task<T>>& tasks, TaskService* svc)
            {
                using RetType = std::vector<T>;
                std::shared_ptr<std::promise<RetType>> prom = std::make_shared<std::promise<RetType>>();
                std::shared_future<RetType> fut = prom->get_future().share();

                // 提交一个任务等待所有任务完成
                svc->m_pool->submit_task([tasks, prom]() mutable
                {
                    RetType results;
                    results.reserve(tasks.size());
                    try
                    {
                        for (auto& t : tasks)
                            results.push_back(t.GetFuture().get()); // 等待每个任务
                        prom->set_value(results);
                    }
                    catch (...)
                    {
                        prom->set_exception(std::current_exception());
                    }
                });

                return Task<RetType>(fut, svc);
            }

            template<typename T>
            static Task<T> WhenAny(std::vector<Task<T>>& tasks, TaskService* svc)
            {
                auto prom = std::make_shared<std::promise<T>>();
                auto fut = prom->get_future().share();
                auto done = std::make_shared<std::atomic_bool>(false);

                for (auto& t : tasks)
                {
                    svc->m_pool->submit_task([t, prom, done]()
                    {
                        try {
                            T val = t.GetFuture().get();
                            bool expected = false;
                            if (done->compare_exchange_strong(expected, true))
                            {
                                prom->set_value(val); // 第一个完成的任务设置 promise
                            }
                        }
                        catch (...) {
                            bool expected = false;
                            if (done->compare_exchange_strong(expected, true))
                            {
                                prom->set_exception(std::current_exception());
                            }
                        }
                    });
                }

                return Task<T>(fut, svc);
            }

            AUTO_REGIST_SERVICE(TaskService, TaskService, ILogService);
        };
    }
}


//!-------------------------调用示例------------------------
//#include <iostream>
//#include <string>
//#include <thread>
//#include <chrono>
//#include "TaskService.h"
//
//using namespace iCAX::Services;
//
//int main()
//{
//    TaskService taskService;
//    taskService.OnLoad();
//
//    // 提交一个异步任务
//    auto task1 = taskService.RunAsync([]() -> int {
//        std::this_thread::sleep_for(std::chrono::milliseconds(500));
//        std::cout << "Task1 running..." << std::endl;
//        return 42;
//        });
//
//    // 链式 ContinueWith
//    auto task2 = task1.ContinueWith([](TaskResult<int> res) -> std::string {
//        if (res.success)
//        {
//            std::cout << "Task1 succeeded with value: " << res.value << std::endl;
//            return "Result: " + std::to_string(res.value * 2);
//        }
//        else
//        {
//            try { if (res.ex) std::rethrow_exception(res.ex); }
//            catch (const std::exception& e) { std::cout << "Task1 failed: " << e.what() << std::endl; }
//            return "Task1 failed";
//        }
//        });
//
//    // 再链一个 ContinueWith，处理字符串结果
//    auto task3 = task2.ContinueWith([](TaskResult<std::string> res) {
//        if (res.success)
//            std::cout << "Task2 succeeded with value: " << res.value << std::endl;
//        else
//            std::cout << "Task2 failed" << std::endl;
//        });
//
//    // 等待最终任务完成
//    task3.GetFuture().wait();
//
//    std::cout << "All tasks done!" << std::endl;
//
//    taskService.OnUnload();
//    return 0;
//}
