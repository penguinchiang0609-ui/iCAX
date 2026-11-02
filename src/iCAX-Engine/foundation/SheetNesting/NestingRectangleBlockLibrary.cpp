#include "NestingRectangleDirections.h"
#include "NestingRectangleBlockSearch.h"
#include "NestingRectangleResidualCorners.h"
#include "NestingRectangleRepairRegions.h"
#include "NestingRectangleStripeRegions.h"

#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <numeric>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    using Budget=RectangleBlockBudget;
    using LimitedMemory=RectangleBlockMemory;
    using Inventory = std::array<std::uint64_t,8>;
    using Dimension = std::pair<int,int>;
    constexpr std::size_t MaxInstances = 512;
    constexpr std::size_t MaxNodes = 2000000;
    constexpr std::size_t LibraryBytes = 512ull*1024*1024;
    constexpr std::size_t RootBytes = 128ull*1024*1024;
    constexpr std::size_t RepairRootBytes = 2048ull*1024*1024;
    constexpr int MaxSide = 1000000;

    enum class SearchPolicy {RoleOrder,InventoryMrv,FairRollout,FairRolloutDiverse,FairResidual,ResidualGenerator,FairRepair,RepairRegion};
    bool UsesFairConstruction(SearchPolicy policy)
    {return policy==SearchPolicy::FairRollout||policy==SearchPolicy::FairRolloutDiverse||policy==SearchPolicy::FairResidual||policy==SearchPolicy::ResidualGenerator||policy==SearchPolicy::FairRepair;}
    bool UsesDiverseAdmission(SearchPolicy policy)
    {return UsesFairConstruction(policy)&&policy!=SearchPolicy::ResidualGenerator;}
    bool UsesResidualSearch(SearchPolicy policy)
    {return policy==SearchPolicy::FairResidual||policy==SearchPolicy::ResidualGenerator;}
    bool CountsDetailedConstruction(SearchPolicy policy)
    {return UsesResidualSearch(policy)||policy==SearchPolicy::FairRepair||policy==SearchPolicy::RepairRegion;}

    struct ResidualSearchStats
    {
        std::size_t InitialNodes=0,InitialChecks=0,Selected=0,Considered=0,Skipped=0,Trials=0;
        std::size_t Generated=0,GenerationChecks=0,SelectionChecks=0,PlacementChecks=0;
        std::size_t MrvNodes=0,RootBranches=0,RootTruncations=0,GenerationLimits=0,TrialLimits=0,MemoryLimits=0;
        int CoreWidth=0,CoreHeight=0,CoreMembers=0;
        bool Complement=false;
    };
    struct ResidualTrialStats
    {
        int SelectionIndex=0,Width=0,Height=0,Members=0;
        std::uint64_t CoreHash=0,LibraryHash=0,SelectedHash=0;
        std::size_t Generated=0,GenerationChecks=0,InventoryAdds=0,Selected=0,PlacementChecks=0,Nodes=0,Branches=0,Truncations=0;
        bool GenerationLimited=false,TrialLimited=false,Complete=false;
    };
    struct RepairTrialStats
    {
        RectangleRepairBounds Bounds;
        std::size_t Members=0,Generated=0,Checks=0;
        bool Complete=false,PhaseLimited=false,LibraryLimited=false,RootLimited=false;
    };
    struct RepairSearchStats
    {
        std::size_t InitialNodes=0,InitialChecks=0,RegionNodes=0,RegionChecks=0,SnapshotChecks=0;
        std::size_t Considered=0,Skipped=0,LibraryPeak=0,RootPeak=0;
        int PartialCount=0,PartialArea=0;
        bool Unsupported=false,InvalidSnapshot=false,LibraryReleased=false;
        RectangleBlockRolloutStats Rollout;
        RectangleRepairRegionStats Regions;
        RectangleStripeRegionStats Stripe;
        std::vector<RepairTrialStats> Trials;
    };

    struct Type { int Width,Height,Quantity,Limb=0,Shift=0,Bits=0; };
    struct Node
    {
        int Width=0,Height=0,Members=0,TypeIndex=-1;
        Inventory Used{};
        std::array<int,5> Children{};
        int ChildCount=0;
    };
    static_assert(std::is_trivially_copyable_v<Node> && sizeof(Node)<=112);

    // Stable fixed-size segments avoid the old/new whole-vector allocation
    // overlap at growth boundaries. Both segments and their index are charged
    // to the same 512 MiB library resource; no candidate is evicted or reordered.
    class NodeStorage
    {
    public:
        explicit NodeStorage(std::pmr::memory_resource* resource):Allocator(resource),Blocks(resource){}
        ~NodeStorage()
        {
            auto remaining=Count;
            for(auto* block:Blocks)
            {
                const auto used=std::min(remaining,BlockSize);std::destroy_n(block,used);
                Allocator.deallocate(block,BlockSize);remaining-=used;
            }
        }
        NodeStorage(const NodeStorage&)=delete;
        NodeStorage& operator=(const NodeStorage&)=delete;
        std::size_t size()const noexcept{return Count;}
        Node& operator[](std::size_t index)noexcept{return Blocks[index/BlockSize][index%BlockSize];}
        const Node& operator[](std::size_t index)const noexcept{return Blocks[index/BlockSize][index%BlockSize];}
        void ReserveOne()
        {
            if(Count<Blocks.size()*BlockSize)return;
            if(Blocks.size()==Blocks.capacity())Blocks.reserve(std::max<std::size_t>(1,Blocks.capacity()*2));
            auto* block=Allocator.allocate(BlockSize);Blocks.push_back(block);
        }
        void push_back(const Node& node)noexcept
        {
            assert(Count<Blocks.size()*BlockSize);
            std::construct_at(&(*this)[Count],node);++Count;
        }
    private:
        static constexpr std::size_t BlockSize=4096;
        std::pmr::polymorphic_allocator<Node> Allocator;
        std::pmr::vector<Node*> Blocks;
        std::size_t Count=0;
    };
    struct Key
    {
        int Width,Height;Inventory Used;
        bool operator==(const Key&)const=default;
    };
    struct Hash
    {
        std::size_t operator()(const Key& key)const noexcept
        {
            std::uint64_t h=std::uint64_t(key.Width)*0x9e3779b97f4a7c15ULL+std::uint64_t(key.Height);
            for(auto x:key.Used)h^=x+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2);
            return static_cast<std::size_t>(h);
        }
    };
    struct Leaf { int TypeIndex,X,Y; };
    using Indices=std::pmr::vector<int>;
    using Groups=std::pmr::map<Dimension,Indices>;
    using CountRanges=std::pmr::map<Dimension,std::pair<int,int>>;
    using InventoryUpper=std::pmr::map<Dimension,Inventory>;

    class BlockLibrary
    {
        struct FairWork {int Id,Phase=0,Lo=0,Hi=-1;bool Initialized=false,Back=true;};
    public:
        BlockLibrary(int width,int height,const std::vector<Type>& types,Budget& budget,SearchPolicy policy,
            LimitedMemory* sharedLibrary=nullptr,LimitedMemory* sharedRoot=nullptr,int coreWidth=0,int coreHeight=0)
          : W(width),H(height),Types(types),Job(budget),Policy(policy),OwnedLibraryMemory(LibraryBytes),OwnedRootMemory(RootBytes),
            LibraryMemory(sharedLibrary?*sharedLibrary:OwnedLibraryMemory),RootMemory(sharedRoot?*sharedRoot:OwnedRootMemory),
            Nodes(&LibraryMemory),Lookup(&LibraryMemory),ByWidth(&LibraryMemory),ByHeight(&LibraryMemory),Queue(&LibraryMemory),
            TierQueues(&LibraryMemory),Admission(&LibraryMemory),RolloutPlacement(&RootMemory),ResidualSolution(&RootMemory),ResidualTrials(&RootMemory),CoreWidth(coreWidth),CoreHeight(coreHeight)
        {
            const auto remaining=Job.Settings.MaxPlacementAttempts-Job.Attempts;
            LibraryAttemptEnd=Job.Attempts+remaining-remaining/5;
            LibraryTimeEnd=Job.Elapsed()+std::max(0.,Job.Settings.TimeLimitSeconds-Job.Elapsed())*.75;
            if(Policy==SearchPolicy::ResidualGenerator)
            {
                LibraryAttemptEnd=Job.Attempts+std::min<std::size_t>(remaining,100000000);
                LibraryTimeEnd=Job.Settings.TimeLimitSeconds;
            }
            if(Policy==SearchPolicy::RepairRegion)
            {
                const auto phaseChecks=std::min<std::size_t>(remaining,30000000000ull);
                PhaseAttemptEnd=Job.Attempts+phaseChecks;
                PhaseTimeEnd=std::min(Job.Settings.TimeLimitSeconds,Job.Elapsed()+120.);
                LibraryAttemptEnd=Job.Attempts+phaseChecks-phaseChecks/5;
                LibraryTimeEnd=Job.Elapsed()+std::max(0.,PhaseTimeEnd-Job.Elapsed())*.75;
            }
            int limb=0,shift=0;
            for(auto& t:Types)
            {
                int bits=0;for(int q=t.Quantity;q;q>>=1)++bits;
                if(shift+bits+1>64){++limb;shift=0;}
                if(limb>=8){Supported=false;return;}
                t.Limb=limb;t.Shift=shift;t.Bits=bits;Demand+=t.Quantity;
                Target[limb]|=std::uint64_t(t.Quantity)<<shift;
                Bias[limb]|=((std::uint64_t(1)<<bits)-1-t.Quantity)<<shift;
                Guard[limb]|=std::uint64_t(1)<<(shift+bits);
                shift+=bits+1;
            }
            Limbs=limb+1;
            if(UsesFairConstruction(Policy))TierQueues.resize(static_cast<std::size_t>(Demand)+1);
        }

        bool Supported=true,HasSolution=false,LibraryLimited=false,RootLimited=false,PhaseLimited=false,HasPartialSnapshot=false;
        std::size_t CandidateCount()const{return Nodes.size()+ResidualGenerated;}
        std::size_t LibraryPeak()const{return LibraryMemory.Peak;}
        std::size_t RootPeak()const{return RootMemory.Peak;}
        RectangleBlockRolloutStats RolloutStats;
        ResidualSearchStats ResidualStats;
        const std::pmr::vector<ResidualTrialStats>& TrialStatistics()const{return ResidualTrials;}
        void Run()
        {
            if(!Supported)return;
            try
            {
                for(std::size_t i=0;i<Types.size();++i)
                {
                    if(StageStop()||!Probe())break;
                    const auto& t=Types[i];Node n;n.Width=t.Width;n.Height=t.Height;n.Members=1;n.TypeIndex=static_cast<int>(i);
                    n.Used[t.Limb]=std::uint64_t(1)<<t.Shift;Insert(n);
                }
                SeedBinary();SeedWheels();
                if(UsesFairConstruction(Policy))FairBinaryClosure();else BinaryClosure();
            }
            catch(const std::bad_alloc&){LibraryLimited=true;}
            // A cap does not invalidate retained alternatives. The root lives
            // outside Nodes, so no final node allocation is required to publish.
            if(Policy!=SearchPolicy::ResidualGenerator&&!HasSolution&&!Stop())
                try
                {
                    if(Policy==SearchPolicy::RoleOrder)RootCover();
                    else if(Policy==SearchPolicy::InventoryMrv||Policy==SearchPolicy::RepairRegion)RootCoverMrv();
                    else if(Policy==SearchPolicy::FairResidual)ResidualCover();
                    else RolloutCover();
                }
                catch(const std::bad_alloc&){RootLimited=true;}
                catch(const RectangleBlockInterrupted&){}
        }
        template<class Container>bool Leaves(Container& output)
        {
            if(!HasSolution)return false;
            if(!ResidualSolution.empty())
            {
                for(const auto& leaf:ResidualSolution){if(!Probe()||Poll())return false;output.push_back(leaf);}
                return output.size()==static_cast<std::size_t>(Demand);
            }
            if(!RolloutPlacement.empty())
            {
                for(const auto& p:RolloutPlacement)if(!Expand(Nodes[p.Node],p.X,p.Y,output))return false;
                return output.size()==static_cast<std::size_t>(Demand);
            }
            return Expand(Solution,0,0,output)&&output.size()==static_cast<std::size_t>(Demand);
        }
        template<class Container>bool SnapshotLeaves(Container& output)
        {
            if(HasSolution)return Leaves(output);
            if(!HasPartialSnapshot)return false;
            for(const auto& p:RolloutPlacement)if(!Expand(Nodes[p.Node],p.X,p.Y,output))return false;
            return output.size()==static_cast<std::size_t>(RolloutStats.SnapshotCount);
        }

    private:
        int W,H,Limbs=0,Demand=0;
        std::vector<Type> Types; // At most 512 bounded input types.
        Budget& Job;
        SearchPolicy Policy;
        LimitedMemory OwnedLibraryMemory,OwnedRootMemory;
        LimitedMemory& LibraryMemory;
        LimitedMemory& RootMemory;
        NodeStorage Nodes;
        std::pmr::unordered_map<Key,int,Hash> Lookup;
        std::pmr::unordered_map<int,Indices> ByWidth,ByHeight;
        using Rank=std::tuple<int,std::int64_t,int>;
        std::pmr::vector<Rank> Queue;
        std::pmr::vector<std::pmr::deque<FairWork>> TierQueues;
        std::pmr::unordered_map<std::uint64_t,int> Admission;
        std::pmr::vector<RectangleBlockPlacement> RolloutPlacement;
        std::pmr::vector<Leaf> ResidualSolution;
        std::pmr::vector<ResidualTrialStats> ResidualTrials;
        int CoreWidth=0,CoreHeight=0;
        std::size_t ResidualGenerated=0;
        std::size_t InventoryAdds=0;
        int NextTier=1;
        Inventory Target{},Bias{},Guard{};
        Node Solution;
        std::size_t LibraryAttemptEnd=0;
        double LibraryTimeEnd=0;
        std::size_t PhaseAttemptEnd=0;
        double PhaseTimeEnd=0;

        bool LocalPhaseStop()
        {
            if(Policy!=SearchPolicy::RepairRegion)return false;
            if(PhaseLimited||Job.Attempts>=PhaseAttemptEnd||Job.SampledElapsed>=PhaseTimeEnd)
            {PhaseLimited=true;return true;}return false;
        }
        bool Stop(){return Job.Stop()||LocalPhaseStop();}
        bool Poll()
        {
            if(Policy==SearchPolicy::RepairRegion&&Job.Stop())return true;
            return Job.Poll()||LocalPhaseStop();
        }
        bool Probe(){return !LocalPhaseStop()&&Job.Probe();}
        bool Interrupted()const{return Job.Exhausted||Job.Cancelled||PhaseLimited;}
        void CheckBudget(bool force=false){if(force?Poll():Stop())throw RectangleBlockInterrupted{};}
        void ProbeBudget(){if(!Probe())throw RectangleBlockInterrupted{};}

        bool StageStop()
        {
            if(HasSolution||LibraryLimited||Stop())return true;
            if(UsesFairConstruction(Policy)&&Nodes.size()>=(Policy==SearchPolicy::ResidualGenerator?50000:1000000)){LibraryLimited=true;return true;}
            if(Job.Attempts>=LibraryAttemptEnd||Job.SampledElapsed>=LibraryTimeEnd)
            { LibraryLimited=true;return true; }
            return false;
        }
        bool SeedStop(){return StageStop()||((UsesFairConstruction(Policy)||Policy==SearchPolicy::RepairRegion)&&Nodes.size()>=(Policy==SearchPolicy::ResidualGenerator?5000:100000));}
        // Exactly one shared probe for every candidate inventory addition,
        // including rejected/over-quota additions at every recursive depth.
        bool Add(const Inventory& a,const Inventory& b,Inventory& out)
        {
            if(!Probe())return false;
            if(CountsDetailedConstruction(Policy))++InventoryAdds;
            for(int l=0;l<Limbs;++l)
            {out[l]=a[l]+b[l];if((out[l]+Bias[l])&Guard[l])return false;}
            return true;
        }
        Inventory Complement(const Inventory& used)const
        {Inventory result{};for(int l=0;l<Limbs;++l)result[l]=Target[l]-used[l];return result;}
        bool Coexists(const Node& n)
        {
            if(CountsDetailedConstruction(Policy)&&(StageStop()||!Probe()))return false;
            if(Policy==SearchPolicy::ResidualGenerator&&!((n.Width<=W-CoreWidth&&n.Height<=H)||(n.Width<=W&&n.Height<=H-CoreHeight)))return false;
            for(const auto& t:Types)
            {
                if(CountsDetailedConstruction(Policy)&&(StageStop()||!Probe()))return false;
                const auto used=(n.Used[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1);
                if(used==static_cast<std::uint64_t>(t.Quantity))continue;
                if((n.Width+t.Width>W&&n.Height+t.Height>H)||
                   (n.Width==W&&t.Height>H-n.Height)||(n.Height==H&&t.Width>W-n.Width))return false;
            }
            return true;
        }
        template<class T>static void ReserveOne(std::pmr::vector<T>& v)
        {
            if(v.size()==v.capacity())v.reserve(std::min(MaxNodes,std::max<std::size_t>(v.size()+1,v.capacity()*2)));
        }
        static void ReserveOne(NodeStorage& nodes){nodes.ReserveOne();}
        Node Binary(int a,int b,bool horizontal,const Inventory& used)const
        {
            const auto& x=Nodes[a];const auto& y=Nodes[b];Node n;
            n.Width=horizontal?x.Width+y.Width:x.Width;n.Height=horizontal?x.Height:x.Height+y.Height;
            n.Members=x.Members+y.Members;n.Used=used;n.ChildCount=2;n.Children[0]=a;n.Children[1]=b;
            return n;
        }
        void Insert(const Node& n)
        {
            if(HasSolution)return;
            if(Policy!=SearchPolicy::ResidualGenerator&&n.Width==W&&n.Height==H&&n.Used==Target){Solution=n;HasSolution=true;return;}
            const Key key{n.Width,n.Height,n.Used};if(Lookup.find(key)!=Lookup.end())return;
            std::uint64_t bucket=0;
            if(UsesDiverseAdmission(Policy)&&n.Members>3)
            {
                bucket=(((std::uint64_t(n.Width)*(H+1)+n.Height)*(Demand+1)+n.Members)*8)+(Hash{}(key)&7);
                const auto admission=Admission.find(bucket);
                if(admission!=Admission.end()&&admission->second>=2)
                {
                    bool completes=false;
                    if(n.Width==W&&n.Height<H)
                    {if(!Probe())return;completes=Lookup.contains({W,H-n.Height,Complement(n.Used)});}
                    if(!completes&&n.Height==H&&n.Width<W)
                    {if(!Probe())return;completes=Lookup.contains({W-n.Width,H,Complement(n.Used)});}
                    if(!completes){++RolloutStats.DiversityPrunes;return;} // Heuristic truncation, not an infeasibility proof.
                }
            }
            const auto nodeCap=Policy==SearchPolicy::ResidualGenerator?50000:UsesFairConstruction(Policy)?1000000:Policy==SearchPolicy::RepairRegion?1999999:MaxNodes;
            if(Nodes.size()>=nodeCap){LibraryLimited=true;return;}
            if(Poll())return;
            // Prepare allocations before committing nodes and indexes. A fair
            // queue entry is rolled back if the final lookup allocation fails.
            auto& widths=ByWidth[n.Width];auto& heights=ByHeight[n.Height];
            ReserveOne(Nodes);ReserveOne(widths);ReserveOne(heights);
            auto admitted=Admission.end();
            if(UsesFairConstruction(Policy))
            {
                if(UsesDiverseAdmission(Policy)&&n.Members>3)admitted=Admission.try_emplace(bucket,0).first;
                TierQueues[n.Members].push_back({static_cast<int>(Nodes.size())});
            }
            else ReserveOne(Queue);
            const int id=static_cast<int>(Nodes.size());
            try{Lookup.emplace(key,id);}
            catch(...){if(UsesFairConstruction(Policy))TierQueues[n.Members].pop_back();throw;}
            if(admitted!=Admission.end())++admitted->second;
            Nodes.push_back(n);widths.push_back(id);heights.push_back(id);
            if(!UsesFairConstruction(Policy))
            {Queue.emplace_back(-n.Members,-std::int64_t(n.Width)*n.Height,id);std::push_heap(Queue.begin(),Queue.end());}
            if(Poll())return;
            if(Policy==SearchPolicy::ResidualGenerator){CloseResidual(id);return;}
            if(n.Width==W&&n.Height<H)
            {
                if(!Probe())return;
                const auto found=Lookup.find({W,H-n.Height,Complement(n.Used)});
                if(found!=Lookup.end()){Solution=Binary(id,found->second,false,Target);HasSolution=true;return;}
            }
            if(n.Height==H&&n.Width<W)
            {
                if(!Probe())return;
                const auto found=Lookup.find({W-n.Width,H,Complement(n.Used)});
                if(found!=Lookup.end()){Solution=Binary(id,found->second,true,Target);HasSolution=true;}
            }
        }
        bool BinarySeedStop(){return Policy==SearchPolicy::RepairRegion?StageStop():SeedStop();}
        void SeedBinary()
        {
            for(std::size_t a=0;a<Nodes.size()&&!BinarySeedStop();++a)
            {
                const Node x=Nodes[a];
                for(bool horizontal:{true,false})
                {
                    const auto& peers=horizontal?ByHeight.at(x.Height):ByWidth.at(x.Width);
                    const auto count=peers.size();
                    for(std::size_t j=0;j<count&&!BinarySeedStop();++j)
                    {
                        const int b=peers[j];const Node y=Nodes[b];if(x.Members+y.Members>3)continue;
                        Inventory used{};if(!Add(x.Used,y.Used,used))continue;
                        auto n=Binary(static_cast<int>(a),b,horizontal,used);
                        if(n.Width<=W&&n.Height<=H&&Coexists(n))Insert(n);
                    }
                    if(BinarySeedStop())break;
                }
            }
        }
        void SeedWheels()
        {
            if(SeedStop())return;
            Groups seeds(&LibraryMemory);
            std::pmr::map<int,std::pmr::vector<Dimension>> widths(&LibraryMemory),heights(&LibraryMemory);
            for(std::size_t i=0;i<Nodes.size();++i)
            {
                if(SeedStop())return;
                if(Nodes[i].Members<=3)seeds[{Nodes[i].Width,Nodes[i].Height}].push_back(static_cast<int>(i));
            }
            for(const auto& [d,ids]:seeds){widths[d.first].push_back(d);heights[d.second].push_back(d);}
            for(const auto& [e,eids]:seeds)for(const auto& [a,aids]:seeds)
            {
                if(SeedStop())return;
                const int dw=a.first-e.first,bh=a.second+e.second;if(dw<=0)continue;
                const auto bs=heights.find(bh);if(bs==heights.end())continue;
                for(auto b:bs->second)
                {
                    if(SeedStop())return;
                    const auto cs=widths.find(b.first+e.first);if(cs==widths.end())continue;
                    for(auto c:cs->second)
                    {
                        if(SeedStop())return;
                        const int w=a.first+b.first,h=bh+c.second;if(w>W||h>H)continue;
                        const auto ds=seeds.find({dw,c.second+e.second});if(ds==seeds.end())continue;
                        std::array<const Indices*,5> lists{&aids,&seeds.at(b),&seeds.at(c),&ds->second,&eids};
                        Node node;node.Width=w;node.Height=h;node.ChildCount=5;
                        const auto join=[&](auto&& self,int depth,const Inventory& used,int members)->void
                        {
                            if(SeedStop())return;
                            if(depth==5){node.Used=used;node.Members=members;if(Coexists(node))Insert(node);return;}
                            for(int id:*lists[depth])
                            {
                                if(SeedStop())return;
                                Inventory next{};if(!Add(used,Nodes[id].Used,next))continue;
                                node.Children[depth]=id;self(self,depth+1,next,members+Nodes[id].Members);
                            }
                        };
                        const Inventory empty{};join(join,0,empty,0);
                    }
                }
            }
        }
        void BinaryClosure()
        {
            while(!Queue.empty()&&!StageStop())
            {
                std::pop_heap(Queue.begin(),Queue.end());const int a=std::get<2>(Queue.back());Queue.pop_back();
                const Node x=Nodes[a];
                for(bool horizontal:{true,false})
                {
                    const auto& peers=horizontal?ByHeight.at(x.Height):ByWidth.at(x.Width);const auto count=peers.size();
                    for(std::size_t j=0;j<count&&!StageStop();++j)
                    {
                        const int b=peers[j];Inventory used{};if(!Add(x.Used,Nodes[b].Used,used))continue;
                        auto n=Binary(a,b,horizontal,used);if(n.Width<=W&&n.Height<=H&&Coexists(n))Insert(n);
                    }
                    if(StageStop())break;
                }
            }
        }
        void FairBinaryClosure()
        {
            while(!StageStop())
            {
                int tier=-1;
                for(int tried=0;tried<Demand;++tried)
                {
                    if(Stop())return;
                    const int candidate=NextTier;NextTier=NextTier%Demand+1;
                    if(!TierQueues[candidate].empty()){tier=candidate;break;}
                }
                if(tier<0)return;
                auto work=TierQueues[tier].front();TierQueues[tier].pop_front();
                const Node a=Nodes[work.Id];int turns=0;
                while(work.Phase<2&&turns<64&&!StageStop())
                {
                    const bool horizontal=work.Phase==0;
                    const auto& ids=horizontal?ByHeight.at(a.Height):ByWidth.at(a.Width);
                    if(!work.Initialized)
                    {
                        work.Lo=0;work.Hi=static_cast<int>(std::upper_bound(ids.begin(),ids.end(),work.Id)-ids.begin())-1;
                        work.Initialized=true;work.Back=true;
                    }
                    if(work.Lo>work.Hi){++work.Phase;work.Initialized=false;continue;}
                    const int b=work.Back?ids[work.Hi--]:ids[work.Lo++];work.Back=!work.Back;++turns;
                    Inventory used{};if(!Add(a.Used,Nodes[b].Used,used))continue;
                    const auto node=Binary(work.Id,b,horizontal,used);
                    if(node.Width<=W&&node.Height<=H&&Coexists(node))Insert(node);
                }
                if(work.Phase<2)
                {if(Poll())return;TierQueues[tier].push_back(work);if(Poll())return;}
            }
        }
        void SelectionCheck(bool force=false)
        {
            if(CountsDetailedConstruction(Policy))ProbeBudget();
            else if(!force)CheckBudget();
            if(force)CheckBudget(true);
        }
        Indices SelectMacros(std::size_t cap)
        {
            // Preserve the original 20k selector: eight largest-member and
            // eight smallest-inventory-hash choices for each exact footprint,
            // followed by one global area/member/hash order. No solved layout
            // or source identity enters either retention or rollout.
            using CountRank=std::tuple<int,std::uint64_t,int>;
            using HashRank=std::pair<std::uint64_t,int>;
            struct DiversePool
            {std::array<CountRank,9> Counts;std::array<HashRank,9> Hashes;int Count=0,HashCount=0;};
            Indices chosen(&RootMemory);
            {
                std::pmr::map<Dimension,DiversePool> pools(&RootMemory);
                for(std::size_t i=0;i<Nodes.size();++i)
                {
                    ProbeBudget();
                    const auto& n=Nodes[i];if(n.Members<2)continue;
                    const auto hash=static_cast<std::uint64_t>(Hash{}({n.Width,n.Height,n.Used}));
                    auto& pool=pools[{n.Width,n.Height}];
                    pool.Counts[pool.Count++]={n.Members,~hash,static_cast<int>(i)};
                    std::push_heap(pool.Counts.begin(),pool.Counts.begin()+pool.Count,std::greater<CountRank>{});
                    if(pool.Count>8){std::pop_heap(pool.Counts.begin(),pool.Counts.begin()+pool.Count,std::greater<CountRank>{});--pool.Count;}
                    pool.Hashes[pool.HashCount++]={hash,static_cast<int>(i)};
                    std::push_heap(pool.Hashes.begin(),pool.Hashes.begin()+pool.HashCount);
                    if(pool.HashCount>8){std::pop_heap(pool.Hashes.begin(),pool.Hashes.begin()+pool.HashCount);--pool.HashCount;}
                }
                std::pmr::set<int> unique(&RootMemory);
                for(const auto& [dimension,pool]:pools)
                {
                    SelectionCheck();
                    for(int i=0;i<pool.Count;++i)unique.insert(std::get<2>(pool.Counts[i]));
                    for(int i=0;i<pool.HashCount;++i)unique.insert(pool.Hashes[i].second);
                }
                SelectionCheck(true);chosen.assign(unique.begin(),unique.end());SelectionCheck(true);
            }
            std::sort(chosen.begin(),chosen.end(),[&](int a,int b)
            {
                SelectionCheck();
                const auto& x=Nodes[a];const auto& y=Nodes[b];
                if(x.Width*x.Height!=y.Width*y.Height)return x.Width*x.Height>y.Width*y.Height;
                if(x.Members!=y.Members)return x.Members>y.Members;
                return Hash{}({x.Width,x.Height,x.Used})<Hash{}({y.Width,y.Height,y.Used});
            });
            if(chosen.size()>cap)chosen.resize(cap);
            return chosen;
        }
        void MakeMacros(Indices& chosen,std::pmr::vector<RectangleBlockType>& types,
            std::pmr::vector<RectangleBlockMacro>& macros)
        {
            if(Policy==SearchPolicy::FairRolloutDiverse||Policy==SearchPolicy::FairRepair)AppendDiverseRolloutMacros(chosen);
            // Primitive nodes always remain available, independent of selection.
            for(std::size_t i=0;i<Nodes.size();++i)
            {SelectionCheck();if(Nodes[i].Members==1)chosen.push_back(static_cast<int>(i));}
            SelectionCheck(true);types.reserve(Types.size());macros.reserve(chosen.size());SelectionCheck(true);
            for(const auto& type:Types)types.push_back({type.Width,type.Height,type.Quantity});
            for(int id:chosen)
            {
                ProbeBudget();const auto& n=Nodes[id];RectangleBlockMacro macro(&RootMemory);
                macro.Width=n.Width;macro.Height=n.Height;macro.Members=n.Members;macro.Node=id;
                for(std::size_t j=0;j<Types.size();++j)
                {
                    SelectionCheck();const auto& t=Types[j];
                    const int count=static_cast<int>((n.Used[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1));
                    if(count)macro.Uses.emplace_back(static_cast<int>(j),count);
                }
                macros.push_back(std::move(macro));
            }
            std::sort(macros.begin(),macros.end(),[&](const auto& a,const auto& b)
            {
                SelectionCheck();
                const auto x=std::tuple(-a.Width*a.Height,-a.Members,a.Width,a.Height);
                const auto y=std::tuple(-b.Width*b.Height,-b.Members,b.Width,b.Height);
                return x!=y?x<y:a.Uses<b.Uses;
            });
            macros.erase(std::unique(macros.begin(),macros.end(),[&](const auto& a,const auto& b)
            {SelectionCheck();return a.Width==b.Width&&a.Height==b.Height&&a.Uses==b.Uses;}),macros.end());
        }
        void RolloutCover()
        {
            auto chosen=SelectMacros(20000);
            std::pmr::vector<RectangleBlockType> types(&RootMemory);
            std::pmr::vector<RectangleBlockMacro> macros(&RootMemory);
            MakeMacros(chosen,types,macros);
            if(Policy==SearchPolicy::FairRepair)
            {
                RectangleBlockRolloutLimits limits;limits.AllowInternalPartial=true;limits.BeamWidth=1024;limits.Applied=100000000;
                const auto remaining=Job.Settings.MaxPlacementAttempts-Job.Attempts;
                limits.Checks=std::min<std::size_t>(100000000000ull,remaining-remaining/4);
                limits.Seconds=std::min(600.,std::max(0.,Job.Settings.TimeLimitSeconds-Job.Elapsed())*.75);
                if(!limits.Checks||limits.Seconds<=0)return;
                const auto status=SearchRectangleBlockRolloutSnapshot(W,H,types,macros,Job,&RootMemory,limits,RolloutPlacement,RolloutStats);
                HasPartialSnapshot=status!=RectangleBlockSnapshotStatus::Stopped;
                HasSolution=status==RectangleBlockSnapshotStatus::Complete;
                RootLimited=RolloutStats.MemoryLimitReached;return;
            }
            HasSolution=SearchRectangleBlockRollout(W,H,types,macros,Job,&RootMemory,RolloutPlacement,RolloutStats,
                Policy==SearchPolicy::FairRolloutDiverse?BlockRolloutPolicy::Diverse:BlockRolloutPolicy::Score);
        }
        void CloseResidual(int id)
        {
            // The final admitted node still gets its complement lookup. Node
            // capacity limits further insertion, not work on this retained node.
            const auto closureStop=[&]()
            {
                if(Stop())return true;
                if(Job.Attempts>=LibraryAttemptEnd||Job.SampledElapsed>=LibraryTimeEnd)
                {LibraryLimited=true;return true;}return false;
            };
            // The two disjoint rectangle partitions of the L outside the fixed
            // origin core. Exact inventory complements are required in either.
            const std::array<std::array<RectangleResidualFixed,2>,2> partitions{{
                {{{CoreWidth,0,W-CoreWidth,H},{0,CoreHeight,CoreWidth,H-CoreHeight}}},
                {{{0,CoreHeight,W,H-CoreHeight},{CoreWidth,0,W-CoreWidth,CoreHeight}}}}};
            const auto& node=Nodes[id];
            for(const auto& regions:partitions)for(int role=0;role<2;++role)
            {
                if(closureStop()||!Probe())return;
                const auto& r=regions[role];const auto& other=regions[1-role];
                if(r.Width<=0||r.Height<=0||node.Width!=r.Width||node.Height!=r.Height)continue;
                int peer=-1;
                if(other.Width<=0||other.Height<=0)
                {if(closureStop()||!Probe())return;if(node.Used!=Target)continue;}
                else
                {
                    if(closureStop()||!Probe())return;
                    const auto found=Lookup.find({other.Width,other.Height,Complement(node.Used)});
                    if(found==Lookup.end())continue;peer=found->second;
                }
                if(Poll())return;
                RolloutPlacement.reserve(2);
                // Preserve region order even when the newly generated node is
                // the second region. These are tree IDs, never reference poses.
                for(int k=0;k<2;++k)
                {
                    const int selected=k==role?id:peer;if(selected<0)continue;
                    if(closureStop()||!Probe()){RolloutPlacement.clear();return;}
                    RolloutPlacement.push_back({selected,regions[k].X,regions[k].Y});
                }
                if(Poll()){RolloutPlacement.clear();return;}
                HasSolution=true;ResidualStats.Complement=true;return;
            }
        }
        void ResidualCover()
        {
            ResidualStats.InitialNodes=Nodes.size();ResidualStats.InitialChecks=Job.Attempts;
            const auto selected=SelectMacros(20000);ResidualStats.Selected=selected.size();
            Groups grouped(&RootMemory);
            for(std::size_t index=0;index<selected.size();++index)
            {ProbeBudget();const auto& n=Nodes[selected[index]];grouped[{n.Width,n.Height}].push_back(static_cast<int>(index));}
            std::pmr::vector<Dimension> shapes(&RootMemory);
            for(const auto& [shape,ids]:grouped){ProbeBudget();shapes.push_back(shape);}
            std::sort(shapes.begin(),shapes.end(),[&](auto a,auto b)
            {ProbeBudget();return std::tuple(-a.first*a.second,-a.first,-a.second)<std::tuple(-b.first*b.second,-b.first,-b.second);});
            if(shapes.size()>64)shapes.resize(64);
            Indices order(&RootMemory);
            for(std::size_t round=0;order.size()<128*8;++round)
            {
                bool added=false;
                for(auto shape:shapes)
                {
                    ProbeBudget();const auto& ids=grouped.at(shape);
                    if(round<ids.size()){order.push_back(ids[round]);added=true;}
                }
                if(!added)break;
            }
            for(int selectionIndex:order)
            {
                CheckBudget(true);if(ResidualStats.Trials>=128)break;
                const int coreId=selected[selectionIndex];
                ProbeBudget();++ResidualStats.Considered;const auto& core=Nodes[coreId];
                std::vector<Type> residualTypes;residualTypes.reserve(Types.size());
                Indices mapping(&RootMemory);bool fits=true;
                for(std::size_t i=0;i<Types.size();++i)
                {
                    ProbeBudget();const auto& t=Types[i];
                    const int used=static_cast<int>((core.Used[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1));
                    const int remain=t.Quantity-used;if(!remain)continue;
                    if(!((t.Width<=W-core.Width&&t.Height<=H)||(t.Width<=W&&t.Height<=H-core.Height)))
                    {fits=false;break;}
                    residualTypes.push_back({t.Width,t.Height,remain});mapping.push_back(static_cast<int>(i));
                }
                if(!fits){++ResidualStats.Skipped;continue;}
                if(residualTypes.empty())
                {
                    if(Expand(core,0,0,ResidualSolution)){HasSolution=true;return;}ResidualSolution.clear();return;
                }
                ++ResidualStats.Trials;
                ResidualTrials.emplace_back();auto& trial=ResidualTrials.back();
                trial.SelectionIndex=selectionIndex;trial.Width=core.Width;trial.Height=core.Height;trial.Members=core.Members;
                trial.CoreHash=Hash{}({core.Width,core.Height,core.Used});
                BlockLibrary fresh(W,H,residualTypes,Job,SearchPolicy::ResidualGenerator,&LibraryMemory,&RootMemory,core.Width,core.Height);
                if(!fresh.Supported)continue;
                const auto generationStart=Job.Attempts;fresh.Run();
                ResidualStats.GenerationChecks+=Job.Attempts-generationStart;
                ResidualGenerated+=fresh.Nodes.size();ResidualStats.Generated+=fresh.Nodes.size();
                trial.Generated=fresh.Nodes.size();trial.GenerationChecks=Job.Attempts-generationStart;trial.InventoryAdds=fresh.InventoryAdds;
                trial.GenerationLimited=fresh.LibraryLimited;trial.LibraryHash=14695981039346656037ull;
                for(std::size_t i=0;i<fresh.Nodes.size();++i)
                {ProbeBudget();const auto& n=fresh.Nodes[i];trial.LibraryHash^=Hash{}({n.Width,n.Height,n.Used});trial.LibraryHash*=1099511628211ull;}
                if(fresh.LibraryLimited)++ResidualStats.GenerationLimits;
                if(Stop())return;
                const bool complementary=fresh.HasSolution;
                if(!fresh.HasSolution)
                {
                    struct SelectionAccounting
                    {
                        Budget& Job;std::size_t& Total;std::size_t Start;
                        ~SelectionAccounting(){Total+=Job.Attempts-Start;}
                    } accounting{Job,ResidualStats.SelectionChecks,Job.Attempts};
                    auto chosen=fresh.SelectMacros(4000);
                    trial.Selected=chosen.size();trial.SelectedHash=14695981039346656037ull;
                    for(int id:chosen)
                    {ProbeBudget();const auto& n=fresh.Nodes[id];trial.SelectedHash^=Hash{}({n.Width,n.Height,n.Used});trial.SelectedHash*=1099511628211ull;}
                    std::pmr::vector<RectangleBlockType> types(&RootMemory);
                    std::pmr::vector<RectangleBlockMacro> macros(&RootMemory);
                    fresh.MakeMacros(chosen,types,macros);
                    Indices remaining(&RootMemory),primitives(types.size(),-1,&RootMemory);
                    for(const auto& t:types){ProbeBudget();remaining.push_back(t.Quantity);}
                    for(std::size_t id=0;id<fresh.Nodes.size();++id)
                    {ProbeBudget();const auto& n=fresh.Nodes[id];if(n.Members==1)primitives[n.TypeIndex]=static_cast<int>(id);}
                    const std::array<RectangleResidualFixed,1> fixed{{{0,0,core.Width,core.Height}}};
                    RectangleResidualLimits limits;limits.MacroLimit=32;
                    // Uniform work slices for every generated core. Fine-grained
                    // geometry accounting makes these different from research
                    // wall-clock slices; neither is claimed equivalent.
                    limits.Probes=1000000000;limits.RootBranchProbes=20000000;
                    RectangleResidualStats stats;
                    const auto placementStart=Job.Attempts;
                    fresh.HasSolution=SearchRectangleResidualCorners(W,H,types,macros,remaining,primitives,fixed,
                        Job,&RootMemory,limits,fresh.RolloutPlacement,stats);
                    // SelectionAccounting excludes the placement phase.
                    accounting.Start+=Job.Attempts-placementStart;
                    ResidualStats.PlacementChecks+=Job.Attempts-placementStart;
                    ResidualStats.MrvNodes+=stats.Nodes;ResidualStats.RootBranches+=stats.RootBranches;
                    ResidualStats.RootTruncations+=stats.RootTruncations;
                    trial.PlacementChecks=Job.Attempts-placementStart;trial.Nodes=stats.Nodes;trial.Branches=stats.RootBranches;trial.Truncations=stats.RootTruncations;
                    trial.TrialLimited=stats.TrialNodeLimit||stats.TrialProbeLimit;
                    if(stats.TrialNodeLimit||stats.TrialProbeLimit)++ResidualStats.TrialLimits;
                    if(stats.MemoryLimit)++ResidualStats.MemoryLimits;
                }
                if(!fresh.HasSolution)continue;
                std::pmr::vector<Leaf> residualLeaves(&RootMemory);
                if(!fresh.Leaves(residualLeaves))return;
                if(!Expand(core,0,0,ResidualSolution)){ResidualSolution.clear();return;}
                for(auto leaf:residualLeaves)
                {ProbeBudget();leaf.TypeIndex=mapping.at(leaf.TypeIndex);ResidualSolution.push_back(leaf);}
                if(ResidualSolution.size()!=static_cast<std::size_t>(Demand)){ResidualSolution.clear();continue;}
                CheckBudget(true);
                ResidualStats.CoreWidth=core.Width;ResidualStats.CoreHeight=core.Height;ResidualStats.CoreMembers=core.Members;
                ResidualStats.Complement=complementary;trial.Complete=true;HasSolution=true;return;
            }
        }
        void AppendDiverseRolloutMacros(Indices& chosen)
        {
            // Preserve the complete original 20k selection, then fairly add
            // member-count, footprint and inventory alternatives up to 40k.
            // Every grouping key is derived from this generated library.
            CheckBudget(true);
            std::pmr::vector<unsigned char> selected(Nodes.size(),0,&RootMemory);
            for(int id:chosen){CheckBudget();selected[id]=1;}
            std::pmr::map<std::tuple<int,int,int>,Indices> grouped(&RootMemory);
            for(std::size_t i=0;i<Nodes.size();++i)
            {
                ProbeBudget();const auto& n=Nodes[i];
                if(n.Members>=2&&!selected[i])grouped[{n.Members,n.Width,n.Height}].push_back(static_cast<int>(i));
            }
            struct Group
            {
                int Width,Height;std::uint64_t Rank;Indices* IDs;std::size_t Cursor=0;
            };
            std::pmr::vector<Group> groups(&RootMemory);
            std::pmr::vector<Indices> tierOrder(&RootMemory);tierOrder.resize(static_cast<std::size_t>(Demand)+1);
            CheckBudget(true);groups.reserve(grouped.size());CheckBudget(true);
            for(auto& [key,ids]:grouped)
            {
                CheckBudget();const auto [members,width,height]=key;
                std::sort(ids.begin(),ids.end(),[&](int a,int b)
                {
                    CheckBudget();const auto& x=Nodes[a];const auto& y=Nodes[b];
                    const auto hx=Hash{}({x.Width,x.Height,x.Used}),hy=Hash{}({y.Width,y.Height,y.Used});
                    return hx!=hy?hx<hy:a<b;
                });
                Inventory marker{};marker[0]=static_cast<std::uint64_t>(members);
                const int index=static_cast<int>(groups.size());
                groups.push_back({width,height,Hash{}({width,height,marker}),&ids});tierOrder[members].push_back(index);
            }
            std::pmr::vector<std::pmr::deque<int>> tiers(&RootMemory);tiers.resize(static_cast<std::size_t>(Demand)+1);
            for(int members=2;members<=Demand;++members)
            {
                CheckBudget();auto& ids=tierOrder[members];
                std::sort(ids.begin(),ids.end(),[&](int a,int b)
                {CheckBudget();const auto& x=groups[a];const auto& y=groups[b];return std::tie(x.Rank,x.Width,x.Height)<std::tie(y.Rank,y.Width,y.Height);});
                tiers[members].assign(ids.begin(),ids.end());
            }
            int next=2;
            while(chosen.size()<40000&&Demand>=2)
            {
                ProbeBudget();int members=-1;
                for(int tried=0;tried<Demand-1;++tried)
                {
                    CheckBudget();const int candidate=next;next=next==Demand?2:next+1;
                    if(!tiers[candidate].empty()){members=candidate;break;}
                }
                if(members<0)break;
                const int index=tiers[members].front();tiers[members].pop_front();auto& group=groups[index];
                chosen.push_back((*group.IDs)[group.Cursor++]);
                if(group.Cursor<group.IDs->size())tiers[members].push_back(index);
            }
            std::sort(chosen.begin(),chosen.end(),[&](int a,int b)
            {
                CheckBudget();const auto& x=Nodes[a];const auto& y=Nodes[b];
                if(x.Width*x.Height!=y.Width*y.Height)return x.Width*x.Height>y.Width*y.Height;
                if(x.Members!=y.Members)return x.Members>y.Members;
                return Hash{}({x.Width,x.Height,x.Used})<Hash{}({y.Width,y.Height,y.Used});
            });
        }
        bool PrepareRoot(Groups& pool,CountRanges& ranges,InventoryUpper& upper)
        {
            if(Poll())return false;
            for(std::size_t i=0;i<Nodes.size();++i)
            {if(Stop())return false;pool[{Nodes[i].Width,Nodes[i].Height}].push_back(static_cast<int>(i));}
            for(const auto& [d,ids]:pool)
            {
                if(Stop())return false;
                int lo=Demand,hi=0;Inventory combined{};
                for(int id:ids)
                {
                    if(Stop())return false;
                    lo=std::min(lo,Nodes[id].Members);hi=std::max(hi,Nodes[id].Members);
                    for(int l=0;l<Limbs;++l)combined[l]|=Nodes[id].Used[l];
                }
                ranges[d]={lo,hi};upper[d]=combined;
            }
            return true;
        }
        void RootCover()
        {
            Groups pool(&RootMemory);CountRanges ranges(&RootMemory);InventoryUpper upper(&RootMemory);
            if(!PrepareRoot(pool,ranges,upper))return;
            for(const auto& [a,aids]:pool)for(const auto& [e,eids]:pool)
            {
                if(Stop()||HasSolution)return;
                const int dw=a.first-e.first,bh=a.second+e.second,bw=W-a.first,ch=H-bh;
                if(std::min({dw,bw,ch})<=0)continue;
                std::array<Dimension,5> roles{a,Dimension{bw,bh},Dimension{W-dw,ch},Dimension{dw,H-a.second},e};
                std::array<const Indices*,5> lists{};bool found=true;
                for(int i=0;i<5;++i){const auto p=pool.find(roles[i]);if(p==pool.end()){found=false;break;}lists[i]=&p->second;}
                if(!found)continue;
                std::array<int,5> order{0,1,2,3,4};
                std::stable_sort(order.begin(),order.end(),[&](int x,int y){return lists[x]->size()<lists[y]->size();});
                std::array<int,6> minRemaining{},maxRemaining{};
                std::array<Inventory,5> required{};std::array<int,MaxInstances> supplied{};
                for(int i=4;i>=0;--i)
                {
                    const auto [lo,hi]=ranges.at(roles[order[i]]);
                    minRemaining[i]=minRemaining[i+1]+lo;maxRemaining[i]=maxRemaining[i+1]+hi;
                    const auto& canSupply=upper.at(roles[order[i]]);
                    for(std::size_t j=0;j<Types.size();++j)
                    {
                        const auto& t=Types[j];const auto amount=(canSupply[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1);
                        supplied[j]+=std::min(static_cast<int>(amount),t.Quantity);
                        required[i][t.Limb]|=std::uint64_t(std::max(0,t.Quantity-supplied[j]))<<t.Shift;
                    }
                }
                if(Demand<minRemaining[0]||Demand>maxRemaining[0])continue;
                Node node;node.Width=W;node.Height=H;node.Members=Demand;node.Used=Target;node.ChildCount=5;
                const auto join=[&](auto&& self,int depth,const Inventory& used,int members)->bool
                {
                    if(Stop())return false;
                    if(members+minRemaining[depth]>Demand||members+maxRemaining[depth]<Demand)return false;
                    for(int l=0;l<Limbs;++l)
                        if((((used[l]|Guard[l])-required[depth][l])&Guard[l])!=Guard[l])return false;
                    const int role=order[depth];
                    if(depth==4)
                    {
                        if(!Probe())return false; // Final inventory-complement join is counted too.
                        const auto it=Lookup.find({roles[role].first,roles[role].second,Complement(used)});
                        if(it==Lookup.end())return false;
                        node.Children[role]=it->second;Solution=node;HasSolution=true;return true;
                    }
                    for(int id:*lists[role])
                    {
                        if(Stop())return false;
                        Inventory next{};if(!Add(used,Nodes[id].Used,next))continue;
                        node.Children[role]=id;if(self(self,depth+1,next,members+Nodes[id].Members))return true;
                    }
                    return false;
                };
                const Inventory empty{};if(join(join,0,empty,0))return;
            }
        }
        void RootCoverMrv()
        {
            Groups pool(&RootMemory);CountRanges ranges(&RootMemory);InventoryUpper upper(&RootMemory);
            if(!PrepareRoot(pool,ranges,upper)||Poll())return;

            // This is dimension-type presence, not source-ID information. The
            // complete mask table, recursive active lists and failure cache all
            // allocate from the same caller-owned bounded root resource.
            const std::size_t supportWords=(Types.size()+63)/64;
            std::pmr::vector<std::uint64_t> supportMasks(&RootMemory);
            supportMasks.resize(Nodes.size()*supportWords);
            if(Poll())return;
            for(std::size_t id=0;id<Nodes.size();++id)
            {
                if(Stop())return;
                for(std::size_t j=0;j<Types.size();++j)
                {
                    const auto& t=Types[j];
                    if((Nodes[id].Used[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1))
                        supportMasks[id*supportWords+j/64]|=std::uint64_t(1)<<(j%64);
                }
            }
            for(const auto& [a,aids]:pool)for(const auto& [e,eids]:pool)
            {
                if(Stop()||HasSolution)return;
                const int dw=a.first-e.first,bh=a.second+e.second,bw=W-a.first,ch=H-bh;
                if(std::min({dw,bw,ch})<=0)continue;
                const std::array<Dimension,5> roles{a,Dimension{bw,bh},Dimension{W-dw,ch},Dimension{dw,H-a.second},e};
                std::array<const Indices*,5> lists{};bool found=true;
                for(int i=0;i<5;++i){const auto p=pool.find(roles[i]);if(p==pool.end()){found=false;break;}lists[i]=&p->second;}
                if(!found)continue;
                bool impossible=false;
                for(const auto& t:Types)
                {
                    int available=0;
                    for(auto role:roles)available+=static_cast<int>((upper.at(role)[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1));
                    if(available<t.Quantity){impossible=true;break;}
                }
                if(impossible)continue;
                Node node;node.Width=W;node.Height=H;node.Members=Demand;node.Used=Target;node.ChildCount=5;
                // A memo belongs to this exact five-role geometry. It cannot be
                // reused across different role dimensions or stock sizes.
                std::pmr::set<std::pair<int,Inventory>> failed(&RootMemory);
                const auto cover=[&](auto&& self,int mask,const Inventory& used,int depth)->bool
                {
                    if(Stop())return false;
                    if(depth==5)return used==Target;
                    if(depth==4)
                    {
                        int role=0;while(mask&(1<<role))++role;
                        if(!Probe())return false;
                        const auto it=Lookup.find({roles[role].first,roles[role].second,Complement(used)});
                        if(it==Lookup.end())return false;
                        node.Children[role]=it->second;return true;
                    }
                    if(failed.contains({mask,used}))return false;
                    std::array<std::uint64_t,8> neededBits{};int usedCount=0;
                    for(std::size_t j=0;j<Types.size();++j)
                    {
                        const auto& t=Types[j];const int count=static_cast<int>((used[t.Limb]>>t.Shift)&((std::uint64_t(1)<<t.Bits)-1));
                        usedCount+=count;if(count<t.Quantity)neededBits[j/64]|=std::uint64_t(1)<<(j%64);
                    }
                    int minimum=0,maximum=0;
                    for(int role=0;role<5;++role)if(!(mask&(1<<role)))
                    {const auto [lo,hi]=ranges.at(roles[role]);minimum+=lo;maximum+=hi;}
                    if(usedCount+minimum>Demand||usedCount+maximum<Demand)return false;

                    std::array<Indices,5> active{Indices(&RootMemory),Indices(&RootMemory),Indices(&RootMemory),Indices(&RootMemory),Indices(&RootMemory)};
                    std::array<std::size_t,MaxInstances> support{};
                    int bestRole=-1;std::size_t bestCount=std::numeric_limits<std::size_t>::max();
                    for(int role=0;role<5;++role)if(!(mask&(1<<role)))
                    {
                        for(int id:*lists[role])
                        {
                            Inventory next{};if(!Add(used,Nodes[id].Used,next)){if(Interrupted())return false;continue;}
                            if(active[role].size()==active[role].capacity())
                            {
                                if(Poll())return false;ReserveOne(active[role]);if(Poll())return false;
                            }
                            active[role].push_back(id);
                            for(std::size_t word=0;word<supportWords;++word)
                                for(auto bits=supportMasks[static_cast<std::size_t>(id)*supportWords+word]&neededBits[word];bits;bits&=bits-1)
                                    ++support[word*64+std::countr_zero(bits)];
                        }
                        if(active[role].empty())return false;
                        if(active[role].size()<bestCount){bestRole=role;bestCount=active[role].size();}
                    }
                    int bestType=-1;
                    for(std::size_t j=0;j<Types.size();++j)
                    {
                        if(!(neededBits[j/64]&(std::uint64_t(1)<<(j%64))))continue;
                        if(!support[j])return false;
                        if(support[j]<bestCount){bestType=static_cast<int>(j);bestCount=support[j];}
                    }
                    // For a needed type, every complete assignment must choose
                    // at least one remaining role candidate containing that type,
                    // even when its remaining quantity is greater than one.
                    for(int role=0;role<5;++role)if(!(mask&(1<<role))&&(bestType>=0||role==bestRole))
                        for(int id:active[role])
                        {
                            if(bestType>=0)
                            {
                                if(!Probe())return false; // Candidate/type eligibility check.
                                if(!(supportMasks[static_cast<std::size_t>(id)*supportWords+static_cast<std::size_t>(bestType)/64]&
                                    (std::uint64_t(1)<<(bestType%64))))continue;
                            }
                            Inventory next{};if(!Add(used,Nodes[id].Used,next)){if(Interrupted())return false;continue;}
                            node.Children[role]=id;
                            if(self(self,mask|(1<<role),next,depth+1))return true;
                            if(Interrupted())return false;
                        }
                    if(Stop())return false;
                    if(failed.size()<20000)failed.emplace(mask,used);
                    return false;
                };
                const Inventory empty{};
                if(cover(cover,0,empty,0)){Solution=node;HasSolution=true;return;}
            }
        }
        template<class Container>bool Expand(const Node& n,int x,int y,Container& output)
        {
            if(CountsDetailedConstruction(Policy)&&!Probe())return false;
            if(Poll())return false;
            if(!n.ChildCount){output.push_back({n.TypeIndex,x,y});return true;}
            if(n.ChildCount==2)
            {
                const auto& a=Nodes[n.Children[0]];const auto& b=Nodes[n.Children[1]];
                const bool horizontal=n.Width==a.Width+b.Width;
                return Expand(a,x,y,output)&&Expand(b,x+(horizontal?a.Width:0),y+(horizontal?0:a.Height),output);
            }
            const auto& a=Nodes[n.Children[0]];const auto& b=Nodes[n.Children[1]];const auto& d=Nodes[n.Children[3]];
            const std::array<Dimension,5> positions{Dimension{0,0},Dimension{a.Width,0},Dimension{d.Width,b.Height},Dimension{0,a.Height},Dimension{d.Width,a.Height}};
            for(int i=0;i<5;++i)
                if(!Expand(Nodes[n.Children[i]],x+positions[i].first,y+positions[i].second,output))return false;
            return true;
        }
    };

    bool AuditRepairSnapshot(int width,int height,const std::vector<Type>& types,
        std::span<const RectangleRepairPrimitive> snapshot,Budget& job,bool requireComplete)
    {
        std::array<int,MaxInstances> actual{};std::int64_t area=0;
        for(std::size_t i=0;i<snapshot.size();++i)
        {
            ProbeRectangleBlockBudget(job);const auto& p=snapshot[i];
            if(p.TypeIndex<0||p.TypeIndex>=static_cast<int>(types.size()))return false;
            const auto& t=types[p.TypeIndex];
            if(p.Width!=t.Width||p.Height!=t.Height||++actual[p.TypeIndex]>t.Quantity||p.X<0||p.Y<0||p.X>width-p.Width||p.Y>height-p.Height)return false;
            area+=p.Width*p.Height;
            for(std::size_t j=0;j<i;++j)
            {
                ProbeRectangleBlockBudget(job);const auto& other=snapshot[j];
                if(p.X<other.X+other.Width&&other.X<p.X+p.Width&&p.Y<other.Y+other.Height&&other.Y<p.Y+p.Height)return false;
            }
        }
        for(std::size_t i=0;i<types.size();++i)
        {ProbeRectangleBlockBudget(job);if(requireComplete&&actual[i]!=types[i].Quantity)return false;}
        return !requireComplete||area==std::int64_t(width)*height;
    }

    bool RepairCover(int width,int height,const std::vector<Type>& types,Budget& job,
        std::vector<Leaf>& output,RepairSearchStats& stats)
    {
        LimitedMemory libraryMemory(LibraryBytes),rootMemory(RepairRootBytes);
        struct RecordPeaks
        {
            LimitedMemory& Library;LimitedMemory& Root;RepairSearchStats& Stats;
            ~RecordPeaks(){Stats.LibraryPeak=Library.Peak;Stats.RootPeak=Root.Peak;}
        } peaks{libraryMemory,rootMemory,stats};
        std::pmr::vector<RectangleRepairPrimitive> snapshot(&rootMemory);
        bool alreadyComplete=false;const auto initialStart=job.Attempts;
        {
            // No generated NodeID escapes this scope. Expanding the retained
            // internal partial releases the initial library before regeneration.
            BlockLibrary initial(width,height,types,job,SearchPolicy::FairRepair,&libraryMemory,&rootMemory);
            if(!initial.Supported){stats.Unsupported=true;return false;}
            initial.Run();stats.InitialNodes=initial.CandidateCount();stats.Rollout=initial.RolloutStats;
            stats.InitialChecks=job.Attempts-initialStart;
            if(job.Stop())return false;
            std::pmr::vector<Leaf> leaves(&rootMemory);
            const auto auditStart=job.Attempts;
            if(!initial.SnapshotLeaves(leaves))return false;
            for(const auto& leaf:leaves)
            {
                ProbeRectangleBlockBudget(job);const auto& t=types.at(leaf.TypeIndex);
                snapshot.push_back({leaf.TypeIndex,leaf.X,leaf.Y,t.Width,t.Height});
                stats.PartialArea+=t.Width*t.Height;
            }
            stats.PartialCount=static_cast<int>(snapshot.size());alreadyComplete=initial.HasSolution;
            if(!AuditRepairSnapshot(width,height,types,snapshot,job,alreadyComplete)){stats.InvalidSnapshot=true;return false;}
            stats.SnapshotChecks+=job.Attempts-auditStart;
        }
        stats.LibraryReleased=libraryMemory.RetainedBytes()==0;
        if(!stats.LibraryReleased){stats.InvalidSnapshot=true;return false;}
        const auto publish=[&](std::span<const RectangleRepairPrimitive> complete)
        {
            if(!AuditRepairSnapshot(width,height,types,complete,job,true)){stats.InvalidSnapshot=true;return false;}
            CheckRectangleBlockBudget(job,true);output.reserve(complete.size());
            for(const auto& p:complete){ProbeRectangleBlockBudget(job);output.push_back({p.TypeIndex,p.X,p.Y});}
            CheckRectangleBlockBudget(job,true);return true;
        };
        if(alreadyComplete)return publish(snapshot);
        std::pmr::vector<RectangleRepairRegion> regions(&rootMemory);
        if(!GenerateRectangleRepairRegions(width,height,snapshot,job,&rootMemory,regions,stats.Regions))return false;
        for(const auto& candidate:regions)
        {
            CheckRectangleBlockBudget(job,true);if(stats.Trials.size()>=12)break;++stats.Considered;
            std::pmr::vector<int> remaining(&rootMemory);for(const auto& t:types){ProbeRectangleBlockBudget(job);remaining.push_back(t.Quantity);}
            for(auto index:candidate.OutsidePrimitiveIndices)
            {ProbeRectangleBlockBudget(job);if(--remaining[snapshot[index].TypeIndex]<0){stats.InvalidSnapshot=true;return false;}}
            const auto& r=candidate.Bounds;const int w=r.X1-r.X0,h=r.Y1-r.Y0;
            std::vector<Type> localTypes;Indices mapping(&rootMemory);std::int64_t area=0;std::size_t members=0;bool fits=true;
            for(std::size_t i=0;i<types.size();++i)
            {
                ProbeRectangleBlockBudget(job);if(!remaining[i])continue;const auto& t=types[i];
                area+=std::int64_t(t.Width)*t.Height*remaining[i];members+=remaining[i];
                fits=fits&&t.Width<=w&&t.Height<=h;
                localTypes.push_back({t.Width,t.Height,remaining[i]});mapping.push_back(static_cast<int>(i));
            }
            if(area!=r.Area()){stats.InvalidSnapshot=true;return false;}
            if(!fits){++stats.Skipped;continue;}
            stats.Trials.push_back({r,members});auto& trial=stats.Trials.back();
            std::pmr::vector<Leaf> placed(&rootMemory);const auto began=job.Attempts;
            {
                BlockLibrary local(w,h,localTypes,job,SearchPolicy::RepairRegion,&libraryMemory,&rootMemory);
                if(!local.Supported){stats.Unsupported=true;return false;}
                local.Run();trial.Generated=local.CandidateCount();
                trial.Complete=local.HasSolution&&local.Leaves(placed);
                trial.Checks=job.Attempts-began;
                trial.PhaseLimited=local.PhaseLimited;trial.LibraryLimited=local.LibraryLimited;trial.RootLimited=local.RootLimited;
                stats.RegionNodes+=trial.Generated;stats.RegionChecks+=job.Attempts-began;
            }
            if(job.Stop())return false;
            if(libraryMemory.RetainedBytes()!=0){stats.InvalidSnapshot=true;return false;}
            if(!trial.Complete)continue;
            std::pmr::vector<RectangleRepairPrimitive> complete(&rootMemory);
            for(auto index:candidate.OutsidePrimitiveIndices)
            {ProbeRectangleBlockBudget(job);complete.push_back(snapshot[index]);}
            for(const auto& leaf:placed)
            {
                ProbeRectangleBlockBudget(job);const int typeIndex=mapping.at(leaf.TypeIndex);const auto& t=types[typeIndex];
                complete.push_back({typeIndex,leaf.X+r.X0,leaf.Y+r.Y0,t.Width,t.Height});
            }
            return publish(complete);
        }
        // Existing dense-macro repairs retain their successful trajectories.
        // Only their bounded failure proceeds to an exact primitive repair,
        // including stripe-compatible adjoining donors in irregular free space.
        std::pmr::vector<RectangleBlockType> stripeTypes(&rootMemory);
        for(const auto& t:types)
        {ProbeRectangleBlockBudget(job);stripeTypes.push_back({t.Width,t.Height,t.Quantity});}
        std::pmr::vector<RectangleRepairPrimitive> repaired(&rootMemory);
        if(SearchRectangleStripeRegions(width,height,stripeTypes,snapshot,job,&rootMemory,repaired,stats.Stripe))
            return publish(repaired);
        if(stats.Stripe.InvalidInput)stats.InvalidSnapshot=true;
        return false;
    }

    bool RoundoffEqual(double a,double b)
    {return std::abs(a-b)<=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});}
    bool IntegerSide(double value,int& result)
    {
        if(!std::isfinite(value)||value<1||value>MaxSide)return false;
        const double rounded=std::round(value);if(!RoundoffEqual(value,rounded))return false;
        result=static_cast<int>(rounded);return true;
    }
    struct Original {std::size_t PartIndex,Instance;Pose Local;double Area;};
}

static Result RunRectangleBlocks(const Problem& problem,const Options& options,SearchPolicy policy)
{
    Budget budget{options};Result result;std::size_t candidates=0,libraryPeak=0,rootPeak=0;
    RectangleBlockRolloutStats rolloutStats;
    ResidualSearchStats residualStats;
    RepairSearchStats repairStats;
    const auto appendRepairDiagnostics=[&]()
    {
        const auto& s=repairStats;
        result.Diagnostics.push_back("Block repair: initial-nodes="+std::to_string(s.InitialNodes)+", initial-checks="+std::to_string(s.InitialChecks)+
            ", internal-partial-count="+std::to_string(s.PartialCount)+", internal-partial-area="+std::to_string(s.PartialArea)+
            ", initial-library-released="+(s.LibraryReleased?"true":"false")+", snapshot-checks="+std::to_string(s.SnapshotChecks)+
            ", region-candidates="+std::to_string(s.Regions.Candidates)+", region-generation-checks="+std::to_string(s.Regions.Probes)+
            ", considered="+std::to_string(s.Considered)+", skipped="+std::to_string(s.Skipped)+", trials="+std::to_string(s.Trials.size())+
            ", region-nodes="+std::to_string(s.RegionNodes)+", region-checks="+std::to_string(s.RegionChecks)+
            ", wide-applied="+std::to_string(s.Rollout.Applied)+", wide-rollouts="+std::to_string(s.Rollout.Rollouts)+", wide-layers="+std::to_string(s.Rollout.Layers)+
            ", wide-stage-limit="+(s.Rollout.PhaseLimitReached?"true":"false")+", wide-memory-limit="+(s.Rollout.MemoryLimitReached?"true":"false")+
            ", invalid-snapshot="+(s.InvalidSnapshot?"true":"false")+". Internal partials are never published; all original demand is retained unless complete.");
        for(std::size_t index=0;index<s.Trials.size();++index)
        {
            const auto& t=s.Trials[index];const auto& r=t.Bounds;
            result.Diagnostics.push_back("Repair region="+std::to_string(index)+", bounds="+std::to_string(r.X0)+","+std::to_string(r.Y0)+","+std::to_string(r.X1)+","+std::to_string(r.Y1)+
                ", members="+std::to_string(t.Members)+", generated="+std::to_string(t.Generated)+", checks="+std::to_string(t.Checks)+
                ", phase-limited="+(t.PhaseLimited?"true":"false")+", library-limited="+(t.LibraryLimited?"true":"false")+
                ", root-limited="+(t.RootLimited?"true":"false")+", complete="+(t.Complete?"true":"false")+".");
        }
        result.Diagnostics.push_back("Repair limits: separate 512 MiB library / 2048 MiB root PMR; initial 1m fair-diverse nodes and 40k macros, beam 1024 / shortlist 64 / applied 100m; wide stage at most 100b checks and 600s, reserving one quarter of remaining work/time for repair. Up to 1000 geometric regions and 12 actual trials; each region 1999999 ordinary nodes plus an external root, seeds 100k, 30b checks / 120s. Every phase remains under the original caller budget; no case-specific settings.");
        const auto& stripe=s.Stripe;
        result.Diagnostics.push_back("Primitive stripe repair after macro failure: candidates="+std::to_string(stripe.Candidates)+
            ", donors-tested="+std::to_string(stripe.DonorsTested)+", stripe-rejected="+std::to_string(stripe.StripeRejected)+
            ", trials="+std::to_string(stripe.Trials)+", local-infeasible="+std::to_string(stripe.LocalInfeasible)+
            ", limited="+std::to_string(stripe.LimitedTrials)+", checks="+std::to_string(stripe.Checks)+
            ", nodes="+std::to_string(stripe.ExactNodes)+", whole-board-regions-skipped="+std::to_string(stripe.FullBoardRegions)+
            ", complete="+(stripe.Complete?"true":"false")+
            ". At most 64 regions, 8 adjoining donors per incompatible region, 16 exact trials; each at most 120s / 100m nodes / 5b checks, all under the original job budget. No bounded failure proves whole-board infeasibility.");
    };
    std::vector<ResidualTrialStats> residualTrials;
    const auto appendResidualTrials=[&]()
    {
        for(std::size_t index=0;index<residualTrials.size();++index)
        {
            const auto& t=residualTrials[index];
            result.Diagnostics.push_back("Residual trial="+std::to_string(index)+", selection="+std::to_string(t.SelectionIndex)+
                ", core="+std::to_string(t.Width)+"x"+std::to_string(t.Height)+", members="+std::to_string(t.Members)+", core-hash="+std::to_string(t.CoreHash)+
                ", generated="+std::to_string(t.Generated)+", generation-checks="+std::to_string(t.GenerationChecks)+", inventory-adds="+std::to_string(t.InventoryAdds)+
                ", library-hash="+std::to_string(t.LibraryHash)+", selected="+std::to_string(t.Selected)+", selected-hash="+std::to_string(t.SelectedHash)+
                ", generation-limited="+(t.GenerationLimited?"true":"false")+", placement-checks="+std::to_string(t.PlacementChecks)+
                ", nodes="+std::to_string(t.Nodes)+", branches="+std::to_string(t.Branches)+", truncations="+std::to_string(t.Truncations)+
                ", trial-limited="+(t.TrialLimited?"true":"false")+", complete="+(t.Complete?"true":"false")+".");
        }
    };
    const auto residualDiagnostic=[&]()
    {
        const auto& s=residualStats;
        return "Block residual: initial-nodes="+std::to_string(s.InitialNodes)+", initial-checks="+std::to_string(s.InitialChecks)+
            ", selected="+std::to_string(s.Selected)+", considered="+std::to_string(s.Considered)+", skipped="+std::to_string(s.Skipped)+
            ", trials="+std::to_string(s.Trials)+", residual-nodes="+std::to_string(s.Generated)+", generation-checks="+std::to_string(s.GenerationChecks)+
            ", selection-checks="+std::to_string(s.SelectionChecks)+", placement-checks="+std::to_string(s.PlacementChecks)+
            ", mrv-nodes="+std::to_string(s.MrvNodes)+", root-branches="+std::to_string(s.RootBranches)+", root-truncations="+std::to_string(s.RootTruncations)+
            ", generation-limits="+std::to_string(s.GenerationLimits)+", trial-limits="+std::to_string(s.TrialLimits)+", memory-limits="+std::to_string(s.MemoryLimits)+
            ", successful-core="+std::to_string(s.CoreWidth)+"x"+std::to_string(s.CoreHeight)+", core-members="+std::to_string(s.CoreMembers)+
            ", complement="+(s.Complement?"true":"false")+". All trials, construction, filters, complement lookups, MRV and restoration share the caller budget.";
    };
    bool memoryLimited=false;
    const auto untouched=[&](const std::string& message)
    {
        result.Layouts.clear();result.Remaining.clear();
        for(const auto& part:problem.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
        result.State=budget.Cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
        result.Stats.PlacementAttempts=budget.Attempts;result.Stats.CandidatePatterns=candidates;
        result.Stats.ElapsedSeconds=result.Stats.PreparationSeconds=budget.Elapsed();result.Stats.PlacementSeconds=0;
        result.Diagnostics.push_back(message);
        if(policy==SearchPolicy::FairResidual){result.Diagnostics.push_back(residualDiagnostic());appendResidualTrials();}
        if(policy==SearchPolicy::FairRepair)appendRepairDiagnostics();
        if(UsesFairConstruction(policy)&&policy!=SearchPolicy::FairResidual&&policy!=SearchPolicy::FairRepair&&candidates)
            result.Diagnostics.push_back("Block rollout: generated="+std::to_string(candidates)+", selected="+std::to_string(rolloutStats.Selected)+
                ", applied="+std::to_string(rolloutStats.Applied)+", rollouts="+std::to_string(rolloutStats.Rollouts)+", layers="+std::to_string(rolloutStats.Layers)+
                ", diversity-prunes="+std::to_string(rolloutStats.DiversityPrunes)+", applied-state-limit="+(rolloutStats.AppliedLimitReached?"reached":"not reached")+
                "; retained-memory peaks library="+std::to_string(libraryPeak)+", root="+std::to_string(rootPeak)+" bytes.");
        return result;
    };
    if(budget.Stop())return untouched("Block library stopped before preparation; original demand retained.");
    if(problem.PartGap!=0||!options.EnableCombinations)
        return untouched("Block library not applicable: requires zero part gap and enabled combinations.");
    std::size_t demand=0;std::string material;bool haveMaterial=false;
    std::map<Dimension,std::deque<Original>> originals;
    std::int64_t totalArea=0;
    for(std::size_t index=0;index<problem.Parts.size();++index)
    {
        if(budget.Stop())return untouched("Block library preparation stopped; original demand retained.");
        const auto& part=problem.Parts[index];if(!part.Quantity)continue;
        if(part.Quantity>MaxInstances-demand||part.FreeRotation||(haveMaterial&&part.Material!=material))
            return untouched("Block library not applicable: requires one material, fixed directions, and at most 512 instances.");
        demand+=part.Quantity;material=part.Material;haveMaterial=true;
        const auto description=FindRectangleDirections(part,problem.Tolerance,[&]{return budget.Stop();});
        if(description.Interrupted||budget.Stop())return untouched("Block library direction preparation stopped; original demand retained.");
        if(!description.Supported)return untouched("Block library not applicable: requires original filled axis-aligned rectangular footprints.");
        int w=0,h=0;Pose direction;bool first=true;
        for(const auto& d:description.Directions)
        {
            int dw=0,dh=0;
            if(!IntegerSide(d.Width,dw)||!IntegerSide(d.Height,dh)||!RoundoffEqual(description.Area,double(dw)*dh))
                return untouched("Block library not applicable: footprint must be exactly filled and integer-sized; no envelope substitution.");
            if(!first&&(w!=dw||h!=dh))
                return untouched("Block library not applicable: allowed directions produce multiple distinct footprints.");
            if(first||d.Offset.AngleDegrees<direction.AngleDegrees)direction=d.Offset;
            first=false;w=dw;h=dh;
        }
        // Tighten the generic recognition tolerance to floating-point roundoff.
        // A user tolerance must not turn a notched/near rectangle into a cell.
        for(std::size_t j=0;j<part.Geometry.Outer.size();++j)
        {
            if(budget.Stop())return untouched("Block library shape preparation stopped; original demand retained.");
            const auto a=Transform(part.Geometry.Outer[j],direction);
            const auto b=Transform(part.Geometry.Outer[(j+1)%part.Geometry.Outer.size()],direction);
            if(!(RoundoffEqual(a.X,0)||RoundoffEqual(a.X,w)||RoundoffEqual(a.Y,0)||RoundoffEqual(a.Y,h))||
               !(RoundoffEqual(a.X,b.X)||RoundoffEqual(a.Y,b.Y)))
                return untouched("Block library not applicable: original contour is not an exact rectangular footprint.");
        }
        totalArea+=std::int64_t(w)*h*static_cast<std::int64_t>(part.Quantity);
        auto& queue=originals[{w,h}];
        for(std::size_t copy=0;copy<part.Quantity;++copy)queue.push_back({index,copy,direction,description.Area});
    }
    if(!demand)return untouched("No demanded instances.");
    std::vector<Type> types;types.reserve(originals.size());
    for(const auto& [d,queue]:originals)types.push_back({d.first,d.second,static_cast<int>(queue.size())});
    // An exact axis lattice is a change of units, without rounding geometry.
    // Only reduce inputs that exceed the dense-search guards; established
    // small-input search order and budgets remain unchanged.
    struct StockGrid {std::size_t Index;int ScaleX=1,ScaleY=1;};
    std::map<Dimension,StockGrid> stocks;
    for(std::size_t index=0;index<problem.Stocks.size();++index)
    {
        if(budget.Stop())return untouched("Block library stock preparation stopped; original demand retained.");
        const auto& stock=problem.Stocks[index];int w=0,h=0;
        if(!stock.Quantity||stock.Material!=material||!IntegerSide(stock.Width-2*problem.EdgeMargin,w)||
           !IntegerSide(stock.Height-2*problem.EdgeMargin,h)||std::int64_t(w)*h!=totalArea)continue;
        bool fits=true;for(const auto& t:types)if(t.Width>w||t.Height>h){fits=false;break;}
        if(!fits)continue;
        StockGrid grid{index};
        if(UsesFairConstruction(policy)&&(w>512||h>512||totalArea>65536))
        {
            grid.ScaleX=w;grid.ScaleY=h;
            for(const auto& t:types)
            {
                if(!budget.Probe())return untouched("Exact rectangle lattice preparation stopped; original demand retained.");
                grid.ScaleX=std::gcd(grid.ScaleX,t.Width);grid.ScaleY=std::gcd(grid.ScaleY,t.Height);
            }
            const int nw=w/grid.ScaleX,nh=h/grid.ScaleY;
            if(nw>512||nh>512||std::int64_t(nw)*nh>65536)continue;
        }
        stocks.emplace(Dimension{w,h},grid);
    }
    if(stocks.empty())return untouched(UsesFairConstruction(policy)?
        "Block rollout not applicable: demand must exactly fill one available compatible integer stock interior; its exact axis lattice must have each usable side <=512 and area <=65536.":
        "Block library not applicable: demand must exactly fill one available compatible integer-sized stock interior.");
    for(const auto& [size,grid]:stocks)
    {
        if(budget.Stop())break;
        try
        {
            auto searchTypes=types;
            if(grid.ScaleX!=1||grid.ScaleY!=1)
                for(auto& t:searchTypes)
                {
                    ProbeRectangleBlockBudget(budget);
                    t.Width/=grid.ScaleX;t.Height/=grid.ScaleY;
                }
            const Dimension searchSize{size.first/grid.ScaleX,size.second/grid.ScaleY};
            std::vector<Leaf> leaves;double prepared=0;
            std::optional<BlockLibrary> retainedLibrary;
            if(policy==SearchPolicy::FairRepair)
            {
                repairStats={};
                bool complete=false;
                try{complete=RepairCover(searchSize.first,searchSize.second,searchTypes,budget,leaves,repairStats);}
                catch(const RectangleBlockInterrupted&){}
                candidates+=repairStats.InitialNodes+repairStats.RegionNodes;
                libraryPeak=std::max(libraryPeak,repairStats.LibraryPeak);rootPeak=std::max(rootPeak,repairStats.RootPeak);
                if(!complete)continue;
                prepared=budget.Elapsed();
            }
            else
            {
                retainedLibrary.emplace(searchSize.first,searchSize.second,searchTypes,budget,policy);auto& library=*retainedLibrary;
                if(!library.Supported)return untouched("Block library not applicable: inventory exceeds the fixed guarded-count representation.");
                library.Run();candidates+=library.CandidateCount();libraryPeak=std::max(libraryPeak,library.LibraryPeak());rootPeak=std::max(rootPeak,library.RootPeak());
                rolloutStats=library.RolloutStats;
                residualStats=library.ResidualStats;
                residualTrials.assign(library.TrialStatistics().begin(),library.TrialStatistics().end());
                memoryLimited=memoryLimited||library.LibraryLimited||library.RootLimited;
                if(!library.HasSolution)continue;
                prepared=budget.Elapsed();leaves.reserve(demand);
                if(!library.Leaves(leaves))break;
            }
            auto available=originals;const auto& stock=problem.Stocks[grid.Index];
            Layout layout{stock.ID,0,stock.Width,stock.Height,{},0};layout.Placements.reserve(demand);
            for(const auto& leaf:leaves)
            {
                if((policy==SearchPolicy::FairResidual||policy==SearchPolicy::FairRepair)&&!budget.Probe())break;
                if(budget.Poll())break;
                const auto& type=types.at(static_cast<std::size_t>(leaf.TypeIndex));auto& queue=available.at({type.Width,type.Height});
                if(queue.empty())return untouched("Block library reconstruction rejected an inventory mismatch.");
                const auto original=queue.front();queue.pop_front();const auto& source=problem.Parts[original.PartIndex];
                layout.Placements.push_back({source.ID,original.Instance,
                    {problem.EdgeMargin+leaf.X*grid.ScaleX+original.Local.X,problem.EdgeMargin+leaf.Y*grid.ScaleY+original.Local.Y,original.Local.AngleDegrees},1,false});
                layout.MaterialArea+=original.Area;
            }
            if(budget.Poll()||layout.Placements.size()!=demand)break;
            for(const auto& [d,queue]:available)if(!queue.empty())return untouched("Block library reconstruction rejected missing source instances.");
            result.Layouts.push_back(std::move(layout));result.State=Status::Complete;
            result.Stats.PlacementAttempts=budget.Attempts;result.Stats.CandidatePatterns=candidates;
            result.Stats.CombinationGroups=demand>1?1:0;result.Stats.PreparationSeconds=prepared;
            result.Stats.ElapsedSeconds=budget.Elapsed();result.Stats.PlacementSeconds=result.Stats.ElapsedSeconds-prepared;
            result.Diagnostics.push_back("Block library: dimension/inventory search; original identities and poses restored. All inventory additions and complement joins share the reported attempt budget.");
            if(grid.ScaleX!=1||grid.ScaleY!=1)
                result.Diagnostics.push_back("Exact axis lattice: scale-x="+std::to_string(grid.ScaleX)+", scale-y="+std::to_string(grid.ScaleY)+
                    ", search="+std::to_string(searchSize.first)+"x"+std::to_string(searchSize.second)+
                    ". Integer dimensions are divided exactly and all original coordinates are restored; no geometric rounding.");
            result.Diagnostics.push_back("Block library retained-memory peaks: library="+std::to_string(libraryPeak)+", root="+std::to_string(rootPeak)+" bytes; caps 512 MiB + "+(policy==SearchPolicy::FairRepair?"2048":"128")+" MiB.");
            if(policy==SearchPolicy::FairResidual)
            {
                result.Diagnostics.push_back(residualDiagnostic());
                appendResidualTrials();
                result.Diagnostics.push_back("Residual limits: initial 1m nodes / 20k selected; up to 64 shapes / 128 trials; fresh 50k nodes / 5k seeds / 100m checks; MRV 1m nodes / 1b checks, each root branch 2k nodes / 20m checks, macro limits 32 / 512. Caps are heuristic truncations, not infeasibility proofs.");
            }
            if(policy==SearchPolicy::FairRepair)appendRepairDiagnostics();
            if(UsesFairConstruction(policy)&&policy!=SearchPolicy::FairResidual&&policy!=SearchPolicy::FairRepair)
                result.Diagnostics.push_back(std::string(policy==SearchPolicy::FairRolloutDiverse?
                    "Block rollout diverse: fixed fair-diverse construction, preserved 20k plus fair 20k additions and primitives; selected=":
                    "Block rollout: fixed fair-diverse construction, 20k macro selection plus primitives; selected=")+std::to_string(rolloutStats.Selected)+
                    ", applied="+std::to_string(rolloutStats.Applied)+", rollouts="+std::to_string(rolloutStats.Rollouts)+", layers="+std::to_string(rolloutStats.Layers)+
                    ", diversity-prunes="+std::to_string(rolloutStats.DiversityPrunes)+", applied-state-limit="+(rolloutStats.AppliedLimitReached?"reached":"not reached")+
                    ". Generation, selection, active inventory filtering and placement share the reported budget.");
            if(policy==SearchPolicy::FairRolloutDiverse)
                result.Diagnostics.push_back("Diverse candidate selection: sets="+std::to_string(rolloutStats.CandidateSets)+
                    ", primitive-selections="+std::to_string(rolloutStats.PrimitiveSelections)+", small-selections="+std::to_string(rolloutStats.SmallSelections)+
                    "; first shortlist primitive="+std::to_string(rolloutStats.RootPrimitiveCount)+", small="+std::to_string(rolloutStats.RootSmallCount)+
                    ", shapes="+std::to_string(rolloutStats.RootShapeCount)+". Count/shape/inventory diversity is a search heuristic, not a feasibility proof.");
            return result;
        }
        catch(const RectangleBlockInterrupted&){break;}
        catch(const std::bad_alloc&){memoryLimited=true;}
    }
    return untouched(budget.Cancelled?"Block library cancelled; all original demand retained.":
        budget.Exhausted?"Block library exhausted the shared time/attempt budget; all original demand retained.":
        memoryLimited?"Block library reached a bounded search-stage or memory limit without a complete cover; all original demand retained.":
        "Block library found no complete cover in its bounded heuristic search; this is not a general infeasibility proof.");
}

Result RunRectangleBlockLibrary(const Problem& problem,const Options& options,BlockRootPolicy policy)
{return RunRectangleBlocks(problem,options,policy==BlockRootPolicy::RoleOrder?SearchPolicy::RoleOrder:SearchPolicy::InventoryMrv);}
Result RunRectangleBlockRollout(const Problem& problem,const Options& options,BlockRolloutPolicy policy)
{return RunRectangleBlocks(problem,options,policy==BlockRolloutPolicy::Score?SearchPolicy::FairRollout:SearchPolicy::FairRolloutDiverse);}
Result RunRectangleBlockResidual(const Problem& problem,const Options& options)
{return RunRectangleBlocks(problem,options,SearchPolicy::FairResidual);}
Result RunRectangleBlockRepair(const Problem& problem,const Options& options)
{return RunRectangleBlocks(problem,options,SearchPolicy::FairRepair);}
}
