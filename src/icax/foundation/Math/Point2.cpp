#include "pch.h"
#include "Point2.h"
#include "Vector2.h"
#include <cmath>

//!< 默认构造函数，初始化为 (0,0)
iCAX::Math::Point2::Point2()
    : m_nXY(0, 0)
{
}

//!< 构造函数，使用 Double2 初始化
iCAX::Math::Point2::Point2(IN const Double2& nXY_)
    : m_nXY(nXY_)
{
}

//!< 构造函数，使用 X, Y 初始化
iCAX::Math::Point2::Point2(IN const double& nX_, IN const double& nY_)
    : m_nXY(nX_, nY_)
{
}

//!< 析构函数
iCAX::Math::Point2::~Point2()
{
}

//!< 获取 X
double& iCAX::Math::Point2::X()
{
    return m_nXY[0];
}

//!< 获取 X（常量版本）
const double& iCAX::Math::Point2::X() const
{
    return m_nXY[0];
}

//!< 获取 Y
double& iCAX::Math::Point2::Y()
{
    return m_nXY[1];
}

//!< 获取 Y（常量版本）
const double& iCAX::Math::Point2::Y() const
{
    return m_nXY[1];
}

//!< 获取 XY
Double2& iCAX::Math::Point2::XY()
{
    return m_nXY;
}

//!< 获取 XY（常量版本）
const Double2& iCAX::Math::Point2::XY() const
{
    return m_nXY;
}

//!< 判断是否为非法点
bool iCAX::Math::Point2::IsNil() const
{
    return std::isnan(X()) || std::isnan(Y());
}

//!< 线性插值
iCAX::Math::Point2 iCAX::Math::Point2::Lerp(IN const Point2& ptOther_, IN const double& nFactor_) const
{
    Vector2 _vOffset = ptOther_ - *this;
    return *this + (_vOffset * nFactor_);
}

//!< 判断是否相等（带容差）
bool iCAX::Math::Point2::IsEqual(IN const Point2& ptOther_, IN const double& nTol_) const
{
    return (std::fabs(X() - ptOther_.X()) <= nTol_ && std::fabs(Y() - ptOther_.Y()) <= nTol_);
}

//!< 计算平方距离
double iCAX::Math::Point2::DistanceSquared(IN const Point2& ptOther_) const
{
    return (*this - ptOther_).MagnitudeSquared();
}

//!< 计算距离
double iCAX::Math::Point2::Distance(IN const Point2& ptOther_) const
{
    return (*this - ptOther_).Magnitude();
}

//!< 加权组合
iCAX::Math::Point2 iCAX::Math::Point2::Combine(IN const Point2& ptOther_, IN const double& nW1_, IN const double& nW2_) const
{
    return { X() * nW1_ + ptOther_.X() * nW2_, Y() * nW1_ + ptOther_.Y() * nW2_ };
}

//!< 返回零点 (0,0)
const iCAX::Math::Point2& iCAX::Math::Point2::Zero()
{
    static Point2 _Zero(0, 0);
    return _Zero;
}

//!< 返回点 (1,1)
const iCAX::Math::Point2& iCAX::Math::Point2::One()
{
    static Point2 _One(1, 1);
    return _One;
}

//!< 获取非法向量
const iCAX::Math::Point2& iCAX::Math::Point2::Nil()
{
    static const Point2 _Nil
    (
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    );
    return _Nil;
}


//!< 点 + 向量
iCAX::Math::Point2 iCAX::Math::operator+(IN const Point2& ptLHS_, IN const Vector2& vOffset_)
{
    return Point2(ptLHS_.XY() + vOffset_.XY());
}

//!< 点 - 向量
iCAX::Math::Point2 iCAX::Math::operator-(IN const Point2& ptLHS_, IN const Vector2& vOffset_)
{
    return Point2(ptLHS_.XY() - vOffset_.XY());
}

//!< 点 - 点 = 向量
iCAX::Math::Vector2 iCAX::Math::operator-(IN const Point2& ptLHS_, IN const Point2& ptRHS_)
{
    return Vector2(ptLHS_.XY() - ptRHS_.XY());
}

//!< 点加向量
iCAX::Math::Point2& iCAX::Math::operator+=(IN OUT Point2& ptLHS_, IN const Vector2& vOffset_)
{
    ptLHS_.XY() += vOffset_.XY();
    return ptLHS_;
}

//!< 点减向量
iCAX::Math::Point2& iCAX::Math::operator-=(IN OUT Point2& ptLHS_, IN const Vector2& vOffset_)
{
    ptLHS_.XY() -= vOffset_.XY();
    return ptLHS_;
}

//!< 相等比较
bool iCAX::Math::operator==(IN const Point2& ptLHS_, IN const Point2& ptRHS_)
{
    return ptLHS_.IsEqual(ptRHS_);
}

//!< 小于比较（用于 map 排序）
bool iCAX::Math::operator<(IN const Point2& ptLHS_, IN const Point2& ptRHS_)
{
    if (ptLHS_.X() < ptRHS_.X()) return true;
    if (ptLHS_.X() > ptRHS_.X()) return false;
    return ptLHS_.Y() < ptRHS_.Y();
}
