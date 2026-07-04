#pragma once
#include "ApplicationHostExport.h"
#include "ApplicationContext/ApplicationContext.h"
#include "ApplicationHostCommands.h"
#include "ApplicationHostConfig.h"
#include "ProductFileResolver.h"
#include "CommandHandler/CommandDispatcher.h"
#include "CommandHandler/CommandMessage.h"
#include "CommandHandler/CommandRegistry.h"
#include "Data/Variant.h"
#include "Mailbox/MailPostOffice.h"
#include "MailHandler/CMailCommandHandler.h"
#include "Product/ProductRuntime.h"
#include "Mailbox/MailChannelRegistry.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
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
#include <vector>


namespace iCAX
{
    namespace ApplicationHost
    {
        enum class EApplicationHostState : uint8_t
        {
            Stopped = 0,
            Starting = 1,
            Running = 2,
            Stopping = 3,
            Faulted = 4,
        };

        enum class EApplicationHostEventCode : uint8_t
        {
            Starting = 0,
            Started = 1,
            Stopping = 2,
            Stopped = 3,
            Faulted = 4,
        };

        enum class EApplicationHostStopReason : uint8_t
        {
            None = 0,
            Requested = 1,
            StartFailed = 2,
            Faulted = 3,
        };

        enum class EApplicationHostPhase : uint8_t
        {
            None = 0,
            Starting = 1,
            Loading = 2,
            Running = 3,
            Stopping = 4,
            Unloading = 5,
        };

        struct _APPLICATION_HOST_EXP ApplicationHostFault final
        {
            EApplicationHostPhase Phase = EApplicationHostPhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        struct _APPLICATION_HOST_EXP ApplicationHostEvent final
        {
            EApplicationHostEventCode Code = EApplicationHostEventCode::Stopped;
            EApplicationHostState State = EApplicationHostState::Stopped;
            EApplicationHostStopReason StopReason = EApplicationHostStopReason::None;
            EApplicationHostPhase Phase = EApplicationHostPhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        using ApplicationHostEventHandler = std::function<void(const ApplicationHostEvent&)>;

        /*
        * @brief 应用宿主
        * @details
        *   ApplicationHost 是前端启动后最先连接的后台入口。
        *   它只负责应用上下文、产品清单、产品运行时生命周期和应用级 mailbox。
        */
        class _APPLICATION_HOST_EXP CApplicationHost
        {
        public:
            /*
            * @brief 构造应用宿主。
            * @details 默认会创建一个 icax.default 产品定义，调用方可在 Start 前 SetConfig 覆盖。
            */
            CApplicationHost();
            virtual ~CApplicationHost();

        public:
            /*
            * @brief 设置宿主配置。
            * @param [in] Config_ 新配置。
            * @throws std::logic_error 宿主正在运行或停止时抛出。
            */
            void SetConfig(IN const ApplicationHostConfig& Config_);

            /*
            * @brief 启动宿主工作线程。
            * @details Start 会等待工作线程完成 Load 并进入 Running，启动失败会重新抛出工作线程异常。
            */
            void Start();

            /*
            * @brief 停止宿主工作线程并卸载产品运行时。
            */
            void Stop();

            /*
            * @brief 判断宿主是否运行中。
            */
            bool IsRunning() const;

            /*
            * @brief 获取宿主状态。
            */
            EApplicationHostState GetState() const;

            /*
            * @brief 获取宿主当前阶段。
            */
            EApplicationHostPhase GetPhase() const;

            /*
            * @brief 获取最近一次停止原因。
            */
            EApplicationHostStopReason GetStopReason() const;

            /*
            * @brief 获取最近一次异常。
            */
            std::optional<ApplicationHostFault> GetLastFault() const;

            /*
            * @brief 订阅宿主事件。
            * @param [in] Handler_ 事件回调，不能为空。
            * @return 订阅 ID，可用于取消订阅。
            */
            uint64_t SubscribeEvent(IN ApplicationHostEventHandler Handler_);

            /*
            * @brief 取消宿主事件订阅。
            * @return true 表示移除了订阅。
            */
            bool UnsubscribeEvent(IN uint64_t nSubscriptionID_);

        private:
            /*
            * @brief 宿主工作线程入口。
            * @details 执行 Load -> MainLoop -> Unload，并把异常转换为 Faulted 状态和事件。
            */
            void WorkerMain();

            /*
            * @brief 加载应用上下文、注册表、服务和应用邮箱。
            */
            void Load();

            /*
            * @brief 宿主主循环。
            * @details 每帧只分发应用邮箱邮件；产品邮箱由各 ProductRuntime 自己的线程分发。
            */
            void MainLoop();

            /*
            * @brief 卸载产品运行时、应用邮箱、服务和上下文。
            */
            void Unload();

            /*
            * @brief 通知宿主事件。
            */
            void NotifyEvent(
                IN EApplicationHostEventCode Code_,
                IN const std::string& strMessage_ = {},
                IN std::exception_ptr pException_ = nullptr);
            /*
            * @brief 设置当前阶段。
            */
            void SetPhase(IN EApplicationHostPhase Phase_);

            /*
            * @brief 记录异常信息。
            */
            void RecordFault(
                IN EApplicationHostPhase Phase_,
                IN const std::string& strMessage_,
                IN std::exception_ptr pException_);
            /*
            * @brief 从 exception_ptr 提取错误文本。
            */
            static std::string GetExceptionMessage(IN std::exception_ptr pException_);

            /*
            * @brief 分发应用邮箱邮件。
            */
            void DispatchApplicationMails();

            /*
            * @brief 分配后端响应邮件 ID。
            */
            uint64_t AllocateBackendMailID();

            /*
            * @brief 加载应用设置。
            */
            iCAX::Data::PropertyBag LoadApplicationSettings() const;

            /*
            * @brief 创建应用上下文。
            */
            std::shared_ptr<iCAX::Application::CApplicationContext> CreateApplicationContext() const;

            /*
            * @brief 注册 ApplicationHost 内建命令。
            */
            void RegisterBuiltInApplicationCommands();

            /*
            * @brief 处理获取应用状态命令。
            */
            iCAX::Command::CCommandResponse HandleGetStateCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

            /*
            * @brief 处理列出产品命令。
            * @details 当前实现返回完整应用状态，其中包含产品定义和运行态信息。
            */
            iCAX::Command::CCommandResponse HandleListProductsCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

            /*
            * @brief 处理启动产品命令。
            * @param [in] Request_ Payload 可包含 productId；单产品时可省略。
            */
            iCAX::Command::CCommandResponse HandleStartProductCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

            /*
            * @brief 处理停止产品命令。
            * @param [in] Request_ Payload 必须包含 productId。
            */
            iCAX::Command::CCommandResponse HandleStopProductCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

            /*
            * @brief 处理识别项目文件产品命令。
            * @param [in] Request_ Payload 必须包含 projectPath。
            */
            iCAX::Command::CCommandResponse HandleResolveProjectFileCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

            /*
            * @brief 处理打开项目文件命令。
            * @param [in] Request_ Payload 必须包含 projectPath，可包含 catalogName/projectName。
            */
            iCAX::Command::CCommandResponse HandleOpenProjectFileCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_);

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
            * @brief 获取宿主支持的产品定义。
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
            * @brief 获取应用邮箱前端端点。
            * @details 前端启动后首先通过该邮局与 ApplicationHost 通信。
            */
            iCAX::Mail::CMailPostOffice GetApplicationFrontendPostOffice() const;

            /*
            * @brief 向应用级前端邮箱主动发送事件邮件。
            * @param [in] nTypeCode_ 事件类型码，使用 CommandRoute 的 64 位主/子编码。
            * @param [in] strPayloadText_ UTF-8 文本负载，通常是 VariantSerializer 文本。
            * @details
            *   主动事件邮件的 nOriginId 固定为 0，前端通过 FrontendMailbox.subscribe/subscribeAll 接收。
            */
            void SendFrontendEvent(IN uint64_t nTypeCode_, IN const std::string& strPayloadText_);

            /*
            * @brief 获取产品邮箱前端端点。
            */
            iCAX::Mail::CMailPostOffice GetProductFrontendPostOffice(IN const std::string& strProductID_) const;

            /*
            * @brief 获取项目邮箱前端端点。
            */
            iCAX::Mail::CMailPostOffice GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const;

            /*
            * @brief 获取应用通信通道 ID。
            */
            const iCAX::Data::uuid& GetApplicationChannelID() const;

            const iCAX::Application::IApplicationContext& GetApplicationContext() const
            {
                return *m_pApplicationContext;
            }

            iCAX::Command::CCommandRegistry& GetCommandRegistry() const
            {
                return *m_pCommandRegistry;
            }

        private:
            ApplicationHostConfig m_Config;
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StartupCondition;
            std::thread m_WorkThread;
            std::atomic_bool m_bStopRequested = false;
            bool m_bStartupCompleted = false;
            EApplicationHostState m_State = EApplicationHostState::Stopped;
            EApplicationHostStopReason m_StopReason = EApplicationHostStopReason::None;
            EApplicationHostPhase m_Phase = EApplicationHostPhase::None;
            std::optional<ApplicationHostFault> m_LastFault;
            std::exception_ptr m_pWorkerException = nullptr;
            mutable std::mutex m_EventMutex;
            std::map<uint64_t, ApplicationHostEventHandler> m_mapEventHandlers;
            uint64_t m_nNextEventSubscriptionID = 1;
            iCAX::Data::uuid m_ApplicationChannelID;
            std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry;
            std::unique_ptr<iCAX::Command::CCommandDispatcher> m_pCommandDispatcher;
            std::unique_ptr<iCAX::MailHandler::CMailCommandHandler> m_pMailCommandHandler;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pApplicationServiceProvider;
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> m_pMailChannelRegistry;
            mutable std::mutex m_ProductRuntimeMutex;
            std::condition_variable m_ProductRuntimeCondition;
            std::set<std::string> m_StartingProductIDs;
            std::set<std::string> m_StoppingProductIDs;
            std::map<std::string, std::shared_ptr<iCAX::Product::CProductRuntime>> m_ProductRuntimes;
        };
    }
}

