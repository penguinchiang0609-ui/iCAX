#pragma once

#include "IApplicationContext.h"

#include <mutex>

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
            CApplicationContext(IN const CApplicationDescriptor& Descriptor_, IN const CApplicationPaths& Paths_, IN const CApplicationSettings& Settings_);
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
            CApplicationSettings GetSettings() const override;

            /*
            * @brief 整体替换应用设置。
            * @param [in] Settings_ 新设置。
            * @details 通常由 CApplicationSettingsService::Reload 或 SetValue 调用。
            */
            void ReplaceSettings(IN const CApplicationSettings& Settings_);

        private:
            CApplicationDescriptor m_Descriptor;
            CApplicationPaths m_Paths;
            mutable std::mutex m_SettingsMutex;
            CApplicationSettings m_Settings;
        };
    }
}
