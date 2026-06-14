#include "pch.h"
#include "HalfPlane2.h"
#include <cmath>
#include "../Math/Point2.h"


// 默认构造函数
iCAX::Physics::cHalfPlane2::cHalfPlane2()
    : m_csLocal(Point2(0, 0), Dir2(1, 0)) // 原点，法向量默认指向 X 方向
{
}

// 构造函数，指定直线上的一点和法向量
iCAX::Physics::cHalfPlane2::cHalfPlane2(IN const Point2& ptOnLine, IN const Dir2& normal)
{
    m_csLocal = CoordSys2(ptOnLine, Dir2(normal.Y(), -normal.X()));
}

// 获取直线上的一点
const Point2& iCAX::Physics::cHalfPlane2::Location() const
{
    return m_csLocal.Location();
}

// 获取法向量
const Dir2& iCAX::Physics::cHalfPlane2::Normal() const
{
    return m_csLocal.YDirection();
}

// 判断点是否在直线上（用容差避免浮点误差）
bool iCAX::Physics::cHalfPlane2::IsOn(IN const Point2& ptTargt_) const
{
    return std::fabs((ptTargt_ - m_csLocal.Location()).Dot(m_csLocal.YDirection().Vector())) < EPSILON;
}

// 判断点是否在半平面前方（法向量指向方向）
bool iCAX::Physics::cHalfPlane2::IsFront(IN const Point2& ptTargt_) const
{
    return (ptTargt_ - m_csLocal.Location()).Dot(m_csLocal.YDirection().Vector()) > 0;
}

// 判断点是否在半平面后方（法向量反方向）
bool iCAX::Physics::cHalfPlane2::IsBack(IN const Point2& ptTargt_) const
{
    return (ptTargt_ - m_csLocal.Location()).Dot(m_csLocal.YDirection().Vector()) < 0;
}
