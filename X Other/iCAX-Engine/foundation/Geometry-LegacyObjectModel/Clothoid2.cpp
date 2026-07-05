#include "pch.h"
#include "Clothoid2.h"
#include <numbers>
#include <cmath>
#include <cassert>
#include <algorithm>

using namespace iCAX::Geom;

//!< 菲涅尔积分
inline void _FresnelCS(double t, double& C, double& S)
{
    C = 0.0;
    S = 0.0;
    double t2 = t * t;
    double termC = t;
    double termS = t2 * t;
    double pi2 = M_PI / 2.0;
    int n = 0;
    const int MAX_ITER = 20;

    for (n = 0; n < MAX_ITER; ++n)
    {
        if (n > 0)
        {
            termC *= -pi2 * pi2 * t2 * t2 / ((2 * n) * (4 * n + 1));
            termS *= -pi2 * pi2 * t2 * t2 / ((2 * n + 1) * (4 * n + 3));
        }
        C += termC;
        S += termS;
    }
}

//!< 构造函数
Clothoid2::Clothoid2(IN const CoordSys2& csLocal_, IN const double& nKappa0_,
    IN const double& nKappaRate_,
    IN const double& nLength_,
    IN const double& nTheta0_)
    : SglBndCurve2(csLocal_)
    , m_nKappa0(nKappa0_)
    , m_nKappaRate(nKappaRate_)
    , m_nLength(nLength_)
    , m_nTheta0(nTheta0_)
{
    assert(nLength_ >= 0);
}

//!< 析构函数
Clothoid2::~Clothoid2()
{
}

//!< 获取起始曲率
const double& Clothoid2::StartCurvature() const
{
    return m_nKappa0;
}
//!< 设置起始曲率
void iCAX::Geom::Clothoid2::SetStartCurvature(IN const double& nKappa0_)
{
    m_nKappa0 = nKappa0_;
}
//!< 获取曲率变化率
const double& Clothoid2::CurvatureRate() const
{
    return m_nKappaRate;
}
//!< 设置曲率变化率
void iCAX::Geom::Clothoid2::SetCurvatureRate(IN const double& nKappaRate_)
{
    m_nKappaRate = nKappaRate_;
}
//!< 获取曲线长度
const double& Clothoid2::Length() const
{
    return m_nLength;
}
//!< 设置曲线长度
void iCAX::Geom::Clothoid2::SetLength(IN const double& nLength_)
{
    m_nLength = nLength_;
}
//!< 获取Theta0
const double& iCAX::Geom::Clothoid2::Theta0() const
{
    return m_nTheta0;
}
//!< 设置Theta0
void iCAX::Geom::Clothoid2::SetTheta0(IN const double& nTheta_)
{
    m_nTheta0 = nTheta_;
}
//!< 获取曲线起始参数
double Clothoid2::StartParam() const
{
    return 0.0;
}
//!< 获取曲线终止参数
double Clothoid2::EndParam() const
{
    return m_nLength;
}

//!< 在给定参数处计算点坐标
Point2 iCAX::Geom::Clothoid2::Evaluate(IN const double& nU_) const
{
    double s = NormalizeParam(nU_) * m_nLength;

    if (fabs(m_nKappaRate) < EPSILON) // 圆弧或直线
    {
        if (fabs(m_nKappa0) < EPSILON) // 直线
        {
            Point2 ptLocal(s * std::cos(m_nTheta0), s * std::sin(m_nTheta0));
            return m_csLocal.LocalToWorld().Applied(ptLocal);
        }
        else // 圆弧
        {
            double theta1 = m_nTheta0 + m_nKappa0 * s;
            double x = (std::sin(theta1) - std::sin(m_nTheta0)) / m_nKappa0;
            double y = (-std::cos(theta1) + std::cos(m_nTheta0)) / m_nKappa0;
            Point2 ptLocal(x, y);
            return m_csLocal.LocalToWorld().Applied(ptLocal);
        }
    }

    // Fresnel 积分解析解
    double a = std::sqrt(m_nKappaRate / M_PI);
    double t0 = a * (0 + m_nKappa0 / m_nKappaRate); // s=0
    double t1 = a * (s + m_nKappa0 / m_nKappaRate); // s=s
    double C, S;
    _FresnelCS(t1, C, S);
    double C0, S0;
    _FresnelCS(t0, C0, S0);

    double x = (C - C0) / a;
    double y = (S - S0) / a;

    // 旋转起始角
    double cosTheta0 = std::cos(m_nTheta0);
    double sinTheta0 = std::sin(m_nTheta0);
    double xRot = cosTheta0 * x - sinTheta0 * y;
    double yRot = sinTheta0 * x + cosTheta0 * y;

    Point2 ptLocal(xRot, yRot);
    return m_csLocal.LocalToWorld().Applied(ptLocal);
}

//!< 反转
void Clothoid2::Reverse()
{
    // 1️⃣ 计算原曲线终点处参数
    double kappaEnd = m_nKappa0 + m_nKappaRate * m_nLength;

    // 2️⃣ 计算终点的坐标与方向
    Point2 endPoint = Evaluate(m_nLength);
    double thetaEnd = m_nTheta0 + m_nKappa0 * m_nLength + 0.5 * m_nKappaRate * m_nLength * m_nLength;

    // 3️⃣ 更新局部坐标系：新起点 = 原终点，方向旋转 π
    m_csLocal = CoordSys2(endPoint, Dir2(cos(thetaEnd + M_PI), sin(thetaEnd + M_PI)));

    // 4️⃣ 更新几何参数
    m_nKappa0 = kappaEnd;
    m_nKappaRate = -m_nKappaRate;
    m_nTheta0 = 0.0; // 重置为局部系的新起始角

}

//!< 缩放和剪切
void Clothoid2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    //!< 该曲线过于特殊，无法保证仿射不变性，所以只支持平移旋转，不支持剪切、缩放、镜像
}

//!< 克隆
std::shared_ptr<Geometry> Clothoid2::Clone() const
{
    return std::make_shared<Clothoid2>(*this);
}
