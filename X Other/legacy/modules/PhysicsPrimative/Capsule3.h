#pragma once
#include "PhysicsPrimative.h"
#include "Collider3.h"

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维胶囊碰撞器
        * @details
        *   cCapsule3 表示一个三维空间中的胶囊体碰撞器（Capsule Collider）。
        *   胶囊体由一个圆柱体和其两端的半球体组成。
        *   常用于角色控制器、物理模拟等需要平滑边界的碰撞检测场景。
        *
        *   胶囊的中心轴方向由局部坐标系 (cCollider3::m_csLocal) 的 Z 轴定义，
        *   高度参数表示两个半球球心之间的距离。
        */
        class _PHYSICSPRIMATIVE_EXP cCapsule3 : public cCollider3
        {
        public:
            /**
            * @brief 默认构造函数
            * @details
            *   初始化半径和高度为 0。
            */
            cCapsule3();

            /**
            * @brief 析构函数
            */
            virtual ~cCapsule3();

        public:
            /**
            * @brief 获取或设置胶囊体半径（可修改）
            * @return double& 半径引用
            * @note 半径定义了圆柱体和半球体的半径。
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
            * @note 高度为两个半球球心之间的距离，不包括半球部分。
            */
            double& Height();

            /**
            * @brief 获取胶囊体高度（只读）
            * @return const double& 高度引用
            */
            const double& Height() const;

        private:
            double m_nRadius;   //!< 胶囊体半径（Cylinder 和两端球体的半径）
            double m_nHeight;   //!< 胶囊体高度（两个半球球心之间的直线距离）
        };
    }
}
