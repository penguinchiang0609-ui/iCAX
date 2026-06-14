#pragma once
#include "PhysicsPrimative.h"
#include "Collider2.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 二维圆形碰撞器
        * @details
        *   cCircle2 表示一个二维空间中的圆形碰撞体（Circle Collider）。
        *   圆形的中心由局部坐标系 (cCollider2::m_csLocal) 定义。
        *   常用于角色控制器、物理检测、触发器等场景。
        */
        class _PHYSICSPRIMATIVE_EXP cCircle2 : public cCollider2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details
            *   初始化半径为 0。
            */
            cCircle2();

            /**
            * @brief 析构函数
            */
            virtual ~cCircle2();

        public:
            /**
            * @brief 获取或设置圆形半径（可修改）
            * @return double& 半径引用
            */
            double& Radius();

            /**
            * @brief 获取圆形半径（只读）
            * @return const double& 半径引用
            */
            const double& Radius() const;

        private:
            double m_nRadius;   //!< 圆形半径
        };
    }
}
