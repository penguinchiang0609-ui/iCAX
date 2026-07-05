#include "pch.h"
#include "Runway3.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include "Segment3.h"
#include "Arc3.h"

//!< 构造函数
iCAX::Geom::Runway3::Runway3(IN const Point3& ptCenter_, IN const double& nWidth_, IN const double& nRadius_, IN const bool& bCCW_ /*= true*/, IN const Dir3& dirNormal_/* = Dir3::Up()*/, IN const Dir3& dirRight_/* = Dir3::Right()*/)
    : MltBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nWidth(nWidth_)
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
iCAX::Geom::Runway3::~Runway3()
{
}

//!< 获取朝向
iCAX::Geom::CurveOrientType iCAX::Geom::Runway3::Orientation(IN const Dir3& dirNormal_) const
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

//!< 反转
void iCAX::Geom::Runway3::Reverse()
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

//!< 获取平面
std::optional<Plane3> iCAX::Geom::Runway3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 缩放和剪切
void iCAX::Geom::Runway3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
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
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Runway3::Clone() const
{
    return std::make_shared<Runway3>(m_csLocal.Location(), m_nWidth, m_nRadius, m_bCCW, m_csLocal.ZDirection(), m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::Runway3::ReCalcCache()
{
    double _nHalfWidth = m_nWidth * 0.5;
    double _nRadius = m_nRadius;

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment3>(Point3(-_nHalfWidth, -_nRadius,.0), Point3(_nHalfWidth, -_nRadius, .0)));//!< 底部直线，左->右
    m_lstSegments.push_back(std::make_shared<Arc3>(Point3(_nHalfWidth, 0, .0), _nRadius, .0, M_PI, true, Dir3::Up(), Dir3::Back()));//!< 右侧半圆，自身坐标系X朝下，半圆
    m_lstSegments.push_back(std::make_shared<Segment3>(Point3(_nHalfWidth, _nRadius, .0), Point3(-_nHalfWidth, _nRadius, .0)));//!< 顶部直线，右->左
    m_lstSegments.push_back(std::make_shared<Arc3>(Point3(-_nHalfWidth, 0, .0), _nRadius, .0, M_PI, true, Dir3::Up(), Dir3::Forward()));//!< 左侧半圆，自身坐标系X朝上，半圆
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve3::Reverse();
    }
}