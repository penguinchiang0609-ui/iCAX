#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Point2.h"
#include <vector>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维网格索引模式
        * @details
        *   定义二维网格索引的组织方式：
        *     - kIndicesLines: 直线段
        *     - kIndicesPoints: 点云
        */
        enum _PHYSICSPRIMATIVE_EXP cIndicesMode2
        {
            kIndicesLines,   //!< 直线段
            kIndicesPoints,  //!< 点云
        };

        /**
        * @brief 二维网格
        * @details
        *   cMeshData2 表示二维空间中的网格碰撞体或几何体。
        *   包含顶点列表、索引列表以及索引模式。
        */
        class _PHYSICSPRIMATIVE_EXP cMeshData2 final
        {
        public:
            /**
            * @brief 默认构造函数
            */
            cMeshData2();

            /**
            * @brief 析构函数
            */
            ~cMeshData2();

        public:
            /**
            * @brief 顶点列表（可修改）
            * @return std::vector<Point2>& 顶点引用
            */
            std::vector<Point2>& Vertices();

            /**
            * @brief 顶点列表（只读）
            * @return const std::vector<Point2>& 顶点引用
            */
            const std::vector<Point2>& Vertices() const;

            /**
            * @brief 索引列表（可修改）
            * @return std::vector<int>& 索引引用
            */
            std::vector<int>& Indices();

            /**
            * @brief 索引列表（只读）
            * @return const std::vector<int>& 索引引用
            */
            const std::vector<int>& Indices() const;

            /**
            * @brief 索引模式（可修改）
            * @return cIndicesMode2& 引用
            */
            cIndicesMode2& Mode();

            /**
            * @brief 索引模式（只读）
            * @return const cIndicesMode2& 引用
            */
            const cIndicesMode2& Mode() const;

            /**
            * @brief 顶点数量
            * @return int
            */
            int VerticesCount() const;

            /**
            * @brief 索引数量
            * @return int
            */
            int IndicesCount() const;

        private:
            std::vector<Point2> m_vecVertices;   //!< 顶点列表
            std::vector<int>    m_vecIndices;    //!< 索引列表
            cIndicesMode2       m_nIndicesMode;  //!< 索引模式
        };
    }
}
