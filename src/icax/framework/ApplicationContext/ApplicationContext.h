#pragma once

#include "IApplicationContext.h"

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP CApplicationContext final : public IApplicationContext
        {
        public:
            CApplicationContext() = default;
            CApplicationContext(IN const CApplicationDescriptor& Descriptor_, IN const CApplicationPaths& Paths_, IN const CApplicationSettings& Settings_);
            ~CApplicationContext() override = default;

        public:
            const CApplicationDescriptor& GetDescriptor() const override;
            const CApplicationPaths& GetPaths() const override;
            const CApplicationSettings& GetSettings() const override;

            void ReplaceSettings(IN const CApplicationSettings& Settings_);

        private:
            CApplicationDescriptor m_Descriptor;
            CApplicationPaths m_Paths;
            CApplicationSettings m_Settings;
        };
    }
}
