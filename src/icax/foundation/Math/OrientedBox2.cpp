#include "pch.h"

#include "OrientedBox2.h"
#include "Box2.h"
#include <vector>
#include <algorithm>
#include <cmath>

using namespace iCAX::Math;

//------------------------ 基础方法 ----------------------------

iCAX::Math::OrientedBox2::OrientedBox2()
    : m_csLocal(CoordSys2())
    , m_nHalfSize(-1, -1)
{
}

//!< 构造函数
iCAX::Math::OrientedBox2::OrientedBox2(const CoordSys2& cs, const Double2& hs) 
    : m_csLocal(cs), m_nHalfSize(hs)
{
}

//!< 局部坐标系
const iCAX::Math::CoordSys2& iCAX::Math::OrientedBox2::Local() const
{
    return m_csLocal;
}


//!< 半尺寸
const Double2& iCAX::Math::OrientedBox2::HalfSize() const
{
    return m_nHalfSize;
}


// 获取四个角点（世界坐标）
std::vector<Point2> OrientedBox2::Corners() const
{
    std::vector<Point2> corners(4);

    // 局部轴向单位向量，按半尺寸缩放
    Vector2 ux = m_csLocal.XDirection().Vector() * m_nHalfSize[0];
    Vector2 uy = m_csLocal.YDirection().Vector() * m_nHalfSize[1];
    Point2 c = m_csLocal.Location();

    // 计算四个角点
    corners[0] = c + ux + uy; // 左上
    corners[1] = c - ux + uy; // 右上
    corners[2] = c - ux - uy; // 右下
    corners[3] = c + ux - uy; // 左下

    return corners;
}

//!< 是否为空
bool iCAX::Math::OrientedBox2::IsEmpty() const
{
    return (m_nHalfSize[0] < 0) || (m_nHalfSize[1] < 0);
}

// 判断点是否在盒子内
bool OrientedBox2::Contains(const Point2& pt) const
{
    // 将点转换到局部坐标系
    Point2 local = m_csLocal.WorldToLocal().Applied(pt);

    // 检查局部坐标是否在半尺寸范围内
    return (local.X() >= -m_nHalfSize[0] && local.X() <= m_nHalfSize[0]) &&
        (local.Y() >= -m_nHalfSize[1] && local.Y() <= m_nHalfSize[1]);
}

// 转换为世界坐标下的 AABB
Box2 OrientedBox2::ToAABB() const
{
    std::vector<Point2> corners = Corners();
    if (corners.empty()) return Box2();

    Point2 minPt = corners[0];
    Point2 maxPt = corners[0];

    for (size_t i = 1; i < corners.size(); ++i)
    {
        const Point2& p = corners[i];
        minPt.X() = min(minPt.X(), p.X());
        minPt.Y() = min(minPt.Y(), p.Y());
        maxPt.X() = max(maxPt.X(), p.X());
        maxPt.Y() = max(maxPt.Y(), p.Y());
    }

    return Box2(minPt, maxPt);
}
