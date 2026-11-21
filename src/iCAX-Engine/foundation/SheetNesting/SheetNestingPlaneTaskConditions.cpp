#include "SheetNestingPlaneTaskConditions.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iCAX::SheetNesting
{
    void ApplyPlaneTaskConditions(Problem& problem, const PlaneTaskConditions& conditions)
    {
        if (!std::isfinite(conditions.StockWidth) || !std::isfinite(conditions.StockHeight) ||
            conditions.StockWidth <= 0 || conditions.StockHeight <= 0 ||
            conditions.StockWidth > 1e7 || conditions.StockHeight > 1e7 ||
            !std::isfinite(conditions.EdgeMargin) || conditions.EdgeMargin < 0 ||
            2 * conditions.EdgeMargin >= std::min(conditions.StockWidth, conditions.StockHeight) ||
            !std::isfinite(conditions.PartGap) || conditions.PartGap < 0 ||
            (conditions.StockQuantity && *conditions.StockQuantity > 100000))
            throw std::invalid_argument("Invalid common plane task conditions.");

        problem.PartGap = conditions.PartGap;
        problem.EdgeMargin = conditions.EdgeMargin;
        problem.AllowCommonEdges = conditions.AllowCommonEdges;
        for (auto& stock : problem.Stocks)
        {
            stock.Width = conditions.StockWidth;
            stock.Height = conditions.StockHeight;
            if (conditions.StockQuantity) stock.Quantity = *conditions.StockQuantity;
        }
        for (auto& part : problem.Parts)
        {
            part.FreeRotation = true;
            part.AllowedAnglesDegrees.clear();
        }
    }
}
