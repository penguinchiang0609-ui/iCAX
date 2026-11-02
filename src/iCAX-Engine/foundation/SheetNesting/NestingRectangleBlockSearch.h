#pragma once

#include "NestingInternal.h"

#include <algorithm>
#include <chrono>
#include <memory_resource>
#include <span>

namespace iCAX::SheetNesting::Detail
{
    // One caller-owned budget crosses construction, selection, search and
    // reconstruction. Stage boundaries never reset its attempts or deadline.
    struct RectangleBlockBudget
    {
        using Clock=std::chrono::steady_clock;
        const Options& Settings;
        Clock::time_point Started=Clock::now();
        std::size_t Attempts=0,ControlChecks=255;
        double SampledElapsed=0;
        bool Cancelled=false,Exhausted=false;
        double Elapsed()const{return std::chrono::duration<double>(Clock::now()-Started).count();}
        bool Poll()
        {
            if(Cancelled||Exhausted)return true;
            if(Settings.Cancel&&Settings.Cancel()){Cancelled=true;return true;}
            SampledElapsed=Elapsed();
            if(SampledElapsed>=Settings.TimeLimitSeconds){Exhausted=true;return true;}
            return false;
        }
        bool Stop()
        {
            if(Cancelled||Exhausted)return true;
            if(Attempts>=Settings.MaxPlacementAttempts){Exhausted=true;return true;}
            return (++ControlChecks&255)==0&&Poll();
        }
        bool Probe(){if(Stop())return false;++Attempts;return true;}
    };

    class RectangleBlockMemory final:public std::pmr::memory_resource
    {
    public:
        explicit RectangleBlockMemory(std::size_t limit):Limit(limit){}
        std::size_t Peak=0;
        std::size_t RetainedBytes()const{return Used;}
    private:
        std::size_t Limit,Used=0;
        void* do_allocate(std::size_t bytes,std::size_t alignment)override
        {
            if(bytes>Limit-Used)throw std::bad_alloc();
            void* p=std::pmr::new_delete_resource()->allocate(bytes,alignment);
            Used+=bytes;Peak=std::max(Peak,Used);return p;
        }
        void do_deallocate(void* p,std::size_t bytes,std::size_t alignment)override
        {Used-=bytes;std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);}
        bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override{return this==&other;}
    };

    struct RectangleBlockType {int Width,Height,Quantity;};
    struct RectangleBlockMacro
    {
        int Width=0,Height=0,Members=0,Node=-1;
        std::pmr::vector<std::pair<int,int>> Uses;
        explicit RectangleBlockMacro(std::pmr::memory_resource* memory):Uses(memory){}
    };
    struct RectangleBlockPlacement {int Node,X,Y;};
    struct RectangleBlockRolloutStats
    {
        std::size_t Selected=0,Applied=0,Rollouts=0,Layers=0,DiversityPrunes=0;
        std::size_t CandidateSets=0,PrimitiveSelections=0,SmallSelections=0;
        int RootPrimitiveCount=0,RootSmallCount=0,RootShapeCount=0;
        bool AppliedLimitReached=false;
        int SnapshotCount=0,SnapshotArea=0;
        bool PhaseLimitReached=false,MemoryLimitReached=false;
    };
    struct RectangleBlockRolloutLimits
    {
        int BeamWidth=128,Shortlist=64;
        std::size_t Applied=5000000,Checks=0;
        double Seconds=0;
        bool AllowInternalPartial=false;
    };
    enum class RectangleBlockSnapshotStatus { Complete,Partial,Stopped };
    struct RectangleBlockInterrupted {};
    inline void CheckRectangleBlockBudget(RectangleBlockBudget& budget,bool force=false)
    {if(force?budget.Poll():budget.Stop())throw RectangleBlockInterrupted{};}
    inline void ProbeRectangleBlockBudget(RectangleBlockBudget& budget)
    {if(!budget.Probe())throw RectangleBlockInterrupted{};}

    // Macros refer to the caller's immutable, generated decomposition tree.
    // Search sees dimensions and counts only; original IDs and primitive poses
    // are restored by the caller after a complete placement has been found.
    bool SearchRectangleBlockRollout(int width,int height,
        std::span<const RectangleBlockType> types,std::span<const RectangleBlockMacro> macros,
        RectangleBlockBudget& budget,std::pmr::memory_resource* memory,
        std::pmr::vector<RectangleBlockPlacement>& placement,RectangleBlockRolloutStats& stats,
        BlockRolloutPolicy policy=BlockRolloutPolicy::Score);

    // Internal repair seed only. A partial snapshot is never a public Result.
    // Local stage limits may return the best retained state; global cancellation
    // or exhaustion clears it. Generated Node IDs must be expanded before the
    // caller destroys their library.
    RectangleBlockSnapshotStatus SearchRectangleBlockRolloutSnapshot(int width,int height,
        std::span<const RectangleBlockType> types,std::span<const RectangleBlockMacro> macros,
        RectangleBlockBudget& budget,std::pmr::memory_resource* memory,
        const RectangleBlockRolloutLimits& limits,
        std::pmr::vector<RectangleBlockPlacement>& placement,RectangleBlockRolloutStats& stats);
}
