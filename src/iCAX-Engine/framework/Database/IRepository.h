#pragma once
#include "Database.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "Data/Variant.h"
#include "IEntitiesView.h"
#include "IEntity.h"
#include "IRepositoryEvent.h"
#include "DerivedProperty.h"

namespace iCAX
{
    namespace Database
    {
        class IMetaRegistry;

        /*
        * @brief Repository 事务
        * @details
        *   事务对象只负责收集操作意图；生命周期由 Repository 的 Begin/Commit/CancelTransaction 管理。
        *   提交和取消时必须把 BeginTransaction 返回的事务对象传回 Repository，用于明确事务身份。
        */
        class _DATABASE_EXP ITransaction
        {
        public:
            ITransaction() = default;
            virtual ~ITransaction() = default;

            ITransaction(IN const ITransaction&) = delete;
            ITransaction& operator=(IN const ITransaction&) = delete;

            /*
            * @brief 在事务中记录创建实体操作。
            */
            virtual void CreateEntity(IN const iCAX::Data::uuid& EntityID_) = 0;

            /*
            * @brief 在事务中记录释放实体操作。
            */
            virtual void DisposeEntity(IN const iCAX::Data::uuid& EntityID_) = 0;

            /*
            * @brief 在事务中记录附加组件操作。
            */
            virtual void AttachComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_ = {}) = 0;

            /*
            * @brief 在事务中记录移除组件操作。
            */
            virtual void DetachComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) = 0;

            /*
            * @brief 在事务中记录修改组件字段操作。
            */
            virtual void ModifyComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_) = 0;

            /*
            * @brief 在事务中记录启用组件操作。
            */
            virtual void EnableComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) = 0;

            /*
            * @brief 在事务中记录禁用组件操作。
            */
            virtual void DisableComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_) = 0;
        };

        /*
        * @brief Repository 撤销还原记录作用域
        * @details BeginUndoCommand/End 之间提交的操作会合并为一个 undo step。
        */
        class _DATABASE_EXP IRepositoryUndoScope
        {
        public:
            IRepositoryUndoScope() = default;
            virtual ~IRepositoryUndoScope() = default;

            IRepositoryUndoScope(IN const IRepositoryUndoScope&) = delete;
            IRepositoryUndoScope& operator=(IN const IRepositoryUndoScope&) = delete;

            /*
            * @brief 结束当前撤销记录。
            */
            virtual void End() = 0;

            /*
            * @brief 判断撤销记录是否已结束。
            */
            virtual bool IsCompleted() const = 0;
        };

        /*
        * @brief 仓储
        */
        class _DATABASE_EXP IRepository : public IRepositoryEventPublisher
        {
        public:
            IRepository() = default;
            virtual ~IRepository() = default;
            //!< 禁用
            IRepository(IN const IRepository&) = delete;
            IRepository(IN const IRepository&&) = delete;
            IRepository& operator=(IN const IRepository&) = delete;
            IRepository& operator=(IN const IRepository&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            * @remark 此处给予仓储唯一ID，是为了以后可能存在多仓储留下可扩展能力
            *   以后希望的是，同一个app可以同时支持打开平面、平坡、管材、三维五轴等项目，或者单纯的平面多开
            */
            virtual const iCAX::Data::uuid& GetID() const = 0;

            virtual std::shared_ptr<IMetaRegistry> GetMetaRegistry() const = 0;

        public:
            /*
            * @brief 开始普通批量命令。
            * @details
            *   Begin/End 之间的修改立即作用于内存，但不会逐条外发事件；End 后最多发布一次批量事件。
            */
            virtual void BeginBatch() = 0;

            /*
            * @brief 结束普通批量命令。
            */
            virtual void EndBatch() = 0;

            /*
            * @brief 开始加载基线数据。
            * @details
            *   用于首次打开项目文件；End 后不进入撤销栈、不写快速保存日志、不发布用户修改事件。
            */
            virtual void BeginLoadBaseline() = 0;

            /*
            * @brief 结束加载基线数据。
            */
            virtual void EndLoadBaseline() = 0;

            /*
            * @brief 开始事务
            * @details
            *   返回当前事务对象。上层先向事务写入操作意图，再把该事务对象传给 CommitTransaction 或 CancelTransaction。
            *   事务不等同于撤销还原记录；若需要撤销，应由 BeginUndoCommand/End 包裹事务提交。
            */
            virtual ITransaction& BeginTransaction(IN const std::string& strName_ = std::string()) = 0;

            /*
            * @brief 提交指定事务。
            * @param [in] Transaction_ BeginTransaction 返回的事务对象。
            * @return true 表示全部操作成功应用；false 表示失败且已回滚提交阶段的部分修改。
            */
            [[nodiscard]] virtual bool CommitTransaction(IN ITransaction& Transaction_, OUT std::string& strError_) = 0;

            /*
            * @brief 提交指定事务，忽略错误文本。
            * @param [in] Transaction_ BeginTransaction 返回的事务对象。
            * @return true 表示全部操作成功应用；false 表示失败且已回滚提交阶段的部分修改。
            */
            [[nodiscard]] bool CommitTransaction(IN ITransaction& Transaction_)
            {
                std::string _strError;
                return CommitTransaction(Transaction_, _strError);
            }

            /*
            * @brief 取消指定事务，丢弃尚未提交的操作意图。
            * @param [in] Transaction_ BeginTransaction 返回的事务对象。
            */
            virtual void CancelTransaction(IN ITransaction& Transaction_) = 0;

            /*
            * @brief 开始一次撤销还原记录
            * @details
            *   Begin/End 之间提交的普通修改、批量修改和事务提交会被汇总为一个保序 undo step。
            */
            virtual std::unique_ptr<IRepositoryUndoScope> BeginUndoCommand(IN const std::string& strName_) = 0;

            /*
            * @brief 当前是否正在记录撤销还原步骤
            */
            virtual bool IsUndoCommandRecording() const = 0;

            /*
            * @brief 是否可以撤销
            */
            virtual bool CanUndo() const = 0;

            /*
            * @brief 是否可以重做
            */
            virtual bool CanRedo() const = 0;

            /*
            * @brief 获取撤销步骤列表
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray() const = 0;

            /*
            * @brief 获取重做步骤列表
            */
            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray() const = 0;

            /*
            * @brief 撤销栈顶步骤
            */
            virtual bool Undo() = 0;

            /*
            * @brief 重做栈顶步骤
            */
            virtual bool Redo() = 0;

            /*
            * @brief 打开快速保存操作日志
            * @param [in] strPath_ 日志路径。
            * @param [in] bTruncate_ true 表示截断旧日志并写入新日志头。
            * @param [in] strMagic_ 期望日志 magic，由上层产品或文件模块传入。
            * @param [in] nVersion_ 期望日志格式版本；只做严格匹配，不做迁移。
            */
            virtual void OpenOperationLog(
                IN const std::string& strPath_,
                IN const bool bTruncate_,
                IN const std::string& strMagic_,
                IN const uint32_t nVersion_) = 0;

            /*
            * @brief 关闭快速保存操作日志
            */
            virtual void CloseOperationLog() = 0;

            /*
            * @brief 是否已打开快速保存操作日志
            */
            virtual bool HasOperationLog() const = 0;

            /*
            * @brief 回放操作日志
            * @param [in] strPath_ 日志路径。
            * @param [in] strMagic_ 期望日志 magic。
            * @param [in] nVersion_ 期望日志格式版本；不匹配时拒绝回放。
            */
            virtual void ReplayOperationLog(
                IN const std::string& strPath_,
                IN const std::string& strMagic_,
                IN const uint32_t nVersion_) = 0;

        public:
            /*
            * @brief 创建实体
            */
            [[nodiscard]] virtual bool CreateEntity(IN const iCAX::Data::uuid& ID_, OUT std::shared_ptr<IEntity>& pEntity_, OUT std::string& strError_) = 0;

            std::shared_ptr<IEntity> CreateEntity(IN const iCAX::Data::uuid& ID_)
            {
                std::shared_ptr<IEntity> _pEntity;
                std::string _strError;
                if (!CreateEntity(ID_, _pEntity, _strError))
                {
                    throw std::runtime_error(_strError.empty() ? "Create entity failed" : _strError);
                }
                return _pEntity;
            }

            /*
            * @brief 是否包含实体
            */
            virtual bool HasEntity(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 移除实体
            */
            [[nodiscard]] virtual bool DeleteEntity(IN const iCAX::Data::uuid& ID_, OUT std::string& strError_) = 0;

            void DeleteEntity(IN const iCAX::Data::uuid& ID_)
            {
                std::string _strError;
                if (!DeleteEntity(ID_, _strError))
                {
                    throw std::runtime_error(_strError.empty() ? "Delete entity failed" : _strError);
                }
            }

            /*
            * @brief 获取实体
            */
            virtual std::shared_ptr<IEntity> GetEntity(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 筛选实体
            */
            virtual std::vector<std::shared_ptr<IEntity>> FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const = 0;

            /*
            * @brief 实体数量
            */
            virtual int EntityCount() const = 0;

            /*
            * @brief 获取实体ID列表
            */
            virtual std::vector<iCAX::Data::uuid> GetEntityIDs() const = 0;

            /*
            * @brief 获取实体视图
            */
            virtual IEntitiesView& GetView() const = 0;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 仓储自身的描述、配置信息承载者
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() = 0;

            /*
            * @brief 清空
            * @param [in] bForced_ 是否强制，默认false。如果为true，则本体实体也会被删除
            */
            virtual void CleanUp(IN const bool& bForced_ = false) = 0;

            /*
            * @brief 是否有效
            * @details
            *   如果缺失了本体实体，则为无效
            */
            virtual bool IsValid() = 0;

            /*
            * @brief 获取组件版本
            * @param [in] Component_
            * @return uint64_t
            */
            uint64_t GetComponentVersion(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return 0;
                }
                return GetComponentVersion(_pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 是否发生了更改
            * @param [in] Component_
            * @return bool
            */
            bool IsComponentChanged(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return false;
                }
                return IsComponentChanged(_pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 组件版本升级
            * @param [in] Component_
            */
            void BumpComponentVersion(IN const CComponentBase& Component_) const
            {
                auto _pEntity = Component_.GetEntity();
                if (!_pEntity)
                {
                    return;
                }
                return BumpComponentVersion(_pEntity->GetID(), Component_.GetComponentClass());
            }

            /*
            * @brief 是否发生了更改
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return bool
            */
            virtual bool IsComponentChanged(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 获取组件版本
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return uint64_t
            */
            virtual uint64_t GetComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 组件版本升级
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            */
            virtual void BumpComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

            /*
            * @brief 计算派生字段
            * @param [in] Component_ 目标组件
            * @param [in] strPropertyName_ 派生字段名称
            * @param [in] Evaluator_ 计算器
            */
            virtual PropertyValue EvaluateDerivedProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_) = 0;

            /*
            * @brief 重置组件更改标记
            * @details
            *   上层调用，每一帧结束的时候调用一次
            */
            virtual void ResetComponentChangedFlag() = 0;
        };

        /*
        * @brief 生成数据仓储
        * @return  std::shared_ptr<IRepository>
        */
        std::shared_ptr<IRepository> _DATABASE_EXP GenerateRepository(
            IN const iCAX::Data::uuid& RepositoryID_,
            IN std::shared_ptr<IMetaRegistry> pMetaRegistry_);
    }
}
