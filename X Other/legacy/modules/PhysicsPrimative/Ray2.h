#pragma once
#include "PhysicsPrimative.h"
#include "../Math/Axis2.h"
#include <cfloat>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @class cRay2
        * @brief 二维射线类
        * @details
        *   表示二维空间中的射线，用于物理查询、碰撞检测和射线投射等场景。
        *   射线由起点和方向向量定义，可选择有限长度或无限长度（使用 DBL_MAX）。
        *
        * @note 与 cSegment2 的区别：
        *   - Segment：有限起点到终点
        *   - Ray：起点 + 方向向量，可无限延伸
        */
        class _PHYSICSPRIMATIVE_EXP cRay2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 射线起点为原点，方向为 X 轴正方向，长度无限大
            */
            cRay2();

            /**
            * @brief 构造函数
            * @param [in] ptLocation_ 射线起点
            * @param [in] dir_ 射线方向（不必归一化，会自动处理）
            * @param [in] nLength_ 射线长度，默认无限长
            */
            cRay2(IN const Point2& ptLocation_, IN const Dir2& dir_, IN const double& nLength_ = DBL_MAX);

        public:
            /**
            * @brief 获取或修改射线起点
            * @return Point2&
            */
            Point2& Location();

            /**
            * @brief 获取射线起点（只读）
            * @return const Point2&
            */
            const Point2& Location() const;

            /**
            * @brief 获取或修改射线方向
            * @return Dir2&
            * @note 构造函数会自动归一化方向向量
            */
            Dir2& Direction();

            /**
            * @brief 获取射线方向（只读）
            * @return const Dir2&
            */
            const Dir2& Direction() const;

            /**
            * @brief 获取或修改射线长度
            * @return double&
            */
            double& Length();

            /**
            * @brief 获取射线长度（只读）
            * @return const double&
            */
            const double& Length() const;

        private:
            Axis2 m_ax;       //!< 射线局部坐标轴，Origin 为射线起点，X 方向为射线方向
            double m_nLength; //!< 射线长度，默认无限长
        };
    }
}
