#include "pch.h"
#include "CubicBezier2.h"
#include <memory>
#include <algorithm>


//!< 构造函数
iCAX::Geom::CubicBezier2::CubicBezier2(IN const Point2& pt0_, IN const Point2& pt1_, IN const Point2& pt2_, IN const Point2& pt3_)
    : SglBndCurve2(CoordSys2())
    , m_pt0(pt0_)
    , m_pt1(pt1_)
    , m_pt2(pt2_)
    , m_pt3(pt3_)
{
}

//!< 默认析构函数
iCAX::Geom::CubicBezier2::~CubicBezier2()
{
}

//!< 起点
const Point2& iCAX::Geom::CubicBezier2::P0() const 
{
    return m_pt0; 
}
//!< 控制点1
const Point2& iCAX::Geom::CubicBezier2::P1() const
{
    return m_pt1;
}
//!< 控制点2
const Point2& iCAX::Geom::CubicBezier2::P2() const
{
    return m_pt2;
}
//!< 终点
const Point2& iCAX::Geom::CubicBezier2::P3() const
{
    return m_pt3;
}
//!< 设置起点
void iCAX::Geom::CubicBezier2::SetP0(IN const Point2& pt_)
{
    m_pt0 = pt_;
}
//!< 设置控制点1
void iCAX::Geom::CubicBezier2::SetP1(IN const Point2& pt_)
{
    m_pt1 = pt_;
}
//!< 设置控制点2
void iCAX::Geom::CubicBezier2::SetP2(IN const Point2& pt_)
{
    m_pt2 = pt_;
}
//!< 设置终点
void iCAX::Geom::CubicBezier2::SetP3(IN const Point2& pt_)
{
    m_pt3 = pt_;
}
//!< 获取曲线起始参数
double iCAX::Geom::CubicBezier2::StartParam() const
{
    return 0.0;
}
//!< 获取曲线终止参数
double iCAX::Geom::CubicBezier2::EndParam() const
{
    return 0.0;
}

//!< 计算三阶贝塞尔曲线上指定参数 u 对应的点
Point2 iCAX::Geom::CubicBezier2::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    double _nU1 = 1.0 - _nU;

    Point2 pt = _nU1 * _nU1 * _nU1 * m_pt0.XY()
        + 3 * _nU1 * _nU1 * _nU * m_pt1.XY()
        + 3 * _nU1 * _nU * _nU * m_pt2.XY()
        + _nU * _nU * _nU * m_pt3.XY();

    return m_csLocal.LocalToWorld().Applied(pt);
}

//!< 反转曲线方向
void iCAX::Geom::CubicBezier2::Reverse()
{
    std::swap(m_pt0, m_pt3);
    std::swap(m_pt1, m_pt2);
}

//!< 缩放和剪切
void iCAX::Geom::CubicBezier2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    m_pt0 = _mat.Applied(m_pt0);
    m_pt1 = _mat.Applied(m_pt1);
    m_pt2 = _mat.Applied(m_pt2);
    m_pt3 = _mat.Applied(m_pt3);
}

//!< 克隆曲线
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::CubicBezier2::Clone() const
{
    return std::make_shared<CubicBezier2>(*this);
}
