#include "pch.h"
#include "Capsule2.h"

using namespace iCAX::Physics;

//!< 默认构造函数
cCapsule2::cCapsule2()
    : m_nRadius(0.0)
    , m_nHeight(0.0)
{
}

//!< 析构函数
cCapsule2::~cCapsule2()
{
}

//!< 获取或设置胶囊体半径（可修改）
double& cCapsule2::Radius()
{
    return m_nRadius;
}

//!< 获取胶囊体半径（只读）
const double& cCapsule2::Radius() const
{
    return m_nRadius;
}

//!< 获取或设置胶囊体高度（可修改）
double& cCapsule2::Height()
{
    return m_nHeight;
}

//!< 获取胶囊体高度（只读）
const double& cCapsule2::Height() const
{
    return m_nHeight;
}
