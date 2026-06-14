#pragma once
#include "PhysicsPrimative.h"
#include "Collider3.h"

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维圆柱碰撞器
        * @details
        *   cCylinder3 表示一个三维空间中的圆柱形碰撞体（Cylinder Collider）。
        *   圆柱体的中心轴方向由局部坐标系 (cCollider3::m_csLocal) 的 Z 轴定义，
        *   半径控制圆柱底面大小，高度控制圆柱中心轴长度。
        */
        class _PHYSICSPRIMATIVE_EXP cCylinder3 : public cCollider3
        {
        public:
            /**
            * @brief 默认构造函数
            * @details
            *   初始化半径和高度为 0。
            */
            cCylinder3();

            /**
            * @brief 析构函数
            */
            virtual ~cCylinder3();

        public:
            /**
            * @brief 获取或设置圆柱半径（可修改）
            * @return double& 半径引用
            */
            double& Radius();

            /**
            * @brief 获取圆柱半径（只读）
            * @return const double& 半径引用
            */
            const double& Radius() const;

            /**
            * @brief 获取或设置圆柱高度（可修改）
            * @return double& 高度引用
            */
            double& Height();

            /**
            * @brief 获取圆柱高度（只读）
            * @return const double& 高度引用
            */
            const double& Height() const;

        private:
            double m_nRadius;   //!< 圆柱体半径（底面圆半径）
            double m_nHeight;   //!< 圆柱体高度（中心轴长度）
        };
    }
}
