#pragma once

#include "NestingRectangleResidualCorners.h"

namespace iCAX::SheetNesting::Detail
{
    enum class RectangleStripeCompatibility { Compatible,Incompatible,Unknown };
    struct RectangleStripeStats
    {
        RectangleResidualStats Search;
        std::size_t Checks=0;
        int RowTarget=0,ColumnTarget=0,CheckerTarget=0;
        bool Complete=false,Infeasible=false,StripeRejected=false,InvalidInput=false;
        bool GlobalBudget=false,Cancelled=false,MemoryLimit=false;
    };

    // Necessary conditions for an exact fill of an integer rectangle with fixed
    // occupied rectangles. Compatible is not a feasibility certificate. Invalid
    // or interrupted input returns Unknown; no negative proof is inferred.
    RectangleStripeCompatibility CheckRectangleStripeCompatibility(int width,int height,
        std::span<const RectangleBlockType> types,std::span<const int> remaining,
        std::span<const RectangleResidualFixed> fixedOccupancy,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,RectangleStripeStats& stats);

    // A stripe preflight followed by the shared exact primitive corner solver.
    // Macro and per-root branch limits are disabled, so a completed exhaustion
    // is distinguished from a bounded or interrupted unknown result. Only a
    // complete local fill publishes placements. Active shapes must be canonical
    // (one type per width/height). The caller retains all outside
    // primitives and restores the original identities from the supplied nodes.
    bool SearchRectangleStripeRepair(int width,int height,
        std::span<const RectangleBlockType> types,std::span<const int> remaining,
        std::span<const int> primitiveNodeByType,std::span<const RectangleResidualFixed> fixedOccupancy,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,
        const RectangleResidualLimits& limits,
        std::pmr::vector<RectangleBlockPlacement>& output,RectangleStripeStats& stats);
}
