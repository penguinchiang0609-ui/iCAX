#pragma once
#include <map>
#include <memory>
#include "IRepository.h"
#include "Entity.h"
#include "EntitiesView.h"
#include "VersionTable.h"
#include "DerivedProperty.h"
#include "OperationLog.h"

namespace iCAX
{
    namespace Database
    {
        class COperationBatchJournal;
        class CRepositoryHistory;
        class IMetaRegistry;

        /*
        * @brief 仓储实现
        */
        class CRepository final : public IRepository, public IEntityEventListener, public std::enable_shared_from_this<CRepository>
        {
            friend class CRepositoryChangeScope;
            friend class CRepositoryEventSuppressor;

        public:
            /*
            * @brief 构造函数
            * @param [in] 
            */
            explicit CRepository(IN const iCAX::Data::uuid& UID_);
            CRepository(IN const iCAX::Data::uuid& UID_, IN std::shared_ptr<IMetaRegistry> pMetaRegistry_);

            /*
            * @brief 析构函数
            */
            virtual ~CRepository();

            //!< 禁用
            CRepository(IN const CRepository&) = delete;
            CRepository(IN const CRepository&&) = delete;
            CRepository& operator= (IN const CRepository&) = delete;
            CRepository& operator= (IN const CRepository&&) = delete;

        public:
            /*
            * @brief 获取ID
            * @return const iCAX::Data::uuid&
            */
            virtual const iCAX::Data::uuid& GetID() const override;
            virtual std::shared_ptr<IMetaRegistry> GetMetaRegistry() const override;

        public:
            /*
            * @brief 开始批量变更作用域
            */
            virtual std::unique_ptr<IRepositoryChangeScope> BeginChangeScope(IN EChangeScopeKind Kind_, IN const std::string& strName_ = std::string()) override;

            /*
            * @brief 开始事务
            */
            virtual std::unique_ptr<IRepositoryChangeScope> BeginTransaction(IN const std::string& strName_ = std::string()) override;

            /*
            * @brief 开始一次撤销还原记录
            */
            virtual std::unique_ptr<IRepositoryUndoScope> BeginUndoCommand(IN const std::string& strName_) override;

            /*
            * @brief 当前是否正在记录撤销还原步骤
            */
            virtual bool IsUndoCommandRecording() const override;

            virtual bool CanUndo() const override;

            virtual bool CanRedo() const override;

            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray() const override;

            virtual std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray() const override;

            /*
            * @brief 撤销
            */
            virtual bool Undo() override;

            /*
            * @brief 重做
            */
            virtual bool Redo() override;

            /*
            * @brief 打开快速保存操作日志
            */
            virtual void OpenOperationLog(IN const std::string& strPath_, IN const bool bTruncate_ = false) override;

            /*
            * @brief 关闭快速保存操作日志
            */
            virtual void CloseOperationLog() override;

            /*
            * @brief 是否已打开快速保存操作日志
            */
            virtual bool HasOperationLog() const override;

            /*
            * @brief 回放操作日志
            */
            virtual void ReplayOperationLog(IN const std::string& strPath_) override;

        public:
            /*
            * @brief 初始化
            */
            void Initialzie();

            /*
            * @brief 创建实体
            */
            virtual std::shared_ptr<IEntity> CreateEntity(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 是否包含实体
            */
            virtual bool HasEntity(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 移除实体
            */
            virtual void DeleteEntity(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 获取实体
            */
            virtual std::shared_ptr<IEntity> GetEntity(IN const iCAX::Data::uuid& ID_) const override;

            /*
            * @brief 筛选实体
            */
            virtual std::vector<std::shared_ptr<IEntity>> FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const override;

            /*
            * @brief 实体数量
            */
            virtual int EntityCount() const override;

            /*
            * @brief 获取实体ID列表
            */
            virtual std::vector<iCAX::Data::uuid> GetEntityIDs() const override;

            /*
            * @brief 获取实体视图
            */
            virtual IEntitiesView& GetView() const override;

            /*
            * @brief 获取MetaEntity
            * @return std::shared_ptr<IEntity>
            * @remark 仓储自身的描述、配置信息承载者
            */
            virtual std::shared_ptr<IEntity> GetMetaEntity() override;

            /*
            * @brief 清空
            */
            virtual void CleanUp(IN const bool& bForced_ = false) override;

            /*
            * @brief 是否有效
            */
            virtual bool IsValid() override;

            /*
            * @brief 获取组件版本
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return size_t
            */
            virtual size_t GetComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 组件版本升级
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            */
            virtual void BumpComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 计算派生字段
            */
            virtual PropertyValue EvaluateDerivedProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_) override;

            /*
            * @brief 是否发生了更改
            * @param [in] nEntityID_
            * @param [in] strComponentType_
            * @return bool
            */
            virtual bool IsComponentChanged(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const override;

            /*
            * @brief 重置组件更改标记
            * @details
            *   上层调用，每一帧结束的时候调用一次
            */
            virtual void ResetComponentChangedFlag() override;

        public://!< 实体事件观察方法
            /*
            * @brief 实体修改前事件
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanging(IN void* pSender_, IN const EntityEventArgs& Args_) override;

            /*
            * @brief 实体更改后事件
            * @param [in] strPropertyName_
            * @param [in] NewValue_ 属性值
            */
            virtual void OnEntityChanged(IN void* pSender_, IN const EntityEventArgs& Args_) override;

        public:
            /*
            * @brief 添加观察者
            * @param [in] Observer_
            */
            virtual void AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) override;

            /*
            * @brief 移除观察者
            * @param [in] Observer_
            */
            virtual void RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_) override;

        protected:
            /*
            * @brief 前触发
            * @param [in] nType_
            * @param [in] EntityID_ 目标实体 ID。
            * @param [in] strClassName_ 目标组件类型；实体级事件为空。
            * @param [in] Previous_ 操作前字段集合。
            * @param [in] New_ 操作后字段集合。
            * @param [in] pComponent_ 关联组件；实体级事件可为空。
            * @param [in] pEntity_ 关联实体。
            * @param [in] pChangeSet_ 批量事件携带的 ChangeSet 摘要；普通事件为空。
            */
            void TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_ = nullptr);

            /*
            * @brief 后触发
            * @param [in] nType_
            * @param [in] EntityID_ 目标实体 ID。
            * @param [in] strClassName_ 目标组件类型；实体级事件为空。
            * @param [in] Previous_ 操作前字段集合。
            * @param [in] New_ 操作后字段集合。
            * @param [in] pComponent_ 关联组件；实体级事件可为空。
            * @param [in] pEntity_ 关联实体。
            * @param [in] pChangeSet_ 批量事件携带的 ChangeSet 摘要；普通事件为空。
            */
            void TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_ = nullptr);

        private:
            /*
            * @brief 创建 Repository 内部批量变更作用域。
            * @param [in] Kind_ 批次语义。
            * @param [in] strName_ 批次名称。
            * @param [in] bAutoCommitOnDestroy_ 作用域析构时是否自动提交；false 时自动取消。
            * @return 批量变更作用域。
            */
            std::unique_ptr<IRepositoryChangeScope> BeginChangeScopeCore(IN EChangeScopeKind Kind_, IN const std::string& strName_, IN const bool bAutoCommitOnDestroy_);

            /*
            * @brief 判断当前是否存在活跃批量变更作用域。
            * @return true 表示当前正在收集 OperationBatch。
            */
            bool IsChangeScopeActive() const;

            /*
            * @brief 判断 Repository 事件是否被临时抑制。
            * @return true 表示当前不应向外发布事件，也不应记录操作。
            */
            bool IsRepositoryEventSuppressed() const;

            /*
            * @brief 结束当前批量变更作用域。
            * @param [in] bCommit_ true 表示提交，false 表示取消并静默回滚。
            * @details
            *   提交时先冻结有序 OperationBatch，再派生 ChangeSet 净摘要。
            *   ChangeSet 非空才发布批量事件、更新版本/派生字段、进入历史和快速保存日志。
            *   取消时按 OperationBatch 反向顺序静默回滚，不发布事件。
            */
            void EndChangeScope(IN const bool bCommit_);

            /*
            * @brief 将 Repository 事件追加为有序操作。
            * @param [in] Args_ Repository 事件参数。
            */
            void RecordRepositoryOperation(IN const RepositoryEventArgs& Args_);

            /*
            * @brief 应用单个实体事件对版本和派生字段的影响。
            * @param [in] Args_ 实体事件参数。
            */
            void ApplyEntityChangedEffects(IN const EntityEventArgs& Args_);

            /*
            * @brief 应用 ChangeSet 摘要对版本和派生字段的影响。
            * @param [in] ChangeSet_ 批量净变更摘要。
            */
            void ApplyChangeSetEffects(IN const CChangeSet& ChangeSet_);

            /*
            * @brief 正向执行一条 Repository 操作。
            * @param [in] Operation_ 待执行操作。
            */
            void ApplyOperationForward(IN const CRepositoryOperation& Operation_);

            /*
            * @brief 按原顺序正向执行操作批次。
            * @param [in] Batch_ 待执行操作批次。
            */
            void ApplyOperationBatchForward(IN const COperationBatch& Batch_);

            /*
            * @brief 按反向语义执行操作批次。
            * @param [in] Batch_ 原始操作批次。
            * @details
            *   内部先生成反向 OperationBatch，再按反向批次的正向顺序执行。
            *   等价于按原批次倒序逐条恢复。
            */
            void ApplyOperationBatchBackward(IN const COperationBatch& Batch_);

            /*
            * @brief 以 Replay 批量事件语义执行操作批次。
            * @param [in] Batch_ 待回放操作批次。
            * @details
            *   Undo、Redo 和快速保存恢复都会走该路径。
            *   该方法会创建 Replay 作用域，使回放期间仍能产生一次批量事件，但不会再次进入历史栈。
            */
            void ApplyOperationBatchWithReplayEvent(IN const COperationBatch& Batch_);

            /*
            * @brief 静默回滚操作批次。
            * @param [in] Batch_ 原始操作批次。
            * @details
            *   用于事务 Cancel 和批量作用域 Cancel。
            *   回滚期间通过事件抑制器避免通知外部，也避免写入历史或快速保存日志。
            */
            void RollbackOperationBatchSilently(IN const COperationBatch& Batch_);

            /*
            * @brief 处理已提交操作批次。
            * @param [in] Batch_ 有序操作批次，是历史和日志的事实来源。
            * @param [in] Summary_ 净变更摘要，用于判断提交是否有对外可见效果。
            */
            void HandleCommittedOperationBatch(IN const COperationBatch& Batch_, IN const CChangeSet& Summary_);

            /*
            * @brief 追加快速保存操作日志。
            * @param [in] Batch_ 待写入的有序操作批次。
            */
            void AppendOperationLog(IN const COperationBatch& Batch_);

        private:
            std::list<std::weak_ptr<IRepositoryEventListener>> m_Observers;
            std::unique_ptr<COperationBatchBuilder> m_pOperationBatchBuilder;
            EChangeScopeKind m_nChangeScopeKind = EChangeScopeKind::UserCommand;
            std::string m_strChangeScopeName;
            int m_nRepositoryEventSuppressionDepth = 0;
            int m_nHistoryReplayDepth = 0;
            std::unique_ptr<CRepositoryHistory> m_pHistory;
            std::unique_ptr<COperationBatchJournal> m_pOperationLog;

        private:
            iCAX::Data::uuid m_UID;                                                           //!< 仓储ID
            std::map<iCAX::Data::uuid, std::shared_ptr<CEntity>> m_mapEntities;               //!< 实体字典
            std::shared_ptr<CEntitiesView> m_pEntitesView;                                    //!< 视图数据
            std::shared_ptr<VersionTable> m_pVerisonTable;                                     //!< 版本号表
            std::shared_ptr<CDerivedPropertyManager> m_pDerivedPropertyManager;                 //!< 派生字段管理器
            std::shared_ptr<IMetaRegistry> m_pMetaRegistry;
        };
    }
}
