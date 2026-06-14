#include "pch.h"
#include "CoordSys3.h"
#include "Tranform3.h"

//!< 构造函数
iCAX::Math::CoordSys3::CoordSys3()
    : m_Axis()//!< 0 0 1
    , m_dirY(0, 1, 0)
    , m_dirZ(0, 0, 1)
{
}

//!< 使用原点与两个方向向量构造坐标系
iCAX::Math::CoordSys3::CoordSys3(IN const Point3& ptLocation_, IN const Dir3 dirX_)
    : m_Axis(ptLocation_, dirX_)
{
    // 选择参考向量，避免与dirX_共线
    Vector3 _vRef(0, 0, 1);
    if (fabs(dirX_.Vector().Dot(_vRef)) > 0.717) // 几乎共线
        _vRef = Vector3(0, 1, 0);

    Vector3 _vZ = dirX_.Vector().Cross(_vRef).Normalized(); // 垂直于dirX_
    Vector3 _vY = _vZ.Cross(dirX_.Vector()).Normalized();   // 右手系，垂直X和Z

    m_dirY = Dir3(_vY);
    m_dirZ = Dir3(_vZ);
}

//!< 使用原点与两个方向向量构造坐标系
iCAX::Math::CoordSys3::CoordSys3(IN const Point3& ptLocation_, IN const Dir3 dirX_, IN const Dir3 dirY_)
    : CoordSys3(Axis3(ptLocation_, dirX_), dirY_)
{
}

//!< 使用 X 轴与 Y 方向构造坐标系
iCAX::Math::CoordSys3::CoordSys3(IN const Axis3& axX_, IN const Dir3& dirY_)
    : m_Axis(axX_)
    , m_dirY(dirY_)
{
    //!< 右手笛卡尔正交坐标系
    m_dirZ = Dir3(XDirection().Vector() ^ YDirection().Vector());
}

//!< 获取原点
const iCAX::Math::Point3& iCAX::Math::CoordSys3::Location() const
{
    return m_Axis.Location();
}
//!< 获取 X 方向
const iCAX::Math::Dir3& iCAX::Math::CoordSys3::XDirection() const
{
    return m_Axis.Direction();
}
//!< 获取 Y 方向
const iCAX::Math::Dir3& iCAX::Math::CoordSys3::YDirection() const
{
    return m_dirY;
}
//!< 获取 Z 方向
const iCAX::Math::Dir3& iCAX::Math::CoordSys3::ZDirection() const
{
    return m_dirZ;
}
//!< 获取坐标系的 X 轴
const iCAX::Math::Axis3& iCAX::Math::CoordSys3::Axis() const
{
    return m_Axis;
}
//!< 变换
iCAX::Math::CoordSys3 iCAX::Math::CoordSys3::Transformed(IN const Double3& vTranslate_, IN const Quaternion& nRotate_) const
{
    return CoordSys3(Axis().Transformed(vTranslate_, nRotate_), nRotate_.Applied(YDirection()));
}
//!< 变换
iCAX::Math::CoordSys3& iCAX::Math::CoordSys3::Transform(IN const Double3& vTranslate_, IN const Quaternion& nRotate_)
{
    m_Axis.Transform(vTranslate_, nRotate_);
    m_dirY = nRotate_.Applied(m_dirY);
    m_dirZ = nRotate_.Applied(m_dirZ);
    return *this;
}
//!< 获取当前坐标系到世界坐标系的变换
iCAX::Math::Tranform3 iCAX::Math::CoordSys3::LocalToWorld() const
{
    return Tranform3(
        XDirection().X(), YDirection().X(), ZDirection().X(), Location().X(),
        XDirection().Y(), YDirection().Y(), ZDirection().Y(), Location().Y(),
        XDirection().Z(), YDirection().Z(), ZDirection().Z(), Location().Z(),
        0,                  0,                   0,              1
    );
}
//!< 获取世界坐标系到当前坐标系的变换
iCAX::Math::Tranform3 iCAX::Math::CoordSys3::WorldToLocal() const
{
    return LocalToWorld().Inverse();
}

//!< 获取世界坐标系实例
const iCAX::Math::CoordSys3& iCAX::Math::CoordSys3::WorldCoordSys()
{
    static CoordSys3 s_CoordSys(Axis3(Point3::Zero(), Dir3::Right()), Dir3::Forward());
    return s_CoordSys;
}

//!< 判断是否相等
bool iCAX::Math::operator==(IN const CoordSys3& csLHS_, IN const CoordSys3& csRHS_)
{
    return csLHS_.Axis() == csRHS_.Axis() && csLHS_.YDirection() == csRHS_.YDirection();
}

//!< 小于比较（主要用于 map / set 等有序容器排序）
bool iCAX::Math::operator<(IN const CoordSys3& csLHS_, IN const CoordSys3& csRHS_)
{
    if (csLHS_.Axis() < csRHS_.Axis())
    {
        return true;
    }
    if (csRHS_.Axis() < csLHS_.Axis())
    {
        return false;
    }

    return csLHS_.YDirection() < csRHS_.YDirection();
}
