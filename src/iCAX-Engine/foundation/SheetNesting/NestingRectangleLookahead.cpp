#include "NestingInternal.h"
#include "NestingRectangleDirections.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Direction {Pose Offset;double Width,Height;};
    struct Spec {std::size_t Index;double Area;std::vector<Direction> Directions;};
    struct Choice {std::size_t SpecIndex,DirectionIndex;Rect Occupied;Pose Placed;double Short,Long,Area;};
}
Result RunRectangleLookahead(const Problem& p,const Options& options)
{
    const auto begin=std::chrono::steady_clock::now();bool cancelled=false,exhausted=false;
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();};
    Result best;best.State=Status::Partial;
    for(const auto& part:p.Parts)if(part.Quantity)best.Remaining.push_back({part.ID,part.Quantity});
    std::size_t attempts=0,nodes=0,bestPlaced=0,directionsCount=0;double preparation=-1;
    const auto stop=[&]
    {
        if(cancelled||exhausted)return true;
        if(options.Cancel&&options.Cancel()){cancelled=true;return true;}
        if(elapsed()>=options.TimeLimitSeconds||attempts>=options.MaxPlacementAttempts){exhausted=true;return true;}
        return false;
    };
    const auto finish=[&]()
    {
        if(cancelled)best.State=Status::Cancelled;
        best.Stats.PlacementAttempts=attempts;best.Stats.ElapsedSeconds=elapsed();
        best.Stats.CandidatePatterns=directionsCount;best.Stats.PreparationSeconds=preparation;
        best.Stats.PlacementSeconds=preparation<0?-1:std::max(0.0,best.Stats.ElapsedSeconds-preparation);
        if(exhausted)best.Diagnostics.push_back("Rectangle lookahead time or attempt budget exhausted; retained placements are heuristic partial results.");
        best.Diagnostics.push_back("Bounded rectangle lookahead explored "+std::to_string(nodes)+" states; finite candidates do not prove infeasibility.");
        return best;
    };
    if(stop())return finish();
    std::vector<Spec> specs;std::size_t count=0;std::string material;
    for(std::size_t i=0;i<p.Parts.size()&&!stop();++i)if(p.Parts[i].Quantity)
    {
        const auto& part=p.Parts[i];
        if(specs.empty())material=part.Material;
        if(part.Material!=material||part.Quantity>20-count||specs.size()>=8)
        {best.Diagnostics.push_back("Rectangle lookahead scope: one compatible stock, <=20 rectangle instances, <=8 active specifications.");return finish();}
        auto extracted=FindRectangleDirections(part,p.Tolerance,stop);
        if(extracted.Interrupted||stop())return finish();
        if(!extracted.Supported){best.Diagnostics.push_back("Rectangle lookahead requires original rectangle geometry and legal axis-aligned directions.");return finish();}
        Spec spec{i,extracted.Area,{}};
        for(const auto& d:extracted.Directions)
        {
            // Equivalent legal directions have the same conservative footprint.
            if(std::any_of(spec.Directions.begin(),spec.Directions.end(),[&](const auto& old){return std::abs(old.Width-d.Width)<=p.Tolerance&&std::abs(old.Height-d.Height)<=p.Tolerance;}))continue;
            spec.Directions.push_back({d.Offset,d.Width,d.Height});
        }
        if(spec.Directions.empty()){best.Diagnostics.push_back("No supported legal axis-aligned direction for a rectangle specification.");return finish();}
        count+=part.Quantity;specs.push_back(std::move(spec));
    }
    for(const auto& s:specs)directionsCount+=s.Directions.size();preparation=elapsed();
    if(stop())return finish();
    if(!count){best.Remaining.clear();best.State=Status::Complete;return finish();}
    std::vector<std::size_t> stocks(p.Stocks.size());std::iota(stocks.begin(),stocks.end(),0);
    std::stable_sort(stocks.begin(),stocks.end(),[&](auto a,auto b)
    {const auto& x=p.Stocks[a];const auto& y=p.Stocks[b];return std::tuple(x.Width*x.Height,x.ID)<std::tuple(y.Width*y.Height,y.ID);});
    double totalArea=0;for(const auto& s:specs)totalArea+=s.Area*p.Parts[s.Index].Quantity;
    for(auto si:stocks)
    {
        if(stop())break;const auto& stock=p.Stocks[si];
        const double width=stock.Width-2*p.EdgeMargin,height=stock.Height-2*p.EdgeMargin;
        if(!stock.Quantity||stock.Material!=material||width<=p.Tolerance||height<=p.Tolerance||totalArea>width*height+p.Tolerance*(width+height))continue;
        std::vector<std::size_t> remaining;for(const auto& s:specs)remaining.push_back(p.Parts[s.Index].Quantity);
        Layout current{stock.ID,0,stock.Width,stock.Height,{},0};
        std::unordered_set<std::string> visited;
        const auto save=[&](bool complete)
        {
            Result candidate;candidate.State=complete?Status::Complete:Status::Partial;candidate.Layouts={current};
            if(current.Placements.empty())candidate.Layouts.clear();
            for(std::size_t i=0;i<specs.size();++i)if(remaining[i])candidate.Remaining.push_back({p.Parts[specs[i].Index].ID,remaining[i]});
            if(Validate(p,candidate).Valid){best=std::move(candidate);bestPlaced=current.Placements.size();return true;}return false;
        };
        const auto canContinue=[&](const MaxRects& packing)
        {
            for(std::size_t i=0;i<specs.size();++i)if(remaining[i])
            {
                bool fits=false;
                for(const auto& d:specs[i].Directions)for(const auto& free:packing.FreeRectangles())
                    if(d.Width+p.PartGap<=free.Width+p.Tolerance&&d.Height+p.PartGap<=free.Height+p.Tolerance){fits=true;break;}
                if(!fits)return false;
            }
            return true;
        };
        const auto stateKey=[&](const MaxRects& packing)
        {
            std::string key;key.reserve(64+packing.FreeRectangles().size()*32);
            const auto add=[&](auto value){key.append(reinterpret_cast<const char*>(&value),sizeof(value));};
            for(auto n:remaining)add(n);
            auto free=packing.FreeRectangles();std::sort(free.begin(),free.end(),[](const auto& a,const auto& b){return std::tie(a.X,a.Y,a.Width,a.Height)<std::tie(b.X,b.Y,b.Width,b.Height);});
            for(const auto& r:free){add(std::bit_cast<std::uint64_t>(r.X));add(std::bit_cast<std::uint64_t>(r.Y));add(std::bit_cast<std::uint64_t>(r.Width));add(std::bit_cast<std::uint64_t>(r.Height));}
            return key;
        };
        std::function<bool(const MaxRects&)> search=[&](const MaxRects& packing)
        {
            if(stop())return false;++nodes;
            if(current.Placements.size()==count)return save(true);
            if(current.Placements.size()>bestPlaced)save(false);
            if(!canContinue(packing))return false;
            if(visited.size()<12000&&!visited.insert(stateKey(packing)).second)return false;
            std::vector<Choice> choices;
            for(std::size_t i=0;i<specs.size()&&!stop();++i)if(remaining[i])
            {
                std::set<std::tuple<std::size_t,long long,long long>> seen;
                for(std::size_t di=0;di<specs[i].Directions.size()&&!stop();++di)
                {
                    const auto& d=specs[i].Directions[di];const double w=d.Width+p.PartGap,h=d.Height+p.PartGap;
                    for(const auto& free:packing.FreeRectangles())
                    {
                        if(stop())break;if(w>free.Width+p.Tolerance||h>free.Height+p.Tolerance)continue;
                        for(double x:{free.X,free.X+free.Width-w})for(double y:{free.Y,free.Y+free.Height-h})
                        {
                            if(stop())break;
                            if(!seen.emplace(di,std::llround(x/p.Tolerance),std::llround(y/p.Tolerance)).second)continue;
                            ++attempts;
                            Pose pose{x+p.EdgeMargin+d.Offset.X,y+p.EdgeMargin+d.Offset.Y,d.Offset.AngleDegrees};
                            if(!InsideSheet(p.Parts[specs[i].Index].Geometry,pose,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance))continue;
                            choices.push_back({i,di,{x,y,w,h},pose,std::min(free.Width-w,free.Height-h),std::max(free.Width-w,free.Height-h),specs[i].Area});
                        }
                    }
                }
            }
            std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b)
            {return std::tuple(-std::max(a.Occupied.Width,a.Occupied.Height),-a.Area,a.Short,a.Long,a.Occupied.Y,a.Occupied.X,a.SpecIndex,a.DirectionIndex)<
                    std::tuple(-std::max(b.Occupied.Width,b.Occupied.Height),-b.Area,b.Short,b.Long,b.Occupied.Y,b.Occupied.X,b.SpecIndex,b.DirectionIndex);});
            for(const auto& c:choices)
            {
                if(stop())break;
                const auto& s=specs[c.SpecIndex];const auto& part=p.Parts[s.Index];
                const auto instance=part.Quantity-remaining[c.SpecIndex];--remaining[c.SpecIndex];
                current.Placements.push_back({part.ID,instance,c.Placed,current.Placements.size()+1,false});current.MaterialArea+=s.Area;
                auto next=packing;next.Occupy(c.Occupied);
                if(search(next))return true;
                current.MaterialArea-=s.Area;current.Placements.pop_back();++remaining[c.SpecIndex];
            }
            return false;
        };
        if(search(MaxRects(width+p.PartGap,height+p.PartGap,p.Tolerance)))break;
    }
    return finish();
}

}
