#pragma once

#include "ProductExport.h"
#include "ProductContext/ProductDefinition.h"

#include <string>
#include <vector>

namespace iCAX
{
    namespace Product
    {
        /*
        * @brief 产品 manifest。
        * @details manifest 是产品的统一入口。backend 模块和 webpage 入口都从同一份文件读取。
        */
        struct _PRODUCT_EXP CProductManifest final
        {
            std::string Schema; //!< manifest schema 名称。
            uint32_t nSchemaVersion = 1; //!< manifest schema 版本。
            std::string ManifestPath; //!< manifest 文件路径。
            iCAX::Product::CProductDefinition Definition; //!< 转换后的 backend 产品定义。
        };

        /*
        * @brief 加载单个产品 manifest。
        * @param [in] strManifestPath_ product.manifest.json 路径。
        * @return 产品 manifest。
        * @throws std::runtime_error manifest 不合法时抛出。
        */
        _PRODUCT_EXP CProductManifest LoadProductManifest(IN const std::string& strManifestPath_);

        /*
        * @brief 扫描产品根目录并加载所有产品定义。
        * @param [in] strProductRoot_ 产品根目录，通常是 src/apps。
        * @return 产品定义列表。
        * @details 只扫描产品根目录下一层子目录的 product.manifest.json。
        */
        _PRODUCT_EXP std::vector<iCAX::Product::CProductDefinition> LoadProductDefinitions(
            IN const std::string& strProductRoot_);
    }
}
