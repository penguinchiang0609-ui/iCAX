#include "pch.h"
#include "Postrue.h"

iCAX::NcData::ncPostrue::ncPostrue()
    : m_vecNodes()
{
}

//!< 获取节点列表
std::vector<iCAX::NcData::ncPostrue::Node>& iCAX::NcData::ncPostrue::GetNodes()
{
    return m_vecNodes;
}

//!< 获取节点列表
const std::vector<iCAX::NcData::ncPostrue::Node>& iCAX::NcData::ncPostrue::GetNodes() const
{
    return m_vecNodes;
}

//!< 追加节点
void iCAX::NcData::ncPostrue::PushNode(IN const Vector3& vDir_, IN const double& nRatio_)
{
    m_vecNodes.push_back({ vDir_, nRatio_ });
}
