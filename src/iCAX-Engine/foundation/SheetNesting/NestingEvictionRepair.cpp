#include "SheetNestingLocalRepair.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t MaxPlaced=128,MaxSpecs=128,MaxVertices=128,MaxAngles=16,
        MaxMissing=2,MaxPoses=30000,MaxProposals=1024,MaxRestoreBranches=12,MaxDisplaced=5,
        MaxRestoreAttempts=1500,MaxPerBlockerSet=6,MaxAttempts=500000;
    using PartMap=std::map<std::string,const Part*>;
    struct Trial {Pose PoseValue;std::vector<std::size_t> Blockers;};

    class EvictionRepair final:public IPostProcessor
    {
    public:
        Result Run(const Problem& problem,const Result& incumbent,const Options& options) override
        {
            const auto started=Clock::now();Result kept=incumbent;Statistics work;
            work.VerificationSeconds=work.EvaluationSeconds=0;bool cancelled=false;
            std::size_t trials=0;std::string reason="finite-candidates-exhausted";
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]
            {
                if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
                return cancelled||elapsed()>=options.TimeLimitSeconds||
                    work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts);
            };
            const auto finish=[&]
            {
                if(cancelled)reason="cancelled";
                else if(elapsed()>=options.TimeLimitSeconds)reason="time-limit";
                else if(work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts))reason="attempt-limit";
                const auto original=kept.Stats;kept.Stats=work;
                kept.Stats.CombinationGroups=original.CombinationGroups;
                kept.Stats.CircleLatticeGroups=original.CircleLatticeGroups;
                kept.Stats.GapFilledParts=original.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();
                if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("repair.eviction: "+reason+"; candidates="+std::to_string(trials));
                return kept;
            };
            if(stop())return finish();
            std::size_t missing=0;for(const auto& r:incumbent.Remaining)missing+=r.Quantity;
            if(incumbent.State!=Status::Partial||incumbent.Layouts.size()!=1||problem.PartGap!=0||
                !missing||missing>MaxMissing||incumbent.Layouts[0].Placements.size()>MaxPlaced||problem.Parts.size()>MaxSpecs)
            {reason="partial-single-sheet-zero-gap-scope";return finish();}
            PartMap parts;for(const auto& p:problem.Parts)
            {
                if(stop())return finish();
                if(p.Geometry.Radius>0||p.Geometry.Outer.size()>MaxVertices||!p.Geometry.Holes.empty())
                {reason="polygon-scope";return finish();}
                parts.emplace(p.ID,&p);
            }
            const auto checkStart=Clock::now();const auto initial=Validate(problem,incumbent);
            work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-checkStart).count();
            if(!initial.Valid){reason="invalid-incumbent";return finish();}
            if(stop())return finish();
            std::size_t bestMissing=missing;
            const auto targetPoints=[&](const Layout& layout)
            {
                std::vector<Point> points;
                for(const auto& item:layout.Placements)
                {
                    if(stop())break;const auto& p=*parts.at(item.PartID);
                    for(std::size_t i=0;i<p.Geometry.Outer.size();++i)
                    {
                        const auto a=Detail::Transform(p.Geometry.Outer[i],item.Transform);
                        const auto b=Detail::Transform(p.Geometry.Outer[(i+1)%p.Geometry.Outer.size()],item.Transform);
                        points.push_back(a);points.push_back({(a.X+b.X)/2,(a.Y+b.Y)/2});
                    }
                }
                return points;
            };
            const auto poses=[&](const Part& part,const Layout& layout)
            {
                std::vector<Pose> out;std::set<std::tuple<double,double,double>> distinct;
                auto angles=Detail::CandidateAngles(part,problem.Tolerance);if(angles.size()>MaxAngles)angles.resize(MaxAngles);
                const auto points=targetPoints(layout);
                for(double angle:angles)
                {
                    if(stop()||out.size()>=MaxPoses)break;
                    std::vector<Point> moving;for(auto v:part.Geometry.Outer)moving.push_back(Detail::Transform(v,{0,0,angle}));
                    const auto bounds=Detail::Bounds(part.Geometry,{0,0,angle});
                    const double left=problem.EdgeMargin-bounds.X,right=layout.Width-problem.EdgeMargin-bounds.X-bounds.Width;
                    const double bottom=problem.EdgeMargin-bounds.Y,top=layout.Height-problem.EdgeMargin-bounds.Y-bounds.Height;
                    const auto add=[&](double x,double y)
                    {
                        if(stop()||out.size()>=MaxPoses||!std::isfinite(x)||!std::isfinite(y))return;
                        if(distinct.emplace(x,y,angle).second)out.push_back({x,y,angle});
                    };
                    add(left,bottom);add(right,bottom);add(left,top);add(right,top);
                    for(const auto fixed:points)
                    {
                        if(stop()||out.size()>=MaxPoses)break;
                        for(const auto local:moving)
                        {
                            const double x=fixed.X-local.X,y=fixed.Y-local.Y;
                            add(x,y);add(left,y);add(right,y);add(x,bottom);add(x,top);
                            if(stop()||out.size()>=MaxPoses)break;
                        }
                    }
                }
                return out;
            };
            const auto conflicts=[&](const Part& part,Pose pose,const Layout& layout,std::size_t maxConflicts)
            {
                std::vector<std::size_t> blockers;
                if(stop())return blockers;
                ++work.PlacementAttempts;++trials;
                if(!Detail::InsideSheet(part.Geometry,pose,layout.Width,layout.Height,problem.EdgeMargin,problem.Tolerance))
                {blockers.resize(maxConflicts+1);return blockers;}
                for(std::size_t i=0;i<layout.Placements.size();++i)
                {
                    if(stop()){blockers.resize(maxConflicts+1);return blockers;}
                    const auto& other=layout.Placements[i];
                    if(Detail::ShapesConflict(part.Geometry,pose,parts.at(other.PartID)->Geometry,other.Transform,0,problem.Tolerance))
                    {blockers.push_back(i);if(blockers.size()>maxConflicts)break;}
                }
                return blockers;
            };
            const auto missingCount=[](const Result& r)
            {std::size_t n=0;for(const auto& q:r.Remaining)n+=q.Quantity;return n;};
            const auto certify=[&](Result& candidate)
            {
                candidate.Remaining.erase(std::remove_if(candidate.Remaining.begin(),candidate.Remaining.end(),
                    [](const auto& item){return item.Quantity==0;}),candidate.Remaining.end());
                candidate.State=candidate.Remaining.empty()?Status::Complete:Status::Partial;
                for(auto& l:candidate.Layouts)
                {l.MaterialArea=0;for(const auto& p:l.Placements)l.MaterialArea+=Detail::Area(parts.at(p.PartID)->Geometry);}
                candidate.GeometryValidated=false;const auto t=Clock::now();const auto check=Validate(problem,candidate);
                work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-t).count();
                if(!check.Valid||stop())return false;candidate.GeometryValidated=true;return true;
            };
            using InstanceKey=std::pair<std::string,std::size_t>;
            std::function<bool(Result&,std::vector<Placement>,std::size_t,std::set<InstanceKey>)> restore;
            restore=[&](Result& candidate,std::vector<Placement> evicted,std::size_t index,std::set<InstanceKey> protectedItems)
            {
                if(stop())return false;if(index==evicted.size())return true;
                const auto& item=evicted[index];const auto& part=*parts.at(item.PartID);
                const auto list=poses(part,candidate.Layouts[0]);std::vector<Trial> choices;
                const auto attemptStart=work.PlacementAttempts;
                for(const auto pose:list)
                {
                    if(stop()||work.PlacementAttempts-attemptStart>=MaxRestoreAttempts)break;
                    auto blockers=conflicts(part,pose,candidate.Layouts[0],1);
                    if(stop())return false;if(blockers.size()>1)continue;
                    if(!blockers.empty())
                    {
                        const auto& blocker=candidate.Layouts[0].Placements[blockers.front()];
                        if(evicted.size()>=MaxDisplaced||protectedItems.count({blocker.PartID,blocker.Instance}))continue;
                    }
                    choices.push_back({pose,std::move(blockers)});
                }
                std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b)
                {return std::tuple(a.Blockers.size(),a.PoseValue.Y,a.PoseValue.X)<std::tuple(b.Blockers.size(),b.PoseValue.Y,b.PoseValue.X);});
                std::size_t branches=0;
                for(const auto& choice:choices)
                {
                    if(stop()||branches++>=MaxRestoreBranches)break;
                    auto trial=candidate;auto queue=evicted;auto protectedNext=protectedItems;
                    if(!choice.Blockers.empty())
                    {
                        const auto b=choice.Blockers.front();queue.push_back(trial.Layouts[0].Placements[b]);
                        trial.Layouts[0].Placements.erase(trial.Layouts[0].Placements.begin()+static_cast<std::ptrdiff_t>(b));
                    }
                    auto moved=item;moved.Transform=choice.PoseValue;moved.Group=0;
                    trial.Layouts[0].Placements.push_back(moved);protectedNext.emplace(moved.PartID,moved.Instance);
                    if(restore(trial,std::move(queue),index+1,std::move(protectedNext))){candidate=std::move(trial);return true;}
                }
                return false;
            };
            std::function<void(const Result&,std::size_t)> search;
            search=[&](const Result& current,std::size_t depth)
            {
                if(stop()||current.Remaining.empty()||depth>=MaxMissing)return;
                const auto unmet=std::find_if(current.Remaining.begin(),current.Remaining.end(),[](const auto& item){return item.Quantity>0;});
                if(unmet==current.Remaining.end())return;
                const auto remainingIndex=static_cast<std::size_t>(std::distance(current.Remaining.begin(),unmet));
                const auto entry=*unmet;const auto& part=*parts.at(entry.PartID);
                std::set<std::size_t> used;
                for(const auto& p:current.Layouts[0].Placements)if(p.PartID==part.ID)used.insert(p.Instance);
                std::size_t instance=0;while(instance<part.Quantity&&used.count(instance))++instance;
                if(instance>=part.Quantity)return;
                std::vector<Trial> proposals;std::map<std::vector<std::size_t>,std::size_t> byBlockers;
                const auto candidates=poses(part,current.Layouts[0]);
                for(const auto pose:candidates)
                {
                    if(stop()||proposals.size()>=MaxProposals)break;
                    auto blockers=conflicts(part,pose,current.Layouts[0],2);
                    if(stop())break;
                    if(blockers.size()<=2&&byBlockers[blockers]++<MaxPerBlockerSet)
                        proposals.push_back({pose,std::move(blockers)});
                }
                std::stable_sort(proposals.begin(),proposals.end(),[](const auto& a,const auto& b)
                {return std::tuple(a.Blockers.size(),a.PoseValue.Y,a.PoseValue.X)<std::tuple(b.Blockers.size(),b.PoseValue.Y,b.PoseValue.X);});
                for(const auto& trial:proposals)
                {
                    if(stop()||bestMissing==0)break;
                    Result candidate=current;auto& layout=candidate.Layouts[0];std::vector<Placement> evicted;
                    for(auto index:trial.Blockers)evicted.push_back(layout.Placements[index]);
                    for(auto it=trial.Blockers.rbegin();it!=trial.Blockers.rend();++it)layout.Placements.erase(layout.Placements.begin()+static_cast<std::ptrdiff_t>(*it));
                    layout.Placements.push_back({part.ID,instance,trial.PoseValue,0,false});
                    if(--candidate.Remaining[remainingIndex].Quantity==0)
                        candidate.Remaining.erase(candidate.Remaining.begin()+static_cast<std::ptrdiff_t>(remainingIndex));
                    const std::set<InstanceKey> protectedItems{{part.ID,instance}};
                    bool restored=restore(candidate,evicted,0,protectedItems);
                    if(!restored&&evicted.size()==2&&!stop())
                    {std::reverse(evicted.begin(),evicted.end());restored=restore(candidate,evicted,0,protectedItems);}
                    if(!restored||stop()||!certify(candidate))continue;
                    const auto count=missingCount(candidate);
                    if(count<bestMissing){kept=candidate;bestMissing=count;reason="validated-improvement";}
                    if(count)search(candidate,depth+1);
                }
            };
            search(incumbent,0);
            return finish();
        }
    };
}
PostProcessStage MakeEvictionRepairStage()
{return {"repair.eviction",[]{return std::make_unique<EvictionRepair>();}};}
}
