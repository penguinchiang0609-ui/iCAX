#pragma once

#include "TubeDesignerExport.h"

#include <cstdint>
#include <filesystem>
#include <string>
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
        std::string ProfileType;
        double SectionWidth = 0.0;
        double SectionDepth = 0.0;
        double WallThickness = 0.0;
        double CornerRadius = 0.0;
        double Length = 0.0;
        std::uint64_t Quantity = 1;
        std::string FileName;
        std::string RelativePath;
    };

    _TUBE_DESIGNER_EXP void WritePartListWorkbook(
        const std::filesystem::path& TargetPath_,
        const std::vector<SPartListRow>& Rows_);
}
