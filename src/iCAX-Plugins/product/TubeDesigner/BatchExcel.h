#pragma once

#include "TubeDesignerExport.h"

#include "Data/Variant.h"

#include <filesystem>
#include <string>
#include <vector>

namespace iCAX::TemplateRuntime { struct STemplateDescriptor; }

namespace iCAX::TubeDesigner
{
    struct SBatchExcelColumn final
    {
        std::string Key;
        std::string Title;
        bool Required = false;
        std::string DefaultValue;
    };

    struct SBatchExcelImportRow final
    {
        std::uint64_t SourceRow = 0;
        std::string InstanceName;
        std::uint64_t InstanceQuantity = 1;
        iCAX::Data::ObjectMap Parameters;
    };

    struct SBatchExcelImport final
    {
        std::string TemplateID;
        std::string TemplateVersion;
        std::string TemplateName;
        std::vector<SBatchExcelImportRow> Rows;
    };

    struct SBatchExcelDefinition final
    {
        std::string TemplateID;
        std::string TemplateVersion;
        std::string TemplateName;
        std::vector<SBatchExcelColumn> Columns;
    };

    // A standard .xlsx is both the user-facing blank workbook and the import
    // file. Its hidden metadata worksheet carries the product import contract.
    _TUBE_DESIGNER_EXP std::filesystem::path WriteBatchExcelTemplate(
        const std::filesystem::path& TemplatePath_,
        const iCAX::TemplateRuntime::STemplateDescriptor& Descriptor_,
        const std::vector<SBatchExcelColumn>& Columns_);

    _TUBE_DESIGNER_EXP SBatchExcelDefinition ReadBatchExcelDefinition(
        const std::filesystem::path& WorkbookPath_);

    // Reads the .xlsx workbook previously exported for batch product input.
    _TUBE_DESIGNER_EXP SBatchExcelImport ReadBatchExcelImport(
        const std::filesystem::path& WorkbookPath_,
        const iCAX::TemplateRuntime::STemplateDescriptor& Descriptor_);
}
