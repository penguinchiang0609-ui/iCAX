#include "pch.h"
#include "Star2.h"
#include <cmath>
#include <stdexcept>
#include <memory>
#include "Polyline2.h"
#include <algorithm>

using namespace iCAX::Geom;

//!< 构造函数
Star2::Star2(IN const Point2& ptCenter_, IN const int nPoints_, IN const double nOuterRadius_, IN const double nInnerRadius_, IN const bool bCCW_ /*= true*/, IN const Dir2& dirRight_ /*= Dir2::Right()*/)
    : MltBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nPoints(max(3, nPoints_))
    , m_nOuterRadius(nOuterRadius_ > 0 ? nOuterRadius_ : 1.0)
    , m_nInnerRadius(std::clamp(nInnerRadius_, 0.0, nOuterRadius_))
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
Star2::~Star2()
{
}

//!< 获取尖角数
int Star2::Points() const
{
    return m_nPoints;
}
//!< 获取外顶点半径
const double& Star2::OuterRadius() const
{
    return m_nOuterRadius;
}
//!< 获取内顶点半径
const double& Star2::InnerRadius() const
{
    return m_nInnerRadius;
}
//!< 设置尖角数
void Star2::SetPoints(IN const int nPoints_)
{
    m_nPoints = max(3, nPoints_);
    ReCalcCache();
}

//!< 设置外顶点半径
void Star2::SetOuterRadius(IN const double outerRadius_)
{
    if (outerRadius_ <= 0) throw std::runtime_error("Outer radius must be >0");
    m_nOuterRadius = outerRadius_;
    if (m_nInnerRadius > m_nOuterRadius)
        m_nInnerRadius = m_nOuterRadius * 0.5;
    ReCalcCache();
}

//!< 设置内顶点半径
void Star2::SetInnerRadius(IN const double innerRadius_)
{
    m_nInnerRadius = std::clamp(innerRadius_, 0.0, m_nOuterRadius);
    ReCalcCache();
}

//!< 曲线朝向
CurveOrientType Star2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< Reverse
void Star2::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve2::Reverse();
}

//!< 缩放和剪切
void iCAX::Geom::Star2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nOuterRadius *= min(nScale_[0], nScale_[1]);//!< 当不一致的缩放，选择小的作为缩放比例
    m_nInnerRadius *= min(nScale_[0], nScale_[1]);//!< 当不一致的缩放，选择小的作为缩放比例

    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
    }

    ReCalcCache();
}

//!< Clone
std::shared_ptr<Geometry> Star2::Clone() const
{
    return std::make_shared<Star2>(m_csLocal.Location(), m_nPoints, m_nOuterRadius, m_nInnerRadius, m_bCCW, m_csLocal.XDirection());
}

//!< ReCalcCache
void Star2::ReCalcCache()
{
    std::vector<Point2> _lstVertices;
    const double angleStep = M_PI / m_nPoints; // 内外顶点交替，每步 π/nPoints

    for (int i = 0; i < 2 * m_nPoints; ++i)
    {
        double _Radius = (i % 2 == 0) ? m_nOuterRadius : m_nInnerRadius;
        double _nAngle = i * angleStep;
        if (!m_bCCW) _nAngle = -_nAngle;

        _lstVertices.emplace_back(_Radius * cos(_nAngle), _Radius * sin(_nAngle));
    }
    Point2 _ptStart = _lstVertices.front();
    for (size_t i = 1; i < _lstVertices.size(); i++)
    {
        m_lstSegments.push_back(std::make_shared<Segment2>(_ptStart, _lstVertices[i]));
        _ptStart = _lstVertices[i];
    }
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptStart, _lstVertices[0]));
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve2::Reverse();
    }
}
