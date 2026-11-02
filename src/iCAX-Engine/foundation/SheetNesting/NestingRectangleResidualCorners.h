#pragma once
#include "NestingRectangleBlockSearch.h"

namespace iCAX::SheetNesting::Detail
{
    struct RectangleResidualFixed {int X,Y,Width,Height;};
    struct RectangleResidualLimits
    {
        std::size_t Nodes=1000000,Probes=100000000;
        std::size_t RootBranchNodes=2000,RootBranchProbes=2000000;
        std::size_t MemoBytes=16*1024*1024,ProjectionBytes=16*1024*1024;
        int MacroLimit=32,RootMacroLimit=512;
    };
    struct RectangleResidualStats
    {
        std::size_t Nodes=0,Probes=0,RootBranches=0,RootTruncations=0;
        std::size_t SpanPrunes=0,DomainPrunes=0,MemoHits=0;
        std::size_t MemoEntries=0,ProjectionEntries=0;
        int BestPlacedCount=0,RequiredCount=0;
        bool Complete=false,InvalidInput=false,MemoryLimit=false;
        bool TrialNodeLimit=false,TrialProbeLimit=false;
        bool GlobalBudget=false,Cancelled=false;
    };

    // Caller supplies validated dense macros from its immutable decomposition
    // tree. No source IDs or primitive member coordinates are consulted here.
    // The remaining inventory plus fixed occupancy must fill the board exactly.
    // Output contains residual node placements only, and is empty on any failure.
    // The caller owns the common RootMemory cap (currently 128 MiB) and Job.
    bool SearchRectangleResidualCorners(int width,int height,
        std::span<const RectangleBlockType> types,
        std::span<const RectangleBlockMacro> macros,
        std::span<const int> remaining,std::span<const int> primitiveNodeByType,
        std::span<const RectangleResidualFixed> fixedOccupancy,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,
        const RectangleResidualLimits& limits,
        std::pmr::vector<RectangleBlockPlacement>& output,RectangleResidualStats& stats);
}
