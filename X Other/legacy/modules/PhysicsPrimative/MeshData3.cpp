#include "pch.h"
#include "Mesh3.h"

//!< 默认构造函数
iCAX::Physics::cMeshData3::cMeshData3()
    : m_nIndicesMode(kIndicesTriangles) // 默认索引模式为三角面片
{
}

//!< 析构函数
iCAX::Physics::cMeshData3::~cMeshData3()
{
}

//!< 顶点列表（可修改）
std::vector<Point3>& iCAX::Physics::cMeshData3::Vertices()
{
    return m_vecVertices;
}

//!< 顶点列表（只读）
const std::vector<Point3>& iCAX::Physics::cMeshData3::Vertices() const
{
    return m_vecVertices;
}

//!< 索引列表（可修改）
std::vector<int>& iCAX::Physics::cMeshData3::Indices()
{
    return m_vecIndices;
}

//!< 索引列表（只读）
const std::vector<int>& iCAX::Physics::cMeshData3::Indices() const
{
    return m_vecIndices;
}

//!< 索引模式（可修改）
iCAX::Physics::cIndicesMode3& iCAX::Physics::cMeshData3::Mode()
{
    return m_nIndicesMode;
}

//!< 索引模式（只读）
const iCAX::Physics::cIndicesMode3& iCAX::Physics::cMeshData3::Mode() const
{
    return m_nIndicesMode;
}

//!< 顶点数量
int iCAX::Physics::cMeshData3::VerticesCount() const
{
    return static_cast<int>(m_vecVertices.size());
}

//!< 索引数量
int iCAX::Physics::cMeshData3::IndicesCount() const
{
    return static_cast<int>(m_vecIndices.size());
}
