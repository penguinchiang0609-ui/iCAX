#include "pch.h"
#include "Circle2.h"

//!< 默认构造函数
iCAX::Physics::cCircle2::cCircle2()
    : m_nRadius(0.0)
{
}

//!< 析构函数
iCAX::Physics::cCircle2::~cCircle2()
{
}

//!< 获取或设置圆形半径（可修改）
double& iCAX::Physics::cCircle2::Radius()
{
    return m_nRadius;
}

//!< 获取圆形半径（只读）
const double& iCAX::Physics::cCircle2::Radius() const
{
    return m_nRadius;
}
