#include "pch.h"
#include "Box2.h"

//!< 构造函数
iCAX::Physics::cBox2::cBox2()
    : m_vHalfExtents()
{
}

//!< 析构函数
iCAX::Physics::cBox2::~cBox2()
{
}

//!< 半偏移
const Vector2& iCAX::Physics::cBox2::HalfExtents() const
{
    return m_vHalfExtents;
}

//!< 半偏移
Vector2& iCAX::Physics::cBox2::HalfExtents()
{
    return m_vHalfExtents;
}
