#pragma once
#include "System.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../Database/ComponentBase.h"
#include "Data/uuid.h"
#include "ApplicationContext/IApplicationContext.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Task/Coroutine.h"

namespace iCAX
{
    namespace Behaviour
    {
        class CBehaviourDispatcher;

        /*
        * @brief Behaviour 调度描述。
        * @details
        *   ExecutionOrder 越小越早执行；RunAfter/RunBefore 使用 BehaviourClass 名称表达依赖。
        *   缺失的依赖目标会被忽略，循环依赖会在绑定 Behaviour 时抛出异常。
        */
        struct CBehaviourSchedule final
        {
            int ExecutionOrder = 0;                 //!< 越小越早执行。
            std::vector<std::string> RunAfter;      //!< 当前 Behaviour 在这些 Behaviour 之后执行。
            std::vector<std::string> RunBefore;     //!< 当前 Behaviour 在这些 Behaviour 之前执行。
        };

        /*
        * @brief 组件销毁后的快照信息。
        * @details
        *   Delayed OnDestroy 执行时，组件已经从 Entity/Repository 中移除，因此不能再传递 CComponentBase&。
        *   该结构只表达“被销毁的是什么”，以及销毁前的字段快照。
        */
        struct CComponentDestroyInfo final
        {
            iCAX::Data::uuid EntityID;                       //!< 被销毁组件所属实体 ID。
            std::string ComponentClass;                      //!< 被销毁组件类型。
            iCAX::Data::PropertySet PreviousProperties;      //!< 组件移除前的字段快照。
        };

        /*
        * @brief 行为
        * @remark
        *   1、Behaviour 是 Component 对应的 system/行为逻辑。
        *   2、行为对象由 Universe 通过注册表工厂创建，同一个 Scene/Universe 独享自己的 Behaviour 实例。
        *      Behaviour 可以保存本运行容器内的轻量状态，运行上下文由 Application/Product/Project/Scene 显式传入。
        *   3、公开生命周期方法由 Dispatcher 调用，派生类只需要覆写 OnXxx 钩子。
        *   4、Behaviour 回调遵循 Scene 单线程模型，不做内部并发保护；不要从前端线程或其他 Scene 线程直接调用。
        */
        class _SYSTEM_EXP CBehaviourBase
        {
        public:
            virtual ~CBehaviourBase() = default;

        public:
            /*
            * @brief 获取行为类型名称
            * @return 行为类名，通常由 AUTO_REGIST_BEHAVIOUR 宏生成。
            */
            virtual std::string GetBehaviourClass() const = 0;

            /*
            * @brief 获取该行为关注的组件类型名称
            * @return 组件类型名称。
            */
            virtual std::string GetComponentClass() const = 0;

            /*
            * @brief 获取行为调度描述。
            * @return 调度描述；默认 ExecutionOrder 为 0，且无显式依赖。
            * @details
            *   Dispatcher 使用该描述统一决定 Tick 阶段和批量 Repository 生命周期通知的执行顺序。
            *   普通单条 Repository 事件只有一个目标 Behaviour，因此不存在跨 Behaviour 排序问题。
            */
            virtual CBehaviourSchedule GetSchedule() const { return {}; }

        public:
            /*
            * @brief 组件进入 Universe 后的首次通知。
            * @param [in,out] Component_ 目标组件。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in,out] ProjectContext_ 项目上下文。
            * @details 该方法只转发到 OnAwake；视图缓存由 Dispatcher/Tick 统一维护。
            */
            void Awake(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件第一次参与帧循环时调用。
            */
            void Start(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件启用后调用。
            */
            void Enable(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 帧更新前调用。
            * @param [in] nDeltaTime_ 当前帧间隔秒数。
            * @param [in] nTotalTime_ 累计运行秒数。
            * @details 禁用组件不会进入帧更新阶段。
            */
            void PreUpdate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_);

            /*
            * @brief 帧更新调用。
            * @details 禁用组件不会进入帧更新阶段。
            */
            void Update(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_);

            /*
            * @brief 帧更新后调用。
            * @details 禁用组件不会进入帧更新阶段。
            */
            void PostUpdate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_);

            /*
            * @brief 组件禁用后调用。
            */
            void Disable(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件销毁后的统一销毁阶段调用。
            * @param [in] DestroyInfo_ 被销毁组件的快照信息。
            * @details
            *   Dispatcher 会在收到 Repository remove 事件时先调用 DestroyImmediate，
            *   再把销毁快照排入销毁队列；下一次 Tick 开始的统一销毁阶段调用 Destroy。
            *   若 Destroy 中继续删除其他组件，新产生的 Destroy 会在同一个销毁阶段继续处理。
            *   此时组件已经脱离 Entity/Repository，因此只提供销毁快照，不提供 CComponentBase&。
            */
            void Destroy(
                IN const CComponentDestroyInfo& DestroyInfo_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件销毁时立即调用。
            * @details
            *   该回调在 Repository remove 事件链路中同步触发，适合做必须立即保持数据一致的清理。
            */
            void DestroyImmediate(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件字段修改前调用。
            * @param [in] NewValues_ 即将写入的新字段集合。
            */
            void Modifying(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Data::PropertySet& NewValues_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 组件字段修改后调用。
            * @param [in] NewValues_ 已写入的新字段集合。
            */
            void Modified(
                IN iCAX::Database::CComponentBase& Component_,
                IN const iCAX::Data::PropertySet& NewValues_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_);

            /*
            * @brief 行为实例从 Dispatcher 中解除绑定前调用。
            * @details
            *   该钩子用于释放 Behaviour 持有的运行期资源。它不携带上下文参数，
            *   因为解绑可能发生在 Project 关闭阶段；需要访问上下文的数据应在正常生命周期内完成。
            */
            void Detach();

        public:
            /*
            * @brief 获取函数指针
            * @param [in] strFnName_ 函数名称。
            * @return 函数指针；不存在时返回 nullptr。
            * @details 该接口用于少量动态调用场景，常规行为逻辑优先通过生命周期钩子表达。
            */
            virtual void* GetFunction(const std::string& strFnName_) const { return nullptr; };

        protected:
            /*
            * @brief 为指定 Component 启动受其生命周期管理的协程。
            * @details Component 销毁时 Coroutine frame 会在 DestroyImmediate 前同步销毁。
            */
            template<typename TResult>
            iCAX::Coroutines::CCoroutineHandle<TResult> StartCoroutine(
                IN iCAX::Database::CComponentBase& Component_,
                IN iCAX::Coroutines::CCoroutine<TResult> Coroutine_)
            {
                auto _pRuntime = m_pCoroutineRuntime.lock();
                if (!_pRuntime)
                {
                    throw std::logic_error("Behaviour is not attached to a coroutine runtime");
                }
                if (m_ClosingCoroutineComponents.contains(&Component_))
                {
                    throw std::logic_error("Cannot start a coroutine while the component is being destroyed");
                }

                const auto _Found = m_ComponentCoroutines.find(&Component_);
                if (_Found == m_ComponentCoroutines.end())
                {
                    throw std::logic_error("Component is not attached to this behaviour");
                }

                std::erase_if(
                    _Found->second,
                    [&_pRuntime](const iCAX::Coroutines::CCoroutineHandleBase& Handle_)
                    {
                        return !_pRuntime->IsRunning(Handle_);
                    });

                auto _Handle = _pRuntime->Start(std::move(Coroutine_));
                try
                {
                    _Found->second.emplace_back(_Handle);
                }
                catch (...)
                {
                    _pRuntime->Cancel(_Handle);
                    throw;
                }
                if (!Component_.IsEnable())
                {
                    _pRuntime->Pause(_Handle);
                }
                return _Handle;
            }

            void CancelCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);
            void CancelAllCoroutines(IN iCAX::Database::CComponentBase& Component_);
            bool IsCoroutineRunning(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_) const;

        protected:
            virtual void OnAwake(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnStart(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnEnable(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnPreUpdate(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_, IN const double& nDeltaTime_, IN const double& nTotalTime_) {}
            virtual void OnUpdate(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_, IN const double& nDeltaTime_, IN const double& nTotalTime_) {}
            virtual void OnPostUpdate(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_, IN const double& nDeltaTime_, IN const double& nTotalTime_) {}
            virtual void OnDisable(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnDestroy(IN const CComponentDestroyInfo& DestroyInfo_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnDestroyImmediate(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnModifying(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnModified(IN iCAX::Database::CComponentBase& Component_, IN const iCAX::Data::PropertySet& NewValues_, IN const iCAX::Application::IApplicationContext& ApplicationContext_, IN const iCAX::Product::IProductContext& ProductContext_, IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_) {}
            virtual void OnDetach() {}

        private:
            void BindCoroutineRuntime(
                IN const std::shared_ptr<iCAX::Coroutines::CCoroutineRuntime>& pRuntime_);
            void UnbindCoroutineRuntime();
            void AttachComponentCoroutines(IN iCAX::Database::CComponentBase& Component_);
            void SetComponentCoroutinesEnabled(
                IN iCAX::Database::CComponentBase& Component_,
                IN bool Enabled_);
            void DetachComponentCoroutines(IN iCAX::Database::CComponentBase& Component_);
            void CompleteComponentCoroutineDetach(IN iCAX::Database::CComponentBase& Component_) noexcept;
            void CancelAllTrackedCoroutines();
            void ClearTrackedCoroutines() noexcept;

        private:
            std::weak_ptr<iCAX::Coroutines::CCoroutineRuntime> m_pCoroutineRuntime;
            std::unordered_map<
                iCAX::Database::CComponentBase*,
                std::vector<iCAX::Coroutines::CCoroutineHandleBase>> m_ComponentCoroutines;
            std::unordered_set<iCAX::Database::CComponentBase*> m_ClosingCoroutineComponents;

            friend class CBehaviourDispatcher;
        };
    }
}
