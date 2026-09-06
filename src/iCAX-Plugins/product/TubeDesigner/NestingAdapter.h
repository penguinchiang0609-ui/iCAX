#pragma once

#include "TubeDesignerExport.h"
#include "Data/Variant.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    struct SNestingEnd final
    {
        double Projection = 0.0;
        std::string NestingPlane;
        bool AllowTrapezoidNesting = false;
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
    };

    struct SNestingStock final
    {
        std::string ID;
        std::string ProfileKey;
        double Length = 0.0;
        std::int64_t Quantity = 0; // -1: unlimited, 0: excluded, >0: finite inventory.
    };

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
}
