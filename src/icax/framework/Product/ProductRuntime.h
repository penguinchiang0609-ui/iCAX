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
            /*
            * @brief 构造产品运行时。
            * @param [in] Definition_ 产品静态定义。
            * @param [in] pApplicationContext_ 应用上下文，不能为空。
            * @param [in] pApplicationSettings_ 应用设置属性包，可为空。
            * @param [in] pApplicationServiceProvider_ 应用级服务容器，不能为空。
            * @param [in] pProductDataStore_ 产品数据存储，可为空；为空时使用默认文件存储。
            */
            CProductRuntime(
                IN const CProductDefinition& Definition_,
                IN std::shared_ptr<iCAX::Application::CApplicationContext> pApplicationContext_,
                IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_,
                IN std::shared_ptr<iCAX::Services::CServiceProvider> pApplicationServiceProvider_,
                IN std::shared_ptr<IProductDataStore> pProductDataStore_ = nullptr);
            ~CProductRuntime();

            CProductRuntime(IN const CProductRuntime&) = delete;
            CProductRuntime& operator=(IN const CProductRuntime&) = delete;

        public:
            /*
            * @brief 启动产品运行时。
            * @details
            *   加载产品数据和模块，按模块路径回放组件/行为/服务/资源/命令注册，并创建产品邮箱通道。
            *   重复调用无副作用。
            */
            void Start();

            /*
            * @brief 停止产品运行时。
            * @details 会关闭所有项目 runtime/catalog，移除产品邮箱，并清空命令上下文。
            */
            void Stop();

            /*
            * @brief 判断产品是否已启动。
            */
            bool IsStarted() const;

            /*
            * @brief 获取产品定义。
            */
            const CProductDefinition& GetDefinition() const;

            /*
            * @brief 获取产品数据快照。
            */
            CProductData GetProductData() const;

            /*
            * @brief 获取产品 ID。
            */
            const std::string& GetProductID() const;

            /*
            * @brief 获取产品邮箱通道 ID。
            */
            const iCAX::Data::uuid& GetProductMailID() const;

            /*
            * @brief 获取前端视角的产品邮局。
            * @return 产品邮箱的前端端点。
            * @throws std::logic_error 产品未启动时抛出。
            */
            iCAX::Mail::CMailPostOffice GetProductFrontendPostOffice() const;

            /*
            * @brief 分发产品邮箱中的邮件。
            * @details 由 ApplicationHost 主循环调用；产品命令上下文不包含具体 ProjectRuntime。
            */
            void DispatchProductMails();

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
            * @details 会创建 main project、project runtime，启动项目线程，并记录最近项目。
            */
            std::shared_ptr<iCAX::Project::CProjectCatalog> OpenProjectCatalog(
                IN const std::string& strCatalogName_ = std::string(),
                IN const std::string& strCatalogPath_ = std::string(),
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());
            /*
            * @brief 在指定 catalog 中打开临时项目。
            * @param [in] CatalogID_ catalog ID。
            * @return 新打开的临时项目。
            */
            std::shared_ptr<iCAX::Project::CProject> OpenTransientProject(
                IN const iCAX::Data::uuid& CatalogID_,
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
            * @brief 获取指定项目的前端邮局。
            * @return 项目邮箱的前端端点。
            * @throws std::runtime_error 项目不存在时抛出。
            */
            iCAX::Mail::CMailPostOffice GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const;

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
            * @brief 获取产品命令注册表。
            */
            iCAX::Command::CCommandRegistry& GetCommandRegistry() const;

            /*
            * @brief 获取应用级服务容器。
            * @details 当前服务容器在 Application 范围共享。
            */
            iCAX::Services::CServiceProvider& GetServiceProvider() const;

            /*
            * @brief 获取产品命令上下文。
            */
            iCAX::Command::ICommandContext& GetCommandContext();
            const iCAX::Command::ICommandContext& GetCommandContext() const;

        private:
            /*
            * @brief 确保产品已启动。
            * @throws std::logic_error 未启动时抛出。
            */
            void EnsureStarted() const;

            /*
            * @brief 准备产品级命令上下文。
            */
            void PrepareCommandContext();

            /*
            * @brief 填充命令上下文。
            * @param [in,out] Context_ 待填充上下文。
            * @param [in] pProjectRuntime_ 项目运行时；为空表示产品级命令。
            * @details
            *   产品级上下文只包含 ProductRuntime 和产品/应用依赖；
            *   项目级上下文会额外注入 Project、Repository、Universe 和 ResourceLibrary。
            */
            void PopulateCommandContext(
                IN OUT iCAX::Command::CCommandContext& Context_,
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_) const;
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
            * @brief 创建项目级资源加载器注册表。
            * @details 根据产品已加载模块路径回放 loader，保证每个项目有独立 Registry。
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
            * @brief 分发指定项目或产品邮箱中的邮件。
            * @param [in] PostOffice_ 后端视角邮局。
            * @param [in] pProjectRuntime_ 项目运行时；为空表示产品邮箱。
            */
            void DispatchProjectMails(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);
            /*
            * @brief 启动项目 runtime 并接入项目帧回调。
            */
            void StartProject(IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_);

            /*
            * @brief 分配后端响应邮件 ID。
            */
            uint64_t AllocateBackendMailID();

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
            * @brief 注册产品内建命令。
            */
            void RegisterBuiltInProductCommands();

            /*
            * @brief 处理获取产品状态命令。
            * @return 包含产品状态的命令响应。
            */
            iCAX::Command::CCommandResponse HandleGetStateCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);

            /*
            * @brief 处理列出项目 catalog 命令。
            * @return 当前实现返回完整产品状态，其中包含 catalog 列表。
            */
            iCAX::Command::CCommandResponse HandleListProjectCatalogsCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);

            /*
            * @brief 处理打开项目 catalog 命令。
            * @param [in] Request_ Payload 可包含 catalogName/catalogPath/projectName/projectPath。
            * @return 打开的 catalog 状态和产品状态。
            */
            iCAX::Command::CCommandResponse HandleOpenProjectCatalogCommand(
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_);

            /*
            * @brief 处理关闭项目 catalog 命令。
            * @param [in] Request_ Payload 必须包含 catalogId。
            * @return 关闭后的产品状态。
            */
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
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pProductMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pProductBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pProductResourceLoaderRegistry;
            std::shared_ptr<IProductDataStore> m_pProductDataStore;
            std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry;
            std::unique_ptr<iCAX::Command::CCommandDispatcher> m_pCommandDispatcher;
            iCAX::Command::CCommandContext m_CommandContext;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            mutable std::mutex m_RuntimeMutex;
            bool m_bStarted = false;
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
