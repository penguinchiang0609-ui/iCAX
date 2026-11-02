#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    enum class RasterScanAxis { XThenY, YThenX };
    struct RasterStrategySettings
    {
        // Physical millimetres per cell; not automatically chosen from the problem.
        double Step = 0;
        // Complete permutation of the original Parts indices, including zero quantities.
        std::vector<std::size_t> Priority;
        RasterScanAxis Axis = RasterScanAxis::YThenX;
        bool Interleave = false;
    };

    // Each factory specifies one independent deterministic job; not a default strategy.
    // Supports one rectangular stock type, uses at most its first sheet, zero gap,
    // and simple polygon parts without holes that permit 0 degrees. This strategy
    // tries only 0 degrees even if the input permits other angles or free rotation.
    // Original vertices retain their physical coordinates; only translations are
    // quantized to the specified physical grid.
    // Invoke through RunJob/RunPortfolio for input and output validation. Direct
    // IStrategy::Run requires an already valid Problem and Options.
    StrategyFactory MakeConservativeRasterFactory(RasterStrategySettings settings);
}
