#include "pch.h"
#include "Vector2.h"
#include "Tranform2.h"

//!< 构造函数
iCAX::Math::Vector2::Vector2()
    : m_nXY(0, 0)
{
}

//!< 构造函数
iCAX::Math::Vector2::Vector2(IN const Double2& nXY_)
    : m_nXY(nXY_)
{
}

//!< 构造函数
iCAX::Math::Vector2::Vector2(IN const double& nX_, IN const double& nY_)
    : m_nXY(nX_, nY_)
{
}

//!< 析构函数
iCAX::Math::Vector2::~Vector2()
{
}

//!< X
double& iCAX::Math::Vector2::X()
{
    return m_nXY[0];
}

//!< X
const double& iCAX::Math::Vector2::X() const
{
    return m_nXY[0];
}

//!< Y
double& iCAX::Math::Vector2::Y()
{
    return m_nXY[1];
}


//!< Y
const double& iCAX::Math::Vector2::Y() const
{
    return m_nXY[1];
}

//!< XY
Double2& iCAX::Math::Vector2::XY()
{
    return m_nXY;
}

//!< XY
const Double2& iCAX::Math::Vector2::XY() const
{
    return m_nXY;
}
//!< 判断是否为非法向量
bool iCAX::Math::Vector2::IsNil() const
{
    return std::isnan(X()) || std::isnan(Y());
}
//!< 点积
double iCAX::Math::Vector2::Dot(IN const Vector2& vOthevRHS_) const
{
    return X() * vOthevRHS_.X() + Y() * vOthevRHS_.Y();
}
//!< 叉积
double iCAX::Math::Vector2::Cross(IN const Vector2& vOthevRHS_) const
{
    return  X() * vOthevRHS_.Y() - Y() * vOthevRHS_.X();
}
//!< 模长
double iCAX::Math::Vector2::Magnitude() const
{
    return std::sqrt(X() * X() + Y() * Y());
}
//!< 模长平方
double iCAX::Math::Vector2::MagnitudeSquared() const
{
    return X() * X() + Y() * Y();
}
//!< 规范化
iCAX::Math::Vector2 iCAX::Math::Vector2::Normalized() const
{
    double len = Magnitude();
    if (len < EPSILON) return Vector2(0, 0);
    return{ X() / len, Y() / len };
}
//!< 规范化
iCAX::Math::Vector2& iCAX::Math::Vector2::Normalize()
{
    double len = Magnitude();
    if (len < EPSILON)
    {
        X() = 0;
        Y() = 0;
        return *this;
    }

    X() /= len; Y() /= len; 
    return *this;
}
//!< 夹角
double iCAX::Math::Vector2::Angle(IN const Vector2& vOthevRHS_) const
{
    double cosTheta = Dot(vOthevRHS_);
    double sinTheta = Cross(vOthevRHS_);
    if (cosTheta > -0.70710678118655 && cosTheta < 0.70710678118655) {
        return sinTheta >= 0.0 ? std::acos(cosTheta) : -std::acos(cosTheta);
    }
    else {
        if (cosTheta > 0.0)
            return std::asin(sinTheta);
        else
            return sinTheta >= 0.0 ? M_PI - std::asin(sinTheta) : -M_PI - std::asin(sinTheta);
    }
}
//!< 垂直向量，逆时针旋转90度
iCAX::Math::Vector2 iCAX::Math::Vector2::Perpendicular() const
{
    return { -Y(), X() };
}
//!< 获取是否单位化
bool iCAX::Math::Vector2::IsNormalized(IN const double& nTol_) const
{
    double len = MagnitudeSquared();
    return fabs(len - 1) <= nTol_ * nTol_;
}
//!< 是否零长
bool iCAX::Math::Vector2::IsZero(IN const double& nTol_) const
{
    return MagnitudeSquared() <= nTol_ * nTol_;
}
//!< 是否相等
bool iCAX::Math::Vector2::IsEqual(IN const Vector2& vOthevRHS_, IN const double& nTol_) const
{
    return std::fabs(X() - vOthevRHS_.X()) <= nTol_ &&
        std::fabs(Y() - vOthevRHS_.Y()) <= nTol_;
}
//!< 是否平行
bool iCAX::Math::Vector2::IsParallel(IN const Vector2& vOthevRHS_, IN const double& tol) const
{
    double cross = Cross(vOthevRHS_);
    return fabs(cross) <= tol;
}
//!< 是否同向
bool iCAX::Math::Vector2::IsCodirectional(IN const Vector2& vOthevRHS_, IN const double& tol) const
{
    return IsParallel(vOthevRHS_, tol) && (Dot(vOthevRHS_) > 0);
}
//!< 是否反向
bool iCAX::Math::Vector2::IsOpposite(IN const Vector2& vOthevRHS_, IN const double& tol) const
{
    // 平行且点积为负 → 反向
    return IsParallel(vOthevRHS_, tol) && (Dot(vOthevRHS_) < 0);
}
//!< 是否垂直
bool iCAX::Math::Vector2::IsPerpendicular(IN const Vector2& vOthevRHS_, IN const double& tol) const
{
    return std::fabs(Dot(vOthevRHS_)) <= tol;
}

//!< 生成取反方向的新向量
iCAX::Math::Vector2 iCAX::Math::Vector2::Reversed() const
{
    return -*this;
}

//!< 当前向量取反
iCAX::Math::Vector2& iCAX::Math::Vector2::Reverse()
{
    m_nXY = -m_nXY;
    return *this;
}

//!< 获取零向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Zero()
{
    static Vector2 _Zero(0, 0);
    return _Zero;
}
//!< 获取左向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Left()
{
    static Vector2 _Left(-1, 0);
    return _Left;
}
//!< 获取右向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Right()
{
    static Vector2 _Right(1, 0);
    return _Right;
}
//!< 获取上向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Up()
{
    static Vector2 _Up(0, 1);
    return _Up;
}
//!< 获取下向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Down()
{
    static Vector2 _Down(0, -1);
    return _Down;
}
//!< 获取(1,1)向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::One()
{
    static Vector2 _One(1, 1);
    return _One;
}

//!< 获取非法向量
const iCAX::Math::Vector2& iCAX::Math::Vector2::Nil()
{
    static const Vector2 _Nil
    (
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    );
    return _Nil;
}

//!< 加
iCAX::Math::Vector2 iCAX::Math::operator+(IN const iCAX::Math::Vector2& vLHS_, IN const iCAX::Math::Vector2& vRHS_)
{
    return Vector2(vLHS_.XY() + vRHS_.XY());
}
//!< 减
iCAX::Math::Vector2 iCAX::Math::operator-(IN const Vector2& vLHS_, IN const Vector2& vRHS_)
{
    return Vector2(vLHS_.XY() - vRHS_.XY());
}
//!< 点乘
double iCAX::Math::operator*(IN const Vector2& vLHS_, IN const Vector2& vRHS_)
{
    return vLHS_.Dot(vRHS_);
}
//!< 叉乘
double iCAX::Math::operator^(IN const Vector2& vLHS_, IN const Vector2& vRHS_)
{
    return vLHS_.Cross(vRHS_);
}
//!< v * 标量
iCAX::Math::Vector2 iCAX::Math::operator*(IN const Vector2& vLHS_, IN const double& n_)
{
    return Vector2(vLHS_.XY() * n_);
}
//!< 标量 * v
iCAX::Math::Vector2 iCAX::Math::operator*(IN const double& n_, IN const Vector2& vRHS_)
{
    return Vector2(vRHS_.XY() * n_);
}
//!< v / 标量
iCAX::Math::Vector2 iCAX::Math::operator/(IN const Vector2& vLHS_, IN const double& n_)
{
    return Vector2(vLHS_.XY() / n_);
}
//!< 加=
iCAX::Math::Vector2& iCAX::Math::operator+=(IN OUT Vector2& vLHS_, IN const Vector2& vRHS_)
{
    vLHS_.XY() += vRHS_.XY();
    return vLHS_;
}
//!< 减=
iCAX::Math::Vector2& iCAX::Math::operator-=(IN OUT Vector2& vLHS_, IN const Vector2& vRHS_)
{
    vLHS_.XY() -= vRHS_.XY();
    return vLHS_;
}
//!< v * 标量=
iCAX::Math::Vector2& iCAX::Math::operator*=(IN OUT Vector2& vLHS_, IN const double& n_)
{
    vLHS_.XY() *= n_;
    return vLHS_;
}
//!< v / 标量=
iCAX::Math::Vector2& iCAX::Math::operator/=(IN OUT Vector2& vLHS_, IN const double& n_)
{
    vLHS_.XY() /= n_;
    return vLHS_;
}
//!< 取反
iCAX::Math::Vector2 iCAX::Math::operator-(IN const Vector2& vLHS_)
{
    return Vector2(-vLHS_.XY());
}
//!< ==
bool iCAX::Math::operator==(IN const Vector2& vLHS_, IN const Vector2& vRHS_)
{
    return fabs(vLHS_.X() - vRHS_.X()) < EPSILON && fabs(vLHS_.Y() - vRHS_.Y()) < EPSILON;
}
//!< <
bool iCAX::Math::operator<(IN const Vector2& vLHS_, IN const Vector2& vRHS_)
{
    if (fabs(vLHS_.X() - vRHS_.X()) >= EPSILON)
        return vLHS_.X() < vRHS_.X();
    return vLHS_.Y() < vRHS_.Y();
}
