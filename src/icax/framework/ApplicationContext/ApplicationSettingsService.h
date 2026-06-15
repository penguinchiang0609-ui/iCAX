#pragma once

#include "ApplicationContext.h"
#include "IApplicationConfigStore.h"

#include <memory>

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP CApplicationSettingsService final
        {
        public:
            CApplicationSettingsService(IN std::shared_ptr<CApplicationContext> pContext_, IN std::shared_ptr<IApplicationConfigStore> pStore_, IN const std::string& strConfigPath_);
            ~CApplicationSettingsService() = default;

            CApplicationSettingsService(IN const CApplicationSettingsService&) = delete;
            CApplicationSettingsService& operator=(IN const CApplicationSettingsService&) = delete;

        public:
            const CApplicationSettings& GetSettings() const;
            void SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);
            void SetValue(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_);
            void Save() const;
            void Reload();

        private:
            std::shared_ptr<CApplicationContext> m_pContext;
            std::shared_ptr<IApplicationConfigStore> m_pStore;
            std::string m_strConfigPath;
        };
    }
}
