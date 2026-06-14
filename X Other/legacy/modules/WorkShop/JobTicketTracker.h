#pragma once
#include "pch.h"

#include <any>
#include <atomic>
#include <functional>
#include "JobTicket.h"
#include "TradeRegistry.h"
#include "WorkFlowRegistry.h"

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 跟单员
        * @remark 每个工单都有一个跟单员
        */
        class CJobTicketTracker final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] TradeRegistry_
            * @param [in] GraphRegistry_
            * @param [in] pTicket_
            * @param [in] funOnSuccess_
            * @param [in] funOnFault_
            */
            CJobTicketTracker(IN std::shared_ptr<CTradeRegistry> TradeRegistry_, IN std::shared_ptr<CWorkFlowRegistry>  GraphRegistry_, IN std::shared_ptr<CJobTicket> pTicket_, std::function<void(std::shared_ptr<CJobTicket>)> funOnSuccess_, std::function<void(std::shared_ptr<CJobTicket>)> funOnFault_);

            /*
            * @brief 析构函数
            */
            ~CJobTicketTracker();

        public:
            /*
            * @brief 获取工单信息
            * @return std::shared_ptr<CJobTicket>
            */
            std::shared_ptr<CJobTicket> GetTicket() const;

            /*
            * @brief 主线程循环调用
            */
            void LoopUpdate();

        private:
            std::shared_ptr<CTradeRegistry> m_pTradeRegistry;                                   //!< 工种注册表
            std::shared_ptr<CWorkFlowRegistry> m_pWorkFlowRegistry;                             //!< 图纸注册表
            std::shared_ptr<CJobTicket> m_pTicket;                                              //!< 工单
            std::shared_ptr<CWorkFlowGraph> m_pWrokFlowGraph;                                   //!< 图纸
            std::function<void(std::shared_ptr<CJobTicket>)> m_funOnSuccess;                    //!< 成功后的回调
            std::function<void(std::shared_ptr<CJobTicket>)> m_funOnFault;                      //!< 失败后的回调
            std::list<std::tuple<int, std::shared_ptr<CWorkerBase>>> m_lstWorkers;              //!< 工人列表
            std::list<std::tuple<int, std::shared_ptr<CJobTicketTracker>>> m_lstSubTrackers;    //!< 子跟单员，用于工序某个节点是另一张图纸的情形
        };
    }
}