#include "pch.h"
#include "CatmullRomSpline3.h"
#include <algorithm>
#include <cmath>

using namespace iCAX::Geom;
using namespace iCAX::Math;

//!< 构造函数
CatmullRomSpline3::CatmullRomSpline3(IN const std::vector<Point3>& lstCtrlPts_, IN const double& nTension_)
    : SglBndCurve3(CoordSys3())
    , m_lstCtrlPts(lstCtrlPts_)
    , m_nTension(nTension_)
{
    if (m_lstCtrlPts.size() < 4)
    {
        throw std::invalid_argument("CatmullRomSpline3: 至少需要 4 个控制点。");
    }
}

//!< 析构函数
CatmullRomSpline3::~CatmullRomSpline3()
{
}

//!< 获取控制点
const std::vector<Point3>& CatmullRomSpline3::ControlPoints() const
{
    return m_lstCtrlPts;
}

//!< 获取张力
const double& CatmullRomSpline3::Tension() const
{
    return m_nTension;
}

//!< 设置张力
void CatmullRomSpline3::SetTension(IN const double& nTension_)
{
    m_nTension = nTension_;
}

//!< 设置控制点列表
void iCAX::Geom::CatmullRomSpline3::SetCtrlPoints(IN const std::vector<Point3>& lstCtrlPts_)
{
    m_lstCtrlPts = lstCtrlPts_;
}

//!< 获取曲线终止参数
double iCAX::Geom::CatmullRomSpline3::StartParam() const
{
    return 0.0;
}

//!< 获取曲线朝向类型
double iCAX::Geom::CatmullRomSpline3::EndParam() const
{
    return 1.0;
}

//!< 参数点计算
Point3 CatmullRomSpline3::Evaluate(IN const double& nU_) const
{
    if (m_lstCtrlPts.size() < 4)
        return Point3();

    double _nU = NormalizeParam(nU_);
    size_t _nSegCount = m_lstCtrlPts.size() - 3;
    double _nSegLen = 1.0 / static_cast<double>(_nSegCount);
    size_t _nIdx = min(static_cast<size_t>(_nU / _nSegLen), _nSegCount - 1);
    double _nRatio = (_nU - _nIdx * _nSegLen) / _nSegLen;

    const Point3& _pt0 = m_lstCtrlPts[_nIdx];
    const Point3& _pt1 = m_lstCtrlPts[_nIdx + 1];
    const Point3& _pt2 = m_lstCtrlPts[_nIdx + 2];
    const Point3& _pt3 = m_lstCtrlPts[_nIdx + 3];

    double _nS = (1.0 - m_nTension) * 0.5;

    Point3 _ptResult =
        ((-_nS * _pt0.XYZ() + (2.0 - _nS) * _pt1.XYZ() + (_nS - 2.0) * _pt2.XYZ() + _nS * _pt3.XYZ()) * (_nRatio * _nRatio * _nRatio)) +
        ((2.0 * _nS * _pt0.XYZ() + (_nS - 3.0) * _pt1.XYZ() + (3.0 - 2.0 * _nS) * _pt2.XYZ() - _nS * _pt3.XYZ()) * (_nRatio * _nRatio)) +
        ((-_nS * _pt0.XYZ() + _nS * _pt2.XYZ()) * _nRatio) +
        _pt1.XYZ();

    return m_csLocal.LocalToWorld().Applied(_ptResult);
}

//!< 翻转方向
void CatmullRomSpline3::Reverse()
{
    std::reverse(m_lstCtrlPts.begin(), m_lstCtrlPts.end());
}

//!< 获取圆弧所在平面
std::optional<Plane3> iCAX::Geom::CatmullRomSpline3::SuggestPlane() const
{
    return Plane3(m_lstCtrlPts[0], m_lstCtrlPts[1], m_lstCtrlPts[2]);
}

//!< 是否在平面上
bool iCAX::Geom::CatmullRomSpline3::IsOnPlane(IN const Plane3& pln_) const
{
    for (size_t i = 0; i < m_lstCtrlPts.size(); i++)
    {
        if (!pln_.IsON(m_lstCtrlPts[i]))
        {
            return false;
        }
    }

    return true;
}

//!< 缩放和剪切
void CatmullRomSpline3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    Tranform3 _mat = Tranform3::Shear(nShear_) * Tranform3::Scale(nScale_) * (bMirrorX_ ? Tranform3::MirrorX() : Tranform3::Identity());
    for (auto& pt : m_lstCtrlPts)
        pt = _mat.Applied(pt);
}

//!< 克隆
std::shared_ptr<Geometry> CatmullRomSpline3::Clone() const
{
    return std::make_shared<CatmullRomSpline3>(*this);
}
