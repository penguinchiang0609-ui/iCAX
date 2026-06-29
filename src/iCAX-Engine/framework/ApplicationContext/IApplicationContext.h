#pragma once

#include "ApplicationDescriptor.h"
#include "ApplicationPaths.h"
#include "ApplicationSettings.h"
#include "Data/uuid.h"
#include "Mailbox/MailPostOffice.h"

#include <stdexcept>

namespace iCAX
{
    namespace Services
    {
        class CServiceProvider;
    }
}

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
            * @brief 获取应用设置副本。
            * @return 当前应用设置副本。
            */
            virtual CApplicationSettings GetSettings() const = 0;

            /*
            * @brief 获取应用级通信通道 ID。
            * @return ApplicationHost 创建的应用 channel id。
            */
            virtual const iCAX::Data::uuid& GetApplicationChannelID() const
            {
                static const iCAX::Data::uuid _Nil = iCAX::Data::GenerateNilUUID();
                return _Nil;
            }

            /*
            * @brief 获取后端视角应用邮局。
            * @return 应用 channel 的后端端点。
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice() const
            {
                throw std::logic_error("Application backend post office is not configured");
            }

            /*
            * @brief 获取前端视角应用邮局。
            * @return 应用 channel 的前端端点。
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const
            {
                throw std::logic_error("Application frontend post office is not configured");
            }

            /*
            * @brief 获取应用级服务容器。
            * @return Application 范围共享的服务容器。
            */
            virtual iCAX::Services::CServiceProvider& Services() const
            {
                throw std::logic_error("Application service provider is not configured");
            }
        };
    }
}
