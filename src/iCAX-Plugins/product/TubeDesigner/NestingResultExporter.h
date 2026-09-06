#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    struct SNestingExportPart final
    {
        std::string ID;
        std::string Name;
        std::string Number;
        TopoDS_Shape Shape; // Final manufacturing BRep, already normalized along +X.
    };

    struct SNestingExportPlacement final
    {
        std::string PartID;
        double Start = 0.0;
        double End = 0.0;
        bool Flipped = false;
        double GapBefore = (std::numeric_limits<double>::quiet_NaN)();
        bool NestedWithPrevious = false;
        double RotationRadians = 0.0;
        std::string VariantID = "default";
    };

    struct SNestingExportPlan final
    {
        std::string ID;
        std::string Profile;
        double StockLength = 0.0;
        double UsedLength = 0.0;
        double RemainingLength = 0.0;
        std::vector<SNestingExportPlacement> Placements;
        std::string Name; // Original UI stock label; subset export must not renumber it.
        double PartLength = 0.0; // Net material-equivalent length used for utilization.
    };

    // Rigidly places real manufacturing shapes. No stock/remnant proxy solids are added.
    _TUBE_DESIGNER_EXP TopoDS_Shape BuildNestingExportCompound(
        const SNestingExportPlan& Plan_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_);

    // Publishes numbered STEP files and one workbook into a new unique child directory.
    // The selected root must already exist; existing files are never overwritten.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap ExportNestingResults(
        const std::filesystem::path& TargetRoot_,
        const std::vector<SNestingExportPlan>& Plans_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_);
}
