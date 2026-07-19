#pragma once

#include "ProductExport.h"
#include "ProductContext/ProductDefinition.h"
#include "ProductContext/ProductData.h"
#include "ProductFacades.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Behaviour/BehaviourRegistrationCatalog.h"
#include "Facades/FacadeInvoker.h"
#include "Facades/Invocation.h"
#include "Facades/FacadeRegistry.h"
#include "Data/Variant.h"
#include "Database/IMetaRegistry.h"
#include "Database/MetaRegistrationCatalog.h"
#include "Facades/FacadeChannelRegistry.h"
#include "Facades/FacadeEndpoint.h"
#include "Project/Project.h"
#include "Project/ProjectCatalog.h"
#include "Project/ProjectRuntime.h"
#include "Resources/ResourceLoaderRegistrationCatalog.h"
#include "Resources/ResourceLoaderRegistry.h"
#include "ProductContext/IProductContext.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include "Task/Coroutine.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        /*
        * @brief 产品运行时
        * @details
        *   ProductRuntime 是产品级后台入口，拥有产品邮箱和多个 ProjectCatalog。
        *   Project 只承载项目身份和 ProjectSetting；Repository、ResourceLibrary、Universe、PDOHub、邮箱和工作线程归属 Scene。
        */
        class _PRODUCT_EXP CProductRuntime final
            : public IProductContext
            , public std::enable_shared_from_this<CProductRuntime>
        {
        public:
            /*
            * @brief 构造产品运行时。
            * @param [in] Definition_ 产品静态定义。
            * @param [in] pApplicationContext_ 应用上下文，不能为空。
            * @param [in] pProductDataStore_ 产品数据存储，可为空；为空时使用默认文件存储。
            */
            CProductRuntime(
                IN const CProductDefinition& Definition_,
                IN std::shared_ptr<const iCAX::Application::IApplicationContext> pApplicationContext_,
                IN std::shared_ptr<iCAX::Interaction::CFacadeChannelRegistry> pFacadeChannelRegistry_,
                IN std::shared_ptr<IProductDataStore> pProductDataStore_ = nullptr,
                IN uint32_t nFrameIntervalMilliseconds_ = 16);
            ~CProductRuntime();

            CProductRuntime(IN const CProductRuntime&) = delete;
            CProductRuntime& operator=(IN const CProductRuntime&) = delete;

        public:
            /*
            * @brief 启动产品运行时。
            * @details
            *   加载产品数据和模块，按模块路径回放组件/行为/服务/资源/Facade 注册，并创建产品邮箱通道。
            *   重复调用无副作用。
            */
            void Start();

            /*
            * @brief 停止产品运行时。
            * @details 会关闭所有项目 runtime/catalog，移除产品邮箱，并清空 Facade 上下文。
            */
            void Stop();

            /*
            * @brief 获取 ProductRuntime 工作循环的 Task 调度器。
            * @details Schedule 只负责线程安全入队，任务由产品工作线程在后续循环中执行。
            */
            iCAX::Tasks::TaskSchedulerPtr GetProductTaskScheduler() const noexcept;

            /*
            * @brief 在 ProductRuntime 工作循环中启动协程。
            * @details 只能在产品工作线程调用；协程由产品循环逐帧恢复，产品停止时自动取消。
            */
            template<typename TResult>
            iCAX::Coroutines::CCoroutineHandle<TResult> StartCoroutine(
                iCAX::Coroutines::CCoroutine<TResult> coroutine_)
            {
                return RequireProductCoroutineRuntimeOnWorker().Start(std::move(coroutine_));
            }

            /*
            * @brief 停止一个 ProductRuntime 协程。
            * @details 只能在产品工作线程调用。
            */
            void PauseCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);
            void ResumeCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);
            void CancelCoroutine(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_);

            /*
            * @brief 取消当前 ProductRuntime 中的全部协程。
            * @details 取消后仍可启动新协程；只能在产品工作线程调用。
            */
            void CancelAllCoroutines();

            /*
            * @brief 判断指定产品协程是否仍在运行。
            * @details 只能在产品工作线程调用。
            */
            bool IsCoroutineRunning(IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_) const;

            /*
            * @brief 判断产品是否已启动。
            */
            bool IsStarted() const;

            /*
            * @brief 判断产品线程是否异常退出。
            */
            bool IsFaulted() const;

            /*
            * @brief 获取产品线程最近异常文本。
            */
            std::string GetFaultMessage() const;

            /*
            * @brief 获取产品定义。
            */
            const CProductDefinition& GetDefinition() const override;

            /*
            * @brief 获取产品数据快照。
            */
            CProductData GetProductData() const override;

            /*
            * @brief 获取产品级设置快照。
            */
            iCAX::Data::PropertyBag GetSettings() const override;

            /*
            * @brief 替换并保存产品级设置。
            */
            void ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_) override;

            /*
            * @brief 获取产品 ID。
            */
            const std::string& GetProductID() const override;

            /*
            * @brief 获取产品通信通道 ID。
            */
            const iCAX::Data::uuid& GetProductChannelID() const override;

            /*
            * @brief 获取后端视角的产品Facade 端点。
            * @return 产品邮箱的后端端点。
            * @throws std::logic_error 产品未启动时抛出。
            */
            iCAX::Interaction::CFacadeEndpoint GetBackendFacadeEndpoint() const override;

            /*
            * @brief 获取前端视角的产品Facade 端点。
            */
            iCAX::Interaction::CFacadeEndpoint GetFrontendFacadeEndpoint() const override;

            /*
            * @brief 获取前端视角的产品 Facade 端点。
            * @return 产品 channel 的前端端点。
            * @throws std::logic_error 产品未启动时抛出。
            */
            iCAX::Interaction::CFacadeEndpoint GetProductFrontendFacadeEndpoint() const;

            /*
            * @brief 向产品级前端主动发送 Facade Event。
            * @param [in] nMethodCode_ 事件方法码，使用 FacadeMethod 的 64 位主/子编码。
            * @param [in] strPayloadText_ UTF-8 文本负载，通常是 VariantSerializer 文本。
            * @details
            *   主动事件使用 Event 帧，前端通过 FacadeClient.subscribe/subscribeAll 接收。
            */
            void SendFrontendEvent(IN uint64_t nMethodCode_, IN const std::string& strPayloadText_);

            /*
            * @brief 分发产品级 Facade frame。
            * @details ProductRuntime 自己的工作线程会调用；白盒测试也可以手动调用一次以加快响应。
            */
            void DispatchProductFacadeFrames();

            /*
            * @brief 获取当前项目 catalog 快照。
            */
            std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> GetProjectCatalogs() const;

            /*
            * @brief 按 catalog ID 查找项目 catalog。
            * @return 找到时返回 catalog，否则返回 nullptr。
            */
            std::shared_ptr<iCAX::Project::CProjectCatalog> FindProjectCatalog(
                IN const iCAX::Data::uuid& CatalogID_) const;

            /*
            * @brief 按项目 ID 查找所属 catalog。
            * @return 找到时返回 catalog，否则返回 nullptr。
            */
            std::shared_ptr<iCAX::Project::CProjectCatalog> FindProjectCatalogByProjectID(
                IN const iCAX::Data::uuid& ProjectID_) const;

            /*
            * @brief 打开主项目 catalog。
            * @param [in] strCatalogName_ catalog 名称，空时自动推导。
            * @param [in] strCatalogPath_ catalog 路径，空时使用项目路径。
            * @param [in] strProjectName_ 主项目名称，空时使用 catalog 名称。
            * @param [in] strProjectPath_ 主项目路径。
            * @return 新打开的项目 catalog。
            * @details 会创建 main project、project runtime，启动主 Scene 线程，并记录最近项目。
            */
            std::shared_ptr<iCAX::Project::CProjectCatalog> OpenProjectCatalog(
                IN const std::string& strCatalogName_ = std::string(),
                IN const std::string& strCatalogPath_ = std::string(),
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());
            /*
            * @brief 关闭指定项目。
            * @return true 表示项目存在并被关闭。
            */
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);

            /*
            * @brief 关闭指定项目 catalog。
            * @return true 表示 catalog 存在并被关闭。
            */
            bool CloseProjectCatalog(IN const iCAX::Data::uuid& CatalogID_);

            /*
            * @brief 获取指定 Scene 的前端Facade 端点。
            * @return Scene 邮箱的前端端点。
            * @throws std::runtime_error 项目或 Scene 不存在时抛出。
            */
            iCAX::Interaction::CFacadeEndpoint GetSceneFrontendFacadeEndpoint(
                IN const iCAX::Data::uuid& ProjectID_,
                IN const iCAX::Data::uuid& SceneID_) const;

            /*
            * @brief 构建产品状态负载。
            * @return 可序列化 Variant，供前端查询产品状态。
            */
            iCAX::Data::Variant BuildProductStatePayload() const;

            /*
            * @brief 构建项目 catalog 状态负载。
            */
            iCAX::Data::Variant BuildProjectCatalogPayload(
                IN const std::shared_ptr<iCAX::Project::CProjectCatalog>& pCatalog_) const;

            /*
            * @brief 获取产品 Facade 注册表。
            */
            iCAX::Interaction::CFacadeRegistry& GetFacadeRegistry() const override;

            /*
            * @brief 获取应用级服务容器。
            * @details 当前服务容器在 Application 范围共享。
            */
            iCAX::Services::CServiceProvider& GetServiceProvider() const override;

            /*
            * @brief 获取产品级组件元数据注册表。
            */
            iCAX::Database::IMetaRegistry& GetMetaRegistry() const override;

            /*
            * @brief 获取产品级 Behaviour 注册表。
            */
            iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override;

            /*
            * @brief 获取产品级资源加载器注册表。
            */
            iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override;

        private:
            /*
            * @brief 确保产品已启动。
            * @throws std::logic_error 未启动时抛出。
            */
            void EnsureStarted() const;

            /*
            * @brief 产品工作线程入口。
            * @details 只分发产品邮箱；Scene 邮箱由 Scene 线程每帧分发。
            */
            void WorkerMain();

            void InitializeProductCoroutinesOnWorker();
            void ShutdownProductCoroutinesOnWorker() noexcept;
            iCAX::Coroutines::CCoroutineRuntime& RequireProductCoroutineRuntimeOnWorker();
            const iCAX::Coroutines::CCoroutineRuntime& RequireProductCoroutineRuntimeOnWorker() const;

            /*
            * @brief 关闭项目、catalog 和产品邮箱。
            * @details 调用方必须已经停止产品工作线程，或当前就在产品工作线程的异常退出路径中。
            */
            void CloseRuntimeObjects();

            /*
            * @brief 记录产品线程异常。
            */
            void RecordFault(IN const std::string& strMessage_, IN std::exception_ptr pException_);

            /*
            * @brief 获取 catalog 快照。
            */
            std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> SnapshotProjectCatalogs() const;

            /*
            * @brief 获取项目 runtime 快照。
            */
            std::vector<std::shared_ptr<iCAX::Project::IProjectRuntime>> SnapshotProjectRuntimes() const;

            /*
            * @brief 获取产品数据快照。
            */
            CProductData SnapshotProductData() const;

            /*
            * @brief 创建 Scene 级资源加载器注册表。
            * @details 根据产品已加载模块路径回放 loader，保证每个 Scene 有独立 Registry。
            */
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> CreateProjectResourceLoaderRegistry() const;

            /*
            * @brief 查找项目 runtime。
            */
            std::shared_ptr<iCAX::Project::IProjectRuntime> FindProjectRuntime(
                IN const iCAX::Data::uuid& ProjectID_) const;

            /*
            * @brief 登记项目 runtime。
            */
            void RegisterProjectRuntime(
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);

            /*
            * @brief 移除并返回项目 runtime。
            */
            std::shared_ptr<iCAX::Project::IProjectRuntime> RemoveProjectRuntime(
                IN const iCAX::Data::uuid& ProjectID_);

            /*
            * @brief 分发指定 Scene 或产品 channel 中的 Facade frame。
            * @param [in] Endpoint_ 后端视角 Facade 端点。
            * @param [in] pProjectRuntime_ 项目运行时；为空表示产品级 channel。
            * @param [in] pSceneContext_ 当前 Scene 上下文；为空表示产品级 channel。
            */
            void DispatchSceneFacadeFrames(
                IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_,
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_,
                IN iCAX::Project::ISceneContext* pSceneContext_);
            /*
            * @brief 启动项目 runtime 并接入项目帧回调。
            */
            void StartProject(IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);

            /*
            * @brief 加载产品数据。
            */
            void LoadProductData();

            /*
            * @brief 保存产品数据。
            */
            void SaveProductData(IN const CProductData& ProductData_) const;

            /*
            * @brief 记录最近打开项目。
            */
            void RecordRecentProject(
                IN const std::string& strProjectPath_,
                IN const std::string& strDisplayName_);
            /*
            * @brief 加载产品定义中的模块。
            */
            void LoadProductModules();

            /*
            * @brief 注册产品内建 Facade。
            */
            void RegisterBuiltInProductFacades();

            /*
            * @brief 处理获取产品状态方法。
            * @return 包含产品状态的调用结果。
            */
            iCAX::Interaction::CInvocationResult HandleGetState(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理列出项目 catalog 方法。
            * @return 当前实现返回完整产品状态，其中包含 catalog 列表。
            */
            iCAX::Interaction::CInvocationResult HandleListProjectCatalogs(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理打开项目 catalog 方法。
            * @param [in] Request_ Payload 可包含 catalogName/catalogPath/projectName/projectPath。
            * @return 打开的 catalog 状态和产品状态。
            */
            iCAX::Interaction::CInvocationResult HandleOpenProjectCatalog(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理关闭项目 catalog 方法。
            * @param [in] Request_ Payload 必须包含 catalogId。
            * @return 关闭后的产品状态。
            */
            iCAX::Interaction::CInvocationResult HandleCloseProjectCatalog(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理项目状态查询方法。
            * @details 必须在项目 Scene 的 Facade scope 调用；响应包含 project state 和 undoRedo 快照。
            */
            iCAX::Interaction::CInvocationResult HandleProjectGetState(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理项目撤销重做状态查询方法。
            */
            iCAX::Interaction::CInvocationResult HandleProjectGetUndoRedoState(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理项目撤销方法。
            */
            iCAX::Interaction::CInvocationResult HandleProjectUndo(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

            /*
            * @brief 处理项目重做方法。
            */
            iCAX::Interaction::CInvocationResult HandleProjectRedo(
                IN const iCAX::Interaction::CInvocation& Request_,
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_);

        private:
            CProductDefinition m_Definition;
            CProductData m_ProductData;
            iCAX::Data::uuid m_ProductChannelID;
            std::shared_ptr<const iCAX::Application::IApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pProductServiceProvider;
            std::shared_ptr<iCAX::Interaction::CFacadeChannelRegistry> m_pFacadeChannelRegistry;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pProductMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pProductBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pProductResourceLoaderRegistry;
            std::shared_ptr<IProductDataStore> m_pProductDataStore;
            std::shared_ptr<iCAX::Interaction::CFacadeRegistry> m_pFacadeRegistry;
            std::unique_ptr<iCAX::Interaction::CFacadeInvoker> m_pFacadeInvoker;
            mutable std::mutex m_RuntimeMutex;
            std::condition_variable m_WorkerCondition;
            std::thread m_WorkThread;
            std::thread::id m_WorkThreadID;
            std::atomic_bool m_bStopWorkerRequested = false;
            bool m_bStarted = false;
            bool m_bFaulted = false;
            std::string m_strFaultMessage;
            std::exception_ptr m_pFaultException = nullptr;
            std::shared_ptr<iCAX::Tasks::EventLoopTaskScheduler> m_pProductTaskScheduler;
            std::unique_ptr<iCAX::Coroutines::CCoroutineRuntime> m_pProductCoroutineRuntime;
            uint32_t m_nFrameIntervalMilliseconds = 16;
            bool m_bModulesLoaded = false;
            bool m_bRegistrationsReplayed = false;
            std::vector<std::string> m_LoadedModulePaths;
            mutable std::mutex m_ProductDataMutex;
            mutable std::mutex m_ProjectCatalogMutex;
            std::map<iCAX::Data::uuid, std::shared_ptr<iCAX::Project::CProjectCatalog>> m_ProjectCatalogs;
            mutable std::mutex m_ProjectRuntimeMutex;
            std::map<iCAX::Data::uuid, std::shared_ptr<iCAX::Project::IProjectRuntime>> m_ProjectRuntimes;
        };
    }
}

