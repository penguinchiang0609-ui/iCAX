#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include "TubeNesting/TubeNesting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    struct SLinearNestingGeometry;
    struct SNestingEnd final
    {
        double Projection = 0.0;
        std::string NestingPlane;
        bool AllowTrapezoidNesting = false;
        // 展开条带端曲线编码；空编码时保持旧版包络排样行为。
        iCAX::TubeNesting::CutLineFeatureCode Feature;
    };

    struct SNestingVariant final
    {
        std::string ID = "default";
        double EnvelopeLength = 0.0;
        double MaterialLength = 0.0;
        SNestingEnd LeftEnd;
        SNestingEnd RightEnd;
        bool Reversed = false;
        double RotationRadians = 0.0;
        // 截面展开方向的相位偏移，单位 mm；排样适配层会量化到 0.01 mm。
        double PhaseOffset = 0.0;
    };

    struct SNestingPart final
    {
        std::string ID;
        std::string ProfileKey;
        double Length = 0.0; // Authoritative, conservative axial envelope in mm.
        std::size_t Quantity = 1;
        // Empty means the conservative single-envelope path. Geometry-aware callers
        // explicitly enumerate only manufacturing-safe flip/rotation variants.
        std::vector<SNestingVariant> Variants;
        // Centre of the conservative knife-plane blank (or straight-cut AABB).
        // Placement TRSF values use it so downstream preview/export never has to
        // reconstruct a transform from geometry again.
        std::array<double, 3> LocalCenter{ 0.0, 0.0, 0.0 };
    };

    struct SNestingStock final
    {
        std::string ID;
        std::string ProfileKey;
        double Length = 0.0;
        std::int64_t Quantity = 0; // -1: unlimited, 0: excluded, >0: finite inventory.
    };

    // Shared geometry-to-solver boundary. Tilted knife planes do not imply that
    // the source BRep has planar ends; only proven section symmetry permits roll.
    _TUBE_DESIGNER_EXP std::vector<SNestingVariant> BuildKnifePlaneNestingVariants(
        const SLinearNestingGeometry& Geometry_, double EnvelopeLength_, bool AllowHalfTurn_);

    // Legacy untyped tube items remain supported. Explicit empty/unknown kinds,
    // malformed tags and all plate metadata are rejected before linear nesting.
    _TUBE_DESIGNER_EXP bool IsTubeManufacturingPart(const iCAX::Data::ObjectMap& Properties_);

    // Verify the browser's grouping against the manufacturing part's section snapshot.
    _TUBE_DESIGNER_EXP bool MatchesNestingProfileKey(
        const std::string& Key_, const iCAX::Data::ObjectMap& Profile_, const std::string& PartID_);

    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap NormalizeNestingSettings(
        const iCAX::Data::ObjectMap& Settings_);

    // Uses foundation/TubeNesting. Never mutates the scene or consumes inventory.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap SolveManufacturingNesting(
        const std::vector<SNestingPart>& Parts_,
        const std::vector<SNestingStock>& Stocks_,
        double PartGap_);

    // One independent task per profile group. 0 selects bounded hardware
    // concurrency (up to 8); 1 provides a deterministic serial reference.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap SolveManufacturingNesting(
        const std::vector<SNestingPart>& Parts_,
        const std::vector<SNestingStock>& Stocks_,
        double PartGap_, std::size_t MaximumConcurrency_);

    // Row-major 4x4 rigid transform stored on every solved placement.
    _TUBE_DESIGNER_EXP iCAX::Data::VariantArray MakeNestingPlacementTransform(
        const std::array<double, 3>& LocalCenter_,
        double Start_, double End_, bool Reversed_, double RotationRadians_);
}
