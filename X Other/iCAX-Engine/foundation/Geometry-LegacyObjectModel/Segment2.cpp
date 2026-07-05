#include "pch.h"
#include "Segment2.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::Segment2::Segment2(/*IN const CoordSys2& csLocal_, */IN const Point2& ptStart_, IN const Point2& ptEnd_)
    : SglBndCurve2(CoordSys2(ptStart_.Lerp(ptEnd_, .5), Dir2(ptEnd_ - ptStart_)))
    , m_nLength((ptEnd_ - ptStart_).Magnitude())
{
}

//!< 析构函数
iCAX::Geom::Segment2::~Segment2()
{
}

//!< 设置点
void iCAX::Geom::Segment2::SetPoints(const Point2& ptStart_, const Point2& ptEnd_)
{
    m_csLocal = CoordSys2(ptStart_.Lerp(ptEnd_, .5), Dir2(ptEnd_ - ptStart_));
    m_nLength = (ptEnd_ - ptStart_).Magnitude();
}

//!< 获取曲线起始参数
double iCAX::Geom::Segment2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::Segment2::EndParam() const
{
    return 1.0;
}

//!< 在给定参数处计算点坐标
Point2 iCAX::Geom::Segment2::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    return m_csLocal.LocalToWorld().Applied(Point2(m_bReversed ? m_nLength / 2 - m_nLength * _nU : -m_nLength / 2 + m_nLength * _nU, 0));
}

//!< 反转
void iCAX::Geom::Segment2::Reverse()
{
    m_bReversed = !m_bReversed;
}

//!< 缩放和剪切
void iCAX::Geom::Segment2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nLength *= nScale_[0];

    if (bMirrorX_)
    {
        m_bReversed = !m_bReversed;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Segment2::Clone() const
{
    return std::make_shared<Segment2>(*this);
}
