#include "pch.h"
#include "Line2.h"

//!< 构造函数
iCAX::Geom::Line2::Line2(IN const Point2& pt0_, IN const Point2& pt1_)
    : UnBndCurve2(CoordSys2(pt0_, Dir2(pt1_ - pt0_)))
{
}

//!< 析构函数
iCAX::Geom::Line2::~Line2()
{
}

//!< 根据参数计算直线上点坐标
Point2 iCAX::Geom::Line2::Evaluate(const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    return m_csLocal.LocalToWorld().Applied(Point2(_nU, 0));
}

//!< 反转
void iCAX::Geom::Line2::Reverse()
{
}


//!< 缩放和剪切
void iCAX::Geom::Line2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    //!< 不用处理
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Line2::Clone() const
{
    return std::make_shared<Line2>(m_csLocal.Location(), m_csLocal.Location() + m_csLocal.XDirection().Vector());
}