#include "pch.h"
#include "QuadraticBezier2.h"
#include <memory>
#include <algorithm>

//!< 构造函数
iCAX::Geom::QuadraticBezier2::QuadraticBezier2(IN const Point2& pt0_, IN const Point2& pt1_, IN const Point2& pt2_)
    : SglBndCurve2(CoordSys2()),
    m_pt0(pt0_)
    , m_pt1(pt1_)
    , m_pt2(pt2_)
{
}

//!< 默认析构函数
iCAX::Geom::QuadraticBezier2::~QuadraticBezier2() 
{
}

//!< 起点
const Point2& iCAX::Geom::QuadraticBezier2::P0() const
{
    return m_pt0;
}
//!< 控制点1
const Point2& iCAX::Geom::QuadraticBezier2::P1() const
{
    return m_pt1;
}
//!< 终点
const Point2& iCAX::Geom::QuadraticBezier2::P2() const
{
    return m_pt2; 
}
//!< 设置起点
void iCAX::Geom::QuadraticBezier2::SetP0(IN const Point2& pt_)
{
    m_pt0 = pt_;
}
//!< 设置控制点1
void iCAX::Geom::QuadraticBezier2::SetP1(IN const Point2& pt_)
{
    m_pt1 = pt_;
}
//!< 设置控制点2
void iCAX::Geom::QuadraticBezier2::SetP2(IN const Point2& pt_)
{
    m_pt2 = pt_;
}

//!< 获取曲线起始参数
double iCAX::Geom::QuadraticBezier2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::QuadraticBezier2::EndParam() const
{
    return 1.0;
}

//!< 计算曲线上指定参数点坐标
Point2 iCAX::Geom::QuadraticBezier2::Evaluate(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);
    double _nU1 = 1.0 - _nU;
    Point2 pt = _nU1 * _nU1 * m_pt0.XY() + 2 * _nU1 * _nU * m_pt1.XY() + _nU * _nU * m_pt2.XY();
    return m_csLocal.LocalToWorld().Applied(pt);
}

//!< 反转曲线方向
void iCAX::Geom::QuadraticBezier2::Reverse()
{
    std::swap(m_pt0, m_pt2);
}

//!< 缩放和剪切
void iCAX::Geom::QuadraticBezier2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    m_pt0 = _mat.Applied(m_pt0);
    m_pt1 = _mat.Applied(m_pt1);
    m_pt2 = _mat.Applied(m_pt2);

}
//!< 克隆曲线
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::QuadraticBezier2::Clone() const
{
    return std::make_shared<QuadraticBezier2>(*this);
}
