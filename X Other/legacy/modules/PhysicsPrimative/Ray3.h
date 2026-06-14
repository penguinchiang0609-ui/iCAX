#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Axis3.h"
#include <cfloat>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @class cRay3
        * @brief 三维射线类
        * @details
        *   表示空间中的射线，用于物理查询、碰撞检测和射线投射等场景。
        *   射线由起点和方向向量定义，可选择有限长度或无限长度（使用 DBL_MAX）。
        *
        * @note 与 cSegment3 区别：
        *   - Segment：有限起点到终点
        *   - Ray：起点 + 方向向量，可无限延伸
        */
        class _PHYSICSPRIMATIVE_EXP cRay3
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 射线起点为原点，方向为 (0,0,1)，长度无限大
            */
            cRay3();

            /**
            * @brief 构造函数
            * @param [in] ptLocation_ 射线起点
            * @param [in] dir_ 射线方向（不必归一化，会自动处理）
            * @param [in] nLength_ 射线长度，默认无限长
            */
            cRay3(IN const Point3& ptLocation_, IN const Dir3& dir_, IN const double& nLength_ = DBL_MAX);

        public:
            /**
            * @brief 获取射线起点
            * @return Point3& 可修改
            */
            Point3& Location();

            /**
            * @brief 获取射线起点（只读）
            * @return const Point3&
            */
            const Point3& Location() const;

            /**
            * @brief 获取射线方向向量
            * @return Vector3& 可修改（建议保持单位向量）
            */
            Dir3& Direction();

            /**
            * @brief 获取射线方向向量（只读）
            * @return const Vector3&
            */
            const Dir3& Direction() const;

            /**
            * @brief 获取射线长度
            * @return double& 可修改
            */
            double& Length();

            /**
            * @brief 获取射线长度（只读）
            * @return const double&
            */
            const double& Length() const;

        private:
            Axis3 m_ax;
            double  m_nLength;    //!< 射线长度，默认无限长
        };
    }
}
