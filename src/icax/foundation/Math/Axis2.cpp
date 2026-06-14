#include "pch.h"
#include "Axis2.h"

//!< 默认构造函数
iCAX::Math::Axis2::Axis2()
    : m_ptLocation(0, 0)
    , m_dir(1, 0)
{
}

//!< 构造函数
iCAX::Math::Axis2::Axis2(IN const Point2& ptLocation_, IN const Dir2& vDir_)
    : m_ptLocation(ptLocation_)
    , m_dir(vDir_)
{
    if (vDir_.IsNil())
    {
        throw "direction is nil";
    }
}

//!< 获取原点（常量版本）
const iCAX::Math::Point2& iCAX::Math::Axis2::Location() const
{
    return m_ptLocation;
}

//!< 获取原点
iCAX::Math::Point2& iCAX::Math::Axis2::Location()
{
    return m_ptLocation;
}

//!< 获取方向（常量版本）
const iCAX::Math::Dir2& iCAX::Math::Axis2::Direction() const
{
    return m_dir;
}

//!< 获取方向
iCAX::Math::Dir2& iCAX::Math::Axis2::Direction()
{
    return m_dir;
}

//!< 将点投影到轴上
iCAX::Math::Point2 iCAX::Math::Axis2::Project(IN const Point2& ptTarget_) const
{
    Vector2 _vOffset = ptTarget_ - m_ptLocation;
    Vector2 _vDir = m_dir.Vector();
    double _nParam = _vOffset.Dot(_vDir);
    return m_ptLocation + _vDir * _nParam;
}

//!< 点是否在轴上
bool iCAX::Math::Axis2::IsOn(IN const Point2& ptTarget_, IN const double& nTol_) const
{
    Vector2 _vDiff = ptTarget_ - Project(ptTarget_);
    return _vDiff.Magnitude() <= nTol_;
}

//!< 点到轴距离
double iCAX::Math::Axis2::Distance(IN const Point2& ptTarget_) const
{
    Vector2 _vDiff = ptTarget_ - Project(ptTarget_);
    return _vDiff.Magnitude();
}

//!< 计算点到轴的平方距离
double iCAX::Math::Axis2::DistanceSquared(IN const Point2& ptTarget_) const
{
    Vector2 _vDiff = ptTarget_ - Project(ptTarget_);
    return _vDiff.MagnitudeSquared();
}

//!< 判断是否与另一条轴几何相等
bool iCAX::Math::Axis2::IsEqual(IN const Axis2& axOther_, IN const double& nTol_) const
{
    return Location().IsEqual(axOther_.Location(), nTol_) && Direction().IsEqual(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴平行
bool iCAX::Math::Axis2::IsParallel(IN const Axis2& axOther_, IN const double& nTol_) const
{
    return Direction().IsParallel(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴同向
bool iCAX::Math::Axis2::IsCodirectional(IN const Axis2& axOther_, IN const double& nTol_) const
{
    return Direction().IsCodirectional(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴反向
bool iCAX::Math::Axis2::IsOpposite(IN const Axis2& axOther_, IN const double& nTol_) const
{
    return Direction().IsOpposite(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴垂直
bool iCAX::Math::Axis2::IsPerpendicular(IN const Axis2& axOther_, IN const double& nTol_) const
{
    return Direction().IsPerpendicular(axOther_.Direction(), nTol_);
}

//!< 判断是否与另一条轴共线
bool iCAX::Math::Axis2::IsCoaxial(IN const Axis2& axOther_, IN const double& nTol_) const
{
    const Point2& _pt1 = Location();
    const Dir2& _dir1 = Direction();
    const Point2& _pt2 = axOther_.Location();
    const Dir2& _dir2 = axOther_.Direction();

    // 1. 方向需平行（或反向）
    double _nDot = _dir1.X() * _dir2.X() + _dir1.Y() * _dir2.Y();
    if (fabs(fabs(_nDot) - 1.0) > nTol_)
        return false;

    // 2. 检查两轴位置是否在同一直线上
    Vector2 _vTangent(_pt2.X() - _pt1.X(), _pt2.Y() - _pt1.Y());
    Vector2 _vNormal(-_dir1.Y(), _dir1.X()); // 垂直于 d1 的法向量

    double _nDist = fabs(_vTangent.Dot(_vNormal)); // 点到直线距离
    return _nDist <= nTol_;
}

//!< 生成取反方向的新轴
iCAX::Math::Axis2 iCAX::Math::Axis2::Reversed() const
{
    return Axis2(Location(), Direction().Reversed());
}

//!< 当前轴方向取反
iCAX::Math::Axis2& iCAX::Math::Axis2::Reverse()
{
    Direction().Reverse();
    return *this;
}

//!< 变换
iCAX::Math::Axis2 iCAX::Math::Axis2::Transformed(IN const Double2& vTranslate_, IN const double& nRotate_) const
{
    Tranform2 _mat = Tranform2::Translate(vTranslate_) * Tranform2::Rotate(nRotate_);
    return Axis2(_mat.Applied(Location()), _mat.Applied(Direction()));
}

//!< 变换
iCAX::Math::Axis2& iCAX::Math::Axis2::Transform(IN const Double2& vTranslate_, IN const double& nRotate_)
{
    Tranform2 _mat = Tranform2::Translate(vTranslate_) * Tranform2::Rotate(nRotate_);
    Location() = _mat.Applied(Location());
    Direction() = _mat.Applied(Direction());
    return *this;
}

//!< ==
bool iCAX::Math::operator==(IN const Axis2& axLHS_, IN const Axis2& axRHS_)
{
    return axLHS_.Location().IsEqual(axRHS_.Location()) &&
        axLHS_.Direction().IsEqual(axRHS_.Direction());
}

//!< <
bool iCAX::Math::operator<(IN const Axis2& axLHS_, IN const Axis2& axRHS_)
{
    if (axLHS_.Location() < axRHS_.Location())
        return true;
    if (axRHS_.Location() < axLHS_.Location())
        return false;
    return axLHS_.Direction() < axRHS_.Direction();
}
