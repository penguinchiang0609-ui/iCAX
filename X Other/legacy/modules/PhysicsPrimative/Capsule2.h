#pragma once
#include "PhysicsPrimative.h"
#include "Collider2.h"

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维胶囊碰撞器
        * @details
        *   cCapsule2 表示一个二维空间中的胶囊体碰撞器（Capsule Collider）。
        *   胶囊由一条矩形中心线和两端的半圆组成。
        *   常用于角色控制器、物理模拟等需要平滑边界的碰撞检测场景。
        *
        *   胶囊的中心轴方向由局部坐标系 (cCollider2::m_csLocal) 的 X 轴或 Y 轴定义。
        */
        class _PHYSICSPRIMATIVE_EXP cCapsule2 : public cCollider2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details
            *   初始化半径和高度为 0。
            */
            cCapsule2();

            /**
            * @brief 析构函数
            */
            virtual ~cCapsule2();

        public:
            /**
            * @brief 获取或设置胶囊体半径（可修改）
            * @return double& 半径引用
            * @note 半径定义了两端半圆的半径。
            */
            double& Radius();

            /**
            * @brief 获取胶囊体半径（只读）
            * @return const double& 半径引用
            */
            const double& Radius() const;

            /**
            * @brief 获取或设置胶囊体高度（可修改）
            * @return double& 高度引用
            * @note 高度为矩形中心线长度，不包括半圆部分。
            */
            double& Height();

            /**
            * @brief 获取胶囊体高度（只读）
            * @return const double& 高度引用
            */
            const double& Height() const;

        private:
            double m_nRadius;   //!< 胶囊体半径（两端半圆的半径）
            double m_nHeight;   //!< 胶囊体高度（矩形中心线长度）
        };
    }
}
