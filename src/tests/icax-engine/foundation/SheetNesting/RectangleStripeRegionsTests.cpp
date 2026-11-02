#include "NestingRectangleStripeRegions.h"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;
namespace
{
    void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
    struct Model {int W,H;std::vector<RectangleBlockType> Types;std::vector<RectangleRepairPrimitive> Snapshot;};
    Options Settings(std::size_t attempts=2000000000,double seconds=60)
    {Options settings;settings.MaxPlacementAttempts=attempts;settings.TimeLimitSeconds=seconds;return settings;}
    struct Solved
    {
        bool Complete;RectangleStripeRegionStats Stats;std::size_t Checks;
        std::vector<std::tuple<int,int,int,int>> Signature;
    };
    Solved Solve(const Model& model,Options settings=Settings(),RectangleStripeRegionLimits limits={},std::size_t bytes=128*1024*1024)
    {
        RectangleBlockMemory memory(bytes);RectangleBlockBudget job{settings};
        std::pmr::vector<RectangleRepairPrimitive> output(&memory);RectangleStripeRegionStats stats;
        const bool complete=SearchRectangleStripeRegions(model.W,model.H,model.Types,model.Snapshot,job,&memory,output,stats,limits);
        Require(job.Attempts<=settings.MaxPlacementAttempts&&stats.Checks==job.Attempts,"all region, donor, exact and reconstruction checks share one budget");
        std::vector<std::tuple<int,int,int,int>> signature;
        if(complete)
        {
            std::vector<int> counts(model.Types.size()),cells(model.W*model.H);
            for(const auto& p:output)
            {
                Require(p.TypeIndex>=0&&p.TypeIndex<static_cast<int>(model.Types.size()),"original canonical type restored");
                const auto& type=model.Types[p.TypeIndex];Require(p.Width==type.Width&&p.Height==type.Height,"original dimensions and fixed direction retained");
                signature.emplace_back(p.X,p.Y,p.Width,p.Height);
                Require(p.X>=0&&p.Y>=0&&p.X<=model.W-p.Width&&p.Y<=model.H-p.Height,"original footprint inside sheet");
                ++counts[p.TypeIndex];for(int y=p.Y;y<p.Y+p.Height;++y)for(int x=p.X;x<p.X+p.Width;++x)
                    Require(++cells[y*model.W+x]==1,"all true primitives have disjoint occupied cells");
            }
            for(std::size_t i=0;i<counts.size();++i)Require(counts[i]==model.Types[i].Quantity,"all original copies retained");
            Require(std::all_of(cells.begin(),cells.end(),[](int n){return n==1;}),"all original cells covered by the full output");
        }
        else Require(output.empty(),"bounded, invalid or cancelled service never publishes partial output");
        std::sort(signature.begin(),signature.end());return {complete,stats,job.Attempts,std::move(signature)};
    }
    Model ResearchPartial()
    {
        // Saved self-generated partial, used solely as a deterministic service
        // fixture. It is not a fresh public case or an external reference layout.
        Model model{80,120,{
            {1,1,3},{1,6,1},{10,2,1},{10,29,1},{10,33,1},{10,4,1},{10,8,1},{11,13,1},{11,2,1},{11,50,1},{12,55,1},{12,6,1},{15,26,1},{15,3,1},{15,5,1},{16,7,1},{16,8,1},{17,5,1},{19,5,1},{19,9,1},{2,1,1},{2,10,1},{2,11,1},{2,2,2},{2,3,2},{2,6,2},{2,9,1},{20,7,1},{22,7,1},{23,9,1},{24,30,1},{24,4,1},{26,11,1},{26,5,1},{26,8,1},{28,6,1},{29,8,1},{3,1,1},{3,11,1},{3,2,3},{3,3,2},{3,4,1},{3,5,1},{3,6,1},{30,5,1},{36,6,1},{39,6,1},{4,13,1},{4,14,1},{4,22,1},{4,25,1},{4,26,1},{4,3,1},{4,4,3},{4,5,1},{5,1,1},{5,11,1},{5,13,1},{5,15,1},{5,2,2},{5,20,1},{5,3,1},{5,30,1},{5,5,1},{6,2,1},{6,20,1},{6,23,1},{6,4,1},{6,5,2},{6,9,1},{7,18,1},{7,3,1},{7,39,1},{7,6,1},{7,9,1},{8,2,2},{8,33,1},{8,45,1},{9,11,1},{9,13,1},{9,15,1},{9,17,1},{9,2,2}},{} };
        const int footprint[][4]={
            {0,0,12,55},{12,0,5,3},{17,0,4,3},{21,0,15,3},{12,3,24,30},{36,0,26,8},{36,8,26,11},{36,19,26,5},{36,24,7,9},{43,24,19,9},{62,0,10,4},{62,4,10,29},{72,0,8,33},{45,44,5,15},{45,59,5,20},{45,79,5,11},{45,90,5,30},{50,44,15,5},{50,49,15,26},{50,75,8,45},{58,75,7,6},{58,81,7,39},{65,44,4,25},{65,69,4,13},{65,82,4,26},{69,44,11,13},{69,57,11,2},{69,59,11,50},{68,109,12,6},{68,115,6,5},{74,115,6,5},{65,108,3,4},{65,112,3,3},{65,115,3,5},{0,112,16,8},{16,112,29,8},{7,55,36,6},{43,55,2,3},{43,58,2,3},{7,61,16,7},{23,61,22,7},{0,69,10,8},{0,77,10,2},{0,79,10,33},{10,69,6,20},{10,89,6,23},{16,68,9,13},{16,81,9,15},{25,68,9,11},{25,79,9,17},{12,33,28,6},{41,33,39,6},{12,39,6,4},{18,39,4,4},{22,39,24,4},{46,39,4,5},{50,39,30,5},{16,96,20,7},{41,68,4,22},{22,103,23,9},{0,55,4,14},{4,55,3,3},{4,58,3,11},{43,90,2,11},{36,101,3,2},{39,101,6,2},{16,103,6,9},{12,43,5,5},{17,43,19,5},{36,86,5,2},{36,88,5,13},{12,48,17,5},{12,53,8,2},{20,53,3,2},{23,53,9,2},{34,68,7,18},{29,48,7,3},{40,43,2,6},{42,43,3,6},{36,43,4,4},{36,47,4,4},{29,51,5,2},{34,51,9,2},{43,49,2,6},{32,53,8,2},{41,49,2,2},{40,53,3,2},{41,90,2,2},{34,86,2,10},{41,92,2,9},{40,33,1,6},{7,68,5,1},{12,68,3,1},{45,43,1,1},{15,68,1,1},{68,108,1,1}};
        for(const auto& p:footprint)
        {
            const auto found=std::find_if(model.Types.begin(),model.Types.end(),[&](const auto& type){return type.Width==p[2]&&type.Height==p[3];});
            Require(found!=model.Types.end(),"fixture dimensions in original inventory");
            model.Snapshot.push_back({static_cast<int>(found-model.Types.begin()),p[0],p[1],p[2],p[3]});
        }
        Require(model.Snapshot.size()==96,"fixture retains 96 of the 97 required originals");return model;
    }
}
int main()
{
    try
    {
        const auto fixture=ResearchPartial();const auto repaired=Solve(fixture);
        Require(repaired.Complete&&!repaired.Stats.AlreadyComplete&&repaired.Stats.Candidates>=1&&repaired.Stats.Trials>=1,
            "automatic stripe donor selection and exact original repair complete the saved partial");
        Require(repaired.Stats.DonorsTested>0&&repaired.Stats.StripeRejected>0,"fixture exercises incompatible-base and automatic donor path");
        std::cout<<"97-original service candidates="<<repaired.Stats.Candidates<<" trials="<<repaired.Stats.Trials
            <<" exactNodes="<<repaired.Stats.ExactNodes<<" checks="<<repaired.Checks<<'\n';
        auto reordered=fixture;std::reverse(reordered.Snapshot.begin(),reordered.Snapshot.end());
        const auto reversed=Solve(reordered);Require(reversed.Complete&&reversed.Signature==repaired.Signature,"reordered saved snapshot produces the same complete original geometry");
        auto typeReordered=fixture;std::reverse(typeReordered.Types.begin(),typeReordered.Types.end());
        for(auto& p:typeReordered.Snapshot)p.TypeIndex=static_cast<int>(typeReordered.Types.size())-1-p.TypeIndex;
        const auto reversedTypes=Solve(typeReordered);
        Require(reversedTypes.Complete&&reversedTypes.Signature==repaired.Signature,"canonical types may arrive in any row order without changing geometry");
        Model fullBoardClosure{3,3,{{1,1,9}},{{0,1,1,1,1}}};const auto noCandidates=Solve(fullBoardClosure);
        Require(!noCandidates.Complete&&noCandidates.Stats.NoCandidates&&noCandidates.Stats.FullBoardRegions==1&&
            !noCandidates.Stats.InvalidInput&&!noCandidates.Stats.GlobalBudget,"all-board closure is no bounded candidate, not whole-board infeasibility");
        Model complete{4,3,{{2,3,2}},{{0,0,0,2,3},{0,2,0,2,3}}};const auto ready=Solve(complete);
        Require(ready.Complete&&ready.Stats.AlreadyComplete,"complete source snapshot is exactly validated");
        auto overlap=complete;overlap.Snapshot[1].X=1;Require(Solve(overlap).Stats.InvalidInput,"overlapping original snapshot rejected");
        auto wrong=complete;wrong.Snapshot[0].Width=1;Require(Solve(wrong).Stats.InvalidInput,"snapshot shape must match original type");
        auto missing=complete;missing.Types[0].Quantity=3;Require(Solve(missing).Stats.InvalidInput,"original inventory area must match board");
        for(std::size_t cap:{0u,1u,64u,4096u,200000u,2000000u})
        {const auto limited=Solve(fixture,Settings(cap));Require(!limited.Complete&&limited.Stats.GlobalBudget,"global budget stops all selection and exact stages");}
        for(std::size_t pulse:{1u,4u,25u,1000u,10000u})
        {
            std::size_t polls=0;auto options=Settings();options.Cancel=[&]{return ++polls==pulse;};const auto cancelled=Solve(fixture,options);
            Require(!cancelled.Complete&&cancelled.Stats.Cancelled&&polls==pulse,"real cancellation remains latched");
            if(pulse==10000)Require(cancelled.Stats.Trials>0&&cancelled.Stats.ExactNodes>0,"late cancellation interrupts the exact service through the shared budget view");
        }
        RectangleStripeRegionLimits tinyTime;tinyTime.TrialSeconds=1e-9;const auto trialTime=Solve(fixture,Settings(),tinyTime);
        Require(!trialTime.Complete&&trialTime.Stats.TrialTimeLimits>0&&!trialTime.Stats.Cancelled&&!trialTime.Stats.GlobalBudget,
            "local trial deadline remains an unknown result and does not cancel global job");
        RectangleStripeRegionLimits tinyNode;tinyNode.Exact.Nodes=1;const auto trialNode=Solve(fixture,Settings(),tinyNode);
        Require(!trialNode.Complete&&trialNode.Stats.LimitedTrials>0&&!trialNode.Stats.GlobalBudget,"local node cap remains unknown");
        Require(Solve(fixture,Settings(),{},1).Stats.MemoryLimit,"shared memory cap applies before and during region selection");
        Require(Solve(fixture,Settings(2000000000,1e-9)).Stats.GlobalBudget,"global elapsed-time deadline remains active in selection and trials");
        std::cout<<"RectangleStripeRegionsTests passed; saved-partial service controls do not add public benchmark successes\n";return 0;
    }
    catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
