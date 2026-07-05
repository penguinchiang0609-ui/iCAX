#include "pch.h"
#include "Polyline3.h"
#include <algorithm>
#include "Segment3.h"

//!< 构造函数
iCAX::Geom::Polyline3::Polyline3()
    : MltBndCurve3(CoordSys3())
    , m_ptStart(Point3::Nil())
{
}

//!< 构造函数
iCAX::Geom::Polyline3::Polyline3(IN const std::vector<Point3>& Vertices_)
    : MltBndCurve3(CoordSys3())
    , m_ptStart(Vertices_.front())
{
    size_t n = Vertices_.size();
    if (n < 2)
        return; // 空或单点，不构造线段
    m_lstSegments.clear();
    for (size_t i = 0; i < n - 1; ++i)
        m_lstSegments.emplace_back(std::make_shared<Segment3>(Vertices_[i], Vertices_[i + 1]));

    ReCalcParamTable();
}

//!< 析构函数
iCAX::Geom::Polyline3::~Polyline3()
{
}

//!< 获取起点
Point3 iCAX::Geom::Polyline3::StartPoint() const
{
    return m_ptStart;
}

//!< 追加点
void iCAX::Geom::Polyline3::PushBack(IN const Point3& ptEnd_)
{
    if (m_ptStart.IsNil())
    {
        m_ptStart = m_csLocal.WorldToLocal().Applied(ptEnd_);
    }
    else
    {
        Point3 _ptStart = m_lstSegments.empty() ? m_ptStart : m_lstSegments.back()->EndPoint();
        auto _pSeg = std::make_shared<Segment3>(_ptStart, m_csLocal.WorldToLocal().Applied(ptEnd_));
        m_lstSegments.push_back(_pSeg);
        //!< 此处性能考虑，不再重算，直接尾部追加
        double _nAcc = EndParam();
        double _nParamLen = _pSeg->EndParam() - _pSeg->StartParam(); //!< 段参数区间
        m_lstParams.push_back(std::make_pair(_nAcc, _nAcc + _nParamLen));
    }

    //ReCalcParamTable(); //!< 此处性能考虑，不再重算，直接尾部追加
}

//!< 清空
void iCAX::Geom::Polyline3::Clear()
{
    m_ptStart = Point3::Nil();
    m_lstSegments.clear();
    m_lstParams.clear();
}

//!< 是否为空
bool iCAX::Geom::Polyline3::IsEmpty() const
{
    return m_lstSegments.empty();
}

//!< 获取顶点数量
int iCAX::Geom::Polyline3::VertexCount() const
{
    return m_ptStart.IsNil() ? 0 : (int)m_lstSegments.size() + 1;
}


//!< 方向发转
void iCAX::Geom::Polyline3::Reverse()
{
    MltBndCurve3::Reverse();
}

std::optional<Plane3> iCAX::Geom::Polyline3::SuggestPlane() const
{
    if (m_lstSegments.size() == 0)
    {
        return Plane3(m_csLocal);
    }
    else if (m_lstSegments.size() == 1)
    {
        return m_lstSegments.front()->SuggestPlane();
    }
    else if (m_lstSegments.size() >= 2)
    {
        return Plane3(m_lstSegments.front()->StartPoint(), m_lstSegments.front()->EndPoint(), m_lstSegments[1]->EndPoint());
    }

    return {};
}

//!< 缩放和剪切
void iCAX::Geom::Polyline3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    for (auto& seg : m_lstSegments)
        seg->ScaleAndShear(nScale_, nShear_, bMirrorX_);
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::Polyline3::Clone() const
{
    auto _pPolyline = std::make_shared<Polyline3>();
    _pPolyline->Local() = Local();
    for (auto& _seg : m_lstSegments)
        _pPolyline->m_lstSegments.push_back(std::dynamic_pointer_cast<SglBndCurve3>(_seg->Clone()));
    _pPolyline->m_lstParams = m_lstParams;
    _pPolyline->m_ptStart = m_ptStart;
    return _pPolyline;
}

