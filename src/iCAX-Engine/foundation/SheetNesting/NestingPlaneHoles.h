#pragma once
#include "NestingNfpPlacement.h"

namespace iCAX::SheetNesting::Detail
{
    // Query only: the caller owns branch inventory and commits every returned
    // original through its normal placement path. No host or child is moved.
    // Hole material is an actual local polygon domain, not its bounding box.
    // Candidate geometry may conservatively fill moving holes or circumscribe
    // a circle; acceptance always checks the unchanged original material.
    NfpPlacementQuery FindPlaneHolePlacements(const Problem&, const Layout&,
        std::size_t partIndex, double angle, const NfpPlacementLimits& limits = {},
        const std::function<bool()>& stop = {});
}
