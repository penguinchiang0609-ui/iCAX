#include "pch.h"
#include "Plane3.h"

//!< 构造函数
iCAX::Math::Plane3::Plane3(IN const CoordSys3& csLocal_)
    : m_csLocal(csLocal_)
{
}

//!< 构造函数
iCAX::Math::Plane3::Plane3(IN const double& nDistance_, IN const Dir3 dirNormal_)
{
    // 选择参考向量，避免与dirX_共线
    Vector3 _vRef(1, 0, 0);
    if (fabs(dirNormal_.Vector().Dot(_vRef)) > 0.717) // 几乎共线
        _vRef = Vector3(0, 1, 0);

    Vector3 _vX = dirNormal_.Vector().Cross(_vRef).Normalized(); // 垂直于dirNormal_
    Vector3 _vY = dirNormal_.Vector().Cross(_vX).Normalized();   // 右手系，垂直X和Z

    m_csLocal = CoordSys3(Point3((dirNormal_.Vector() * nDistance_).XYZ()), Dir3(_vX), Dir3(_vY));
}

//!< 构造函数
iCAX::Math::Plane3::Plane3(IN const Point3& ptCenter_, IN const Dir3 dirNormal_)
{
    // 选择参考向量，避免与dirX_共线
    Vector3 _vRef(1, 0, 0);
    if (fabs(dirNormal_.Vector().Dot(_vRef)) > 0.717) // 几乎共线
        _vRef = Vector3(0, 1, 0);

    Vector3 _vX = dirNormal_.Vector().Cross(_vRef).Normalized(); // 垂直于dirNormal_
    Vector3 _vY = dirNormal_.Vector().Cross(_vX).Normalized();   // 右手系，垂直X和Z

    m_csLocal = CoordSys3(ptCenter_, Dir3(_vX), Dir3(_vY));
}

//!< 构造函数
iCAX::Math::Plane3::Plane3(IN const Point3& pt0_, IN const Point3& pt1_, IN const Point3& pt2_)
{
    Vector3 _vX = pt1_ - pt0_;
    Vector3 _zNormal = (pt1_ - pt0_).Cross(pt2_ - pt1_);
    m_csLocal = CoordSys3(pt0_, Dir3(_vX), Dir3(_zNormal.Cross(_vX)));
}

//!< 局部坐标系
iCAX::Math::CoordSys3& iCAX::Math::Plane3::Local()
{
    return m_csLocal;
}

//!< 局部坐标系
const iCAX::Math::CoordSys3& iCAX::Math::Plane3::Local() const
{
    return m_csLocal;
}

//!< 获取原点
iCAX::Math::Point3 iCAX::Math::Plane3::Location() const
{
    return m_csLocal.Location();;
}

//!< 接触法线（只读）
iCAX::Math::Dir3 iCAX::Math::Plane3::Normal() const
{
    return m_csLocal.ZDirection();
}

bool iCAX::Math::Plane3::IsON(IN const Point3& ptTarget_, IN const double& nTol_ ) const
{
    return Distance(ptTarget_) < nTol_;
}

//!< 是否平行
bool iCAX::Math::Plane3::IsParallel(IN const Dir3& dirOther_, IN const double& nTol_) const
{
    return m_csLocal.ZDirection().IsParallel(dirOther_, nTol_);
}

//!< 点到平面的距离
double iCAX::Math::Plane3::Distance(IN const Point3& ptTarget_) const
{
    return (ptTarget_ - m_csLocal.Location()).Dot(m_csLocal.ZDirection().Vector());
}

//!< 点投影到平面
iCAX::Math::Point3 iCAX::Math::Plane3::Project(IN const Point3& ptTarget_) const
{
    return ptTarget_ - Distance(ptTarget_) *  m_csLocal.ZDirection().Vector();
}

//!< 投影到平面内的二维坐标系
iCAX::Math::Point2 iCAX::Math::Plane3::Project2Plane(IN const Point3& ptTarget_) const
{
    Vector3 _vOffset = ptTarget_ - m_csLocal.Location();
    return Point2(_vOffset.Dot(m_csLocal.XDirection().Vector()), _vOffset.Dot(m_csLocal.YDirection().Vector()));
}

//!< 投影到三维空间
iCAX::Math::Point3 iCAX::Math::Plane3::Project2Space(IN const Point2& ptTarget_) const
{
    return m_csLocal.LocalToWorld().Applied(Point3(ptTarget_.X(), 0, ptTarget_.Y()));
}

//!< 是否等价
bool iCAX::Math::Plane3::IsEqual(IN const Plane3& pln_)
{
    return false;
}

