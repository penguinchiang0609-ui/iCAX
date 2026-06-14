#pragma once
#include "ITrackCoordinator.h"
#include "Transaction.h"
#include <list>
#include <memory.h>

namespace iCAX
{
    namespace Tracker
    {
        class CTrackCoordinator : public ITrackCoordinator
        {
        public:
            /*
            * @brief 构造函数
            */
            CTrackCoordinator(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_);

            /*
            * @brief 析构函数
            */
            virtual ~CTrackCoordinator();

        public:
            /*
            * @brief 获取仓储
            * @return iCAX::Data::uuid
            */
            virtual std::weak_ptr<iCAX::Database::IRepository> GetRepository() const override;

            /*
            * @brief 获取撤销还原
            * @return std::shared_ptr<IRepositoryUndoRedo>
            */
            virtual std::shared_ptr<IRepositoryUndoRedo> GetUndoRedo() const override;

            /*
            * @brief 开始事务
            * @return std::weak_ptr<ITransaction>
            */
            virtual std::weak_ptr<ITransaction> BeginTransaction() override;

            /*
            * @brief 结束事务
            * @param [in] pTransaction_
            * @param [in] bCommit_
            */
            virtual bool EndTransaction(IN std::weak_ptr<ITransaction> pTransaction_, IN const bool& bCommit_ = true) override;

        private:
            std::weak_ptr<iCAX::Database::IRepository> m_pRepository;
            std::shared_ptr<IRepositoryUndoRedo> m_pUndoRedo;
            std::list<std::shared_ptr<CTransaction>> m_lstTransaction;
        };
    }
}