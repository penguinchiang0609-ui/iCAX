#include "NestingRectangleResidualCorners.h"
#include <array>
#include <bit>
#include <cstdint>
#include <numeric>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
namespace
{
using U=std::uint64_t;
struct Line {std::array<U,8> Word{};};
struct Reach
{
    std::array<U,9> Word{};
    bool Has(int n)const{return (Word[n/64]>>(n%64))&1;}
};
struct Candidate {int Index,X,Y;bool Macro;};
struct Corner {int Space,Width,Height,Y,X,RX,RY;};
struct TrialStopped {};
struct RootStopped {};
struct Invalid {};
using Ints=std::pmr::vector<int>;
struct Projection
{
    Reach X,Y;
    Ints WidthArea,HeightArea,WidthCross,HeightCross,WidthQuantity,HeightQuantity,WidthExact,HeightExact;
    Ints WidthThresholds,HeightThresholds;
    std::pmr::vector<Reach> WidthExcluded,HeightExcluded;
    explicit Projection(std::pmr::memory_resource* r)
        :WidthArea(r),HeightArea(r),WidthCross(r),HeightCross(r),WidthQuantity(r),HeightQuantity(r),
         WidthExact(r),HeightExact(r),WidthThresholds(r),HeightThresholds(r),WidthExcluded(r),HeightExcluded(r){}
};

class Search
{
    int W,H,PlacedCount=0,Required=0;
    std::span<const RectangleBlockType> Types;
    std::span<const RectangleBlockMacro> Macros;
    std::span<const int> PrimitiveNodes;
    RectangleBlockBudget& Job;
    std::pmr::memory_resource* Memory;
    const RectangleResidualLimits& Limits;
    RectangleResidualStats& Stats;
    Ints Remaining,RootRemaining,TypeOrder,TypeRank,MacroOrder,Histogram,BestWidth;
    std::pmr::vector<std::pmr::vector<std::pair<int,int>>> MacroSignatures;
    std::pmr::vector<std::pair<int,int>> HistogramStack;
    std::pmr::vector<Line> Rows,Cols,RootRows,RootCols;
    std::pmr::vector<std::array<U,8>> RequiredBits;
    std::pmr::vector<RectangleBlockPlacement> Path;
    std::pmr::unordered_set<std::pmr::string> Seen;
    std::pmr::unordered_map<std::pmr::string,Projection> SpanCache;
    std::size_t MemoCap=0,ProjectionCap=0,BranchNodeBase=0,BranchProbeBase=0;
    bool BranchActive=false,EmptyRoot=true;

    void Check(bool force=false)
    {
        CheckRectangleBlockBudget(Job);if(force)CheckRectangleBlockBudget(Job,true);
        if(Stats.Probes>=Limits.Probes){Stats.TrialProbeLimit=true;throw TrialStopped{};}
        if(BranchActive&&Limits.RootBranchProbes&&Stats.Probes-BranchProbeBase>=Limits.RootBranchProbes)throw RootStopped{};
    }
    void Probe(){Check();ProbeRectangleBlockBudget(Job);++Stats.Probes;}
    void EnterNode()
    {
        Check(true);
        if(Stats.Nodes>=Limits.Nodes){Stats.TrialNodeLimit=true;throw TrialStopped{};}
        if(BranchActive&&Limits.RootBranchNodes&&Stats.Nodes-BranchNodeBase>=Limits.RootBranchNodes)throw RootStopped{};
        ++Stats.Nodes;
    }
    static U Full(int word,int extent){const int n=extent-word*64;return n>=64?~U{}:(U{1}<<n)-1;}
    static U Mask(int lo,int hi){return (~U{}<<lo)&(hi==64?~U{}:(U{1}<<hi)-1);}
    bool Intersects(const Line& line,int from,int length)
    {
        const int first=from/64,last=(from+length-1)/64;
        for(int k=first;k<=last;++k){Probe();if(line.Word[k]&Mask(k==first?from%64:0,k==last?(from+length-1)%64+1:64))return true;}
        return false;
    }
    void Flip(Line& line,int from,int length)
    {
        const int first=from/64,last=(from+length-1)/64;
        for(int k=first;k<=last;++k){Probe();line.Word[k]^=Mask(k==first?from%64:0,k==last?(from+length-1)%64+1:64);}
    }
    int Next(const Line& line,int from,int extent,bool occupied)
    {
        if(from>=extent)return extent;
        for(int k=from/64;k<(extent+63)/64;++k)
        {
            Probe();U bits=(occupied?line.Word[k]:~line.Word[k])&Full(k,extent);
            if(k==from/64)bits&=~U{}<<(from%64);
            if(bits)return k*64+std::countr_zero(bits);
        }
        return extent;
    }
    int Ray(const Line& line,int at,int extent,bool reverse)
    {
        if(!reverse)return Next(line,at+1,extent,true)-at;
        for(int k=at/64;k>=0;--k)
        {
            Probe();U bits=line.Word[k];if(k==at/64)bits&=(U{1}<<(at%64))-1;
            if(bits)return at-(k*64+63-std::countl_zero(bits));
        }
        return at+1;
    }
    std::pmr::string CountKey()
    {
        std::pmr::string key(Memory);key.reserve(Remaining.size()*2);
        for(int n:Remaining){Probe();key.push_back(char(n&255));key.push_back(char(n>>8));}
        return key;
    }
    Reach Subsets(int limit,bool vertical)
    {
        Reach bits;bits.Word[0]=1;const int length=limit/64+1;
        for(std::size_t i=0;i<Types.size();++i)
        {
            Probe();const int weight=vertical?Types[i].Height:Types[i].Width;int n=std::min(Remaining[i],limit/weight);
            for(int batch=1;n;batch*=2)
            {
                Probe();const int take=std::min(batch,n);n-=take;const int shift=take*weight,words=shift/64,offset=shift%64;
                for(int j=length;j-->words;)
                {Probe();U value=bits.Word[j-words]<<offset;if(offset&&j>words)value|=bits.Word[j-words-1]>>(64-offset);bits.Word[j]|=value;}
                if(limit%64!=63)bits.Word[length-1]&=(U{1}<<(limit%64+1))-1;
            }
        }
        return bits;
    }
    Reach Excluding(const Ints& quantity,const Ints& thresholds,int omit,int limit)
    {
        Reach bits;bits.Word[0]=1;const int length=limit/64+1;
        for(int weight:thresholds)
        {
            Probe();if(weight==omit)continue;int n=std::min(quantity[weight],limit/weight);
            for(int batch=1;n;batch*=2)
            {
                Probe();const int take=std::min(batch,n);n-=take;const int shift=take*weight,words=shift/64,offset=shift%64;
                for(int j=length;j-->words;)
                {Probe();U value=bits.Word[j-words]<<offset;if(offset&&j>words)value|=bits.Word[j-words-1]>>(64-offset);bits.Word[j]|=value;}
                if(limit%64!=63)bits.Word[length-1]&=(U{1}<<(limit%64+1))-1;
            }
        }
        return bits;
    }
    bool ExactCrossings(const std::pmr::vector<Line>& lines,int extent,const Ints& quantity,
        const Ints& demand,const Ints& thresholds,std::pmr::vector<Reach>& excluded)
    {
        Ints lengths(extent+1,0,Memory);
        for(const auto& line:lines)for(int start=Next(line,0,extent,false);start<extent;)
        {Probe();const int end=Next(line,start,extent,true);++lengths[end-start];start=Next(line,end,extent,false);}
        if(excluded.empty())
        {
            // A root slice may interrupt construction. Publish the cached
            // exclusion vector only after every entry is complete.
            std::pmr::vector<Reach> built(Memory);built.reserve(thresholds.size());
            for(int width:thresholds){Probe();built.push_back(Excluding(quantity,thresholds,width,extent));}
            excluded=std::move(built);
        }
        for(std::size_t index=0;index<thresholds.size();++index)
        {
            Probe();const int width=thresholds[index];int lower=0,upper=0;
            for(int length=1;length<=extent;++length)
            {
                Probe();if(!lengths[length])continue;const int cap=std::min(quantity[width],length/width);int low=cap+1,high=-1;
                for(int count=0;count<=cap;++count)
                {Probe();if(excluded[index].Has(length-count*width)){low=std::min(low,count);high=count;}}
                if(high<0)return false;lower+=low*lengths[length];upper+=high*lengths[length];if(lower>demand[width])return false;
            }
            if(upper<demand[width])return false;
        }
        return true;
    }
    bool Runs(const std::pmr::vector<Line>& lines,int extent,const Reach& reachable,
        const Ints& demand,const Ints& crossing,const Ints& thresholds)
    {
        std::array<int,513> capacity{},runCounts{};
        for(const auto& line:lines)for(int start=Next(line,0,extent,false);start<extent;)
        {
            Probe();const int end=Next(line,start,extent,true),length=end-start;
            if(!reachable.Has(length))return false;capacity[length]+=length;++runCounts[length];start=Next(line,end,extent,false);
        }
        for(int i=extent;i>=1;--i)
        {Probe();if(i<extent){capacity[i]+=capacity[i+1];runCounts[i]+=runCounts[i+1];}if(demand[i]>capacity[i])return false;}
        for(int threshold:thresholds)
        {
            Probe();int available=0;for(int k=threshold;k<=extent;k+=threshold){Probe();available+=runCounts[k];}
            if(crossing[threshold]>available)return false;
        }
        return true;
    }
    void BuildProjection(Projection& p)
    {
        p.X=Subsets(W,false);p.Y=Subsets(H,true);
        p.WidthArea.resize(W+1);p.HeightArea.resize(H+1);p.WidthCross.resize(W+1);p.HeightCross.resize(H+1);
        p.WidthQuantity.resize(W+1);p.HeightQuantity.resize(H+1);p.WidthExact.resize(W+1);p.HeightExact.resize(H+1);
        for(std::size_t i=0;i<Types.size();++i)
        {
            Probe();const auto& t=Types[i];const int area=t.Width*t.Height*Remaining[i];p.WidthArea[t.Width]+=area;p.HeightArea[t.Height]+=area;
            p.WidthCross[t.Width]+=t.Height*Remaining[i];p.HeightCross[t.Height]+=t.Width*Remaining[i];
            p.WidthExact[t.Width]+=t.Height*Remaining[i];p.HeightExact[t.Height]+=t.Width*Remaining[i];
            p.WidthQuantity[t.Width]+=Remaining[i];p.HeightQuantity[t.Height]+=Remaining[i];
            if(Remaining[i]){p.WidthThresholds.push_back(t.Width);p.HeightThresholds.push_back(t.Height);}
        }
        for(auto* thresholds:{&p.WidthThresholds,&p.HeightThresholds})
        {
            std::sort(thresholds->begin(),thresholds->end(),[&](int a,int b){Probe();return a<b;});
            thresholds->erase(std::unique(thresholds->begin(),thresholds->end(),[&](int a,int b){Probe();return a==b;}),thresholds->end());
        }
        for(int i=W-1;i>=1;--i){Probe();p.WidthArea[i]+=p.WidthArea[i+1];p.WidthCross[i]+=p.WidthCross[i+1];}
        for(int i=H-1;i>=1;--i){Probe();p.HeightArea[i]+=p.HeightArea[i+1];p.HeightCross[i]+=p.HeightCross[i+1];}
    }
    bool AllHaveSpace()
    {
        std::fill(Histogram.begin(),Histogram.end(),0);std::fill(BestWidth.begin(),BestWidth.end(),0);
        for(int y=0;y<H;++y)
        {
            for(int x=0;x<W;++x){Probe();Histogram[x]=((Rows[y].Word[x/64]>>(x%64))&1)?0:Histogram[x]+1;}
            HistogramStack.clear();
            for(int x=0;x<=W;++x)
            {
                Probe();const int height=x<W?Histogram[x]:0;int start=x;
                while(!HistogramStack.empty()&&HistogramStack.back().second>=height)
                {Probe();const auto [begin,h]=HistogramStack.back();HistogramStack.pop_back();if(h)BestWidth[h]=std::max(BestWidth[h],x-begin);start=begin;}
                HistogramStack.emplace_back(start,height);
            }
        }
        for(int h=H-1;h>=1;--h){Probe();BestWidth[h]=std::max(BestWidth[h],BestWidth[h+1]);}
        for(std::size_t i=0;i<Types.size();++i){Probe();if(Remaining[i]&&Types[i].Width>BestWidth[Types[i].Height])return false;}
        return true;
    }
    bool FeasibleAndNew()
    {
        auto key=CountKey();std::pmr::string whole(key,Memory);
        for(const auto& row:Rows){Probe();whole.append(reinterpret_cast<const char*>(row.Word.data()),((W+63)/64)*sizeof(U));}
        Probe();if(Seen.size()<MemoCap&&!Seen.insert(std::move(whole)).second){++Stats.MemoHits;return false;}
        Projection local(Memory);Projection* p=nullptr;
        Probe();if(auto found=SpanCache.find(key);found!=SpanCache.end())p=&found->second;
        else
        {
            BuildProjection(local);p=&local;
            if(SpanCache.size()<ProjectionCap)
            {Probe();auto [entry,inserted]=SpanCache.try_emplace(std::move(key),std::move(local));p=&entry->second;}
        }
        if(!Runs(Rows,W,p->X,p->WidthArea,p->WidthCross,p->WidthThresholds)||
           !Runs(Cols,H,p->Y,p->HeightArea,p->HeightCross,p->HeightThresholds)||
           !ExactCrossings(Rows,W,p->WidthQuantity,p->WidthExact,p->WidthThresholds,p->WidthExcluded)||
           !ExactCrossings(Cols,H,p->HeightQuantity,p->HeightExact,p->HeightThresholds,p->HeightExcluded))
        {++Stats.SpanPrunes;return false;}
        if(!AllHaveSpace()){++Stats.DomainPrunes;return false;}return true;
    }
    bool Fits(int x,int y,int width,int height)
    {
        Probe();if(x<0||y<0||x>W-width||y>H-height)return false;
        for(int yy=y;yy<y+height;++yy)if(Intersects(Rows[yy],x,width))return false;return true;
    }
    std::pmr::vector<Candidate> Choose(bool root)
    {
        std::pmr::vector<Corner> corners(Memory);
        for(int y=0;y<H;++y)for(int k=0;k<(W+63)/64;++k)
        {
            Probe();const U row=Rows[y].Word[k],full=Full(k,W),empty=full^row;if(!empty)continue;
            const U left=((row<<1)|(k?Rows[y].Word[k-1]>>63:1))&full;
            U right=row>>1;if(k+1<(W+63)/64)right|=Rows[y].Word[k+1]<<63;else right|=U{1}<<((W-1)%64);
            const U down=y?Rows[y-1].Word[k]:full,up=y+1<H?Rows[y+1].Word[k]:full;
            const U masks[4]={empty&left&down,empty&right&down,empty&left&up,empty&right&up};
            for(int direction=0;direction<4;++direction)for(auto bits=masks[direction];bits;bits&=bits-1)
            {
                Probe();const int x=k*64+std::countr_zero(bits),rx=direction&1,ry=direction>>1;
                const int width=Ray(Rows[y],x,W,rx),height=Ray(Cols[x],y,H,ry);corners.push_back({width*height,width,height,y,x,rx,ry});
            }
        }
        std::sort(corners.begin(),corners.end(),[&](const auto& a,const auto& b)
        {Probe();return std::tie(a.Space,a.Width,a.Height,a.Y,a.X,a.RX,a.RY)<std::tie(b.Space,b.Width,b.Height,b.Y,b.X,b.RX,b.RY);});
        std::pmr::vector<Candidate> best(Memory);bool selected=false;
        for(const auto& c:corners)
        {
            Probe();std::pmr::vector<Candidate> choices(Memory);
            for(int i:TypeOrder)
            {
                Probe();const auto& t=Types[i];if(!Remaining[i]||t.Width>c.Width||t.Height>c.Height)continue;
                const int x=c.RX?c.X-t.Width+1:c.X,y=c.RY?c.Y-t.Height+1:c.Y;
                if(Fits(x,y,t.Width,t.Height))
                {choices.push_back({i,x,y,false});if(selected&&choices.size()>best.size())break;}
            }
            if(choices.empty()){++Stats.DomainPrunes;return {};}
            if(!selected||choices.size()<best.size()){best=std::move(choices);selected=true;}
        }
        const int limit=root?Limits.RootMacroLimit:Limits.MacroLimit;
        if(!selected||MacroOrder.empty()||limit<=0)return best;
        std::array<U,8> present{};
        for(std::size_t i=0;i<Remaining.size();++i){Probe();if(Remaining[i])present[i/64]|=U{1}<<(i%64);}
        std::pmr::vector<Candidate> combined(Memory);combined.reserve(limit+best.size());
        for(int index:MacroOrder)
        {
            Probe();if(combined.size()>=static_cast<std::size_t>(limit))break;const auto& macro=Macros[index];bool available=true;
            for(std::size_t k=0;k<(Types.size()+63)/64;++k){Probe();if(RequiredBits[index][k]&~present[k]){available=false;break;}}
            if(!available)continue;
            for(const auto [id,count]:macro.Uses){Probe();if(Remaining[id]<count){available=false;break;}}
            if(!available)continue;
            std::pmr::vector<std::pair<int,int>> positions(Memory);
            for(const auto& c:corners)
            {
                Probe();if(macro.Width>c.Width||macro.Height>c.Height)continue;
                if(root&&EmptyRoot&&(c.X!=0||c.Y!=0||c.RX||c.RY))continue;
                const int x=c.RX?c.X-macro.Width+1:c.X,y=c.RY?c.Y-macro.Height+1:c.Y;
                bool duplicate=false;for(const auto& p:positions){Probe();if(p==std::make_pair(x,y)){duplicate=true;break;}}
                if(duplicate)continue;positions.emplace_back(x,y);
                if(Fits(x,y,macro.Width,macro.Height))combined.push_back({index,x,y,true});
                if(combined.size()>=static_cast<std::size_t>(limit))break;
            }
        }
        for(const auto& candidate:best){Probe();combined.push_back(candidate);}return combined;
    }
    void Change(const Candidate& candidate,bool add)
    {
        Probe();const int index=candidate.Index,sign=add?-1:1;
        const int width=candidate.Macro?Macros[index].Width:Types[index].Width;
        const int height=candidate.Macro?Macros[index].Height:Types[index].Height;
        const int members=candidate.Macro?Macros[index].Members:1;
        if(candidate.Macro)for(const auto [id,count]:Macros[index].Uses){Probe();Remaining[id]+=sign*count;}
        else{Probe();Remaining[index]+=sign;}
        for(int y=candidate.Y;y<candidate.Y+height;++y)Flip(Rows[y],candidate.X,width);
        for(int x=candidate.X;x<candidate.X+width;++x)Flip(Cols[x],candidate.Y,height);
        if(add)Path.push_back({candidate.Macro?Macros[index].Node:PrimitiveNodes[index],candidate.X,candidate.Y});
        else Path.pop_back();PlacedCount-=sign*members;
    }
    bool Dfs()
    {
        EnterNode();Stats.BestPlacedCount=std::max(Stats.BestPlacedCount,PlacedCount);
        if(PlacedCount==Required)return true;
        if(!FeasibleAndNew())return false;
        const auto options=Choose(false);
        for(const auto& candidate:options)
        {Check(true);Change(candidate,true);if(Dfs())return true;Change(candidate,false);}return false;
    }
    void RestoreRoot()
    {
        BranchActive=false;Check(true);
        for(std::size_t i=0;i<Rows.size();++i){Probe();Rows[i]=RootRows[i];}
        for(std::size_t i=0;i<Cols.size();++i){Probe();Cols[i]=RootCols[i];}
        for(std::size_t i=0;i<Remaining.size();++i){Probe();Remaining[i]=RootRemaining[i];}
        for(const auto& p:Path){(void)p;Probe();}Path.clear();PlacedCount=0;Check(true);
    }
public:
    Search(int width,int height,std::span<const RectangleBlockType> types,std::span<const RectangleBlockMacro> macros,
        std::span<const int> remaining,std::span<const int> primitiveNodes,std::span<const RectangleResidualFixed> fixed,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,const RectangleResidualLimits& limits,RectangleResidualStats& stats)
        :W(width),H(height),Types(types),Macros(macros),PrimitiveNodes(primitiveNodes),Job(job),Memory(memory),Limits(limits),Stats(stats),
         Remaining(memory),RootRemaining(memory),TypeOrder(memory),TypeRank(memory),MacroOrder(memory),Histogram(memory),BestWidth(memory),MacroSignatures(memory),HistogramStack(memory),
         Rows(memory),Cols(memory),RootRows(memory),RootCols(memory),RequiredBits(memory),Path(memory),Seen(memory),SpanCache(memory)
    {
        Check(true);
        if(W<1||H<1||W>512||H>512||W*H>65536||Types.size()>512||Macros.size()>100000||remaining.size()!=Types.size()||
           primitiveNodes.size()!=Types.size()||!limits.Nodes||!limits.Probes||limits.MacroLimit<0||limits.MacroLimit>512||
           limits.RootMacroLimit<0||limits.RootMacroLimit>512)throw Invalid{};
        Remaining.assign(remaining.begin(),remaining.end());Rows.resize(H);Cols.resize(W);Histogram.resize(W);BestWidth.resize(H+1);HistogramStack.reserve(W+1);
        std::int64_t area=0;
        for(std::size_t i=0;i<Types.size();++i)
        {
            Probe();const auto& t=Types[i];if(t.Width<1||t.Height<1||t.Width>W||t.Height>H||t.Quantity<0||t.Quantity>512||
                Remaining[i]<0||Remaining[i]>t.Quantity||PrimitiveNodes[i]<0)throw Invalid{};
            Required+=Remaining[i];if(Required>512)throw Invalid{};area+=std::int64_t(t.Width)*t.Height*Remaining[i];TypeOrder.push_back(static_cast<int>(i));
        }
        Stats.RequiredCount=Required;
        std::sort(TypeOrder.begin(),TypeOrder.end(),[&](int a,int b)
        {Probe();const auto& x=Types[a];const auto& y=Types[b];return std::tuple(-x.Width*x.Height,-x.Width,-x.Height)<std::tuple(-y.Width*y.Height,-y.Width,-y.Height);});
        for(std::size_t i=1;i<TypeOrder.size();++i)
        {Probe();const auto& a=Types[TypeOrder[i-1]];const auto& b=Types[TypeOrder[i]];if(a.Width==b.Width&&a.Height==b.Height)throw Invalid{};}
        TypeRank.resize(Types.size());for(std::size_t i=0;i<TypeOrder.size();++i){Probe();TypeRank[TypeOrder[i]]=static_cast<int>(i);}
        EmptyRoot=fixed.empty();
        for(const auto& r:fixed)
        {
            Probe();if(r.Width<1||r.Height<1||r.X<0||r.Y<0||r.X>W-r.Width||r.Y>H-r.Height)throw Invalid{};
            for(int y=r.Y;y<r.Y+r.Height;++y){if(Intersects(Rows[y],r.X,r.Width))throw Invalid{};Flip(Rows[y],r.X,r.Width);}
            for(int x=r.X;x<r.X+r.Width;++x)Flip(Cols[x],r.Y,r.Height);area+=std::int64_t(r.Width)*r.Height;
        }
        if(area!=W*H)throw Invalid{};
        RequiredBits.resize(Macros.size());MacroSignatures.resize(Macros.size());
        for(std::size_t i=0;i<Macros.size();++i)
        {
            Probe();const auto& m=Macros[i];if(m.Width<1||m.Height<1||m.Width>W||m.Height>H||m.Members<1||m.Members>512||m.Node<0||m.Uses.empty())throw Invalid{};
            int members=0,last=-1;std::int64_t material=0;bool available=true;
            for(const auto [id,count]:m.Uses)
            {
                Probe();if(id<0||id>=static_cast<int>(Types.size())||id<=last||count<1||count>Types[id].Quantity)throw Invalid{};
                last=id;members+=count;material+=std::int64_t(Types[id].Width)*Types[id].Height*count;RequiredBits[i][id/64]|=U{1}<<(id%64);
                MacroSignatures[i].emplace_back(TypeRank[id],count);
                if(count>Remaining[id])available=false;
            }
            if(members!=m.Members||material!=m.Width*m.Height)throw Invalid{};
            std::sort(MacroSignatures[i].begin(),MacroSignatures[i].end(),[&](auto a,auto b){Probe();return a<b;});
            if(available&&m.Members>=2)MacroOrder.push_back(static_cast<int>(i));
        }
        std::sort(MacroOrder.begin(),MacroOrder.end(),[&](int a,int b)
        {
            Probe();const auto& x=Macros[a];const auto& y=Macros[b];
            if(x.Width*x.Height!=y.Width*y.Height)return x.Width*x.Height>y.Width*y.Height;
            if(x.Members!=y.Members)return x.Members>y.Members;
            if(x.Width!=y.Width)return x.Width<y.Width;if(x.Height!=y.Height)return x.Height<y.Height;
            return std::lexicographical_compare(MacroSignatures[a].begin(),MacroSignatures[a].end(),MacroSignatures[b].begin(),MacroSignatures[b].end(),[&](auto p,auto q){Probe();return p<q;});
        });
        // Duplicate decomposition trees with the same dense envelope/inventory
        // have identical placement feasibility. Keep the first canonical one.
        MacroOrder.erase(std::unique(MacroOrder.begin(),MacroOrder.end(),[&](int a,int b)
        {
            Probe();const auto& x=Macros[a];const auto& y=Macros[b];if(x.Width!=y.Width||x.Height!=y.Height||x.Uses.size()!=y.Uses.size())return false;
            for(std::size_t i=0;i<x.Uses.size();++i){Probe();if(x.Uses[i]!=y.Uses[i])return false;}return true;
        }),MacroOrder.end());
        const std::size_t seenEstimate=H*((W+63)/64)*sizeof(U)+Types.size()*2+128;
        const std::size_t projectionEstimate=(W+H+2)*16+2*Types.size()*(sizeof(Reach)+sizeof(int))+Types.size()*2+sizeof(Projection)+256;
        MemoCap=std::min<std::size_t>(100000,Limits.MemoBytes/seenEstimate);
        ProjectionCap=std::min<std::size_t>(100000,Limits.ProjectionBytes/projectionEstimate);
        RootRemaining=Remaining;RootRows=Rows;RootCols=Cols;Check(true);
    }
    ~Search(){Stats.MemoEntries=Seen.size();Stats.ProjectionEntries=SpanCache.size();}
    bool Run(std::pmr::vector<RectangleBlockPlacement>& output)
    {
        EnterNode();
        if(Required==0){Stats.Complete=true;return true;}
        if(!FeasibleAndNew())return false;
        const auto options=Choose(true);
        for(const auto& candidate:options)
        {
            RestoreRoot();++Stats.RootBranches;BranchNodeBase=Stats.Nodes;BranchProbeBase=Stats.Probes;BranchActive=true;
            bool complete=false;
            try{Change(candidate,true);complete=Dfs();}
            catch(const RootStopped&){++Stats.RootTruncations;BranchActive=false;Seen.clear();}
            BranchActive=false;
            if(complete)
            {
                Check(true);for(int value:Remaining){Probe();if(value)throw Invalid{};}
                for(const auto& row:Rows)for(int word=0;word<(W+63)/64;++word){Probe();if(row.Word[word]!=Full(word,W))throw Invalid{};}
                output.reserve(Path.size());for(const auto& p:Path){Probe();output.push_back(p);}Check(true);Stats.Complete=true;return true;
            }
        }
        RestoreRoot();Stats.MemoEntries=Seen.size();Stats.ProjectionEntries=SpanCache.size();return false;
    }
};
}

bool SearchRectangleResidualCorners(int width,int height,std::span<const RectangleBlockType> types,
    std::span<const RectangleBlockMacro> macros,std::span<const int> remaining,std::span<const int> primitiveNodeByType,
    std::span<const RectangleResidualFixed> fixedOccupancy,RectangleBlockBudget& job,std::pmr::memory_resource* memory,
    const RectangleResidualLimits& limits,std::pmr::vector<RectangleBlockPlacement>& output,RectangleResidualStats& stats)
{
    output.clear();stats={};
    if(!memory||output.get_allocator().resource()!=memory){stats.InvalidInput=true;return false;}
    try
    {
        Search search(width,height,types,macros,remaining,primitiveNodeByType,fixedOccupancy,job,memory,limits,stats);
        if(search.Run(output))return true;
    }
    catch(const Invalid&){stats.InvalidInput=true;}
    catch(const TrialStopped&){}
    catch(const RectangleBlockInterrupted&){stats.GlobalBudget=true;stats.Cancelled=job.Cancelled;}
    catch(const std::bad_alloc&){stats.MemoryLimit=true;}
    output.clear();stats.Complete=false;return false;
}
}
