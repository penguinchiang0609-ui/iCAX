#include "NestingRectangleRepairRegions.h"
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        using Bounds=RectangleRepairBounds;
        struct InvalidGeometry {};
        auto Coordinates(const Bounds& b){return std::tie(b.X0,b.Y0,b.X1,b.Y1);}
        bool Intersects(const RectangleRepairPrimitive& p,const Bounds& b)
        {return p.X<b.X1&&p.X+p.Width>b.X0&&p.Y<b.Y1&&p.Y+p.Height>b.Y0;}
        struct BoundsLess
        {
            RectangleBlockBudget* Job;
            bool operator()(const Bounds& a,const Bounds& b)const
            {ProbeRectangleBlockBudget(*Job);return Coordinates(a)<Coordinates(b);}
        };
        struct BoundsGreater
        {
            RectangleBlockBudget* Job;
            bool operator()(const Bounds& a,const Bounds& b)const
            {
                ProbeRectangleBlockBudget(*Job);
                if(a.Area()!=b.Area())return a.Area()>b.Area();
                return Coordinates(a)>Coordinates(b);
            }
        };
        class Generator
        {
            int W,H;
            std::span<const RectangleRepairPrimitive> Primitives;
            RectangleBlockBudget& Job;
            std::pmr::memory_resource* Memory;
            RectangleRepairRegionStats& Stats;
            void Probe(){ProbeRectangleBlockBudget(Job);}
            void Check(){CheckRectangleBlockBudget(Job);CheckRectangleBlockBudget(Job,true);}
            Bounds Close(Bounds r)
            {
                bool changed=true;
                while(changed)
                {
                    Probe();changed=false;
                    for(const auto& p:Primitives)
                    {
                        Probe();if(!Intersects(p,r))continue;
                        const Bounds expanded{std::min(r.X0,p.X),std::min(r.Y0,p.Y),
                            std::max(r.X1,p.X+p.Width),std::max(r.Y1,p.Y+p.Height)};
                        if(expanded!=r){r=expanded;changed=true;}
                    }
                }
                return r;
            }
            // Sweep exact y slabs instead of allocating a W*H raster. Every
            // free slab interval contributes to the same hole AABB as cells.
            bool HoleBounds(Bounds& holes)
            {
                std::pmr::vector<int> levels(Memory);
                levels.push_back(0);levels.push_back(H);
                for(const auto& p:Primitives)
                {
                    Probe();
                    if(p.TypeIndex<0||p.Width<=0||p.Height<=0||p.X<0||p.Y<0||
                       p.Width>W||p.Height>H||p.X>W-p.Width||p.Y>H-p.Height)
                        throw InvalidGeometry{};
                    levels.push_back(p.Y);levels.push_back(p.Y+p.Height);
                }
                std::sort(levels.begin(),levels.end(),[&](int a,int b){Probe();return a<b;});
                std::size_t write=0;
                for(const int y:levels){Probe();if(!write||levels[write-1]!=y)levels[write++]=y;}
                levels.resize(write);
                std::pmr::vector<std::pair<int,int>> intervals(Memory);
                holes={W,H,0,0};bool found=false;
                const auto gap=[&](int x0,int x1,int y0,int y1)
                {
                    Probe();if(x0==x1)return;
                    holes.X0=std::min(holes.X0,x0);holes.X1=std::max(holes.X1,x1);
                    holes.Y0=std::min(holes.Y0,y0);holes.Y1=std::max(holes.Y1,y1);found=true;
                };
                for(std::size_t i=1;i<levels.size();++i)
                {
                    Probe();const int y0=levels[i-1],y1=levels[i];intervals.clear();
                    for(const auto& p:Primitives)
                    {
                        Probe();if(p.Y<y1&&p.Y+p.Height>y0)intervals.emplace_back(p.X,p.X+p.Width);
                    }
                    std::sort(intervals.begin(),intervals.end(),[&](const auto& a,const auto& b){Probe();return a<b;});
                    int x=0;
                    for(const auto& interval:intervals)
                    {
                        Probe();if(interval.first<x)throw InvalidGeometry{};
                        gap(x,interval.first,y0,y1);x=interval.second;
                    }
                    gap(x,W,y0,y1);
                }
                Check();return found;
            }
        public:
            Generator(int width,int height,std::span<const RectangleRepairPrimitive> primitives,
                RectangleBlockBudget& job,std::pmr::memory_resource* memory,RectangleRepairRegionStats& stats)
                :W(width),H(height),Primitives(primitives),Job(job),Memory(memory),Stats(stats){}
            void Run(std::size_t limit,std::pmr::vector<RectangleRepairRegion>& output)
            {
                Bounds holes;
                if(!HoleBounds(holes)){Stats.AlreadyComplete=true;return;}
                const Bounds initial=Close(holes);
                std::pmr::set<Bounds,BoundsLess> seen(BoundsLess{&Job},Memory);
                std::pmr::vector<Bounds> queue(Memory);
                const BoundsGreater order{&Job};
                seen.insert(initial);queue.push_back(initial);Stats.Discovered=1;
                while(!queue.empty()&&output.size()<limit)
                {
                    Check();Probe();
                    std::pop_heap(queue.begin(),queue.end(),order);
                    const Bounds r=queue.back();queue.pop_back();
                    output.emplace_back(Memory);auto& candidate=output.back();candidate.Bounds=r;
                    for(std::size_t i=0;i<Primitives.size();++i)
                    {Probe();if(!Intersects(Primitives[i],r))candidate.OutsidePrimitiveIndices.push_back(i);}
                    ++Stats.Candidates;
                    for(int side=0;side<4;++side)
                    {
                        Probe();Bounds next=r;
                        if(side==0){if(next.X0==0)continue;--next.X0;}
                        if(side==1){if(next.Y0==0)continue;--next.Y0;}
                        if(side==2){if(next.X1==W)continue;++next.X1;}
                        if(side==3){if(next.Y1==H)continue;++next.Y1;}
                        next=Close(next);
                        if(seen.insert(next).second)
                        {++Stats.Discovered;queue.push_back(next);std::push_heap(queue.begin(),queue.end(),order);}
                    }
                }
                Stats.CandidateLimit=!queue.empty();Check();
            }
        };
    }
    bool GenerateRectangleRepairRegions(int width,int height,
        std::span<const RectangleRepairPrimitive> primitives,RectangleBlockBudget& budget,
        std::pmr::memory_resource* memory,std::pmr::vector<RectangleRepairRegion>& regions,
        RectangleRepairRegionStats& stats,std::size_t maxCandidates)
    {
        regions.clear();stats={};const auto before=budget.Attempts;
        if(!memory||regions.get_allocator().resource()!=memory||width<=0||height<=0||
           maxCandidates==0||maxCandidates>1000){stats.InvalidInput=true;return false;}
        bool success=false;
        try
        {
            CheckRectangleBlockBudget(budget);CheckRectangleBlockBudget(budget,true);
            std::pmr::vector<RectangleRepairRegion> pending(memory);
            Generator(width,height,primitives,budget,memory,stats).Run(maxCandidates,pending);
            regions.swap(pending);success=true;
        }
        catch(const RectangleBlockInterrupted&){stats.Cancelled=budget.Cancelled;stats.GlobalBudget=true;}
        catch(const std::bad_alloc&){stats.MemoryLimit=true;}
        catch(const InvalidGeometry&){stats.InvalidInput=true;}
        stats.Probes=budget.Attempts-before;return success;
    }
}
