#include "pch.h"
#include "Mesh2.h"

//!< 默认构造函数
iCAX::Physics::cMeshData2::cMeshData2()
    : m_nIndicesMode(kIndicesLines) // 默认索引模式为直线段
{
}

//!< 析构函数
iCAX::Physics::cMeshData2::~cMeshData2()
{
}

//!< 顶点列表（可修改）
std::vector<Point2>& iCAX::Physics::cMeshData2::Vertices()
{
    return m_vecVertices;
}

//!< 顶点列表（只读）
const std::vector<Point2>& iCAX::Physics::cMeshData2::Vertices() const
{
    return m_vecVertices;
}

//!< 索引列表（可修改）
std::vector<int>& iCAX::Physics::cMeshData2::Indices()
{
    return m_vecIndices;
}

//!< 索引列表（只读）
const std::vector<int>& iCAX::Physics::cMeshData2::Indices() const
{
    return m_vecIndices;
}

//!< 索引模式（可修改）
iCAX::Physics::cIndicesMode2& iCAX::Physics::cMeshData2::Mode()
{
    return m_nIndicesMode;
}

//!< 索引模式（只读）
const iCAX::Physics::cIndicesMode2& iCAX::Physics::cMeshData2::Mode() const
{
    return m_nIndicesMode;
}

//!< 顶点数量
int iCAX::Physics::cMeshData2::VerticesCount() const
{
    return static_cast<int>(m_vecVertices.size());
}

//!< 索引数量
int iCAX::Physics::cMeshData2::IndicesCount() const
{
    return static_cast<int>(m_vecIndices.size());
}
