#pragma once
#include "Database.h"
#include <functional>
#include <memory>
#include <tuple>
#include <vector>
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
            * @brief 开始批量变更作用域
            */
            virtual std::unique_ptr<IRepositoryChangeScope> BeginChangeScope(IN EChangeScopeKind Kind_, IN const std::string& strName_ = std::string()) = 0;

            /*
            * @brief 开始事务
            * @details
            *   事务内的更改立即作用于内存；Commit 后写入快速保存日志，Cancel 后按反向日志回滚。
            *   事务不等同于撤销还原记录，Commit 不会自动进入 undo 栈。
            *   如果事务型提交没有被 BeginUndoCommand/End 捕获，会清理旧撤销/重做历史。
            */
            virtual std::unique_ptr<IRepositoryChangeScope> BeginTransaction(IN const std::string& strName_ = std::string()) = 0;

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
            */
            virtual void OpenOperationLog(IN const std::string& strPath_, IN const bool bTruncate_ = false) = 0;

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
            */
            virtual void ReplayOperationLog(IN const std::string& strPath_) = 0;

        public:
            /*
            * @brief 创建实体
            */
            virtual std::shared_ptr<IEntity> CreateEntity(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 是否包含实体
            */
            virtual bool HasEntity(IN const iCAX::Data::uuid& ID_) const = 0;

            /*
            * @brief 移除实体
            */
            virtual void DeleteEntity(IN const iCAX::Data::uuid& ID_) = 0;

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
            * @return size_t
            */
            size_t GetComponentVersion(IN const CComponentBase& Component_) const
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
            * @return size_t
            */
            virtual size_t GetComponentVersion(IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const = 0;

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
        std::shared_ptr<IRepository> _DATABASE_EXP GenerateRepository(IN const iCAX::Data::uuid& RepositoryID_);
        std::shared_ptr<IRepository> _DATABASE_EXP GenerateRepository(
            IN const iCAX::Data::uuid& RepositoryID_,
            IN std::shared_ptr<IMetaRegistry> pMetaRegistry_);
    }
}
