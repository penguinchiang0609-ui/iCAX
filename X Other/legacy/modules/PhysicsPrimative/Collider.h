#pragma once
#include "PhysicsPrimative.h"

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 碰撞器基类
        * @details
        *   所有具体碰撞器（Box、Sphere、Capsule 等）都继承自该类。
        *   提供基础属性：是否作为触发器（Trigger）。
        *   Trigger 碰撞器不参与物理反应，只检测重叠。
        */
        class _PHYSICSPRIMATIVE_EXP cCollider

        {
        public:
            /**
            * @brief 构造函数
            * @details 初始化 Trigger 为 false。
            */
            cCollider();

            /**
            * @brief 析构函数
            */
            virtual ~cCollider();

        public: // 成员访问
            /**
            * @brief 获取或设置触发器状态
            * @return bool& 可修改 Trigger 状态
            */
            bool& Trigger();

            /**
            * @brief 获取触发器状态（只读）
            * @return const bool& Trigger 状态
            */
            const bool& Trigger() const;

        private:
            bool m_bTrigger;  //!< 是否作为触发器（不参与物理反应，只检测重叠）
        };
    }
}
