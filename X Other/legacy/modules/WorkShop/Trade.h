#pragma once
#include "pch.h"
#include <memory>
#include "Worker.h"
#include "Setting.h"
#include <queue>
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工种
        * @remak 
        *   1、工人按类型分为不同的工种
        *   2、外部需要使用工人时，像工种申请工人，如果没有工人则会返回false
        *   3、外部使用完工人后需要归还
        */
        class Trade final
        {
        public:
            /*
            * @brief 初始化
            * @param [in] UID_ 工种配置
            * @param [in] nCount_ 工种配置
            * @param [in] pfunCreator_ 工种配置
            * @return bool
            */
            bool Initialize(IN const iCAX::Data::uuid& UID_, IN const int nCount_, IN std::function<std::shared_ptr<CWorkerBase>()> pfunCreator_);

            /*
            * @brief 释放
            */
            void Dispose();

            /*
            * @brief 尝试获取工人
            * @param [out] pWorker_
            * @return bool false 表示没有可用工人
            */
            bool TryFetchWorker(OUT std::shared_ptr<CWorkerBase>& pWorker_);

            /*
            * @brief 归还工人
            * @param [in] pWorker_
            * @return bool
            */
            bool ReturnWorker(IN std::shared_ptr<CWorkerBase>& pWorker_);

            /*
            * @brief 获取ID
            * @return uuid
            */
            iCAX::Data::uuid GetID() const;

        private:
            iCAX::Data::uuid m_ID;                                                    //!< 工种唯一标记
            std::queue<std::shared_ptr<CWorkerBase>> m_queueWorkerIdle;                 //!< 工人队列，此处无需考虑多线程，因为只有调度线程会访问该队列
        };
    }
}