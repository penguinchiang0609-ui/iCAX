#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "Setting.h"
#include "TradeRegistry.h"
#include "WorkFlowRegistry.h"
#include "JobTicketTracker.h"

namespace iCAX
{
    namespace WorkShop
    {
        /// <summary>
        /// 工厂
        /// </summary>
        class CPlant final
        {
        public:
            /*
            * @brief 构造函数
            */
            CPlant();

            /*
            * @brief 析构函数
            */
            ~CPlant();
        public:
            /*
            * @brief 获取工种登记表
            * @return std::shared_ptr<CTradeRegistry>
            */
            std::shared_ptr<CTradeRegistry> GetTradeRegistry();

            /*
            * @brief 获取图纸登记表
            * @return std::shared_ptr<CWorkFlowRegistry>
            */
            std::shared_ptr<CWorkFlowRegistry> GetGraphRegistry();

            /*
            * @brief 调度工单
            * @param [in] pTicket_
            * @param [in] funOnSuccess_
            * @param [in] funOnFault_
            */
            void DispatchJobTicket(IN std::shared_ptr<CJobTicket> pTicket_, std::function<void(std::shared_ptr<CJobTicket>)> funOnSuccess_, std::function<void(std::shared_ptr<CJobTicket>)> funOnFault_);

            /*
            * @brief 循环检测
            */
            void LoopUpdate();

        private:
            std::shared_ptr<CTradeRegistry> m_pTradeRegistry;                               //!< 工种登记表
            std::shared_ptr<CWorkFlowRegistry> m_pGraphRegistry;                            //!< 图纸登记表
            std::map<unsigned long long, std::shared_ptr<CJobTicketTracker>> m_mapTracker;  //!< 跟单员
            std::thread::id m_MainThreadID;
        };
    }
}