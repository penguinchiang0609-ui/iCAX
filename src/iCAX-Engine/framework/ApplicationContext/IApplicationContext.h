#pragma once

#include "ApplicationDescriptor.h"
#include "ApplicationPaths.h"
#include "Data/PropertyBag.h"

namespace iCAX::Services
{
    class CServiceProvider;
}

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用上下文接口。
        * @details
        *   ApplicationContext 管理应用作用域内的描述、路径、配置数据和服务环境。
        *   ApplicationRuntime 管理它的线程与生命周期；产品、项目、场景和普通 Facade
        *   只能通过 const 接口读取，修改必须回到 ApplicationRuntime 的应用级 Facade。
        */
        class _APPLICATION_CONTEXT_EXP IApplicationContext
        {
        public:
            IApplicationContext() = default;
            virtual ~IApplicationContext() = default;

            IApplicationContext(IN const IApplicationContext&) = delete;
            IApplicationContext& operator=(IN const IApplicationContext&) = delete;

        public:
            /*
            * @brief 获取应用描述。
            */
            virtual const CApplicationDescriptor& GetDescriptor() const = 0;

            /*
            * @brief 获取应用目录集合。
            */
            virtual const CApplicationPaths& GetPaths() const = 0;

            /*
            * @brief 获取应用设置副本。
            * @return 当前应用设置副本。
            */
            virtual iCAX::Data::PropertyBag GetSettings() const = 0;

            /*
            * @brief 获取应用作用域服务环境的只读入口。
            * @details 只能取得已由 ApplicationRuntime 初始化的 const 服务实例；
            *   不能触发构造，也不能修改注册关系或卸载服务。
            */
            virtual const iCAX::Services::CServiceProvider& Services() const = 0;

        };
    }
}
