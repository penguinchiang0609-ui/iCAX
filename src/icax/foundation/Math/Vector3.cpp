#include "pch.h"
#include "Vector3.h"

//!< 构造函数
iCAX::Math::Vector3::Vector3()
    : m_nXYZ(0, 0, 0)
{
}

//!< 构造函数
iCAX::Math::Vector3::Vector3(IN const Double3& nXYZ_)
    : m_nXYZ(nXYZ_)
{
}

//!< 构造函数
iCAX::Math::Vector3::Vector3(IN const double& nX_, IN const double& nY_, IN const double& nZ_)
    : m_nXYZ(nX_, nY_, nZ_)
{
}

//!< 析构函数
iCAX::Math::Vector3::~Vector3()
{
}

//!< 获取X
double& iCAX::Math::Vector3::X()
{
    return m_nXYZ[0];
}

//!< 获取X
const double& iCAX::Math::Vector3::X() const
{
    return m_nXYZ[0];
}

//!< 获取Y
double& iCAX::Math::Vector3::Y()
{
    return m_nXYZ[1];
}


//!< 获取Y
const double& iCAX::Math::Vector3::Y() const
{
    return m_nXYZ[1];
}


//!< 获取Z
double& iCAX::Math::Vector3::Z()
{
    return m_nXYZ[2];
}


//!< 获取Z
const double& iCAX::Math::Vector3::Z() const
{
    return m_nXYZ[2];
}

//!< 获取XYZ
Double3& iCAX::Math::Vector3::XYZ()
{
    return m_nXYZ;
}

//!< 获取XYZ
const Double3& iCAX::Math::Vector3::XYZ() const
{
    return m_nXYZ;
}
//!< 判断是否为非法向量
bool iCAX::Math::Vector3::IsNil() const
{
    return std::isnan(X()) || std::isnan(Y()) || std::isnan(Z());
}
//!< 点积
double iCAX::Math::Vector3::Dot(IN const Vector3& vOther_) const
{
    return X() * vOther_.X() + Y() * vOther_.Y() + Z() * vOther_.Z();
}
//!< 叉积
iCAX::Math::Vector3 iCAX::Math::Vector3::Cross(IN const Vector3& vOther_) const
{
    return Vector3(
        m_nXYZ[1] * vOther_.m_nXYZ[2] - m_nXYZ[2] * vOther_.m_nXYZ[1],
        m_nXYZ[2] * vOther_.m_nXYZ[0] - m_nXYZ[0] * vOther_.m_nXYZ[2],
        m_nXYZ[0] * vOther_.m_nXYZ[1] - m_nXYZ[1] * vOther_.m_nXYZ[0]
    );
}
//!< 模长
double iCAX::Math::Vector3::Magnitude() const
{
    return std::sqrt(X() * X() + Y() * Y() + Z() * Z());
}
//!< 模长平方
double iCAX::Math::Vector3::MagnitudeSquared() const
{
    return std::sqrt(X() * X() + Y() * Y() + Z() * Z());
}
//!< 单位化后的向量
iCAX::Math::Vector3 iCAX::Math::Vector3::Normalized() const
{
    double _nLen = Magnitude();
    if (_nLen < EPSILON) return Vector3(0, 0, 0);
    return{ X() / _nLen, Y() / _nLen , Z() / _nLen};
}
//!< 将向量单位化
iCAX::Math::Vector3& iCAX::Math::Vector3::Normalize()
{
    double _nLen = Magnitude();
    if (_nLen < EPSILON)
    {
        X() = 0;
        Y() = 0;
        Z() = 0;
        return *this;
    }
    X() /= _nLen; Y() /= _nLen; Z() /= _nLen;
    return *this;
}
//!< 与另一个向量的夹角
double iCAX::Math::Vector3::Angle(IN const Vector3& vOther_) const
{
    double _nDot = Dot(vOther_);
    double _nLen = Magnitude() * vOther_.Magnitude();
    // 零向量直接返回 0
    if (_nLen < EPSILON)
        return 0.0;

    double _nCosTheta = Dot(vOther_) / _nLen;
    if (_nCosTheta > 1.0) _nCosTheta = 1.0;
    else if (_nCosTheta < -1.0) _nCosTheta = -1.0;

    return (_nLen < EPSILON) ? 0.0 : acos(_nCosTheta);
}
//!< 获取是否单位化
bool iCAX::Math::Vector3::IsNormalized(IN const double& nTol_) const
{
    double _nLen = MagnitudeSquared();
    return fabs(_nLen - 1) <= nTol_ * nTol_;
}
//!< 判断是否为零向量
bool iCAX::Math::Vector3::IsZero(IN const double& nTol_) const
{
    return MagnitudeSquared() <= nTol_ * nTol_;
}
//!< 判断两个向量是否相等
bool iCAX::Math::Vector3::IsEqual(IN const Vector3& vOther_, IN const double& nTol_) const
{
    return std::fabs(X() - vOther_.X()) <= nTol_ &&
        std::fabs(Y() - vOther_.Y()) <= nTol_ &&
        std::fabs(Z() - vOther_.Z()) <= nTol_;
}
//!< 是否平行
bool iCAX::Math::Vector3::IsParallel(IN const Vector3& vOther_, IN const double& nTol_) const
{
    Vector3 _vCross = Cross(vOther_);
    return _vCross.IsZero(nTol_);
}
//!< 是否同向
bool iCAX::Math::Vector3::IsCodirectional(IN const Vector3& vOther_, IN const double& nTol_) const
{
    return IsParallel(vOther_, nTol_) && (Dot(vOther_) > 0);
}
//!< 是否反向
bool iCAX::Math::Vector3::IsOpposite(IN const Vector3& vOther_, IN const double& nTol_) const
{
    // 平行且点积为负 → 反向
    return IsParallel(vOther_, nTol_) && (Dot(vOther_) < 0);
}
//!< 是否垂直
bool iCAX::Math::Vector3::IsPerpendicular(IN const Vector3& vOther_, IN const double& nTol_) const
{
    return std::fabs(Dot(vOther_)) <= nTol_;
}
//!< 生成取反方向的新向量
iCAX::Math::Vector3 iCAX::Math::Vector3::Reversed() const
{
    return -*this;
}

//!< 当前向量取反
iCAX::Math::Vector3& iCAX::Math::Vector3::Reverse()
{
    m_nXYZ = -m_nXYZ;
    return *this;
}
//!< 获取零向量
const iCAX::Math::Vector3& iCAX::Math::Vector3::Zero()
{
    static Vector3 s_vZero(0, 0, 0);
    return s_vZero;
}
//!< 获取 (1, 1, 1) 向量
const iCAX::Math::Vector3& iCAX::Math::Vector3::One()
{
    static Vector3 s_vOne(1, 1, 1);
    return s_vOne;
}
//!< 获取右向量 (1, 0, 0)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Right()
{
    static Vector3 s_vRight(1, 0, 0);
    return s_vRight;
}
//!< 获取左向量 (-1, 0, 0)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Left()
{
    static Vector3 s_vRight(-1, 0, 0);
    return s_vRight;
}
//!< 获取上向量 (0, 0, 1)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Up()
{
    static Vector3 s_vUp(0, 0, 1);
    return s_vUp;
}
//!< 获取下向量 (0, 0, 1)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Down()
{
    static Vector3 s_vDown(0, 0, -1);
    return s_vDown;
}
//!< 获取前向量 (0, 1, 0)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Forward()
{
    static Vector3 s_vForward(0, 1, 0);
    return s_vForward;
}
//!< 获取后向量 (0, -1, 0)
const iCAX::Math::Vector3& iCAX::Math::Vector3::Back()
{
    static Vector3 s_vBack(0, -1, 0);
    return s_vBack;
}
//!< 获取非法向量
const iCAX::Math::Vector3& iCAX::Math::Vector3::Nil()
{
    static const Vector3 _Nil
    (
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    );
    return _Nil;
}

//!< 向量加
iCAX::Math::Vector3 iCAX::Math::operator+(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    return Vector3(vLHS_.XYZ() + vRHS_.XYZ());
}
//!< 向量减
iCAX::Math::Vector3 iCAX::Math::operator-(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    return Vector3(vLHS_.XYZ() - vRHS_.XYZ());
}
//!< 向量点乘
double iCAX::Math::operator*(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    return vLHS_.Dot(vRHS_);
}
//!< 向量叉乘
iCAX::Math::Vector3 iCAX::Math::operator^(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    return vLHS_.Cross(vRHS_);
}
//!< 向量 * 标量
iCAX::Math::Vector3 iCAX::Math::operator*(IN const Vector3& vLHS_, IN const double& nFactor_)
{
    return Vector3(vLHS_.XYZ() * nFactor_);
}
//!< 标量 * 向量
iCAX::Math::Vector3 iCAX::Math::operator*(IN const double& nFactor_, IN const Vector3& vRHS_)
{
    return Vector3(vRHS_.XYZ() * nFactor_);
}
//!< 向量 / 标量
iCAX::Math::Vector3 iCAX::Math::operator/(IN const Vector3& vLHS_, IN const double& nFactor_)
{
    return Vector3(vLHS_.XYZ() / nFactor_);
}
//!< 向量加=
iCAX::Math::Vector3& iCAX::Math::operator+=(IN Vector3& vLHS_, IN const Vector3& vRHS_)
{
    vLHS_.XYZ() += vRHS_.XYZ();
    return vLHS_;
}
//!< 向量减=
iCAX::Math::Vector3& iCAX::Math::operator-=(IN Vector3& vLHS_, IN const Vector3& vRHS_)
{
    vLHS_.XYZ() -= vRHS_.XYZ();
    return vLHS_;
}
//!< 向量 * 标量=
iCAX::Math::Vector3& iCAX::Math::operator*=(IN Vector3& vLHS_, IN const double& nFactor_)
{
    vLHS_.XYZ() *= nFactor_;
    return vLHS_;
}
//!< 向量 / 标量=
iCAX::Math::Vector3& iCAX::Math::operator/=(IN Vector3& vLHS_, IN const double& nFactor_)
{
    vLHS_.XYZ() /= nFactor_;
    return vLHS_;

}
//!< 取反
iCAX::Math::Vector3 iCAX::Math::operator-(IN const Vector3& vLHS_)
{
    return Vector3(-vLHS_.XYZ());
}

//!< ==
bool iCAX::Math::operator==(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    return std::fabs(vLHS_.X() - vRHS_.X()) < EPSILON &&
        std::fabs(vLHS_.Y() - vRHS_.Y()) < EPSILON &&
        std::fabs(vLHS_.Z() - vRHS_.Z()) < EPSILON;
}
//!< 小于比较（用于排序/映射）
bool iCAX::Math::operator<(IN const Vector3& vLHS_, IN const Vector3& vRHS_)
{
    if (std::fabs(vLHS_.X() - vRHS_.X()) > EPSILON) return vLHS_.X() < vRHS_.X();
    if (std::fabs(vLHS_.Y() - vRHS_.Y()) > EPSILON) return vLHS_.Y() < vRHS_.Y();
    return vLHS_.Z() < vRHS_.Z();
}
