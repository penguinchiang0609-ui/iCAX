#pragma once

#include "IApplicationContext.h"
#include "IApplicationConfigStore.h"
#include "Services/ServiceProvider.h"

#include <memory>
#include <mutex>
#include <string>

namespace iCAX
{
    namespace Application
    {
        class CApplicationConfigService;
        class CApplicationRuntime;

        /*
        * @brief 默认应用作用域环境实现。
        * @details
        *   管理 Descriptor、Paths、Settings、配置存储和应用级 ServiceProvider。
        *   ApplicationRuntime 拥有本对象的生命周期；下游只获得 const IApplicationContext 视图。
        */
        class _APPLICATION_CONTEXT_EXP CApplicationContext final : public IApplicationContext
        {
        public:
            /*
            * @brief 构造空上下文。
            */
            CApplicationContext();

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

            /*
            * @brief 构造带持久化配置的应用上下文。
            * @details 构造时由 Context 从 ConfigStore_ 加载设置。
            */
            CApplicationContext(
                IN const CApplicationDescriptor& Descriptor_,
                IN const CApplicationPaths& Paths_,
                IN std::shared_ptr<IApplicationConfigStore> pConfigStore_,
                IN std::string strConfigPath_);
            ~CApplicationContext() override;

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
            * @brief 获取应用作用域服务环境的只读入口。
            */
            const iCAX::Services::CServiceProvider& Services() const override;

        private:
            friend class CApplicationConfigService;
            friend class CApplicationRuntime;

            /*
            * @brief 整体替换应用设置。
            * @details 活跃 Context 的写入口只对 ApplicationRuntime 及其配置服务开放；
            *   Product/Project/Scene/Facade 只能持有 const IApplicationContext 视图。
            */
            void ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_);

            void SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);
            void SetValue(
                IN const std::string& strKey_,
                IN const std::string& strPath_,
                IN const iCAX::Data::Variant& Value_);
            void SaveSettings() const;
            void ReloadSettings();
            iCAX::Services::CServiceProvider& MutableServices();

            CApplicationDescriptor m_Descriptor;
            CApplicationPaths m_Paths;
            mutable std::mutex m_SettingsMutex;
            iCAX::Data::PropertyBag m_Settings;
            std::shared_ptr<IApplicationConfigStore> m_pConfigStore;
            std::string m_strConfigPath;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
        };
    }
}
