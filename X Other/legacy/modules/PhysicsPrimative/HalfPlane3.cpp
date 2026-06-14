#include "pch.h"
#include "HalfPlane3.h"

// 默认构造，平面经过原点，法向量指向 Z+
iCAX::Physics::cHalfPlane3::cHalfPlane3()
    : m_csLocal()
{
}

// 构造函数，指定平面上一点和法向量
iCAX::Physics::cHalfPlane3::cHalfPlane3(IN const Point3& ptLocation_, IN const Dir3& normal)
{
    Vector3 _vRef;

    // 稳健参考向量选择策略
    if (fabs(normal.Z()) > 0.707)
        _vRef = Vector3(1, 0, 0);  // Z轴接近竖直时，用X轴参考
    else
        _vRef = Vector3(0, 0, 1);  // 否则用Z轴参考

    // 构造正交坐标系
    Vector3 _vX = (_vRef ^ normal.Vector()).Normalized(); // X = _vRef × Z
    Vector3 _vY = (normal.Vector() ^ _vX).Normalized();  // Y = Z × X

    // 初始化局部坐标系
    m_csLocal = CoordSys3(ptLocation_, Dir3(_vX), Dir3(_vY));

}

//!< location
const Point3& iCAX::Physics::cHalfPlane3::Location() const
{
    return m_csLocal.Location();
}

//!< normal
const Dir3& iCAX::Physics::cHalfPlane3::Normal() const
{
    return m_csLocal.ZDirection();
}
//!< 判断点是否在平面上
bool iCAX::Physics::cHalfPlane3::IsOn(IN const Point3& pt) const
{
    return (abs((pt - m_csLocal.Location()).Dot(m_csLocal.ZDirection().Vector())) <= EPSILON);
}
//!< 判断点是否在平面前方
bool iCAX::Physics::cHalfPlane3::IsFront(IN const Point3& pt) const
{
    return (pt - m_csLocal.Location()).Dot(m_csLocal.ZDirection().Vector()) > 0;
}
//!< 判断点是否在平面后方
bool iCAX::Physics::cHalfPlane3::IsBack(IN const Point3& pt) const
{
    return (pt - m_csLocal.Location()).Dot(m_csLocal.ZDirection().Vector()) < 0;
}

