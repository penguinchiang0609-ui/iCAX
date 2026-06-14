#include "pch.h"
#include "Ray3.h"

//!< 构造函数
iCAX::Geom::Ray3::Ray3(IN const Point3& ptStart_, IN const Point3& ptTarget_)
    : HalfBndCurve3(CoordSys3(ptStart_, Dir3(ptTarget_ - ptStart_)))
    , m_bReversed(false)
{
}

//!< 析构函数
iCAX::Geom::Ray3::~Ray3()
{
}

//!< 根据参数计算直线上点坐标
Point3 iCAX::Geom::Ray3::Evaluate(const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    if (m_bReversed)
    {
        _nU = -_nU;
    }
    return m_csLocal.LocalToWorld().Applied(Point3(_nU, 0, 0));
}

//!< 反转
void iCAX::Geom::Ray3::Reverse()
{
    m_bReversed = !m_bReversed;
}

std::optional<Plane3> iCAX::Geom::Ray3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::Ray3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsON(Evaluate(0)) && pln_.IsON(Evaluate(1));
}
//!< 缩放和剪切
void iCAX::Geom::Ray3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    if (bMirrorX_)
    {
        m_bReversed = !m_bReversed;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Ray3::Clone() const
{
    return std::make_shared<Ray3>(m_csLocal.Location(), !m_bReversed ? m_csLocal.Location() + m_csLocal.XDirection().Vector() : m_csLocal.Location() - m_csLocal.XDirection().Vector());
}