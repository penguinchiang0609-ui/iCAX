#include "pch.h"
#include "BSpline2.h"
#include <stdexcept>
#include <cmath>
#include <vector>

//!< 构造函数
iCAX::Geom::BSpline2::BSpline2(IN const std::vector<Point2>& lstControlPoints_,
    IN const std::vector<double>& lstKnotVector_,
    IN const int& nDegree_)
    : SglBndCurve2(CoordSys2())
    , m_lstControlPoints(lstControlPoints_)
    , m_lstKnotVector(lstKnotVector_)
    , m_nDegree(nDegree_)
{
    int n = static_cast<int>(m_lstControlPoints.size());
    if (n < 2)
        throw std::invalid_argument("BSpline2: controlPoints must have at least 2 points.");
    if (m_lstKnotVector.size() != n + m_nDegree + 1)
        throw std::invalid_argument("BSpline2: knotVector length must be n + degree + 1.");
}

//!< 析构函数
iCAX::Geom::BSpline2::~BSpline2() {}

//!< 获取控制点列表
const std::vector<Point2>& iCAX::Geom::BSpline2::ControlPoints() const
{
    return m_lstControlPoints;
}
//!< 获取阶数
int iCAX::Geom::BSpline2::Degree() const
{
    return m_nDegree;
}
//!< 获取节点向量
const std::vector<double>& iCAX::Geom::BSpline2::KnotVector() const
{
    return m_lstKnotVector;
}
//!< 获取曲线起始参数
double iCAX::Geom::BSpline2::StartParam() const
{
    return m_lstKnotVector[m_nDegree];
}
//!< 获取曲线结束参数
double iCAX::Geom::BSpline2::EndParam() const
{
    return m_lstKnotVector[m_lstControlPoints.size() - m_nDegree - 1];
}

//!< Cox-de Boor 迭代实现
Point2 iCAX::Geom::BSpline2::Evaluate(IN const double& nU_) const
{
    Point2 _pt(0.0, 0.0);
    for (int i = 0; i < m_lstControlPoints.size(); ++i)
    {
        double N = BasisFunc(i, m_nDegree, nU_);
        _pt += N * m_lstControlPoints[i].XY();
    }

    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反转
void iCAX::Geom::BSpline2::Reverse()
{
    //!< 反转控制点顺序
    std::reverse(m_lstControlPoints.begin(), m_lstControlPoints.end());

    //!< 反转节点向量
    //    对称映射到 [u_min, u_max] 区间
    if (!m_lstKnotVector.empty())
    {
        double uMin = m_lstKnotVector.front();
        double uMax = m_lstKnotVector.back();

        for (auto& u : m_lstKnotVector)
        {
            u = uMin + uMax - u;
        }

        std::reverse(m_lstKnotVector.begin(), m_lstKnotVector.end());
    }
}

//!< 缩放和剪切
void iCAX::Geom::BSpline2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    for (Point2& _pt : m_lstControlPoints)
        _pt = _mat.Applied(_pt);
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::BSpline2::Clone() const
{
    return std::make_shared<BSpline2>(*this);
}

//!< 基函数
double iCAX::Geom::BSpline2::BasisFunc(IN const int& i, IN const int& p, IN const double& u) const
{
    //const auto& U = m_lstKnotVector;
    if (p == 0)
        return (u >= m_lstKnotVector[i] && u < m_lstKnotVector[i + 1]) ? 1.0 : 0.0;

    double left = (m_lstKnotVector[i + p] - m_lstKnotVector[i]) == 0 ? 0 :
        (u - m_lstKnotVector[i]) / (m_lstKnotVector[i + p] - m_lstKnotVector[i]) * BasisFunc(i, p - 1, u);
    double right = (m_lstKnotVector[i + p + 1] - m_lstKnotVector[i + 1]) == 0 ? 0 :
        (m_lstKnotVector[i + p + 1] - u) / (m_lstKnotVector[i + p + 1] - m_lstKnotVector[i + 1]) * BasisFunc(i + 1, p - 1, u);
    return left + right;
}
