#include "pch.h"
#include "Quaternion.h"
#include <cmath>
#include "Axis3.h"
#include "CoordSys3.h"

//!< 默认构造函数
iCAX::Math::Quaternion::Quaternion()
    : m_nXYZW(0, 0, 0, 1)
{
}

//!< 从四元数向量构造
iCAX::Math::Quaternion::Quaternion(IN const Double4& nXYZW_)
    : m_nXYZW(nXYZW_)
{
}

//!< 从欧拉角构造（Z-Y-X 顺序）
iCAX::Math::Quaternion::Quaternion(IN const Double3& nEuler_)
{
    double _nCR = std::cos(nEuler_[0] * 0.5);
    double _nSR = std::sin(nEuler_[0] * 0.5);
    double _nCP = std::cos(nEuler_[1] * 0.5);
    double _nSP = std::sin(nEuler_[1] * 0.5);
    double _nCY = std::cos(nEuler_[2] * 0.5);
    double _nSY = std::sin(nEuler_[2] * 0.5);

    W() = _nCY * _nCP * _nCR + _nSY * _nSP * _nSR;
    X() = _nCY * _nCP * _nSR - _nSY * _nSP * _nCR;
    Y() = _nCY * _nSP * _nCR + _nSY * _nCP * _nSR;
    Z() = _nSY * _nCP * _nCR - _nCY * _nSP * _nSR;
}

//!< 从分量构造
iCAX::Math::Quaternion::Quaternion(IN const double& nX_, IN const double& nY_, IN const double& nZ_, IN const double& nW_)
    : m_nXYZW(nX_, nY_, nZ_, nW_)
{
}

//!< 从轴角构造
iCAX::Math::Quaternion::Quaternion(IN const Dir3& dir_, IN const double& nRad_)
{
    double _nHalfAngle = 0.5 * nRad_;
    double _nS = std::sin(_nHalfAngle);
    X() = dir_.X() * _nS;
    Y() = dir_.Y() * _nS;
    Z() = dir_.Z() * _nS;
    W() = std::cos(_nHalfAngle);
}

//!< 析构函数
iCAX::Math::Quaternion::~Quaternion()
{
}

//!< 成员访问
double& iCAX::Math::Quaternion::X() { return m_nXYZW[0]; }
const double& iCAX::Math::Quaternion::X() const { return m_nXYZW[0]; }
double& iCAX::Math::Quaternion::Y() { return m_nXYZW[1]; }
const double& iCAX::Math::Quaternion::Y() const { return m_nXYZW[1]; }
double& iCAX::Math::Quaternion::Z() { return m_nXYZW[2]; }
const double& iCAX::Math::Quaternion::Z() const { return m_nXYZW[2]; }
double& iCAX::Math::Quaternion::W() { return m_nXYZW[3]; }
const double& iCAX::Math::Quaternion::W() const { return m_nXYZW[3]; }
Double4& iCAX::Math::Quaternion::XYZW() { return m_nXYZW; }
const Double4& iCAX::Math::Quaternion::XYZW() const { return m_nXYZW; }

bool iCAX::Math::Quaternion::IsEqual(IN const Quaternion& qOther_, IN const double& nTol_) const
{
    return std::fabs(X() - qOther_.X()) <= nTol_ &&
        std::fabs(Y() - qOther_.Y()) <= nTol_ &&
        std::fabs(Z() - qOther_.Z()) <= nTol_ &&
        std::fabs(W() - qOther_.W()) <= nTol_;
}

//!< 四元数乘法（旋转复合）
iCAX::Math::Quaternion iCAX::Math::Quaternion::operator*(IN const Quaternion& qRHS_) const
{
    return Quaternion(
        W() * qRHS_.X() + X() * qRHS_.W() + Y() * qRHS_.Z() - Z() * qRHS_.Y(),
        W() * qRHS_.Y() - X() * qRHS_.Z() + Y() * qRHS_.W() + Z() * qRHS_.X(),
        W() * qRHS_.Z() + X() * qRHS_.Y() - Y() * qRHS_.X() + Z() * qRHS_.W(),
        W() * qRHS_.W() - X() * qRHS_.X() - Y() * qRHS_.Y() - Z() * qRHS_.Z()
    );
}

//!< 四元数乘法赋值
iCAX::Math::Quaternion& iCAX::Math::Quaternion::operator*=(IN const Quaternion& qRHS_)
{
    *this = *this * qRHS_;
    return *this;
}

//!< 取负（共轭方向）
iCAX::Math::Quaternion iCAX::Math::Quaternion::operator-() const
{
    return Quaternion(-X(), -Y(), -Z(), -W());
}

//!< 共轭
iCAX::Math::Quaternion iCAX::Math::Quaternion::Conjugate() const
{
    return Quaternion(-X(), -Y(), -Z(), W());
}

//!< 逆四元数
iCAX::Math::Quaternion iCAX::Math::Quaternion::Inverse() const
{
    double _nNormSq = MagnitudeSquared();
    if (_nNormSq < 1e-12) return Quaternion(); // 返回单位四元数
    auto _qConj = Conjugate();
    return Quaternion(_qConj.X() / _nNormSq, _qConj.Y() / _nNormSq, _qConj.Z() / _nNormSq, _qConj.W() / _nNormSq);
}

//!< 四元数模
double iCAX::Math::Quaternion::Magnitude() const
{
    return std::sqrt(MagnitudeSquared());
}

//!< 模平方
double iCAX::Math::Quaternion::MagnitudeSquared() const
{
    return X() * X() + Y() * Y() + Z() * Z() + W() * W();
}

//!< 获取单位化后的四元数
iCAX::Math::Quaternion iCAX::Math::Quaternion::Normalized() const
{
    double _nMag = Magnitude();
    if (_nMag < 1e-12) return Quaternion();
    return Quaternion(X() / _nMag, Y() / _nMag, Z() / _nMag, W() / _nMag);
}

//!< 单位化自身
iCAX::Math::Quaternion& iCAX::Math::Quaternion::Normalize()
{
    double _nMag = Magnitude();
    if (_nMag < 1e-12)
    {
        X() = 0; Y() = 0; Z() = 0; W() = 1;
        return *this;
    }
    X() /= _nMag; Y() /= _nMag; Z() /= _nMag; W() /= _nMag;
    return *this;
}

//!< 使用四元数旋转向量
iCAX::Math::Vector3 iCAX::Math::Quaternion::Applied(IN const Vector3& vTarget_) const
{
    Quaternion _qVec(vTarget_.X(), vTarget_.Y(), vTarget_.Z(), 0.0);
    Quaternion _qRes = *this * _qVec * Conjugate();
    return Vector3(_qRes.X(), _qRes.Y(), _qRes.Z());
}

//!< 使用四元数旋转点
iCAX::Math::Point3 iCAX::Math::Quaternion::Applied(IN const Point3& ptTarget_) const
{
    Vector3 _v(ptTarget_.X(), ptTarget_.Y(), ptTarget_.Z());
    Vector3 _r = Applied(_v);
    return Point3(_r.X(), _r.Y(), _r.Z());
}

//!< 应用变换到方向
iCAX::Math::Dir3 iCAX::Math::Quaternion::Applied(IN const Dir3& dirTarget_) const
{
    return Dir3(Applied(dirTarget_.Vector()));
}

//!< 转为旋转矩阵
iCAX::Math::Tranform3 iCAX::Math::Quaternion::ToTrsf3() const
{
    double _nXX = X() * X(), _nYY = Y() * Y(), _nZZ = Z() * Z();
    double _nXY = X() * Y(), _nXZ = X() * Z(), _nYZ = Y() * Z();
    double _nWX = W() * X(), _nWY = W() * Y(), _nWZ = W() * Z();

    return Tranform3(
        1 - 2 * (_nYY + _nZZ), 2 * (_nXY - _nWZ), 2 * (_nXZ + _nWY), 0,
        2 * (_nXY + _nWZ), 1 - 2 * (_nXX + _nZZ), 2 * (_nYZ - _nWX), 0,
        2 * (_nXZ - _nWY), 2 * (_nYZ + _nWX), 1 - 2 * (_nXX + _nYY), 0,
        0, 0, 0, 1
    );
}

iCAX::Math::Quaternion& iCAX::Math::Quaternion::FromMatrix(IN const iCAX::Data::Double3x3 mat_)
{
    // === Step 1: 反推欧拉角（X->Y->Z顺序） ===
// 注意 mat[row][col]：行主
    double theta_y = std::asin(mat_(0,2)); // Ry分量
    double cy = std::cos(theta_y);

    double theta_x = 0.0;
    double theta_z = 0.0;

    if (std::fabs(cy) > 1e-6) { // 非奇异
        theta_x = std::atan2(-mat_(1,2), mat_(2,2));
        theta_z = std::atan2(-mat_(0,1), mat_(0,0));
    }
    else {
        // Gimbal lock：Y = ±90°
        theta_x = std::atan2(mat_(2,1), mat_(1,1));
        theta_z = 0.0;
    }

    // === Step 2: 根据欧拉角生成四元数（顺序 X->Y->Z） ===
    double cx = std::cos(theta_x / 2.0);
    double sx = std::sin(theta_x / 2.0);
    double cy2 = std::cos(theta_y / 2.0);
    double sy = std::sin(theta_y / 2.0);
    double cz = std::cos(theta_z / 2.0);
    double sz = std::sin(theta_z / 2.0);

    W() = cx * cy2 * cz + sx * sy * sz;
    X() = sx * cy2 * cz - cx * sy * sz;
    Y() = cx * sy * cz + sx * cy2 * sz;
    Z() = cx * cy2 * sz - sx * sy * cz;

    // 归一化
    double len = std::sqrt(W() * W() + X() * X() + Y() * Y() + Z() * Z());
    if (len > EPSILON) 
    {
        W() /= len; X() /= len; Y() /= len; Z() /= len;
    }
    else
    {
        // 默认单位四元数
        W() = 1.0; X() = 0.0; Y() = 0.0; Z() = 0.0;
    }

    return *this;
}

//!< 球面插值（SLERP）
iCAX::Math::Quaternion iCAX::Math::Quaternion::Slerp(IN const Quaternion& qStart_, IN const Quaternion& qEnd_, IN const double& nFactor_)
{
    double _nCosTheta = qStart_.X() * qEnd_.X() + qStart_.Y() * qEnd_.Y() + qStart_.Z() * qEnd_.Z() + qStart_.W() * qEnd_.W();
    Quaternion _qEnd = qEnd_;
    if (_nCosTheta < 0.0)
    {
        _qEnd = -qEnd_;
        _nCosTheta = -_nCosTheta;
    }

    if (_nCosTheta > 0.9995) return Lerp(qStart_, _qEnd, nFactor_);

    double _nAngle = std::acos(_nCosTheta);
    double _nSinAngle = std::sin(_nAngle);
    double _nW1 = std::sin((1 - nFactor_) * _nAngle) / _nSinAngle;
    double _nW2 = std::sin(nFactor_ * _nAngle) / _nSinAngle;

    return Quaternion(
        qStart_.X() * _nW1 + _qEnd.X() * _nW2,
        qStart_.Y() * _nW1 + _qEnd.Y() * _nW2,
        qStart_.Z() * _nW1 + _qEnd.Z() * _nW2,
        qStart_.W() * _nW1 + _qEnd.W() * _nW2
    );
}

//!< 线性插值（LERP）
iCAX::Math::Quaternion iCAX::Math::Quaternion::Lerp(IN const Quaternion& qStart_, IN const Quaternion& qEnd_, IN const double& nFactor_)
{
    Quaternion _qRes(
        qStart_.X() * (1 - nFactor_) + qEnd_.X() * nFactor_,
        qStart_.Y() * (1 - nFactor_) + qEnd_.Y() * nFactor_,
        qStart_.Z() * (1 - nFactor_) + qEnd_.Z() * nFactor_,
        qStart_.W() * (1 - nFactor_) + qEnd_.W() * nFactor_
    );
    return _qRes.Normalized();
}

//!< 获取欧拉角
Double3 iCAX::Math::Quaternion::Euler() const
{
    double _nRoll = 0, _nPitch = 0, _nYaw = 0;

    double _nSinR = 2.0 * (W() * X() + Y() * Z());
    double _nCosR = 1.0 - 2.0 * (X() * X() + Y() * Y());
    _nRoll = std::atan2(_nSinR, _nCosR);

    double _nSinP = 2.0 * (W() * Y() - Z() * X());
    if (std::fabs(_nSinP) >= 1)
        _nPitch = std::copysign(M_PI / 2, _nSinP);
    else
        _nPitch = std::asin(_nSinP);

    double _nSinY = 2.0 * (W() * Z() + X() * Y());
    double _nCosY = 1.0 - 2.0 * (Y() * Y() + Z() * Z());
    _nYaw = std::atan2(_nSinY, _nCosY);

    return { _nRoll, _nPitch, _nYaw };
}

//!< == 运算符
bool iCAX::Math::operator==(IN const Quaternion& qLHS_, IN const Quaternion& qRHS_)
{
    return qLHS_.X() == qRHS_.X() &&
        qLHS_.Y() == qRHS_.Y() &&
        qLHS_.Z() == qRHS_.Z() &&
        qLHS_.W() == qRHS_.W();
}

//!< < 运算符
bool iCAX::Math::operator<(IN const Quaternion& qLHS_, IN const Quaternion& qRHS_)
{
    if (qLHS_.X() != qRHS_.X()) return qLHS_.X() < qRHS_.X();
    if (qLHS_.Y() != qRHS_.Y()) return qLHS_.Y() < qRHS_.Y();
    if (qLHS_.Z() != qRHS_.Z()) return qLHS_.Z() < qRHS_.Z();
    return qLHS_.W() < qRHS_.W();
}
