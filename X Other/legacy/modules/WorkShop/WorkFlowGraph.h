#pragma once

#include "pch.h"
#include <map>
#include <vector>
#include <string>
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace WorkShop
    {
        /*
        * @brief 工序图纸，有向无环图
        * @remark 
        *   1、工序路线图初始化不再改变，可搭配不同工件重复使用
        *   2、如果两个工序互相不依赖可以并行执行，如一个进行坐标系的变换，一个进行表面修复
        *   3、基于数据的完整性原子性考虑，工序流不支持或的概念，即A依赖B或C完成的情形。（ps：houdini的SOP也未提供该机制）
        */
        class CWorkFlowGraph final
        {
        private:
            /*
            * @brief 工序节点
            */
            struct Node final
            {
                int nNodeIndex;                                 //!< 工序索引，此处不使用uuid，因为此处索引是在图纸内的id，很容易保证局部唯一。而且int也更方便记忆与理解。
                bool bIsComplex;                                //!< 复合，即为另一个工序图纸
                iCAX::Data::uuid nTradeOrGraphID;             //!< 工种ID或复合图纸ID，如果bIsComplex取值为true，则此处表示图纸ID
                std::vector<int> vecPreAdjacency;               //!< 前邻接工序节点索引列表
                std::vector<int> vecPostAdjacency;              //!< 后邻接工序节点索引列表
            };

        public:
            /*
            * @brief 构造函数
            * @param [in] UID_ 图纸索引
            */
            CWorkFlowGraph(IN const iCAX::Data::uuid& UID_);

            /*
            * @brief 析构函数
            */
            ~CWorkFlowGraph();

        public:
            /*
            * @brief 获取名称
            * @return std::string
            */
            std::string& GetNameRef();

            /*
            * @brief 获取名称
            * @return std::string
            */
            const std::string& GetNameRef() const;

            /*
            * @brief 获取备注
            * @return std::string
            */
            std::string& GetRemarkRef();

            /*
            * @brief 获取备注
            * @return std::string
            */
            const std::string& GetRemarkRef() const;

            /*
            * @brief 获取类别
            * @return std::string
            */
            std::string& GetCatalogRef();

            /*
            * @brief 获取类别
            * @return std::string
            */
            const std::string& GetCatalogRef() const;

            /*
            * @brief 获取索引
            * @return iCAX::Data::uuid
            */
            iCAX::Data::uuid GetID() const;

            /*
            * @brief 追加工序
            * @param [in] nNodeIndex_ 工序索引
            * @param [in] nTradeIndex_ 工种索引
            * @param [in] vecPreAdjacency_ 前置工序
            */
            void Append(IN const int& nNodeIndex_, IN const iCAX::Data::uuid& nTradeIndex_, IN const std::vector<int>& vecPreAdjacency_, const bool bIsComplex = false);

            /*
            * @brief 构建
            */
            void Build();

            /*
            * @brief 目标节点是否为图纸
            * @param [in] nNodeIndex_
            * @return bool
            */
            bool IsComplex(IN const int& nNodeIndex_) const;

            /*
            * @brief 获取工种或图纸ID
            * @param [in] nNodeIndex_ 工序索引
            * @return 工种类型
            */
            iCAX::Data::uuid GetTradeOrGraphID(IN const int& nNodeIndex_) const;

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
            iCAX::Data::uuid m_ID;                                //!< 图纸主键
            std::map<int, Node> m_Graph;                            //!< 无环有向图

            //!< 辅助说明
            std::string m_strName;                                  //!< 图纸名称
            std::string m_strRemark;                                //!< 图纸说明
            std::string m_strCatalog;                               //!< 图纸类别，逻辑分组，/作为分隔符
        };
    }
}
