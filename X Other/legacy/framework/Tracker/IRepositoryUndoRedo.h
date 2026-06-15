#pragma once
#include <string>
#include <vector>
#include "Data/uuid.h"
#include "../Database/IRepository.h"
#include "../Database/IRepositoryEvent.h"

namespace iCAX
{
    namespace Tracker
    {
        /*
        * @brief 仓储撤销还原接口
        */
        class IRepositoryUndoRedo
        {
        public:
            /*
            * @brief 构造函数
            */
            IRepositoryUndoRedo() = default;
            /*
            * @brief 析构函数
            */
            virtual ~IRepositoryUndoRedo() = default;
            //!< 禁用
            IRepositoryUndoRedo(IN const IRepositoryUndoRedo&) = delete;
            IRepositoryUndoRedo(IN const IRepositoryUndoRedo&&) = delete;
            IRepositoryUndoRedo& operator=(IN const IRepositoryUndoRedo&) = delete;
            IRepositoryUndoRedo& operator=(IN const IRepositoryUndoRedo&&) = delete;

        public:
            /*
            * @brief 获取仓储ID
            * @return iCAX::Data::uuid
            */
            virtual std::weak_ptr<iCAX::Database::IRepository> GetRepository() const = 0;

        public://!< 撤销还原
            /*
            * @brief 开始记录撤销还原
            * @param [in] DomainID_ 域ID
            * @param [in] strName_ 步骤名称
            * @return bool 操作是否成功
            */
            virtual bool BeginStep(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_) = 0;

            /*
            * @brief 停止撤销还原
            */
            virtual bool EndStep() = 0;
            
            /*
            * @brief 是否正在记录
            * @return bool
            */
            virtual bool IsRecording() const = 0;

            /*
            * @brief 获取撤销还原录制中的域ID
            * @return CGuid
            */
            virtual iCAX::Data::uuid GetCurrentRecordingDomain() const = 0;

            /*
            * @brief 是否可以重做
            * @param [in] DomainID_
            * @return bool
            */
            virtual bool CanRedo(IN const iCAX::Data::uuid& DomainID_) const = 0;

            /*
            * @brief 是否可以撤销
            * @param [in] DomainID_
            * @return bool
            */
            virtual bool CanUndo(IN const iCAX::Data::uuid& DomainID_) const = 0;

            /*
            * @brief 获取撤销步骤列表
            * @param [in] DomainID_
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const = 0;

            /*
            * @brief 获取重做步骤列表
            * @param [in] DomainID_
            * @return std::vector<std::tuple<iCAX::Data::uuid, std::string>>
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const = 0;

            /*
            * @brief 重做
            * @param [in] DomainID_
            * @return bool 操作结果 true 成功 false 失败，失败时可以查看DomainSuggest_得知建议的实例（即从对应的实例开始重做）
            */
            virtual bool Redo(IN const iCAX::Data::uuid& DomainID_) = 0;

            /*
            * @brief 撤销
            * @param [in] DomainID_
            * @return bool 操作结果 true 成功 false 失败，失败时可以查看DomainSuggest_得知建议的实例（即从对应的实例开始重做）
            */
            virtual bool Undo(IN const iCAX::Data::uuid& DomainID_) = 0;

            /*
            * @brief 挂起
            * @param [in] bHalt_ 是否挂起
            */
            virtual void Halt(IN bool bHalt_) = 0;

            /*
            * @brief 批量记录
            * @param [in] Operations_
            */
            virtual void BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_) = 0;
        };
    }
}