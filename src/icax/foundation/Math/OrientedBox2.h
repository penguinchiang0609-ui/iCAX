#pragma once
#include "Point2.h"
#include "Vector2.h"
#include "CoordSys2.h"
#include "Box2.h"
#include <vector>

namespace iCAX::Math
{
    /**
    * @brief 二维方向包围盒 (OBB / OrientedBox2)
    * @details
    * 使用局部坐标系 CoordSys2 表示盒子中心和方向，
    * m_nHalfSize 表示沿局部 X/Y 轴的半尺寸。
    *
    * 支持：
    * - Extend(point)：扩展 OBox 以包含新点，生成最小方向包围盒
    * - Union(a, b)：合并两个 OBox，生成最小方向包围盒
    *
    * @note 所有操作都会生成最小面积包围盒，方向可能改变。
    */
    class _MATH_EXP OrientedBox2 final
    {
    public:
        OrientedBox2();

        /**
        * @brief 使用给定坐标系和半尺寸构造 OBox
        * @param [in] cs 局部坐标系
        * @param [in] hs 半尺寸向量
        */
        OrientedBox2(const CoordSys2& cs, const Double2& hs);

    public:
        /*
        * @brief 局部坐标系
        * @return const CoordSys2&
        */
        const CoordSys2& Local() const;
        /*
        * @brief 半尺寸
        * @return const Double2&
        */
        const Double2& HalfSize() const;
        /**
        * @brief 获取盒子中心点（世界坐标）
        * @return 中心点坐标
        */
        Point2 Center() const { return m_csLocal.Location(); }

        /**
        * @brief 获取四个角点（世界坐标）
        * @return 包含四个角点的向量
        * @note 顺序为左上、右上、右下、左下（局部坐标系相对中心）
        */
        std::vector<Point2> Corners() const;

        /**
        * @brief 判断当前包围盒是否为空
        * @return 若 min > max（任意轴上）则为空
        */
        bool IsEmpty() const;

        /**
        * @brief 判断点是否在盒子内
        * @param [in] pt 待检测点（世界坐标）
        * @return 若点在盒子内（含边界）返回 true，否则返回 false
        */
        bool Contains(const Point2& pt) const;

        /**
        * @brief 转换为世界坐标下的轴对齐包围盒 (AABB)
        * @return Box2 包含 OBox 所有角点的最小 AABB
        */
        Box2 ToAABB() const;

        //!< 不提供Extend与Union，因为他们涉及到重新计算Oriented，需要先寻找凸包再来寻求最大轴（最小面积法），这里面涉及大量的计算，应放到algo库
    private:
        CoordSys2 m_csLocal;   //!< 局部坐标系（中心 + 方向）
        Double2 m_nHalfSize;  //!< 半尺寸（沿局部坐标轴）

    };
}
