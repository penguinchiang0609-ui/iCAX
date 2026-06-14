#include "pch.h"
#include "Mesh2.h"

//!< 默认构造函数
iCAX::Physics::cMesh2::cMesh2()
    : m_Mesh(nullptr) // 默认网格指针为空
{
}

//!< 析构函数
iCAX::Physics::cMesh2::~cMesh2()
{
}

//!< 网格数据（可修改）
std::shared_ptr<iCAX::Physics::cMeshData2>& iCAX::Physics::cMesh2::MeshData()
{
    return m_Mesh;
}

//!< 网格数据（只读）
const std::shared_ptr<iCAX::Physics::cMeshData2>& iCAX::Physics::cMesh2::MeshData() const
{
    return m_Mesh;
}
