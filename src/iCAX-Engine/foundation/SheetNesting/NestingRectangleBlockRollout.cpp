#include "NestingRectangleBlockSearch.h"

#include <numeric>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Rect {int X,Y,Width,Height;};
    struct Placed {int Block,X,Y;};
    struct State
    {
        std::pmr::vector<int> Remaining,Active;
        std::pmr::vector<Rect> Spaces;
        std::pmr::vector<Placed> Placements;
        int Area=0,Count=0,Rollout=0;
        explicit State(std::pmr::memory_resource* memory):Remaining(memory),Active(memory),Spaces(memory),Placements(memory){}
        State(const State&)=delete;
        State& operator=(const State&)=delete;
        State(State&&)=default;
        State& operator=(State&&)=default;
    };
    struct Candidate {int Block,X,Y;double Score;int Area,Contact;};
    using CandidateList=std::pmr::vector<Candidate>;
    struct CandidateDiversity
    {
        CandidateList Primitives;
        std::pmr::vector<CandidateList> Tiers,Inventories;
        std::pmr::map<std::pair<int,int>,Candidate> Shapes;
        explicit CandidateDiversity(std::pmr::memory_resource* memory)
            :Primitives(memory),Tiers(memory),Inventories(memory),Shapes(memory)
        {Tiers.resize(10);Inventories.resize(8);}
    };
    struct Complete {};
    struct RolloutPhaseStopped {};
    bool Overlaps(const Rect& a,const Rect& b)
    {return a.X<b.X+b.Width&&b.X<a.X+a.Width&&a.Y<b.Y+b.Height&&b.Y<a.Y+a.Height;}
    bool Contains(const Rect& a,const Rect& b)
    {return a.X<=b.X&&a.Y<=b.Y&&a.X+a.Width>=b.X+b.Width&&a.Y+a.Height>=b.Y+b.Height;}
    bool Better(const Candidate& a,const Candidate& b)
    {
        if(a.Score!=b.Score)return a.Score>b.Score;
        return std::tuple(a.Area,a.Contact,-a.Y,-a.X,-a.Block)>std::tuple(b.Area,b.Contact,-b.Y,-b.X,-b.Block);
    }

    class RolloutSearch
    {
    public:
        RolloutSearch(int width,int height,std::span<const RectangleBlockType> types,
            std::span<const RectangleBlockMacro> macros,RectangleBlockBudget& job,
            std::pmr::memory_resource* memory,RectangleBlockRolloutStats& stats,BlockRolloutPolicy policy,
            const RectangleBlockRolloutLimits& limits={})
            :W(width),H(height),Types(types),Macros(macros),Job(job),Memory(memory),Stats(stats),Policy(policy),
             Limits(limits),StartedChecks(job.Attempts),StartedSeconds(job.Elapsed()),InventorySignatures(memory),Best(memory)
        {
            Check(true);Best.Spaces.push_back({0,0,W,H});
            for(const auto& type:Types){Check();Total+=type.Quantity;Best.Remaining.push_back(type.Quantity);}
            Best.Active.resize(Macros.size());std::iota(Best.Active.begin(),Best.Active.end(),0);
            Stats.Selected=Macros.size();Check(true);
            if(Policy==BlockRolloutPolicy::Diverse)
                for(const auto& macro:Macros)
                {
                    Probe();std::uint64_t hash=1469598103934665603ULL;
                    for(const auto [id,count]:macro.Uses)
                    {Check();hash^=static_cast<std::uint64_t>(id+1);hash*=1099511628211ULL;hash^=static_cast<std::uint64_t>(count);hash*=1099511628211ULL;}
                    InventorySignatures.push_back(hash);
                }
        }
        bool Run(std::pmr::vector<RectangleBlockPlacement>& result)
        {
            try
            {
                State root=Clone(Best);root.Rollout=Rollout(root);
                std::pmr::vector<State> beam(Memory);beam.push_back(std::move(root));
                while(!beam.empty())
                {
                    Check(true);++Stats.Layers;
                    std::pmr::vector<State> next(Memory);
                    std::pmr::unordered_set<std::pmr::string> seen(Memory);
                    for(const auto& state:beam)
                    {
                        auto options=Candidates(state,Limits.Shortlist,false);
                        for(const auto& option:options)
                        {
                            Check(true);State child=Apply(state,option);Accept(child);
                            if(!CanComplete(child)||!seen.insert(Key(child)).second)continue;
                            child.Rollout=Rollout(child);next.push_back(std::move(child));
                        }
                    }
                    std::sort(next.begin(),next.end(),[&](const auto& a,const auto& b)
                    {
                        Check();
                        if(a.Rollout!=b.Rollout)return a.Rollout>b.Rollout;
                        if(a.Area!=b.Area)return a.Area>b.Area;
                        if(a.Spaces.size()!=b.Spaces.size())return a.Spaces.size()<b.Spaces.size();
                        return a.Count>b.Count;
                    });
                    if(next.size()>static_cast<std::size_t>(Limits.BeamWidth))next.erase(next.begin()+Limits.BeamWidth,next.end());
                    beam=std::move(next);
                }
            }
            catch(const Complete&){}
            catch(const RolloutPhaseStopped&)
            {if(!Limits.AllowInternalPartial)throw;Stats.PhaseLimitReached=true;}
            catch(const std::bad_alloc&)
            {if(!Limits.AllowInternalPartial)throw;Stats.MemoryLimitReached=true;}
            GlobalCheck(true);
            if(!Limits.AllowInternalPartial&&(Best.Count!=Total||Best.Area!=W*H))return false;
            Stats.SnapshotCount=Best.Count;Stats.SnapshotArea=Best.Area;
            result.reserve(Best.Placements.size());
            for(const auto& p:Best.Placements)
            {GlobalCheck();result.push_back({Macros[p.Block].Node,p.X,p.Y});}
            GlobalCheck(true);return true;
        }
    private:
        int W,H,Total=0;
        std::span<const RectangleBlockType> Types;
        std::span<const RectangleBlockMacro> Macros;
        RectangleBlockBudget& Job;
        std::pmr::memory_resource* Memory;
        RectangleBlockRolloutStats& Stats;
        BlockRolloutPolicy Policy;
        RectangleBlockRolloutLimits Limits;
        std::size_t StartedChecks;
        double StartedSeconds;
        std::pmr::vector<std::uint64_t> InventorySignatures;
        State Best;
        void GlobalCheck(bool force=false)
        {
            if(Limits.AllowInternalPartial&&force)CheckRectangleBlockBudget(Job);
            CheckRectangleBlockBudget(Job,force);
        }
        void CheckPhase()
        {
            if((Limits.Checks&&Job.Attempts-StartedChecks>=Limits.Checks)||
               (Limits.Seconds>0&&Job.SampledElapsed-StartedSeconds>=Limits.Seconds))throw RolloutPhaseStopped{};
        }
        void Check(bool force=false)
        {GlobalCheck(force);if(Limits.AllowInternalPartial)CheckPhase();}
        void Probe()
        {if(Limits.AllowInternalPartial)CheckPhase();ProbeRectangleBlockBudget(Job);}
        State Clone(const State& source)
        {
            Check(true);State state(Memory);
            state.Remaining=source.Remaining;state.Active=source.Active;state.Spaces=source.Spaces;state.Placements=source.Placements;
            state.Area=source.Area;state.Count=source.Count;state.Rollout=source.Rollout;Check(true);return state;
        }
        bool Available(const State& state,const RectangleBlockMacro& macro)
        {
            Probe();
            for(const auto [type,count]:macro.Uses){Check();if(state.Remaining[type]<count)return false;}
            return true;
        }
        std::pmr::vector<int> Reachable(const State& state,bool vertical)
        {
            const int extent=vertical?H:W;Check(true);
            std::pmr::vector<unsigned char> bits(static_cast<std::size_t>(extent)+1,0,Memory);bits[0]=1;
            for(std::size_t i=0;i<Types.size();++i)
            {
                Probe();const int step=vertical?Types[i].Height:Types[i].Width;
                int remaining=std::min(state.Remaining[i],extent/step);
                for(int batch=1;remaining;batch*=2)
                {
                    Check();const int take=std::min(batch,remaining);remaining-=take;const int shift=take*step;
                    for(int x=extent;x>=shift;--x){Check();bits[x]|=bits[x-shift];}
                }
            }
            std::pmr::vector<int> length(static_cast<std::size_t>(extent)+1,0,Memory);
            for(int x=1;x<=extent;++x){Check();length[x]=bits[x]?x:length[x-1];}
            return length;
        }
        int Contact(const State& state,const Rect& rect)
        {
            int value=(rect.X==0||rect.X+rect.Width==W?rect.Height:0)+(rect.Y==0||rect.Y+rect.Height==H?rect.Width:0);
            if(rect.X==0&&rect.X+rect.Width==W)value+=rect.Height;
            if(rect.Y==0&&rect.Y+rect.Height==H)value+=rect.Width;
            for(const auto& p:state.Placements)
            {
                Probe();const auto& macro=Macros[p.Block];
                if(rect.X+rect.Width==p.X||p.X+macro.Width==rect.X)
                    value+=std::max(0,std::min(rect.Y+rect.Height,p.Y+macro.Height)-std::max(rect.Y,p.Y));
                if(rect.Y+rect.Height==p.Y||p.Y+macro.Height==rect.Y)
                    value+=std::max(0,std::min(rect.X+rect.Width,p.X+macro.Width)-std::max(rect.X,p.X));
            }
            return value;
        }
        std::pmr::vector<Candidate> Candidates(const State& state,int limit,bool greedy)
        {
            Check(true);std::pmr::vector<int> order(state.Spaces.size(),Memory);std::iota(order.begin(),order.end(),0);
            const auto rank=[&](int id)
            {const auto& r=state.Spaces[id];return std::tuple(std::min(r.X,W-r.X-r.Width)+std::min(r.Y,H-r.Y-r.Height),-r.Width*r.Height,r.Y,r.X);};
            std::sort(order.begin(),order.end(),[&](int a,int b){Check();return rank(a)<rank(b);});
            const auto rw=Reachable(state,false),rh=Reachable(state,true);
            std::pmr::vector<Candidate> keep(Memory);keep.reserve(static_cast<std::size_t>(limit));
            std::optional<CandidateDiversity> diversity;
            const int quota=std::max(1,limit/8);
            if(Policy==BlockRolloutPolicy::Diverse)diversity.emplace(Memory);
            const auto offer=[](CandidateList& heap,const Candidate& candidate,int cap)
            {
                if(heap.size()<static_cast<std::size_t>(cap)){heap.push_back(candidate);std::push_heap(heap.begin(),heap.end(),Better);}
                else if(Better(candidate,heap.front()))
                {std::pop_heap(heap.begin(),heap.end(),Better);heap.back()=candidate;std::push_heap(heap.begin(),heap.end(),Better);}
            };
            std::pmr::unordered_set<std::uint64_t> seen(Memory);int usedSpaces=0;
            for(int sid:order)
            {
                const auto& r=state.Spaces[sid];bool any=false;
                const bool right=W-r.X-r.Width<r.X,top=H-r.Y-r.Height<r.Y;
                for(int id:state.Active)
                {
                    Probe();const auto& macro=Macros[id];if(macro.Width>r.Width||macro.Height>r.Height)continue;any=true;
                    const double loss=double(r.Width*r.Height-(macro.Width+rw[r.Width-macro.Width])*(macro.Height+rh[r.Height-macro.Height]))/double(r.Width*r.Height);
                    const int corners=greedy||state.Placements.empty()?1:4;
                    for(int corner=0;corner<corners;++corner)
                    {
                        Probe();const bool rx=greedy?right:bool(corner&1),ry=greedy?top:bool(corner&2);
                        const int x=rx?r.X+r.Width-macro.Width:r.X,y=ry?r.Y+r.Height-macro.Height:r.Y;
                        const std::uint64_t key=(std::uint64_t(id)*1024+x)*1024+y;
                        if(!seen.insert(key).second)continue;
                        const int touch=Contact(state,{x,y,macro.Width,macro.Height});
                        const double factor=1.+double(touch)/(2*(macro.Width+macro.Height));
                        const double score=double(macro.Width*macro.Height)*factor/(1.+loss*r.Width*r.Height);
                        const Candidate candidate{id,x,y,score,macro.Width*macro.Height,touch};
                        if(keep.size()<static_cast<std::size_t>(limit)){keep.push_back(candidate);std::push_heap(keep.begin(),keep.end(),Better);}
                        else if(Better(candidate,keep.front()))
                        {std::pop_heap(keep.begin(),keep.end(),Better);keep.back()=candidate;std::push_heap(keep.begin(),keep.end(),Better);}
                        if(Policy==BlockRolloutPolicy::Diverse)
                        {
                            Probe();
                            if(macro.Members==1)offer(diversity->Primitives,candidate,quota*4);
                            else{int tier=0;for(int members=macro.Members;members>1;members>>=1)++tier;offer(diversity->Tiers[tier],candidate,quota);}
                            offer(diversity->Inventories[InventorySignatures[id]&7],candidate,quota);
                            const auto [shape,inserted]=diversity->Shapes.try_emplace(std::make_pair(macro.Width,macro.Height),candidate);
                            if(!inserted&&Better(candidate,shape->second))shape->second=candidate;
                        }
                    }
                }
                Check(true);if(any&&++usedSpaces>=(greedy?1:2))break;
            }
            std::sort(keep.begin(),keep.end(),[&](const auto& a,const auto& b){Check();return Better(a,b);});
            if(Policy==BlockRolloutPolicy::Score)return keep;
            return DiverseCandidates(state,limit,greedy,keep,diversity->Primitives,diversity->Tiers,diversity->Inventories,diversity->Shapes);
        }
        CandidateList DiverseCandidates(const State& state,int limit,bool greedy,const CandidateList& ranked,
            CandidateList& primitives,std::pmr::vector<CandidateList>& tiers,std::pmr::vector<CandidateList>& inventories,
            const std::pmr::map<std::pair<int,int>,Candidate>& shapeBest)
        {
            const int quota=std::max(1,limit/8),ordinary=std::max(1,limit-4*quota);
            const auto sort=[&](CandidateList& list)
            {std::sort(list.begin(),list.end(),[&](const auto& a,const auto& b){Check();return Better(a,b);});};
            CandidateList answer(Memory);answer.reserve(static_cast<std::size_t>(limit));
            std::pmr::unordered_set<std::uint64_t> picked(Memory);
            const auto take=[&](const Candidate& candidate)
            {
                Probe();const auto key=(std::uint64_t(candidate.Block)*1024+candidate.X)*1024+candidate.Y;
                if(answer.size()>=static_cast<std::size_t>(limit)||!picked.insert(key).second)return false;
                answer.push_back(candidate);return true;
            };
            for(int i=0;i<std::min(ordinary,static_cast<int>(ranked.size()));++i)take(ranked[i]);
            std::pmr::set<int> primitiveTypes(Memory);int added=0;sort(primitives);
            for(const auto& candidate:primitives)
                if(!primitiveTypes.contains(candidate.Block)&&take(candidate))
                {primitiveTypes.insert(candidate.Block);if(++added>=quota)break;}
            std::array<std::size_t,10> positions{};
            for(int i=1;i<10;++i)sort(tiers[i]);added=0;bool progress=true;
            while(progress&&added<quota)
            {
                Check();progress=false;
                for(int i=1;i<10&&added<quota;++i)
                    while(positions[i]<tiers[i].size())
                        if(take(tiers[i][positions[i]++])){++added;progress=true;break;}
            }
            std::pmr::set<std::pair<int,int>> shapes(Memory);
            for(const auto& candidate:answer){Check();const auto& macro=Macros[candidate.Block];shapes.emplace(macro.Width,macro.Height);}
            CandidateList byShape(Memory);
            for(const auto& [shape,candidate]:shapeBest){Check();byShape.push_back(candidate);}
            sort(byShape);added=0;
            for(const auto& candidate:byShape)
            {
                Check();const auto& macro=Macros[candidate.Block];
                if(!shapes.contains({macro.Width,macro.Height})&&take(candidate))
                {shapes.emplace(macro.Width,macro.Height);if(++added>=quota)break;}
            }
            // Exact weighted Jaccard on counts, against the score leader.
            // Hashes only bound the candidate pool; no source ID enters it.
            std::pmr::vector<CandidateList> byDistance(Memory);byDistance.resize(4);
            std::array<std::size_t,4> distances{};
            if(!ranked.empty())for(auto& heap:inventories)
            {
                sort(heap);
                for(const auto& candidate:heap)
                {
                    Probe();const auto& a=Macros[candidate.Block];const auto& b=Macros[ranked[0].Block];
                    std::size_t i=0,j=0;int shared=0;
                    while(i<a.Uses.size()&&j<b.Uses.size())
                    {
                        Check();
                        if(a.Uses[i].first<b.Uses[j].first)++i;
                        else if(b.Uses[j].first<a.Uses[i].first)++j;
                        else{shared+=std::min(a.Uses[i].second,b.Uses[j].second);++i;++j;}
                    }
                    const int unionCount=a.Members+b.Members-shared;
                    byDistance[std::min(3,4*(unionCount-shared)/unionCount)].push_back(candidate);
                }
            }
            for(auto& list:byDistance)sort(list);added=0;progress=true;
            while(progress&&added<quota)
            {
                Check();progress=false;
                for(int i=3;i>=0&&added<quota;--i)
                    while(distances[i]<byDistance[i].size())
                        if(take(byDistance[i][distances[i]++])){++added;progress=true;break;}
            }
            for(const auto& candidate:ranked)take(candidate);sort(answer);++Stats.CandidateSets;
            int primitiveCount=0,smallCount=0;shapes.clear();
            for(const auto& candidate:answer)
            {
                Check();const auto& macro=Macros[candidate.Block];primitiveCount+=macro.Members==1;
                smallCount+=macro.Members>=2&&macro.Members<=7;shapes.emplace(macro.Width,macro.Height);
            }
            Stats.PrimitiveSelections+=static_cast<std::size_t>(primitiveCount);Stats.SmallSelections+=static_cast<std::size_t>(smallCount);
            if(state.Placements.empty()&&!greedy)
            {Stats.RootPrimitiveCount=primitiveCount;Stats.RootSmallCount=smallCount;Stats.RootShapeCount=static_cast<int>(shapes.size());}
            return answer;
        }
        State Apply(const State& old,const Candidate& candidate)
        {
            Probe();if(Stats.Applied>=Limits.Applied)
            {
                Stats.AppliedLimitReached=true;
                if(Limits.AllowInternalPartial)throw RolloutPhaseStopped{};
                throw RectangleBlockInterrupted{};
            }++Stats.Applied;
            State state=Clone(old);const auto& macro=Macros[candidate.Block];
            for(const auto [type,count]:macro.Uses){Check();state.Remaining[type]-=count;}
            state.Area+=macro.Width*macro.Height;state.Count+=macro.Members;
            state.Placements.push_back({candidate.Block,candidate.X,candidate.Y});
            const Rect placed{candidate.X,candidate.Y,macro.Width,macro.Height};
            // Availability can only decrease along a branch. Stable filtering
            // retains canonical macro order for all subsequent score ties.
            state.Active.erase(std::remove_if(state.Active.begin(),state.Active.end(),[&](int id){return !Available(state,Macros[id]);}),state.Active.end());
            Check(true);std::pmr::vector<Rect> spaces(Memory);spaces.reserve(state.Spaces.size()*2);
            for(const auto& r:state.Spaces)
            {
                Probe();if(!Overlaps(r,placed)){spaces.push_back(r);continue;}
                if(candidate.X>r.X)spaces.push_back({r.X,r.Y,candidate.X-r.X,r.Height});
                if(candidate.X+macro.Width<r.X+r.Width)spaces.push_back({candidate.X+macro.Width,r.Y,r.X+r.Width-candidate.X-macro.Width,r.Height});
                if(candidate.Y>r.Y)spaces.push_back({r.X,r.Y,r.Width,candidate.Y-r.Y});
                if(candidate.Y+macro.Height<r.Y+r.Height)spaces.push_back({r.X,candidate.Y+macro.Height,r.Width,r.Y+r.Height-candidate.Y-macro.Height});
            }
            std::pmr::vector<Rect> maximal(Memory);
            for(std::size_t i=0;i<spaces.size();++i)
            {
                const auto& r=spaces[i];bool contained=false;
                for(std::size_t j=0;j<spaces.size();++j)
                {Probe();if(i!=j&&Contains(spaces[j],r)&&(!Contains(r,spaces[j])||j<i)){contained=true;break;}}
                if(contained)continue;
                bool fits=false;
                for(std::size_t k=0;k<Types.size();++k)
                {Probe();if(state.Remaining[k]&&Types[k].Width<=r.Width&&Types[k].Height<=r.Height){fits=true;break;}}
                if(fits)maximal.push_back(r);
            }
            std::sort(maximal.begin(),maximal.end(),[&](const auto& a,const auto& b)
            {Check();return std::tie(a.Y,a.X,a.Height,a.Width)<std::tie(b.Y,b.X,b.Height,b.Width);});
            state.Spaces=std::move(maximal);return state;
        }
        void Accept(const State& state)
        {
            if(state.Area>Best.Area||(state.Area==Best.Area&&state.Count>Best.Count))Best=Clone(state);
            if(state.Count==Total)throw Complete{};
        }
        bool CanComplete(const State& state)
        {
            // Overlapping maximal spaces overcount capacity. This is only a
            // necessary upper bound and never a certificate of packability.
            for(std::size_t i=0;i<Types.size();++i)
            {
                Probe();if(!state.Remaining[i])continue;const auto& type=Types[i];int capacity=0;
                for(const auto& r:state.Spaces)
                {Probe();capacity+=(r.Width/type.Width)*(r.Height/type.Height);if(capacity>=state.Remaining[i])break;}
                if(capacity<state.Remaining[i])return false;
            }
            return true;
        }
        int Rollout(const State& seed)
        {
            ++Stats.Rollouts;State state=Clone(seed);Accept(state);
            while(state.Count<Total)
            {
                Check(true);auto next=Candidates(state,16,true);if(next.empty())break;bool advanced=false;
                for(const auto& option:next)
                {
                    Check(true);State child=Apply(state,option);Accept(child);
                    if(CanComplete(child)){state=std::move(child);advanced=true;break;}
                }
                if(!advanced)break;
            }
            return state.Area;
        }
        std::pmr::string Key(const State& state)
        {
            std::pmr::string key(Memory);
            const auto append=[&](int value){Check();key.push_back(char(value&255));key.push_back(char((value>>8)&255));};
            for(int count:state.Remaining)append(count);
            for(const auto& r:state.Spaces){append(r.X);append(r.Y);append(r.Width);append(r.Height);}
            return key;
        }
    };
}

bool SearchRectangleBlockRollout(int width,int height,std::span<const RectangleBlockType> types,
    std::span<const RectangleBlockMacro> macros,RectangleBlockBudget& budget,std::pmr::memory_resource* memory,
    std::pmr::vector<RectangleBlockPlacement>& placement,RectangleBlockRolloutStats& stats,BlockRolloutPolicy policy)
{
    try
    {RolloutSearch search(width,height,types,macros,budget,memory,stats,policy);return search.Run(placement);}
    catch(const RectangleBlockInterrupted&){placement.clear();return false;}
}

RectangleBlockSnapshotStatus SearchRectangleBlockRolloutSnapshot(int width,int height,
    std::span<const RectangleBlockType> types,std::span<const RectangleBlockMacro> macros,
    RectangleBlockBudget& budget,std::pmr::memory_resource* memory,const RectangleBlockRolloutLimits& limits,
    std::pmr::vector<RectangleBlockPlacement>& placement,RectangleBlockRolloutStats& stats)
{
    placement.clear();
    if(!limits.AllowInternalPartial||limits.BeamWidth<1||limits.BeamWidth>1024||limits.Shortlist<1||limits.Shortlist>64||!limits.Applied)
        return RectangleBlockSnapshotStatus::Stopped;
    try
    {
        RolloutSearch search(width,height,types,macros,budget,memory,stats,BlockRolloutPolicy::Diverse,limits);
        if(!search.Run(placement))return RectangleBlockSnapshotStatus::Stopped;
        int total=0;for(const auto& type:types){ProbeRectangleBlockBudget(budget);total+=type.Quantity;}
        return stats.SnapshotCount==total&&stats.SnapshotArea==width*height?RectangleBlockSnapshotStatus::Complete:RectangleBlockSnapshotStatus::Partial;
    }
    catch(const RectangleBlockInterrupted&){}
    catch(const RolloutPhaseStopped&){stats.PhaseLimitReached=true;}
    catch(const std::bad_alloc&){stats.MemoryLimitReached=true;}
    placement.clear();return RectangleBlockSnapshotStatus::Stopped;
}
}
