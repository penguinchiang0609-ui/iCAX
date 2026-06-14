#include "pch.h"
#include "Arc2.h"
#include <cmath>
#include <memory>
#include <algorithm>
#include "../Data/CommonFunction.h"

//!< 构造函数
iCAX::Geom::Arc2::Arc2(IN const Point2& ptCenter_, IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : SglBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nRadius(nRadius_)
    , m_nStartRad(nStartRad_)
    , m_nEndRad(nEndRad_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
iCAX::Geom::Arc2::~Arc2()
{
}

//!< 半径
const double& iCAX::Geom::Arc2::Radius() const
{
    return m_nRadius;
}

//!< 起始角
const double& iCAX::Geom::Arc2::StartAngle() const
{
    return m_nStartRad;
}

//!< 结束角
const double& iCAX::Geom::Arc2::EndAngle() const
{
    return m_nEndRad;
}

//!< 是否为逆时针方向
const bool& iCAX::Geom::Arc2::IsCCW() const
{
    return m_bCCW;
}
//!< 是否为逆时针方向
bool& iCAX::Geom::Arc2::IsCCW()
{
    return m_bCCW;
}

//!< 设置尺寸
void iCAX::Geom::Arc2::SetSize(IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_)
{
    m_nRadius = nRadius_;
    m_nStartRad = nStartRad_;
    m_nEndRad = nEndRad_;
    m_bCCW = bCCW_;
}

//!< 根据参数得到角度
double iCAX::Geom::Arc2::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);//!< 归一化

    double _ndDeltaRad = m_nEndRad - m_nStartRad;
    if (m_bCCW && _ndDeltaRad < 0)
        _ndDeltaRad += 2 * M_PI;
    else if (!m_bCCW && _ndDeltaRad > 0)
        _ndDeltaRad -= 2 * M_PI;

    return m_nStartRad + _nU * _ndDeltaRad;
}

//!< 获取曲线起始参数
double iCAX::Geom::Arc2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::Arc2::EndParam() const
{
    return 1.0;//!< 此处选择0-1区间的主要原因是为了屏蔽顺逆时针对于上层调用的影响。保证上层调用者可以统一使用0-1的数值来达到想要的采样或取点
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Arc2::Orientation() const
{
    if (!IsClosed())
    {
        return kCrvOrientUnknown;
    }

    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 计算圆弧上指定角度对应的点
Point2 iCAX::Geom::Arc2::Evaluate(IN const double& nU_) const
{
    double _nTheta = GetTheta(nU_);
    Point2 _pt = Point2(m_nRadius * cos(_nTheta), m_nRadius * sin(_nTheta));
    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反转方向
void iCAX::Geom::Arc2::Reverse()
{
    std::swap(m_nStartRad, m_nEndRad);
    m_bCCW = !m_bCCW;
}

//!< 缩放和剪切
void iCAX::Geom::Arc2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nRadius *= min(nScale_[0], nScale_[1]);
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对Y轴镜像
    {
        m_bCCW = !m_bCCW;
        m_nStartRad = wrap(M_PI - m_nStartRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
        m_nEndRad = wrap(M_PI - m_nEndRad, .0, 2 * M_PI);//!< 归一化到0-2pi之间
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Arc2::Clone() const
{
    return std::make_shared<Arc2>(*this);
}
