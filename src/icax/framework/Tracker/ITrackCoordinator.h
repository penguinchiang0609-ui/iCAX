#pragma once
#include <string>
#include <vector>
#include "Data/uuid.h"
#include "../Database/IRepository.h"
#include "../Database/IRepositoryEvent.h"
#include "IRepositoryUndoRedo.h"
#include "ITransaction.h"

namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 跟踪器协调者
        */
        class ITrackCoordinator
        {
        public:
            /*
            * @brief 构造函数
            */
            ITrackCoordinator() = default;

            /*
            * @brief 析构函数
            */
            virtual ~ITrackCoordinator() = default;

            //!< 禁用
            ITrackCoordinator(IN const ITrackCoordinator&) = delete;
            ITrackCoordinator(IN const ITrackCoordinator&&) = delete;
            ITrackCoordinator& operator=(IN const ITrackCoordinator&) = delete;
            ITrackCoordinator& operator=(IN const ITrackCoordinator&&) = delete;

        public:
            /*
            * @brief 获取仓储ID
            * @return iCAX::Data::uuid
            */
            virtual std::weak_ptr<iCAX::Database::IRepository> GetRepository() const = 0;

            /*
            * @brief 获取撤销还原
            * @return std::shared_ptr<IRepositoryUndoRedo>
            */
            virtual std::shared_ptr<IRepositoryUndoRedo> GetUndoRedo() const = 0;

            /*
            * @brief 获取撤销还原
            * @return std::weak_ptr<ITransaction>
            */
            virtual std::weak_ptr<ITransaction> BeginTransaction() = 0;

            /*
            * @brief 结束事务
            * @param [in] pTransaction_
            * @param [in] bCommit_
            */
            virtual bool EndTransaction(IN std::weak_ptr<ITransaction> pTransaction_, IN const bool& bCommit_ = true) = 0;
        };

        /*
        * @brief 生成Tracker
        * @return  std::shared_ptr<ITrackCoordinator>
        */
        std::shared_ptr<ITrackCoordinator> GenerateTracker(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_);
    }
}