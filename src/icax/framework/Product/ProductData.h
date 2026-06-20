#pragma once

#include "ProductExport.h"
#include "Data/PropertyBag.h"

#include <string>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        struct _PRODUCT_EXP CRecentProjectItem final
        {
            std::string ProjectPath;
            std::string DisplayName;
            std::string LastOpenedTime;
        };

        struct _PRODUCT_EXP CProductData final
        {
            std::vector<CRecentProjectItem> RecentProjects;
            iCAX::Data::PropertyBag UserSettings;
        };

        class _PRODUCT_EXP IProductDataStore
        {
        public:
            IProductDataStore() = default;
            virtual ~IProductDataStore() = default;

            IProductDataStore(IN const IProductDataStore&) = delete;
            IProductDataStore& operator=(IN const IProductDataStore&) = delete;

        public:
            virtual CProductData Load(IN const std::string& strProductID_) const = 0;
            virtual void Save(IN const std::string& strProductID_, IN const CProductData& Data_) const = 0;
        };

        class _PRODUCT_EXP CFileProductDataStore final : public IProductDataStore
        {
        public:
            explicit CFileProductDataStore(IN std::string strProductDataRoot_);
            ~CFileProductDataStore() override = default;

        public:
            CProductData Load(IN const std::string& strProductID_) const override;
            void Save(IN const std::string& strProductID_, IN const CProductData& Data_) const override;
            std::string GetProductDataPath(IN const std::string& strProductID_) const;

        private:
            std::string m_strProductDataRoot;
        };
    }
}
