#include "NestingRectangleStripeRegions.h"
#include <cmath>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct InvalidStripeRegion {};
    using Bounds=RectangleRepairBounds;
    using Ints=std::pmr::vector<int>;
    struct LocalProblem
    {
        Bounds Box;
        Ints Remaining;
        std::pmr::vector<unsigned char> Released;
        std::pmr::vector<RectangleResidualFixed> Fixed;
        int Count=0;
        explicit LocalProblem(std::pmr::memory_resource* memory):Remaining(memory),Released(memory),Fixed(memory){}
    };
    class Regions
    {
        int W,H,Required=0;
        std::span<const RectangleBlockType> Types;
        std::span<const RectangleRepairPrimitive> Snapshot;
        RectangleBlockBudget& Job;
        std::pmr::memory_resource* Memory;
        RectangleStripeRegionStats& Stats;
        const RectangleStripeRegionLimits& Limits;
        Bounds Holes;
        void Probe(){ProbeRectangleBlockBudget(Job);}
        void Check(){CheckRectangleBlockBudget(Job,true);}
        auto Coordinates(const Bounds& bounds)const{return std::tie(bounds.X0,bounds.Y0,bounds.X1,bounds.Y1);}
        Ints Validate(std::span<const RectangleRepairPrimitive> primitives,bool complete)
        {
            Ints counts(Types.size(),0,Memory);std::pmr::vector<unsigned char> cells(W*H,0,Memory);
            for(const auto& p:primitives)
            {
                Probe();if(p.TypeIndex<0||p.TypeIndex>=static_cast<int>(Types.size()))throw InvalidStripeRegion{};
                const auto& type=Types[p.TypeIndex];
                if(p.Width!=type.Width||p.Height!=type.Height||p.X<0||p.Y<0||p.X>W-p.Width||p.Y>H-p.Height||
                   ++counts[p.TypeIndex]>type.Quantity)throw InvalidStripeRegion{};
                for(int y=p.Y;y<p.Y+p.Height;++y)for(int x=p.X;x<p.X+p.Width;++x)
                {Probe();if(cells[y*W+x]++)throw InvalidStripeRegion{};}
            }
            Holes={W,H,0,0};
            for(int y=0;y<H;++y)for(int x=0;x<W;++x)
            {
                Probe();if(cells[y*W+x])continue;
                Holes.X0=std::min(Holes.X0,x);Holes.Y0=std::min(Holes.Y0,y);
                Holes.X1=std::max(Holes.X1,x+1);Holes.Y1=std::max(Holes.Y1,y+1);
            }
            if(complete)
            {
                if(Holes.X0<Holes.X1)throw InvalidStripeRegion{};
                for(std::size_t i=0;i<Types.size();++i){Probe();if(counts[i]!=Types[i].Quantity)throw InvalidStripeRegion{};}
            }
            return counts;
        }
        bool Make(std::span<const unsigned char> released,LocalProblem& problem)
        {
            problem.Box=Holes;problem.Released.assign(released.begin(),released.end());
            for(const auto& type:Types){Probe();problem.Remaining.push_back(type.Quantity);}
            for(std::size_t i=0;i<Snapshot.size();++i)
            {
                Probe();const auto& p=Snapshot[i];
                if(released[i])
                {
                    problem.Box.X0=std::min(problem.Box.X0,p.X);problem.Box.Y0=std::min(problem.Box.Y0,p.Y);
                    problem.Box.X1=std::max(problem.Box.X1,p.X+p.Width);problem.Box.Y1=std::max(problem.Box.Y1,p.Y+p.Height);
                }
                else --problem.Remaining[p.TypeIndex];
            }
            for(std::size_t i=0;i<Types.size();++i)
            {
                Probe();problem.Count+=problem.Remaining[i];
            }
            for(std::size_t i=0;i<Snapshot.size();++i)
            {
                Probe();if(released[i])continue;const auto& p=Snapshot[i];
                const int x0=std::max(problem.Box.X0,p.X),y0=std::max(problem.Box.Y0,p.Y);
                const int x1=std::min(problem.Box.X1,p.X+p.Width),y1=std::min(problem.Box.Y1,p.Y+p.Height);
                if(x0<x1&&y0<y1)problem.Fixed.push_back({x0-problem.Box.X0,y0-problem.Box.Y0,x1-x0,y1-y0});
            }
            std::sort(problem.Fixed.begin(),problem.Fixed.end(),[&](const auto& a,const auto& b)
            {Probe();return std::tie(a.X,a.Y,a.Width,a.Height)<std::tie(b.X,b.Y,b.Width,b.Height);});
            return true;
        }
        RectangleStripeCompatibility Compatible(const LocalProblem& problem)
        {
            // Test the exact free footprint in board coordinates. A base region
            // too narrow for a missing part can still obtain a useful donor;
            // stripe screening must precede the local bounding-box fit test.
            std::pmr::vector<RectangleResidualFixed> fullFixed(Memory);
            for(std::size_t i=0;i<Snapshot.size();++i)
            {Probe();if(!problem.Released[i]){const auto& p=Snapshot[i];fullFixed.push_back({p.X,p.Y,p.Width,p.Height});}}
            RectangleStripeStats stats;
            const auto outcome=CheckRectangleStripeCompatibility(W,H,Types,problem.Remaining,fullFixed,Job,Memory,stats);
            if(stats.GlobalBudget)throw RectangleBlockInterrupted{};
            if(stats.MemoryLimit)throw std::bad_alloc{};
            if(stats.InvalidInput)throw InvalidStripeRegion{};
            if(outcome==RectangleStripeCompatibility::Incompatible)++Stats.StripeRejected;
            return outcome;
        }
        bool Fits(const LocalProblem& problem)
        {
            const int width=problem.Box.X1-problem.Box.X0,height=problem.Box.Y1-problem.Box.Y0;
            for(std::size_t i=0;i<Types.size();++i)
            {Probe();if(problem.Remaining[i]&&(Types[i].Width>width||Types[i].Height>height))return false;}
            return true;
        }
        bool Same(const LocalProblem& a,const LocalProblem& b)
        {
            Probe();if(a.Box.X1-a.Box.X0!=b.Box.X1-b.Box.X0||a.Box.Y1-a.Box.Y0!=b.Box.Y1-b.Box.Y0||
                a.Remaining.size()!=b.Remaining.size()||a.Fixed.size()!=b.Fixed.size())return false;
            for(std::size_t i=0;i<a.Remaining.size();++i){Probe();if(a.Remaining[i]!=b.Remaining[i])return false;}
            for(std::size_t i=0;i<a.Fixed.size();++i)
            {Probe();const auto& x=a.Fixed[i];const auto& y=b.Fixed[i];if(std::tie(x.X,x.Y,x.Width,x.Height)!=std::tie(y.X,y.Y,y.Width,y.Height))return false;}
            return true;
        }
    public:
        Regions(int width,int height,std::span<const RectangleBlockType> types,std::span<const RectangleRepairPrimitive> snapshot,
            RectangleBlockBudget& job,std::pmr::memory_resource* memory,RectangleStripeRegionStats& stats,const RectangleStripeRegionLimits& limits)
            :W(width),H(height),Types(types),Snapshot(snapshot),Job(job),Memory(memory),Stats(stats),Limits(limits){}
        bool Run(std::pmr::vector<RectangleRepairPrimitive>& output)
        {
            Check();
            if(W<1||H<1||W>512||H>512||W*H>65536||Types.size()>512||Snapshot.size()>512||!Limits.Regions||Limits.Regions>1000||
               !Limits.Donors||Limits.Donors>512||!Limits.Trials||Limits.Trials>1000||!std::isfinite(Limits.TrialSeconds)||Limits.TrialSeconds<=0||
               !Limits.Exact.Nodes||!Limits.Exact.Probes)
                throw InvalidStripeRegion{};
            std::int64_t area=0;
            for(std::size_t i=0;i<Types.size();++i)
            {
                Probe();const auto& t=Types[i];
                if(t.Width<1||t.Height<1||t.Width>W||t.Height>H||t.Quantity<1||t.Quantity>512)throw InvalidStripeRegion{};
                Required+=t.Quantity;if(Required>512)throw InvalidStripeRegion{};area+=std::int64_t(t.Width)*t.Height*t.Quantity;
                for(std::size_t j=0;j<i;++j){Probe();if(t.Width==Types[j].Width&&t.Height==Types[j].Height)throw InvalidStripeRegion{};}
            }
            if(area!=W*H)throw InvalidStripeRegion{};
            Validate(Snapshot,false);
            if(Holes.X0>=Holes.X1)
            {
                Validate(Snapshot,true);for(const auto& p:Snapshot){Probe();output.push_back(p);}Check();
                Stats.Complete=Stats.AlreadyComplete=true;return true;
            }
            std::pmr::vector<RectangleRepairRegion> regions(Memory);
            if(!GenerateRectangleRepairRegions(W,H,Snapshot,Job,Memory,regions,Stats.Regions,Limits.Regions))
            {
                if(Stats.Regions.GlobalBudget)throw RectangleBlockInterrupted{};
                if(Stats.Regions.MemoryLimit)throw std::bad_alloc{};throw InvalidStripeRegion{};
            }
            std::pmr::vector<LocalProblem> candidates(Memory);
            const auto add=[&](LocalProblem&& problem)
            {if(!Fits(problem))return;for(const auto& existing:candidates){Probe();if(Same(existing,problem))return;}candidates.push_back(std::move(problem));};
            for(const auto& region:regions)
            {
                Probe();if(region.OutsidePrimitiveIndices.empty()){++Stats.FullBoardRegions;continue;}
                std::pmr::vector<unsigned char> released(Snapshot.size(),1,Memory);
                for(auto i:region.OutsidePrimitiveIndices){Probe();released[i]=0;}
                LocalProblem base(Memory);if(!Make(released,base))continue;
                if(Compatible(base)==RectangleStripeCompatibility::Compatible){add(std::move(base));continue;}
                std::pmr::vector<std::size_t> donors(Memory);const auto& r=region.Bounds;
                for(std::size_t i=0;i<Snapshot.size();++i)
                {
                    Probe();if(released[i])continue;const auto& p=Snapshot[i];
                    const bool touches=((p.X==r.X1||p.X+p.Width==r.X0)&&std::min(p.Y+p.Height,r.Y1)>std::max(p.Y,r.Y0))||
                        ((p.Y==r.Y1||p.Y+p.Height==r.Y0)&&std::min(p.X+p.Width,r.X1)>std::max(p.X,r.X0));
                    if(touches)donors.push_back(i);
                }
                std::sort(donors.begin(),donors.end(),[&](std::size_t a,std::size_t b)
                {Probe();const auto& x=Snapshot[a];const auto& y=Snapshot[b];return std::tuple(x.Width*x.Height,x.Y,x.X,a)<std::tuple(y.Width*y.Height,y.Y,y.X,b);});
                std::size_t compatibleDonors=0;
                for(auto i:donors)
                {
                    Probe();++Stats.DonorsTested;released[i]=1;LocalProblem donor(Memory);
                    const bool built=Make(released,donor);released[i]=0;if(!built)continue;
                    if(Compatible(donor)!=RectangleStripeCompatibility::Compatible)continue;
                    add(std::move(donor));if(++compatibleDonors>=Limits.Donors)break;
                }
            }
            std::sort(candidates.begin(),candidates.end(),[&](const LocalProblem& a,const LocalProblem& b)
            {Probe();if(a.Count!=b.Count)return a.Count<b.Count;if(a.Box.Area()!=b.Box.Area())return a.Box.Area()<b.Box.Area();return Coordinates(a.Box)<Coordinates(b.Box);});
            Stats.Candidates=candidates.size();Stats.NoCandidates=candidates.empty();
            Ints nodes(Memory);for(std::size_t i=0;i<Types.size();++i){Probe();nodes.push_back(static_cast<int>(i));}
            for(const auto& problem:candidates)
            {
                Check();if(Stats.Trials>=Limits.Trials){Stats.TrialLimit=true;break;}++Stats.Trials;
                std::pmr::vector<RectangleBlockPlacement> local(Memory);RectangleStripeStats exactStats;
                // A view keeps the original start, cumulative work counter and
                // global deadline. Only its optional trial deadline is local.
                const double deadline=Job.Elapsed()+Limits.TrialSeconds;bool trialTime=false,realCancel=false;
                Options settings=Job.Settings;settings.Cancel=[&]
                {
                    if(Job.Settings.Cancel&&Job.Settings.Cancel()){realCancel=true;return true;}
                    if(Job.Elapsed()>=deadline){trialTime=true;return true;}return false;
                };
                RectangleBlockBudget trial{settings};trial.Started=Job.Started;trial.Attempts=Job.Attempts;
                trial.ControlChecks=Job.ControlChecks;trial.SampledElapsed=Job.SampledElapsed;
                const bool complete=SearchRectangleStripeRepair(problem.Box.X1-problem.Box.X0,problem.Box.Y1-problem.Box.Y0,
                    Types,problem.Remaining,nodes,problem.Fixed,trial,Memory,Limits.Exact,local,exactStats);
                Job.Attempts=trial.Attempts;Job.ControlChecks=trial.ControlChecks;Job.SampledElapsed=trial.SampledElapsed;
                Job.Cancelled=Job.Cancelled||realCancel;Job.Exhausted=Job.Exhausted||trial.Exhausted;
                Stats.ExactNodes+=exactStats.Search.Nodes;Stats.ExactChecks+=exactStats.Checks;
                if(realCancel||trial.Exhausted)throw RectangleBlockInterrupted{};
                if(trialTime){++Stats.TrialTimeLimits;++Stats.LimitedTrials;continue;}
                if(exactStats.MemoryLimit){Stats.MemoryLimit=true;++Stats.LimitedTrials;continue;}
                if(exactStats.InvalidInput)throw InvalidStripeRegion{};
                if(!complete){if(exactStats.Infeasible)++Stats.LocalInfeasible;else ++Stats.LimitedTrials;continue;}
                std::pmr::vector<RectangleRepairPrimitive> pending(Memory);
                for(std::size_t i=0;i<Snapshot.size();++i){Probe();if(!problem.Released[i])pending.push_back(Snapshot[i]);}
                for(const auto& p:local)
                {
                    Probe();const auto& t=Types[p.Node];pending.push_back({p.Node,p.X+problem.Box.X0,p.Y+problem.Box.Y0,t.Width,t.Height});
                }
                Validate(pending,true);Check();output.swap(pending);Stats.Complete=true;return true;
            }
            Check();return false;
        }
    };
}
bool SearchRectangleStripeRegions(int width,int height,std::span<const RectangleBlockType> types,
    std::span<const RectangleRepairPrimitive> snapshot,RectangleBlockBudget& job,std::pmr::memory_resource* memory,
    std::pmr::vector<RectangleRepairPrimitive>& output,RectangleStripeRegionStats& stats,const RectangleStripeRegionLimits& limits)
{
    stats={};output.clear();const auto initial=job.Attempts;bool complete=false;
    if(!memory||output.get_allocator().resource()!=memory){stats.InvalidInput=true;return false;}
    try{complete=Regions(width,height,types,snapshot,job,memory,stats,limits).Run(output);}
    catch(const InvalidStripeRegion&){stats.InvalidInput=true;}
    catch(const RectangleBlockInterrupted&){stats.GlobalBudget=true;stats.Cancelled=job.Cancelled;}
    catch(const std::bad_alloc&){stats.MemoryLimit=true;}
    stats.Checks=job.Attempts-initial;if(!complete){stats.Complete=false;output.clear();}return complete;
}
}
