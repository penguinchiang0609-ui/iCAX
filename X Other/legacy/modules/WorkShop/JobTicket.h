#pragma once
#include "pch.h"

#include <any>
#include <atomic>
#include "WorkFlowGraph.h"
#include "WorkPiece.h"
#include "ProcessingStatics.h"
#include <functional>

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工单
        */
        class CJobTicket final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] Graph_ 图纸
            * @param [in] Picece_ 工件
            */ 
            CJobTicket(IN const CWorkFlowGraph& Graph_, IN std::shared_ptr<WorkPiece> pPicece_);

            /*
            * @brief 析构函数
            */
            ~CJobTicket();

            //!< 禁用
            CJobTicket(IN const CJobTicket& Other_) = delete;
            CJobTicket& operator=(IN const CJobTicket& Other_) = delete;
            CJobTicket(IN const CJobTicket&& Other_) = delete;
            CJobTicket& operator=(IN const CJobTicket&& Other_) = delete;

        public:
            /*
            * @brief 获取工单单号
            * @return unsigned long long
            */
            unsigned long long GetID() const;

            /*
            * @brief 获取工件
            * @return std::shared_ptr<WorkPiece>
            */
            std::shared_ptr<WorkPiece> GetWorkPiece() const;

            /*
            * @brief 获取加工统计
            * @return std::shared_ptr<CProcessingStatics>
            */
            std::shared_ptr<CProcessingStatics> GetStatics() const;

        private:
            unsigned long long m_ID;                                            //!< 工单号，自增，工单的唯一标记
            std::shared_ptr<WorkPiece> m_pWorkPiece;                            //!< 工件，工件随着工序逐步加工
            std::shared_ptr<CProcessingStatics> m_pStatics;                     //!< 加工统计
        };
    }
}
