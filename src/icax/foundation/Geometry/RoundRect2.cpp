#include "pch.h"
#include "RoundRect2.h"
#include <cmath>
#include <memory>
#include <algorithm>
#include "Arc2.h"
#include "Segment2.h"

using namespace iCAX::Geom;

//!< 构造函数
RoundRect2::RoundRect2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : MltBndCurve2(CoordSys2(ptCenter_, dirRight_))
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
RoundRect2::~RoundRect2() 
{
}

//!< 设置尺寸
void iCAX::Geom::RoundRect2::SetSize(IN const double& nWidth_, IN const double& nHeight_, IN const double& nRadius_)
{
    m_nWidth = nWidth_;
    m_nHeight = nHeight_;
    m_nRadius = nRadius_;

    ReCalcCache();
}

//!< 获取曲线朝向类型
CurveOrientType iCAX::Geom::RoundRect2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}


//!< 反转
void iCAX::Geom::RoundRect2::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve2::Reverse();
}

//!< 缩放和剪切
void iCAX::Geom::RoundRect2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
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
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::RoundRect2::Clone() const
{
    return std::make_shared<RoundRect2>(m_csLocal.Location(), m_nWidth, m_nHeight, m_nRadius, m_bCCW, m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::RoundRect2::ReCalcCache()
{
    double _nHalfWidth = m_nWidth * 0.5;
    double _nHalfHeight = m_nHeight * 0.5;
    double _nRadius = m_nRadius;

    //!< ==== Step 1. 计算各圆角圆心 ====
    Point2 _ptLBCenter(-_nHalfWidth + _nRadius, -_nHalfHeight + _nRadius);// 左下角圆心
    Point2 _ptRBCenter(_nHalfWidth - _nRadius, -_nHalfHeight + _nRadius); // 右下角圆心
    Point2 _ptRTCenter(_nHalfWidth - _nRadius, _nHalfHeight - _nRadius); // 右上角圆心
    Point2 _ptLTCenter(-_nHalfWidth + _nRadius, _nHalfHeight - _nRadius); // 左上角圆心

    //!< step2: 计算各个节点
    Point2 _ptLB0(-_nHalfWidth + _nRadius, -_nHalfHeight);
    Point2 _ptRB0(_nHalfWidth - _nRadius, -_nHalfHeight);

    Point2 _ptRB1(_nHalfWidth, -_nHalfHeight + _nRadius);
    Point2 _ptRT1(_nHalfWidth, _nHalfHeight - _nRadius);

    Point2 _ptRT2(_nHalfWidth - _nRadius, _nHalfHeight);
    Point2 _ptLT2(-_nHalfWidth + _nRadius, _nHalfHeight);

    Point2 _ptLT3(-_nHalfWidth, _nHalfHeight - _nRadius);
    Point2 _ptLT4(-_nHalfWidth, -_nHalfHeight + _nRadius);

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptLB0, _ptRB0));//下线段
    m_lstSegments.push_back(std::make_shared<Arc2>(_ptRBCenter, _nRadius, 0, M_PI / 2.0, true, Dir2::Down()));//右下圆弧
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptRB1, _ptRT1));//右线段
    m_lstSegments.push_back(std::make_shared<Arc2>(_ptRTCenter, _nRadius, 0, M_PI / 2.0, true, Dir2::Right()));// 右上圆弧
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptRT2, _ptLT2));//上线段
    m_lstSegments.push_back(std::make_shared<Arc2>(_ptLTCenter, _nRadius, 0, M_PI / 2.0, true, Dir2::Up()));// 左上圆弧
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptLT3, _ptLT4));//左线段
    m_lstSegments.push_back(std::make_shared<Arc2>(_ptLBCenter, _nRadius, 0, M_PI / 2.0, true, Dir2::Left()));// 左下线段
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve2::Reverse();
    }
}
