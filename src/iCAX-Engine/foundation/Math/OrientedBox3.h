#pragma once
#include "Point3.h"
#include "Vector3.h"
#include "CoordSys3.h"
#include "Box3.h"
#include <vector>

namespace iCAX::Math
{
    /**
    * @brief 三维方向包围盒 (OBB / OrientedBox3)
    * @details
    * 使用局部坐标系 CoordSys3 表示盒子中心和方向，
    * m_nHalfSize 表示沿局部 X/Y/Z 轴的半尺寸。
    *
    * 支持：
    * - 获取中心点
    * - 获取八个角点
    * - 判断点是否在盒子内
    * - 转换为世界坐标下的轴对齐包围盒 (AABB)
    *
    * @note 方向固定为 CoordSys3，不提供 Extend 和 Union。
    */
    class _MATH_EXP OrientedBox3 final
    {
    public:
        OrientedBox3();

        /**
        * @brief 使用给定坐标系和半尺寸构造 OBox3
        * @param [in] cs 局部坐标系
        * @param [in] hs 半尺寸向量 (hx, hy, hz)
        */
        OrientedBox3(const CoordSys3& cs, const Double3& hs);

    public:
        /*
        * @brief 局部坐标系
        * @return const CoordSys2&
        */
        const CoordSys3& Local() const;
        /*
        * @brief 半尺寸
        * @return const Double2&
        */
        const Double3& HalfSize() const;

        /**
        * @brief 获取盒子中心点（世界坐标）
        * @return 中心点坐标
        */
        Point3 Center() const { return m_csLocal.Location(); }

        /**
        * @brief 获取八个角点（世界坐标）
        * @return 包含八个角点的向量
        * @note 顺序为局部坐标系相对中心的立方体顶点
        */
        std::vector<Point3> Corners() const;

        /**
        * @brief 判断当前包围盒是否为空
        * @return 若任意半尺寸 < 0，则为空
        */
        bool IsEmpty() const;

        /**
        * @brief 判断点是否在盒子内
        * @param [in] pt 待检测点（世界坐标）
        * @return 若点在盒子内（含边界）返回 true，否则返回 false
        */
        bool Contains(const Point3& pt) const;

        /**
        * @brief 转换为世界坐标下的轴对齐包围盒 (AABB)
        * @return Box3 包含 OBox3 所有角点的最小 AABB
        */
        Box3 ToAABB() const;

    private:
        CoordSys3 m_csLocal;   //!< 局部坐标系（中心 + 方向）
        Double3 m_nHalfSize;   //!< 半尺寸（沿局部坐标轴 X/Y/Z）
    };
}
