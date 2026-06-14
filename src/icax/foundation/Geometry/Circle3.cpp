#include "pch.h"
#include "Circle3.h"

//!< 构造函数
iCAX::Geom::Circle3::Circle3(IN const Point3& ptCenter_, IN const double& nRadius_, IN const bool& bCCW_, IN const Dir3& dirNormal_, IN const Dir3& dirRight_)
    : SglBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
iCAX::Geom::Circle3::~Circle3()
{
}

//!< 半径
const double& iCAX::Geom::Circle3::Radius() const
{
    return m_nRadius;
}

//!< 设置尺寸
void iCAX::Geom::Circle3::SetSize(IN const double& nRadius_)
{
    m_nRadius = nRadius_;
}

//!< 根据参数得到角度
double iCAX::Geom::Circle3::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);;

    double _ndDeltaRad = m_bCCW ? 2 * M_PI : -2 * M_PI;
    return  _nU * _ndDeltaRad;
}

//!< 获取朝向
iCAX::Geom::CurveOrientType iCAX::Geom::Circle3::Orientation(IN const Dir3& dirNormal_) const
{
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


//!< 起始参数
double iCAX::Geom::Circle3::StartParam() const
{
    return 0.0;
}
//!< 截止参数
double iCAX::Geom::Circle3::EndParam() const
{
    return 1.0;
}

//!< 在给定参数处计算点坐标
Point3 iCAX::Geom::Circle3::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);
    Point3 _pt = Point3(m_nRadius * cos(_nTheta), m_nRadius * sin(_nTheta), 0);
    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反转方向
void iCAX::Geom::Circle3::Reverse()
{
    m_bCCW = !m_bCCW;
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::Circle3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::Circle3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsParallel(m_csLocal.ZDirection()) && pln_.IsON(m_csLocal.Location());
}

//!< 缩放和剪切
void iCAX::Geom::Circle3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    m_nRadius *= min(nScale_[0], nScale_[1]);//!< 此处不需要关注nScale_[2]分量，因为圆弧为平面图形
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对Y轴镜像
    {
        m_bCCW = !m_bCCW;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Circle3::Clone() const
{
    return std::make_shared<Circle3>(*this);
}

