#include "pch.h"
#include "Segment3.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::Segment3::Segment3(IN const Point3& ptStart_, IN const Point3& ptEnd_)
    : SglBndCurve3(CoordSys3(ptStart_.Lerp(ptEnd_, .5), Dir3(ptEnd_ - ptStart_)))
    , m_nLength((ptEnd_ - ptStart_).Magnitude())
    , m_bReversed(false)
{
}

//!< 析构函数
iCAX::Geom::Segment3::~Segment3()
{
}

//!< 设置点
void iCAX::Geom::Segment3::SetPoints(IN const Point3& ptStart_, IN const Point3& ptEnd_)
{
    m_csLocal = CoordSys3(ptStart_.Lerp(ptEnd_, .5), Dir3(ptEnd_ - ptStart_));
    m_nLength = (ptEnd_ - ptStart_).Magnitude();
}

//!< 获取曲线起始参数
double iCAX::Geom::Segment3::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::Segment3::EndParam() const
{
    return 1.0;
}

//!< 在给定参数处计算点坐标
Point3 iCAX::Geom::Segment3::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    return m_csLocal.LocalToWorld().Applied(Point3(m_bReversed ? m_nLength / 2 - m_nLength * _nU : -m_nLength / 2 + m_nLength * _nU, 0, 0));
}

//!< 反转
void iCAX::Geom::Segment3::Reverse()
{
    m_bReversed = !m_bReversed;
}

//!< 二维仿射变换
void iCAX::Geom::Segment3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorXoZ_)
{
    m_nLength *= nScale_[0];

    if (bMirrorXoZ_)//!< 镜像反映再Y轴
    {
        m_bReversed = !m_bReversed;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Segment3::Clone() const
{
    return std::make_shared<Segment3>(StartPoint(), EndPoint());
}

//!< 获取所在平面
std::optional<Plane3> iCAX::Geom::Segment3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 判断是否在指定平面内
bool iCAX::Geom::Segment3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsON(StartPoint()) && pln_.IsON(EndPoint());
}
