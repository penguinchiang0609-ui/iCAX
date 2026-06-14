#include "pch.h"
#include "Ellipse2.h"

//!< 构造函数
iCAX::Geom::Ellipse2::Ellipse2(IN const Point2& ptCenter_, IN const double& nMajorRadius_, IN const double& nMinorRadius_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : SglBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nMajorRadius(nMajorRadius_)
    , m_nMinorRadius(nMinorRadius_)
    , m_bCCW(bCCW_)
{
}

//!< 析构函数
iCAX::Geom::Ellipse2::~Ellipse2()
{
}


//!<  获取长半轴（只读）
const double& iCAX::Geom::Ellipse2::MajorRadius() const
{
    return m_nMajorRadius;
}
//!< 获取短半轴（只读）
const double& iCAX::Geom::Ellipse2::MinorRadius() const
{
    return m_nMinorRadius;
}

//!< 设置尺寸
void iCAX::Geom::Ellipse2::SetSize(IN const double& nMajorRadius_, IN const double& nMinorRadius_)
{
    m_nMajorRadius = nMajorRadius_;
    m_nMinorRadius = nMinorRadius_;
}

//!< 根据参数得到角度
double iCAX::Geom::Ellipse2::GetTheta(IN const double& nU_) const
{
    double _nU = NormalizeParam(nU_);

    double _ndDeltaRad = m_bCCW ? 2 * M_PI : -2 * M_PI;
    return  _nU * _ndDeltaRad;
}

//!< 获取曲线起始参数
double iCAX::Geom::Ellipse2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线终止参数
double iCAX::Geom::Ellipse2::EndParam() const
{
    return 1.0;
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Ellipse2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 计算指定参数处的点坐标
Point2 iCAX::Geom::Ellipse2::Evaluate(IN const double& nU_) const
{
    double _nTheta = m_bCCW ? nU_ * 2 * M_PI : -nU_ * 2 * M_PI;
    return m_csLocal.LocalToWorld().Applied(Point2(m_nMajorRadius * cos(_nTheta), m_nMinorRadius * sin(_nTheta)));
}

//!< 反向
void iCAX::Geom::Ellipse2::Reverse()
{
    m_bCCW = !m_bCCW;
}

//!< 缩放和剪切
void iCAX::Geom::Ellipse2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nMajorRadius *= nScale_[0];
    m_nMinorRadius *= nScale_[1];
    if (bMirrorX_)
    {
        m_bCCW = !m_bCCW;
    }
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Ellipse2::Clone() const
{
    return std::make_shared<Ellipse2>(*this);
}
