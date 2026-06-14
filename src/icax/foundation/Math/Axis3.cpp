#include "pch.h"
#include "Axis3.h"
#include "Tranform3.h"

//!< 构造函数
iCAX::Math::Axis3::Axis3()
    : m_ptLocation(0, 0, 0)
    , m_dir(1, 0, 0)
{
}

//!< 构造函数
iCAX::Math::Axis3::Axis3(IN const Point3& pt_, IN const Dir3& v_)
    : m_ptLocation(pt_)
    , m_dir(v_)
{
}

//!< 获取原点
const iCAX::Math::Point3& iCAX::Math::Axis3::Location() const
{
    return m_ptLocation;
}

//!< 设置原点
iCAX::Math::Point3& iCAX::Math::Axis3::Location()
{
    return m_ptLocation;
}

//!< 获取方向
const iCAX::Math::Dir3& iCAX::Math::Axis3::Direction() const
{
    return m_dir;
}

//!< 设置方向
iCAX::Math::Dir3& iCAX::Math::Axis3::Direction()
{
    return m_dir;
}

iCAX::Math::Point3 iCAX::Math::Axis3::Project(IN const Point3& ptTarget_) const
{
    Vector3 w = ptTarget_ - m_ptLocation;
    Vector3 dir = m_dir.Vector();
    double t = w.Dot(dir);
    return m_ptLocation + dir * t;
}

//!< 点是否在轴上
bool iCAX::Math::Axis3::IsOn(IN const Point3& ptTarget_, IN const double& tol) const
{
    return Distance(ptTarget_) <= tol;
}

//!< 点到轴距离
double iCAX::Math::Axis3::Distance(IN const Point3& ptTarget_) const
{
    return std::sqrt(DistanceSquared(ptTarget_));
}

double iCAX::Math::Axis3::DistanceSquared(IN const Point3& ptTarget_) const
{
    Vector3 w = ptTarget_ - m_ptLocation;
    Vector3 dir = m_dir.Vector();
    Vector3 perp = w - dir * w.Dot(dir);
    return perp.MagnitudeSquared();
}

//!< 判断是否与另一条轴几何相等
bool iCAX::Math::Axis3::IsEqual(IN const Axis3& axOther_, IN const double& nTol_) const
{
    return Location().IsEqual(axOther_.Location(), nTol_) && Direction().IsEqual(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴平行
bool iCAX::Math::Axis3::IsParallel(IN const Axis3& axOther_, IN const double& nTol_) const
{
    return Direction().IsParallel(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴同向
bool iCAX::Math::Axis3::IsCodirectional(IN const Axis3& axOther_, IN const double& nTol_) const
{
    return Direction().IsCodirectional(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴反向
bool iCAX::Math::Axis3::IsOpposite(IN const Axis3& axOther_, IN const double& nTol_) const
{
    return Direction().IsOpposite(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴垂直
bool iCAX::Math::Axis3::IsPerpendicular(IN const Axis3& axOther_, IN const double& nTol_) const
{
    return Direction().IsPerpendicular(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴共线
bool iCAX::Math::Axis3::IsCoaxial(IN const Axis3& axOther_, IN const double& nTol_) const
{
    const Point3& _pt1 = Location();
    const Dir3& _dir1 = Direction();
    const Point3& _pt2 = axOther_.Location();
    const Dir3& _dir2 = axOther_.Direction();

    // 1. 方向必须平行或反向
    double _nDot = _dir1.Vector() * _dir2.Vector();
    if (fabs(fabs(_nDot) - 1.0) > nTol_)
        return false;

    // 2. 点 _pt2 是否在以 _pt1 为原点，方向为 D1 的直线上
    Vector3 _vTangent = _pt2 - _pt1;
    Vector3 _vNormal = _vTangent ^ _dir1.Vector();
    double _nDist = _vNormal.Magnitude(); // _dir1 已经单位化，除以 ||D1||=1
    return _nDist <= nTol_;
}

//!< 生成取反方向的新轴
iCAX::Math::Axis3 iCAX::Math::Axis3::Reversed() const
{
    return Axis3(Location(), Direction().Reversed());
}

//!< 当前轴方向取反
iCAX::Math::Axis3& iCAX::Math::Axis3::Reverse()
{
    Direction().Reverse();
    return *this;
}

//!< 变换
iCAX::Math::Axis3 iCAX::Math::Axis3::Transformed(IN const Double3& vTranslate_, IN const Quaternion& nRotate_) const
{
   Tranform3 _mat =  Tranform3::Translate(vTranslate_) * nRotate_.ToTrsf3();
    return Axis3(_mat.Applied(Location()), _mat.Applied(Direction()));
}

//!< 变换
iCAX::Math::Axis3& iCAX::Math::Axis3::Transform(IN const Double3& vTranslate_, IN const Quaternion& nRotate_)
{
    Tranform3 _mat = Tranform3::Translate(vTranslate_) * nRotate_.ToTrsf3();
    Location() = _mat.Applied(Location());
    Direction() = _mat.Applied(Direction());
    return *this;
}

//!< ==
bool iCAX::Math::operator==(IN const Axis3& axLHS_, IN const Axis3& axRHS_)
{
    return axLHS_.Location().IsEqual(axRHS_.Location()) &&
        axLHS_.Direction().IsCodirectional(axRHS_.Direction());

}
//!< <
bool iCAX::Math::operator<(IN const Axis3& axLHS_, IN const Axis3& axRHS_)
{
    if (axLHS_.Location() < axRHS_.Location()) return true;
    if (axLHS_.Location() < axRHS_.Location()) return false;
    return axLHS_.Direction() < axRHS_.Direction();
}
