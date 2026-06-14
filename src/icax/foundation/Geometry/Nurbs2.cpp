#include "pch.h"
#include "NURBS2.h"
#include <stdexcept>
#include <cmath>
#include <numeric>
#include <memory>

using namespace iCAX::Geom;

//!< 构造函数
NURBS2::NURBS2(IN const std::vector<Point2>& lstControlPoints_,
    IN const std::vector<double>& lstWeights_,
    IN const std::vector<double>& lstKnotVector_,
    IN const int nDegree_)
    : SglBndCurve2(CoordSys2())
    , m_lstControlPoints(lstControlPoints_)
    , m_lstWeights(lstWeights_)
    , m_lstKnots(lstKnotVector_)
    , m_nDegree(nDegree_)
{
    if (m_lstControlPoints.size() != m_lstWeights.size())
        throw std::invalid_argument("Control points and weights size mismatch");
    if (m_lstKnots.size() != m_lstControlPoints.size() + m_nDegree + 1)
        throw std::invalid_argument("Knot vector size invalid");
}

//!< 析构函数
NURBS2::~NURBS2() {}

//!< 获取控制点
const std::vector<Point2>& NURBS2::ControlPoints() const
{
    return m_lstControlPoints;
}

//!< 获取权重
const std::vector<double>& NURBS2::Weights() const
{
    return m_lstWeights;
}

//!< 获取节点向量
const std::vector<double>& NURBS2::KnotVector() const
{
    return m_lstKnots;
}

//!< 获取曲线阶数
int NURBS2::Degree() const
{
    return m_nDegree;
}

//!< 起始参数
double NURBS2::StartParam() const
{
    return m_lstKnots[m_nDegree];
}

//!< 终止参数
double NURBS2::EndParam() const
{
    return m_lstKnots[m_lstKnots.size() - m_nDegree - 1];
}

//!< 计算 B 样条基函数 N_{i,p}(u)
static double CoxDeBoor(const std::vector<double>& knots, int i, int p, double u)
{
    if (p == 0)
    {
        if (u >= knots[i] && u < knots[i + 1])
            return 1.0;
        else
            return 0.0;
    }

    double left = 0.0, right = 0.0;
    double denom1 = knots[i + p] - knots[i];
    if (denom1 != 0.0)
        left = (u - knots[i]) / denom1 * CoxDeBoor(knots, i, p - 1, u);

    double denom2 = knots[i + p + 1] - knots[i + 1];
    if (denom2 != 0.0)
        right = (knots[i + p + 1] - u) / denom2 * CoxDeBoor(knots, i + 1, p - 1, u);

    return left + right;
}

//!< 计算曲线上指定参数点
Point2 NURBS2::Evaluate(IN const double& u_) const
{
    if (m_lstControlPoints.empty()) return Point2(0, 0);

    double numeratorX = 0.0;
    double numeratorY = 0.0;
    double denominator = 0.0;

    int n = static_cast<int>(m_lstControlPoints.size()) - 1;

    for (int i = 0; i <= n; i++)
    {
        double N = CoxDeBoor(m_lstKnots, i, m_nDegree, u_);
        double wN = N * m_lstWeights[i];

        numeratorX += wN * m_lstControlPoints[i].X();
        numeratorY += wN * m_lstControlPoints[i].Y();
        denominator += wN;
    }

    if (denominator == 0.0)
        throw std::runtime_error("NURBS evaluation denominator is zero");

    Point2 ptLocal(numeratorX / denominator, numeratorY / denominator);
    return m_csLocal.LocalToWorld().Applied(ptLocal);
}

//!< 反转曲线
void NURBS2::Reverse()
{
    std::reverse(m_lstControlPoints.begin(), m_lstControlPoints.end());
    std::reverse(m_lstWeights.begin(), m_lstWeights.end());

    // 节点向量翻转
    int m = static_cast<int>(m_lstKnots.size()) - 1;
    std::vector<double> newKnots(m + 1);
    double u0 = m_lstKnots.front();
    double um = m_lstKnots.back();
    for (int i = 0; i <= m; i++)
    {
        newKnots[i] = u0 + um - m_lstKnots[m - i];
    }
    m_lstKnots = newKnots;
}

//!< 缩放和剪切
void NURBS2::ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_)
{
    Tranform2 _mat = Tranform2::Shear(nShear_) * Tranform2::Scale(nScale_) * (bMirrorX_ ? Tranform2::MirrorX() : Tranform2::Identity());
    for (auto& pt : m_lstControlPoints)
        pt = _mat.Applied(pt);
}

//!< 克隆
std::shared_ptr<Geometry> NURBS2::Clone() const
{
    return std::make_shared<NURBS2>(*this);
}
