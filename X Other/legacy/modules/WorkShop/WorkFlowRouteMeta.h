#pragma once

#include "pch.h"
#include <map>
#include <vector>


namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工序路线图，有向无环图
        * @remark 
        *   1、工序路线图初始化不再改变，可搭配不同工件重复使用
        *   2、如果两个工序互相不依赖可以并行执行，如一个进行坐标系的变换，一个进行表面修复
        *   3、基于数据的完整性原子性考虑，工序流不支持或的概念，即A依赖B或C完成的情形。（ps：houdini的SOP也未提供该机制）
        */
        class CWorkFlowRoute final
        {
        private:
            /*
            * @brief 工序节点
            */
            struct Node final
            {
                int nNodeIndex;                                 //! 工序索引
                int nTradeType;                                 //! 工种类型
                std::vector<int> vecPreAdjacency;               //! 前邻接工序节点索引列表
                std::vector<int> vecPostAdjacency;              //! 后邻接工序节点索引列表
            };

        public:
            /*
            * @brief 构造函数
            * @param [in] nNodeIndex_
            */
            CWorkFlowRoute(IN const unsigned long long nIndex_);

            /*
            * @brief 析构函数
            */
            ~CWorkFlowRoute();

        public:
            /*
            * @brief 获取下一个索引
            * @return unsigned long long
            */
            static unsigned long long GetNextIndex();

            /*
            * @brief 获取索引
            * @return unsigned long long
            */
            unsigned long long GetIndex() const;

            /*
            * @brief 追加工序
            * @param [in] nNodeIndex_ 工序索引
            * @param [in] nTradeType_ 工种类型
            * @param [in] vecPreAdjacency_ 前置工序
            */
            void Append(IN const int& nNodeIndex_, IN const int& nTradeType_, IN const std::vector<int>& vecPreAdjacency_);

            /*
            * @brief 构建
            */
            void Build();

            /*
            * @brief 获取工种类型
            * @param [in] nNodeIndex_ 工序索引
            * @return 工种类型
            */
            int GetTradeType(IN const int& nNodeIndex_) const;

            /*
            * @brief 获取后邻接工序节点
            * @param [in] nNodeIndex_ 工序索引
            * @return const std::vector<int>&
            */
            const std::vector<int>& GetPostAdjacency(IN const int& nNodeIndex_) const;

            /*
            * @brief 获取前邻接工序节点
            * @param [in] nNodeIndex_ 工序索引
            * @return const std::vector<int>&
            */
            const std::vector<int>& GetPreAdjacency(IN const int& nNodeIndex_) const;

            /*
            * @brief 获取工序索引列表
            * @return std::vector<int>
            */
            std::vector<int> GetNodeIndexes() const;

        private:
            unsigned long long m_nIndex;                //! 索引，工序流程的唯一标记
            std::map<int, Node> m_Graph;                //! 无环有向图
        };
    }
}
