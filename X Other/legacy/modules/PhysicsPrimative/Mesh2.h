#pragma once

#include "PhysicsPrimative.h"
#include "Collider2.h"
#include "../../01 Foundation/Data/uuid.h"
#include "MeshData2.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维网格碰撞器
        * @details
        *   cMesh2 表示一个基于二维网格的碰撞体。
        *   内部包含一个 std::shared_ptr<cMeshData2>，用于存储顶点、索引和索引模式等数据。
        *   提供可修改和只读访问接口，方便 Collider 系统直接使用。
        */
        class _PHYSICSPRIMATIVE_EXP cMesh2 : public cCollider2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化内部网格指针为空。
            */
            cMesh2();

            /**
            * @brief 析构函数
            */
            virtual ~cMesh2();

        public:
            /**
            * @brief 网格数据（可修改）
            * @return std::shared_ptr<cMeshData2>& 网格数据引用
            */
            std::shared_ptr<cMeshData2>& MeshData();

            /**
            * @brief 网格数据（只读）
            * @return const std::shared_ptr<cMeshData2>& 网格数据引用
            */
            const std::shared_ptr<cMeshData2>& MeshData() const;

        private:
            std::shared_ptr<cMeshData2> m_Mesh;  //!< 网格数据指针
        };
    }
}
