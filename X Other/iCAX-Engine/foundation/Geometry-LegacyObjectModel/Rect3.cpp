#include "pch.h"
#include "Rect3.h"
#include "Segment3.h"

//!< 构造函数
iCAX::Geom::Rect3::Rect3(IN const Point3& ptCenter_, IN const double& nWidth_, IN const double& nHeight_, IN const bool& bCCW_/* = true*/, IN const Dir3& dirNormal_/* = Dir3::Up()*/, IN const Dir3& dirRight_/* = Dir3::Right()*/)
    : MltBndCurve3(CoordSys3(ptCenter_, dirRight_, Dir3(dirNormal_.Vector().Cross(dirRight_.Vector()))))
    , m_nWidth(nWidth_)
    , m_nHeight(nHeight_)
    , m_bCCW(bCCW_)
{
    ReCalcCache();
}

//!< 析构函数
iCAX::Geom::Rect3::~Rect3()
{
}

//!< 宽度
const double& iCAX::Geom::Rect3::Width() const
{
    return m_nWidth;
}

//!< 获取高度
const double& iCAX::Geom::Rect3::Height() const
{
    return m_nHeight;
}

//!< 设置尺寸
void iCAX::Geom::Rect3::SetSize(IN const double& nWidth_, IN const double& nHeight_)
{
    m_nWidth = nWidth_;
    m_nHeight = nHeight_;
    ReCalcCache();//!< 重新计算
}

//!< 获取朝向
iCAX::Geom::CurveOrientType iCAX::Geom::Rect3::Orientation(IN const Dir3& dirNormal_) const
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
void iCAX::Geom::Rect3::Reverse()
{
    m_bCCW = !m_bCCW;
    MltBndCurve3::Reverse();
}

//!< 获取平面
std::optional<Plane3> iCAX::Geom::Rect3::SuggestPlane() const
{
    return Plane3(m_csLocal);
}

//!< 缩放和剪切
void iCAX::Geom::Rect3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
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
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Rect3::Clone() const
{
    return std::make_shared<Rect3>(m_csLocal.Location(), m_nWidth, m_nHeight, m_bCCW, m_csLocal.ZDirection(), m_csLocal.XDirection());
}

//!< 重新计算内部缓存多边形
void iCAX::Geom::Rect3::ReCalcCache()
{
    //!< 半宽半高
    double _nHalfWidth = m_nWidth * 0.5;
    double _nHalfHeight = m_nHeight * 0.5;

    //!< 顶点顺序：左下 -> 右下 -> 右上 -> 左上 -> 左下（闭合）
    Point3 _ptLB(-_nHalfWidth, -_nHalfHeight, .0);  //!< 左下
    Point3 _ptLR(_nHalfWidth, -_nHalfHeight, .0);   //!< 右下
    Point3 _ptTR(_nHalfWidth, _nHalfHeight, .0);   //!< 右上
    Point3 _ptTB(-_nHalfWidth, _nHalfHeight, .0);  //!< 左上

    m_lstSegments.clear();
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptLB, _ptLR));//!< 底部直线，左->右
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptLR, _ptTR));//!< 右侧直线，下->上
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptTR, _ptTB));//!< 顶部直线，右->左
    m_lstSegments.push_back(std::make_shared<Segment3>(_ptTB, _ptLB));//!< 左侧直线，上->下
    ReCalcParamTable();

    if (!m_bCCW)
    {
        MltBndCurve3::Reverse();
    }
}
