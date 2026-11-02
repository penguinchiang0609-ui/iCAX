#include "NestingRectangleStripeRepair.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;
namespace
{
    void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
    struct Model
    {
        int W,H;
        std::vector<RectangleBlockType> Types;
        std::vector<RectangleResidualFixed> Fixed;
    };
    Options Settings(std::size_t attempts=1000000000,double seconds=30)
    {Options options;options.MaxPlacementAttempts=attempts;options.TimeLimitSeconds=seconds;return options;}
    struct Solved {bool Complete=false;RectangleStripeStats Stats;std::size_t Checks=0;};
    void Verify(const Model& model,const std::pmr::vector<RectangleBlockPlacement>& output)
    {
        std::vector<int> grid(model.W*model.H,0),counts(model.Types.size());
        const auto occupy=[&](int x,int y,int width,int height)
        {
            Require(x>=0&&y>=0&&width>0&&height>0&&x<=model.W-width&&y<=model.H-height,"placement inside board");
            for(int yy=y;yy<y+height;++yy)for(int xx=x;xx<x+width;++xx)
                Require(++grid[yy*model.W+xx]==1,"fixed and movable rectangles never overlap");
        };
        for(const auto& f:model.Fixed)occupy(f.X,f.Y,f.Width,f.Height);
        for(const auto& p:output)
        {
            const int type=p.Node-1000;Require(type>=0&&type<static_cast<int>(model.Types.size()),"primitive node mapping preserved");
            const auto& t=model.Types[type];++counts[type];occupy(p.X,p.Y,t.Width,t.Height);
        }
        for(std::size_t i=0;i<counts.size();++i)Require(counts[i]==model.Types[i].Quantity,"all original multiplicities conserved");
        Require(std::all_of(grid.begin(),grid.end(),[](int n){return n==1;}),"complete fill covers every exact cell");
    }
    Solved Solve(const Model& model,Options options=Settings(),std::size_t bytes=64*1024*1024,
        RectangleResidualLimits limits={})
    {
        RectangleBlockBudget budget{options};RectangleBlockMemory memory(bytes);
        std::pmr::vector<RectangleBlockPlacement> output(&memory);
        std::vector<int> counts,nodes;
        for(std::size_t i=0;i<model.Types.size();++i){counts.push_back(model.Types[i].Quantity);nodes.push_back(1000+static_cast<int>(i));}
        Solved result;
        result.Complete=SearchRectangleStripeRepair(model.W,model.H,model.Types,counts,nodes,model.Fixed,budget,&memory,limits,output,result.Stats);
        result.Checks=budget.Attempts;
        Require(result.Checks<=options.MaxPlacementAttempts,"one shared check cap covers validation, stripes and exact search");
        Require(result.Stats.Checks==result.Checks,"reported checks include every stage");
        if(result.Complete){Require(result.Stats.Complete&&!result.Stats.Infeasible,"complete has no infeasibility claim");Verify(model,output);}
        else Require(output.empty(),"unsuccessful search publishes no partial placement");
        return result;
    }
    // Independent all-position exact cover. The selected cell may occur at
    // any offset in a rectangle; no skyline, corner or projection rules apply.
    bool Oracle(const Model& model)
    {
        std::vector<int> grid(model.W*model.H),counts;
        for(const auto& type:model.Types)counts.push_back(type.Quantity);
        for(const auto& f:model.Fixed)for(int y=f.Y;y<f.Y+f.Height;++y)for(int x=f.X;x<f.X+f.Width;++x)grid[y*model.W+x]=1;
        const auto dfs=[&](auto&& recurse)->bool
        {
            const auto first=std::find(grid.begin(),grid.end(),0);
            if(first==grid.end())return std::all_of(counts.begin(),counts.end(),[](int n){return n==0;});
            const int cell=static_cast<int>(first-grid.begin()),px=cell%model.W,py=cell/model.W;
            for(std::size_t i=0;i<model.Types.size();++i)if(counts[i])
            {
                const auto& type=model.Types[i];
                for(int y=std::max(0,py-type.Height+1);y<=py&&y<=model.H-type.Height;++y)
                    for(int x=std::max(0,px-type.Width+1);x<=px&&x<=model.W-type.Width;++x)
                    {
                        bool fits=true;
                        for(int yy=y;fits&&yy<y+type.Height;++yy)for(int xx=x;xx<x+type.Width;++xx)
                            if(grid[yy*model.W+xx]){fits=false;break;}
                        if(!fits)continue;
                        for(int yy=y;yy<y+type.Height;++yy)for(int xx=x;xx<x+type.Width;++xx)grid[yy*model.W+xx]=1;
                        --counts[i];if(recurse(recurse))return true;++counts[i];
                        for(int yy=y;yy<y+type.Height;++yy)for(int xx=x;xx<x+type.Width;++xx)grid[yy*model.W+xx]=0;
                    }
            }
            return false;
        };
        return dfs(dfs);
    }
}
int main()
{
    try
    {
        std::mt19937 random(20261029);int oracleTrue=0,oracleFalse=0,stripeRejections=0;
        for(int trial=0;trial<360;++trial)
        {
            Model model{2+static_cast<int>(random()%4),2+static_cast<int>(random()%4)};
            std::map<std::pair<int,int>,int> inventory;int area=0;
            const int desired=1+static_cast<int>(random()%7);
            for(int i=0;i<desired;++i)
            {
                const int w=1+static_cast<int>(random()%std::min(3,model.W)),h=1+static_cast<int>(random()%std::min(3,model.H));
                if(area+w*h>model.W*model.H)continue;++inventory[{w,h}];area+=w*h;
            }
            if(!area){++inventory[{1,1}];area=1;}
            for(const auto& [shape,count]:inventory)model.Types.push_back({shape.first,shape.second,count});
            std::vector<int> cells(model.W*model.H);for(std::size_t i=0;i<cells.size();++i)cells[i]=static_cast<int>(i);
            std::shuffle(cells.begin(),cells.end(),random);
            for(int i=0;i<model.W*model.H-area;++i)model.Fixed.push_back({cells[i]%model.W,cells[i]/model.W,1,1});
            const bool expected=Oracle(model);const auto actual=Solve(model);
            Require(!actual.Stats.InvalidInput&&!actual.Stats.GlobalBudget&&!actual.Stats.MemoryLimit&&
                !actual.Stats.Search.TrialNodeLimit&&!actual.Stats.Search.TrialProbeLimit,"small oracle comparison must finish within limits");
            Require(actual.Complete==expected,"stripe and exact kernel disagree with independent all-position oracle");
            Require(actual.Complete||actual.Stats.Infeasible,"finite exhausted search supplies a scoped infeasibility result");
            if(expected)++oracleTrue;else ++oracleFalse;if(actual.Stats.StripeRejected)++stripeRejections;
        }
        Require(oracleTrue&&oracleFalse&&stripeRejections,"oracle set exercises feasible, infeasible and stripe-rejected problems");

        // Exact local input from the already solved research service fixture.
        // This is not an end-to-end public benchmark or a fresh result claim.
        Model local{34,12,{{1,1,1},{2,1,1},{2,2,1},{2,6,2},{3,2,2},{3,6,1},{4,4,2},
            {5,2,1},{5,5,1},{7,3,1},{8,2,2},{9,2,2},{17,5,1},{19,5,1}},{{33,1,1,11}}};
        RectangleResidualLimits full;full.Nodes=100000000;full.Probes=5000000000ull;
        const auto solved=Solve(local,Settings(10000000000ull,120),128*1024*1024,full);
        Require(solved.Complete,"known 19-piece irregular-space service input must be completely solved");
        std::cout<<"19-piece service nodes="<<solved.Stats.Search.Nodes<<" checks="<<solved.Checks<<'\n';

        Model valid{4,3,{{2,3,1}},{{2,0,2,3}}};Require(Solve(valid).Complete,"fixed obstacle exact fill");
        Model bad=valid;bad.Fixed.push_back({2,0,1,1});const auto overlap=Solve(bad);
        Require(overlap.Stats.InvalidInput&&!overlap.Stats.Infeasible,"overlapping obstacles are invalid, not an infeasibility proof");
        Model wrong=valid;wrong.Fixed.clear();const auto wrongArea=Solve(wrong);
        Require(wrongArea.Stats.InvalidInput&&!wrongArea.Stats.Infeasible,"incorrect total area is invalid input");
        Model wide{512,1,{{128,1,1},{256,1,1}},{{128,0,128,1}}};Require(Solve(wide).Complete,"word-boundary and maximum-axis control");
        Model vertical{1,512,{{1,128,1},{1,256,1}},{{0,128,1,128}}};Require(Solve(vertical).Complete,"transposed word-boundary control");
        Model onlyFixed{3,2,{},{{0,0,3,2}}};Require(Solve(onlyFixed).Complete,"zero remaining inventory is complete only with full fixed occupancy");
        Model duplicate{2,2,{{1,2,1},{1,2,1}},{} };const auto duplicated=Solve(duplicate);
        Require(duplicated.Stats.InvalidInput&&!duplicated.Stats.Infeasible,"duplicate active shapes require canonical input rather than a negative proof");
        Model inactive{2,2,{{512,512,0},{2,2,1}},{} };Require(Solve(inactive).Complete,"inactive global types larger than local region do not affect repair");
        Model tooWide{513,1,{{513,1,1}},{} };const auto unsupported=Solve(tooWide);
        Require(unsupported.Stats.InvalidInput&&!unsupported.Stats.Infeasible,"unsupported large extent is not an infeasibility proof");
        {
            auto options=Settings();RectangleBlockBudget job{options};RectangleBlockMemory memory(1024*1024);
            std::pmr::vector<RectangleBlockPlacement> output(&memory);RectangleStripeStats stats;
            const RectangleBlockType types[]={{1,2,1},{2,2,1}};const int counts[]={1,1},duplicateNodes[]={7,7};
            Require(!SearchRectangleStripeRepair(3,2,types,counts,duplicateNodes,{},job,&memory,{},output,stats)&&
                stats.InvalidInput&&!stats.Infeasible&&output.empty(),"different active types cannot share one primitive reconstruction node");
        }
        for(std::size_t cap:{0u,1u,8u,32u,128u,1024u})
        {
            const auto limited=Solve(local,Settings(cap,30));
            Require(!limited.Complete&&limited.Stats.GlobalBudget&&!limited.Stats.Infeasible,"interruption never becomes negative proof");
        }
        for(std::size_t pulse:{1u,4u,25u})
        {
            std::size_t calls=0;auto options=Settings();options.Cancel=[&]{return ++calls==pulse;};const auto cancelled=Solve(local,options);
            Require(!cancelled.Complete&&cancelled.Stats.Cancelled&&!cancelled.Stats.Infeasible&&calls==pulse,"cancellation is latched across validation and search");
        }
        const auto timed=Solve(local,Settings(100000000,1e-9));
        Require(timed.Stats.GlobalBudget&&!timed.Stats.Infeasible,"expired deadline remains unknown");
        const auto memoryLimited=Solve(local,Settings(),1);
        Require(memoryLimited.Stats.MemoryLimit&&!memoryLimited.Stats.Infeasible,"shared memory cap remains unknown");
        RectangleResidualLimits nodes;nodes.Nodes=1;const auto nodeLimited=Solve(local,Settings(),64*1024*1024,nodes);
        Require(nodeLimited.Stats.Search.TrialNodeLimit&&!nodeLimited.Stats.Infeasible,"local node limit remains unknown");
        std::cout<<"oracle="<<oracleTrue<<" feasible/"<<oracleFalse<<" infeasible; stripeRejected="<<stripeRejections<<"; RectangleStripeRepairTests passed\n";
        return 0;
    }
    catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
