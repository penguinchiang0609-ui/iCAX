#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Point3.h"
#include <vector>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 索引模式
        * @details
        *   定义网格索引的组织方式：
        *     - kIndicesTriangles: 三角面片
        *     - kIndicesLines: 直线段
        *     - kIndicesPoints: 点云
        */
        enum _PHYSICSPRIMATIVE_EXP cIndicesMode3
        {
            kIndicesTriangles,   //!< 三角面片
            kIndicesLines,       //!< 直线段
            kIndicesPoints,      //!< 点云
        };

        /**
        * @brief 三维网格
        * @details
        *   cMeshData3 表示一个三维网格碰撞体或几何体。
        *   包含顶点列表、索引列表以及索引模式。
        */
        class _PHYSICSPRIMATIVE_EXP cMeshData3 final
        {
        public:
            /**
            * @brief 默认构造函数
            */
            cMeshData3();

            /**
            * @brief 析构函数
            */
            ~cMeshData3();

        public:
            /**
            * @brief 顶点列表（可修改）
            * @return std::vector<Point3>& 顶点引用
            */
            std::vector<Point3>& Vertices();

            /**
            * @brief 顶点列表（只读）
            * @return const std::vector<Point3>& 顶点引用
            */
            const std::vector<Point3>& Vertices() const;

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
            * @return cIndicesMode3& 引用
            */
            cIndicesMode3& Mode();

            /**
            * @brief 索引模式（只读）
            * @return const cIndicesMode3& 引用
            */
            const cIndicesMode3& Mode() const;

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
            std::vector<Point3> m_vecVertices;   //!< 顶点列表
            std::vector<int>   m_vecIndices;     //!< 索引列表
            cIndicesMode3       m_nIndicesMode;   //!< 索引模式
        };
    }
}
