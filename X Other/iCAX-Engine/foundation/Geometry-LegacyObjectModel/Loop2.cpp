#include "pch.h"
#include "Loop2.h"
#include <algorithm>
#include "../Data/CommonFunction.h"

//!< 构造函数
iCAX::Geom::Loop2::Loop2()
    : Geometry2(CoordSys2())
    , m_lstSegments()
    , m_lstParams()
{
}

//!< 析构函数
iCAX::Geom::Loop2::~Loop2()
{
}

//!< 段列表
const std::vector<std::shared_ptr<iCAX::Geom::BndCurve2>>& iCAX::Geom::Loop2::Segments() const
{
    return m_lstSegments;
}

//!< 顶点列表
const std::vector<Point2> iCAX::Geom::Loop2::GetVertices() const
{
    std::vector<Point2> _lstVerts;
    for (auto& seg : m_lstSegments)
        _lstVerts.push_back(seg->Evaluate(seg->StartParam()));

    if (!_lstVerts.empty())
        _lstVerts.push_back(_lstVerts.front());

    return _lstVerts;
}

//!< 解析u得到段以及段内参数
void iCAX::Geom::Loop2::ResolveU2Segment(IN const double& nU_, IN const bool& bLeft_, OUT std::shared_ptr<BndCurve2>& pSeg_, OUT double& nLocalU_) const
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

//!< 追加段
void iCAX::Geom::Loop2::PushBack(IN const std::shared_ptr<BndCurve2>& pSeg_)
{
    if (!EndPoint().IsEqual(pSeg_->StartPoint()))
    {
        throw std::runtime_error("iCAX::Geom::Loop2::PushBack end point not equal pSeg_->StartPoint");
    }
    if (IsClosed())
    {
        throw std::runtime_error("iCAX::Geom::Loop2::PushBack, has been closed");
    }
    m_lstSegments.push_back(pSeg_);
    //!< 此处性能考虑，不再重算，直接尾部追加
    double _nAcc = EndParam();
    double _nParamLen = pSeg_->EndParam() - pSeg_->StartParam(); //!< 段参数区间
    m_lstParams.push_back(std::make_pair(_nAcc, _nAcc + _nParamLen));
    //ReCalcParamTable();
}

//!< 获取曲线起始参数
double iCAX::Geom::Loop2::StartParam() const
{
    return m_lstParams.empty() ? 0.0 : m_lstParams.front().first;
}

//!< 获取曲线终止参数
double iCAX::Geom::Loop2::EndParam() const
{
    return m_lstParams.empty() ? 0.0 : m_lstParams.back().second;
}

//!< 起始点
Point2 iCAX::Geom::Loop2::StartPoint() const
{
    return Evaluate(StartParam());
}

//!< 终止点
Point2 iCAX::Geom::Loop2::EndPoint() const
{
    return Evaluate(EndParam());
}

//!< 是否闭合
bool iCAX::Geom::Loop2::IsClosed(IN const double& nTol_) const
{
    return StartPoint().Distance(EndPoint()) < nTol_;
}

//!< 获取曲线朝向类型
iCAX::Geom::LoopType iCAX::Geom::Loop2::ContourType() const
{
    // 不是闭合的返回未知
    if (!IsClosed())
        return kLoopUnknow;

    const int _nSamples = 100; // 可调节精度
    std::vector<Point2> _lstPts;
    _lstPts.reserve(_nSamples + 1);

    double _nU0 = StartParam();
    double _nU1 = EndParam();
    for (int i = 0; i <= _nSamples; ++i)
    {
        double _nU = _nU0 + (_nU1 - _nU0) * i / _nSamples;
        _lstPts.push_back(Evaluate(_nU));
    }

    double _nArea = 0.0;
    for (int i = 0; i < _nSamples; ++i)
    {
        const auto& _pt0 = _lstPts[i];
        const auto& _pt1 = _lstPts[i + 1];
        _nArea += (_pt0.X() * _pt1.Y() - _pt1.X() * _pt0.Y());
    }

    return (_nArea > 0) ? kLoopOutter : kLoopInner;
}

//!< 在给定参数处计算点坐标
Point2 iCAX::Geom::Loop2::Evaluate(IN const double& nU_) const
{
    std::shared_ptr<BndCurve2> _pSeg = nullptr;
    double _nLocalU = .0;
    ResolveU2Segment(nU_, true, _pSeg, _nLocalU);
    return _pSeg->Evaluate(_nLocalU);
}

//!< 反转
void iCAX::Geom::Loop2::Reverse()
{
    std::reverse(m_lstSegments.begin(), m_lstSegments.end());
    for (size_t i = 0; i < m_lstSegments.size(); i++)
    {
        m_lstSegments[i]->Reverse();
    }
    ReCalcParamTable();
}

//!< 重算参数表
void iCAX::Geom::Loop2::ReCalcParamTable()
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


//!< 参数归一化
double iCAX::Geom::Loop2::NormalizeParam(IN const double& nU_) const
{
    double _nStart = StartParam();
    double _nEnd = EndParam();
    if (IsClosed())
    {
        return wrap(nU_, _nStart, _nEnd);
    }
    else
    {
        return std::clamp(nU_, _nStart, _nEnd);
    }
}

