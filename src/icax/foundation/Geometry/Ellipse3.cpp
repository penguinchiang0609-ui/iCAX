#include "pch.h"
#include "Ellipse3.h"

//!< 构造函数
iCAX::Geom::Ellipse3::Ellipse3(IN const Point3& ptCenter_, IN const double& nMajorRadius_, IN const double& nMinorRadius_, IN const bool& bCCW_/* = true*/, IN const Dir3& dirNormal_/* = Dir3::Up()*/, IN const Dir3& dirRight_/* = Dir3::Right()*/)
    : SglBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nMajorRadius(nMajorRadius_)
    , m_nMinorRadius(nMinorRadius_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
iCAX::Geom::Ellipse3::~Ellipse3()
{
}


//!<  获取长半轴（只读）
const double& iCAX::Geom::Ellipse3::MajorRadius() const
{
    return m_nMajorRadius;
}
//!< 获取短半轴（只读）
const double& iCAX::Geom::Ellipse3::MinorRadius() const
{
    return m_nMinorRadius;
}

//!< 设置尺寸
void iCAX::Geom::Ellipse3::SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_)
{
    m_nMajorRadius = nMajorRadius_;
    m_nMinorRadius = nMinorRadius_;
}

//!< 根据参数得到角度
double iCAX::Geom::Ellipse3::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);

    double _ndDeltaRad = m_bCCW ? 2 * M_PI : -2 * M_PI;
    return  _nU * _ndDeltaRad;
}

//!< 获取曲线起始参数
double iCAX::Geom::Ellipse3::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::Ellipse3::EndParam() const
{
    return 1.0;
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Ellipse3::Orientation(IN const Dir3& dirNormal_) const
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

//!< 计算指定参数处的点坐标
Point3 iCAX::Geom::Ellipse3::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);
    return m_csLocal.LocalToWorld().Applied(Point3(m_nMajorRadius * cos(_nTheta), m_nMinorRadius * sin(_nTheta), 0));
}

//!< 反向
void iCAX::Geom::Ellipse3::Reverse()
{
    m_bCCW = !m_bCCW;
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::Ellipse3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::Ellipse3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsParallel(m_csLocal.ZDirection()) && pln_.IsON(m_csLocal.Location());
}

//!< 缩放和剪切
void iCAX::Geom::Ellipse3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    m_nMajorRadius *= nScale_[0];
    m_nMinorRadius *= nScale_[1];
    if (bMirrorX_)
    {
        m_bCCW = !m_bCCW;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Ellipse3::Clone() const
{
    return std::make_shared<Ellipse3>(*this);
}
