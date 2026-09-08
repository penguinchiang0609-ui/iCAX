#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::GeometryData { struct BRepModel; }

namespace iCAX::TubeDesigner
{
    struct SNestingExportPart final
    {
        std::string ID;
        std::string Name;
        std::string Number;
        TopoDS_Shape Shape; // Final manufacturing BRep, already normalized along +X.
        std::string SourceResourceID;
        std::uint64_t SourceResourceVersion = 0;
        // Background jobs retain the immutable BRep source and rebuild the OCCT
        // shape off the SDO thread. Export callers may continue to provide Shape.
        std::shared_ptr<const iCAX::GeometryData::BRepModel> SourceModel;
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
        std::array<double, 16> Transform{};
        bool HasTransform = false;
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
        std::string ProfileKey; // Authoritative grouping identity, not the display label.
    };

    // Rigidly places real manufacturing shapes. No stock/remnant proxy solids are added.
    _TUBE_DESIGNER_EXP TopoDS_Shape BuildNestingExportCompound(
        const SNestingExportPlan& Plan_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_);

    // Schedules low-priority, one-at-a-time resource construction. A completed
    // compound is published atomically to the runtime resource pool; failed or
    // superseded jobs never publish partial resources.
    _TUBE_DESIGNER_EXP void QueueNestingResultResources(
        const std::string& CacheScope_,
        const std::vector<SNestingExportPlan>& Plans_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_);

    _TUBE_DESIGNER_EXP bool IsNestingResultResourceReady(
        const std::string& CacheScope_,
        const SNestingExportPlan& Plan_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_);

    // Publishes STEP files and group workbooks in per-profile subdirectories,
    // plus an overall workbook in a new unique output directory.
    // The selected root must already exist; existing files are never overwritten.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap ExportNestingResults(
        const std::filesystem::path& TargetRoot_,
        const std::vector<SNestingExportPlan>& Plans_,
        const std::vector<SNestingExportPart>& Parts_,
        double PartGap_,
        const std::string& CacheScope_ = {});
}
