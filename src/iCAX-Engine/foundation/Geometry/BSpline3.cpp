#include "pch.h"
#include "BSpline3.h"
#include <stdexcept>
#include <cmath>
#include <vector>

//!< 构造函数
iCAX::Geom::BSpline3::BSpline3(IN const std::vector<Point3>& lstControlPoints_,
    IN const std::vector<double>& lstKnotVector_,
    IN const int& nDegree_)
    : SglBndCurve3(CoordSys3())
    , m_lstControlPoints(lstControlPoints_)
    , m_lstKnotVector(lstKnotVector_)
    , m_nDegree(nDegree_)
{
    int n = static_cast<int>(m_lstControlPoints.size());
    if (n < 2)
        throw std::invalid_argument("BSpline3: controlPoints must have at least 2 points.");
    if (m_lstKnotVector.size() != n + m_nDegree + 1)
        throw std::invalid_argument("BSpline3: knotVector length must be n + degree + 1.");
}

//!< 析构函数
iCAX::Geom::BSpline3::~BSpline3() {}

//!< 获取控制点列表
const std::vector<Point3>& iCAX::Geom::BSpline3::ControlPoints() const
{
    return m_lstControlPoints;
}
//!< 获取阶数
int iCAX::Geom::BSpline3::Degree() const
{
    return m_nDegree;
}
//!< 获取节点向量
const std::vector<double>& iCAX::Geom::BSpline3::KnotVector() const
{
    return m_lstKnotVector;
}
//!< 获取曲线起始参数
double iCAX::Geom::BSpline3::StartParam() const
{
    return m_lstKnotVector[m_nDegree];
}
//!< 获取曲线结束参数
double iCAX::Geom::BSpline3::EndParam() const
{
    return m_lstKnotVector[m_lstControlPoints.size() - m_nDegree - 1];
}

//!< Cox-de Boor 迭代实现
Point3 iCAX::Geom::BSpline3::Evaluate(IN const double& nU_) const
{
    Point3 _pt(0.0, 0.0, .0);
    for (int i = 0; i < m_lstControlPoints.size(); ++i)
    {
        double N = BasisFunc(i, m_nDegree, nU_);
        _pt += N * m_lstControlPoints[i].XYZ();
    }

    return m_csLocal.LocalToWorld().Applied(_pt);
}

//!< 反转
void iCAX::Geom::BSpline3::Reverse()
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

//!< 获取圆弧所在平面
std::optional<Plane3> iCAX::Geom::BSpline3::SuggestPlane() const
{
    return Plane3(m_lstControlPoints[0], m_lstControlPoints[1], m_lstControlPoints[2]);
}

//!< 是否在平面上
bool iCAX::Geom::BSpline3::IsOnPlane(IN const Plane3& pln_) const
{
    for (size_t i = 0; i < m_lstControlPoints.size(); i++)
    {
        if (!pln_.IsON(m_lstControlPoints[i]))
        {
            return false;
        }
    }

    return true;
}

//!< 缩放和剪切
void iCAX::Geom::BSpline3::ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_)
{
    Tranform3 _mat = Tranform3::Shear(nShear_) * Tranform3::Scale(nScale_) * (bMirrorX_ ? Tranform3::MirrorX() : Tranform3::Identity());
    for (Point3& _pt : m_lstControlPoints)
        _pt = _mat.Applied(_pt);
}

//!< 克隆
std::shared_ptr<iCAX::Geom::Geometry> iCAX::Geom::BSpline3::Clone() const
{
    return std::make_shared<BSpline3>(*this);
}

//!< 基函数
double iCAX::Geom::BSpline3::BasisFunc(IN const int& i, IN const int& p, IN const double& u) const
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
