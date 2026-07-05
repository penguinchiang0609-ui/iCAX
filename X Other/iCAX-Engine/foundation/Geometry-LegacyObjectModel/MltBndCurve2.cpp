#include "pch.h"
#include "MltBndCurve2.h"

//!< 构造函数
iCAX::Geom::MltBndCurve2::MltBndCurve2(IN const CoordSys2& csLocal_)
    : BndCurve2(csLocal_)
    , m_lstSegments()
    , m_lstParams()
{
}

//!< 默认析构函数
iCAX::Geom::MltBndCurve2::~MltBndCurve2()
{
}


//!< 段列表
const std::vector<std::shared_ptr<iCAX::Geom::SglBndCurve2>>& iCAX::Geom::MltBndCurve2::Segments() const
{
    return m_lstSegments;
}

//!< 顶点列表
const std::vector<Point2> iCAX::Geom::MltBndCurve2::GetVertices() const
{
    std::vector<Point2> _lstVerts;
    for (auto& seg : m_lstSegments)
        _lstVerts.push_back(seg->Evaluate(seg->StartParam()));

    if (!_lstVerts.empty())
        _lstVerts.push_back(_lstVerts.front());

    return _lstVerts;
}

//!< 解析u得到段以及段内参数
void iCAX::Geom::MltBndCurve2::ResolveU2Segment(IN const double& nU_, IN const bool& bLeft_, OUT std::shared_ptr<SglBndCurve2>& pSeg_, OUT double& nLocalU_) const
{
    double _nU = NormalizeParam(nU_);

    size_t _nSegIndex = 0;
    if (bLeft_)//!< 左侧，则查找以此u为终点的段
    {
        for (; _nSegIndex < m_lstParams.size(); ++_nSegIndex)//!< 左侧，则对比每一个参数的右侧
        {
            if (std::fabs(_nU - m_lstParams[_nSegIndex].second) <= EPSILON)
            {
                pSeg_ = m_lstSegments[_nSegIndex];
                nLocalU_ = pSeg_->EndParam();
                return;
            }
        }

        if (std::fabs(_nU - m_lstParams.front().first) <= EPSILON)//!< 处理位于整个PolylineEx起点的场景
        {
            if (IsClosed())//!< 闭合，则起点的左侧为最后一段
            {
                pSeg_ = m_lstSegments.back();
                nLocalU_ = pSeg_->EndParam();
            }
            else//!< 开口，只能返回第一段
            {
                pSeg_ = m_lstSegments.front();
                nLocalU_ = pSeg_->StartParam();
            }
            return;
        }
    }
    else//!< 右侧，则查找以此u为起点的段
    {
        for (; _nSegIndex < m_lstParams.size(); ++_nSegIndex)//!< 左侧，则对比每一个参数的左侧
        {
            if (std::fabs(_nU - m_lstParams[_nSegIndex].first) <= EPSILON)
            {
                pSeg_ = m_lstSegments[_nSegIndex];
                nLocalU_ = pSeg_->StartParam();
                return;
            }
        }

        if (std::fabs(_nU - m_lstParams.back().second) <= EPSILON)//!< 处理位于整个PolylineEx终点的场景
        {
            if (IsClosed())//!< 闭合，终点的右侧为第一段
            {
                pSeg_ = m_lstSegments.front();
                nLocalU_ = pSeg_->StartParam();
            }
            else//!< 开口，只能返回最后一段
            {
                pSeg_ = m_lstSegments.back();
                nLocalU_ = pSeg_->EndParam();
            }
            return;
        }
    }

    //!< 在段内
    if (std::fabs(_nU - m_lstParams[_nSegIndex].first) >= EPSILON && std::fabs(_nU - m_lstParams[_nSegIndex].second) >= EPSILON)
    {
        pSeg_ = m_lstSegments[_nSegIndex];
        nLocalU_ = pSeg_->StartParam() + _nU - m_lstParams[_nSegIndex].first;
        return;
    }
}

//!< 获取曲线起始参数
double iCAX::Geom::MltBndCurve2::StartParam() const
{
    return m_lstParams.empty() ? 0.0 : m_lstParams.front().first;
}

//!< 获取曲线终止参数
double iCAX::Geom::MltBndCurve2::EndParam() const
{
    return m_lstParams.empty() ? 0.0 : m_lstParams.back().second;
}

//!< 在给定参数处计算点坐标
Point2 iCAX::Geom::MltBndCurve2::Evaluate(IN const double& nU_) const
{
    std::shared_ptr<SglBndCurve2> _pSeg = nullptr;
    double _nLocalU = .0;
    ResolveU2Segment(nU_, true, _pSeg, _nLocalU);
    return _pSeg->Evaluate(_nLocalU);
}

//!< 反转
void iCAX::Geom::MltBndCurve2::Reverse()
{
    std::reverse(m_lstSegments.begin(), m_lstSegments.end());
    for (size_t i = 0; i < m_lstSegments.size(); i++)
    {
        m_lstSegments[i]->Reverse();
    }
    ReCalcParamTable();
}

//!< 重算参数表
void iCAX::Geom::MltBndCurve2::ReCalcParamTable()
{
    // 构建累加参数表
    m_lstParams.clear();
    double _nAcc = 0.0;
    for (auto& _pSeg : m_lstSegments)
    {
        double _nParamLen = _pSeg->EndParam() - _pSeg->StartParam(); //!< 段参数区间
        m_lstParams.push_back(std::make_pair(_nAcc, _nAcc + _nParamLen));
        _nAcc += _nParamLen;
    }
}
