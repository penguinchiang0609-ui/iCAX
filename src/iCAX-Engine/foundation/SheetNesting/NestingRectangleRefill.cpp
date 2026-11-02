#include "SheetNestingPipeline.h"
#include "NestingRectangleDirections.h"
#include <chrono>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    // Current operation scope, not a statement of solver capability or optimality.
    constexpr std::size_t MaxLayouts=32,MaxPlacements=512,MaxPartSpecs=2048,MaxStockTypes=64;
    constexpr std::size_t MaxDonorParts=64,MaxDonorVertices=128,MaxAllowedAngles=32,MaxDirections=8;
    constexpr std::size_t MaxWorldVertices=32768,MaxFreeSpaces=256,MaxTrials=256,MaxAttempts=20000;
    using Clock=std::chrono::steady_clock;

    class RectangleRefill final:public IPostProcessor
    {
    public:
        Result Run(const Problem& input,const Result& incumbent,const Options& options) override
        {
            const auto started=Clock::now();Result kept=incumbent;Statistics work;
            work.VerificationSeconds=work.EvaluationSeconds=0;
            std::size_t trials=0,removed=0;bool cancelled=false,freeSpaceLimited=false;
            std::string reason="finite-candidates-exhausted";
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto pollCancel=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled;};
            const auto interrupted=[&]{return pollCancel()||elapsed()>=options.TimeLimitSeconds;};
            const auto stop=[&]{return interrupted()||work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts)||trials>=MaxTrials;};
            const auto finish=[&]
            {
                if(reason=="finite-candidates-exhausted"||reason=="cancelled-or-budget")
                {
                    if(cancelled)reason="cancelled";
                    else if(elapsed()>=options.TimeLimitSeconds)reason="time-limit";
                    else if(work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts))reason="attempt-limit";
                    else if(trials>=MaxTrials)reason="trial-limit";
                    else if(freeSpaceLimited)reason="free-space-limit";
                }
                const auto layout=kept.Stats;kept.Stats=work;
                kept.Stats.CombinationGroups=layout.CombinationGroups;
                kept.Stats.CircleLatticeGroups=layout.CircleLatticeGroups;
                kept.Stats.GapFilledParts=layout.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();
                if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("rect.refill: "+reason+"; removed="+std::to_string(removed)+
                    "; trials="+std::to_string(trials)+(freeSpaceLimited?"; free-space-cap reached":""));
                return std::move(kept);
            };
            // Pipeline assessment precedes the stage. Do not repair caches or scan geometry on these exits.
            if(stop()){reason="cancelled-or-budget";return finish();}
            if(incumbent.State!=Status::Complete||!incumbent.GeometryValidated||!incumbent.Remaining.empty())
            {reason="requires-validated-complete-incumbent";return finish();}
            if(incumbent.Layouts.size()<2){reason="one-or-zero-used-sheets";return finish();}
            if(incumbent.Layouts.size()>MaxLayouts||input.Parts.size()>MaxPartSpecs||input.Stocks.size()>MaxStockTypes)
            {reason="layout-part-stock-scope-cap";return finish();}
            std::size_t placed=0;
            for(const auto& sheet:incumbent.Layouts)
            {
                if(interrupted())return finish();
                if(sheet.Width-2*input.EdgeMargin<=input.Tolerance||sheet.Height-2*input.EdgeMargin<=input.Tolerance)
                {reason="empty-usable-sheet-scope";return finish();}
                if(sheet.Placements.size()>MaxPlacements-placed){reason="placement-scope-cap";return finish();}
                placed+=sheet.Placements.size();
            }
            struct PartData{const Part* Original=nullptr;std::size_t Vertices=0;double Area=0;Detail::RectangleDirectionSet Rectangle;};
            std::map<std::string,PartData> parts;
            for(const auto& part:input.Parts)if(part.Quantity)
            {
                if(interrupted())return finish();
                std::size_t count=part.Geometry.Outer.size();
                if(count>MaxWorldVertices){reason="geometry-vertex-cap";return finish();}
                for(const auto& hole:part.Geometry.Holes)
                {
                    if(interrupted())return finish();
                    if(hole.size()>MaxWorldVertices-count){reason="geometry-vertex-cap";return finish();}
                    count+=hole.size();
                }
                parts.emplace(part.ID,PartData{&part,count});
            }
            std::size_t worldVertices=0;
            for(const auto& sheet:incumbent.Layouts)for(const auto& placement:sheet.Placements)
            {
                if(interrupted())return finish();const auto n=parts.at(placement.PartID).Vertices;
                if(n>MaxWorldVertices-worldVertices){reason="placed-vertex-cap";return finish();}worldVertices+=n;
            }
            for(auto& [id,data]:parts)
            {
                if(interrupted())return finish();const auto& part=*data.Original;data.Area=Detail::Area(part.Geometry);
                if(part.Geometry.Radius==0&&part.Geometry.Holes.empty()&&part.Geometry.Outer.size()<=MaxDonorVertices&&part.AllowedAnglesDegrees.size()<=MaxAllowedAngles)
                {
                    data.Rectangle=Detail::FindRectangleDirections(part,input.Tolerance,interrupted);
                    if(data.Rectangle.Directions.size()>MaxDirections)data.Rectangle.Directions.resize(MaxDirections);
                }
            }
            std::map<std::string,const Stock*> stocks;for(const auto& stock:input.Stocks)stocks[stock.ID]=&stock;
            while(kept.Layouts.size()>1&&!stop())
            {
                std::vector<std::size_t> donors(kept.Layouts.size());std::iota(donors.begin(),donors.end(),0);
                const auto area=[&](const Layout& l){double a=0;for(const auto& p:l.Placements)a+=parts.at(p.PartID).Area;return a;};
                std::stable_sort(donors.begin(),donors.end(),[&](auto a,auto b)
                {const auto& x=kept.Layouts[a];const auto& y=kept.Layouts[b];return area(x)/((x.Width-2*input.EdgeMargin)*(x.Height-2*input.EdgeMargin))<area(y)/((y.Width-2*input.EdgeMargin)*(y.Height-2*input.EdgeMargin));});
                bool reduced=false;
                for(auto donor:donors)
                {
                    if(stop()||reduced)break;const auto source=kept.Layouts[donor];
                    if(source.Placements.empty()||source.Placements.size()>MaxDonorParts||
                       std::any_of(source.Placements.begin(),source.Placements.end(),[&](const auto& a){return !parts.at(a.PartID).Rectangle.Supported;}))continue;
                    const auto material=stocks.at(source.StockID)->Material;
                    std::set<std::vector<std::size_t>> distinct;std::vector<std::vector<std::size_t>> orders;
                    for(int mode=0;mode<3;++mode)
                    {
                        std::vector<std::size_t> order(source.Placements.size());std::iota(order.begin(),order.end(),0);
                        if(mode)std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
                        {const auto& x=parts.at(source.Placements[a].PartID);const auto& y=parts.at(source.Placements[b].PartID);return mode==1?x.Area>y.Area:std::max(x.Rectangle.Directions.front().Width,x.Rectangle.Directions.front().Height)>std::max(y.Rectangle.Directions.front().Width,y.Rectangle.Directions.front().Height);});
                        if(distinct.insert(order).second)orders.push_back(std::move(order));
                    }
                    for(const auto& order:orders)for(int score=0;score<2&&!stop()&&!reduced;++score)
                    {
                        ++trials;++work.CandidatePatterns;auto proposal=kept;
                        struct Bin{std::size_t Index;Detail::MaxRects Packing;Bin(std::size_t i,double w,double h,double t):Index(i),Packing(w,h,t){}};
                        std::vector<Bin> bins;
                        const double scale=std::max(source.Width,source.Height);
                        const double strictTol=std::max(16*std::numeric_limits<double>::epsilon()*scale,std::min(input.Tolerance/1024,scale*1e-13));
                        bool complete=true;
                        for(std::size_t i=0;i<proposal.Layouts.size()&&complete;++i)if(i!=donor&&stocks.at(proposal.Layouts[i].StockID)->Material==material)
                        {
                            if(interrupted()){complete=false;break;}const auto& target=proposal.Layouts[i];
                            bins.emplace_back(i,target.Width-2*input.EdgeMargin+input.PartGap,target.Height-2*input.EdgeMargin+input.PartGap,strictTol);
                            for(const auto& fixed:target.Placements)
                            {
                                if(interrupted()){complete=false;break;}
                                const auto b=Detail::Bounds(parts.at(fixed.PartID).Original->Geometry,fixed.Transform);
                                // Single-sided growth reserves a gap in either relative ordering.
                                bins.back().Packing.Occupy({b.X-input.EdgeMargin,b.Y-input.EdgeMargin,b.Width+input.PartGap,b.Height+input.PartGap});
                                if(bins.back().Packing.FreeRectangles().size()>MaxFreeSpaces){freeSpaceLimited=true;complete=false;break;}
                            }
                        }
                        for(auto index:order)
                        {
                            if(!complete||stop()){complete=false;break;}const auto moving=source.Placements[index];const auto& part=*parts.at(moving.PartID).Original;
                            struct Fit{std::size_t BinIndex;Rect Box;Pose Transform;std::tuple<double,double,double,double,std::size_t> Rank;};std::optional<Fit> selected;
                            for(std::size_t bi=0;bi<bins.size()&&!stop();++bi)
                            {
                                const auto& target=proposal.Layouts[bins[bi].Index];
                                for(const auto& d:parts.at(moving.PartID).Rectangle.Directions)
                                {
                                    if(stop())break;const double w=d.Width+input.PartGap,h=d.Height+input.PartGap;
                                    for(const auto& free:bins[bi].Packing.FreeRectangles())
                                    {
                                        if(stop())break;if(w>free.Width+strictTol||h>free.Height+strictTol)continue;
                                        const double dx=free.Width-w,dy=free.Height-h;
                                        const auto rank=std::tuple(score==0?std::min(dx,dy):free.Width*free.Height-w*h,score==0?std::max(dx,dy):std::min(dx,dy),free.Y,free.X,bi);
                                        if(selected&&!(rank<selected->Rank))continue;
                                        ++work.PlacementAttempts;Pose pose=d.Offset;pose.X+=input.EdgeMargin+free.X;pose.Y+=input.EdgeMargin+free.Y;
                                        if(!Detail::AngleAllowed(part,pose.AngleDegrees,input.Tolerance)||!Detail::InsideSheet(part.Geometry,pose,target.Width,target.Height,input.EdgeMargin,strictTol))continue;
                                        bool fits=true;
                                        for(const auto& fixed:target.Placements)
                                        {
                                            if(interrupted()||Detail::ShapesConflict(part.Geometry,pose,parts.at(fixed.PartID).Original->Geometry,fixed.Transform,input.PartGap,strictTol)){fits=false;break;}
                                        }
                                        if(fits)selected=Fit{bi,{free.X,free.Y,w,h},pose,rank};
                                    }
                                }
                            }
                            if(!selected||cancelled){complete=false;break;}
                            auto moved=moving;moved.Transform=selected->Transform;moved.Group=0;proposal.Layouts[bins[selected->BinIndex].Index].Placements.push_back(moved);
                            bins[selected->BinIndex].Packing.Occupy(selected->Box);
                            if(bins[selected->BinIndex].Packing.FreeRectangles().size()>MaxFreeSpaces){freeSpaceLimited=true;complete=false;break;}
                        }
                        if(!complete||interrupted())continue;
                        proposal.Layouts.erase(proposal.Layouts.begin()+static_cast<std::ptrdiff_t>(donor));
                        // Group is a planning tag, not a rigid constraint. Moved singles are detached;
                        // untouched target tags and construction-origin statistics remain exact.
                        for(auto& layout:proposal.Layouts)
                        {
                            layout.MaterialArea=0;for(const auto& p:layout.Placements)layout.MaterialArea+=parts.at(p.PartID).Area;
                        }
                        proposal.GeometryValidated=false;const auto verify=Clock::now();const auto checked=Validate(input,proposal);
                        work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-verify).count();
                        if(pollCancel())continue;
                        // Complete and exactly one fewer board strictly improves every shared Ranking policy.
                        if(checked.Valid&&checked.Complete&&proposal.Layouts.size()+1==kept.Layouts.size())
                        {proposal.GeometryValidated=true;kept=std::move(proposal);++removed;reduced=true;}
                    }
                }
                if(!reduced)break;
            }
            pollCancel();return finish();
        }
    };
}

PostProcessStage MakeRectangleRefillStage()
{
    return {"rect.refill",[]{return std::make_unique<RectangleRefill>();}};
}
}
