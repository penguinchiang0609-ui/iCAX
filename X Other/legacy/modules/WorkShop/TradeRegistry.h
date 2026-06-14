#pragma once

#include "pch.h"
#include <vector>
#include <memory>
#include "Setting.h"
#include "Trade.h"
#include <tuple>
#include "../../01 Foundation/Data/uuid.h"
#include <map>
#include <thread>

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工种注册表
        * @remark
        *   1、注册和注销应仅在程序开始或程序结束时发生，在程序运行过程中尽量少修改工种注册表
        */
        class CTradeRegistry final
        {
        public:
            /*
            * @brief 构造函数
            */
            CTradeRegistry();

            /*
            * @brief 析构函数
            */
            ~CTradeRegistry();

        public:
            /*
            * @brief 注册工种
            * @param [in] UID_ 工种配置
            * @param [in] nCount_ 工种配置
            * @param [in] pfunCreator_ 工种配置
            * @return bool 操作结果
            */
            bool Regist(IN const iCAX::Data::uuid& UID_, IN const int nCount_, IN std::function<std::shared_ptr<CWorkerBase>()> pfunCreator_);

            /*
            * @brief 注销工种
            * @param [in] UID_ 工种配置
            * @return bool 操作结果
            */
            bool Deregist(IN const iCAX::Data::uuid& UID_);

            /*
            * @brief 是否包含指定工种
            * @param [in] nID_
            * @return bool
            */
            bool Has(IN const iCAX::Data::uuid& nID_) const;

            /*
            * @brief 获取工人
            * @param [in] nID_
            * @param [out] pWorker_
            * @return bool
            */
            bool TryFetchWorker(IN const iCAX::Data::uuid& nID_, OUT std::shared_ptr<CWorkerBase>& pWorker_) const;

            /*
            * @brief 归还工人
            * @param [in] pWorker_
            */
            bool ReturnWorker(IN std::shared_ptr<CWorkerBase>& pWorker_) const;

            /*
            * @brief 获取工种索引列表
            * @return std::vector<iCAX::Data::uuid>
            */
            std::vector<iCAX::Data::uuid> GetTradeIndexes() const;

        private:
            std::map<iCAX::Data::uuid, std::shared_ptr<Trade>> m_mapTrade;                        //!< 工种
            std::thread::id m_MainThreadID;
        };
    }
}
