#pragma once

#include "TubeDesignerExport.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::TubeDesigner
{
    struct SPartListRow final
    {
        std::uint64_t ProductIndex = 0;
        std::string ProductName;
        std::string ProductCode;
        std::uint64_t PartIndex = 0;
        std::string PartNumber;
        std::string PartName;
        std::string ProfileDisplayName;
        std::string ProfileSpecification;
        double Length = 0.0;
        std::uint64_t Quantity = 1;
        std::string FileName;
        std::string RelativePath;
        std::string PartKind = "tube";
        double PlateWidth = 0.0;
        double PlateHeight = 0.0;
        double PlateThickness = 0.0;
        std::string Material;
    };

    _TUBE_DESIGNER_EXP void WritePartListWorkbook(
        const std::filesystem::path& TargetPath_,
        const std::vector<SPartListRow>& Rows_);

    using STableWorkbookCell = std::variant<std::string, double>;

    // Uses the same ZIP package, cell escaping and styles as the existing part list.
    // Strings remain literal cells, never formulas. The destination must not exist.
    _TUBE_DESIGNER_EXP void WriteTableWorkbook(
        const std::filesystem::path& TargetPath_,
        const std::string& Title_,
        const std::vector<std::string>& Headers_,
        const std::vector<std::vector<STableWorkbookCell>>& Rows_);
}
