#include "pch.h"
#include "ProcessingStatics.h"

//!< 初始化
void iCAX::WorkShop::CProcessingStatics::Initialize(IN const CWorkFlowGraph& Graph_)
{
    m_GraphID = Graph_.GetID();
    for (auto& _nIndex : Graph_.GetNodeIndexes())
    {
        auto _Pre = Graph_.GetPreAdjacency(_nIndex);
        auto _Post = Graph_.GetPostAdjacency(_nIndex);
        m_Graph[_nIndex] =
        { 
            _nIndex, 
            _Pre,
            _Post,
            _Pre.size(), 
            _Pre.size() == 0 ? kReady : kJoin,
            { 0 }
        };
    }
}

//!< 获取工序图索引
iCAX::Data::uuid iCAX::WorkShop::CProcessingStatics::GetGraphID()
{
    return m_GraphID;
}

//!< 筛选指定状态的索引
std::vector<int> iCAX::WorkShop::CProcessingStatics::FilterIndexesByStates(IN const RouteNodeState& nState_)
{
    std::vector<int> _nIndexes;

    for (auto& [_nIndex, _Node] : m_Graph)
    {
        if (_Node.nState.load() == nState_)
        {
            _nIndexes.push_back(_nIndex);
        }
    }

    return _nIndexes;
}

//!< 更新指定索引的状态
void iCAX::WorkShop::CProcessingStatics::UpdateState(IN const int& nIndex_, IN const RouteNodeState& State_)
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return;//!< 代码保护，不会走到这里
    }
    _Ite->second.nState.store(State_);

    if (State_ == kFault)
    {
        m_bFaultAnyNode = true;
    }
    if (State_ == kSuccess || State_ == kFault)//!< 如果完成了，则后置工序项更新入度
    {
        m_nNodeNotCompleted--;
        for (auto& _nPostIndex : _Ite->second.vecPostAdjacency)
        {
            DecrementDegree(_nPostIndex);
        }
    }
}

//!< 报告错误信息
void iCAX::WorkShop::CProcessingStatics::ReportDetail(IN const int& nIndex_, IN const int& nErrorCode_, std::exception Exception_, std::shared_ptr<CProcessingStatics> pSubStatics_/* = nullptr*/)
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return;//!< 代码保护，不会走到这里
    }

    _Ite->second.Detail.nErrorCode = nErrorCode_;
    _Ite->second.Detail.Exception = Exception_;
    _Ite->second.Detail.pSubStatics = pSubStatics_;
}

//!< 报告开始时间
void iCAX::WorkShop::CProcessingStatics::ReportStartTime(IN const int& nIndex_)
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return;//!< 代码保护，不会走到这里
    }

    _Ite->second.Detail.StartTime = std::chrono::system_clock::now();
}

//!< 报告结束时间
void iCAX::WorkShop::CProcessingStatics::ReportEndTime(IN const int& nIndex_)
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return;//!< 代码保护，不会走到这里
    }

    _Ite->second.Detail.StartTime = std::chrono::system_clock::now();
}

//!< 获取是否成功
bool iCAX::WorkShop::CProcessingStatics::IsCompleted() const
{
    return m_nNodeNotCompleted == 0;
}

//!< 获取是否失败
bool iCAX::WorkShop::CProcessingStatics::IsFault() const
{
    return m_bFaultAnyNode;
}

//!< 入度自减
void iCAX::WorkShop::CProcessingStatics::DecrementDegree(IN const int& nIndex_)
{
    auto _Ite = m_Graph.find(nIndex_);
    if (_Ite == m_Graph.end())
    {
        return;//!< 代码保护，不会走到这里
    }

    if (_Ite->second.nDegree.load() == 0)
    {
        return;//!< 代码保护，不会走到这里
    }

    --_Ite->second.nDegree;//!< 自减，等价于调用fetch_sub(1)

    if (_Ite->second.nDegree.load() < 0)
    {
        _Ite->second.nDegree.store(0);//!< 代码保护，不会走到这里
    }
    //!< 入度到0
    if (_Ite->second.nDegree.load() == 0 && _Ite->second.nState.load() == kJoin)
    {
        //!< 检查依赖的前置工序结果，如果都是success，则进入就绪
        //!< 如果前置工序存在失败，则直接置失败
        auto _nPreIndexes = _Ite->second.vecPreAdjacency;
        bool _bSuccess = true;
        for (auto& _nPreIndex : _nPreIndexes)
        {
            auto _ItePre = m_Graph.find(nIndex_);
            if (_ItePre == m_Graph.end() || _ItePre->second.nState.load() == kFault)
            {
                _bSuccess = false;
                break;
            }
        }
        UpdateState(nIndex_, _bSuccess ? kReady : kFault);
    }
}

//!< 计算耗时
std::chrono::milliseconds iCAX::WorkShop::CProcessingStatics::DetailInfo::Duration() const
{
    if (StartTime.time_since_epoch().count() == 0 || EndTime.time_since_epoch().count() == 0)
    {
        return std::chrono::milliseconds(0);
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(EndTime - StartTime);
}
