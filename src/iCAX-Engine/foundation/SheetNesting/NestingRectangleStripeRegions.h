#pragma once
#include "NestingRectangleStripeRepair.h"
#include "NestingRectangleRepairRegions.h"

namespace iCAX::SheetNesting::Detail
{
    struct RectangleStripeRegionLimits
    {
        std::size_t Regions=64,Donors=8,Trials=16;
        double TrialSeconds=120;
        RectangleResidualLimits Exact=[]
        {RectangleResidualLimits limits;limits.Nodes=100000000;limits.Probes=5000000000ull;return limits;}();
    };
    struct RectangleStripeRegionStats
    {
        RectangleRepairRegionStats Regions;
        std::size_t Checks=0,Candidates=0,DonorsTested=0,StripeRejected=0;
        std::size_t Trials=0,LocalInfeasible=0,LimitedTrials=0,TrialTimeLimits=0,ExactNodes=0,ExactChecks=0;
        std::size_t FullBoardRegions=0;
        bool Complete=false,AlreadyComplete=false,NoCandidates=false,InvalidInput=false;
        bool GlobalBudget=false,Cancelled=false,MemoryLimit=false,TrialLimit=false;
    };

    // Automatic closed-hole regions and, only when stripe-incompatible, one
    // adjoining original donor. Fixed outside originals remain fixed; donors
    // are returned to the exact original inventory. A bounded search failure
    // never claims whole-board infeasibility. Output is full original primitives
    // only after complete inventory and exact-cell verification.
    bool SearchRectangleStripeRegions(int width,int height,
        std::span<const RectangleBlockType> types,std::span<const RectangleRepairPrimitive> snapshot,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,
        std::pmr::vector<RectangleRepairPrimitive>& output,RectangleStripeRegionStats& stats,
        const RectangleStripeRegionLimits& limits={});
}
