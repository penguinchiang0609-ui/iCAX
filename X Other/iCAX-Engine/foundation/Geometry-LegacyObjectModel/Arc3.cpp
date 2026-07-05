#include "pch.h"
#include "Arc3.h"
#include "../Data/CommonFunction.h"

//!< 构造函数
iCAX::Geom::Arc3::Arc3(IN const Point3& ptCenter_, IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_, IN const Dir3& dirNormal_, IN const Dir3& dirRight_)
    : SglBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nRadius(nRadius_)
    , m_nStartRad(nStartRad_)
    , m_nEndRad(nEndRad_)
    , m_bCCW(bCCW_)
{
}

//!< 默认析构函数
iCAX::Geom::Arc3::~Arc3()
{
}

//!< 半径
const double& iCAX::Geom::Arc3::Radius() const
{
    return m_nRadius;
}

//!< 起始角
const double& iCAX::Geom::Arc3::StartAngle() const
{
    return m_nStartRad;
}

//!< 结束角
const double& iCAX::Geom::Arc3::EndAngle() const
{
    return m_nEndRad;
}

//!< 是否为逆时针方向
const bool& iCAX::Geom::Arc3::IsCCW() const
{
    return m_bCCW;
}
//!< 是否为逆时针方向
bool& iCAX::Geom::Arc3::IsCCW()
{
    return m_bCCW;
}

//!< 设置尺寸
void iCAX::Geom::Arc3::SetSize(IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_)
{
    m_nRadius = nRadius_;
    m_nStartRad = nStartRad_;
    m_nEndRad = nEndRad_;
    m_bCCW = bCCW_;
}

//!< 根据参数得到角度
double iCAX::Geom::Arc3::GetTheta(IN const double& nU_) const
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
iCAX::Geom::CurveOrientType iCAX::Geom::Arc3::Orientation(IN const Dir3& dirNormal_) const
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


//!< 获取起始参数
double iCAX::Geom::Arc3::StartParam() const
{
    return 0.0;
}

//!< 获取终止参数
double iCAX::Geom::Arc3::EndParam() const
{
    return 1.0;
}

//!< 在给定参数处计算点坐标
Point3 iCAX::Geom::Arc3::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);
    Point3 _pt = Point3(m_nRadius * cos(_nTheta), m_nRadius * sin(_nTheta), 0);
    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反转方向
void iCAX::Geom::Arc3::Reverse()
{
    std::swap(m_nStartRad, m_nEndRad);
    m_bCCW = !m_bCCW;
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::Arc3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

bool iCAX::Geom::Arc3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsParallel(m_csLocal.ZDirection()) && pln_.IsON(m_csLocal.Location());
}

//!< 缩放和剪切
void iCAX::Geom::Arc3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    m_nRadius *= min(nScale_[0], nScale_[1]);//!< 此处不需要关注nScale_[2]分量，因为圆弧为平面图形
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对Y轴镜像
    {
        m_bCCW = !m_bCCW;
        m_nStartRad = wrap(M_PI - m_nStartRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
        m_nEndRad = wrap(M_PI - m_nEndRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Arc3::Clone() const
{
    return std::make_shared<Arc3>(*this);
}
