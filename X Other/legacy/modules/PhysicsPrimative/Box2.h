#pragma once
#include "PhysicsPrimative.h"
#include "Collider2.h"
#include "../Math/Vector2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维盒子碰撞器
        * @details
        *   表示一个二维轴对齐或局部坐标系下的矩形碰撞区域，
        *   使用半偏移（HalfExtents）表示矩形在 x 和 y 方向的半宽、高。
        *   继承自 cCollider2，支持局部坐标系、旋转和偏移。
        */
        class _PHYSICSPRIMATIVE_EXP cBox2 : public cCollider2
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化半偏移为零向量。
            */
            cBox2();

            /**
            * @brief 析构函数
            */
            virtual ~cBox2();

        public:
            /**
            * @brief 获取半偏移（只读）
            * @return Vector2 矩形在 x 和 y 方向的半宽、高
            */
            const Vector2& HalfExtents() const;

            /**
            * @brief 获取半偏移（可修改）
            * @return Vector2 矩形在 x 和 y 方向的半宽、高
            */
            Vector2& HalfExtents();

        private:
            Vector2 m_vHalfExtents;  //!< 矩形在 x 和 y 方向的半宽、高
        };
    }
}
