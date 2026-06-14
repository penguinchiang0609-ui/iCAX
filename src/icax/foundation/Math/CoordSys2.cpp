#include "pch.h"
#include "CoordSys2.h"
#include "Tranform2.h"
#include "CoordSys3.h"

//!< 默认构造函数
iCAX::Math::CoordSys2::CoordSys2()
    : m_Axis()
    , m_dirY(0, 1)
{
}

//!< 构造函数：原点 + X方向
iCAX::Math::CoordSys2::CoordSys2(IN const Point2& ptLocation_, IN const Dir2& dirX_)
    : CoordSys2(Axis2(ptLocation_, dirX_))
{
}

//!< 构造函数：使用 X 轴
iCAX::Math::CoordSys2::CoordSys2(IN const Axis2& axX_)
    : m_Axis(axX_)
{
    // Y方向正交于 X方向
    m_dirY = Dir2(axX_.Direction().Vector().Perpendicular());
}

//!< 获取坐标系原点
const iCAX::Math::Point2& iCAX::Math::CoordSys2::Location() const
{
    return m_Axis.Location();
}

//!< 获取 X 方向单位向量
const iCAX::Math::Dir2& iCAX::Math::CoordSys2::XDirection() const
{
    return m_Axis.Direction();
}

//!< 获取 Y 方向单位向量
const iCAX::Math::Dir2& iCAX::Math::CoordSys2::YDirection() const
{
    return m_dirY;
}

//!< 获取坐标系 X 轴
const iCAX::Math::Axis2& iCAX::Math::CoordSys2::Axis() const
{
    return m_Axis;
}

//!< 变换
iCAX::Math::CoordSys2 iCAX::Math::CoordSys2::Transformed(IN const Double2& vTranslate_, IN const double& nRotate_) const
{
    return CoordSys2(Axis().Transformed(vTranslate_, nRotate_));
}

//!< 变换
iCAX::Math::CoordSys2& iCAX::Math::CoordSys2::Transform(IN const Double2& vTranslate_, IN const double& nRotate_)
{
    m_Axis.Transform(vTranslate_, nRotate_);
    // Y方向正交于 X方向
    m_dirY = Dir2(XDirection().Vector().Perpendicular());
    return *this;
}

//!< 获取本地到世界转换矩阵
iCAX::Math::Tranform2 iCAX::Math::CoordSys2::LocalToWorld() const
{
    return Tranform2(
        XDirection().X(), YDirection().X(), Location().X(),
        XDirection().Y(), YDirection().Y(), Location().Y(),
        0.0, 0.0, 1.0
    );
}

//!< 获取世界到本地转换矩阵
iCAX::Math::Tranform2 iCAX::Math::CoordSys2::WorldToLocal() const
{
    return LocalToWorld().Inverse();
}

//!< == 比较运算符
bool iCAX::Math::operator==(IN const CoordSys2& csLHS_, IN const CoordSys2& csRHS_)
{
    return csLHS_.Axis() == csRHS_.Axis();
}

//!< < 比较运算符
bool iCAX::Math::operator<(IN const CoordSys2& csLHS_, IN const CoordSys2& csRHS_)
{
    return csLHS_.Axis() < csRHS_.Axis();
}
