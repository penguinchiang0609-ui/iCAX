#include "pch.h"

#include "OrientedBox3.h"
#include "Box3.h"
#include <vector>
#include <algorithm>
#include <limits>

using namespace iCAX::Math;

//------------------------ 基础方法 ----------------------------

OrientedBox3::OrientedBox3()
    : m_csLocal(CoordSys3()), m_nHalfSize(-1, -1, -1)
{
}

OrientedBox3::OrientedBox3(const CoordSys3& cs, const Double3& hs)
    : m_csLocal(cs), m_nHalfSize(hs)
{
}

//!< 局部坐标系
const iCAX::Math::CoordSys3& iCAX::Math::OrientedBox3::Local() const
{
    return m_csLocal;
}


//!< 半尺寸
const Double3& iCAX::Math::OrientedBox3::HalfSize() const
{
    return m_nHalfSize;
}


// 获取八个角点
std::vector<Point3> OrientedBox3::Corners() const
{
    std::vector<Point3> corners(8);

    Vector3 ux = m_csLocal.XDirection().Vector() * m_nHalfSize[0];
    Vector3 uy = m_csLocal.YDirection().Vector() * m_nHalfSize[1];
    Vector3 uz = m_csLocal.ZDirection().Vector() * m_nHalfSize[2];
    Point3 c = m_csLocal.Location();

    // 八个角点组合 (+/- ux, +/- uy, +/- uz)
    corners[0] = c + ux + uy + uz;
    corners[1] = c - ux + uy + uz;
    corners[2] = c - ux - uy + uz;
    corners[3] = c + ux - uy + uz;
    corners[4] = c + ux + uy - uz;
    corners[5] = c - ux + uy - uz;
    corners[6] = c - ux - uy - uz;
    corners[7] = c + ux - uy - uz;

    return corners;
}

// 是否为空
bool OrientedBox3::IsEmpty() const
{
    return (m_nHalfSize[0] < 0) || (m_nHalfSize[1] < 0) || (m_nHalfSize[2] < 0);
}

// 判断点是否在盒子内
bool OrientedBox3::Contains(const Point3& pt) const
{
    // 转到局部坐标系
    Point3 local = m_csLocal.WorldToLocal().Applied(pt);

    return (local.X() >= -m_nHalfSize[0] && local.X() <= m_nHalfSize[0]) &&
        (local.Y() >= -m_nHalfSize[1] && local.Y() <= m_nHalfSize[1]) &&
        (local.Z() >= -m_nHalfSize[2] && local.Z() <= m_nHalfSize[2]);
}

// 转换为 AABB
Box3 OrientedBox3::ToAABB() const
{
    std::vector<Point3> corners = Corners();
    if (corners.empty()) return Box3();

    Point3 minPt = corners[0];
    Point3 maxPt = corners[0];

    for (size_t i = 1; i < corners.size(); ++i)
    {
        const Point3& p = corners[i];
        minPt.X() = min(minPt.X(), p.X());
        minPt.Y() = min(minPt.Y(), p.Y());
        minPt.Z() = min(minPt.Z(), p.Z());

        maxPt.X() = max(maxPt.X(), p.X());
        maxPt.Y() = max(maxPt.Y(), p.Y());
        maxPt.Z() = max(maxPt.Z(), p.Z());
    }

    return Box3(minPt, maxPt);
}
