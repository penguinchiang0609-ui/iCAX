#pragma once

#include "IApplicationContext.h"

#include <functional>
#include <mutex>
#include <memory>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 默认应用上下文实现。
        * @details
            *   保存启动时确定的 Descriptor/Paths/Settings。
            *   Settings 可以通过 ReplaceSettings 整体替换，避免外部长期持有可变引用。
        */
        class _APPLICATION_CONTEXT_EXP CApplicationContext final : public IApplicationContext
        {
        public:
            using PostOfficeProvider = std::function<iCAX::Mail::CMailPostOffice()>;

        public:
            /*
            * @brief 构造空上下文。
            */
            CApplicationContext() = default;

            /*
            * @brief 构造应用上下文。
            * @param [in] Descriptor_ 应用描述。
            * @param [in] Paths_ 应用路径集合。
            * @param [in] Settings_ 应用设置。
            */
            CApplicationContext(
                IN const CApplicationDescriptor& Descriptor_,
                IN const CApplicationPaths& Paths_,
                IN const iCAX::Data::PropertyBag& Settings_);
            ~CApplicationContext() override = default;

        public:
            /*
            * @brief 获取应用描述。
            */
            const CApplicationDescriptor& GetDescriptor() const override;

            /*
            * @brief 获取应用路径集合。
            */
            const CApplicationPaths& GetPaths() const override;

            /*
            * @brief 获取应用设置副本。
            */
            iCAX::Data::PropertyBag GetSettings() const override;

            /*
            * @brief 整体替换应用设置。
            * @param [in] Settings_ 新设置。
            * @details 通常由 CApplicationConfigService::Reload 或 SetValue 调用。
            */
            void ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_);

            /*
            * @brief 绑定运行时能力。
            * @param [in] ApplicationChannelID_ 应用 channel id。
            * @param [in] pServiceProvider_ 应用级服务容器。
            * @param [in] BackendPostOfficeProvider_ 后端邮局获取器。
            * @param [in] FrontendPostOfficeProvider_ 前端邮局获取器。
            * @details
            *   ApplicationContext 本身不拥有 MailChannelRegistry，只保存轻量获取器。
            *   这样通信能力由 ApplicationHost 管理生命周期，但上层调用仍然通过 context 访问。
            */
            void BindRuntimeCapabilities(
                IN const iCAX::Data::uuid& ApplicationChannelID_,
                IN std::weak_ptr<iCAX::Services::CServiceProvider> pServiceProvider_,
                IN PostOfficeProvider BackendPostOfficeProvider_,
                IN PostOfficeProvider FrontendPostOfficeProvider_);

            /*
            * @brief 获取应用级通信通道 ID。
            */
            const iCAX::Data::uuid& GetApplicationChannelID() const override;

            /*
            * @brief 获取后端视角应用邮局。
            */
            iCAX::Mail::CMailPostOffice GetBackendPostOffice() const override;

            /*
            * @brief 获取前端视角应用邮局。
            */
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const override;

            /*
            * @brief 获取应用级服务容器。
            */
            iCAX::Services::CServiceProvider& Services() const override;

        private:
            CApplicationDescriptor m_Descriptor;
            CApplicationPaths m_Paths;
            mutable std::mutex m_SettingsMutex;
            iCAX::Data::PropertyBag m_Settings;
            iCAX::Data::uuid m_ApplicationChannelID;
            std::weak_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
            PostOfficeProvider m_BackendPostOfficeProvider;
            PostOfficeProvider m_FrontendPostOfficeProvider;
        };
    }
}
