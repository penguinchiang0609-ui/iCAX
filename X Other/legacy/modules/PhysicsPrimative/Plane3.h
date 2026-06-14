#pragma once

#include "PhysicsPrimative.h"
#include "Collider3.h"
#include "../Math/Vector3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维平面碰撞器
        * @details
        *   表示一个无限大的三维平面碰撞体，可用于物理或碰撞检测。
        *   继承自 cCollider3，支持：
        *   - 局部坐标系 Local（相对于宿主实体）
        *   - 缩放 Scale
        *   - Trigger 功能（是否仅检测重叠，不参与物理反应）
        *
        *   平面的位置和方向由 Local 坐标系决定，当前类不存储额外的法向或偏移数据。
        */
        class _PHYSICSPRIMATIVE_EXP cPlane3 : public cCollider3
        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化局部坐标系为单位坐标系，缩放为 {1,1,1}。
            */
            cPlane3();

            /**
            * @brief 析构函数
            */
            virtual ~cPlane3();
        };
    }
}
