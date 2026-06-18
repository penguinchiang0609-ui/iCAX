#pragma once

#include "ProductCatalog.h"
#include "ProjectSession.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class _PROJECT_EXP CProjectManager final
        {
        public:
            CProjectManager();
            explicit CProjectManager(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);
            ~CProjectManager();

            CProjectManager(IN const CProjectManager&) = delete;
            CProjectManager& operator=(IN const CProjectManager&) = delete;

        public:
            CProductCatalog& Products();
            const CProductCatalog& Products() const;
            void SetApplicationSettings(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_);

            std::shared_ptr<CProjectSession> OpenProject(
                IN const std::string& strProductID_,
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());
            std::shared_ptr<CProjectSession> OpenProject(IN const CProjectSessionCreateInfo& CreateInfo_);
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);
            void CloseAll();

            std::shared_ptr<CProjectSession> FindProject(IN const iCAX::Data::uuid& ProjectID_) const;
            std::shared_ptr<CProjectSession> GetActiveProject() const;
            bool SetActiveProject(IN const iCAX::Data::uuid& ProjectID_);
            std::vector<std::shared_ptr<CProjectSession>> GetProjects() const;
            std::vector<iCAX::Data::uuid> GetProjectIDs() const;
            size_t Count() const;

        private:
            std::vector<std::shared_ptr<CProjectSession>> SnapshotProjects() const;

        private:
            mutable std::mutex m_Mutex;
            CProductCatalog m_ProductCatalog;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSettings;
            std::map<iCAX::Data::uuid, std::shared_ptr<CProjectSession>> m_Projects;
            iCAX::Data::uuid m_ActiveProjectID;
        };
    }
}
