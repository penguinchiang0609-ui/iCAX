#pragma once

#include "pch.h"
#include <map>
#include <vector>
#include "WorkFlowGraph.h"
#include <memory>
#include <tuple>
#include "../../01 Foundation/Data/uuid.h"
#include <thread>


namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工序图纸注册表
        * @remark
        *   1、负责管理所有工序图纸
        *   2、单例，一个app只需要一个工序图纸仓储
        */
        class CWorkFlowRegistry final
        {
        public:
            /*
            * @brief 构造函数
            */
            CWorkFlowRegistry();

            /*
            * @brief 析构函数
            */
            ~CWorkFlowRegistry();

        public:
            /*
            * @brief 注册工序图
            * @param [in] pRoute_
            * @return bool 操作结果
            */
            bool Regist(IN std::shared_ptr<CWorkFlowGraph>& pRoute_);

            /*
            * @brief 注销工序图
            * @param [in] pRoute_
            * @return bool 操作结果
            */
            bool Deregist(IN std::shared_ptr<CWorkFlowGraph>& pRoute_);

            /*
            * @brief 获取工序图
            * @param [in] UID_
            * @return std::shared_ptr<CWorkFlowGraph>&
            */
            std::shared_ptr<CWorkFlowGraph> GetGraph(IN const iCAX::Data::uuid& UID_) const;
        private:
           std::map<iCAX::Data::uuid, std::shared_ptr<CWorkFlowGraph>> m_mapRoutePtr;                //!< 图纸库
            std::thread::id m_MainThreadID;
        };
    }
}
