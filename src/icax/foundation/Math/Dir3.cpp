#include "pch.h"
#include "Dir3.h"


//!< 构造函数
iCAX::Math::Dir3::Dir3()
    : m_vector(1, 0, 0)
{
}
//!< 构造函数
iCAX::Math::Dir3::Dir3(IN const Vector3& vDir_)
    : m_vector(vDir_)
{
    m_vector.Normalize();
}
//!< 构造函数
iCAX::Math::Dir3::Dir3(IN const Double3& XYZ_)
    : m_vector(XYZ_)
{
    if (!IsNil())
    {
        m_vector.Normalize();
    }
    else
    {
        m_vector = Vector3::Zero();
    }
}
//!< 构造函数
iCAX::Math::Dir3::Dir3(IN const double& nX_, IN const double& nY_, IN const double& nZ_)
    : Dir3(Vector3(nX_, nY_, nZ_))
{
}
//!< 析构函数
iCAX::Math::Dir3::~Dir3()
{
}

//!< X
const double& iCAX::Math::Dir3::X() const
{
    return m_vector.X();
}

//!< Y
const double& iCAX::Math::Dir3::Y() const
{
    return m_vector.Y();
}

//!< Z
const double& iCAX::Math::Dir3::Z() const
{
    return m_vector.Z();
}
//!< XY
const Double3& iCAX::Math::Dir3::XYZ() const
{
    return m_vector.XYZ();
}
//!< Vector
const iCAX::Math::Vector3& iCAX::Math::Dir3::Vector() const
{
    return m_vector;
}
//!< 夹角，逆时针
double iCAX::Math::Dir3::Angle(IN const Dir3& dirRight_) const
{
    return m_vector.Angle(dirRight_.m_vector);
}
//!< 是否相等
bool iCAX::Math::Dir3::IsEqual(IN const Dir3& dirRight_, IN const double& nTol_) const
{
    return abs(Angle(dirRight_)) <= nTol_;
}
//!< 是否零长
bool iCAX::Math::Dir3::IsNil(IN const double& nTol_) const
{
    return m_vector.IsZero(nTol_);
}
//!< 是否平行
bool iCAX::Math::Dir3::IsParallel(IN const Dir3& dirOther_, IN const double& tol) const
{
    return Vector().IsParallel(dirOther_.Vector(), tol);
}
//!< 是否同向
bool iCAX::Math::Dir3::IsCodirectional(IN const Dir3& dirOther_, IN const double& tol) const
{
    return Vector().IsCodirectional(dirOther_.Vector(), tol);
}
//!< 是否反向
bool iCAX::Math::Dir3::IsOpposite(IN const Dir3& dirOther_, IN const double& tol) const
{
    return Vector().IsOpposite(dirOther_.Vector(), tol);
}
//!< 是否垂直
bool iCAX::Math::Dir3::IsPerpendicular(IN const Dir3& dirOther_, IN const double& tol) const
{
    return Vector().IsPerpendicular(dirOther_.Vector(), tol);
}
//!< 生成取反方向的新方向
iCAX::Math::Dir3 iCAX::Math::Dir3::Reversed() const
{
    return Dir3(-Vector());
}
//!< 当前方向取反
iCAX::Math::Dir3& iCAX::Math::Dir3::Reverse()
{
    m_vector.Reverse();
    return *this;
}
//!< 获取零方向
const iCAX::Math::Dir3& iCAX::Math::Dir3::Nil()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Zero());
    return s_dirLocal;
}
//!< 获取左方向 (-1, 0, 0)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Left()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Left());
    return s_dirLocal;
}
//!< 获取右方向 (1, 0, 0)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Right()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Right());
    return s_dirLocal;
}
//!< 获取上方向 (0, 0, 1)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Up()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Up());
    return s_dirLocal;
}
//!< 获取下方向 (0, 0, -1)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Down()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Down());
    return s_dirLocal;
}
//!< 获取前方向 (0, 1, 0)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Forward()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Forward());
    return s_dirLocal;
}
//!< 获取后方向 (0, -1, 0)
const iCAX::Math::Dir3& iCAX::Math::Dir3::Back()
{
    static Dir3 s_dirLocal = Dir3(Vector3::Back());
    return s_dirLocal;
}
//!< 获取 (1, 1, 1) 方向（已归一化）
const iCAX::Math::Dir3& iCAX::Math::Dir3::One()
{
    static Dir3 _Local = Dir3(Vector3::One());
    return _Local;
}

bool iCAX::Math::operator==(IN const Dir3& dirLHS_, IN const Dir3& dirRHS_)
{
    return dirLHS_.Vector() == dirRHS_.Vector();;
}

bool iCAX::Math::operator<(IN const Dir3& dirLHS_, IN const Dir3& dirRHS_)
{
    return dirLHS_.Vector() < dirRHS_.Vector();;
}
