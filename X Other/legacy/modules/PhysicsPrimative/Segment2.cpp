#include "Segment2.h"
#include "pch.h"
#include "Segment2.h"
#include <cmath>

// 默认构造函数
iCAX::Physics::cSegment2::cSegment2()
    : m_ptStart(0.0, 0.0), m_ptEnd(0.0, 0.0)
{
}

// 构造函数，指定起点和终点
iCAX::Physics::cSegment2::cSegment2(IN const Point2& start, IN const Point2& end)
    : m_ptStart(start), m_ptEnd(end)
{
}

// 获取起点
Point2& iCAX::Physics::cSegment2::Start()
{
    return m_ptStart;
}

// 获取起点（只读）
const Point2& iCAX::Physics::cSegment2::Start() const
{
    return m_ptStart;
}

// 获取终点
Point2& iCAX::Physics::cSegment2::End()
{
    return m_ptEnd;
}

// 获取终点（只读）
const Point2& iCAX::Physics::cSegment2::End() const
{
    return m_ptEnd;
}

// 获取单位方向向量
Dir2 iCAX::Physics::cSegment2::Direction() const
{
    return Dir2(m_ptEnd - m_ptStart);
}

// 获取线段长度
double iCAX::Physics::cSegment2::Length() const
{
    return (m_ptEnd - m_ptStart).Magnitude();
}
