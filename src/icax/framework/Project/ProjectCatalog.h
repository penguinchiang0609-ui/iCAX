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
        struct _PROJECT_EXP CProjectCatalogCreateInfo final
        {
            iCAX::Data::uuid CatalogID;
            std::string CatalogName;
            std::string CatalogPath;
            std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings;
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry;
            std::shared_ptr<iCAX::Services::IMailChannelService> pMailChannelService;
            std::function<std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry>()> ResourceLoaderRegistryFactory;
        };

        class _PROJECT_EXP CProjectCatalog final
        {
        public:
            CProjectCatalog();
            explicit CProjectCatalog(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);
            explicit CProjectCatalog(IN const CProjectCatalogCreateInfo& CreateInfo_);
            ~CProjectCatalog();

            CProjectCatalog(IN const CProjectCatalog&) = delete;
            CProjectCatalog& operator=(IN const CProjectCatalog&) = delete;

        public:
            const iCAX::Data::uuid& GetCatalogID() const;
            std::string GetCatalogName() const;
            void SetCatalogName(IN const std::string& strCatalogName_);
            std::string GetCatalogPath() const;
            void SetCatalogPath(IN const std::string& strCatalogPath_);
            void SetApplicationSettings(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);

            std::shared_ptr<CProject> OpenMainProject(
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string(),
                IN const std::string& strStartupComponent_ = std::string());
            std::shared_ptr<CProject> OpenMainProject(IN const CProjectCreateInfo& CreateInfo_);
            std::shared_ptr<CProject> OpenTransientProject(
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string(),
                IN const std::string& strStartupComponent_ = std::string());
            std::shared_ptr<CProject> OpenTransientProject(IN const CProjectCreateInfo& CreateInfo_);
            bool CloseMainProject();
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);
            void CloseTransientProjects();
            void CloseAll();

            std::shared_ptr<CProject> FindProject(IN const iCAX::Data::uuid& ProjectID_) const;
            std::shared_ptr<CProject> GetMainProject() const;
            std::vector<std::shared_ptr<CProject>> GetProjects() const;
            std::vector<std::shared_ptr<CProject>> GetTransientProjects() const;
            std::vector<iCAX::Data::uuid> GetProjectIDs() const;
            bool HasMainProject() const;
            size_t Count() const;
            size_t TransientCount() const;

        private:
            std::shared_ptr<CProject> CreateProject(IN CProjectCreateInfo CreateInfo_);
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
