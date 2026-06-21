#pragma once

#include "ProductExport.h"
#include "Data/PropertyBag.h"

#include <string>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        /*
        * @brief 最近打开项目记录。
        */
        struct _PRODUCT_EXP CRecentProjectItem final
        {
            std::string ProjectPath; //!< 项目路径。
            std::string DisplayName; //!< 展示名称。
            std::string LastOpenedTime; //!< 最近打开时间，UTC ISO-8601 文本。
        };

        /*
        * @brief 产品数据。
        * @details
        *   ProductData 是产品级可变数据，例如最近项目和用户偏好。
        *   它不是 ProductDefinition；Definition 描述产品能力，Data 描述用户使用状态。
        */
        struct _PRODUCT_EXP CProductData final
        {
            std::vector<CRecentProjectItem> RecentProjects; //!< 最近打开项目列表。
            iCAX::Data::PropertyBag UserSettings; //!< 产品级用户设置。
        };

        /*
        * @brief 产品数据存储接口。
        */
        class _PRODUCT_EXP IProductDataStore
        {
        public:
            IProductDataStore() = default;
            virtual ~IProductDataStore() = default;

            IProductDataStore(IN const IProductDataStore&) = delete;
            IProductDataStore& operator=(IN const IProductDataStore&) = delete;

        public:
            /*
            * @brief 加载产品数据。
            * @param [in] strProductID_ 产品 ID。
            * @return 产品数据；不存在时返回空数据。
            */
            virtual CProductData Load(IN const std::string& strProductID_) const = 0;

            /*
            * @brief 保存产品数据。
            * @param [in] strProductID_ 产品 ID。
            * @param [in] Data_ 待保存数据。
            */
            virtual void Save(IN const std::string& strProductID_, IN const CProductData& Data_) const = 0;
        };

        /*
        * @brief 文件型产品数据存储。
        * @details 每个产品写入独立目录：{root}/{ProductID}/Product.Data。
        */
        class _PRODUCT_EXP CFileProductDataStore final : public IProductDataStore
        {
        public:
            /*
            * @brief 构造文件型产品数据存储。
            * @param [in] strProductDataRoot_ 产品数据根目录；为空时使用 Setting/Products。
            */
            explicit CFileProductDataStore(IN std::string strProductDataRoot_);
            ~CFileProductDataStore() override = default;

        public:
            /*
            * @brief 从文件加载产品数据。
            */
            CProductData Load(IN const std::string& strProductID_) const override;

            /*
            * @brief 保存产品数据到文件。
            */
            void Save(IN const std::string& strProductID_, IN const CProductData& Data_) const override;

            /*
            * @brief 获取产品数据文件路径。
            * @return {root}/{ProductID}/Product.Data。
            */
            std::string GetProductDataPath(IN const std::string& strProductID_) const;

        private:
            std::string m_strProductDataRoot;
        };
    }
}
