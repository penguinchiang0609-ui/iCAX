#include "pch.h"
#include "RoundRect3.h"
#include <cmath>
#include <memory>
#include <algorithm>
#include "Segment3.h"
#include "Arc3.h"


using namespace iCAX::Geom;

//!< 构造函数
RoundRect3::RoundRect3(IN const Point3& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_, IN const bool& bCCW_/* = true*/, IN const Dir3& dirNormal_/* = Dir3::Up()*/, IN const Dir3& dirRight_/* = Dir3::Right()*/)
    : MltBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nWidth(nWidth_)
    , m_nHeight(nHeight_)
    , m_nRadius(nRadius_)
    , m_bCCW(bCCW_)
{
    // 限制圆角半径不超过矩形尺寸的一半
    if (m_nRadius > 0.5 * min(m_nWidth, m_nHeight))
        m_nRadius = 0.5 * min(m_nWidth, m_nHeight);

    ReCalcCache();
}

//!< 析构函数
RoundRect3::~RoundRect3()
{
}

//!< 设置尺寸
void iCAX::Geom::RoundRect3::SetSize(IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_)
{
    m_nWidth = nWidth_;
    m_nHeight = nHeight_;
    m_nRadius = nRadius_;

    ReCalcCache();
}

//!< 获取朝向
iCAX::Geom::CurveOrientType iCAX::Geom::RoundRect3::Orientation(IN const Dir3& dirNormal_) const
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
void iCAX::Geom::RoundRect3::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve3::Reverse();
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::RoundRect3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 缩放和剪切
void iCAX::Geom::RoundRect3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    m_nWidth *= nScale_[0];
    m_nHeight *= nScale_[1];
    m_nRadius *= min(nScale_[0], nScale_[1]);
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
    }
    ReCalcCache();
}

//!< Clone：深拷贝
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::RoundRect3::Clone() const
{
    return std::make_shared<RoundRect3>(m_csLocal.Location(), m_nWidth, m_nHeight, m_nRadius, m_bCCW, m_csLocal.ZDirection(), m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::RoundRect3::ReCalcCache()
{
    double _nHalfWidth = m_nWidth * 0.5;
    double _nHalfHeight = m_nHeight * 0.5;
    double _nRadius = m_nRadius;

    //!< ==== Step 1. 计算各圆角圆心 ====
    Point3 _ptLBCenter(-_nHalfWidth + _nRadius, -_nHalfHeight + _nRadius, .0);// 左下角圆心
    Point3 _ptRBCenter(_nHalfWidth - _nRadius, -_nHalfHeight + _nRadius, .0); // 右下角圆心
    Point3 _ptRTCenter(_nHalfWidth - _nRadius, _nHalfHeight - _nRadius, .0); // 右上角圆心
    Point3 _ptLTCenter(-_nHalfWidth + _nRadius, _nHalfHeight - _nRadius, .0); // 左上角圆心

    //!< step2: 计算各个节点
    Point3 _ptLB0(-_nHalfWidth + _nRadius, -_nHalfHeight, .0);
    Point3 _ptRB0(_nHalfWidth - _nRadius, -_nHalfHeight, .0);

    Point3 _ptRB1(_nHalfWidth, -_nHalfHeight + _nRadius, .0);
    Point3 _ptRT1(_nHalfWidth, _nHalfHeight - _nRadius, .0);

    Point3 _ptRT2(_nHalfWidth - _nRadius, _nHalfHeight, .0);
    Point3 _ptLT2(-_nHalfWidth + _nRadius, _nHalfHeight, .0);

    Point3 _ptLT3(-_nHalfWidth, _nHalfHeight - _nRadius, .0);
    Point3 _ptLT4(-_nHalfWidth, -_nHalfHeight + _nRadius, .0);

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptLB0, _ptRB0));//下线段
    m_lstSegments.push_back(std::make_shared<Arc3>(_ptRBCenter, _nRadius, 0, M_PI / 2.0, true, Dir3::Up(), Dir3::Back()));//右下圆弧
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptRB1, _ptRT1));//右线段
    m_lstSegments.push_back(std::make_shared<Arc3>(_ptRTCenter, _nRadius, 0, M_PI / 2.0, true, Dir3::Up(), Dir3::Right()));// 右上圆弧
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptRT2, _ptLT2));//上线段
    m_lstSegments.push_back(std::make_shared<Arc3>(_ptLTCenter, _nRadius, 0, M_PI / 2.0, true, Dir3::Up(), Dir3::Forward()));// 左上圆弧
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptLT3, _ptLT4));//左线段
    m_lstSegments.push_back(std::make_shared<Arc3>(_ptLBCenter, _nRadius, 0, M_PI / 2.0, true, Dir3::Up(), Dir3::Left()));// 左下线段
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve3::Reverse();
    }
}
