#include "pch.h"
#include "Ray2.h"

//!< 构造函数
iCAX::Geom::Ray2::Ray2(IN const Point2& ptStart_, IN const Point2& ptTarget_)
    : HalfBndCurve2(CoordSys2(ptStart_, Dir2(ptTarget_ - ptStart_)))
    , m_bReversed(false)
{
}

//!< 析构函数
iCAX::Geom::Ray2::~Ray2()
{
}

//!< 根据参数计算直线上点坐标
Point2 iCAX::Geom::Ray2::Evaluate(const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    if (m_bReversed)
    {
        _nU = -_nU;
    }
    return m_csLocal.LocalToWorld().Applied(Point2(_nU, 0));
}

//!< 反转
void iCAX::Geom::Ray2::Reverse()
{
    m_bReversed = !m_bReversed;
}

//!< 缩放和剪切
void iCAX::Geom::Ray2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    if (bMirrorX_)
    {
        m_bReversed = !m_bReversed;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Ray2::Clone() const
{
    return std::make_shared<Ray2>(m_csLocal.Location(), !m_bReversed ? m_csLocal.Location() + m_csLocal.XDirection().Vector() : m_csLocal.Location() - m_csLocal.XDirection().Vector());
}