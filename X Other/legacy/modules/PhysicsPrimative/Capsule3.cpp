#include "pch.h"
#include "Capsule3.h"


//!< 默认构造函数
iCAX::Physics::cCapsule3::cCapsule3()
    : m_nRadius(0.0)
    , m_nHeight(0.0)
{
}

//!< 析构函数
iCAX::Physics::cCapsule3::~cCapsule3()
{
}

//!< 获取或设置胶囊体半径（可修改）
double& iCAX::Physics::cCapsule3::Radius()
{
    return m_nRadius;
}

//!< 获取胶囊体半径（只读）
const double& iCAX::Physics::cCapsule3::Radius() const
{
    return m_nRadius;
}

//!< 获取或设置胶囊体高度（可修改）
double& iCAX::Physics::cCapsule3::Height()
{
    return m_nHeight;
}

//!< 获取胶囊体高度（只读）
const double& iCAX::Physics::cCapsule3::Height() const
{
    return m_nHeight;
}
