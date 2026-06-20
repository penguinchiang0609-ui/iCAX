#pragma once
#include "ApplicationHostExport.h"
#include "ApplicationContext/ApplicationContext.h"
#include "ApplicationHostCommands.h"
#include "ApplicationHostConfig.h"
#include "ProductFileResolver.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "CommandHandler/CommandContext.h"
#include "CommandHandler/CommandDispatcher.h"
#include "CommandHandler/CommandMessage.h"
#include "CommandHandler/CommandRegistry.h"
#include "CommandHandler/ICommandContext.h"
#include "Data/PropertyBag.h"
#include "Data/Variant.h"
#include "Database/IMetaRegistry.h"
#include "Mailbox/MailPostOffice.h"
#include "Product/ProductRuntime.h"
#include "Resources/ResourceLoaderRegistry.h"
#include "Services/IMailChannelService.h"
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
            CApplicationHost();
            virtual ~CApplicationHost();

        public:
            void SetConfig(IN const ApplicationHostConfig& Config_);
            void Start();
            void Stop();

            bool IsRunning() const;
            EApplicationHostState GetState() const;
            EApplicationHostPhase GetPhase() const;
            EApplicationHostStopReason GetStopReason() const;
            std::optional<ApplicationHostFault> GetLastFault() const;

            uint64_t SubscribeEvent(IN ApplicationHostEventHandler Handler_);
            bool UnsubscribeEvent(IN uint64_t nSubscriptionID_);

        private:
            void WorkerMain();
            void Load();
            void MainLoop();
            void Unload();

            void NotifyEvent(
                IN EApplicationHostEventCode Code_,
                IN const std::string& strMessage_ = {},
                IN std::exception_ptr pException_ = nullptr);
            void SetPhase(IN EApplicationHostPhase Phase_);
            void RecordFault(
                IN EApplicationHostPhase Phase_,
                IN const std::string& strMessage_,
                IN std::exception_ptr pException_);
            static std::string GetExceptionMessage(IN std::exception_ptr pException_);

            void PrepareCommandContext();
            void PopulateApplicationCommandContext(IN OUT iCAX::Command::CCommandContext& Context_) const;
            void DispatchApplicationMails();
            void DispatchProductMails();
            uint64_t AllocateBackendMailID();

            iCAX::Application::CApplicationSettings LoadApplicationSettings() const;
            std::shared_ptr<iCAX::Application::CApplicationContext> CreateApplicationContext() const;
            void RegisterBuiltInApplicationCommands();

            iCAX::Command::CCommandResponse HandleGetStateCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleListProductsCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleStartProductCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleStopProductCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleResolveProjectFileCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleOpenProjectFileCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);

            iCAX::Data::Variant BuildApplicationStatePayload() const;
            iCAX::Data::Variant BuildProductFileResolvePayload(IN const CProductFileResolveResult& Result_) const;
            iCAX::Data::Variant BuildProductPayload(
                IN const iCAX::Product::CProductDefinition& Definition_,
                IN const std::shared_ptr<iCAX::Product::CProductRuntime>& pRuntime_) const;
            iCAX::Product::CProductDefinition FindProductDefinition(IN const std::string& strProductID_) const;
            std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> SnapshotProductRuntimes() const;

        public:
            std::vector<iCAX::Product::CProductDefinition> GetProductDefinitions() const;
            std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> GetProductRuntimes() const;
            std::shared_ptr<iCAX::Product::CProductRuntime> FindProductRuntime(IN const std::string& strProductID_) const;
            std::shared_ptr<iCAX::Product::CProductRuntime> StartProduct(IN const std::string& strProductID_ = std::string());
            bool StopProduct(IN const std::string& strProductID_);
            CProductFileResolveResult ResolveProjectFileProduct(IN const std::string& strProjectPath_) const;
            std::shared_ptr<iCAX::Project::CProjectCatalog> OpenProjectFile(
                IN const std::string& strProjectPath_,
                IN const std::string& strCatalogName_ = std::string(),
                IN const std::string& strProjectName_ = std::string());

            iCAX::Mail::CMailPostOffice GetApplicationFrontendPostOffice() const;
            iCAX::Mail::CMailPostOffice GetProductFrontendPostOffice(IN const std::string& strProductID_) const;
            iCAX::Mail::CMailPostOffice GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const;
            const iCAX::Data::uuid& GetApplicationMailID() const;

            const iCAX::Application::IApplicationContext& GetApplicationContext() const
            {
                return *m_pApplicationContext;
            }

            iCAX::Command::CCommandRegistry& GetCommandRegistry() const
            {
                return *m_pCommandRegistry;
            }

            iCAX::Command::ICommandContext& GetCommandContext()
            {
                return m_CommandContext;
            }

            const iCAX::Command::ICommandContext& GetCommandContext() const
            {
                return m_CommandContext;
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
            iCAX::Data::uuid m_ApplicationMailID;
            std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry;
            std::unique_ptr<iCAX::Command::CCommandDispatcher> m_pCommandDispatcher;
            iCAX::Command::CCommandContext m_CommandContext;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pApplicationServiceProvider;
            std::shared_ptr<iCAX::Services::IMailChannelService> m_pMailChannelService;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pApplicationMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pApplicationBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pApplicationResourceLoaderRegistry;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSetting;
            mutable std::mutex m_ProductRuntimeMutex;
            std::map<std::string, std::shared_ptr<iCAX::Product::CProductRuntime>> m_ProductRuntimes;
        };
    }
}
