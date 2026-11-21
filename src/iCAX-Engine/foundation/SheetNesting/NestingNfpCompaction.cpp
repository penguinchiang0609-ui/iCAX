#include "SheetNestingCompaction.h"
#include "SheetNestingNfp.h"
#include "SheetNestingDomainRepair.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    double Cross(Point a,Point b){return a.X*b.Y-a.Y*b.X;}
    double Primary(Point p,CompactionAxis a){return a==CompactionAxis::X?p.X:p.Y;}
    bool Overlap(Rect a,Rect b,double gap)
    {return a.X+a.Width+gap>=b.X&&b.X+b.Width+gap>=a.X&&a.Y+a.Height+gap>=b.Y&&b.Y+b.Height+gap>=a.Y;}

    class NfpCompaction final:public IPostProcessor
    {
        NfpCompactionSettings Settings;
        std::shared_ptr<NfpCache> Shared;
    public:
        NfpCompaction(NfpCompactionSettings settings,std::shared_ptr<NfpCache> cache)
            :Settings(settings),Shared(std::move(cache)){}
        Result Run(const Problem& p,const Result& source,const Options& o) override
        {
            const auto start=Clock::now();Result result=source;Statistics work;
            std::size_t accepted=0,groupMoves=0,rays=0,inserted=0;bool cancelled=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-start).count();};
            const auto pollCancel=[&]{if(!cancelled&&o.Cancel&&o.Cancel())cancelled=true;return cancelled;};
            const auto globalStop=[&]{return pollCancel()||elapsed()>=o.TimeLimitSeconds||work.PlacementAttempts>=o.MaxPlacementAttempts;};
            const double compactSeconds=o.TimeLimitSeconds*(Settings.RefillMissing?2./3.:1.);
            const auto compactAttempts=Settings.RefillMissing?o.MaxPlacementAttempts-o.MaxPlacementAttempts/3:o.MaxPlacementAttempts;
            const auto stop=[&]{return globalStop()||elapsed()>=compactSeconds||work.PlacementAttempts>=compactAttempts;};
            const auto finish=[&](const std::string& why)
            {
                if(Settings.RefillMissing&&!result.Remaining.empty()&&!globalStop()&&Validate(p,result).Valid)
                {
                    auto settings=o;settings.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());
                    settings.MaxPlacementAttempts=o.MaxPlacementAttempts-work.PlacementAttempts;
                    settings.Cancel=pollCancel;
                    auto repaired=MakeDomainRepairStage().Factory()->Run(p,result,settings);
                    work.PlacementAttempts+=repaired.Stats.PlacementAttempts;
                    const auto check=Validate(p,repaired);std::size_t oldCount=0;
                    for(const auto& layout:result.Layouts)oldCount+=layout.Placements.size();
                    if(check.Valid&&check.PlacedCount>=oldCount){inserted=check.PlacedCount-oldCount;result=std::move(repaired);}
                }
                if((accepted||inserted)&&!Validate(p,result).Valid){result=source;accepted=groupMoves=inserted=0;
                    result.Diagnostics.push_back("compact.nfp: original validation rejected the refinement");}
                const auto original=result.Stats;result.Stats=work;
                result.Stats.CombinationGroups=original.CombinationGroups;
                result.Stats.CircleLatticeGroups=original.CircleLatticeGroups;
                result.Stats.GapFilledParts=original.GapFilledParts;
                result.Stats.ElapsedSeconds=result.Stats.PostProcessingSeconds=elapsed();
                if(cancelled)result.State=Status::Cancelled;
                result.Diagnostics.push_back("compact.nfp: "+why+"; moves="+std::to_string(accepted)+
                    ", group moves="+std::to_string(groupMoves)+", inserted="+std::to_string(inserted)+", rays="+std::to_string(rays));
                return result;
            };
            if(stop())return finish("budget");
            if(p.PartGap!=0||source.Layouts.empty()||source.Layouts.size()>8||p.Parts.size()>128)
                return finish("zero-gap polygon layout scope");
            std::size_t count=0;
            for(const auto& layout:source.Layouts)count+=layout.Placements.size();
            if(count>128||!Validate(p,source).Valid)return finish("unsupported or invalid incumbent");
            std::map<std::string,std::size_t> index;
            for(std::size_t i=0;i<p.Parts.size();++i)
            {
                const auto& g=p.Parts[i].Geometry;
                if(g.Radius!=0||!g.Holes.empty()||g.Outer.size()>128)return finish("simple polygon scope");
                index.emplace(p.Parts[i].ID,i);
            }
            auto cache=Shared;
            if(!cache)cache=std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024});
            std::vector<NfpPreparedShape> prepared(p.Parts.size());
            for(std::size_t i=0;i<p.Parts.size()&&!stop();++i)
            {
                NfpBuildOptions options;options.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());options.Cancel=stop;
                prepared[i]=cache->Prepare(p.Parts[i].Geometry,options);
                if(!prepared[i])return finish("NFP source unsupported or interrupted");
            }
            if(stop())return finish("budget");
            const double moveTolerance=std::max(p.Tolerance*4,1e-9);
            for(std::size_t pass=0;pass<Settings.MaxPasses&&!stop();++pass)
            {
                bool changed=false;
                for(auto& layout:result.Layouts)
                {
                    if(stop())break;
                    const auto n=layout.Placements.size();
                    std::vector<std::size_t> part(n);std::vector<Rect> bounds(n);
                    for(std::size_t i=0;i<n;++i){part[i]=index.at(layout.Placements[i].PartID);
                        bounds[i]=Detail::Bounds(p.Parts[part[i]].Geometry,layout.Placements[i].Transform);}
                    std::vector<std::vector<std::size_t>> groups;std::set<std::vector<std::size_t>> seen;
                    const auto addGroup=[&](std::vector<std::size_t> members)
                    {
                        if(members.empty()||members.size()>Settings.MaxGroupMembers)return;
                        std::sort(members.begin(),members.end());
                        if(seen.insert(members).second)groups.push_back(std::move(members));
                    };
                    for(std::size_t i=0;i<n;++i)addGroup({i});
                    if(Settings.MaxGroupMembers>1)
                    {
                        std::map<std::size_t,std::vector<std::size_t>> tagged;
                        for(std::size_t i=0;i<n;++i)if(layout.Placements[i].Group)tagged[layout.Placements[i].Group].push_back(i);
                        for(const auto& [tag,members]:tagged)addGroup(members);
                        std::vector<std::vector<std::size_t>> adjacent(n);
                        const double proximity=std::max(p.Tolerance*16,1e-7);
                        for(std::size_t i=0;i<n&&!stop();++i)for(std::size_t j=0;j<i&&!stop();++j)
                            if(Overlap(bounds[i],bounds[j],proximity)&&Detail::ShapesConflict(
                                p.Parts[part[i]].Geometry,layout.Placements[i].Transform,
                                p.Parts[part[j]].Geometry,layout.Placements[j].Transform,proximity,p.Tolerance))
                            {addGroup({i,j});adjacent[i].push_back(j);adjacent[j].push_back(i);}
                        for(std::size_t i=0;i<n;++i)
                        {
                            std::vector<std::size_t> members{i};std::set<std::size_t> touched{i};
                            for(std::size_t k=0;k<members.size()&&members.size()<Settings.MaxGroupMembers;++k)
                                for(auto j:adjacent[members[k]])if(touched.insert(j).second)
                                {members.push_back(j);addGroup(members);if(members.size()==Settings.MaxGroupMembers)break;}
                        }
                    }
                    // Large outer extents get a chance before their blockers.
                    std::stable_sort(groups.begin(),groups.end(),[&](const auto& a,const auto& b)
                    {
                        const auto right=[&](const auto& g){double v=0;for(auto i:g)v=std::max(v,
                            Settings.Axis==CompactionAxis::X?bounds[i].X+bounds[i].Width:bounds[i].Y+bounds[i].Height);return v;};
                        return right(a)>right(b);
                    });
                    for(const auto& members:groups)
                    {
                        if(stop())break;
                        std::vector<bool> included(n,false);for(auto i:members)included[i]=true;
                        std::vector<Point> directions;
                        const auto addDirection=[&](Point v)
                        {
                            const double length=std::hypot(v.X,v.Y);if(length==0)return;
                            v.X/=length;v.Y/=length;
                            if(Primary(v,Settings.Axis)>0){v.X=-v.X;v.Y=-v.Y;}
                            if(Primary(v,Settings.Axis)>=-1e-8)return;
                            for(auto old:directions)if(std::abs(Cross(old,v))<1e-10)return;
                            if(directions.size()<Settings.MaxDirections)directions.push_back(v);
                        };
                        addDirection(Settings.Axis==CompactionAxis::X?Point{-1,0}:Point{0,-1});
                        // Tangents of actual edges allow sliding alongside a
                        // slanted neighbour rather than axis-only settling.
                        for(std::size_t k=0;k<n&&directions.size()<Settings.MaxDirections;++k)
                        {
                            const auto& ring=p.Parts[part[k]].Geometry.Outer;const auto pose=layout.Placements[k].Transform;
                            for(std::size_t j=0;j<ring.size();++j)
                            {const auto a=Detail::Transform(ring[j],pose),b=Detail::Transform(ring[(j+1)%ring.size()],pose);addDirection({b.X-a.X,b.Y-a.Y});}
                        }
                        Point bestDelta{};double bestPrimary=0;
                        for(auto direction:directions)
                        {
                            if(stop())break;++rays;double limit=std::numeric_limits<double>::infinity();
                            for(auto i:members)
                            {
                                const auto b=bounds[i];
                                if(direction.X<0)limit=std::min(limit,(p.EdgeMargin-b.X)/direction.X);
                                if(direction.X>0)limit=std::min(limit,(layout.Width-p.EdgeMargin-b.X-b.Width)/direction.X);
                                if(direction.Y<0)limit=std::min(limit,(p.EdgeMargin-b.Y)/direction.Y);
                                if(direction.Y>0)limit=std::min(limit,(layout.Height-p.EdgeMargin-b.Y-b.Height)/direction.Y);
                            }
                            if(!std::isfinite(limit)||limit<=moveTolerance)continue;
                            std::vector<double> events{0,limit};
                            const auto emit=[&](double t){if(std::isfinite(t)&&t>=0&&t<=limit)events.push_back(t);};
                            for(auto i:members)for(std::size_t j=0;j<n&&!stop();++j)if(!included[j])
                            {
                                NfpBuildOptions options;options.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());options.Cancel=stop;
                                const auto lookup=cache->Get(prepared[part[j]],layout.Placements[j].Transform.AngleDegrees,
                                    prepared[part[i]],layout.Placements[i].Transform.AngleDegrees,options);
                                if(lookup.State!=NfpStatus::Ready)return finish("NFP interrupted");
                                const auto moving=layout.Placements[i].Transform,fixed=layout.Placements[j].Transform;
                                for(const auto& loop:lookup.Region->Loops)for(std::size_t k=0;k<loop.size();++k)
                                {
                                    auto a=Detail::Transform(loop[k],lookup.Transform),b=Detail::Transform(loop[(k+1)%loop.size()],lookup.Transform);
                                    a.X+=fixed.X-moving.X;a.Y+=fixed.Y-moving.Y;
                                    b.X+=fixed.X-moving.X;b.Y+=fixed.Y-moving.Y;
                                    const Point edge{b.X-a.X,b.Y-a.Y};const double denominator=Cross(direction,edge);
                                    if(std::abs(denominator)>1e-12*std::max(1.,std::hypot(edge.X,edge.Y)))
                                    {const double t=Cross(a,edge)/denominator,u=Cross(a,direction)/denominator;
                                        if(u>=-1e-10&&u<=1+1e-10)emit(t);}
                                    else if(std::abs(Cross(a,direction))<=p.Tolerance)
                                    {emit(a.X*direction.X+a.Y*direction.Y);emit(b.X*direction.X+b.Y*direction.Y);}
                                }
                            }
                            if(stop())break;
                            std::sort(events.begin(),events.end());
                            events.erase(std::unique(events.begin(),events.end(),[&](double a,double b){return std::abs(a-b)<=moveTolerance*.01;}),events.end());
                            const auto boundaryCount=events.size();
                            for(std::size_t k=1;k<boundaryCount;++k)events.push_back((events[k-1]+events[k])*.5);
                            std::sort(events.begin(),events.end(),std::greater<>());
                            std::size_t tested=0;
                            for(double t:events)
                            {
                                if(stop()||tested++>=Settings.MaxCandidatesPerRay)break;
                                const Point delta{direction.X*t,direction.Y*t};const double primary=Primary(delta,Settings.Axis);
                                if(primary>=bestPrimary-moveTolerance)continue;
                                ++work.PlacementAttempts;bool fits=true;
                                for(auto i:members)
                                {
                                    if(stop()){fits=false;break;}
                                    auto pose=layout.Placements[i].Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                                    if(!Detail::InsideSheet(p.Parts[part[i]].Geometry,pose,layout.Width,layout.Height,p.EdgeMargin,p.Tolerance))
                                    {fits=false;break;}
                                    auto box=bounds[i];box.X+=delta.X;box.Y+=delta.Y;
                                    for(std::size_t j=0;j<n;++j)
                                    {
                                        if(stop()){fits=false;break;}
                                        if(!included[j]&&Overlap(box,bounds[j],p.Tolerance)&&
                                            Detail::ShapesConflict(p.Parts[part[i]].Geometry,pose,p.Parts[part[j]].Geometry,
                                                layout.Placements[j].Transform,0,p.Tolerance)){fits=false;break;}
                                    }
                                    if(!fits)break;
                                }
                                if(fits){bestDelta=delta;bestPrimary=primary;break;}
                            }
                        }
                        if(bestPrimary<-moveTolerance)
                        {
                            for(auto i:members){layout.Placements[i].Transform.X+=bestDelta.X;layout.Placements[i].Transform.Y+=bestDelta.Y;
                                bounds[i].X+=bestDelta.X;bounds[i].Y+=bestDelta.Y;}
                            ++accepted;if(members.size()>1)++groupMoves;changed=true;
                        }
                    }
                }
                if(!changed)break;
            }
            if(!Validate(p,result).Valid){result=source;return finish("candidate rejected by original validation");}
            return finish(stop()?"budget":"finished");
        }
    };
}
PostProcessStage MakeNfpCompactionStage(NfpCompactionSettings settings,std::shared_ptr<NfpCache> cache)
{
    if(!settings.MaxPasses||settings.MaxPasses>32||!settings.MaxDirections||settings.MaxDirections>64||
        !settings.MaxGroupMembers||settings.MaxGroupMembers>16||!settings.MaxCandidatesPerRay||settings.MaxCandidatesPerRay>4096)
        throw std::invalid_argument("Invalid bounded NFP compaction settings.");
    return {std::string(settings.Axis==CompactionAxis::X?"compact.nfp.x":"compact.nfp.y")+(settings.RefillMissing?".refill":""),
        [settings,cache=std::move(cache)]{return std::make_unique<NfpCompaction>(settings,cache);}};
}
}
