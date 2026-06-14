#pragma once

#include "PhysicsPrimative.h"
#include "Collider3.h"
#include "../../01 Foundation/Data/uuid.h"
#include "MeshData3.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维网格碰撞器
        * @details
        *   cMesh3 表示一个基于三维网格的碰撞体。
        *   内部包含一个 std::shared_ptr<cMeshData3>，用于存储网格顶点、索引和索引模式等数据。
        *   提供可修改和只读访问接口，方便 Collider 系统直接使用。
        */
        class _PHYSICSPRIMATIVE_EXP cMesh3 : public cCollider3
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化内部网格指针为空。
            */
            cMesh3();

            /**
            * @brief 析构函数
            */
            virtual ~cMesh3();

        public:
            /**
            * @brief 网格数据（可修改）
            * @return std::shared_ptr<cMeshData3>& 网格数据引用
            */
            std::shared_ptr<cMeshData3>& MeshData();

            /**
            * @brief 网格数据（只读）
            * @return const std::shared_ptr<cMeshData3>& 网格数据引用
            */
            const std::shared_ptr<cMeshData3>& MeshData() const;

        private:
            std::shared_ptr<cMeshData3> m_Mesh;  //!< 网格数据指针
        };
    }
}
