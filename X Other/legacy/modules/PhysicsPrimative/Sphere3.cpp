#include "pch.h"
#include "Sphere3.h"


//!< 默认构造函数
iCAX::Physics::cSphere3::cSphere3()
    : m_nRadius(0.0) // 默认半径为 0
{
}

//!< 析构函数
iCAX::Physics::cSphere3::~cSphere3()
{
}

//!< 半径（可修改）
double& iCAX::Physics::cSphere3::Radius()
{
    return m_nRadius;
}

//!< 半径（只读）
const double& iCAX::Physics::cSphere3::Radius() const
{
    return m_nRadius;
}
