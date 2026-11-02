#include "SheetNestingPipeline.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t MaxLayouts=4,MaxParts=200,MaxSpecifications=2048,MaxStocks=64,
        MaxGroup=8,MaxTrials=256,MaxVertices=16000;
    using Placements=std::vector<Placement>;
    using PartMap=std::map<std::string,const Part*>;

    struct Bounds { double X0,Y0,X1,Y1; };
    Bounds GroupBounds(const Placements& group,const PartMap& parts)
    {
        Bounds b{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
        for(const auto& p:group)
        {
            const auto r=Detail::Bounds(parts.at(p.PartID)->Geometry,p.Transform);
            b.X0=std::min(b.X0,r.X);b.Y0=std::min(b.Y0,r.Y);
            b.X1=std::max(b.X1,r.X+r.Width);b.Y1=std::max(b.Y1,r.Y+r.Height);
        }
        return b;
    }
    void Shift(Placements& group,double dx,double dy)
    {
        for(auto& p:group){p.Transform.X+=dx;p.Transform.Y+=dy;p.Group=0;}
    }
    // Group zero represents individual placements, not a shared assembly.
    std::vector<Placements> Groups(const Placements& placements)
    {
        std::vector<Placements> groups;std::map<std::size_t,std::size_t> indices;
        for(const auto& p:placements)
        {
            if(!p.Group){groups.push_back({p});continue;}
            const auto [it,inserted]=indices.emplace(p.Group,groups.size());
            if(inserted)groups.emplace_back();groups[it->second].push_back(p);
        }
        return groups;
    }
    Placements Others(const std::vector<Placements>& groups,std::size_t excluded)
    {
        Placements out;for(std::size_t i=0;i<groups.size();++i)if(i!=excluded)
            out.insert(out.end(),groups[i].begin(),groups[i].end());
        return out;
    }
    class CircleRelocation final:public IPostProcessor
    {
    public:
        Result Run(const Problem& input,const Result& incumbent,const Options& options) override
        {
            const auto started=Clock::now();Result kept=incumbent;Statistics work;
            work.VerificationSeconds=work.EvaluationSeconds=0;
            std::size_t trials=0;bool cancelled=false;std::string reason="finite-candidates-exhausted";
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]
            {
                if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
                return cancelled||elapsed()>=options.TimeLimitSeconds||
                    work.PlacementAttempts>=options.MaxPlacementAttempts||trials>=MaxTrials;
            };
            const auto finish=[&]
            {
                if(cancelled)reason="cancelled";
                else if(elapsed()>=options.TimeLimitSeconds)reason="time-limit";
                else if(work.PlacementAttempts>=options.MaxPlacementAttempts)reason="attempt-limit";
                else if(trials>=MaxTrials)reason="trial-limit";
                const auto origins=kept.Stats;kept.Stats=work;
                kept.Stats.CombinationGroups=origins.CombinationGroups;
                kept.Stats.CircleLatticeGroups=origins.CircleLatticeGroups;
                kept.Stats.GapFilledParts=origins.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();
                if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("circle.relocate: "+reason+"; trials="+std::to_string(trials));
                return kept;
            };
            if(stop())return finish();
            if(incumbent.State!=Status::Complete||!incumbent.GeometryValidated||!incumbent.Remaining.empty())
            {reason="requires-validated-complete-incumbent";return finish();}
            if(input.PartGap<=0||incumbent.Layouts.size()<2||incumbent.Layouts.size()>MaxLayouts)
            {reason="gap-or-layout-scope";return finish();}
            if(input.Parts.size()>MaxSpecifications||input.Stocks.size()>MaxStocks)
            {reason="specification-or-stock-scope";return finish();}
            PartMap parts;std::map<std::string,const Stock*> stocks;
            std::size_t vertices=0,total=0;
            for(const auto& part:input.Parts)
            {
                if(stop())return finish();parts.emplace(part.ID,&part);
                const auto add=[&](std::size_t n){if(n>MaxVertices-vertices)return false;vertices+=n;return true;};
                if(!add(part.Geometry.Outer.size())){reason="vertex-scope";return finish();}
                for(const auto& hole:part.Geometry.Holes)if(!add(hole.size())){reason="vertex-scope";return finish();}
            }
            for(const auto& stock:input.Stocks)stocks.emplace(stock.ID,&stock);
            std::set<std::string> materials;
            std::size_t worldVertices=0;
            for(const auto& l:incumbent.Layouts)
            {
                if(stop())return finish();auto it=stocks.find(l.StockID);
                if(it==stocks.end()||l.Placements.size()>MaxParts-total){reason="input-scope";return finish();}
                total+=l.Placements.size();materials.insert(it->second->Material);
                for(const auto& p:l.Placements)
                {
                    auto part=parts.find(p.PartID);if(part==parts.end()){reason="unknown-part";return finish();}
                    std::size_t count=part->second->Geometry.Outer.size();
                    for(const auto& hole:part->second->Geometry.Holes)count+=hole.size();
                    if(count>MaxVertices-worldVertices){reason="placed-vertex-scope";return finish();}
                    worldVertices+=count;
                }
            }
            if(materials.size()!=1){reason="material-scope";return finish();}
            const auto verifyStart=Clock::now();const auto initial=Validate(input,incumbent);
            work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-verifyStart).count();
            if(!initial.Valid||!initial.Complete){reason="incumbent-validation-failed";return finish();}
            if(stop())return finish();
            const double gap=input.PartGap,tol=input.Tolerance,margin=input.EdgeMargin;
            const auto fits=[&](const Placements& moving,const Placements& fixed,const Layout& layout)
            {
                for(const auto& a:moving)
                {
                    if(stop())return false;++work.PlacementAttempts;
                    const auto& part=*parts.at(a.PartID);
                    if(!Detail::InsideSheet(part.Geometry,a.Transform,layout.Width,layout.Height,margin,tol))return false;
                    for(const auto& b:fixed)
                    {
                        if(stop())return false;
                        if(Detail::ShapesConflict(part.Geometry,a.Transform,parts.at(b.PartID)->Geometry,b.Transform,gap,tol))return false;
                    }
                }
                return true;
            };
            const auto topArea=[&](const Result& r)
            {
                double best=0;
                for(const auto& l:r.Layouts)if(!l.Placements.empty())
                {
                    const auto b=GroupBounds(l.Placements,parts);
                    best=std::max(best,std::max(0.,l.Width-2*margin)*std::max(0.,l.Height-margin-b.Y1-gap-2*tol));
                }
                return best;
            };
            const auto compact=[&](Placements& placements,const Layout& layout)
            {
                auto groups=Groups(placements);
                std::vector<std::size_t> order(groups.size());std::iota(order.begin(),order.end(),0);
                std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
                    {return GroupBounds(groups[a],parts).Y0<GroupBounds(groups[b],parts).Y0;});
                for(auto i:order)
                {
                    if(stop())return false;
                    const auto b=GroupBounds(groups[i],parts);const auto other=Others(groups,i);
                    std::vector<double> ys{margin};
                    for(const auto& q:other)
                    {
                        if(stop())return false;const auto r=Detail::Bounds(parts.at(q.PartID)->Geometry,q.Transform);
                        if(r.X+r.Width+gap>b.X0&&r.X-gap<b.X1)ys.push_back(r.Y+r.Height+gap+2*tol);
                    }
                    std::sort(ys.begin(),ys.end());
                    for(double y:ys)if(y<b.Y0-tol)
                    {
                        auto moved=groups[i];Shift(moved,0,y-b.Y0);
                        if(fits(moved,other,layout)){groups[i]=std::move(moved);break;}
                        if(stop())return false;
                    }
                }
                placements.clear();for(auto& group:groups)placements.insert(placements.end(),group.begin(),group.end());
                return true;
            };
            double bestArea=topArea(incumbent);
            for(std::size_t ti=0;ti<incumbent.Layouts.size()&&!stop();++ti)
            {
                const auto& original=incumbent.Layouts[ti];
                std::map<std::string,Placements> circles;
                for(const auto& p:original.Placements)if(parts.at(p.PartID)->Geometry.Radius>0)circles[p.PartID].push_back(p);
                for(const auto& [id,cluster]:circles)
                {
                    if(stop())break;if(cluster.size()<2)continue;
                    const double radius=parts.at(id)->Geometry.Radius,step=2*radius+gap+2*tol;
                    if((cluster.size()-1)*step+2*radius>original.Height-2*margin)continue;
                    for(int side=0;side<2&&!stop();++side)
                    {
                        auto layout=original;Placements remainder;
                        for(const auto& p:original.Placements)if(p.PartID!=id)remainder.push_back(p);
                        if(remainder.empty())continue;
                        const auto bounds=GroupBounds(remainder,parts);Shift(remainder,0,std::min(0.,margin-bounds.Y0));
                        auto column=cluster;
                        for(std::size_t i=0;i<column.size();++i)
                        {
                            auto& p=column[i];const auto center=Detail::Transform(parts.at(id)->Geometry.Center,p.Transform);
                            const double x=side==0?margin+radius:original.Width-margin-radius;
                            p.Transform.X+=x-center.X;p.Transform.Y+=margin+radius+i*step-center.Y;p.Group=0;
                        }
                        if(!fits(column,remainder,layout))continue;
                        bool columnValid=true;Placements preceding;
                        for(const auto& p:column)
                        {if(!fits(Placements{p},preceding,layout)){columnValid=false;break;}preceding.push_back(p);}
                        if(!columnValid||stop())continue;
                        layout.Placements=std::move(remainder);
                        layout.Placements.insert(layout.Placements.end(),column.begin(),column.end());
                        const double targetTop=GroupBounds(layout.Placements,parts).Y1;
                        for(std::size_t si=0;si<incumbent.Layouts.size()&&!stop();++si)if(si!=ti)
                        {
                            const auto& originalSource=incumbent.Layouts[si];auto groups=Groups(originalSource.Placements);
                            for(std::size_t gi=0;gi<groups.size()&&!stop();++gi)
                            {
                                const auto& group=groups[gi];
                                if(group.size()>MaxGroup||group.size()==originalSource.Placements.size())continue;
                                const auto b=GroupBounds(group,parts);
                                for(int anchor=0;anchor<2&&!stop();++anchor)
                                {
                                    ++trials;++work.CandidatePatterns;auto moving=group;
                                    const double x=anchor==0?margin:layout.Width-margin-(b.X1-b.X0);
                                    Shift(moving,x-b.X0,targetTop+gap+2*tol-b.Y0);
                                    if(!fits(moving,layout.Placements,layout))continue;
                                    auto proposal=incumbent;proposal.Layouts[ti]=layout;
                                    auto& dst=proposal.Layouts[ti];dst.Placements.insert(dst.Placements.end(),moving.begin(),moving.end());
                                    auto& src=proposal.Layouts[si];src.Placements=Others(groups,gi);
                                    if(!compact(src.Placements,src)||stop())continue;
                                    const double area=topArea(proposal);if(area<=bestArea+tol*tol)continue;
                                    for(auto& l:proposal.Layouts)
                                    {l.MaterialArea=0;for(const auto& p:l.Placements)l.MaterialArea+=Detail::Area(parts.at(p.PartID)->Geometry);}
                                    proposal.GeometryValidated=false;const auto t=Clock::now();const auto check=Validate(input,proposal);
                                    work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-t).count();
                                    if(stop())continue;
                                    if(check.Valid&&check.Complete){proposal.GeometryValidated=true;kept=std::move(proposal);bestArea=area;}
                                }
                            }
                        }
                    }
                }
            }
            return finish();
        }
    };
}
PostProcessStage MakeCircleRelocationStage()
{return {"circle.relocate",[]{return std::make_unique<CircleRelocation>();}};}
}
