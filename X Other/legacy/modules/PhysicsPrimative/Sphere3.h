#pragma once

#include "PhysicsPrimative.h"
#include "Collider3.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维球碰撞器
        * @details
        *   cSphere3 表示一个三维球形碰撞体。
        *   半径可修改，可用于碰撞检测。
        */
        class _PHYSICSPRIMATIVE_EXP cSphere3 : public cCollider3
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化半径为 0。
            */
            cSphere3();

            /**
            * @brief 析构函数
            */
            virtual ~cSphere3();

        public:
            /**
            * @brief 半径（可修改）
            * @return double& 半径引用
            */
            double& Radius();

            /**
            * @brief 半径（只读）
            * @return const double& 半径引用
            */
            const double& Radius() const;

        private:
            double m_nRadius;   //!< 球体半径
        };
    }
}
