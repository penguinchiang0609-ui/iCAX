#include "pch.h"
#include "PolylineEx3.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::PolylineEx3::PolylineEx3()
    : MltBndCurve3(CoordSys3())
{
}

//!< 二维闭合多边形曲线
iCAX::Geom::PolylineEx3::PolylineEx3(IN const std::vector<std::shared_ptr<SglBndCurve3>>& lstSegments_)
    : MltBndCurve3(CoordSys3())
{
    m_lstSegments = lstSegments_;
    if (m_lstSegments.empty())
        throw std::invalid_argument("PolygonEx2: segments cannot be empty");

    // 构建累加参数表
    ReCalcParamTable();
}

//!< 析构函数
iCAX::Geom::PolylineEx3::~PolylineEx3()
{
}

//!< 追加段
void iCAX::Geom::PolylineEx3::PushBack(IN const std::shared_ptr<SglBndCurve3>& pSeg_)
{
    if (!EndPoint().IsEqual(pSeg_->StartPoint()))
    {
        throw std::runtime_error("iCAX::Geom::PolylineEx3::Pushback end point not equal pSeg_->StartPoint");
    }
    m_lstSegments.push_back(pSeg_);
    //!< 此处性能考虑，不再重算，直接尾部追加
    double _nAcc = EndParam();
    double _nParamLen = pSeg_->EndParam() - pSeg_->StartParam(); //!< 段参数区间
    m_lstParams.push_back(std::make_pair(_nAcc, _nAcc + _nParamLen));
    //ReCalcParamTable();
}

//!< 反向
void iCAX::Geom::PolylineEx3::Reverse()
{
    MltBndCurve3::Reverse();
}

std::optional<Plane3> iCAX::Geom::PolylineEx3::SuggestPlane() const
{
    return std::optional<Plane3>();
}

//!< 缩放和剪切
void iCAX::Geom::PolylineEx3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    for (auto& seg : m_lstSegments)
        seg->ScaleAndShear(nScale_, nShear_, bMirrorX_);
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::PolylineEx3::Clone() const
{
    auto _pEx = std::make_shared<PolylineEx3>();
    _pEx->Local() = Local();
    for (auto& _seg : m_lstSegments)
        _pEx->m_lstSegments.push_back(std::dynamic_pointer_cast<SglBndCurve3>(_seg->Clone()));
    _pEx->m_lstParams = m_lstParams;

    return _pEx;
}
