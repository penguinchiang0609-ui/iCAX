#pragma once
#include "pch.h"
#include "ITransaction.h"

#include <memory>
#include "../Database/IDomain.h"
#include "ITrackCoordinator.h"
namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 事务
        */
        class CTransaction final : public ITransaction, public iCAX::Database::IOperationListener, public std::enable_shared_from_this<CTransaction>
        {
        private:
            /*
            * @brief 操作
            */
            struct Operation
            {
                enum
                {
                    kCreateEntity,
                    kDisposeEntity,
                    kAttachComponent,
                    kDetachComponent,
                    kModifyComponent,
                } nType;
                iCAX::Data::uuid DomainID;
                iCAX::Data::uuid EntityID;
                std::string ClassType;
                iCAX::Data::PropertySet Properties;
            };

        public:
            /*
            * @brief 构造函数
            * @param [in] pRepository_
            */
            CTransaction(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_);

            /*
            * @brief 析构函数
            */
            virtual ~CTransaction();

            //!< 禁用
            CTransaction(IN const CTransaction&) = delete;
            CTransaction& operator=(IN const CTransaction&) = delete;
            CTransaction(IN const CTransaction&&) = delete;
            CTransaction& operator=(IN const CTransaction&&) = delete;

        public:
            /*
            * @brief 创建实体
            * @param [in] DomainID_
            * @param [in] EntityID_
            */
            virtual void CreateEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_) override;

            /*
            * @brief 释放实体
            * @param [in] DomainID_
            * @param [in] EntityID_
            */
            virtual void DisposeEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_) override;

            /*
            * @brief 附加组件
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体
            * @param [in] ComponentType_ 组件类型
            * @param [in] Properties_ 组件值
            */
            virtual void AttachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) override;

            /*
            * @brief 移除组件
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体ID
            * @param [in] strClassName_ 组件类型
            */
            virtual void DetachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) override;

            /*
            * @brief 修改组件
            * @param [in] DomainID_
            * @param [in] EntityID_ 实体ID
            * @param [in] strClassName_ 组件类型
            * @param [in] Properties_ 组件值
            */
            virtual void ModifyComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) override;

            /*
            * @brief 提交事务
            * @remark 将事务中的所有操作应用到仓库
            */
            void OnCommit();

            /*
            * @brief 回滚事务
            * @remark 将事务中的所有操作逆向撤销
            */
            void OnRollback();

            /*
            * @brief 是否提交成功
            * @return bool
            */
            bool IsCommitSuccess() const;

            /*
            * @brief 获取实际操作列表
            * @return const std::list<iCAX::Database::OperationUnit>&
            */
            const std::list<iCAX::Database::OperationUnit>& GetActualOperations() const;

        public:
            /*
            * @brief 修改前事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_) override;

            /*
            * @brief 更改后事件
            * @param [in] pSender_
            * @param [in] Args_
            */
            virtual void OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_) override;

        private:
            std::weak_ptr<iCAX::Database::IRepository> m_pRepository;               //!< 域
            std::list<Operation> m_Operations;                                      //!< 存储所有操作记录
            std::list<iCAX::Database::OperationUnit> m_ActualOperations;            //!< 实际发生的操作
            bool m_bCommitSuccess;                                                  //!< 是否提交成功
        };
    }
}