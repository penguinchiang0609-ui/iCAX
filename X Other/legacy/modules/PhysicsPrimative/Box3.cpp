#include "pch.h"
#include "Box3.h"

//!< 构造函数
iCAX::Physics::cBox3::cBox3()
    : m_vHalfExtents()
{
}

//!< 析构函数
iCAX::Physics::cBox3::~cBox3()
{
}

//!< 半偏移
const Vector3& iCAX::Physics::cBox3::HalfExtents() const
{
    return m_vHalfExtents;
}

//!< 半偏移
Vector3& iCAX::Physics::cBox3::HalfExtents()
{
    return m_vHalfExtents;
}
