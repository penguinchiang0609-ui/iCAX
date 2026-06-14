#include "pch.h"
#include "Collider.h"

//!< 构造函数
iCAX::Physics::cCollider::cCollider()
    : m_bTrigger(false)
{
}

//!< 析构函数
iCAX::Physics::cCollider::~cCollider()
{
}

//!< 是否触发
bool& iCAX::Physics::cCollider::Trigger()
{
    return m_bTrigger;
}

//!< 是否触发
const bool& iCAX::Physics::cCollider::Trigger() const
{
    return m_bTrigger;
}
