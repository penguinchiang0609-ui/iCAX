#include "pch.h"
#include "Vector4.h"

//!< 构造函数
iCAX::Math::Vector4::Vector4()
    : m_nXYZW(0, 0, 0, 0)
{
}

//!< 构造函数
iCAX::Math::Vector4::Vector4(IN const Double4& nXYZW_)
    : m_nXYZW(nXYZW_)
{
}

//!< 构造函数
iCAX::Math::Vector4::Vector4(IN const double& nX_, IN const double& nY_, IN const double& nZ_, IN const double& nW_)
    : m_nXYZW(nX_, nY_, nZ_, nW_)
{
}

//!< 析构函数
iCAX::Math::Vector4::~Vector4()
{
}

double& iCAX::Math::Vector4::X()
{
    return m_nXYZW[0];
}
const double& iCAX::Math::Vector4::X() const
{
    return m_nXYZW[0];
}
double& iCAX::Math::Vector4::Y()
{
    return m_nXYZW[1];
}
const double& iCAX::Math::Vector4::Y() const
{
    return m_nXYZW[1];
}
double& iCAX::Math::Vector4::Z()
{
    return m_nXYZW[2];
}
const double& iCAX::Math::Vector4::Z() const
{
    return m_nXYZW[2];
}
double& iCAX::Math::Vector4::W()
{
    return m_nXYZW[3];
}
const double& iCAX::Math::Vector4::W() const
{
    return m_nXYZW[3];
}

//!< 获取XYZ
Double4& iCAX::Math::Vector4::XYZW()
{
    return m_nXYZW;
}

//!< 获取XYZ
const Double4& iCAX::Math::Vector4::XYZW() const
{
    return m_nXYZW;
}

//!< 获取模长
double iCAX::Math::Vector4::Magnitude() const
{
    return sqrt(MagnitudeSquared());
}

//!< 获取模长平方
double iCAX::Math::Vector4::MagnitudeSquared() const
{
    return X() * X() + Y() * Y() + Z() * Z() + W() * W();
}

//!< 归一化
iCAX::Math::Vector4 iCAX::Math::Vector4::Normalized() const
{
    double _nM = sqrt(m_nXYZW[0] * m_nXYZW[0] + m_nXYZW[1] * m_nXYZW[1] + m_nXYZW[2] * m_nXYZW[2] + m_nXYZW[3] * m_nXYZW[3]);

    if (ZERO_EQL(_nM))
    {
        return Vector4(0, 0, 0, 0);
    }

    return Vector4(X() / _nM, Y() / _nM, Z() / _nM, W() / _nM);
}

//!< 归一化
iCAX::Math::Vector4& iCAX::Math::Vector4::Normalize()
{
    double n = Magnitude();
    if (n < EPSILON)
    {
        X() = 0;
        Y() = 0;
        Z() = 0;
        W() = 0;
        return *this;
    }
    X() /= n;
    Y() /= n;
    Z() /= n;
    W() /= n;
    return *this;
}

//!< 获取是否单位化
bool iCAX::Math::Vector4::IsNormalized() const
{
    double _nM = sqrt(m_nXYZW[0] * m_nXYZW[0] + m_nXYZW[1] * m_nXYZW[1] + m_nXYZW[2] * m_nXYZW[2] + m_nXYZW[3] * m_nXYZW[3]);
    return DOUBLE_EQL(_nM, 1);
}

//!< == 运算符
bool iCAX::Math::operator==(IN const Vector4& vLHS_, IN const Vector4& vRHS_)
{
    return vLHS_.XYZW() == vRHS_.XYZW();
}

//!< < 运算符
bool iCAX::Math::operator<(IN const Vector4& vLHS_, IN const Vector4& vRHS_)
{
    return vLHS_.XYZW() < vRHS_.XYZW();
}
