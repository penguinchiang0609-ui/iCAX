#include "pch.h"
#include "RegularPolygon2.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include "Segment2.h"

using namespace iCAX::Geom;

//!< 构造函数
RegularPolygon2::RegularPolygon2(IN const Point2& ptCenter_, IN const int nEdges_, IN const double nRadius_, IN const bool bCCW_ /*= true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : MltBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nEdges(max(3, nEdges_))
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
RegularPolygon2::~RegularPolygon2() 
{
}

//!< 获取边数
int RegularPolygon2::Edges() const
{
    return m_nEdges; 
}

//!< 获取外接圆半径
const double& RegularPolygon2::Radius() const
{
    return m_nRadius;
}

//!< 设置边数
void RegularPolygon2::SetEdges(IN const int nEdges_)
{
    m_nEdges = max(3, nEdges_);
    ReCalcCache();
}

//!< 设置外接圆半径
void RegularPolygon2::SetRadius(IN const double nRadius_)
{
    m_nRadius = nRadius_;
    ReCalcCache();
}

//!< 曲线朝向
CurveOrientType RegularPolygon2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 反转方向
void RegularPolygon2::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve2::Reverse();
}

//!< 缩放和剪切
void iCAX::Geom::RegularPolygon2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nRadius *= min(nScale_[0], nScale_[1]);
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
    }

    ReCalcCache();
}

//!< 克隆
std::shared_ptr<Geometry> RegularPolygon2::Clone() const
{
    return std::make_shared<RegularPolygon2>(m_csLocal.Location(), m_nEdges, m_nRadius, m_bCCW, m_csLocal.XDirection());
}

//!< 重新计算缓存多边形
void RegularPolygon2::ReCalcCache()
{
    std::vector<Point2> _lstVertices;
    double _nDeltaTheta = 2.0 * M_PI / m_nEdges;
    double _nStartAngle = 0.0;

    for (int i = 0; i < m_nEdges; ++i)
    {
        double _nAngle = _nStartAngle + (m_bCCW ? i : -i) * _nDeltaTheta;
        _lstVertices.push_back(Point2(m_nRadius * cos(_nAngle), m_nRadius * sin(_nAngle)));
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
