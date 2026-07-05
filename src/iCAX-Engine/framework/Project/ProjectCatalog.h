#pragma once

#include "Project.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        /*
        * @brief ProjectCatalog 创建参数。
        * @details
        *   Catalog 保存一个主项目，并为该项目注入产品级注册表/服务。
        *   临时预览、局部编辑和仿真等隔离现场应由 Project 内部的子 Scene 承载。
        */
        struct _PROJECT_EXP CProjectCatalogCreateInfo final
        {
            iCAX::Data::uuid CatalogID; //!< Catalog ID，nil 时自动生成。
            std::string CatalogName; //!< Catalog 名称。
            std::string CatalogPath; //!< Catalog 路径。
            std::shared_ptr<iCAX::Application::IApplicationContext> pApplicationContext; //!< 应用上下文。
            std::shared_ptr<iCAX::Product::IProductContext> pProductContext; //!< 产品上下文。
            std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider; //!< 服务容器。
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry; //!< 产品级元数据注册表。
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry; //!< 产品级行为注册表。
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> pMailChannelRegistry; //!< 邮件通道注册表。
            bool bEnablePDOHub = false; //!< Catalog 默认传给每个项目主 Scene 的动态 PDOHub 开关。
            iCAX::PDO::CPDOHubCreateInfo PDOHubCreateInfo; //!< Catalog 默认传给每个项目主 Scene 的动态 PDOHub 创建参数。
            std::function<std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>()> ResourceLoaderRegistryFactory; //!< Scene 级资源加载器注册表工厂。
        };

        /*
        * @brief 项目目录。
        * @details
        *   Catalog 是一个工程打开上下文，只包含一个主 Project。
        *   它不启动 Scene 线程；ProductRuntime 会登记项目运行时句柄，并启动项目的主 Scene。
        */
        class _PROJECT_EXP CProjectCatalog final
        {
        public:
            /*
            * @brief 根据创建参数构造 catalog。
            */
            explicit CProjectCatalog(IN const CProjectCatalogCreateInfo& CreateInfo_);
            ~CProjectCatalog();

            CProjectCatalog(IN const CProjectCatalog&) = delete;
            CProjectCatalog& operator=(IN const CProjectCatalog&) = delete;

        public:
            /*
            * @brief 获取 catalog ID。
            */
            const iCAX::Data::uuid& GetCatalogID() const;

            /*
            * @brief 获取 catalog 名称。
            */
            std::string GetCatalogName() const;

            /*
            * @brief 设置 catalog 名称。
            */
            void SetCatalogName(IN const std::string& strCatalogName_);

            /*
            * @brief 获取 catalog 路径。
            */
            std::string GetCatalogPath() const;

            /*
            * @brief 设置 catalog 路径。
            */
            void SetCatalogPath(IN const std::string& strCatalogPath_);

            /*
            * @brief 打开主项目。
            * @return 新创建的主项目。
            * @throws std::logic_error 已存在主项目时抛出。
            */
            std::shared_ptr<CProject> OpenMainProject(
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string(),
                IN const std::string& strStartupComponent_ = std::string());
            /*
            * @brief 使用完整创建参数打开主项目。
            */
            std::shared_ptr<CProject> OpenMainProject(IN const CProjectCreateInfo& CreateInfo_);

            /*
            * @brief 关闭主项目。
            * @return true 表示存在主项目并已关闭。
            */
            bool CloseMainProject();

            /*
            * @brief 关闭指定项目。
            */
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);

            /*
            * @brief 关闭 catalog 内全部项目。
            */
            void CloseAll();

            /*
            * @brief 查找项目。
            */
            std::shared_ptr<CProject> FindProject(IN const iCAX::Data::uuid& ProjectID_) const;

            /*
            * @brief 获取主项目。
            */
            std::shared_ptr<CProject> GetMainProject() const;

            /*
            * @brief 获取全部项目快照。
            */
            std::vector<std::shared_ptr<CProject>> GetProjects() const;

            /*
            * @brief 获取全部项目 ID。
            */
            std::vector<iCAX::Data::uuid> GetProjectIDs() const;

            /*
            * @brief 是否存在主项目。
            */
            bool HasMainProject() const;

            /*
            * @brief 项目总数。
            */
            size_t Count() const;

        private:
            /*
            * @brief 创建并登记项目。
            * @param [in] CreateInfo_ 创建参数；缺失注册表会从 Catalog 默认值补齐。
            * @return 新项目。
            */
            std::shared_ptr<CProject> CreateProject(IN CProjectCreateInfo CreateInfo_);

            /*
            * @brief 获取项目快照。
            */
            std::vector<std::shared_ptr<CProject>> SnapshotProjects() const;

        private:
            mutable std::mutex m_Mutex;
            iCAX::Data::uuid m_CatalogID;
            std::string m_CatalogName;
            std::string m_CatalogPath;
            std::shared_ptr<iCAX::Application::IApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Product::IProductContext> m_pProductContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> m_pMailChannelRegistry;
            bool m_bEnablePDOHub = false;
            iCAX::PDO::CPDOHubCreateInfo m_PDOHubCreateInfo;
            std::function<std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>()> m_ResourceLoaderRegistryFactory;
            std::map<iCAX::Data::uuid, std::shared_ptr<CProject>> m_Projects;
            iCAX::Data::uuid m_MainProjectID;
        };
    }
}

