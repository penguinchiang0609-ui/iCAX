#pragma once
#include <functional>
#include "System.h"
#include <memory>

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief 协程返回值接口
        */
        class _SYSTEM_EXP IYield
        {
        public:
            /*
            * @brief 构造函数
            */
            IYield() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IYield() = default;

        public:
            /*
            * @brief 移动到下一个动作
            * @param [in] nDeltaTime_ 帧时间差
            * @param [in] nTotalTime_ 总时间
            * @return bool false表示工作完成 true表示工作未完成
            */
            virtual bool MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_) = 0;
        };

        /*
        * @brief 延时
        */
        class _SYSTEM_EXP CYieldWaitForSeconds : public IYield
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] nSeconds_
            */
            CYieldWaitForSeconds(IN const double& nSeconds_);

            /*
            * @brief 析构函数
            */
            virtual ~CYieldWaitForSeconds();

            /*
            * @brief 移动到下一个动作
            * @param [in] nDeltaTime_ 帧时间差
            * @return bool false表示工作完成 true表示工作未完成
            */
            bool MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_) override;

        private:
            double m_Remaining;
        };

        /*
        * @brief 下一帧
        */
        class _SYSTEM_EXP CYieldFrame : public IYield
        {
        public:
            /*
            * @brief 构造函数
            */
            CYieldFrame();

            /*
            * @brief 析构函数
            */
            virtual ~CYieldFrame();

        public:
            /*
            * @brief 移动到下一个动作
            * @param [in] nDeltaTime_ 帧时间差
            * @return bool false表示工作完成 true表示工作未完成
            */
            bool MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_) override;
        };

        /*
        * @brief 条件等待
        */
        class _SYSTEM_EXP CYieldUntil : public IYield {
        public:
            /*
            * @brief 构造函数
            * @param [in] Condition_
            */
            CYieldUntil(IN std::function<bool()> Condition_);

            /*
            * @brief 析构函数
            */
            virtual ~CYieldUntil();

        public:
            /*
            * @brief 移动到下一个动作
            * @param [in] nDeltaTime_ 帧时间差
            * @return bool false表示工作完成 true表示工作未完成
            */
            bool MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_) override;

        private:
            std::function<bool()> m_Condition;
        };

        /*
        * @brief 嵌套协程等待
        */
        class _SYSTEM_EXP CYieldCoroutine : public IYield
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] Nested_
            */
            CYieldCoroutine(IN std::function<std::shared_ptr<IYield>()> Nested_);

            /*
            * @brief 析构函数
            */
            virtual ~CYieldCoroutine();

        public:
            /*
            * @brief 移动到下一个动作
            * @param [in] nDeltaTime_ 帧时间差
            * @return bool false表示工作完成 true表示工作未完成
            */
            bool MoveNext(IN const double& nDeltaTime_, IN const double& nTotalTime_) override;

        private:
            std::function<std::shared_ptr<IYield>()> m_Coroutine;
            std::shared_ptr<IYield> m_CurrentYield;
        };

        using CoroutineFunc = std::function<std::shared_ptr<IYield>()>;

        ///*
        //* @brief 协程宿主
        //*/
        //class _SYSTEM_EXP ICoroutineHost
        //{
        //public:
        //    /*
        //    * @brief 构造函数
        //    */
        //    ICoroutineHost() = default;

        //    /*
        //    * @brief 析构函数
        //    */
        //    virtual ~ICoroutineHost() = default;

        //public:
        //    /*
        //    * @brief 启动协程
        //    * @param [in] Coroutine_
        //    * @return size_t
        //    */
        //    virtual size_t Start(IN CoroutineFunc Coroutine_) = 0;

        //    /*
        //    * @brief 取消协程
        //    * @param [in] nID_
        //    */
        //    virtual void Cancel(IN const size_t nID_) = 0;

        //    /*
        //    * @brief TICK
        //    */
        //    virtual void Tick() = 0;

        //};

#define BEGIN_COROUTINE static int __step = 0; switch(__step++) { case 0:
#define YIELD(x) do { __step--; return x; case __COUNTER__:; } while(0)
#define END_COROUTINE }
    }
}