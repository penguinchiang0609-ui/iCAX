#include "pch.h"
#include "Line3.h"

//!< 构造函数
iCAX::Geom::Line3::Line3(IN const Point3& pt0_, IN const Point3& pt1_)
    : UnBndCurve3(CoordSys3(pt0_, Dir3(pt1_ - pt0_)))
{
}

//!< 析构函数
iCAX::Geom::Line3::~Line3()
{
}

//!< 根据参数计算直线上点坐标
Point3 iCAX::Geom::Line3::Evaluate(const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    return m_csLocal.LocalToWorld().Applied(Point3(_nU, .0, .0));
}

//!< 反转
void iCAX::Geom::Line3::Reverse()
{
}

std::optional<Plane3> iCAX::Geom::Line3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::Line3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsON(Evaluate(1)) && pln_.IsON(Evaluate(-1));
}

//!< 缩放和剪切
void iCAX::Geom::Line3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    //!< 不用处理
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Line3::Clone() const
{
    return std::make_shared<Line3>(m_csLocal.Location(), m_csLocal.Location() + m_csLocal.XDirection().Vector());
}