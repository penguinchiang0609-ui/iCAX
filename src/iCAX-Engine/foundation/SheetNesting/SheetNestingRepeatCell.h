#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    // Optional constructor wrapper: one cell hypothesis x constructor is one job.
    // The one compatible stock's usable interior is divided into columns x rows.
    // PartGap must be zero and true demanded area must fill the usable interior.
    // The complete validated unit must also be materially full (roundoff only).
    // Each positive raw Parts row Quantity must be divisible
    // by the cell count; splitting equivalent quantity-one rows is not supported.
    // Positive-demand originals must be proven materially full rectangles with
    // allowed poses that make them world-axis-aligned, at roundoff precision.
    // Geometry and IDs remain original. Complete validated cell output is expanded
    // and validated against the original input under the same caller work budget.
    // No strategy enumeration and no default registry registration are performed.
    StrategyFactory MakeRectangleRepeatCellFactory(StrategyFactory constructor,
        std::size_t columns, std::size_t rows);
}
