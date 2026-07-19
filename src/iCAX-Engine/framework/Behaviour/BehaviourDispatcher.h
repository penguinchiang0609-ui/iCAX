#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "../Database/IRepositoryEvent.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;

        /*
        * @brief Behaviour调度器
        * @remark
        *   Dispatcher 保存 Universe 已绑定的行为列表，并负责按帧顺序或 Repository 事件调用 Behaviour。
        *   行为执行顺序由 GetSchedule() 生成的执行计划决定；无依赖且优先级相同时按绑定顺序保持稳定。
        *   Dispatcher 按 Scene 单线程模型使用，不在内部为 Behaviour 回调加锁。
        */
        class _SYSTEM_EXP CBehaviourDispatcher final
        {
        private:
            enum NotifyType
            {
                kAddComponent = 0,
                kEnableComponent,
                kDisableComponent,
                kDestroyComponent,
                kModifyingComponent,
                kModifiedComponent,
            };

            /*
            * @brief 批量生命周期通知请求。
            * @details Dispatcher 处理 Repository 批量事件时先收集请求，再按 Behaviour 调度顺序执行。
            */
            struct NotifyRequest final
            {
                NotifyType Type = kAddComponent;
                std::shared_ptr<iCAX::Database::CComponentBase> pComponent;
                iCAX::Data::PropertySet Properties;
                CComponentDestroyInfo DestroyInfo;
                size_t Sequence = 0;
            };

        public:
            /*
            * @brief 构造函数
            */
            CBehaviourDispatcher(
                IN std::shared_ptr<IBehaviourRegistry> pRegistry_,
                IN std::shared_ptr<iCAX::Coroutines::CCoroutineRuntime> pCoroutineRuntime_);

            /*
            * @brief 追加行为
            * @param [in] nType_
            * @return true 表示新增绑定；false 表示该行为已绑定。
            */
            bool BindBehaviour(IN const std::type_index& nType_);

            /*
            * @brief 暂停指定类型 Behaviour 的帧更新。
            * @param [in] nType_
            * @details 仅跳过 PreUpdate/Update/PostUpdate；Start 和 Repository 生命周期事件仍正常分发。
            */
            void PauseFrameUpdate(IN const std::type_index& nType_);

            /*
            * @brief 是否已暂停帧更新。
            * @param [in] nType_
            * @return true 表示该 Behaviour 的帧更新已暂停。
            */
            bool IsFrameUpdatePaused(IN const std::type_index& nType_) const;

            /*
            * @brief 恢复指定类型 Behaviour 的帧更新。
            * @param [in] nType_
            */
            void ResumeFrameUpdate(IN const std::type_index& nType_);

            /*
            * @brief 获取所有已暂停帧更新的行为。
            * @return  std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>>
            */
            std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> GetFrameUpdatePaused() const;

            /*
            * @brief 获取所有被绑定的行为
            * @return  std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>>
            */
            std::vector<std::shared_ptr<iCAX::Behaviour::CBehaviourBase>> GetAll() const;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            bool HasBehaviour(IN const std::type_index& nType_) const;

            /*
            * @brief 注销行为
            * @param [in] nType_
            * @details 只解除当前 Dispatcher/Universe 的实例绑定，不影响注册表和插件 DLL 生命周期。
            */
            void UnbindBehaviour(IN const std::type_index& nType_);

            /*
            * @brief 关闭调度器中全部 Behaviour 实例。
            * @details 对每个已绑定 Behaviour 调用 Detach，然后清空绑定表。
            */
            void Shutdown();

            /*
            * @brief tick
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] nDeltaTime_
            * @param [in] nTotalTime_
            * @details
            *   执行顺序为：新组件 Start -> RefreshPreCache -> PreUpdate -> Update -> PostUpdate。
            *   已暂停帧更新的 Behaviour 只跳过 PreUpdate/Update/PostUpdate，Start 和 Repository 生命周期事件仍正常执行。
            *   调用方必须保证同一 Dispatcher 不被多个线程并发 Tick 或并发接收 Repository 通知。
            */
            void Tick(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_,
                IN std::function<void()> CoroutinePhase_ = {}) const;

            /*
            * @brief Repository 修改前通知入口。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] Args_ Repository 事件。
            * @details 只处理 remove/modify 前置语义：DestroyImmediate 入队、Modifying。
            */
            void OnRepositoryChanging(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) const;

            /*
            * @brief Repository 修改后通知入口。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in] ProjectContext_ 项目上下文。
            * @param [in] Args_ Repository 事件。
            * @details 普通事件立即分发；批量事件按 Behaviour 调度顺序和原始事件顺序统一分发。
            */
            void OnRepositoryChanged(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const iCAX::Database::RepositoryEventArgs& Args_) const;

        private:
            void RebuildExecutionPlan();
            size_t GetExecutionRank(IN const std::string& strComponentClass_) const;
            void QueueDestroyNotification(
                IN const CComponentDestroyInfo& DestroyInfo_) const;
            void DispatchDestroyImmediateAndQueue(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
                IN const CComponentDestroyInfo& DestroyInfo_) const;
            void FlushPendingDestroyNotifications(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) const;
            void OnNotify(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN NotifyType nType_,
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Data::PropertySet& Properties_) const;
            void OnNotifyBatch(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const std::vector<NotifyRequest>& Notifications_) const;

        private:
            std::unordered_set<std::type_index> m_setBehaviourIndex;
            //! 一个Component只有一个Behaviour与之对应
            //! 如果发现一个behaviour越来越庞大，此刻应该思考component是否应该拆分
            std::unordered_map<std::string, std::shared_ptr<CBehaviourBase>> m_BehavioursMap;
            std::vector<std::shared_ptr<CBehaviourBase>> m_OrderedList;
            std::vector<std::shared_ptr<CBehaviourBase>> m_ExecutionList;
            std::unordered_map<std::string, size_t> m_ExecutionRankByComponentClass;
            std::unordered_set<std::type_index> m_FrameUpdatePausedBehaviourTypes;
            mutable std::vector<NotifyRequest> m_PendingDestroyNotifications;
            mutable size_t m_nNextPendingDestroySequence = 0;
            std::shared_ptr<IBehaviourRegistry> m_pRegistry;
            std::shared_ptr<iCAX::Coroutines::CCoroutineRuntime> m_pCoroutineRuntime;
        };
    }
}
