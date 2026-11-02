#include "NestingInternal.h"
#include "NestingRectangleDirections.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Node
    {
        std::uint32_t Mask=0;
        double Width=0,Height=0,MaterialArea=0;
        std::size_t Left=0,Right=0,PartIndex=0,Instance=0;
        bool Leaf=true,Horizontal=true;
        Pose OriginalPose;
    };
    struct Work {std::size_t Members,Index;double Area;};
    struct PreferLarge
    {
        bool operator()(const Work& a,const Work& b) const
        {return std::tie(a.Members,a.Area,a.Index)<std::tie(b.Members,b.Area,b.Index);}
    };
    struct StateKey
    {
        std::uint32_t Mask;std::int64_t Width,Height;
        bool operator<(const StateKey& b)const{return std::tie(Mask,Width,Height)<std::tie(b.Mask,b.Width,b.Height);}
    };
}
Result RunRectangleMerge(const Problem& p,const Options& options)
{
    const auto started=std::chrono::steady_clock::now();
    Result result;result.State=Status::Partial;
    for(const auto& a:p.Parts)if(a.Quantity)result.Remaining.push_back({a.ID,a.Quantity});
    bool cancelled=false,exhausted=false;
    std::size_t inventoryChecks=0;
    const auto cheapCheckLimit=options.MaxPlacementAttempts>std::numeric_limits<std::size_t>::max()/32
        ?std::numeric_limits<std::size_t>::max():options.MaxPlacementAttempts*32;
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
    const auto stop=[&]
    {
        if(cancelled||exhausted)return true;
        if(options.Cancel&&options.Cancel()){cancelled=true;return true;}
        if(elapsed()>=options.TimeLimitSeconds||result.Stats.PlacementAttempts>=options.MaxPlacementAttempts||inventoryChecks>=cheapCheckLimit){exhausted=true;return true;}
        return false;
    };
    const auto finish=[&]
    {
        if(cancelled)result.State=Status::Cancelled;
        result.Stats.ElapsedSeconds=elapsed();return result;
    };
    if(stop())return finish();
    if(p.PartGap>0){result.Diagnostics.push_back("Rectangle merge-tree currently requires zero part gap; all demand retained.");return finish();}
    std::vector<std::pair<std::size_t,std::size_t>> instances;
    for(std::size_t i=0;i<p.Parts.size();++i)
    {
        if(stop())return finish();
        if(p.Parts[i].Quantity>20-instances.size()){result.Diagnostics.push_back("Rectangle merge-tree instance limit is 20; all demand retained.");return finish();}
        for(std::size_t k=0;k<p.Parts[i].Quantity;++k)instances.emplace_back(i,k);
    }
    const auto n=instances.size();
    if(n==0){result.State=Status::Complete;return finish();}
    const auto material=p.Parts[instances.front().first].Material;
    for(const auto& a:p.Parts)if(a.Quantity)
    {
        if(stop())return finish();
        if(a.Material!=material||a.Geometry.Radius>0||!a.Geometry.Holes.empty())return finish();
    }
    std::vector<std::size_t> stocks;
    for(std::size_t si=0;si<p.Stocks.size();++si)
        if(p.Stocks[si].Quantity&&p.Stocks[si].Material==material)stocks.push_back(si);
    if(stocks.empty())return finish();
    std::map<std::size_t,RectangleDirectionSet> rectangles;
    for(auto [part,instance]:instances)if(instance==0)
    {
        auto directions=FindRectangleDirections(p.Parts[part],p.Tolerance,stop);
        if(directions.Interrupted)return finish();
        if(!directions.Supported){result.Diagnostics.push_back("Rectangle merge-tree found no permitted axial rectangle description; all demand retained.");return finish();}
        rectangles.emplace(part,std::move(directions));
    }
    const auto fitsAnyStock=[&](double w,double h)
    {
        for(auto si:stocks)if(w<=p.Stocks[si].Width-2*p.EdgeMargin+p.Tolerance&&h<=p.Stocks[si].Height-2*p.EdgeMargin+p.Tolerance)return true;
        return false;
    };
    const std::uint32_t full=(std::uint32_t{1}<<n)-1;
    std::map<std::size_t,std::uint32_t> instanceBits;
    for(std::size_t i=0;i<n;++i)instanceBits[instances[i].first]|=std::uint32_t{1}<<i;
    const auto mergeMasks=[&](std::uint32_t a,std::uint32_t b)->std::uint32_t
    {
        std::uint32_t merged=0;
        for(auto [part,bits]:instanceBits)
        {
            const auto count=static_cast<std::size_t>(std::popcount(a&bits)+std::popcount(b&bits));
            if(count>p.Parts[part].Quantity)return 0;
            // Instances of one Part have identical geometry and constraints.
            // Canonical prefix bits remove permutation-only duplicate states.
            if(count)merged|=((std::uint32_t{1}<<count)-1)<<std::countr_zero(bits);
        }
        return merged;
    };
    std::vector<Node> nodes;nodes.reserve(8192);
    std::map<StateKey,std::size_t> seen;
    std::unordered_map<std::int64_t,std::vector<std::size_t>> byWidth,byHeight;
    std::priority_queue<Work,std::vector<Work>,PreferLarge> pending;
    const auto quantum=[&](double value){return std::llround(value/p.Tolerance);};
    const std::size_t stateLimit=50000; // Job-local storage guard, independent of complex motif settings.
    bool found=false;
    std::map<std::size_t,std::size_t> assignedInstances;
    const auto reconstruct=[&](auto&& self,std::size_t index,double x,double y,Layout& l)->void
    {
        const auto& node=nodes[index];
        if(node.Leaf)
        {
            auto pose=node.OriginalPose;pose.X+=x;pose.Y+=y;
            l.Placements.push_back({p.Parts[node.PartIndex].ID,assignedInstances[node.PartIndex]++,pose,1,false});return;
        }
        self(self,node.Left,x,y,l);
        const auto& left=nodes[node.Left];
        self(self,node.Right,x+(node.Horizontal?left.Width:0),y+(node.Horizontal?0:left.Height),l);
    };
    const auto add=[&](Node node)
    {
        if(found||stop()||!fitsAnyStock(node.Width,node.Height)||nodes.size()>=stateLimit)return;
        const StateKey key{node.Mask,quantum(node.Width),quantum(node.Height)};
        if(!seen.emplace(key,nodes.size()).second)return;
        const auto index=nodes.size();nodes.push_back(std::move(node));
        const auto current=nodes[index];
        if(current.Mask==full)
        {
            for(auto si:stocks)
            {
                const auto& stock=p.Stocks[si];
                if(current.Width>stock.Width-2*p.EdgeMargin+p.Tolerance||current.Height>stock.Height-2*p.EdgeMargin+p.Tolerance)continue;
                auto candidate=result;candidate.State=Status::Complete;candidate.Remaining.clear();
                Layout layout{stock.ID,0,stock.Width,stock.Height,{},current.MaterialArea};
                assignedInstances.clear();reconstruct(reconstruct,index,p.EdgeMargin,p.EdgeMargin,layout);candidate.Layouts.push_back(std::move(layout));
                if(Validate(p,candidate).Valid){result=std::move(candidate);found=true;return;}
            }
        }
        byWidth[key.Width].push_back(index);byHeight[key.Height].push_back(index);
        pending.push({static_cast<std::size_t>(std::popcount(current.Mask)),index,current.MaterialArea});
    };
    for(std::size_t i=0;i<n&&!stop();++i)
    {
        if(instances[i].second)continue;
        const auto& rectangle=rectangles.at(instances[i].first);
        for(const auto& direction:rectangle.Directions)
        {
            if(stop())break;
            add({std::uint32_t{1}<<std::countr_zero(instanceBits.at(instances[i].first)),direction.Width,direction.Height,rectangle.Area,
                0,0,instances[i].first,0,true,true,direction.Offset});
        }
    }
    result.Stats.PreparationSeconds=elapsed();
    while(!pending.empty()&&!found&&!stop())
    {
        const auto index=pending.top().Index;pending.pop();const auto current=nodes[index];
        for(int horizontal:{1,0})
        {
            if(stop()||found)break;
            std::vector<std::size_t> candidates;
            const auto key=quantum(horizontal?current.Height:current.Width);
            const auto& dimensions=horizontal?byHeight:byWidth;
            for(int delta:{-1,0,1})
            {
                const auto it=dimensions.find(key+delta);
                if(it!=dimensions.end())for(auto other:it->second)if(other<=index)candidates.push_back(other);
            }
            std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b)
            {return std::popcount(nodes[a].Mask)>std::popcount(nodes[b].Mask);});
            for(auto other:candidates)
            {
                if(stop()||found)break;++inventoryChecks;
                const auto partner=nodes[other];
                const auto combined=mergeMasks(current.Mask,partner.Mask);
                if(!combined)continue;
                ++result.Stats.PlacementAttempts;
                if(std::abs((horizontal?current.Height:current.Width)-(horizontal?partner.Height:partner.Width))>p.Tolerance)continue;
                const double width=horizontal?current.Width+partner.Width:std::max(current.Width,partner.Width);
                const double height=horizontal?std::max(current.Height,partner.Height):current.Height+partner.Height;
                add({combined,width,height,current.MaterialArea+partner.MaterialArea,index,other,0,0,false,horizontal!=0,{}});
            }
        }
    }
    result.Stats.CandidatePatterns=nodes.size();result.Stats.CombinationGroups=found&&n>1?1:0;
    result.Stats.PlacementSeconds=std::max(0.0,elapsed()-result.Stats.PreparationSeconds);
    result.Diagnostics.push_back("Bounded rectangle merge closure stored "+std::to_string(nodes.size())+" subset-dimension states, "+std::to_string(inventoryChecks)+" bounded inventory-index checks; no infeasibility proof.");
    return finish();
}

}
