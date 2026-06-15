#include "pch.h"
#include "TrackCoordinator.h"
#include "RepositoryUndoRedo.h"
#include "Transaction.h"

//!< 构造函数
iCAX::Tracker::CTrackCoordinator::CTrackCoordinator(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
{
    m_pRepository = pRepository_;
    if (pRepository_)
    {
        auto _pUndoRedo = std::make_shared<CRepositoryUndoRedo>(pRepository_);
        m_pUndoRedo = _pUndoRedo;
    }
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
        _TransactionPtr->OnCommit();
        if (_TransactionPtr->IsCommitSuccess())
        {
            _bResult = true;
        }
        else
        {
            _bResult = false;
        }
    }
    else
    {
        // 不用做任何事
    }
    m_lstTransaction.erase(_Ite);
    return _bResult;
}
