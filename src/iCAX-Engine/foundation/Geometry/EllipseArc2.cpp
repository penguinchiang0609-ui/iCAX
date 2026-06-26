#include "pch.h"
#include "EllipseArc2.h"
#include <cmath>
#include <algorithm>
#include "../Data/CommonFunction.h"

using namespace iCAX::Math;
using namespace iCAX::Geom;

//!< 构造函数
EllipseArc2::EllipseArc2(IN const Point2& ptCenter_, IN const double& nRadiusA_, IN const double& nRadiusB_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : SglBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nMajorRadius(nRadiusA_)
    , m_nMinorRadius(nRadiusB_)
    , m_nStartRad(nStartRad_)
    , m_nEndRad(nEndRad_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
EllipseArc2::~EllipseArc2()
{
}

//!< 长轴半径
const double& EllipseArc2::MajorRadius() const
{
    return m_nMajorRadius;
}
//!< 短轴半径
const double& EllipseArc2::MinorRadius() const
{
    return m_nMinorRadius;
}
//!< 起始角
const double& EllipseArc2::StartAngle() const
{
    return m_nStartRad;
}
//!< 终止角
const double& EllipseArc2::EndAngle() const
{
    return m_nEndRad;
}
//!< 逆时针
const bool& EllipseArc2::IsCCW() const
{
    return m_bCCW;
}
//!< 设置尺寸
void iCAX::Geom::EllipseArc2::SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_)
{
    m_nMajorRadius = nMajorRadius_;
    m_nMinorRadius = nMinorRadius_;
    m_nStartRad = nStartRad_;
    m_nEndRad = nEndRad_;
    m_bCCW = bCCW_;
}

//!< 根据参数得到角度
double iCAX::Geom::EllipseArc2::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);//!< 归一化

    double _ndDeltaRad = m_nEndRad - m_nStartRad;
    if (m_bCCW && _ndDeltaRad < 0)
        _ndDeltaRad += 2 * M_PI;
    else if (!m_bCCW && _ndDeltaRad > 0)
        _ndDeltaRad -= 2 * M_PI;

    return m_nStartRad + _nU * _ndDeltaRad;
}

//!< 获取曲线起始参数
double iCAX::Geom::EllipseArc2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::EllipseArc2::EndParam() const
{
    return 1.0;
}

//!< 获取曲线朝向类型
CurveOrientType iCAX::Geom::EllipseArc2::Orientation() const
{
    if (!IsClosed())
    {
        return kCrvOrientUnknown;
    }

    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 参数点
Point2 EllipseArc2::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);

    // 椭圆方程
    Point2 _ptLocal(m_nMajorRadius * std::cos(_nTheta), m_nMinorRadius * std::sin(_nTheta));
    return m_csLocal.LocalToWorld().Applied(_ptLocal);
}

//!< 反转
void EllipseArc2::Reverse()
{
    std::swap(m_nStartRad, m_nEndRad);
    m_bCCW = !m_bCCW;
}

//!< 缩放和剪切
void iCAX::Geom::EllipseArc2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
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
std::shared_ptr<Geometry> EllipseArc2::Clone() const
{
    return std::make_shared<EllipseArc2>(*this);
}
