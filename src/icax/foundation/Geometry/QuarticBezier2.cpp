#include "pch.h"
#include "QuarticBezier2.h"
#include <memory>
#include <algorithm>

//!< 构造函数
iCAX::Geom::QuarticBezier2::QuarticBezier2(IN const Point2& pt0_, IN const Point2& pt1_, IN const Point2& pt2_, IN const Point2& pt3_, IN const Point2& pt4_)
    : SglBndCurve2(CoordSys2())
    , m_pt0(pt0_)
    , m_pt1(pt1_)
    , m_pt2(pt2_)
    , m_pt3(pt3_)
    , m_pt4(pt4_)
{
}

//!< 析构函数
iCAX::Geom::QuarticBezier2::~QuarticBezier2()
{
}

//!< 起点
const Point2& iCAX::Geom::QuarticBezier2::P0() const
{
    return m_pt0;
}
//!< 控制点1
const Point2& iCAX::Geom::QuarticBezier2::P1() const
{
    return m_pt1;
}
//!< 控制点2
const Point2& iCAX::Geom::QuarticBezier2::P2() const
{
    return m_pt2;
}
//!< 控制点3
const Point2& iCAX::Geom::QuarticBezier2::P3() const
{
    return m_pt3;
}
//!< 终点
const Point2& iCAX::Geom::QuarticBezier2::P4() const
{
    return m_pt4;
}
//!< 设置起点
void iCAX::Geom::QuarticBezier2::SetP0(IN const Point2& pt_)
{
    m_pt0 = pt_;
}
//!< 设置控制点1
void iCAX::Geom::QuarticBezier2::SetP1(IN const Point2& pt_)
{
    m_pt1 = pt_;
}
//!< 设置控制点2
void iCAX::Geom::QuarticBezier2::SetP2(IN const Point2& pt_)
{
    m_pt2 = pt_;
}
//!< 设置控制点3
void iCAX::Geom::QuarticBezier2::SetP3(IN const Point2& pt_)
{
    m_pt3 = pt_;
}
//!< 设置终点
void iCAX::Geom::QuarticBezier2::SetP4(IN const Point2& pt_)
{
    m_pt4 = pt_;
}

//!< 获取曲线起始参数
double iCAX::Geom::QuarticBezier2::StartParam() const
{
    return 0.0;
}
//!< 获取曲线终止参数
double iCAX::Geom::QuarticBezier2::EndParam() const
{
    return 0.0;
}
//!< 计算指定参数处点坐标
Point2 iCAX::Geom::QuarticBezier2::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    double _nU1 = 1.0 - _nU;
    Point2 pt = _nU1 * _nU1 * _nU1 * _nU1 * m_pt0.XY()
        + 4 * _nU1 * _nU1 * _nU1 * _nU * m_pt1.XY()
        + 6 * _nU1 * _nU1 * _nU * _nU * m_pt2.XY()
        + 4 * _nU1 * _nU * _nU * _nU * m_pt3.XY()
        + _nU * _nU * _nU * _nU * m_pt4.XY();
    return m_csLocal.LocalToWorld().Applied(pt);
}

//!< 反转曲线方向
void iCAX::Geom::QuarticBezier2::Reverse()
{
    std::swap(m_pt0, m_pt4);
    std::swap(m_pt1, m_pt3);
    // m_pt2 保持在中间不变
}

//!< 缩放和剪切
void iCAX::Geom::QuarticBezier2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    m_pt0 = _mat.Applied(m_pt0);
    m_pt1 = _mat.Applied(m_pt1);
    m_pt2 = _mat.Applied(m_pt2);
    m_pt3 = _mat.Applied(m_pt3);
    m_pt4 = _mat.Applied(m_pt4);
}

//!< 克隆曲线
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::QuarticBezier2::Clone() const
{
    return std::make_shared<QuarticBezier2>(*this);
}
