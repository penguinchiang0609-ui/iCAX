#include "pch.h"
#include "Ray2.h"
#include <cmath>


//!< 构造函数
iCAX::Physics::cRay2::cRay2()
    : m_ax(), m_nLength(DBL_MAX)
{
}

//!< 构造函数
iCAX::Physics::cRay2::cRay2(IN const Point2& ptLocation_, IN const Dir2& dir_, IN const double& nLength_)
    : m_ax(ptLocation_, dir_), m_nLength(nLength_)
{
}

//!< 起点访问
Point2& iCAX::Physics::cRay2::Location()
{
    return m_ax.Location();
}
//!< 起点访问
const Point2& iCAX::Physics::cRay2::Location() const
{
    return m_ax.Location();
}
//!< 方向访问
Dir2& iCAX::Physics::cRay2::Direction()
{
    return m_ax.Direction();
}
//!< 方向访问
const Dir2& iCAX::Physics::cRay2::Direction() const
{
    return m_ax.Direction();
}
//!< 射线长度访问
double& iCAX::Physics::cRay2::Length()
{
    return m_nLength;
}
//!< 射线长度访问
const double& iCAX::Physics::cRay2::Length() const
{
    return m_nLength;
}
