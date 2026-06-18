#pragma once

#include "ProductDefinition.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class _PROJECT_EXP CProductCatalog final
        {
        public:
            CProductCatalog() = default;
            ~CProductCatalog() = default;

            CProductCatalog(IN const CProductCatalog&) = delete;
            CProductCatalog& operator=(IN const CProductCatalog&) = delete;

        public:
            bool Register(IN const CProductDefinition& Definition_);
            void Set(IN const CProductDefinition& Definition_);
            bool Has(IN const std::string& strProductID_) const;
            std::shared_ptr<const CProductDefinition> Find(IN const std::string& strProductID_) const;
            CProductDefinition Require(IN const std::string& strProductID_) const;
            std::vector<CProductDefinition> GetDefinitions() const;
            std::vector<std::string> GetProductIDs() const;
            size_t Count() const;
            void Clear();

            CProductDefinition LoadManifestFile(IN const std::string& strPath_) const;
            std::vector<CProductDefinition> LoadManifestDirectory(IN const std::string& strDirectory_) const;

        private:
            std::map<std::string, CProductDefinition> m_Products;
        };
    }
}
