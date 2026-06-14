#include "pch.h"
#include "Collision2.h"
#include "Collider.h"


//!< 构造函数
iCAX::Physics::cContactPoint2::cContactPoint2(IN const Point2& ptPos_, IN const Vector2& vNormal_, IN const double& nPenetration_)
    : m_ptPos(ptPos_)
    , m_vNormal(vNormal_)
    , m_nPenetration(nPenetration_)
{
}

//!< 析构函数
iCAX::Physics::cContactPoint2::~cContactPoint2()
{
}

//!< 接触点位置（只读）
const Point2& iCAX::Physics::cContactPoint2::Position() const
{
    return m_ptPos;
}

//!< 接触法线（只读）
const Vector2& iCAX::Physics::cContactPoint2::Normal() const
{
    return m_vNormal;
}

//!< 穿透深度（只读）
double iCAX::Physics::cContactPoint2::Penetration() const
{
    return m_nPenetration;
}

//!< 构造函数
iCAX::Physics::cCollisionResult2::cCollisionResult2(IN const uuid& EntityA_, IN const uuid& EntityB_, IN const bool bTrigger_)
    : m_EntityA(EntityA_)
    , m_EntityB(EntityB_)
    , m_bTrigger(bTrigger_)
{
}

//!< 析构函数
iCAX::Physics::cCollisionResult2::~cCollisionResult2()
{
}

//!< 实体 MajorRadius ID（只读）
const uuid& iCAX::Physics::cCollisionResult2::EntityA() const
{
    return m_EntityA;
}

//!< 实体 MinorRadius ID（只读）
const uuid& iCAX::Physics::cCollisionResult2::EntityB() const
{
    return m_EntityB;
}

//!< 是否触发器（只读）
const bool& iCAX::Physics::cCollisionResult2::IsTrigger() const
{
    return m_bTrigger;
}

//!< 接触点列表（可修改）
std::vector<iCAX::Physics::cContactPoint2>& iCAX::Physics::cCollisionResult2::Contacts()
{
    return m_vecContacts;
}

//!< 接触点列表（只读）
const std::vector<iCAX::Physics::cContactPoint2>& iCAX::Physics::cCollisionResult2::Contacts() const
{
    return m_vecContacts;
}

//!< 增加接触点
void iCAX::Physics::cCollisionResult2::Push(IN const cContactPoint2& Contact_)
{
    m_vecContacts.push_back(Contact_);
}