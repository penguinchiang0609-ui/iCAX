#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include <filesystem>
#include <functional>

namespace iCAX::TubeDesigner
{
    // Filesystem bookkeeping only. Creating and disassembling products remains
    // a scene operation orchestrated by the workbench.
    class _TUBE_DESIGNER_EXP CBatchExcelAutomation final
    {
    public:
        using TWorkbookValidator = std::function<void(const std::filesystem::path&)>;
        explicit CBatchExcelAutomation(std::filesystem::path UserDataDirectory_);
        iCAX::Data::ObjectMap GetSettings() const;
        iCAX::Data::ObjectMap SaveSettings(const iCAX::Data::ObjectMap& Settings_) const;
        iCAX::Data::ObjectMap Scan() const;
        iCAX::Data::ObjectMap Claim(const iCAX::Data::ObjectMap& Request_,
            const TWorkbookValidator& Validate_) const;
        iCAX::Data::ObjectMap Complete(const iCAX::Data::ObjectMap& Request_) const;
    private:
        std::filesystem::path m_Root;
    };
}
