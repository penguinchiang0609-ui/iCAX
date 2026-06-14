#include "pch.h"
#include "Circle2.h"
#include "../Math/CoordSys2.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::Circle2::Circle2(IN const Point2& ptCenter_, IN const double& nRadius_, IN const bool& bCCW_ /*= true*/, IN const Dir2& dirRight_ /*= Dir2::Right()*/)
    : SglBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
iCAX::Geom::Circle2::~Circle2()
{
}

//!< 半径
const double& iCAX::Geom::Circle2::Radius() const
{
    return m_nRadius;
}

//!< 设置尺寸
void iCAX::Geom::Circle2::SetSize(IN const double& nRadius_)
{
    m_nRadius = nRadius_;
}

//!< 根据参数得到角度
double iCAX::Geom::Circle2::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);;

    double _ndDeltaRad = m_bCCW ? 2 * M_PI : -2 * M_PI;
    return  _nU * _ndDeltaRad;
}

//!< 起始参数
double iCAX::Geom::Circle2::StartParam() const
{
    return 0.0;
}
//!< 截止参数
double iCAX::Geom::Circle2::EndParam() const
{
    return 1.0;
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Circle2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 计算指定参数处点坐标
Point2 iCAX::Geom::Circle2::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);
    Point2 _pt = Point2(m_nRadius * cos(_nTheta), m_nRadius * sin(_nTheta));
    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反向
void iCAX::Geom::Circle2::Reverse()
{
    m_bCCW = !m_bCCW;
}

//!< 缩放和剪切
void iCAX::Geom::Circle2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nRadius *= min(nScale_[0], nScale_[1]);
    if (bMirrorX_)
    {
        m_bCCW = !m_bCCW;
    }

}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Circle2::Clone() const
{
    return std::make_shared<Circle2>(*this);
}

