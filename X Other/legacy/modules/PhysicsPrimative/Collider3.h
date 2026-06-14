#pragma once
#include "PhysicsPrimative.h"
#include "Collider.h"
#include "../Math/CoordSys3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维碰撞器基类
        * @details
        *   所有三维碰撞器（Box3、Sphere3、Capsule3、Cylinder3 等）都继承自该类。
        *   提供基础属性：
        *   - 局部坐标系 Local（相对于宿主实体的位置与旋转）
        *   - 缩放 Scale（用于非均匀缩放碰撞体）
        *   继承自 cCollider，支持 Trigger 功能。
        */
        class _PHYSICSPRIMATIVE_EXP cCollider3 : public cCollider
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化局部坐标系为单位坐标系，缩放为 {1,1,1}。
            */
            cCollider3();

            /**
            * @brief 析构函数
            */
            virtual ~cCollider3();

        public: // 成员访问
            /**
            * @brief 局部坐标系（可修改）
            * @return CoordSys3& 局部坐标系
            */
            iCAX::Math::CoordSys3& Local();

            /**
            * @brief 局部坐标系（只读）
            * @return const CoordSys3& 局部坐标系
            */
            const iCAX::Math::CoordSys3& Local() const;

            /**
            * @brief 缩放比例（可修改）
            * @return Vector3& x、y、z 方向缩放比例
            */
            iCAX::Math::Vector3& Scale();

            /**
            * @brief 缩放比例（只读）
            * @return const Vector3& x、y、z 方向缩放比例
            */
            const iCAX::Math::Vector3& Scale() const;

        private:
            CoordSys3 m_csLocal;   //!< 局部坐标系（相对于宿主实体）
            Vector3   m_vScale;    //!< 缩放比例（默认 {1,1,1}）
        };
    }
}
