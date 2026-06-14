#include "pch.h"
#include "Segment3.h"
#include <cmath>


//!< 默认构造函数，起点和终点初始化为原点
iCAX::Physics::cSegment3::cSegment3()
    : m_ptStart(0.0, 0.0, 0.0)
    , m_ptEnd(0.0, 0.0, 0.0)
{
}

//!< 构造函数，指定起点和终点
iCAX::Physics::cSegment3::cSegment3(IN const Point3& start, IN  const Point3& end)
    : m_ptStart(start)
    , m_ptEnd(end)
{
}

//!< 起点访问
Point3& iCAX::Physics::cSegment3::Start()
{
    return m_ptStart;
}

//!< 起点
const Point3& iCAX::Physics::cSegment3::Start() const
{
    return m_ptStart;
}

//!< 终点访问
Point3& iCAX::Physics::cSegment3::End()
{
    return m_ptEnd;
}

//!< 终点
const Point3& iCAX::Physics::cSegment3::End() const
{
    return m_ptEnd;
}

//!< 计算方向向量（单位向量）
Dir3 iCAX::Physics::cSegment3::Direction() const
{
    return Dir3(m_ptEnd - m_ptStart);
}

//!< 计算线段长度
double iCAX::Physics::cSegment3::Length() const
{
    return (m_ptEnd - m_ptStart).Magnitude();
}
