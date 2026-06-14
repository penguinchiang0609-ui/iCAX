#include "pch.h"
#include "CatmullRomSpline2.h"
#include <algorithm>
#include <cmath>

using namespace iCAX::Geom;
using namespace iCAX::Math;

//!< 构造函数
CatmullRomSpline2::CatmullRomSpline2(IN const std::vector<Point2>& lstCtrlPts_, IN const double& nTension_)
    : SglBndCurve2(CoordSys2())
    , m_lstCtrlPts(lstCtrlPts_)
    , m_nTension(nTension_)
{
    if (m_lstCtrlPts.size() < 4)
    {
        throw std::invalid_argument("CatmullRomSpline2: 至少需要 4 个控制点。");
    }
}

//!< 析构函数
CatmullRomSpline2::~CatmullRomSpline2()
{
}

//!< 获取控制点
const std::vector<Point2>& CatmullRomSpline2::ControlPoints() const
{
    return m_lstCtrlPts;
}

//!< 获取张力
const double& CatmullRomSpline2::Tension() const
{
    return m_nTension;
}

//!< 设置张力
void CatmullRomSpline2::SetTension(IN const double& nTension_)
{
    m_nTension = nTension_;
}

//!< 设置控制点列表
void iCAX::Geom::CatmullRomSpline2::SetCtrlPoints(IN const std::vector<Point2>& lstCtrlPts_)
{
    m_lstCtrlPts = lstCtrlPts_;
}

//!< 获取曲线终止参数
double iCAX::Geom::CatmullRomSpline2::StartParam() const
{
    return 0.0;
}

//!< 获取曲线朝向类型
double iCAX::Geom::CatmullRomSpline2::EndParam() const
{
    return 1.0;
}

//!< 参数点计算
Point2 CatmullRomSpline2::Evaluate(IN const double& nU_) const
{
    if (m_lstCtrlPts.size() < 4)
        return Point2();

    double _nU = NormalizeParam(nU_);
    size_t _nSegCount = m_lstCtrlPts.size() - 3;
    double _nSegLen = 1.0 / static_cast<double>(_nSegCount);
    size_t _nIdx = min(static_cast<size_t>(_nU / _nSegLen), _nSegCount - 1);
    double _nRatio = (_nU - _nIdx * _nSegLen) / _nSegLen;

    const Point2& _pt0 = m_lstCtrlPts[_nIdx];
    const Point2& _pt1 = m_lstCtrlPts[_nIdx + 1];
    const Point2& _pt2 = m_lstCtrlPts[_nIdx + 2];
    const Point2& _pt3 = m_lstCtrlPts[_nIdx + 3];

    double _nS = (1.0 - m_nTension) * 0.5;

    Point2 _ptResult =
        ((-_nS * _pt0.XY() + (2.0 - _nS) * _pt1.XY() + (_nS - 2.0) * _pt2.XY() + _nS * _pt3.XY()) * (_nRatio * _nRatio * _nRatio)) +
        ((2.0 * _nS * _pt0.XY() + (_nS - 3.0) * _pt1.XY() + (3.0 - 2.0 * _nS) * _pt2.XY() - _nS * _pt3.XY()) * (_nRatio * _nRatio)) +
        ((-_nS * _pt0.XY() + _nS * _pt2.XY()) * _nRatio) +
        _pt1.XY();

    return m_csLocal.LocalToWorld().Applied(_ptResult);
}

//!< 翻转方向
void CatmullRomSpline2::Reverse()
{
    std::reverse(m_lstCtrlPts.begin(), m_lstCtrlPts.end());
}

//!< 缩放和剪切
void CatmullRomSpline2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    for (auto& pt : m_lstCtrlPts)
        pt = _mat.Applied(pt);
}

//!< 克隆
std::shared_ptr<Geometry> CatmullRomSpline2::Clone() const
{
    return std::make_shared<CatmullRomSpline2>(*this);
}
