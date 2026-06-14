#include "pch.h"
#include "CubicBezier3.h"
#include <memory>
#include <algorithm>


//!< 构造函数
iCAX::Geom::CubicBezier3::CubicBezier3(IN const Point3& pt0_, IN const Point3& pt1_, IN const Point3& pt2_, IN const Point3& pt3_)
    : SglBndCurve3(CoordSys3())
    , m_pt0(pt0_)
    , m_pt1(pt1_)
    , m_pt2(pt2_)
    , m_pt3(pt3_)
{
}

//!< 默认析构函数
iCAX::Geom::CubicBezier3::~CubicBezier3()
{
}

//!< 起点
const Point3& iCAX::Geom::CubicBezier3::P0() const
{
    return m_pt0;
}
//!< 控制点1
const Point3& iCAX::Geom::CubicBezier3::P1() const
{
    return m_pt1;
}
//!< 控制点2
const Point3& iCAX::Geom::CubicBezier3::P2() const
{
    return m_pt2;
}
//!< 终点
const Point3& iCAX::Geom::CubicBezier3::P3() const
{
    return m_pt3;
}
//!< 设置起点
void iCAX::Geom::CubicBezier3::SetP0(IN const Point3& pt_)
{
    m_pt0 = pt_;
}
//!< 设置控制点1
void iCAX::Geom::CubicBezier3::SetP1(IN const Point3& pt_)
{
    m_pt1 = pt_;
}
//!< 设置控制点2
void iCAX::Geom::CubicBezier3::SetP2(IN const Point3& pt_)
{
    m_pt2 = pt_;
}
//!< 设置终点
void iCAX::Geom::CubicBezier3::SetP3(IN const Point3& pt_)
{
    m_pt3 = pt_;
}
//!< 获取曲线起始参数
double iCAX::Geom::CubicBezier3::StartParam() const
{
    return 0.0;
}
//!< 获取曲线终止参数
double iCAX::Geom::CubicBezier3::EndParam() const
{
    return 0.0;
}

//!< 计算三阶贝塞尔曲线上指定参数 u 对应的点
Point3 iCAX::Geom::CubicBezier3::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    double _nU1 = 1.0 - _nU;

    Point3 pt = _nU1 * _nU1 * _nU1 * m_pt0.XYZ()
        + 3 * _nU1 * _nU1 * _nU * m_pt1.XYZ()
        + 3 * _nU1 * _nU * _nU * m_pt2.XYZ()
        + _nU * _nU * _nU * m_pt3.XYZ();

    return m_csLocal.LocalToWorld().Applied(pt);
}

//!< 反转曲线方向
void iCAX::Geom::CubicBezier3::Reverse()
{
    std::swap(m_pt0, m_pt3);
    std::swap(m_pt1, m_pt2);
}

//!< 获取圆弧所在平面
std::optional<Plane3> iCAX::Geom::CubicBezier3::SuggestPlane() const
{
    return Plane3(m_pt0, m_pt1, m_pt2);
}

//!< 是否在平面上
bool iCAX::Geom::CubicBezier3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsON(m_pt0) && pln_.IsON(m_pt1) && pln_.IsON(m_pt2) && pln_.IsON(m_pt3);
}

//!< 缩放和剪切
void iCAX::Geom::CubicBezier3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    Tranform3 _mat = Tranform3::Shear(nShear_) * Tranform3::Scale(nScale_) * (bMirrorX_ ? Tranform3::MirrorX() : Tranform3::Identity());
    m_pt0 = _mat.Applied(m_pt0);
    m_pt1 = _mat.Applied(m_pt1);
    m_pt2 = _mat.Applied(m_pt2);
    m_pt3 = _mat.Applied(m_pt3);
}

//!< 克隆曲线
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::CubicBezier3::Clone() const
{
    return std::make_shared<CubicBezier3>(*this);
}
