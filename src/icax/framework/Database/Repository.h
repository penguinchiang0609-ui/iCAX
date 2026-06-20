#pragma once
#include <map>
#include <memory>
#include "IRepository.h"
#include "Entity.h"
#include "EntitiesView.h"
#include "VersionTable.h"
#include "DerivedProperty.h"
#include "ChangeSet.h"

namespace iCAX
{
    namespace Database
    {
        class CChangeSetJournal;
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
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_ = nullptr);

            /*
            * @brief 后触发
            * @param [in] nType_
            * @param [in] ID_
            * @param [in] strClassName_ 类名
            * @param [in] PreviousValue_ 旧值
            * @param [in] NewValue_ 新值
            */
            void TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<const CChangeSet> pChangeSet_ = nullptr);

        private:
            std::unique_ptr<IRepositoryChangeScope> BeginChangeScopeCore(IN EChangeScopeKind Kind_, IN const std::string& strName_, IN const bool bAutoCommitOnDestroy_);
            bool IsChangeScopeActive() const;
            bool IsRepositoryEventSuppressed() const;
            void EndChangeScope(IN const bool bCommit_);
            void RecordRepositoryChanged(IN const RepositoryEventArgs& Args_);
            CChangeSet BuildChangeSetFromRepositoryEvent(IN const RepositoryEventArgs& Args_) const;
            void ApplyEntityChangedEffects(IN const EntityEventArgs& Args_);
            void ApplyChangeSetEffects(IN const CChangeSet& ChangeSet_);
            void ApplyChangeSetForward(IN const CChangeSet& ChangeSet_);
            void ApplyChangeSetBackward(IN const CChangeSet& ChangeSet_);
            void ApplyChangeSetWithReplayEvent(IN const CChangeSet& ChangeSet_);
            void RollbackChangeSetSilently(IN const CChangeSet& ChangeSet_);
            void HandleCommittedChangeSet(IN const CChangeSet& ChangeSet_);
            void AppendOperationLog(IN const CChangeSet& ChangeSet_);

        private:
            std::list<std::weak_ptr<IRepositoryEventListener>> m_Observers;
            std::unique_ptr<CChangeSetBuilder> m_pChangeSetBuilder;
            EChangeScopeKind m_nChangeScopeKind = EChangeScopeKind::UserCommand;
            std::string m_strChangeScopeName;
            int m_nRepositoryEventSuppressionDepth = 0;
            int m_nHistoryReplayDepth = 0;
            std::unique_ptr<CRepositoryHistory> m_pHistory;
            std::unique_ptr<CChangeSetJournal> m_pOperationLog;

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
