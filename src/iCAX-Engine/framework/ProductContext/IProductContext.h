#pragma once

#include "ProductContextExport.h"
#include "Data/uuid.h"
#include "Mailbox/MailPostOffice.h"
#include "ProductData.h"
#include "ProductDefinition.h"

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;
    }

    namespace Interaction
    {
        class CFacadeRegistry;
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
            * @brief 获取产品级设置快照。
            * @details
            *   产品级设置与产品业务相关，但不跟随项目文件保存。
            *   默认实现从 GetProductData().Settings 返回快照。
            */
            virtual iCAX::Data::PropertyBag GetSettings() const;

            /*
            * @brief 替换并保存产品级设置。
            * @param [in] Settings_ 新的产品级用户设置。
            * @details
            *   只有具备持久化能力的 ProductContext 才应重写该方法。
            */
            virtual void ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_);

            /*
            * @brief 获取产品级通信通道 ID。
            */
            virtual const iCAX::Data::uuid& GetProductChannelID() const;

            /*
            * @brief 获取后端视角产品邮局。
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice() const;

            /*
            * @brief 获取前端视角产品邮局。
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const;

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
            virtual iCAX::Interaction::CFacadeRegistry& GetFacadeRegistry() const = 0;
        };
    }
}
