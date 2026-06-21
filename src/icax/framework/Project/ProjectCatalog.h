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
        *   Catalog 保存一个主项目和多个临时项目，并为这些项目注入相同的产品级注册表/服务。
        */
        struct _PROJECT_EXP CProjectCatalogCreateInfo final
        {
            iCAX::Data::uuid CatalogID; //!< Catalog ID，nil 时自动生成。
            std::string CatalogName; //!< Catalog 名称。
            std::string CatalogPath; //!< Catalog 路径。
            std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings; //!< 应用设置。
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry; //!< 产品级元数据注册表。
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry; //!< 产品级行为注册表。
            std::shared_ptr<iCAX::Services::IMailChannelService> pMailChannelService; //!< 邮件通道服务。
            std::function<std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>()> ResourceLoaderRegistryFactory; //!< 项目级资源加载器注册表工厂。
        };

        /*
        * @brief 项目目录。
        * @details
        *   Catalog 是一个工程打开上下文，可以包含一个主项目和若干临时项目。
        *   它不启动项目线程；ProductRuntime 会为项目创建 ProjectRuntime 并启动。
        */
        class _PROJECT_EXP CProjectCatalog final
        {
        public:
            /*
            * @brief 构造默认 catalog。
            */
            CProjectCatalog();

            /*
            * @brief 构造只带应用设置的 catalog。
            */
            explicit CProjectCatalog(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);

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
            * @brief 更新应用设置引用。
            */
            void SetApplicationSettings(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);

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
            * @brief 打开临时项目。
            */
            std::shared_ptr<CProject> OpenTransientProject(
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string(),
                IN const std::string& strStartupComponent_ = std::string());
            /*
            * @brief 使用完整创建参数打开临时项目。
            */
            std::shared_ptr<CProject> OpenTransientProject(IN const CProjectCreateInfo& CreateInfo_);

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
            * @brief 关闭全部临时项目。
            */
            void CloseTransientProjects();

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
            * @brief 获取临时项目快照。
            */
            std::vector<std::shared_ptr<CProject>> GetTransientProjects() const;

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

            /*
            * @brief 临时项目数量。
            */
            size_t TransientCount() const;

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
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSettings;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
            std::shared_ptr<iCAX::Services::IMailChannelService> m_pMailChannelService;
            std::function<std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>()> m_ResourceLoaderRegistryFactory;
            std::map<iCAX::Data::uuid, std::shared_ptr<CProject>> m_Projects;
            iCAX::Data::uuid m_MainProjectID;
        };
    }
}
