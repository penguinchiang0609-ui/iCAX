#include "pch.h"
#include "Tranform3.h"
#include <stdexcept>
#include <cmath>
#include "Dir3.h"
#include "Axis3.h"
#include "Quaternion.h"
#include "CoordSys2.h"
#include "Tranform2.h"

//!< 默认构造函数
iCAX::Math::Tranform3::Tranform3()
{
    m_Matrix =
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
}
//!< 按元素构造矩阵
iCAX::Math::Tranform3::Tranform3(IN const double& m00_, IN const double& m01_, IN const double& m02_, IN const double& m03_, 
    double m10_, IN const double& m11_, IN const double& m12_, IN const double& m13_, 
    double m20_, IN const double& m21_, IN const double& m22_, IN const double& m23_, 
    double m30_, IN const double& m31_, IN const double& m32_, IN const double& m33_)
{
    m_Matrix =
    {
        m00_, m01_, m02_, m03_,
        m10_, m11_, m12_, m13_,
        m20_, m21_, m22_, m23_,
        m30_, m31_, m32_, m33_
    };
}
//!< 从已有矩阵构造
iCAX::Math::Tranform3::Tranform3(IN const Double4x4& nMat_)
    : m_Matrix(nMat_)
{
}
//!< 析构函数
iCAX::Math::Tranform3::~Tranform3()
{
}

//!< 元素访问
double& iCAX::Math::Tranform3::m00() { return m_Matrix(0, 0); }
const double& iCAX::Math::Tranform3::m00() const { return m_Matrix(0, 0); }
double& iCAX::Math::Tranform3::m01() { return m_Matrix(0, 1); }
const double& iCAX::Math::Tranform3::m01() const { return m_Matrix(0, 1); }
double& iCAX::Math::Tranform3::m02() { return m_Matrix(0, 2); }
const double& iCAX::Math::Tranform3::m02() const { return m_Matrix(0, 2); }
double& iCAX::Math::Tranform3::m03() { return m_Matrix(0, 3); }
const double& iCAX::Math::Tranform3::m03() const { return m_Matrix(0, 3); }
double& iCAX::Math::Tranform3::m10() { return m_Matrix(1, 0); }
const double& iCAX::Math::Tranform3::m10() const { return m_Matrix(1, 0); }
double& iCAX::Math::Tranform3::m11() { return m_Matrix(1, 1); }
const double& iCAX::Math::Tranform3::m11() const { return m_Matrix(1, 1); }
double& iCAX::Math::Tranform3::m12() { return m_Matrix(1, 2); }
const double& iCAX::Math::Tranform3::m12() const { return m_Matrix(1, 2); }
double& iCAX::Math::Tranform3::m13() { return m_Matrix(1, 3); }
const double& iCAX::Math::Tranform3::m13() const { return m_Matrix(1, 3); }
double& iCAX::Math::Tranform3::m20() { return m_Matrix(2, 0); }
const double& iCAX::Math::Tranform3::m20() const { return m_Matrix(2, 0); }
double& iCAX::Math::Tranform3::m21() { return m_Matrix(2, 1); }
const double& iCAX::Math::Tranform3::m21() const { return m_Matrix(2, 1); }
double& iCAX::Math::Tranform3::m22() { return m_Matrix(2, 2); }
const double& iCAX::Math::Tranform3::m22() const { return m_Matrix(2, 2); }
double& iCAX::Math::Tranform3::m23() { return m_Matrix(2, 3); }
const double& iCAX::Math::Tranform3::m23() const { return m_Matrix(2, 3); }
double& iCAX::Math::Tranform3::m30() { return m_Matrix(3, 0); }
const double& iCAX::Math::Tranform3::m30() const { return m_Matrix(3, 0); }
double& iCAX::Math::Tranform3::m31() { return m_Matrix(3, 1); }
const double& iCAX::Math::Tranform3::m31() const { return m_Matrix(3, 1); }
double& iCAX::Math::Tranform3::m32() { return m_Matrix(3, 2); }
const double& iCAX::Math::Tranform3::m32() const { return m_Matrix(3, 2); }
double& iCAX::Math::Tranform3::m33() { return m_Matrix(3, 3); }
const double& iCAX::Math::Tranform3::m33() const { return m_Matrix(3, 3); }
//!< 获取矩阵引用
Double4x4& iCAX::Math::Tranform3::Mat()
{
    return m_Matrix;
}
//!< 获取矩阵常量引用
const Double4x4& iCAX::Math::Tranform3::Mat() const
{
    return m_Matrix;
}

bool iCAX::Math::Tranform3::IsEqual(IN const Tranform3& Other_, IN const double& nTol_) const
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            if (fabs(m_Matrix(i, j) - Other_.m_Matrix(i,j)) > EPSILON)
            {
                return false;
            }
        }
    }
    return true;
}

//!< 矩阵乘法
iCAX::Math::Tranform3 iCAX::Math::Tranform3::operator*(IN const Tranform3& matRHS_) const
{
    Tranform3 _Result;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            _Result.m_Matrix(i, j) = 0.0;
            for (int k = 0; k < 4; ++k)
                _Result.m_Matrix(i, j) += m_Matrix(i, k) * matRHS_.m_Matrix(k, j);
        }
    return _Result;
}
//!< 计算矩阵的逆变换
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Inverse() const
{
    // 仿射矩阵 M = [ R | T ]
    //               [ 0 | 1 ]
    // _matR1: 3x3旋转/缩放矩阵
    // T: 3x1平移向量
    Double4x4 _nInv;

    // 提取3x3旋转/缩放部分 R
    double _m00 = m00(), _m01 = m01(), _m02 = m02();
    double _m10 = m10(), _m11 = m11(), _m12 = m12();
    double _m20 = m20(), _m21 = m21(), _m22 = m22();

    // 提取平移部分 T
    double _nT0 = m03(), _nT1 = m13(), _nT2 = m23();

    // 计算 _matR1 的逆矩阵 (使用通用公式，或者针对正交矩阵直接转置)
    double _nDet = _m00 * (_m11 * _m22 - _m12 * _m21) - _m01 * (_m10 * _m22 - _m12 * _m20) + _m02 * (_m10 * _m21 - _m11 * _m20);
    if (fabs(_nDet) < EPSILON)
        return Identity(); // 不可逆，返回单位矩阵

    double _nInvDet = 1.0 / _nDet;

    // _matR1 的逆
    _nInv(0, 0) = (_m11 * _m22 - _m12 * _m21) * _nInvDet;
    _nInv(0, 1) = (_m02 * _m21 - _m01 * _m22) * _nInvDet;
    _nInv(0, 2) = (_m01 * _m12 - _m02 * _m11) * _nInvDet;

    _nInv(1, 0) = (_m12 * _m20 - _m10 * _m22) * _nInvDet;
    _nInv(1, 1) = (_m00 * _m22 - _m02 * _m20) * _nInvDet;
    _nInv(1, 2) = (_m02 * _m10 - _m00 * _m12) * _nInvDet;

    _nInv(2, 0) = (_m10 * _m21 - _m11 * _m20) * _nInvDet;
    _nInv(2, 1) = (_m01 * _m20 - _m00 * _m21) * _nInvDet;
    _nInv(2, 2) = (_m00 * _m11 - _m01 * _m10) * _nInvDet;

    // 平移部分
    _nInv(0, 3) = -(_nInv(0, 0) * _nT0 + _nInv(0, 1) * _nT1 + _nInv(0, 2) * _nT2);
    _nInv(1, 3) = -(_nInv(1, 0) * _nT0 + _nInv(1, 1) * _nT1 + _nInv(1, 2) * _nT2);
    _nInv(2, 3) = -(_nInv(2, 0) * _nT0 + _nInv(2, 1) * _nT1 + _nInv(2, 2) * _nT2);

    // 最后一行
    _nInv(3, 0) = 0.0; _nInv(3, 1) = 0.0; _nInv(3, 2) = 0.0; _nInv(3, 3) = 1.0;

    return Tranform3(_nInv);
}
//!< 判断是否为单位矩阵
bool iCAX::Math::Tranform3::IsIdentity(IN const double& nTol_) const
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            double _nValue = (i == j) ? 1.0 : 0.0;
            if (fabs(m_Matrix(i, j) - _nValue) > nTol_) return false;
        }
    return true;
}

//!< 应用变换到点
iCAX::Math::Point3 iCAX::Math::Tranform3::Applied(IN const Point3& ptTarget_) const
{
    double _nX = ptTarget_.X(), _nY = ptTarget_.Y(), _nZ = ptTarget_.Z();
    double _nNewX = m00() * _nX + m01() * _nY + m02() * _nZ + m03();
    double _nNewY = m10() * _nX + m11() * _nY + m12() * _nZ + m13();
    double _nNewZ = m20() * _nX + m21() * _nY + m22() * _nZ + m23();
    double _nW = m30() * _nX + m31() * _nY + m32() * _nZ + m33();
    if (fabs(_nW) > EPSILON) { _nNewX /= _nW; _nNewY /= _nW; _nNewZ /= _nW; }
    return Point3(_nNewX, _nNewY, _nNewZ);
}
//!< 应用变换到向量
iCAX::Math::Vector3 iCAX::Math::Tranform3::Applied(IN const Vector3& vTarget_) const
{
    double _nX = vTarget_.X(), _nY = vTarget_.Y(), _nZ = vTarget_.Z();
    double _nNewX = m00() * _nX + m01() * _nY + m02() * _nZ;
    double _nNewY = m10() * _nX + m11() * _nY + m12() * _nZ;
    double _nNewZ = m20() * _nX + m21() * _nY + m22() * _nZ;
    return Vector3(_nNewX, _nNewY, _nNewZ);
}
//!< 应用变换到方向
iCAX::Math::Dir3 iCAX::Math::Tranform3::Applied(IN const Dir3& dirTarget_) const
{
    return Dir3(Applied(dirTarget_.Vector()));
}

//!< 判断是否存在非均匀缩放
bool iCAX::Math::Tranform3::HasNonUniformScaling(IN const double& nTol_) const
{
    // 列向量长度
    double lenX = sqrt(m00() * m00() + m10() * m10() + m20() * m20());
    double lenY = sqrt(m01() * m01() + m11() * m11() + m21() * m21());
    double lenZ = sqrt(m02() * m02() + m12() * m12() + m22() * m22());

    return (fabs(lenX - lenY) > nTol_ || fabs(lenX - lenZ) > nTol_ || fabs(lenY - lenZ) > nTol_);
}

//!< 判断是否存在剪切
bool iCAX::Math::Tranform3::HasShear(IN const double& nTol_) const
{
    // 列向量
    Vector3 colX(m00(), m10(), m20());
    Vector3 colY(m01(), m11(), m21());
    Vector3 colZ(m02(), m12(), m22());

    double dotXY = colX.Dot(colY) / (colX.Magnitude() * colY.Magnitude());
    double dotXZ = colX.Dot(colZ) / (colX.Magnitude() * colZ.Magnitude());
    double dotYZ = colY.Dot(colZ) / (colY.Magnitude() * colZ.Magnitude());

    return (fabs(dotXY) > nTol_ || fabs(dotXZ) > nTol_ || fabs(dotYZ) > nTol_);
}

//!< 解压三维变换矩阵
std::tuple<Double3, iCAX::Math::Quaternion, Double6, Double3, bool> iCAX::Math::Tranform3::Decompose() const
{
    // 1. 提取平移
    Double3 translation = { m03(), m13(), m23() };

    // 2. 构造线性部分矩阵 M
    double a00 = m00(), a01 = m01(), a02 = m02();
    double a10 = m10(), a11 = m11(), a12 = m12();
    double a20 = m20(), a21 = m21(), a22 = m22();

    // 3. 检查镜像
    double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    bool mirrorX = det < 0;
    if (mirrorX)
    {
        // 将镜像归到 X 轴，翻转第一列
        a00 = -a00; a10 = -a10; a20 = -a20;
        det = -det;
    }

    // 4. 提取旋转矩阵 R（通过极分解 / Gram-Schmidt 正交化）
    Vector3 colX(a00, a10, a20);
    Vector3 colY(a01, a11, a21);
    Vector3 colZ(a02, a12, a22);

    // 正交化列向量，得到旋转矩阵 R
    Vector3 xAxis = colX.Normalized();
    Vector3 yAxis = (colY - xAxis * xAxis.Dot(colY)).Normalized();//!< Y减去与X的线性相关部分，保证Y与X的正交
    Vector3 zAxis = (colZ - xAxis * xAxis.Dot(colZ) - yAxis * yAxis.Dot(colZ)).Normalized();//!< Z减去与X、Y的线性相关部分，保证Z与X、Y的正交

    // 构造旋转矩阵 R = [xAxis yAxis zAxis] 并转换为四元数
    iCAX::Math::Quaternion rotation;
    rotation.FromMatrix(Double3x3
    (
        xAxis.X(), yAxis.X(), zAxis.X(),
        xAxis.Y(), yAxis.Y(), zAxis.Y(),
        xAxis.Z(), yAxis.Z(), zAxis.Z()
    ));

    // 5. 旋转逆作用到 M，得到合并的缩放+剪切矩阵: S*SH = R⁻¹ * M（此处利用了旋转矩阵的逆等于其转置）
    double R00 = xAxis.X(), R01 = yAxis.X(), R02 = zAxis.X();
    double R10 = xAxis.Y(), R11 = yAxis.Y(), R12 = zAxis.Y();
    double R20 = xAxis.Z(), R21 = yAxis.Z(), R22 = zAxis.Z();

    double U00 = R00 * a00 + R10 * a10 + R20 * a20;
    double U01 = R00 * a01 + R10 * a11 + R20 * a21;
    double U02 = R00 * a02 + R10 * a12 + R20 * a22;

    double U10 = R01 * a00 + R11 * a10 + R21 * a20;
    double U11 = R01 * a01 + R11 * a11 + R21 * a21;
    double U12 = R01 * a02 + R11 * a12 + R21 * a22;

    double U20 = R02 * a00 + R12 * a10 + R22 * a20;
    double U21 = R02 * a01 + R12 * a11 + R22 * a21;
    double U22 = R02 * a02 + R12 * a12 + R22 * a22;

    // 6. 提取缩放（列向量模长）
    double scaleX = std::sqrt(U00 * U00 + U10 * U10 + U20 * U20);
    double scaleY = std::sqrt(U01 * U01 + U11 * U11 + U21 * U21);
    double scaleZ = std::sqrt(U02 * U02 + U12 * U12 + U22 * U22);

    // 7. 提取上三角剪切
    double shXY = (scaleY > EPSILON) ? (U01 / scaleY) : 0.0; // X对Y
    double shXZ = (scaleZ > EPSILON) ? (U02 / scaleZ) : 0.0; // X对Z
    double shYZ = (scaleZ > EPSILON) ? (U12 / scaleZ) : 0.0; // Y对Z

    // 8. 提取下三角剪切
    double shYX = (scaleX > EPSILON) ? (U10 / scaleX) : 0.0; // Y对X
    double shZX = (scaleX > EPSILON) ? (U20 / scaleX) : 0.0; // Z对X
    double shZY = (scaleY > EPSILON) ? (U21 / scaleY) : 0.0; // Z对Y

    Double3 scale = { scaleX, scaleY, scaleZ };
    Double6 shear = { shXY, shXZ, shYZ, shYX, shZX, shZY };

    return std::make_tuple(translation, rotation, shear, scale, mirrorX);
}


//!< 构建单位矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Identity()
{
    static Tranform3 s_Identity(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1);
    return s_Identity;
}
//!< 构建平移矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Translate(IN const Double3& v_)
{
    return Tranform3(
        1, 0, 0, v_[0],
        0, 1, 0, v_[1],
        0, 0, 1, v_[2],
        0, 0, 0, 1);
}
//!< 构建绕任意轴的旋转矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Rotate(IN const Dir3& dir_, IN const double& nRad_)
{
    double _nX = dir_.X(), y = dir_.Y(), z = dir_.Z();
    double _nLen = sqrt(_nX * _nX + y * y + z * z);
    if (_nLen < EPSILON) return Identity();
    _nX /= _nLen; y /= _nLen; z /= _nLen;
    double c = cos(nRad_), s = sin(nRad_), t = 1 - c;
    return Tranform3(
        t * _nX * _nX + c, t * _nX * y - s * z, t * _nX * z + s * y, 0,
        t * _nX * y + s * z, t * y * y + c, t * y * z - s * _nX, 0,
        t * _nX * z - s * y, t * y * z + s * _nX, t * z * z + c, 0,
        0, 0, 0, 1
    );
}
//!< 构建缩放矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Scale(IN const Double3& nS_)
{
    Tranform3 s(
        nS_[0], 0, 0, 0,
        0, nS_[1], 0, 0,
        0, 0, nS_[2], 0,
        0, 0, 0, 1);
    return s;
}
//!< 构建绕指定点旋转的矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Rotate(IN const Point3& pt_, IN const Dir3& dir_, IN const double& nRad_)
{
    return Translate(-pt_.XYZ())
        * Rotate(dir_, nRad_)
        * Translate(pt_.XYZ());
}
//!< 构建缩放矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Scale(IN const Point3& pt_, const Double3& nS_)
{
    return Translate(-pt_.XYZ())
        * Scale(nS_)
        * Translate(pt_.XYZ());
}
//!<  构建相对轴镜像的矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Mirror(IN const Point3& ptLocation_, IN const Dir3& dir_)
{
    double _nX = dir_.X();
    double _nY = dir_.Y();
    double _nZ = dir_.Z();

    // 若方向接近主轴，直接返回快速路径
    double _nAbsX = fabs(_nX), _nAbsY = fabs(_nY), _nAbsZ = fabs(_nZ);
    if (_nAbsX >= 0.999 && _nAbsX >= _nAbsY && _nAbsX >= _nAbsZ)
        return Tranform3::MirrorX().operator*(Tranform3::Translate(ptLocation_.XYZ())).operator*(Tranform3::Translate(-ptLocation_.XYZ()));
    if (_nAbsY >= 0.999 && _nAbsY >= _nAbsX && _nAbsY >= _nAbsZ)
        return Tranform3::MirrorY().operator*(Tranform3::Translate(ptLocation_.XYZ())).operator*(Tranform3::Translate(-ptLocation_.XYZ()));
    if (_nAbsZ >= 0.999 && _nAbsZ >= _nAbsX && _nAbsZ >= _nAbsY)
        return Tranform3::MirrorZ().operator*(Tranform3::Translate(ptLocation_.XYZ())).operator*(Tranform3::Translate(-ptLocation_.XYZ()));

    // 否则，采用通用的矩阵构造方式
    // 步骤1：构造旋转矩阵 R，使 dir_ 对齐 Z 轴
    Dir3 _dirZ(0.0, 0.0, 1.0);
    Vector3 _vRotAxis = dir_.Vector() ^ _dirZ.Vector(); // 旋转轴 = dir_ × Z
    double _nLen = _vRotAxis.Magnitude();
    Tranform3 _matR1;
    if (_nLen < EPSILON)
    {
        // 如果法向几乎平行Z轴，则无需旋转
        _matR1 = Tranform3::Identity();
    }
    else
    {
        _vRotAxis.Normalize();
        double _nAngle = acos(dir_.Vector().Dot(_dirZ.Vector()));
        _matR1 = Tranform3::Rotate(Dir3(_vRotAxis), _nAngle);
    }

    // 步骤2：平移至原点
    Tranform3 _matT1 = Tranform3::Translate(-ptLocation_.XYZ());
    Tranform3 _matT2 = Tranform3::Translate(ptLocation_.XYZ());

    // 步骤3：关于Z轴的镜像（Z轴镜像表示“绕Z轴旋转180°”）
    Tranform3 _matMirrorZ = Tranform3::MirrorZ();

    // 步骤4：组合变换 _matT2 * R⁻¹ * Mz * R * T1
    Tranform3 _matR2 = _matR1.Inverse();
    return _matT2 * _matR2 * _matMirrorZ * _matR1 * _matT1;
}
//!< 构建相对点镜像的矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Mirror(IN const Point3& pt_)
{
    return Translate(-pt_.XYZ()) * Scale({ -1,-1,-1 }) * Translate(-pt_.XYZ());
}

//!< 构建三维剪切矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::Shear(IN const Double6& nShear_)
{
    Tranform2 trsf;

    // 上三角剪切矩阵
    double shXY = nShear_[0];
    double shXZ = nShear_[1];
    double shYZ = nShear_[2];
    Tranform2 SH_u(
        1.0, shXY, shXZ,
        0.0, 1.0, shYZ,
        0.0, 0.0, 1.0);

    // 下三角剪切矩阵
    double shYX = nShear_[3];
    double shZX = nShear_[4];
    double shZY = nShear_[5];
    Tranform2 SH_l(
        1.0, 0.0, 0.0,
        shYX, 1.0, 0.0,
        shZX, shZY, 1.0);

    // 合并
    Tranform2 M = SH_u * SH_l;

    return Tranform3(
        M.m00(), M.m01(), M.m02(),  .0,
        M.m10(), M.m11(), M.m12(),  .0,
        M.m20(), M.m21(), M.m22(),  .0,
        .0,       .0,       .0,     1.0
    );
}
//!< 构建沿 X 轴的切变矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::ShearX(IN const double& nY_, IN const double& nZ_)
{
    return Tranform3(
        1, nY_, nZ_, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}
//!< 构建沿 Y 轴的切变矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::ShearY(IN const double& nX_, IN const double& nZ_)
{ 
    return Tranform3(
        1, 0, 0, 0,
        nX_, 1, nZ_, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}
//!< 构建沿 Z 轴的切变矩阵
iCAX::Math::Tranform3 iCAX::Math::Tranform3::ShearZ(IN const double& nX_, IN const double& nY_)
{ 
    return Tranform3(
        1, 0, 0, 0,
        0, 1, 0, 0,
        nX_, nY_, 1, 0,
        0, 0, 0, 1
    );
}
//!< 沿 X 轴镜像 
iCAX::Math::Tranform3 iCAX::Math::Tranform3::MirrorX()
{
    // X 轴镜像: _nX -> -_nX, y,z不变
    return Tranform3(
        -1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}
//!< 沿 Y 轴镜像
iCAX::Math::Tranform3 iCAX::Math::Tranform3::MirrorY()
{
    // Y 轴镜像: _nY -> -_nY, x,z不变
    return Tranform3(
        1, 0, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}
//!< 沿 Z 轴镜像
iCAX::Math::Tranform3 iCAX::Math::Tranform3::MirrorZ()
{
    // Z 轴镜像: _nZ -> -_nZ, x,y不变
    return Tranform3(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, -1, 0,
        0, 0, 0, 1
    );
}