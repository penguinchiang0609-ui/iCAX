#pragma once

#include "IApplicationConfigStore.h"

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP CFileApplicationConfigStore final : public IApplicationConfigStore
        {
        public:
            CFileApplicationConfigStore() = default;
            ~CFileApplicationConfigStore() override = default;

        public:
            CApplicationSettings Load(IN const std::string& strPath_) const override;
            void Save(IN const std::string& strPath_, IN const CApplicationSettings& Settings_) const override;
        };
    }
}
