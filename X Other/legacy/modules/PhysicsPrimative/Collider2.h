#pragma once
#include "PhysicsPrimative.h"
#include "Collider.h"
#include "../Math/CoordSys2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维碰撞器基类
        * @details
        *   所有二维碰撞器（Box2、Circle2、Capsule2、Line2 等）都继承自该类。
        *   提供基础属性：
        *   - 局部坐标系 Local（相对于宿主实体的位置与旋转）
        *   - 缩放 Scale（用于非均匀缩放碰撞体）
        *   继承自 cCollider，支持 Trigger 功能。
        */
        class _PHYSICSPRIMATIVE_EXP cCollider2 : public cCollider
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化局部坐标系为单位坐标系，缩放为 {1,1}。
            */
            cCollider2();

            /**
            * @brief 析构函数
            */
            virtual ~cCollider2();

        public: // 成员访问
            /**
            * @brief 局部坐标系（可修改）
            * @return CoordSys2& 局部坐标系
            */
            iCAX::Math::CoordSys2& Local();

            /**
            * @brief 局部坐标系（只读）
            * @return const CoordSys2& 局部坐标系
            */
            const iCAX::Math::CoordSys2& Local() const;

            /**
            * @brief 缩放比例（可修改）
            * @return Vector2& x、y 方向缩放比例
            */
            iCAX::Math::Vector2& Scale();

            /**
            * @brief 缩放比例（只读）
            * @return const Vector2& x、y 方向缩放比例
            */
            const iCAX::Math::Vector2& Scale() const;

        private:
            CoordSys2 m_csLocal;   //!< 局部坐标系（相对于宿主实体）
            Vector2   m_vScale;    //!< 缩放比例（默认 {1,1}）
        };
    }
}
