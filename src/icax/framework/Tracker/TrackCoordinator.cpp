#include "pch.h"
#include "TrackCoordinator.h"
#include "Transaction.h"

//!< 构造函数
iCAX::Tracker::CTrackCoordinator::CTrackCoordinator(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
{
    m_pRepository = pRepository_;
}

//!< 析构函数
iCAX::Tracker::CTrackCoordinator::~CTrackCoordinator()
{
}

//!< 获取仓储
std::weak_ptr<iCAX::Database::IRepository> iCAX::Tracker::CTrackCoordinator::GetRepository() const
{
    return m_pRepository;
}

//!< 获取撤销还原
std::shared_ptr<iCAX::Tracker::IRepositoryUndoRedo> iCAX::Tracker::CTrackCoordinator::GetUndoRedo() const
{
    return m_pUndoRedo;
}

//!< 开始事务
std::weak_ptr<iCAX::Tracker::ITransaction> iCAX::Tracker::CTrackCoordinator::BeginTransaction()
{
    std::shared_ptr<iCAX::Tracker::CTransaction> _pTransaction = std::make_shared<CTransaction>(m_pRepository.lock());
    m_lstTransaction.push_back(_pTransaction);
    return _pTransaction;  // 返回弱指针
}

//!< 结束事务
bool iCAX::Tracker::CTrackCoordinator::EndTransaction(IN std::weak_ptr<ITransaction> pTransaction_, IN const bool& bCommit_)
{
    auto _TransactionPtr = std::dynamic_pointer_cast<CTransaction>(pTransaction_.lock());  // 尝试将弱指针转换为共享指针
    auto _Ite = std::find_if(m_lstTransaction.begin(), m_lstTransaction.end()
        , [&_TransactionPtr](const std::shared_ptr<iCAX::Tracker::ITransaction>& taskPtr)
        {
            return taskPtr == _TransactionPtr;
        });
    if (_Ite == m_lstTransaction.end())
    {
        return false;// 代码保护，不会走到这里
    }

    bool _bResult = false;
    if (bCommit_)
    {
        m_pRepository.lock()->AddObserver(_TransactionPtr);
        m_pUndoRedo->Halt(true);
        //m_SyncWriterPtr->Halt(true);
        _TransactionPtr->OnCommit();
        if (_TransactionPtr->IsCommitSuccess())
        {
            _bResult = true;
            //!< 如果正在撤销还原中，则一次性提交所有实际操作
            if (m_pUndoRedo->IsRecording())
            {
                m_pUndoRedo->BatchRecord(_TransactionPtr->GetActualOperations());
            }
            //if (m_SyncWriterPtr->IsRecording())
            //{
            //    m_SyncWriterPtr->BatchRecord(_TransactionPtr->GetActualOperations());
            //}
        }
        else
        {
            _bResult = false;
            _TransactionPtr->OnRollback();
        }

        m_pUndoRedo->Halt(false);
        //m_SyncWriterPtr->Halt(false);
        m_pRepository.lock()->RemoveObserver(_TransactionPtr);
    }
    else
    {
        // 不用做任何事
    }
    m_lstTransaction.erase(_Ite);
    return _bResult;
}
