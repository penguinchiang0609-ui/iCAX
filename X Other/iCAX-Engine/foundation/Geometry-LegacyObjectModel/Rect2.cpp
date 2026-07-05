#include "pch.h"
#include "Rect2.h"
#include "Polyline2.h"

//!< 构造函数
iCAX::Geom::Rect2::Rect2(IN const Point2& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const bool& bCCW_/* = true*/, IN const Dir2& dirRight_/* = Dir2::Right()*/)
    : MltBndCurve2(CoordSys2(ptCenter_, dirRight_))
    , m_nWidth(nWidth_)
    , m_nHeight(nHeight_)
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
iCAX::Geom::Rect2::~Rect2()
{
}

//!< 宽度
const double& iCAX::Geom::Rect2::Width() const
{
    return m_nWidth;
}

//!< 获取高度
const double& iCAX::Geom::Rect2::Height() const
{
    return m_nHeight;
}

//!< 设置尺寸
void iCAX::Geom::Rect2::SetSize(IN const double& nWidth_, IN const double& nHeight_)
{
    m_nWidth = nWidth_;
    m_nHeight = nHeight_;
    ReCalcCache();//!< 重新计算
}

//!< 获取曲线朝向类型
iCAX::Geom::CurveOrientType iCAX::Geom::Rect2::Orientation() const
{
    return m_bCCW ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 反转
void iCAX::Geom::Rect2::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve2::Reverse();
}

//!< 缩放和剪切
void iCAX::Geom::Rect2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    m_nWidth *= nScale_[0];
    m_nHeight *= nScale_[1];
    if (bMirrorX_)//!< 拆解出来的镜像统一为针对X轴镜像
    {
        m_bCCW = !m_bCCW;
    }

    ReCalcCache();
}

//!< Clone：深拷贝
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Rect2::Clone() const
{
    return std::make_shared<Rect2>(m_csLocal.Location(), m_nWidth, m_nHeight, m_bCCW, m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::Rect2::ReCalcCache()
{
    //!< 半宽半高
    double _nHalfWidth = m_nWidth * 0.5;
    double _nHalfHeight = m_nHeight * 0.5;

    //!< 顶点顺序：左下 -> 右下 -> 右上 -> 左上 -> 左下（闭合）
    Point2 _ptLB(-_nHalfWidth, -_nHalfHeight);  //!< 左下
    Point2 _ptLR(_nHalfWidth, -_nHalfHeight);   //!< 右下
    Point2 _ptTR(_nHalfWidth, _nHalfHeight);   //!< 右上
    Point2 _ptTB(-_nHalfWidth, _nHalfHeight);  //!< 左上

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptLB, _ptLR));//!< 底部直线，左->右
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptLR, _ptTR));//!< 右侧直线，下->上
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptTR, _ptTB));//!< 顶部直线，右->左
    m_lstSegments.push_back(std::make_shared<Segment2>(_ptTB, _ptLB));//!< 左侧直线，上->下
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve2::Reverse();
    }
}
