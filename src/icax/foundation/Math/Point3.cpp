#include "pch.h"
#include "Point3.h"

//!< 默认构造函数
iCAX::Math::Point3::Point3()
    : m_nXYZ(0, 0, 0)
{
}

//!< 使用 Double3 构造
iCAX::Math::Point3::Point3(IN const Double3& nXYZ_)
    : m_nXYZ(nXYZ_)
{
}

//!< 使用 X, Y, Z 构造
iCAX::Math::Point3::Point3(IN const double& nX_, IN const double& nY_, IN const double& nZ_)
    : m_nXYZ(nX_, nY_, nZ_)
{
}

//!< X分量
double& iCAX::Math::Point3::X()
{
    return m_nXYZ[0];
}
//!< X分量（常量版本）
const double& iCAX::Math::Point3::X() const
{
    return m_nXYZ[0];
}
//!< Y分量
double& iCAX::Math::Point3::Y()
{
    return m_nXYZ[1];
}
//!< Y分量（常量版本）
const double& iCAX::Math::Point3::Y() const
{
    return m_nXYZ[1];
}
//!< Z分量
double& iCAX::Math::Point3::Z()
{
    return m_nXYZ[2];
}
//!< Z分量（常量版本）
const double& iCAX::Math::Point3::Z() const
{
    return m_nXYZ[2];
}
//!< XYZ
Double3& iCAX::Math::Point3::XYZ()
{
    return m_nXYZ;
}
//!< XYZ（常量版本）
const Double3& iCAX::Math::Point3::XYZ() const
{
    return m_nXYZ;
}

//!< 判断是否为非法点
bool iCAX::Math::Point3::IsNil() const
{
    return std::isnan(X()) || std::isnan(Y()) || std::isnan(Z());
}


//!< 点的线性插值
iCAX::Math::Point3 iCAX::Math::Point3::Lerp(IN const Point3& ptTarget_, IN const double& nFactor_) const
{
    Vector3 _vOffset = ptTarget_ - *this;
    return *this + _vOffset * nFactor_;
}

//!< 判断点是否相等（带容差）
bool iCAX::Math::Point3::IsEqual(IN const Point3& ptOther_, IN const double& nTol_) const
{
    return DistanceSquared(ptOther_) <= nTol_ * nTol_;
}

//!< 两点之间的欧氏距离
double iCAX::Math::Point3::Distance(IN const Point3& ptOther_) const
{
    double _nDX = X() - ptOther_.X();
    double _nDY = Y() - ptOther_.Y();
    double _nDZ = Z() - ptOther_.Z();
    return sqrt(_nDX * _nDX + _nDY * _nDY + _nDZ * _nDZ);
}

//!< 两点之间的平方距离
double iCAX::Math::Point3::DistanceSquared(IN const Point3& ptOther_) const
{
    double _nDX = X() - ptOther_.X();
    double _nDY = Y() - ptOther_.Y();
    double _nDZ = Z() - ptOther_.Z();
    return _nDX * _nDX + _nDY * _nDY + _nDZ * _nDZ;
}

//!< 按权重组合两个点
iCAX::Math::Point3 iCAX::Math::Point3::Combine(IN const Point3& ptOther_, IN const double& nWeight1_, IN const double& nWeight2_) const
{
    return Point3(
        X() * nWeight1_ + ptOther_.X() * nWeight2_,
        Y() * nWeight1_ + ptOther_.Y() * nWeight2_,
        Z() * nWeight1_ + ptOther_.Z() * nWeight2_
    );
}

//!< 获取零点 (0, 0, 0)
const iCAX::Math::Point3& iCAX::Math::Point3::Zero()
{
    static Point3 s_ptLocal(0, 0, 0);
    return s_ptLocal;
}

//!< 获取一 (1, 1, 1)
const iCAX::Math::Point3& iCAX::Math::Point3::One()
{
    static Point3 s_ptLocal(1, 1, 1);
    return s_ptLocal;
}

//!< 获取右 (1, 0, 0)
const iCAX::Math::Point3& iCAX::Math::Point3::Right()
{
    static Point3 s_ptLocal(1, 0, 0);
    return s_ptLocal;
}

//!< 获取左 (-1, 0, 0)
const iCAX::Math::Point3& iCAX::Math::Point3::Left()
{
    static Point3 s_ptLocal(-1, 0, 0);
    return s_ptLocal;
}

//!< 获取上 (0, 0, 1)
const iCAX::Math::Point3& iCAX::Math::Point3::Up()
{
    static Point3 s_ptLocal(0, 0, 1);
    return s_ptLocal;
}

//!< 获取下 (0, 0, -1)
const iCAX::Math::Point3& iCAX::Math::Point3::Down()
{
    static Point3 s_ptLocal(0, 0, -1);
    return s_ptLocal;
}

//!< 获取前 (0, 1, 0)
const iCAX::Math::Point3& iCAX::Math::Point3::Forward()
{
    static Point3 s_ptLocal(0, 1, 0);
    return s_ptLocal;
}

//!< 获取后 (0, -1, 0)
const iCAX::Math::Point3& iCAX::Math::Point3::Back()
{
    static Point3 s_ptLocal(0, -1, 0);
    return s_ptLocal;
}

//!< 获取非法向量
const iCAX::Math::Point3& iCAX::Math::Point3::Nil()
{
    static const Point3 _Nil
    (
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    );
    return _Nil;
}


//!< 点 + 向量 = 点
iCAX::Math::Point3 iCAX::Math::operator+(IN const Point3& ptLHS_, IN const Vector3& vOffset_)
{
    return Point3(ptLHS_.XYZ() + vOffset_.XYZ());
}

//!< 点 - 向量 = 点
iCAX::Math::Point3 iCAX::Math::operator-(IN const Point3& ptLHS_, IN const Vector3& vOffset_)
{
    return Point3(ptLHS_.XYZ() - vOffset_.XYZ());
}

//!< 点 - 点 = 向量
iCAX::Math::Vector3 iCAX::Math::operator-(IN const Point3& ptLHS_, IN const Point3& ptRHS_)
{
    return Vector3(ptLHS_.X() - ptRHS_.X(), ptLHS_.Y() - ptRHS_.Y(), ptLHS_.Z() - ptRHS_.Z());
}

//!< 点 += 向量
iCAX::Math::Point3& iCAX::Math::operator+=(IN OUT Point3& ptLHS_, IN const Vector3& vOffset_)
{
    ptLHS_.XYZ() += vOffset_.XYZ();
    return ptLHS_;
}

//!< 点 -= 向量
iCAX::Math::Point3& iCAX::Math::operator-=(IN OUT Point3& ptLHS_, IN const Vector3& vOffset_)
{
    ptLHS_.XYZ() -= vOffset_.XYZ();
    return ptLHS_;
}

//!< 相等比较
bool iCAX::Math::operator==(IN const Point3& ptLHS_, IN const Point3& ptRHS_)
{
    return std::fabs(ptLHS_.X() - ptRHS_.X()) < EPSILON &&
        std::fabs(ptLHS_.Y() - ptRHS_.Y()) < EPSILON &&
        std::fabs(ptLHS_.Z() - ptRHS_.Z()) < EPSILON;
}

//!< 小于比较（字典序）
bool iCAX::Math::operator<(IN const Point3& ptLHS_, IN const Point3& ptRHS_)
{
    if (std::fabs(ptLHS_.X() - ptRHS_.X()) > EPSILON) return ptLHS_.X() < ptRHS_.X();
    if (std::fabs(ptLHS_.Y() - ptRHS_.Y()) > EPSILON) return ptLHS_.Y() < ptRHS_.Y();
    return ptLHS_.Z() < ptRHS_.Z();
}
