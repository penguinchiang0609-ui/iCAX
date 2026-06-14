#include "pch.h"
#include "Clothoid3.h"
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
Clothoid3::Clothoid3(IN const CoordSys3& csLocal_, IN const double& nKappa0_,
    IN const double& nKappaRate_,
    IN const double& nLength_,
    IN const double& nTheta0_)
    : SglBndCurve3(csLocal_)
    , m_nKappa0(nKappa0_)
    , m_nKappaRate(nKappaRate_)
    , m_nLength(nLength_)
    , m_nTheta0(nTheta0_)
{
    assert(nLength_ >= 0);
}

//!< 析构函数
Clothoid3::~Clothoid3()
{
}

//!< 获取起始曲率
const double& Clothoid3::StartCurvature() const
{
    return m_nKappa0;
}
//!< 设置起始曲率
void iCAX::Geom::Clothoid3::SetStartCurvature(IN const double& nKappa0_)
{
    m_nKappa0 = nKappa0_;
}
//!< 获取曲率变化率
const double& Clothoid3::CurvatureRate() const
{
    return m_nKappaRate;
}
//!< 设置曲率变化率
void iCAX::Geom::Clothoid3::SetCurvatureRate(IN const double& nKappaRate_)
{
    m_nKappaRate = nKappaRate_;
}
//!< 获取曲线长度
const double& Clothoid3::Length() const
{
    return m_nLength;
}
//!< 设置曲线长度
void iCAX::Geom::Clothoid3::SetLength(IN const double& nLength_)
{
    m_nLength = nLength_;
}
//!< 获取Theta0
const double& iCAX::Geom::Clothoid3::Theta0() const
{
    return m_nTheta0;
}
//!< 设置Theta0
void iCAX::Geom::Clothoid3::SetTheta0(IN const double& nTheta_)
{
    m_nTheta0 = nTheta_;
}
//!< 获取曲线起始参数
double Clothoid3::StartParam() const
{
    return 0.0;
}
//!< 获取曲线终止参数
double Clothoid3::EndParam() const
{
    return m_nLength;
}

//!< 在给定参数处计算点坐标
Point3 iCAX::Geom::Clothoid3::Evaluate(IN const double& nU_) const
{
    double s = NormalizeParam(nU_) * m_nLength;

    if (fabs(m_nKappaRate) < EPSILON) // 圆弧或直线
    {
        if (fabs(m_nKappa0) < EPSILON) // 直线
        {
            Point3 ptLocal(s * std::cos(m_nTheta0), s * std::sin(m_nTheta0), 0);
            return m_csLocal.LocalToWorld().Applied(ptLocal);
        }
        else // 圆弧
        {
            double theta1 = m_nTheta0 + m_nKappa0 * s;
            double x = (std::sin(theta1) - std::sin(m_nTheta0)) / m_nKappa0;
            double y = (-std::cos(theta1) + std::cos(m_nTheta0)) / m_nKappa0;
            Point3 ptLocal(x, y, 0);
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

    Point3 ptLocal(xRot, yRot, 0);
    return m_csLocal.LocalToWorld().Applied(ptLocal);
}

//!< 反转
void Clothoid3::Reverse()
{
    // 1️⃣ 计算原曲线终点处参数
    double kappaEnd = m_nKappa0 + m_nKappaRate * m_nLength;

    // 2️⃣ 计算终点的坐标与方向
    Point3 endPoint = Evaluate(m_nLength);
    double thetaEnd = m_nTheta0 + m_nKappa0 * m_nLength + 0.5 * m_nKappaRate * m_nLength * m_nLength;

    // 3️⃣ 更新局部坐标系：新起点 = 原终点，方向旋转 π
    m_csLocal = CoordSys3(endPoint, Dir3(cos(thetaEnd + M_PI), sin(thetaEnd + M_PI), 0), Dir3(0, 0, 1));

    // 4️⃣ 更新几何参数
    m_nKappa0 = kappaEnd;
    m_nKappaRate = -m_nKappaRate;
    m_nTheta0 = 0.0; // 重置为局部系的新起始角
}

//!< 获取曲线所在平面
std::optional<Plane3> iCAX::Geom::Clothoid3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 是否在平面上
bool iCAX::Geom::Clothoid3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsParallel(m_csLocal.ZDirection()) && pln_.IsON(m_csLocal.Location());
}

//!< 缩放和剪切
void Clothoid3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    //!< 该曲线过于特殊，无法保证仿射不变性，所以只支持平移旋转，不支持剪切、缩放、镜像
}

//!< 克隆
std::shared_ptr<Geometry> Clothoid3::Clone() const
{
    return std::make_shared<Clothoid3>(*this);
}
