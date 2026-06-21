#pragma once

#include "ApplicationDescriptor.h"
#include "ApplicationPaths.h"
#include "ApplicationSettings.h"

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用上下文接口。
        * @details
        *   ApplicationContext 保存应用级只读描述、路径和当前设置。
        *   产品、项目、命令处理器可以通过它读取应用环境，但不应把项目数据塞进这里。
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
            * @brief 获取应用设置。
            */
            virtual const CApplicationSettings& GetSettings() const = 0;
        };
    }
}
