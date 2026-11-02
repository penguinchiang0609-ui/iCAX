#include "NestingRectangleStripeRepair.h"

#include <cstdint>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct InvalidStripeInput {};
    using Word=std::uint64_t;
    int Stripe(int x,int y,int width,int height,bool rows)
    {
        if(rows)return height%2?(y%2?-width:width):0;
        return width%2?(x%2?-height:height):0;
    }
    int Checker(int x,int y,int width,int height)
    {return width%2&&height%2?((x+y)%2?-1:1):0;}

    RectangleStripeCompatibility Check(int width,int height,std::span<const RectangleBlockType> types,
        std::span<const int> remaining,std::span<const RectangleResidualFixed> fixed,
        RectangleBlockBudget& job,std::pmr::memory_resource* memory,RectangleStripeStats& stats)
    {
        const auto probe=[&]{ProbeRectangleBlockBudget(job);};
        CheckRectangleBlockBudget(job,true);
        if(!memory||width<1||height<1||width>512||height>512||width*height>65536||
           types.size()>512||remaining.size()!=types.size()||fixed.size()>65536)throw InvalidStripeInput{};
        std::int64_t area=0;int count=0,oddBoth=0;
        stats.RowTarget=Stripe(0,0,width,height,true);
        stats.ColumnTarget=Stripe(0,0,width,height,false);
        stats.CheckerTarget=Checker(0,0,width,height);
        for(std::size_t i=0;i<types.size();++i)
        {
            probe();const auto& type=types[i];const int quantity=remaining[i];
            if(type.Width<1||type.Height<1||type.Width>512||type.Height>512||type.Quantity<0||
               type.Quantity>512||quantity<0||quantity>type.Quantity)throw InvalidStripeInput{};
            count+=quantity;if(count>512)throw InvalidStripeInput{};
            area+=std::int64_t(type.Width)*type.Height*quantity;
            if(quantity&&(type.Width>width||type.Height>height))throw InvalidStripeInput{};
            if(type.Width%2&&type.Height%2)oddBoth+=quantity;
        }
        for(std::size_t i=0;i<fixed.size();++i)
        {
            probe();const auto& rect=fixed[i];
            if(rect.Width<1||rect.Height<1||rect.Width>width||rect.Height>height||rect.X<0||rect.Y<0||
               rect.X>width-rect.Width||rect.Y>height-rect.Height)throw InvalidStripeInput{};
            for(std::size_t j=0;j<i;++j)
            {
                probe();const auto& other=fixed[j];
                if(rect.X<other.X+other.Width&&other.X<rect.X+rect.Width&&
                   rect.Y<other.Y+other.Height&&other.Y<rect.Y+rect.Height)throw InvalidStripeInput{};
            }
            area+=std::int64_t(rect.Width)*rect.Height;
            stats.RowTarget-=Stripe(rect.X,rect.Y,rect.Width,rect.Height,true);
            stats.ColumnTarget-=Stripe(rect.X,rect.Y,rect.Width,rect.Height,false);
            stats.CheckerTarget-=Checker(rect.X,rect.Y,rect.Width,rect.Height);
        }
        if(area!=width*height)throw InvalidStripeInput{};
        if(stats.CheckerTarget < -oddBoth||stats.CheckerTarget>oddBoth||(oddBoth-stats.CheckerTarget)%2)
        {stats.StripeRejected=true;stats.Infeasible=true;return RectangleStripeCompatibility::Incompatible;}
        for(bool rows:{true,false})
        {
            probe();int total=0;
            for(std::size_t i=0;i<types.size();++i)
            {probe();const auto& t=types[i];if((rows?t.Height:t.Width)%2)total+=(rows?t.Width:t.Height)*remaining[i];}
            const int target=rows?stats.RowTarget:stats.ColumnTarget;
            if(target < -total||target>total||(total-target)%2)
            {stats.StripeRejected=true;stats.Infeasible=true;return RectangleStripeCompatibility::Incompatible;}
            std::pmr::vector<Word> bits(total/64+1,0,memory);bits[0]=1;
            for(std::size_t i=0;i<types.size();++i)
            {
                probe();const auto& t=types[i];if(!((rows?t.Height:t.Width)%2))continue;
                const int weight=rows?t.Width:t.Height;int quantity=remaining[i];
                for(int batch=1;quantity;batch*=2)
                {
                    probe();const int take=std::min(batch,quantity);quantity-=take;
                    const int shift=take*weight,words=shift/64,offset=shift%64;
                    for(int j=static_cast<int>(bits.size());j-->words;)
                    {
                        probe();Word value=bits[j-words]<<offset;
                        if(offset&&j>words)value|=bits[j-words-1]>>(64-offset);
                        bits[j]|=value;
                    }
                }
            }
            const int subset=(total-target)/2;probe();
            if(!((bits[subset/64]>>(subset%64))&1))
            {stats.StripeRejected=true;stats.Infeasible=true;return RectangleStripeCompatibility::Incompatible;}
        }
        CheckRectangleBlockBudget(job,true);return RectangleStripeCompatibility::Compatible;
    }
}

RectangleStripeCompatibility CheckRectangleStripeCompatibility(int width,int height,
    std::span<const RectangleBlockType> types,std::span<const int> remaining,
    std::span<const RectangleResidualFixed> fixedOccupancy,
    RectangleBlockBudget& job,std::pmr::memory_resource* memory,RectangleStripeStats& stats)
{
    stats={};const auto initial=job.Attempts;auto outcome=RectangleStripeCompatibility::Unknown;
    try{outcome=Check(width,height,types,remaining,fixedOccupancy,job,memory,stats);}
    catch(const InvalidStripeInput&){stats.InvalidInput=true;}
    catch(const RectangleBlockInterrupted&){stats.GlobalBudget=true;stats.Cancelled=job.Cancelled;}
    catch(const std::bad_alloc&){stats.MemoryLimit=true;}
    stats.Checks=job.Attempts-initial;return outcome;
}

bool SearchRectangleStripeRepair(int width,int height,
    std::span<const RectangleBlockType> types,std::span<const int> remaining,
    std::span<const int> primitiveNodeByType,std::span<const RectangleResidualFixed> fixedOccupancy,
    RectangleBlockBudget& job,std::pmr::memory_resource* memory,
    const RectangleResidualLimits& limits,
    std::pmr::vector<RectangleBlockPlacement>& output,RectangleStripeStats& stats)
{
    output.clear();stats={};const auto initial=job.Attempts;
    if(!memory||output.get_allocator().resource()!=memory||primitiveNodeByType.size()!=types.size())
    {stats.InvalidInput=true;return false;}
    const auto compatibility=CheckRectangleStripeCompatibility(width,height,types,remaining,fixedOccupancy,job,memory,stats);
    if(compatibility!=RectangleStripeCompatibility::Compatible)return false;
    try
    {
        // Order active specifications using geometry only. The caller supplies
        // a unique primitive decomposition node for each canonical shape.
        struct Active {RectangleBlockType Type;int Node;};
        std::pmr::vector<Active> active(memory);
        for(std::size_t i=0;i<types.size();++i)
        {
            ProbeRectangleBlockBudget(job);if(!remaining[i])continue;
            if(primitiveNodeByType[i]<0)throw InvalidStripeInput{};
            // Distinct node identities must stay distinct in reconstruction;
            // the caller supplies one primitive decomposition node per type.
            active.push_back({{types[i].Width,types[i].Height,remaining[i]},primitiveNodeByType[i]});
        }
        std::sort(active.begin(),active.end(),[&](const Active& a,const Active& b)
        {ProbeRectangleBlockBudget(job);return std::tie(a.Type.Width,a.Type.Height)<std::tie(b.Type.Width,b.Type.Height);});
        std::pmr::vector<RectangleBlockType> compact(memory);
        std::pmr::vector<int> counts(memory),nodes(memory);
        for(const auto& a:active)
        {
            ProbeRectangleBlockBudget(job);
            if(!compact.empty()&&a.Type.Width==compact.back().Width&&a.Type.Height==compact.back().Height)throw InvalidStripeInput{};
            for(int node:nodes){ProbeRectangleBlockBudget(job);if(node==a.Node)throw InvalidStripeInput{};}
            compact.push_back(a.Type);counts.push_back(a.Type.Quantity);nodes.push_back(a.Node);
        }
        auto primitiveLimits=limits;primitiveLimits.MacroLimit=0;primitiveLimits.RootMacroLimit=0;
        primitiveLimits.RootBranchNodes=0;primitiveLimits.RootBranchProbes=0;
        stats.Complete=SearchRectangleResidualCorners(width,height,compact,{},counts,nodes,fixedOccupancy,
            job,memory,primitiveLimits,output,stats.Search);
        stats.InvalidInput=stats.Search.InvalidInput;stats.MemoryLimit=stats.Search.MemoryLimit;
        stats.GlobalBudget=stats.Search.GlobalBudget;stats.Cancelled=stats.Search.Cancelled;
        stats.Infeasible=!stats.Complete&&!stats.InvalidInput&&!stats.MemoryLimit&&!stats.GlobalBudget&&
            !stats.Search.TrialNodeLimit&&!stats.Search.TrialProbeLimit&&!stats.Search.RootTruncations;
    }
    catch(const InvalidStripeInput&){stats.InvalidInput=true;}
    catch(const RectangleBlockInterrupted&){stats.GlobalBudget=true;stats.Cancelled=job.Cancelled;}
    catch(const std::bad_alloc&){stats.MemoryLimit=true;}
    stats.Checks=job.Attempts-initial;
    if(!stats.Complete)output.clear();return stats.Complete;
}
}
