#include "pch.h"
#include "Tranform2.h"
#include <stdexcept>

//!< 默认构造函数
iCAX::Math::Tranform2::Tranform2()
{
    m_Matrix =
    {
        1, 0, 0,
        0, 1, 0,
        0, 0, 1
    };
}

//!< 使用 9 个矩阵元素初始化
iCAX::Math::Tranform2::Tranform2(IN const double& m00_, IN const double& m01_, IN const double& m02_,
    IN const double& m10_, IN const double& m11_, IN const double& m12_,
    IN const double& m20_, IN const double& m21_, IN const double& m22_)
{
    m_Matrix(0, 0) = m00_; m_Matrix(0, 1) = m01_; m_Matrix(0, 2) = m02_;
    m_Matrix(1, 0) = m10_; m_Matrix(1, 1) = m11_; m_Matrix(1, 2) = m12_;
    m_Matrix(2, 0) = m20_; m_Matrix(2, 1) = m21_; m_Matrix(2, 2) = m22_;
}

//!< 使用 3x3 矩阵初始化
iCAX::Math::Tranform2::Tranform2(IN const Double3x3& mat)
    : m_Matrix(mat)
{
}

//!< 析构函数
iCAX::Math::Tranform2::~Tranform2()
{
}

//!< 获取/修改 m00 元素
double& iCAX::Math::Tranform2::m00() { return m_Matrix(0, 0); }
//!< 获取/修改 m00 元素
const double& iCAX::Math::Tranform2::m00() const { return m_Matrix(0, 0); }
//!< 获取/修改 m01 元素
double& iCAX::Math::Tranform2::m01() { return m_Matrix(0, 1); }
//!< 获取/修改 m01 元素
const double& iCAX::Math::Tranform2::m01() const { return m_Matrix(0, 1); }
//!< 获取/修改 m02 元素
double& iCAX::Math::Tranform2::m02() { return m_Matrix(0, 2); }
//!< 获取/修改 m02 元素
const double& iCAX::Math::Tranform2::m02() const { return m_Matrix(0, 2); }
//!< 获取/修改 m10 元素
double& iCAX::Math::Tranform2::m10() { return m_Matrix(1, 0); }
//!< 获取/修改 m10 元素
const double& iCAX::Math::Tranform2::m10() const { return m_Matrix(1, 0); }
//!< 获取/修改 m11 元素
double& iCAX::Math::Tranform2::m11() { return m_Matrix(1, 1); }
//!< 获取/修改 m11 元素
const double& iCAX::Math::Tranform2::m11() const { return m_Matrix(1, 1); }
//!< 获取/修改 m12 元素
double& iCAX::Math::Tranform2::m12() { return m_Matrix(1, 2); }
//!< 获取/修改 m12 元素
const double& iCAX::Math::Tranform2::m12() const { return m_Matrix(1, 2); }
//!< 获取/修改 m20 元素
double& iCAX::Math::Tranform2::m20() { return m_Matrix(2, 0); }
//!< 获取/修改 m20 元素
const double& iCAX::Math::Tranform2::m20() const { return m_Matrix(2, 0); }
//!< 获取/修改 m21 元素
double& iCAX::Math::Tranform2::m21() { return m_Matrix(2, 1); }
//!< 获取/修改 m21 元素
const double& iCAX::Math::Tranform2::m21() const { return m_Matrix(2, 1); }
//!< 获取/修改 m22 元素
double& iCAX::Math::Tranform2::m22() { return m_Matrix(2, 2); }
//!< 获取/修改 m22 元素
const double& iCAX::Math::Tranform2::m22() const { return m_Matrix(2, 2); }
//!< 获取内部矩阵引用，可修改
Double3x3& iCAX::Math::Tranform2::Mat()
{
    return m_Matrix;
}
//!< 获取内部矩阵引用（只读）
const Double3x3& iCAX::Math::Tranform2::Mat() const
{
    return m_Matrix;
}

//!< 计算矩阵行列式
double iCAX::Math::Tranform2::LinearDeterminant() const
{
    return 0.0;
}

bool iCAX::Math::Tranform2::IsEqual(IN const Tranform2& Other_, IN const double& nTol_) const
{
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            if (fabs(m_Matrix(i, j) - Other_.m_Matrix(i, j)) > EPSILON)
            {
                return false;
            }
        }
    }
    return true;
}

//!< 矩阵乘法
iCAX::Math::Tranform2 iCAX::Math::Tranform2::operator*(IN const Tranform2& other) const
{
    Tranform2 _Result;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            _Result.m_Matrix(i, j) = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                _Result.m_Matrix(i, j) += m_Matrix(i, k) * other.m_Matrix(k, j);
            }
        }
    }
    return _Result;
}

//!< 求逆矩阵
iCAX::Math::Tranform2& iCAX::Math::Tranform2::Inverse()
{
    *this = Inversed();
    return *this;
}

//!< 求逆矩阵
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Inversed() const
{
    // 3x3 矩阵求逆 (适合仿射变换)
    double _nDet =
        m00() * (m11() * m22() - m12() * m21()) -
        m01() * (m10() * m22() - m12() * m20()) +
        m02() * (m10() * m21() - m11() * m20());

    if (fabs(_nDet) < EPSILON)
    {
        throw std::runtime_error("Tranform2.h::Inverse failed: determinant is zero");
    }

    double _nInvDet = 1.0 / _nDet;

    Tranform2 _matInv;
    _matInv.m00() = (m11() * m22() - m12() * m21()) * _nInvDet;
    _matInv.m01() = -(m01() * m22() - m02() * m21()) * _nInvDet;
    _matInv.m02() = (m01() * m12() - m02() * m11()) * _nInvDet;

    _matInv.m10() = -(m10() * m22() - m12() * m20()) * _nInvDet;
    _matInv.m11() = (m00() * m22() - m02() * m20()) * _nInvDet;
    _matInv.m12() = -(m00() * m12() - m02() * m10()) * _nInvDet;

    _matInv.m20() = (m10() * m21() - m11() * m20()) * _nInvDet;
    _matInv.m21() = -(m00() * m21() - m01() * m20()) * _nInvDet;
    _matInv.m22() = (m00() * m11() - m01() * m10()) * _nInvDet;

    return _matInv;
}

//!< 判断是否为单位矩阵
bool iCAX::Math::Tranform2::IsIdentity(IN const double&nTol_) const
{
    return fabs(m00() - 1.0) < nTol_ && fabs(m01()) < nTol_ && fabs(m02()) < nTol_ &&
        fabs(m10()) < nTol_ && fabs(m11() - 1.0) < nTol_ && fabs(m12()) < nTol_ &&
        fabs(m20()) < nTol_ && fabs(m21()) < nTol_ && fabs(m22() - 1.0) <nTol_;
}

//!< 应用变换到点
iCAX::Math::Point2 iCAX::Math::Tranform2::Applied(IN const Point2& ptTarget_) const
{
    double _nX = ptTarget_.X();
    double _nY = ptTarget_.Y();
    double _nXP = m00() * _nX + m01() * _nY + m02();
    double _nYP = m10() * _nX + m11() * _nY + m12();
    double _nWP = m20() * _nX + m21() * _nY + m22();
    if (fabs(_nWP) < EPSILON) _nWP = 1.0;
    return Point2(_nXP / _nWP, _nYP / _nWP);
}
//!< 应用变换到向量
iCAX::Math::Vector2 iCAX::Math::Tranform2::Applied(IN const Vector2& vTarget_) const
{
    double _nX = vTarget_.X();
    double _nY = vTarget_.Y();
    // 向量不受平移影响
    double _nXP = m00() * _nX + m01() * _nY;
    double _nYP = m10() * _nX + m11() * _nY;
    return Vector2(_nXP, _nYP);
}
//!< 应用变换到方向
iCAX::Math::Dir2 iCAX::Math::Tranform2::Applied(IN const Dir2& dirTarget_) const
{
    return Dir2(Applied(dirTarget_.Vector()));
}

//!< 是否存在非刚性变换
bool iCAX::Math::Tranform2::HasNonUniformScaling(IN const double& nTol_) const
{
    //!< 列向量
    double _nLenX = sqrt(m00() * m00() + m10() * m10());
    double _nLenY = sqrt(m01() * m01() + m11() * m11());

    if (fabs(_nLenX - _nLenY) > nTol_)
        return true; // 非等比缩放

    // 检查列向量是否正交
    double _nDot = m00() * m01() + m10() * m11();
    if (fabs(_nDot) > nTol_)
        return true; // 存在剪切

    return false; // 仅刚性或等比缩放
}

//!< 是否存在剪切
bool iCAX::Math::Tranform2::HasShear(IN const double& nTol_) const
{
    // 检查列向量是否正交
    double dot = m00() * m01() + m10() * m11();
    if (fabs(dot) > nTol_)
        return true; // 存在剪切

    return false; // 仅刚性或等比缩放
}

//!< 解压出平移、旋转、缩放、剪切
std::tuple<Double2, double, Double2, Double2, bool> iCAX::Math::Tranform2::Decompose() const
{
    // 提取平移
    Double2 translation = { m02(), m12() };

    // 构造线性部分矩阵
    /*
    * a00 a01
    * a10 a11
    */
    double a00 = m00(), a01 = m01();
    double a10 = m10(), a11 = m11();

    // 检查镜像：行列式 < 0 表示存在镜像
    double det = a00 * a11 - a01 * a10;
    bool mirrorX = det < 0;
    if (mirrorX)
    {
        // 将镜像归到 X 轴，翻转第一列
        a00 = -a00; 
        a10 = -a10;
    }

    // 1. 计算旋转角：使用极分解（Polar Decomposition）
    double rotation = std::atan2(a10, a00);//!< 此处返回值范围为-PI~PI

    // 1.1 构造旋转矩阵的逆 R^-1
    double cosR = cos(-rotation);
    double sinR = sin(-rotation);
    double R00 = cosR, R01 = sinR;
    double R10 = -sinR, R11 = cosR;

    // 2. 去掉旋转得到上三角矩阵 U = R^-1 * M
    double U00 = R00 * a00 + R01 * a10;
    double U01 = R00 * a01 + R01 * a11;
    double U10 = R10 * a00 + R11 * a10;
    double U11 = R10 * a01 + R11 * a11;

    // === 2.1 对称化，去除非对称噪声 ===
    /*
    * 此处做对称化会将非对称的剪切平摊到对称的剪切+缩放上
    * 收益：减少浮点数自身误差浮动带来的影响
    */
    double U01s = 0.5 * (U01 + U10);
    double U10s = U01s;
    U01 = U01s;
    U10 = U10s;

    // 3. 缩放
    /*
    * U00 U01
    * U10 U11
    */
    double scaleX = std::sqrt(U00 * U00 + U10 * U10);
    double scaleY = std::sqrt(U01 * U01 + U11 * U11);

    // 4. 上三角剪切 (X 对 Y)，此处根据定义，去除缩放对shear的影响
    double shXY = scaleY > EPSILON ? U01 / scaleY : .0 ;

    // 5. 下三角剪切 (Y 对 X)，此处根据定义，去除缩放对shear的影响
    double shYX = scaleX > EPSILON ? U10 / scaleX : .0;

    Double2 scale = { scaleX, scaleY };
    Double2 shear = { shXY, shYX };

    return std::make_tuple(translation, rotation, shear, scale, mirrorX);
}

//!< 单位变换
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Identity()
{
    return Tranform2(1,   0,  0,
                   0,   1,  0,
                   0,   0,  1);
}
//!< 平移矩阵
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Translate(IN const Double2& vOffset_)
{
    return Tranform2(
        1,  0,  vOffset_[0],
        0,  1,  vOffset_[1],
        0,  0,  1
    );
}

//!< 旋转
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Rotate(IN const double& nRad_)
{
    double _nCos = cos(nRad_);
    double _nSin = sin(nRad_);
    return Tranform2(
        _nCos,  -_nSin, 0,
        _nSin,  _nCos,  0,
        0,  0,  1
    );
}
//!< 缩放矩阵
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Scale(IN const Double2& sXY_)
{
    return Tranform2(
        sXY_[0], 0, 0,
        0, sXY_[1], 0,
        0, 0, 1
    );
}
//!< 切变
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Shear(IN const Double2& xy_)
{
    return Tranform2(
        1, xy_[0], 0,
        xy_[1], 1, 0,
        0, 0, 1
    );
}
//!< 以指定点为中心旋转
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Rotate(IN const Point2& ptCenter_, IN const double& nRad_)
{
    Tranform2 _matT1 = Tranform2::Translate(-ptCenter_.XY());
    Tranform2 _matR = Tranform2::Rotate(nRad_);   // 绕原点旋转
    Tranform2 _matT2 = Tranform2::Translate(ptCenter_.XY());

    return _matT2 * _matR * _matT1;
}
//!< 以指定点为中心缩放
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Scale(IN const Point2& ptCenter_, IN const Double2& sXY_)
{
    Tranform2 _matT1 = Tranform2::Translate(-ptCenter_.XY());
    Tranform2 _matS = Tranform2::Scale(sXY_);  // 绕原点缩放
    Tranform2 _matT2 = Tranform2::Translate(ptCenter_.XY());

    return _matT2 * _matS * _matT1;
}
//!< 轴镜像
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Mirror(IN const Point2& ptCenter_, IN const Dir2& dir_)
{
    double _nTheta = atan2(dir_.Y(), dir_.X());

    Tranform2 _matT1 = Tranform2::Translate(-ptCenter_.XY());
    Tranform2 _matR1 = Tranform2::Rotate(-_nTheta);
    Tranform2 _matMX = Tranform2(
        1, 0, 0,
        0, -1, 0,
        0, 0, 1
    );
    Tranform2 _matR2 = Tranform2::Rotate(_nTheta);
    Tranform2 _matT2 = Tranform2::Translate(ptCenter_.XY());

    return _matT2 * _matR2 * _matMX * _matR1 * _matT1;
}
//!< 点镜像
iCAX::Math::Tranform2 iCAX::Math::Tranform2::Mirror(IN const Point2& ptCenter_)
{
    Tranform2 _matT1 = Tranform2::Translate(-ptCenter_.XY());
    Tranform2 _matNeg = Tranform2(
        -1, 0, 0,
        0, -1, 0,
        0, 0, 1
    );
    Tranform2 _matT2 = Tranform2::Translate(ptCenter_.XY());

    return _matT2 * _matNeg * _matT1;
}
//!< X轴镜像
iCAX::Math::Tranform2 iCAX::Math::Tranform2::MirrorX()
{
    return Tranform2(
        1, 0, 0,
        0, -1, 0,
        0, 0, 1
    );
}
//!< Y轴镜像
iCAX::Math::Tranform2 iCAX::Math::Tranform2::MirrorY()
{
    return Tranform2(
        -1, 0, 0,
        0, 1, 0,
        0, 0, 1
    );
}
