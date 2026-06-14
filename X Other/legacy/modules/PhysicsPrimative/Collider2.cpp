#include "pch.h"
#include "Collider2.h"

//!< 构造函数
iCAX::Physics::cCollider2::cCollider2()
    : m_csLocal()
    , m_vScale(1, 1)
{
}

//!< 析构函数
iCAX::Physics::cCollider2::~cCollider2()
{
}

//!< 局部坐标系
iCAX::Math::CoordSys2& iCAX::Physics::cCollider2::Local()
{
    return m_csLocal;
}

//!< 局部坐标系
const iCAX::Math::CoordSys2& iCAX::Physics::cCollider2::Local() const
{
    return m_csLocal;
}

//!< 缩放
iCAX::Math::Vector2& iCAX::Physics::cCollider2::Scale()
{
    return m_vScale;
}

//!< 缩放
const iCAX::Math::Vector2& iCAX::Physics::cCollider2::Scale() const
{
    return m_vScale;
}
