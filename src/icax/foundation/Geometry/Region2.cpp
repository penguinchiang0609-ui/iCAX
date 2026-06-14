#include "pch.h"
#include "Region2.h"

//!< 使用外轮廓构造区域
iCAX::Geom::Region2::Region2(IN const CoordSys2& csLocal_)
    : Geometry2(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::Region2::~Region2()
{
}

//!< 外轮廓
const std::shared_ptr<iCAX::Geom::Loop2>& iCAX::Geom::Region2::Outter() const
{
    return m_OutterLoop;
}

//!< 设置外轮廓
void iCAX::Geom::Region2::SetOutter(IN const std::shared_ptr<Loop2>& Outer_)
{
    m_OutterLoop = Outer_;
}

//!< 获取内孔
const std::vector<std::shared_ptr<iCAX::Geom::Loop2>>& iCAX::Geom::Region2::InnerLoops() const
{
    return m_InnerLoops;
}

//!< 增加内孔
void iCAX::Geom::Region2::AddInnerLoop(IN const std::shared_ptr<Loop2>& Inner_)
{
    if (Inner_ != nullptr)
    {
        m_InnerLoops.push_back(Inner_);
    }
}

void iCAX::Geom::Region2::RemoveInnerLoop(IN const std::shared_ptr<Loop2>& inner)
{
    m_InnerLoops.erase(
        std::remove(m_InnerLoops.begin(), m_InnerLoops.end(), inner),
        m_InnerLoops.end());
}

//!< 是否包含内孔
bool iCAX::Geom::Region2::HasHoles() const
{
    return !m_InnerLoops.empty();
}
