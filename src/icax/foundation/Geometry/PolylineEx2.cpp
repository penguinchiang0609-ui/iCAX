#include "pch.h"
#include "PolylineEx2.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::PolylineEx2::PolylineEx2()
    : MltBndCurve2(CoordSys2())
{
}

//!< 二维闭合多边形曲线
iCAX::Geom::PolylineEx2::PolylineEx2(IN const std::vector<std::shared_ptr<SglBndCurve2>>& lstSegments_)
    : MltBndCurve2(CoordSys2())
{
    m_lstSegments = lstSegments_;
    if (m_lstSegments.empty())
        throw std::invalid_argument("PolygonEx2: segments cannot be empty");

    // 构建累加参数表
    ReCalcParamTable();
}

//!< 析构函数
iCAX::Geom::PolylineEx2::~PolylineEx2()
{
}

//!< 追加段
void iCAX::Geom::PolylineEx2::PushBack(IN const std::shared_ptr<SglBndCurve2>& pSeg_)
{
    if (!EndPoint().IsEqual(pSeg_->StartPoint()))
    {
        throw std::runtime_error("iCAX::Geom::PolylineEx2::Pushback end point not equal pSeg_->StartPoint");
    }
    m_lstSegments.push_back(pSeg_);
    //!< 此处性能考虑，不再重算，直接尾部追加
    double _nAcc = EndParam();
    double _nParamLen = pSeg_->EndParam() - pSeg_->StartParam(); //!< 段参数区间
    m_lstParams.push_back(std::make_pair(_nAcc, _nAcc + _nParamLen));
    //ReCalcParamTable();
}

//!< 反向
void iCAX::Geom::PolylineEx2::Reverse()
{
    MltBndCurve2::Reverse();
}

//!< 缩放和剪切
void iCAX::Geom::PolylineEx2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    for (auto& seg : m_lstSegments)
        seg->ScaleAndShear(nScale_, nShear_, bMirrorX_);
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::PolylineEx2::Clone() const
{
    auto _pEx = std::make_shared<PolylineEx2>();
    _pEx->Local() = Local();
    for (auto& _seg : m_lstSegments)
        _pEx->m_lstSegments.push_back(std::dynamic_pointer_cast<SglBndCurve2>(_seg->Clone()));
    _pEx->m_lstParams = m_lstParams;

    return _pEx;
}
