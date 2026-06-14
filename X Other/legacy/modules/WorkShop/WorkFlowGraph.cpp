#include "pch.h"
#include "WorkFlowGraph.h"
#include <atomic>

std::atomic<unsigned long long> g_nIndex = 0;

//!< 构造函数
iCAX::WorkShop::CWorkFlowGraph::CWorkFlowGraph(IN const iCAX::Data::uuid& UID_)
    : m_Graph()
    , m_ID(UID_)
{
}

//!< 析构函数
iCAX::WorkShop::CWorkFlowGraph::~CWorkFlowGraph()
{
}

//!< 获取名称
std::string& iCAX::WorkShop::CWorkFlowGraph::GetNameRef()
{
    return m_strName;
}

//!< 获取名称
const std::string& iCAX::WorkShop::CWorkFlowGraph::GetNameRef() const
{
    return m_strName;
}

//!< 获取备注
std::string& iCAX::WorkShop::CWorkFlowGraph::GetRemarkRef()
{
    return m_strRemark;
}

//!< 获取备注
const std::string& iCAX::WorkShop::CWorkFlowGraph::GetRemarkRef() const
{
    return m_strRemark;
}

//!< 获取类别
std::string& iCAX::WorkShop::CWorkFlowGraph::GetCatalogRef()
{
    return m_strCatalog;
}

//!< 获取类别
const std::string& iCAX::WorkShop::CWorkFlowGraph::GetCatalogRef() const
{
    return m_strCatalog;
}

//!< 获取索引
iCAX::Data::uuid iCAX::WorkShop::CWorkFlowGraph::GetID() const
{
    return m_ID;
}

//!< 追加工艺节点
void iCAX::WorkShop::CWorkFlowGraph::Append(IN const int& nNodeIndex_, IN const iCAX::Data::uuid& nTradeIndex_, IN const std::vector<int>& vecPreAdjacency_, const bool bIsComplex/* = false*/)
{
    if (m_Graph.contains(nNodeIndex_))
    {
        return;  // 避免重复添加
    }

    Node _Node = { nNodeIndex_, bIsComplex, nTradeIndex_, vecPreAdjacency_, {} };
    m_Graph.emplace(nNodeIndex_, _Node);
}

//!< 构建有向无环图
void iCAX::WorkShop::CWorkFlowGraph::Build()
{
    for (auto& [_nIndex, _Node] : m_Graph) 
    {
        for (const auto& _nPreIndex : _Node.vecPreAdjacency) 
        {
            m_Graph[_nPreIndex].vecPostAdjacency.push_back(_nIndex);
        }
    }
}

//!< 目标节点是否为图纸
bool iCAX::WorkShop::CWorkFlowGraph::IsComplex(IN const int& nNodeIndex_) const
{
    auto _Ite = m_Graph.find(nNodeIndex_);
    if (_Ite == m_Graph.end())
    {
        return false;//!< 代码保护，不会走到这里
    }

    return _Ite->second.bIsComplex;
}

//!< 获取工种类型
iCAX::Data::uuid iCAX::WorkShop::CWorkFlowGraph::GetTradeOrGraphID(IN const int& nIndex_) const
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return {};
    }
    return _Ite->second.nTradeOrGraphID;
}

//!< 获取后邻接工序节点
const std::vector<int>& iCAX::WorkShop::CWorkFlowGraph::GetPostAdjacency(IN const int& nIndex_) const
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        throw;//!< 代码保护，不会走到这里
    }

    return _Ite->second.vecPostAdjacency;
}

//!< 获取前邻接工序节点
const std::vector<int>& iCAX::WorkShop::CWorkFlowGraph::GetPreAdjacency(IN const int& nIndex_) const
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        throw;//!< 代码保护，不会走到这里
    }

    return _Ite->second.vecPreAdjacency;
}

//!< 获取工序索引列表
std::vector<int> iCAX::WorkShop::CWorkFlowGraph::GetNodeIndexes() const
{
    std::vector<int> _Indexes;

    for (auto& [_nIndex, _Node] : m_Graph)
    {
        _Indexes.push_back(_nIndex);
    }

    return _Indexes;
}
