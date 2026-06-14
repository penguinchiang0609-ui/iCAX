#pragma once
#include "PhysicsPrimative.h"
#include "../../01 Foundation/Data/uuid.h"
#include <vector>
#include "../Math/Point3.h"
#include "../Math/Vector3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @brief 三维接触点信息
        * @details
        *   存储一次碰撞的接触点数据，包括位置、法线和穿透深度。
        */
        class _PHYSICSPRIMATIVE_EXP cContactPoint3 final
        {
        public:
            /**
            * @brief 构造函数
            * @param ptPos_ 接触点位置
            * @param vNormal_ 接触法线，指向第二个对象
            * @param nPenetration_ 穿透深度
            */
            cContactPoint3(IN const Point3& ptPos_, IN const Vector3& vNormal_, IN const double& nPenetration_);

            /**
            * @brief 析构函数
            */
            ~cContactPoint3();

        public:
            /**
            * @brief 接触点位置（只读）
            */
            const Point3& Position() const;

            /**
            * @brief 接触法线（只读），指向第二个对象
            */
            const Vector3& Normal() const;

            /**
            * @brief 穿透深度（只读）
            */
            double Penetration() const;

        private:
            Point3 m_ptPos;         //!< 接触点位置（世界坐标）
            Vector3 m_vNormal;      //!< 接触法线，指向第二个对象
            double m_nPenetration;  //!< 穿透深度
        };

        /**
        * @brief 三维碰撞结果
        * @details
        *   存储一次碰撞的整体结果，包括碰撞的两个实体、触发器标记和接触点列表。
        */
        class _PHYSICSPRIMATIVE_EXP cCollisionResult3 final
        {
        public:
            /**
            * @brief 构造函数
            * @param EntityA_ 实体 MajorRadius 的 ID
            * @param EntityB_ 实体 MinorRadius 的 ID
            * @param bTrigger_ 是否触发器
            */
            cCollisionResult3(IN const uuid& EntityA_, IN const uuid& EntityB_, IN const bool bTrigger_);

            /**
            * @brief 析构函数
            */
            ~cCollisionResult3();

        public:
            /**
            * @brief 实体 MajorRadius ID（只读）
            */
            const uuid& EntityA() const;

            /**
            * @brief 实体 MinorRadius ID（只读）
            */
            const uuid& EntityB() const;

            /**
            * @brief 是否触发器（只读）
            */
            const bool& IsTrigger() const;

            /**
            * @brief 接触点列表（可修改）
            */
            std::vector<cContactPoint3>& Contacts();

            /**
            * @brief 接触点列表（只读）
            */
            const std::vector<cContactPoint3>& Contacts() const;

            /**
            * @brief 增加接触点
            * @param Contact_ 接触点数据
            */
            void Push(IN const cContactPoint3& Contact_);

        private:
            uuid m_EntityA;                        //!< 碰撞对象 MajorRadius 的 EntityId
            uuid m_EntityB;                        //!< 碰撞对象 MinorRadius 的 EntityId
            std::vector<cContactPoint3> m_vecContacts; //!< 可能有多个接触点
            bool m_bTrigger = false;               //!< 如果其中一个是 Trigger，就标记
        };
    }
}
