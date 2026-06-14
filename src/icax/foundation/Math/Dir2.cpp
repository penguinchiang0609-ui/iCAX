#include "pch.h"
#include "Dir2.h"


//!< 默认构造函数
//!< @details 构造非法方向向量 (Nil)，默认指向 X 正方向
iCAX::Math::Dir2::Dir2()
    : m_vector(1, 0)
{
}

//!< 由向量构造方向向量（会归一化）
//!< @param v_ 输入向量
iCAX::Math::Dir2::Dir2(IN const Vector2& v_)
    : m_vector(v_)
{
    m_vector.Normalize();
}

//!< 由坐标对构造方向向量（会归一化）
//!< @param XY_ 输入坐标对
iCAX::Math::Dir2::Dir2(IN const Double2& XY_)
    : m_vector(XY_)
{
    if (!IsNil())
    {
        m_vector.Normalize();
    }
    else
    {
        m_vector = Vector2::Zero();
    }
}

//!< 由坐标值构造方向向量（会归一化）
//!< @param nX_ X 分量
//!< @param nY_ Y 分量
iCAX::Math::Dir2::Dir2(IN const double& nX_, IN const double& nY_)
    : Dir2(Vector2(nX_, nY_))
{
}

//!< 析构函数
iCAX::Math::Dir2::~Dir2()
{
}

//!< 获取 X 分量
const double& iCAX::Math::Dir2::X() const
{
    return m_vector.X();
}

//!< 获取 Y 分量
const double& iCAX::Math::Dir2::Y() const
{
    return m_vector.Y();
}

//!< 获取坐标对
const Double2& iCAX::Math::Dir2::XY() const
{
    return m_vector.XY();
}

//!< 获取内部向量（单位向量）
//!< @return Vector2 内部存储单位向量
const iCAX::Math::Vector2& iCAX::Math::Dir2::Vector() const
{
    return m_vector;
}

//!< 计算与另一方向向量夹角（逆时针）
//!< @param dir_ 另一方向向量
//!< @return double 弧度值
double iCAX::Math::Dir2::Angle(IN const Dir2& dir_) const
{
    return m_vector.Angle(dir_.m_vector);
}

//!< 获取垂直方向向量（逆时针旋转90°）
//!< @return Dir2 垂直方向向量
iCAX::Math::Dir2 iCAX::Math::Dir2::Perpendicular() const
{
    return Dir2(m_vector.Perpendicular());
}

//!< 判断方向向量是否相等
//!< @param dir_ 另一方向向量
//!< @param nTol_ 公差
bool iCAX::Math::Dir2::IsEqual(IN const Dir2& dir_, IN const double& nTol_) const
{
    return abs(Angle(dir_)) <= nTol_;
}

//!< 判断方向向量是否零向量
//!< @param nTol_ 公差
bool iCAX::Math::Dir2::IsNil(IN const double& nTol_) const
{
    return m_vector.IsZero(nTol_);
}

//!< 判断是否与另一方向平行
//!< @param dir_ 另一方向向量
//!< @param tol 公差
bool iCAX::Math::Dir2::IsParallel(IN const Dir2& dir_, IN const double& tol) const
{
    return m_vector.IsParallel(dir_.m_vector, tol);
}

//!< 判断是否与另一方向同向
//!< @param dir_ 另一方向向量
//!< @param tol 公差
bool iCAX::Math::Dir2::IsCodirectional(IN const Dir2& dir_, IN const double& tol) const
{
    return m_vector.IsCodirectional(dir_.m_vector, tol);
}

//!< 判断是否与另一方向反向
//!< @param dir_ 另一方向向量
//!< @param tol 公差
bool iCAX::Math::Dir2::IsOpposite(IN const Dir2& dir_, IN const double& tol) const
{
    return m_vector.IsOpposite(dir_.m_vector, tol);
}

//!< 判断是否与另一方向垂直
//!< @param dir_ 另一方向向量
//!< @param tol 公差
bool iCAX::Math::Dir2::IsPerpendicular(IN const Dir2& dir_, IN const double& tol) const
{
    return m_vector.IsPerpendicular(dir_.m_vector, tol);
}

//!< 生成取反方向的新方向
iCAX::Math::Dir2 iCAX::Math::Dir2::Reversed() const
{
    return Dir2(-Vector());
}

//!< 当前方向取反
iCAX::Math::Dir2& iCAX::Math::Dir2::Reverse()
{
    m_vector.Reverse();
    return *this;
}

//!< 获取零方向向量
const iCAX::Math::Dir2& iCAX::Math::Dir2::Nil()
{
    static Dir2 s_dirNil(Vector2::Zero());
    return s_dirNil;
}

//!< 获取左方向 (-1,0)
const iCAX::Math::Dir2& iCAX::Math::Dir2::Left()
{
    static Dir2 s_dirLeft(Vector2::Left());
    return s_dirLeft;
}

//!< 获取右方向 (1,0)
const iCAX::Math::Dir2& iCAX::Math::Dir2::Right()
{
    static Dir2 s_dirRight(Vector2::Right());
    return s_dirRight;
}

//!< 获取上方向 (0,1)
const iCAX::Math::Dir2& iCAX::Math::Dir2::Up()
{
    static Dir2 s_dirUp(Vector2::Up());
    return s_dirUp;
}

//!< 获取下方向 (0,-1)
const iCAX::Math::Dir2& iCAX::Math::Dir2::Down()
{
    static Dir2 s_dirDown(Vector2::Down());
    return s_dirDown;
}

//!< 获取 (1,1) 方向（已归一化）
const iCAX::Math::Dir2& iCAX::Math::Dir2::One()
{
    static Dir2 s_dirOne(Vector2::One());
    return s_dirOne;
}

//!< 判断两个方向向量是否相等
bool iCAX::Math::operator==(IN const Dir2& dirLHS_, IN const Dir2& dirRHS_)
{
    return dirLHS_.Vector() == dirRHS_.Vector();
}

//!< 判断两个方向向量的排序（可用于 std::set 等）
bool iCAX::Math::operator<(IN const Dir2& dirLHS_, IN const Dir2& dirRHS_)
{
    return dirLHS_.Vector() < dirRHS_.Vector();
}

