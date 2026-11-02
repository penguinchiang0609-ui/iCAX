#pragma once

#include "NestingRectangleBlockSearch.h"
#include <cstdint>

namespace iCAX::SheetNesting::Detail
{
    struct RectangleRepairPrimitive
    {
        int TypeIndex=0,X=0,Y=0,Width=0,Height=0;
        bool operator==(const RectangleRepairPrimitive&)const=default;
    };
    struct RectangleRepairBounds
    {
        int X0=0,Y0=0,X1=0,Y1=0;
        std::int64_t Area()const{return std::int64_t(X1-X0)*(Y1-Y0);}
        bool operator==(const RectangleRepairBounds&)const=default;
    };
    struct RectangleRepairRegion
    {
        RectangleRepairBounds Bounds;
        std::pmr::vector<std::size_t> OutsidePrimitiveIndices;
        explicit RectangleRepairRegion(std::pmr::memory_resource* memory):OutsidePrimitiveIndices(memory){}
    };
    struct RectangleRepairRegionStats
    {
        std::size_t Candidates=0,Discovered=0,Probes=0;
        bool AlreadyComplete=false,InvalidInput=false,Cancelled=false;
        bool GlobalBudget=false,MemoryLimit=false,CandidateLimit=false;
    };

    // Exact integer geometry. All holes seed one AABB; strict intersection
    // closure absorbs whole primitives. Four one-unit outward expansions are
    // enumerated by area then (x0,y0,x1,y1), as in the research Python helper.
    // maxCandidates counts emitted regions, not frontier entries (max 1000).
    // The caller owns inventory subtraction and the max 12 actual repair trials.
    // No partial output is published on interruption or invalid input.
    bool GenerateRectangleRepairRegions(int width,int height,
        std::span<const RectangleRepairPrimitive> primitives,
        RectangleBlockBudget& budget,std::pmr::memory_resource* memory,
        std::pmr::vector<RectangleRepairRegion>& regions,RectangleRepairRegionStats& stats,
        std::size_t maxCandidates=1000);
}
