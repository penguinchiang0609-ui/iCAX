#include "pch.h"
#include "Runway2.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include "Segment2.h"
#include "Arc2.h"

//!< 构造函数
iCAX::Geom::Runway2::Runway2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nRadius_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_ /*= Dir2::Right()*/)
    : MltBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nWidth(nWidth_)
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
iCAX::Geom::Runway2::~Runway2()
{
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Runway2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 反转
void iCAX::Geom::Runway2::Reverse()
{
    m_bCCW = !m_bCCW;
    if (!m_bCCW)
    {
        std::reverse(m_lstSegments.begin(), m_lstSegments.end());
        for (size_t i = 0; i < m_lstSegments.size(); i++)
        {
            m_lstSegments[i]->Reverse();
        }
    }
    ReCalcParamTable();
}

//!< 缩放和剪切
void iCAX::Geom::Runway2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nWidth *= nScale_[0];
    m_nRadius *= nScale_[1];
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
    }

    ReCalcCache();
}

//!< Clone
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Runway2::Clone() const
{
    return std::make_shared<Runway2>(m_csLocal.Location(), m_nWidth, m_nRadius, m_bCCW, m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::Runway2::ReCalcCache()
{
    double _nHalfWidth = m_nWidth * 0.5;
    double _nRadius = m_nRadius;

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment2>(Point2(-_nHalfWidth, -_nRadius), Point2(_nHalfWidth, -_nRadius)));//!< 底部直线，左->右
    m_lstSegments.push_back(std::make_shared<Arc2>(Point2(_nHalfWidth ,0), _nRadius, .0, M_PI, true, Dir2::Down()));//!< 右侧半圆，自身坐标系X朝下，半圆
    m_lstSegments.push_back(std::make_shared<Segment2>(Point2(_nHalfWidth, _nRadius), Point2(-_nHalfWidth, _nRadius)));//!< 顶部直线，右->左
    m_lstSegments.push_back(std::make_shared<Arc2>(Point2(-_nHalfWidth ,0), _nRadius, .0, M_PI, true, Dir2::Up()));//!< 左侧半圆，自身坐标系X朝上，半圆
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve2::Reverse();
    }
}