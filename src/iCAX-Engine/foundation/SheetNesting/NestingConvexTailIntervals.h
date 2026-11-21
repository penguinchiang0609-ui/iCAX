#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace iCAX::SheetNesting::ConvexTailDetail
{
    using Integer=std::int64_t;
    struct IntegerRange {Integer Low=0,High=-1;};
    inline Integer FloorDivide(Integer numerator,Integer positiveDenominator)
    {const auto q=numerator/positiveDenominator,r=numerator%positiveDenominator;return q-(r<0?1:0);}
    inline Integer CeilDivide(Integer numerator,Integer positiveDenominator)
    {const auto q=numerator/positiveDenominator,r=numerator%positiveDenominator;return q+(r>0?1:0);}

    // Moving projection is [aLow+c*p,aHigh+c*p]. Intersect the integer
    // positions where its interior overlaps [bLow,bHigh]. Strict inequalities
    // preserve exact legal contact, including rational noninteger endpoints.
    // The owning convex-tail scope bounds all arithmetic well inside int64.
    inline bool IntersectStrictProjection(IntegerRange& range,Integer c,
        Integer aLow,Integer aHigh,Integer bLow,Integer bHigh)
    {
        if(!c)
        {if(aHigh<=bLow||bHigh<=aLow){range.High=range.Low-1;return false;}return range.Low<=range.High;}
        const auto lower=bLow-aHigh,upper=bHigh-aLow;
        if(c>0)
        {range.Low=std::max(range.Low,FloorDivide(lower,c)+1);range.High=std::min(range.High,CeilDivide(upper,c)-1);}
        else
        {const auto d=-c;range.Low=std::max(range.Low,FloorDivide(-upper,d)+1);range.High=std::min(range.High,CeilDivide(-lower,d)-1);}
        return range.Low<=range.High;
    }
    inline std::optional<Integer> FirstAllowedInteger(Integer upper,std::vector<IntegerRange> forbidden)
    {
        std::sort(forbidden.begin(),forbidden.end(),[](auto a,auto b){return a.Low!=b.Low?a.Low<b.Low:a.High<b.High;});
        Integer position=0;
        for(const auto interval:forbidden)
        {
            if(interval.High<position)continue;if(interval.Low>position)break;
            position=interval.High+1;if(position>upper)return {};
        }
        return position<=upper?std::optional<Integer>{position}:std::nullopt;
    }
}
