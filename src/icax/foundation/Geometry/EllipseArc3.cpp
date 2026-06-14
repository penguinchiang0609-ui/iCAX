#include "pch.h"
#include "EllipseArc3.h"
#include <cmath>
#include <algorithm>
#include "../Data/CommonFunction.h"

using namespace iCAX::Math;
using namespace iCAX::Geom;

//!< 构造函数
EllipseArc3::EllipseArc3(IN const Point3& ptCenter_, IN const double& nRadiusA_, IN const double& nRadiusB_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_/* = true*/, IN const Dir3& dirNormal_ /*= Dir3::Up()*/, IN const Dir3& dirRight_/* = Dir3::Right()*/)
    : SglBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nMajorRadius(nRadiusA_)
    , m_nMinorRadius(nRadiusB_)
    , m_nStartRad(nStartRad_)
    , m_nEndRad(nEndRad_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
EllipseArc3::~EllipseArc3()
{
}

//!< 长轴半径
const double& EllipseArc3::MajorRadius() const
{
    return m_nMajorRadius;
}
//!< 短轴半径
const double& EllipseArc3::MinorRadius() const
{
    return m_nMinorRadius;
}
//!< 起始角
const double& EllipseArc3::StartAngle() const
{
    return m_nStartRad;
}
//!< 终止角
const double& EllipseArc3::EndAngle() const
{
    return m_nEndRad;
}
//!< 逆时针
const bool& EllipseArc3::IsCCW() const
{
    return m_bCCW;
}
//!< 设置尺寸
void iCAX::Geom::EllipseArc3::SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_)
{
    m_nMajorRadius = nMajorRadius_;
    m_nMinorRadius = nMinorRadius_;
    m_nStartRad = nStartRad_;
    m_nEndRad = nEndRad_;
    m_bCCW = bCCW_;
}

//!< 根据参数得到角度
double iCAX::Geom::EllipseArc3::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);//!< 归一化

    double _ndDeltaRad = m_nEndRad - m_nStartRad;
    if (m_bCCW && _ndDeltaRad < 0)
        _ndDeltaRad += 2 * M_PI;
    else if (!m_bCCW && _ndDeltaRad > 0)
        _ndDeltaRad -= 2 * M_PI;

    return m_nStartRad + _nU * _ndDeltaRad;
}

//!< 获取朝向
iCAX::Geom::CurveOrientType iCAX::Geom::EllipseArc3::Orientation(IN const Dir3& dirNormal_) const
{
    // 不是闭合的返回未知
    if (!IsClosed())
        return kCrvOrientUnknown;

    double _nDot = m_csLocal.ZDirection().Vector().Dot(dirNormal_.Vector());
    if (abs(_nDot) < EPSILON)
    {
        return kCrvOrientUnknown;
    }

    if (_nDot > 0)
    {
        return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
    }
    else
    {
        return m_bCCW ? kCrvOrientCW : kCrvOrientCCW;
    }
}
//!< 获取曲线起始参数
double iCAX::Geom::EllipseArc3::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::EllipseArc3::EndParam() const
{
    return 1.0;
}

//!< 参数点
Point3 EllipseArc3::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);

    // 椭圆方程
    Point3 _ptLocal(m_nMajorRadius * std::cos(_nTheta), m_nMinorRadius * std::sin(_nTheta), 0);
    return m_csLocal.LocalToWorld().Applied(_ptLocal);
}

//!< 反转
void EllipseArc3::Reverse()
{
    std::swap(m_nStartRad, m_nEndRad);
    m_bCCW = !m_bCCW;
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::EllipseArc3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::EllipseArc3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsParallel(m_csLocal.ZDirection()) && pln_.IsON(m_csLocal.Location());
}

//!< 缩放和剪切
void iCAX::Geom::EllipseArc3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    m_nMajorRadius *= nScale_[0];
    m_nMinorRadius *= nScale_[1];

    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
        m_nStartRad = wrap(M_PI - m_nStartRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
        m_nEndRad = wrap(M_PI - m_nEndRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
    }
}

//!< 克隆
std::shared_ptr<Geometry> EllipseArc3::Clone() const
{
    return std::make_shared<EllipseArc3>(*this);
}
