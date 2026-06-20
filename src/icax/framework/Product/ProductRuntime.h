#pragma once

#include "ProductExport.h"
#include "ProductDefinition.h"
#include "ProductData.h"
#include "ProductCommands.h"

#include "ApplicationContext/ApplicationContext.h"
#include "Behaviour/IBehaviourRegistry.h"
#include "Behaviour/BehaviourRegistrationCatalog.h"
#include "CommandHandler/CommandContext.h"
#include "CommandHandler/CommandDispatcher.h"
#include "CommandHandler/CommandMessage.h"
#include "CommandHandler/CommandRegistry.h"
#include "Data/PropertyBag.h"
#include "Data/Variant.h"
#include "Database/IMetaRegistry.h"
#include "Database/MetaRegistrationCatalog.h"
#include "Mailbox/MailPostOffice.h"
#include "Project/Project.h"
#include "Project/ProjectCatalog.h"
#include "Project/ProjectRuntime.h"
#include "Resources/ResourceLoaderRegistrationCatalog.h"
#include "Resources/ResourceLoaderRegistry.h"
#include "Services/IMailChannelService.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        /*
        * @brief 产品运行时
        * @details
        *   ProductRuntime 是产品级后台入口，拥有产品邮箱和多个 ProjectCatalog。
        *   Project 自己仍然拥有独立线程、Repository、ResourceLibrary、Universe 和项目邮箱。
        */
        class _PRODUCT_EXP CProductRuntime final : public std::enable_shared_from_this<CProductRuntime>
        {
        public:
            CProductRuntime(
                IN const CProductDefinition& Definition_,
                IN std::shared_ptr<iCAX::Application::CApplicationContext> pApplicationContext_,
                IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_,
                IN std::shared_ptr<iCAX::Services::CServiceProvider> pApplicationServiceProvider_,
                IN std::shared_ptr<iCAX::Database::IMetaRegistry> pApplicationMetaRegistry_,
                IN std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pApplicationBehaviourRegistry_,
                IN std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pApplicationResourceLoaderRegistry_,
                IN std::shared_ptr<IProductDataStore> pProductDataStore_ = nullptr);
            ~CProductRuntime();

            CProductRuntime(IN const CProductRuntime&) = delete;
            CProductRuntime& operator=(IN const CProductRuntime&) = delete;

        public:
            void Start();
            void Stop();
            bool IsStarted() const;

            const CProductDefinition& GetDefinition() const;
            CProductData GetProductData() const;
            const std::string& GetProductID() const;
            const iCAX::Data::uuid& GetProductMailID() const;
            iCAX::Mail::CMailPostOffice GetProductFrontendPostOffice() const;

            void DispatchProductMails();

            std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> GetProjectCatalogs() const;
            std::shared_ptr<iCAX::Project::CProjectCatalog> FindProjectCatalog(
                IN const iCAX::Data::uuid& CatalogID_) const;
            std::shared_ptr<iCAX::Project::CProjectCatalog> FindProjectCatalogByProjectID(
                IN const iCAX::Data::uuid& ProjectID_) const;
            std::shared_ptr<iCAX::Project::CProjectCatalog> OpenProjectCatalog(
                IN const std::string& strCatalogName_ = std::string(),
                IN const std::string& strCatalogPath_ = std::string(),
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());
            std::shared_ptr<iCAX::Project::CProject> OpenTransientProject(
                IN const iCAX::Data::uuid& CatalogID_,
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);
            bool CloseProjectCatalog(IN const iCAX::Data::uuid& CatalogID_);
            iCAX::Mail::CMailPostOffice GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const;

            iCAX::Data::Variant BuildProductStatePayload() const;
            iCAX::Data::Variant BuildProjectCatalogPayload(
                IN const std::shared_ptr<iCAX::Project::CProjectCatalog>& pCatalog_) const;

            iCAX::Command::CCommandRegistry& GetCommandRegistry() const;
            iCAX::Services::CServiceProvider& GetServiceProvider() const;
            iCAX::Command::ICommandContext& GetCommandContext();
            const iCAX::Command::ICommandContext& GetCommandContext() const;

        private:
            void EnsureStarted() const;
            void PrepareCommandContext();
            void PopulateCommandContext(
                IN OUT iCAX::Command::CCommandContext& Context_,
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_) const;
            std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> SnapshotProjectCatalogs() const;
            std::vector<std::shared_ptr<iCAX::Project::IProjectRuntime>> SnapshotProjectRuntimes() const;
            CProductData SnapshotProductData() const;
            std::shared_ptr<iCAX::Project::IProjectRuntime> FindProjectRuntime(
                IN const iCAX::Data::uuid& ProjectID_) const;
            void RegisterProjectRuntime(
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);
            std::shared_ptr<iCAX::Project::IProjectRuntime> RemoveProjectRuntime(
                IN const iCAX::Data::uuid& ProjectID_);
            void DispatchProjectMails(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);
            void StartProject(IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);
            uint64_t AllocateBackendMailID();
            void LoadProductData();
            void SaveProductData(IN const CProductData& ProductData_) const;
            void RecordRecentProject(
                IN const std::string& strProjectPath_,
                IN const std::string& strDisplayName_);
            void LoadProductModules();
            void RegisterBuiltInProductCommands();

            iCAX::Command::CCommandResponse HandleGetStateCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleListProjectCatalogsCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleOpenProjectCatalogCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);
            iCAX::Command::CCommandResponse HandleCloseProjectCatalogCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);

        private:
            CProductDefinition m_Definition;
            CProductData m_ProductData;
            iCAX::Data::uuid m_ProductMailID;
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSettings;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pApplicationServiceProvider;
            std::shared_ptr<iCAX::Services::IMailChannelService> m_pMailChannelService;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pApplicationMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pApplicationBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pApplicationResourceLoaderRegistry;
            std::shared_ptr<IProductDataStore> m_pProductDataStore;
            std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry;
            std::unique_ptr<iCAX::Command::CCommandDispatcher> m_pCommandDispatcher;
            iCAX::Command::CCommandContext m_CommandContext;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            mutable std::mutex m_RuntimeMutex;
            bool m_bStarted = false;
            bool m_bModulesLoaded = false;
            std::vector<std::string> m_LoadedModulePaths;
            mutable std::mutex m_ProductDataMutex;
            mutable std::mutex m_ProjectCatalogMutex;
            std::map<iCAX::Data::uuid, std::shared_ptr<iCAX::Project::CProjectCatalog>> m_ProjectCatalogs;
            mutable std::mutex m_ProjectRuntimeMutex;
            std::map<iCAX::Data::uuid, std::shared_ptr<iCAX::Project::IProjectRuntime>> m_ProjectRuntimes;
        };
    }
}
