#include "pch.h"
#include "Box3.h"

//!< 构造函数
iCAX::Math::Box3::Box3()
    : m_ptMin(1, 1, 1)
    , m_ptMax(-1, -1, -1)
{
}

iCAX::Math::Box3::Box3(const Point3& minPt, const Point3& maxPt)
    : m_ptMin(minPt), m_ptMax(maxPt)
{
}

//!< 最小点
const iCAX::Math::Point3& iCAX::Math::Box3::MinPoint() const
{
    return m_ptMin;
}
//!< 最大点
const iCAX::Math::Point3& iCAX::Math::Box3::MaxPoint() const
{
    return m_ptMax;
}

iCAX::Math::Point3 iCAX::Math::Box3::Center() const
{
    return (m_ptMin.XYZ() + m_ptMax.XYZ()) * 0.5;
}

//!< 获取每个面的边长
Double3 iCAX::Math::Box3::Size() const
{
    return m_ptMax.XYZ() - m_ptMin.XYZ();
}

//!< 是否为空
bool iCAX::Math::Box3::IsEmpty() const
{
    if (m_ptMin.IsEqual(m_ptMax))
    {
        return false;
    }
    return (m_ptMin.X() > m_ptMax.X()) || (m_ptMin.Y() > m_ptMax.Y()) || (m_ptMin.Z() > m_ptMax.Z());
}

void iCAX::Math::Box3::Extend(const Point3& pt)
{
    if (IsEmpty())
    {
        m_ptMin = pt;
        m_ptMax = pt;
        return;
    }

    if (pt.X() < m_ptMin.X()) m_ptMin.X() = pt.X();
    if (pt.Y() < m_ptMin.Y()) m_ptMin.Y() = pt.Y();
    if (pt.Z() < m_ptMin.Z()) m_ptMin.Z() = pt.Z();

    if (pt.X() > m_ptMax.X()) m_ptMax.X() = pt.X();
    if (pt.Y() > m_ptMax.Y()) m_ptMax.Y() = pt.Y();
    if (pt.Z() > m_ptMax.Z()) m_ptMax.Z() = pt.Z();
}

//!< 判断一个点是否位于包围盒内部
bool iCAX::Math::Box3::Contains(IN const Point3& pt) const
{
    return (pt.X() >= m_ptMin.X() && pt.X() <= m_ptMax.X()) &&
        (pt.Y() >= m_ptMin.Y() && pt.Y() <= m_ptMax.Y()) &&
        (pt.Z() >= m_ptMin.Z() && pt.Z() <= m_ptMax.Z());
}

//!< 合并两个包围盒，生成包含二者的新包围盒
iCAX::Math::Box3 iCAX::Math::Box3::Union(const Box3& a, const Box3& b)
{
    if (a.IsEmpty()) return b;
    if (b.IsEmpty()) return a;

    Point3 _ptNewMin
    (
        min(a.m_ptMin.X(), b.m_ptMin.X()),
        min(a.m_ptMin.Y(), b.m_ptMin.Y()),
        min(a.m_ptMin.Z(), b.m_ptMin.Z())
    );

    Point3 _ptNewMax
    (
        max(a.m_ptMax.X(), b.m_ptMax.X()),
        max(a.m_ptMax.Y(), b.m_ptMax.Y()),
        max(a.m_ptMax.Z(), b.m_ptMax.Z())
    );

    return Box3(_ptNewMin, _ptNewMax);
}
