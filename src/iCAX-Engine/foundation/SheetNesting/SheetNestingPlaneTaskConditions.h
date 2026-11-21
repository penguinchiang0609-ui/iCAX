#pragma once
#include "SheetNesting.h"
#include <optional>

namespace iCAX::SheetNesting
{
    // Shared task conditions applied once before independent construction.
    // Geometry stays in its source coordinates; this is not a scale or fit.
    struct PlaneTaskConditions
    {
        double StockWidth = 2440, StockHeight = 1220;
        double EdgeMargin = .5, PartGap = .2;
        bool AllowCommonEdges = true;
        // Empty preserves each stock's real inventory, including zero stock.
        // Callers may explicitly supply another finite inventory for a task.
        std::optional<std::size_t> StockQuantity;
    };

    // Sets all part orientation domains to unrestricted rotation. Constructors
    // still generate finite orientation candidates; no optimality is implied.
    // IDs, materials, costs, demand and all original geometry are retained.
    // Invalid condition values throw before changing the supplied problem.
    void ApplyPlaneTaskConditions(Problem& problem, const PlaneTaskConditions& conditions = {});
}
