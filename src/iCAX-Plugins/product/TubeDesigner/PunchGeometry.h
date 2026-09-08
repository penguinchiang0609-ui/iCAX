#pragma once
#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include <TopoDS_Shape.hxx>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    // All dimensions are millimetres in the immutable, +X part coordinate frame.
    struct SPunchFeature final
    {
        std::string ID;
        std::string Type = "circle";
        // Wall tools: entry face. Part-coordinate tools: top=Y rows, left=Z rows,
        // round=rotation about +X. Signed pitches control direction in both cases.
        std::string Face = "top";
        std::string Reference = "start";
        std::string EndDatum = "long";
        // Empty keeps legacy finished-end positioning. "base" fixes layout to
        // the immutable main blank, independently of subsequent end cutting.
        std::string LayoutDatum;
        bool Enabled = true;
        bool Opposite = false;
        bool Through = false;
        bool Reverse = false;
        bool AllowOpen = false;
        double Station = 0.0;
        double Offset = 0.0;
        double Diameter = 10.0;
        double SpanAlong = 30.0;
        double SpanAcross = 10.0;
        double CornerRadius = 0.0;
        double Rotation = 0.0;
        std::uint64_t ArrayCount = 1;
        double ArrayPitch = 0.0;
        std::uint64_t RowCount = 1;
        double RowPitch = 0.0;
        // Explicit X offsets already include the direction of an end reference.
        // Empty vectors keep the legacy count/pitch expansion. Row offsets are
        // millimetres, except Face="round" where they are angles in degrees.
        std::vector<double> ArrayOffsets;
        std::vector<double> RowOffsets;
        // Stable zero-based candidate indices "row:column", before suppression.
        std::vector<std::string> SkippedInstances;
        bool HasArrayGroups = false;
        std::uint64_t ArrayCandidateCount = 1;
        // Row-major rigid transforms of the already positioned single cutter.
        // Explicit groups replace legacy count/pitch/row expansion entirely.
        std::vector<std::array<double,16>> ArrayTransforms;
        std::vector<std::array<double, 2>> Contour;
        // Refs/parameters and code-free frozenTool persist; toolSnapshot is transient.
        iCAX::Data::ObjectMap TemplateData;
        TopoDS_Shape ToolShape;
        bool ToolInPartCoordinates = false;
        // Already placed cutters in immutable blank coordinates. Used only when
        // the original product-level tool is unavailable; never re-positioned.
        TopoDS_Shape FrozenCut;
        std::uint64_t FrozenCutInstanceCount = 0;
        bool ToolIsProfile = false;
        std::vector<std::array<double, 2>> ToolFootprint;
    };

    struct SPunchEnd final
    {
        std::string Type = "keep";
        std::string Datum = "long";
        double Trim = 0.0;
        double Angle = 45.0;
        double Rotation = 0.0;
        double Diameter = 40.0;
        double Offset = 0.0;
        double Clearance = 0.0;
        iCAX::Data::ObjectMap TemplateData;
        TopoDS_Shape ToolShape;
        bool HasDatumCenter = false;
        double DatumCenter = 0.0;
        bool RequireEndContact = false;
        bool RequireRetainedEndMaterial = false;
    };
    struct SPunchEnds final { SPunchEnd Start; SPunchEnd End; };
    struct SPunchCut final { std::string Target; std::string Key; TopoDS_Shape Shape; std::size_t InstanceCount = 1; };
    struct SPunchGeometryStatistics final {
        std::size_t CandidateCount = 0, SkippedCount = 0, OutsideCount = 0, AppliedCount = 0, EndToolCount = 0;
        bool CountsExact = true;
    };
    // Preview-only relaxation: retain every valid disconnected result while
    // editing. Production creation/application never enables this flag.
    struct SPunchPreviewDiagnostics final {
        bool AllowDisconnectedResults = false;
        bool ResultComputed = false;
        std::size_t SolidCount = 0;
        bool ToolsComplete = false;
        std::string FailureStage = "prepare";
        std::string FailureTarget = "result";
        std::string FailureKey;
        std::int64_t FailureIndex = -1;
    };
    using PunchProgress = std::function<void(std::size_t, std::size_t, const std::string&)>;
    _TUBE_DESIGNER_EXP void PreparePunchToolFootprint(SPunchFeature& Feature);
    _TUBE_DESIGNER_EXP void ValidatePunchFeatureLayout(const SPunchFeature& Feature);

    _TUBE_DESIGNER_EXP std::array<double, 2> PunchFeatureExtents(const SPunchFeature& Feature);
    // Display-only envelope clipping; never pass this result to manufacturing
    // or frozen-cut persistence. Original cutters remain immutable.
    _TUBE_DESIGNER_EXP TopoDS_Shape BuildPunchToolPreview(
        const TopoDS_Shape& Base, const std::vector<SPunchCut>& Cuts);
    // No subtraction, mother-material intersection, or wall-depth probing.
    _TUBE_DESIGNER_EXP std::vector<SPunchCut> BuildPunchToolPlacements(
        const TopoDS_Shape& Base, const std::vector<SPunchFeature>& Features,
        const SPunchEnds& Ends = {}, double NominalWallThickness = 0,
        SPunchGeometryStatistics* Statistics = nullptr, SPunchPreviewDiagnostics* Diagnostics = nullptr);
    _TUBE_DESIGNER_EXP TopoDS_Shape BuildPunchGeometry(
        const TopoDS_Shape& Base, const std::vector<SPunchFeature>& Features,
        const SPunchEnds& Ends = {}, const PunchProgress& Progress = {},
        std::vector<SPunchCut>* AppliedCuts = nullptr, SPunchGeometryStatistics* Statistics = nullptr);
    _TUBE_DESIGNER_EXP TopoDS_Shape BuildPunchGeometry(
        const TopoDS_Shape& Base, const std::vector<SPunchFeature>& Features,
        const SPunchEnds& Ends, const PunchProgress& Progress,
        std::vector<SPunchCut>* AppliedCuts, SPunchGeometryStatistics* Statistics,
        SPunchPreviewDiagnostics* PreviewDiagnostics);
}
