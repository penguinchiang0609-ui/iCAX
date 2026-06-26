#pragma once

#include "ProductContextExport.h"
#include "ProductData.h"
#include "ProductDefinition.h"

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;
    }

    namespace Command
    {
        class CCommandRegistry;
    }

    namespace Database
    {
        class IMetaRegistry;
    }

    namespace Resource
    {
        class CResourceLoaderRegistry;
    }

    namespace Services
    {
        class CServiceProvider;
    }

    namespace Product
    {
        /*
        * @brief 产品级运行上下文。
        * @details
        *   ProductContext 表达当前产品的定义、产品数据、产品级注册表和服务入口。
        *   它不拥有项目数据；项目数据通过 IProjectContext 访问。
        */
        class _PRODUCT_CONTEXT_EXP IProductContext
        {
        public:
            IProductContext();
            virtual ~IProductContext();

            IProductContext(const IProductContext&) = delete;
            IProductContext& operator=(const IProductContext&) = delete;

        public:
            /*
            * @brief 获取产品静态定义。
            */
            virtual const CProductDefinition& GetDefinition() const = 0;

            /*
            * @brief 获取产品 ID。
            */
            virtual const std::string& GetProductID() const = 0;

            /*
            * @brief 获取产品数据快照。
            */
            virtual CProductData GetProductData() const = 0;

            /*
            * @brief 获取当前产品可用的服务容器。
            */
            virtual iCAX::Services::CServiceProvider& GetServiceProvider() const = 0;

            /*
            * @brief 获取产品级组件元数据注册表。
            */
            virtual iCAX::Database::IMetaRegistry& GetMetaRegistry() const = 0;

            /*
            * @brief 获取产品级 Behaviour 注册表。
            */
            virtual iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const = 0;

            /*
            * @brief 获取产品级资源加载器注册表。
            */
            virtual iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const = 0;

            /*
            * @brief 获取产品命令注册表。
            */
            virtual iCAX::Command::CCommandRegistry& GetCommandRegistry() const = 0;
        };
    }
}
