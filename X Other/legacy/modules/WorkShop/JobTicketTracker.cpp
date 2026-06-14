#include "pch.h"
#include "JobTicketTracker.h"

//!< 构造函数
iCAX::WorkShop::CJobTicketTracker::CJobTicketTracker(IN std::shared_ptr<CTradeRegistry> TradeRegistry_, IN std::shared_ptr<CWorkFlowRegistry>  GraphRegistry_, IN std::shared_ptr<CJobTicket> pTicket_, std::function<void(std::shared_ptr<CJobTicket>)> funOnSuccess_, std::function<void(std::shared_ptr<CJobTicket>)> funOnFault_)
    : m_pTradeRegistry(TradeRegistry_)
    , m_pWorkFlowRegistry(GraphRegistry_)
    , m_pTicket(pTicket_)
    , m_funOnSuccess(funOnSuccess_)
    , m_funOnFault(funOnFault_)
{
    m_pWrokFlowGraph = GraphRegistry_->GetGraph(pTicket_->GetStatics()->GetGraphID());
}

//!< 析构函数
iCAX::WorkShop::CJobTicketTracker::~CJobTicketTracker()
{
}

//!< 获取工单
std::shared_ptr<iCAX::WorkShop::CJobTicket> iCAX::WorkShop::CJobTicketTracker::GetTicket() const
{
    return m_pTicket;
}

//!< 循环检测
void iCAX::WorkShop::CJobTicketTracker::LoopUpdate()
{
    //!< 查看有哪些工序已经就绪了，就绪的则尝试申请工人
    auto _pStatics = m_pTicket->GetStatics();
    //!< 检查有哪些工序已就绪
    auto _nReadyIndexes = _pStatics->FilterIndexesByStates(CProcessingStatics::kReady);
    for (auto _nIndex : _nReadyIndexes)
    {
        auto _UID = m_pWrokFlowGraph->GetTradeOrGraphID(_nIndex);
        if (m_pWrokFlowGraph->IsComplex(_nIndex))
        {
            auto _pGraph =  m_pWorkFlowRegistry->GetGraph(_UID);
            if (_pGraph == nullptr)
            {
                _pStatics->ReportDetail(_nIndex, -3, {}, nullptr);//!< -3 表示图纸不存在
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
            }
            else
            {
                auto _pTracker = std::make_shared<CJobTicketTracker>(std::make_shared<CJobTicket>(*_pGraph, m_pTicket->GetWorkPiece()), nullptr, nullptr);
                m_lstSubTrackers.push_back({ _nIndex, _pTracker });//!< 记录在案
                _pStatics->ReportStartTime(_nIndex);
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kBusy);
            }
        }
        else
        {
            if (m_pTradeRegistry->Has(_UID))
            {
                std::shared_ptr<CWorkerBase> _pWorker;
                if (m_pTradeRegistry->TryFetchWorker(_UID, _pWorker))
                {
                    _pStatics->ReportStartTime(_nIndex);
                    _pStatics->UpdateState(_nIndex, CProcessingStatics::kBusy);
                    bool _bError = false;
                    try
                    {
                        _pWorker->Assign(m_pTicket);
                    }
                    catch (const std::exception& _Ex)
                    {
                        _bError = true;
                        _pStatics->ReportDetail(_nIndex, -2, _Ex, nullptr);//!< -2表示工人存在未处理异常
                        _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
                    }
                    if (!_bError)
                    {
                        m_lstWorkers.push_back({ _nIndex, _pWorker });
                    }
                }
            }
            else
            {
                _pStatics->ReportDetail(_nIndex, -1, {}, nullptr);//!< 工种不存在
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
            }
        }
    }

    //!< 工人工作
    for (auto _Ite = m_lstWorkers.begin(); _Ite != m_lstWorkers.end(); )
    {
        auto _nIndex = std::get<0>(*_Ite);
        auto _pWorker = std::get<1>(*_Ite);
        int _nCode = 0;
        bool _bTriggerException = false;
        try
        {
            _nCode = _pWorker->MoveNext();
        }
        catch(const std::exception& _Ex)//!< worker中存在未处理异常
        {
            _bTriggerException = true;
            _pStatics->ReportEndTime(_nIndex);
            _pStatics->ReportDetail(_nIndex, -2, _Ex, nullptr);//!< -2表示工人存在未处理异常
            _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
            try
            {
                _pWorker->Clear();
            }
            catch (...)
            {
                //!< 此处不用做处理
            }
            m_pTradeRegistry->ReturnWorker(_pWorker);
            _Ite = m_lstWorkers.erase(_Ite);
        }

        if (!_bTriggerException)
        {

            if (_nCode == 0)//!< 工作完成
            {
                _pStatics->ReportEndTime(_nIndex);
                _pStatics->ReportDetail(_nIndex, 0, {}, nullptr);
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kSuccess);
                _Ite = m_lstWorkers.erase(_Ite);
                try
                {
                    _pWorker->Clear();
                }
                catch (...)
                {
                    //!< 此处不用做处理
                }
                m_pTradeRegistry->ReturnWorker(_pWorker);
            }
            else if (_nCode < 0)//!< 工作出错
            {
                _pStatics->ReportEndTime(_nIndex);
                _pStatics->ReportDetail(_nIndex, abs(_nCode), {}, nullptr);
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
                try
                {
                    _pWorker->Clear();
                }
                catch (...)
                {
                    //!< 此处不用做处理
                }
                m_pTradeRegistry->ReturnWorker(_pWorker);
                _Ite = m_lstWorkers.erase(_Ite);
            }
            else//!< 工作中
            {
                //!< 等待下一次检测
                _Ite++;
            }
        }
    }

    //!< 检查子图纸
    for (auto _Ite = m_lstSubTrackers.begin(); _Ite != m_lstSubTrackers.end(); )
    {
        auto _nIndex = std::get<0>(*_Ite);
        auto _pTracker = std::get<1>(*_Ite);
        _pTracker->LoopUpdate();

        if (_pTracker->m_pTicket->GetStatics()->IsCompleted())
        {
            _pStatics->ReportEndTime(_nIndex);

            if (_pTracker->m_pTicket->GetStatics()->IsFault())
            {
                _pStatics->ReportDetail(_nIndex, -4, {}, _pTracker->m_pTicket->GetStatics());//!< -4表示子图纸出错
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kFault);
            }
            else
            {
                _pStatics->UpdateState(_nIndex, CProcessingStatics::kSuccess);
            }
            _Ite = m_lstSubTrackers.erase(_Ite);
        }
        else
        {
            //!< 等待下一次检测
            _Ite++;
        }
    }

    //!< 检查是否完成
    if (_pStatics->IsCompleted())
    {
        if (_pStatics->IsFault())
        {
            if (m_funOnFault != nullptr)
            {
                m_funOnFault(m_pTicket);//!< 回调失败处理函数
            }
        }
        else
        {
            if (m_funOnSuccess != nullptr)
            {
                m_funOnSuccess(m_pTicket);//!< 回调成功处理函数
            }
        }
    }
}


