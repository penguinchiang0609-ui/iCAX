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
        * @brief 二维线碰撞器
        * @details
        *   表示一条二维无限长直线碰撞体，可用于二维物理或碰撞检测。
        *   继承自 cCollider2，支持：
        *   - 局部坐标系 Local（相对于宿主实体）
        *   - 缩放 Scale
        *   - Trigger 功能（是否仅检测重叠，不参与物理反应）
        *
        *   当前类不存储具体的法向或距离，线的表示可通过 Local 坐标系决定。
        */
        class _PHYSICSPRIMATIVE_EXP cLine2 : public cCollider2
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化局部坐标系为单位坐标系，缩放为 {1,1}。
            */
            cLine2();

            /**
            * @brief 析构函数
            */
            virtual ~cLine2();
        };
    }
}
