#pragma once

#include "ApplicationContext.h"
#include "IApplicationConfigStore.h"

#include <memory>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用设置服务。
        * @details
        *   封装 ApplicationContext 与配置存储。
        *   修改设置时会复制当前 Settings、写入新值，再整体替换到 Context。
        */
        class _APPLICATION_CONTEXT_EXP CApplicationSettingsService final
        {
        public:
            /*
            * @brief 构造应用设置服务。
            * @param [in] pContext_ 应用上下文，不能为空。
            * @param [in] pStore_ 配置存储，不能为空。
            * @param [in] strConfigPath_ 配置文件路径。
            */
            CApplicationSettingsService(IN std::shared_ptr<CApplicationContext> pContext_, IN std::shared_ptr<IApplicationConfigStore> pStore_, IN const std::string& strConfigPath_);
            ~CApplicationSettingsService() = default;

            CApplicationSettingsService(IN const CApplicationSettingsService&) = delete;
            CApplicationSettingsService& operator=(IN const CApplicationSettingsService&) = delete;

        public:
            /*
            * @brief 获取当前应用设置。
            * @return 上下文中的当前设置。
            */
            const CApplicationSettings& GetSettings() const;

            /*
            * @brief 设置顶层配置值。
            */
            void SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 设置分层路径下的配置值。
            */
            void SetValue(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 保存当前设置到配置存储。
            */
            void Save() const;

            /*
            * @brief 从配置存储重新加载设置并替换上下文中的 Settings。
            */
            void Reload();

        private:
            std::shared_ptr<CApplicationContext> m_pContext;
            std::shared_ptr<IApplicationConfigStore> m_pStore;
            std::string m_strConfigPath;
        };
    }
}
