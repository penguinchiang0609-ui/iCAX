#pragma once
#include "ApplicationRuntimeExport.h"
#include "ApplicationContext/ApplicationContext.h"
#include "ApplicationRuntimeFacades.h"
#include "ApplicationRuntimeConfig.h"
#include "ProductFileResolver.h"
#include "Facades/FacadeInvoker.h"
#include "Facades/Invocation.h"
#include "Facades/FacadeRegistry.h"
#include "Data/Variant.h"
#include "Facades/FacadeEndpoint.h"
#include "Product/ProductRuntime.h"
#include "Facades/FacadeChannelRegistry.h"
#include "Task/Coroutine.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace iCAX
{
    namespace Application
    {
        enum class EApplicationRuntimeState : uint8_t
        {
            Stopped = 0,
            Starting = 1,
            Running = 2,
            Stopping = 3,
            Faulted = 4,
        };

        enum class EApplicationRuntimeEventCode : uint8_t
        {
            Starting = 0,
            Started = 1,
            Stopping = 2,
            Stopped = 3,
            Faulted = 4,
        };

        enum class EApplicationRuntimeStopReason : uint8_t
        {
            None = 0,
            Requested = 1,
            StartFailed = 2,
            Faulted = 3,
        };

        enum class EApplicationRuntimePhase : uint8_t
        {
            None = 0,
            Starting = 1,
            Loading = 2,
            Running = 3,
            Stopping = 4,
            Unloading = 5,
        };

        struct _APPLICATION_RUNTIME_EXP ApplicationRuntimeFault final
        {
            EApplicationRuntimePhase Phase = EApplicationRuntimePhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        struct _APPLICATION_RUNTIME_EXP ApplicationRuntimeEvent final
        {
            EApplicationRuntimeEventCode Code = EApplicationRuntimeEventCode::Stopped;
            EApplicationRuntimeState State = EApplicationRuntimeState::Stopped;
            EApplicationRuntimeStopReason StopReason = EApplicationRuntimeStopReason::None;
            EApplicationRuntimePhase Phase = EApplicationRuntimePhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        using ApplicationRuntimeEventHandler = std::function<void(const ApplicationRuntimeEvent&)>;

        /*
        * @brief 应用运行时
        * @details
        *   ApplicationRuntime 是前端启动后最先连接的后台入口。
        *   它只负责应用上下文、产品清单、产品运行时生命周期和应用级 Facade。
        */
        class _APPLICATION_RUNTIME_EXP CApplicationRuntime
        {
        public:
            /*
            * @brief 构造应用运行时。
            * @details 默认会创建一个 icax.default 产品定义，调用方可在 Start 前 SetConfig 覆盖。
            */
            CApplicationRuntime();
            virtual ~CApplicationRuntime();

        public:
            /*
            * @brief 设置运行时配置。
            * @param [in] Config_ 新配置。
            * @throws std::logic_error 运行时正在运行或停止时抛出。
            */
            void SetConfig(IN const ApplicationRuntimeConfig& Config_);

            /*
            * @brief 启动运行时工作线程。
            * @details Start 会等待工作线程完成 Load 并进入 Running，启动失败会重新抛出工作线程异常。
            */
            void Start();

            /*
            * @brief 停止运行时工作线程并卸载产品运行时。
            */
            void Stop();

            /*
            * @brief 判断运行时是否运行中。
            */
            bool IsRunning() const;

            /*
            * @brief 获取运行时状态。
            */
            EApplicationRuntimeState GetState() const;

            /*
            * @brief 获取运行时当前阶段。
            */
            EApplicationRuntimePhase GetPhase() const;

            /*
            * @brief 获取最近一次停止原因。
            */
            EApplicationRuntimeStopReason GetStopReason() const;

            /*
            * @brief 获取最近一次异常。
            */
            std::optional<ApplicationRuntimeFault> GetLastFault() const;

            /*
            * @brief 订阅运行时事件。
            * @param [in] Handler_ 事件回调，不能为空。
            * @return 订阅 ID，可用于取消订阅。
            */
            uint64_t SubscribeEvent(IN ApplicationRuntimeEventHandler Handler_);

            /*
            * @brief 取消运行时事件订阅。
            * @return true 表示移除了订阅。
            */
            bool UnsubscribeEvent(IN uint64_t nSubscriptionID_);

            /*
            * @brief 获取 ApplicationRuntime 工作循环的 Task 调度器。
            * @details Schedule 只负责线程安全入队，任务由 ApplicationRuntime 工作线程在后续循环中执行。
            */
            iCAX::Tasks::TaskSchedulerPtr GetApplicationTaskScheduler() const noexcept;

            /*
            * @brief 在 ApplicationRuntime 工作循环中启动协程。
            * @details 只能在 ApplicationRuntime 工作线程调用；协程由运行时循环逐帧恢复，运行时停止时自动取消。
            */
            template<typename TResult>
            iCAX::Coroutines::CCoroutineHandle<TResult> StartCoroutine(
                iCAX::Coroutines::CCoroutine<TResult> coroutine_)
            {
                return RequireApplicationCoroutineRuntimeOnWorker().Start(std::move(coroutine_));
            }

            /*
            * @brief 暂停一个 ApplicationRuntime 协程。
            * @details 只能在 ApplicationRuntime 工作线程调用。
            */
            void PauseCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);
            void ResumeCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);
            void CancelCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);

            /*
            * @brief 取消当前 ApplicationRuntime 中的全部协程。
            * @details 取消后仍可启动新协程；只能在运行时工作线程调用。
            */
            void CancelAllCoroutines();

            /*
            * @brief 判断指定运行时协程是否仍在运行。
            * @details 只能在 ApplicationRuntime 工作线程调用。
            */
            bool IsCoroutineRunning(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_) const;

        private:
            /*
            * @brief 运行时工作线程入口。
            * @details 执行 Load -> MainLoop -> Unload，并把异常转换为 Faulted 状态和事件。
            */
            void WorkerMain();

            void InitializeApplicationCoroutinesOnWorker();
            void ShutdownApplicationCoroutinesOnWorker() noexcept;
            iCAX::Coroutines::CCoroutineRuntime& RequireApplicationCoroutineRuntimeOnWorker();
            const iCAX::Coroutines::CCoroutineRuntime& RequireApplicationCoroutineRuntimeOnWorker() const;

            /*
            * @brief 加载应用上下文、注册表、服务和应用级 Facade channel。
            */
            void Load();

            /*
            * @brief 运行时主循环。
            * @details 每帧只分发应用级 Facade frame；产品级 frame 由各 ProductRuntime 自己的线程分发。
            */
            void MainLoop();

            /*
            * @brief 卸载产品运行时、应用级 Facade channel、服务和上下文。
            */
            void Unload();

            /*
            * @brief 通知运行时事件。
            */
            void NotifyEvent(
                IN EApplicationRuntimeEventCode Code_,
                IN const std::string& strMessage_ = {},
                IN std::exception_ptr pException_ = nullptr);
            /*
            * @brief 设置当前阶段。
            */
            void SetPhase(IN EApplicationRuntimePhase Phase_);

            /*
            * @brief 记录异常信息。
            */
            void RecordFault(
                IN EApplicationRuntimePhase Phase_,
                IN const std::string& strMessage_,
                IN std::exception_ptr pException_);
            /*
            * @brief 从 exception_ptr 提取错误文本。
            */
            static std::string GetExceptionMessage(IN std::exception_ptr pException_);

            /*
            * @brief 分发应用级 Facade frame。
            */
            void DispatchApplicationFacadeFrames();

            /*
            * @brief 创建应用上下文。
            */
            std::shared_ptr<iCAX::Application::CApplicationContext> CreateApplicationContext() const;

            /*
            * @brief 注册 ApplicationRuntime 内建 Facade。
            */
            void RegisterBuiltInApplicationFacades();

            /*
            * @brief 处理获取应用状态方法。
            */
            iCAX::Interaction::CInvocationResult HandleGetState(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理列出产品 Facade 方法。
            * @details 当前实现返回完整应用状态，其中包含产品定义和运行态信息。
            */
            iCAX::Interaction::CInvocationResult HandleListProducts(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理启动产品 Facade 方法。
            * @param [in] Request_ Payload 可包含 productId；单产品时可省略。
            */
            iCAX::Interaction::CInvocationResult HandleStartProduct(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理停止产品 Facade 方法。
            * @param [in] Request_ Payload 必须包含 productId。
            */
            iCAX::Interaction::CInvocationResult HandleStopProduct(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理识别项目文件产品 Facade 方法。
            * @param [in] Request_ Payload 必须包含 projectPath。
            */
            iCAX::Interaction::CInvocationResult HandleResolveProjectFile(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理打开项目文件方法。
            * @param [in] Request_ Payload 必须包含 projectPath，可包含 catalogName/projectName。
            */
            iCAX::Interaction::CInvocationResult HandleOpenProjectFile(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 构建应用状态负载。
            */
            iCAX::Data::Variant BuildApplicationStatePayload() const;

            /*
            * @brief 构建项目文件识别结果负载。
            */
            iCAX::Data::Variant BuildProductFileResolvePayload(IN const CProductFileResolveResult& Result_) const;

            /*
            * @brief 构建产品定义和运行态负载。
            */
            iCAX::Data::Variant BuildProductPayload(
                IN const iCAX::Product::CProductDefinition& Definition_,
                IN const std::shared_ptr<iCAX::Product::CProductRuntime>& pRuntime_) const;
            /*
            * @brief 查找产品定义。
            * @param [in] strProductID_ 产品 ID；为空且只有一个产品时返回唯一产品。
            * @return 产品定义。
            */
            iCAX::Product::CProductDefinition FindProductDefinition(IN const std::string& strProductID_) const;

            /*
            * @brief 获取产品 runtime 快照。
            */
            std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> SnapshotProductRuntimes() const;

        public:
            /*
            * @brief 获取运行时支持的产品定义。
            */
            std::vector<iCAX::Product::CProductDefinition> GetProductDefinitions() const;

            /*
            * @brief 获取已启动产品 runtime 快照。
            */
            std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> GetProductRuntimes() const;

            /*
            * @brief 查找已启动产品 runtime。
            */
            std::shared_ptr<iCAX::Product::CProductRuntime> FindProductRuntime(IN const std::string& strProductID_) const;

            /*
            * @brief 启动产品。
            * @param [in] strProductID_ 产品 ID；为空且只有一个产品时启动唯一产品。
            * @return 产品运行时。
            * @details 并发启动同一产品时，只会创建一个 runtime，其他调用等待并复用。
            */
            std::shared_ptr<iCAX::Product::CProductRuntime> StartProduct(IN const std::string& strProductID_ = std::string());

            /*
            * @brief 停止产品。
            * @return true 表示产品 runtime 存在并被停止。
            */
            bool StopProduct(IN const std::string& strProductID_);

            /*
            * @brief 根据项目文件 magic 识别产品。
            */
            CProductFileResolveResult ResolveProjectFileProduct(IN const std::string& strProjectPath_) const;

            /*
            * @brief 打开项目文件。
            * @details 先识别文件所属产品，再启动产品并打开项目 catalog。
            */
            std::shared_ptr<iCAX::Project::CProjectCatalog> OpenProjectFile(
                IN const std::string& strProjectPath_,
                IN const std::string& strCatalogName_ = std::string(),
                IN const std::string& strProjectName_ = std::string());

            /*
            * @brief 获取应用级 Facade 前端端点。
            * @details 前端启动后首先通过该 Facade 端点与 ApplicationRuntime 通信。
            */
            iCAX::Interaction::CFacadeEndpoint GetApplicationFrontendFacadeEndpoint() const;

            /*
            * @brief 向应用级前端主动发送 Facade Event。
            * @param [in] nMethodCode_ 事件方法码，使用 FacadeMethod 的 64 位主/子编码。
            * @param [in] strPayloadText_ UTF-8 文本负载，通常是 VariantSerializer 文本。
            * @details
            *   主动事件使用 Event 帧，前端通过 FacadeClient.subscribe/subscribeAll 接收。
            */
            void SendFrontendEvent(IN uint64_t nMethodCode_, IN const std::string& strPayloadText_);

            /*
            * @brief 获取产品级 Facade 前端端点。
            */
            iCAX::Interaction::CFacadeEndpoint GetProductFrontendFacadeEndpoint(IN const std::string& strProductID_) const;

            /*
            * @brief 获取 Scene 邮箱前端端点。
            */
            iCAX::Interaction::CFacadeEndpoint GetSceneFrontendFacadeEndpoint(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_) const;

            /*
            * @brief 获取应用通信通道 ID。
            */
            const iCAX::Data::uuid& GetApplicationChannelID() const;

            const iCAX::Application::IApplicationContext& GetApplicationContext() const
            {
                return *m_pApplicationContext;
            }

            iCAX::Interaction::CFacadeRegistry& GetFacadeRegistry() const
            {
                return *m_pFacadeRegistry;
            }

        private:
            ApplicationRuntimeConfig m_Config;
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StartupCondition;
            std::thread m_WorkThread;
            std::thread::id m_WorkThreadID;
            std::atomic_bool m_bStopRequested = false;
            bool m_bStartupCompleted = false;
            EApplicationRuntimeState m_State = EApplicationRuntimeState::Stopped;
            EApplicationRuntimeStopReason m_StopReason = EApplicationRuntimeStopReason::None;
            EApplicationRuntimePhase m_Phase = EApplicationRuntimePhase::None;
            std::optional<ApplicationRuntimeFault> m_LastFault;
            std::exception_ptr m_pWorkerException = nullptr;
            std::shared_ptr<iCAX::Tasks::EventLoopTaskScheduler> m_pApplicationTaskScheduler;
            std::unique_ptr<iCAX::Coroutines::CCoroutineRuntime> m_pApplicationCoroutineRuntime;
            mutable std::mutex m_EventMutex;
            std::map<uint64_t, ApplicationRuntimeEventHandler> m_mapEventHandlers;
            uint64_t m_nNextEventSubscriptionID = 1;
            iCAX::Data::uuid m_ApplicationChannelID;
            std::shared_ptr<iCAX::Interaction::CFacadeRegistry> m_pFacadeRegistry;
            std::unique_ptr<iCAX::Interaction::CFacadeInvoker> m_pFacadeInvoker;
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Interaction::CFacadeChannelRegistry> m_pFacadeChannelRegistry;
            mutable std::mutex m_ProductRuntimeMutex;
            std::condition_variable m_ProductRuntimeCondition;
            std::set<std::string> m_StartingProductIDs;
            std::set<std::string> m_StoppingProductIDs;
            std::map<std::string, std::shared_ptr<iCAX::Product::CProductRuntime>> m_ProductRuntimes;
        };
    }
}

