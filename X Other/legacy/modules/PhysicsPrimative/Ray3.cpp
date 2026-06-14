#include "pch.h"
#include "Ray3.h"
#include <cmath>

//!< 默认构造：起点原点，方向为 (0,0,1)，长度无限
iCAX::Physics::cRay3::cRay3()
    : m_ax()
    , m_nLength(DBL_MAX)
{
}

//!< 构造函数，指定起点、方向和长度
iCAX::Physics::cRay3::cRay3(IN const Point3& ptLocation_, IN const Dir3& dir_, IN const double& nLength_)
    : m_ax(ptLocation_, dir_)
    , m_nLength(nLength_)
{
}

//!< 起点访问
Point3& iCAX::Physics::cRay3::Location()
{
    return m_ax.Location();
}
//!< 起点访问
const Point3& iCAX::Physics::cRay3::Location() const
{
    return m_ax.Location();
}
//!< 方向访问
Dir3& iCAX::Physics::cRay3::Direction()
{
    return m_ax.Direction();
}
//!< 方向访问
const Dir3& iCAX::Physics::cRay3::Direction() const
{
    return m_ax.Direction();
}
//!< 长度访问
double& iCAX::Physics::cRay3::Length()
{
    return m_nLength;
}
//!< 长度访问
const double& iCAX::Physics::cRay3::Length() const
{
    return m_nLength;
}
