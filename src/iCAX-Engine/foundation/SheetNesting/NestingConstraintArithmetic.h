#pragma once
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error Exact constraint expansions require precise IEEE floating-point arithmetic.
#endif

namespace iCAX::SheetNesting::Detail
{
    // Exact binary64 affine arithmetic for integer CP right-hand sides. Fixed
    // placement coordinates remain their original doubles; no coordinate grid
    // or epsilon is applied to them. This uses error-free TwoSum and FMA
    // expansions, and rejects unsupported arithmetic instead of rounding it.
    inline bool FloorConstraintAffine(std::int64_t constant,
        const std::vector<std::pair<std::int64_t,double>>& terms,std::int64_t& output)
    {
        constexpr double exactIntegerLimit=9007199254740992.;
        constexpr std::int64_t integerLimit=9007199254740992LL;
        if(!std::numeric_limits<double>::is_iec559||std::fegetround()!=FE_TONEAREST||
            constant>integerLimit||constant< -integerLimit)return false;
        using Expansion=std::vector<double>;
        const auto grow=[](Expansion& expansion,double value)
        {
            if(!std::isfinite(value))return false;
            Expansion next;next.reserve(expansion.size()+1);
            for(const double component:expansion)
            {
                const double sum=value+component,virtualComponent=sum-value;
                const double error=(value-(sum-virtualComponent))+(component-virtualComponent);
                if(!std::isfinite(sum)||!std::isfinite(error))return false;
                if(error!=0)next.push_back(error);value=sum;
            }
            if(value!=0)next.push_back(value);expansion=std::move(next);return true;
        };
        Expansion expansion;
        if(!grow(expansion,static_cast<double>(constant)))return false;
        for(const auto [coefficient,value]:terms)
        {
            if(coefficient==0||value==0)continue;
            if(!std::isfinite(value)||std::abs(value)<1e-200||std::abs(value)>1e100||
                coefficient>integerLimit||coefficient< -integerLimit)return false;
            const double factor=static_cast<double>(coefficient),product=factor*value;
            const double error=std::fma(factor,value,-product);
            if(!grow(expansion,error)||!grow(expansion,product))return false;
        }
        double estimate=0;for(const double component:expansion)estimate+=component;
        if(!std::isfinite(estimate)||std::abs(estimate)>exactIntegerLimit-4)return false;
        auto candidate=static_cast<std::int64_t>(std::floor(estimate));
        const auto compare=[&](std::int64_t integer,int& sign)
        {
            auto difference=expansion;if(!grow(difference,-static_cast<double>(integer)))return false;
            sign=difference.empty()?0:(difference.back()>0?1:-1);return true;
        };
        int lower=0,upper=0;
        if(!compare(candidate,lower))return false;
        if(lower<0)--candidate;
        if(!compare(candidate+1,upper))return false;
        if(upper>=0)++candidate;
        if(!compare(candidate,lower)||!compare(candidate+1,upper)||lower<0||upper>=0)return false;
        output=candidate;return true;
    }
}
