#include "pch.h"
#include "QuadraticBezier3.h"
#include <memory>
#include <algorithm>

//!< 构造函数
iCAX::Geom::QuadraticBezier3::QuadraticBezier3(IN const Point3& pt0_, IN const Point3& pt1_, IN const Point3& pt2_)
    : SglBndCurve3(CoordSys3()),
    m_pt0(pt0_)
    , m_pt1(pt1_)
    , m_pt2(pt2_)
{
}

//!< 默认析构函数
iCAX::Geom::QuadraticBezier3::~QuadraticBezier3()
{
}

//!< 起点
const Point3& iCAX::Geom::QuadraticBezier3::P0() const
{
    return m_pt0;
}
//!< 控制点1
const Point3& iCAX::Geom::QuadraticBezier3::P1() const
{
    return m_pt1;
}
//!< 终点
const Point3& iCAX::Geom::QuadraticBezier3::P2() const
{
    return m_pt2;
}
//!< 设置起点
void iCAX::Geom::QuadraticBezier3::SetP0(IN const Point3& pt_)
{
    m_pt0 = pt_;
}
//!< 设置控制点1
void iCAX::Geom::QuadraticBezier3::SetP1(IN const Point3& pt_)
{
    m_pt1 = pt_;
}
//!< 设置控制点2
void iCAX::Geom::QuadraticBezier3::SetP2(IN const Point3& pt_)
{
    m_pt2 = pt_;
}

//!< 获取曲线起始参数
double iCAX::Geom::QuadraticBezier3::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::QuadraticBezier3::EndParam() const
{
    return 1.0;
}

//!< 计算曲线上指定参数点坐标
Point3 iCAX::Geom::QuadraticBezier3::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    double _nU1 = 1.0 - _nU;
    Point3 pt = _nU1 * _nU1 * m_pt0.XYZ() + 2 * _nU1 * _nU * m_pt1.XYZ() + _nU * _nU * m_pt2.XYZ();
    return m_csLocal.LocalToWorld().Applied(pt);
}

//!< 反转曲线方向
void iCAX::Geom::QuadraticBezier3::Reverse()
{
    std::swap(m_pt0, m_pt2);
}

//!< 获取圆弧所在平面
std::optional<Plane3> iCAX::Geom::QuadraticBezier3::SuggestPlane() const
{
    return Plane3(m_pt0, m_pt1, m_pt2);
}

//!< 是否在平面上
bool iCAX::Geom::QuadraticBezier3::IsOnPlane(IN const Plane3& pln_) const
{
    return pln_.IsON(m_pt0) && pln_.IsON(m_pt1) && pln_.IsON(m_pt2);
}

//!< 对曲线执行二维仿射变换
void iCAX::Geom::QuadraticBezier3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    Tranform3 _mat = Tranform3::Shear(nShear_) * Tranform3::Scale(nScale_) * (bMirrorX_ ? Tranform3::MirrorX() : Tranform3::Identity());
    m_pt0 = _mat.Applied(m_pt0);
    m_pt1 = _mat.Applied(m_pt1);
    m_pt2 = _mat.Applied(m_pt2);
}
//!< 克隆曲线
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::QuadraticBezier3::Clone() const
{
    return std::make_shared<QuadraticBezier3>(*this);
}
