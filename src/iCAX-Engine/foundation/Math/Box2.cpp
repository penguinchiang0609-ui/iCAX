#include "pch.h"
#include "Box2.h"

//!< 构造函数
iCAX::Math::Box2::Box2()
    : m_ptMin(1, 1)
    , m_ptMax(-1, -1)
{
}

iCAX::Math::Box2::Box2(const Point2& minPt, const Point2& maxPt)
    : m_ptMin(minPt), m_ptMax(maxPt)
{
}
//!< 最小点
const iCAX::Math::Point2& iCAX::Math::Box2::MinPoint() const
{
    return m_ptMin;
}
//!< 最大点
const iCAX::Math::Point2& iCAX::Math::Box2::MaxPoint() const
{
    return m_ptMax;
}

iCAX::Math::Point2 iCAX::Math::Box2::Center() const
{
    return (m_ptMin.XY() + m_ptMax.XY()) * 0.5;
}

//!< 获取每个面的边长
Double2 iCAX::Math::Box2::Size() const
{
    return m_ptMax.XY() - m_ptMin.XY();
}

//!< 是否为空
bool iCAX::Math::Box2::IsEmpty() const
{
    if (m_ptMin.IsEqual(m_ptMax))
    {
        return false;
    }
    return (m_ptMin.X() > m_ptMax.X()) || (m_ptMin.Y() > m_ptMax.Y());
}

void iCAX::Math::Box2::Extend(const Point2& pt)
{
    if (IsEmpty())
    {
        m_ptMin = pt;
        m_ptMax = pt;
        return;
    }

    if (pt.X() < m_ptMin.X()) m_ptMin.X() = pt.X();
    if (pt.Y() < m_ptMin.Y()) m_ptMin.Y() = pt.Y();

    if (pt.X() > m_ptMax.X()) m_ptMax.X() = pt.X();
    if (pt.Y() > m_ptMax.Y()) m_ptMax.Y() = pt.Y();
}

//!< 判断一个点是否位于包围盒内部
bool iCAX::Math::Box2::Contains(IN const Point2& pt) const
{
    return (pt.X() >= m_ptMin.X() && pt.X() <= m_ptMax.X()) &&
        (pt.Y() >= m_ptMin.Y() && pt.Y() <= m_ptMax.Y());
}

//!< 合并两个包围盒，生成包含二者的新包围盒
iCAX::Math::Box2 iCAX::Math::Box2::Union(const Box2& a, const Box2& b)
{
    if (a.IsEmpty()) return b;
    if (b.IsEmpty()) return a;

    Point2 _ptNewMin
    (
        min(a.m_ptMin.X(), b.m_ptMin.X()),
        min(a.m_ptMin.Y(), b.m_ptMin.Y())
    );

    Point2 _ptNewMax
    (
        max(a.m_ptMax.X(), b.m_ptMax.X()),
        max(a.m_ptMax.Y(), b.m_ptMax.Y())
    );

    return Box2(_ptNewMin, _ptNewMax);
}
