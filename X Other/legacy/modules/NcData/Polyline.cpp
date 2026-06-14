#include "pch.h"
#include "Polyline.h"

//!< 构造函数
iCAX::NcData::ncPolyline::Node::Node()
    : nSegType(kNcLineSeg)
    , Line()
    , nFeedrate()
{
}

//!< 构造函数
iCAX::NcData::ncPolyline::Node::Node(IN const Point3& ptEnd_, IN const double& nFeedrate_)
    : nSegType(kNcLineSeg)
    , nFeedrate(nFeedrate_)
{
    Line = { ptEnd_ };
}

//!< 构造函数
iCAX::NcData::ncPolyline::Node::Node(IN const Point3& ptEnd_, IN const Point3& ptCenter_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_)
    : nSegType(kNcArcSeg)
    , nFeedrate(nFeedrate_)
{
    Arc = { ptEnd_, ptCenter_, vNormal_, bCW_ };
}

//!< 构造函数
iCAX::NcData::ncPolyline::Node::Node(IN const Point3& ptEnd_, IN const Point3& ptLeftFocus_, IN const Point3& ptRightFocus_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_)
    : nSegType(kNcArcSeg)
    , nFeedrate(nFeedrate_)
{
    EArc = { ptEnd_, ptLeftFocus_, ptRightFocus_, vNormal_, bCW_ };
}

//!< 构造函数
iCAX::NcData::ncPolyline::Node::Node(IN const Point3& ptEnd_, IN const Point3& ptCtrl0_, IN const Point3& ptCtrl1_, IN const double& nFeedrate_)
    : nSegType(kNcBezierSeg)
    , nFeedrate(nFeedrate_)
{
    Bezier = { ptEnd_, ptCtrl0_, ptCtrl1_};
}

iCAX::NcData::ncPolyline::Node::Node(IN const Node& Right_)
    : nSegType(Right_.nSegType)
    , Line()
    , nFeedrate(Right_.nFeedrate)
{
    switch (nSegType)
    {
    case iCAX::NcData::ncPolyline::kNcLineSeg:
        Line = Right_.Line;
        break;
    case iCAX::NcData::ncPolyline::kNcArcSeg:
        Arc = Right_.Arc;
        break;
    case iCAX::NcData::ncPolyline::kNcEllipseArcSeg:
        EArc = Right_.EArc;
        break;
    case iCAX::NcData::ncPolyline::kNcBezierSeg:
        Bezier = Right_.Bezier;
        break;
    default:
        break;
    }
}

iCAX::NcData::ncPolyline::Node& iCAX::NcData::ncPolyline::Node::operator=(IN const Node& Right_)
{
    switch (nSegType)
    {
    case iCAX::NcData::ncPolyline::kNcLineSeg:
        Line = {};
        break;
    case iCAX::NcData::ncPolyline::kNcArcSeg:
        Arc = {};
        break;
    case iCAX::NcData::ncPolyline::kNcEllipseArcSeg:
        EArc = {};
        break;
    case iCAX::NcData::ncPolyline::kNcBezierSeg:
        Bezier = {};
        break;
    default:
        break;
    }

    nSegType = Right_.nSegType;

    switch (nSegType)
    {
    case iCAX::NcData::ncPolyline::kNcLineSeg:
        Line = Right_.Line;
        break;
    case iCAX::NcData::ncPolyline::kNcArcSeg:
        Arc = Right_.Arc;
        break;
    case iCAX::NcData::ncPolyline::kNcEllipseArcSeg:
        EArc = Right_.EArc;
        break;
    case iCAX::NcData::ncPolyline::kNcBezierSeg:
        Bezier = Right_.Bezier;
        break;
    default:
        break;
    }

    return *this;
}

iCAX::NcData::ncPolyline::Node::~Node()
{
    switch (nSegType)
    {
    case iCAX::NcData::ncPolyline::kNcLineSeg:
        Line = {};
        break;
    case iCAX::NcData::ncPolyline::kNcArcSeg:
        Arc = {};
        break;
    case iCAX::NcData::ncPolyline::kNcEllipseArcSeg:
        EArc = {};
        break;
    case iCAX::NcData::ncPolyline::kNcBezierSeg:
        Bezier = {};
        break;
    default:
        break;
    }
}

//!< 构造函数
iCAX::NcData::ncPolyline::ncPolyline()
    : m_ptStart(0, 0, 0)
    , m_vecNodes()
{
}


iCAX::NcData::ncPolyline::~ncPolyline()
{

}

//!< 设置起点
void iCAX::NcData::ncPolyline::SetStart(IN const Point3& pt_)
{
    m_ptStart = pt_;
}

//!< 获取起点
iCAX::Math::Point3 iCAX::NcData::ncPolyline::GetStart() const
{
    return m_ptStart;
}

//!< 获取节点列表
std::vector<iCAX::NcData::ncPolyline::Node>& iCAX::NcData::ncPolyline::GetNodes()
{
    return m_vecNodes;
}

//!< 获取节点列表
const std::vector<iCAX::NcData::ncPolyline::Node>& iCAX::NcData::ncPolyline::GetNodes() const
{
    return m_vecNodes;
}

//!< 追加直线段
void iCAX::NcData::ncPolyline::PushLine(IN const Point3& ptEnd_, IN const double& nFeedrate_)
{
    m_vecNodes.push_back(Node(ptEnd_, nFeedrate_));
}
//!< 追加圆弧段
void iCAX::NcData::ncPolyline::PushArc(IN const Point3& ptEnd_, IN const Point3& ptCenter_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_)
{
    m_vecNodes.push_back(Node(ptEnd_, ptCenter_, vNormal_, bCW_, nFeedrate_));
}

//!< 追加椭圆弧段
void iCAX::NcData::ncPolyline::PushEllipseArc(IN const Point3& ptEnd_, IN const Point3& ptLeftFocus_, IN const Point3& ptRightFocus_, IN const Vector3& vNormal_, IN const bool& bCW_, IN const double& nFeedrate_)
{
    m_vecNodes.push_back(Node(ptEnd_, ptLeftFocus_, ptRightFocus_, vNormal_, bCW_, nFeedrate_));
}

//!< 追加贝塞尔段
void iCAX::NcData::ncPolyline::PushBezier(IN const Point3& ptEnd_, IN const Point3& ptCtrl0_, IN const Point3& ptCtrl1_, IN const double& nFeedrate_)
{
    m_vecNodes.push_back(Node(ptEnd_, ptCtrl0_, ptCtrl1_, nFeedrate_));
}



