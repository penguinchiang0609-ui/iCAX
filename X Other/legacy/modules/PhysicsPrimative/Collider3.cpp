#include "pch.h"
#include "Collider3.h"

//!< 构造函数
iCAX::Physics::cCollider3::cCollider3()
    : m_csLocal()
    , m_vScale(1, 1, 1)
{
}

//!< 析构函数
iCAX::Physics::cCollider3::~cCollider3()
{
}

//!< 局部坐标系
iCAX::Math::CoordSys3& iCAX::Physics::cCollider3::Local()
{
    return m_csLocal;
}

//!< 局部坐标系
const iCAX::Math::CoordSys3& iCAX::Physics::cCollider3::Local() const
{
    return m_csLocal;
}

//!< 缩放
iCAX::Math::Vector3& iCAX::Physics::cCollider3::Scale()
{
    return m_vScale;
}

//!< 缩放
const iCAX::Math::Vector3& iCAX::Physics::cCollider3::Scale() const
{
    return m_vScale;
}
