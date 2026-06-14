#pragma once
#include "pch.h"
#include <chrono>
#include <optional>
#include <map>
#include "WorkFlowGraph.h"
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 加工统计
        */
        class CProcessingStatics final
        {
        public:
            /*
            * @brief 工序路线节点状态
            */
            enum RouteNodeState
            {
                kJoin = 0,                                              //!< 阻塞，等待前置工序
                kReady,                                                 //!< 就绪，依赖的前置工序都已完成
                kBusy,                                                  //!< 处理中
                kSuccess,                                               //!< 成功
                kFault,                                                 //!< 失败
                //!< 此处不提供取消，如C依赖A和B，其中A失败了，C会等到B执行出结果再进行下一步调度。
                //!< 此处不进行取消操作是因为异步取消，风险比较高
            };

            /*
            * @brief 详细信息
            */
            struct DetailInfo final
            {
                int nErrorCode;                                         //!< 异常编码，0 表示成功，负数为系统内置的错误码，-1表示工种不存在，-2 表示工种遇到未处理异常，-3表示图纸不存在，-4表示图纸执行出错。正数表示自定义的工种内部错误。
                //!< 此处记录开始时间与结束时间的原因是：
                //!<     1、便于统计性能，以便改进
                //!<     2、不采用耗时的字段是因为，不希望为了该信息对于实际生产产生太多的影响。当需要统计耗时时，再为此买单
                std::chrono::system_clock::time_point StartTime;        //!< 开始时间
                std::chrono::system_clock::time_point EndTime;          //!< 结束时间

                std::shared_ptr<CProcessingStatics> pSubStatics;        //!< 当节点是图纸时，此字段存储图纸的工作信息
                std::exception Exception;                               //!< worker存在未处理的异常，原始异常信息
                /*
                * @brief 计算耗时
                * @return std::chrono::milliseconds
                */
                std::chrono::milliseconds Duration() const;
            };

        private:
            /// <summary>
            /// 工艺节点
            /// </summary>
            struct Node final
            {
                int nNodeIndex;                                         //!< 节点索引
                std::vector<int> vecPreAdjacency;                       //!< 前邻接工序节点索引列表
                std::vector<int> vecPostAdjacency;                      //!< 后邻接工序节点索引列表
                volatile std::atomic<int> nDegree;                      //!< 尚未完成的依赖工序数
                volatile std::atomic<RouteNodeState> nState;            //!< 节点当前状态
                DetailInfo Detail;                                      //!< 详细信息
            };

        public:
            /*
            * @brief 初始化
            * @param [in] Graph_
            */
            void Initialize(IN const CWorkFlowGraph& Graph_);

            /*
            * @brief 获取图纸ID
            * @return iCAX::Data::uuid
            */
            iCAX::Data::uuid GetGraphID();

            /*
            * @brief 筛选指定状态的索引
            * @param [in] State_ 状态
            * @return std::vector<int>
            */ 
            std::vector<int> FilterIndexesByStates(IN const RouteNodeState& State_);

            /*
            * @brief 更新指定索引的状态
            * @param [in] nIndex_
            * @param [in] State_
            */
            void UpdateState(IN const int& nIndex_, IN const RouteNodeState& State_);

            /*
            * @brief 报告错误信息
            * @param [in] nIndex_
            * @param [in] nErrorCode_
            * @param [in] Exception_
            * @param [in] pSubStatics_
            * @remark 此处只是上报详细信息，其仅起到登记在册的作用，如果是更新节点状态，请使用UpdateState
            */
            void ReportDetail(IN const int& nIndex_, IN const int& nErrorCode_, std::exception Exception_, std::shared_ptr<CProcessingStatics> pSubStatics_ = nullptr);

            /*
            * @brief 报告开始时间
            * @param [in] nIndex_
            */
            void ReportStartTime(IN const int& nIndex_);

            /*
            * @brief 报告结束时间
            * @param [in] nIndex_
            */
            void ReportEndTime(IN const int& nIndex_);

            /*
            * @brief 获取是否完成
            * @return bool
            */
            bool IsCompleted() const;

            /*
            * @brief 获取是否失败
            * @return bool
            */
            bool IsFault() const;

        private:
            /*
            * @brief 入度自减
            * @param [in] nIndex_ 工序节点索引
            */
            void DecrementDegree(IN const int& nIndex_);

        private:
            iCAX::Data::uuid m_GraphID;                                   //!< 图纸索引
            std::map<int, Node> m_Graph;                                    //!< 无环有向图

            mutable int m_nNodeNotCompleted;                                //!< 还未完成的节点数 
            mutable bool m_bFaultAnyNode;                                   //!< 是否有工序出错
        };
    }
}