#pragma once

#include "IRepositoryUndoRedo.h"
#include "../Database/IRepositoryEvent.h"
#include "../Database/IRepository.h"
#include <memory>

namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 仓储的撤销还原
        */
        class CRepositoryUndoRedo : public IRepositoryUndoRedo, public iCAX::Database::IOperationListener
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pRepository_ 仓储
            */
            CRepositoryUndoRedo(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_);
            /*
            * @brief 析构函数
            */
            virtual ~CRepositoryUndoRedo();

        public:
            /*
            * @brief 获取仓储
            * @return std::weak_ptr<iCAX::Database::IRepository>
            */
            virtual std::weak_ptr<iCAX::Database::IRepository> GetRepository() const override;

        public://!< 撤销还原
            /*
            * @brief 开始记录撤销还原
            * @param [in] DomainID_ 域ID
            * @param [in] strName_ 步骤名称
            * @return bool 操作是否成功
            */
            virtual bool BeginStep(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_) override;

            /*
            * @brief 停止撤销还原
            */
            virtual bool EndStep() override;

            /*
            * @brief 是否正在记录
            * @return bool
            */
            virtual bool IsRecording() const override;

            /*
            * @brief 获取撤销还原录制中的域ID
            * @return CGuid
            */
            virtual iCAX::Data::uuid GetCurrentRecordingDomain() const override;

            /*
            * @brief 是否可以重做
            * @param [in] DomainID_
            * @return bool
            */
            virtual bool CanRedo(IN const iCAX::Data::uuid& DomainID_) const override;

            /*
            * @brief 是否可以撤销
            * @param [in] DomainID_
            * @return bool
            */
            virtual bool CanUndo(IN const iCAX::Data::uuid& DomainID_) const override;

            /*
            * @brief 获取撤销步骤列表
            * @param [in] DomainID_
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const override;

            /*
            * @brief 获取重做步骤列表
            * @param [in] DomainID_
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const override;

            /*
            * @brief 重做
            * @param [in] DomainID_
            * @param [out] DomainSuggest_
            * @return bool 操作结果 true 成功 false 失败，失败时可以查看DomainSuggest_得知建议的实例（即从对应的实例开始重做）
            */
            virtual bool Redo(IN const iCAX::Data::uuid& DomainID_) override;

            /*
            * @brief 撤销
            * @param [in] DomainID_
            * @param [out] DomainSuggest_
            * @return bool 操作结果 true 成功 false 失败，失败时可以查看DomainSuggest_得知建议的实例（即从对应的实例开始重做）
            */
            virtual bool Undo(IN const iCAX::Data::uuid& DomainID_) override;

            /*
            * @brief 挂起
            * @param [in] bHalt_ 是否挂起
            */
            virtual void Halt(IN bool bHalt_) override;

            /*
            * @brief 批量记录
            * @param [in] Operations_
            */
            virtual void BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_) override;

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
            std::weak_ptr<iCAX::Database::IRepository> m_pRepository;
            std::unique_ptr<iCAX::Database::IRepositoryUndoScope> m_pCurrentUndoScope;
            bool m_bHalt;                                                                           //!< 挂起标记
        };
    }
}
