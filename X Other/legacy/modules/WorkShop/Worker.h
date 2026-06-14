#pragma once

#include "pch.h"
#include <any>
#include <thread>
#include "JobTicket.h"
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工人
        * @remark
        */
        class CWorkerBase
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] ID_
            */
            CWorkerBase(IN const iCAX::Data::uuid& ID_);

            /*
            * @brif 析构函数
            */
            virtual ~CWorkerBase();

            //!< 禁用
            CWorkerBase(IN const CWorkerBase&) = delete;
            CWorkerBase& operator=(IN const CWorkerBase&) = delete;
            CWorkerBase(IN const CWorkerBase&&) = delete;
            CWorkerBase& operator=(IN const CWorkerBase&&) = delete;

        public:
            /*
            * @brief 获取工种ID
            * @return iCAX::Data::uuid
            */
            iCAX::Data::uuid GetTradeID() const;

            /*
            * @brief 初始化
            * @return bool 操作结果
            */
            bool Initialize();

            /*
            * @brief 分配
            * @param [in] pJob_
            */
            void Assign(IN std::shared_ptr<CJobTicket> pJob_);

            /*
            * @brief 处理工单
            * @return int 负数表示处理出错，其数值表示错误码， 0 表示处理完成， 1 表示还有工作需要处理
            */
            int MoveNext();

            /*
            * @brief 清理
            * @remark 任务完成后，调度者会调用该接口进行现场清理工作
            */
            void Clear();

            /*
            * @brief 释放
            * @return bool 操作结果
            */
            bool Dispose();

            /*
            * @brief 获取工单号
            * @return unsigned long long -1 表示当前无工单
            */
            unsigned long long GetJobIndex() const;

        protected:
            /*
            * @brief 当初始化时
            * @return bool
            */
            virtual bool OnInitialize() = 0;

            /*
            * @brief 移动下一帧
            * @return 负数表示处理出错，其数值表示错误码， 0 表示处理完成， 1 表示还有工作需要处理
            */
            virtual int OnMoveNext() = 0;

            /*
            * @brief 分配时
            */
            virtual void OnAssign() = 0;

            /*
            * @brief 清理时
            */
            virtual void OnClear() = 0;

            /*
            * @brief 当释放时
            * @return bool
            */
            virtual bool OnDispose() = 0;

        protected:
            iCAX::Data::uuid m_ID;                                //!< 工种唯一标记
            std::shared_ptr<CJobTicket> m_pJobTicket;               //!< 正在处理的工单
        };
    }
}