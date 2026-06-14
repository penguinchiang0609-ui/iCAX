#include "pch.h"
#include "Cylinder3.h"

using namespace iCAX::Physics;

//!< 默认构造函数
cCylinder3::cCylinder3()
    : m_nRadius(0.0)
    , m_nHeight(0.0)
{
}

//!< 析构函数
cCylinder3::~cCylinder3()
{
}

//!< 获取或设置圆柱半径（可修改）
double& cCylinder3::Radius()
{
    return m_nRadius;
}

//!< 获取圆柱半径（只读）
const double& cCylinder3::Radius() const
{
    return m_nRadius;
}

//!< 获取或设置圆柱高度（可修改）
double& cCylinder3::Height()
{
    return m_nHeight;
}

//!< 获取圆柱高度（只读）
const double& cCylinder3::Height() const
{
    return m_nHeight;
}
