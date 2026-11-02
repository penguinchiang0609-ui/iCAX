#pragma once
#include "MachineMotion.h"
#include <limits>
#include <utility>

namespace iCAX::CAM::Motion {
struct KeyposeReference {Vector Position{},Direction{0,0,-1};bool Mandatory=false;};
// Seed selection only. The original reference and full-motion checks remain
// authoritative; their FK tolerances trigger additional inverse solves.
inline std::vector<std::size_t> SelectKeyposes(const std::vector<KeyposeReference>& samples,
    double positionSeedMm=5,double directionSeedRad=.08726646259971647,double maximumSeedSpanMm=50){
    if(samples.empty())return {};
    if(!std::isfinite(positionSeedMm)||positionSeedMm<=0||!std::isfinite(directionSeedRad)||directionSeedRad<=0||!std::isfinite(maximumSeedSpanMm)||maximumSeedSpanMm<=0)throw std::invalid_argument("MOTION_KEYPOSE_POLICY_INVALID");
    const auto sub=[](Vector a,const Vector& b){for(int k=0;k<3;++k)a[k]-=b[k];return a;};
    const auto dot=[](const Vector& a,const Vector& b){double r=0;for(int k=0;k<3;++k)r+=a[k]*b[k];return r;};
    std::vector<bool> keep(samples.size());std::vector<double> distance(samples.size());std::vector<Vector> directions;
    for(std::size_t i=0;i<samples.size();++i){for(double v:samples[i].Position)if(!std::isfinite(v))throw std::invalid_argument("MOTION_KEYPOSE_REFERENCE_INVALID");directions.push_back(Unit(samples[i].Direction));keep[i]=samples[i].Mandatory;if(i)distance[i]=distance[i-1]+Length(sub(samples[i].Position,samples[i-1].Position));}
    keep.front()=keep.back()=true;std::vector<std::pair<std::size_t,std::size_t>> pending;std::size_t first=0;
    for(std::size_t i=1;i<keep.size();++i)if(keep[i]){pending.emplace_back(first,i);first=i;}
    while(!pending.empty()){
        const auto [a,b]=pending.back();pending.pop_back();if(b<=a+1)continue;const double length=distance[b]-distance[a];
        std::size_t split=a;double largest=1;const auto da=directions[a],db=directions[b];const double angle=std::acos(std::clamp(dot(da,db),-1.,1.));
        for(std::size_t i=a+1;i<b;++i){const double t=length>1e-12?(distance[i]-distance[a])/length:double(i-a)/(b-a);Vector position{},direction{};
            for(int k=0;k<3;++k){position[k]=samples[a].Position[k]+t*(samples[b].Position[k]-samples[a].Position[k]);direction[k]=(1-t)*da[k]+t*db[k];}
            if(angle>1e-8&&angle<3.141592653589793-1e-6)for(int k=0;k<3;++k)direction[k]=(da[k]*std::sin((1-t)*angle)+db[k]*std::sin(t*angle))/std::sin(angle);
            const double angular=Length(direction)>1e-12?std::acos(std::clamp(dot(directions[i],Unit(direction)),-1.,1.)):3.141592653589793;
            const double score=std::max(Length(sub(samples[i].Position,position))/positionSeedMm,angular/directionSeedRad);if(score>largest){largest=score;split=i;}
        }
        // Closed native curves need an interior seed even when their diameter
        // is below the coarse seed tolerance; never collapse a complete loop.
        if(split==a&&(length>maximumSeedSpanMm||(length>1e-6&&Length(sub(samples[b].Position,samples[a].Position))<1e-6))){split=a+1;const double middle=(distance[a]+distance[b])*.5;for(std::size_t i=a+1;i<b;++i)if(std::abs(distance[i]-middle)<std::abs(distance[split]-middle))split=i;}
        if(split!=a){keep[split]=true;pending.emplace_back(a,split);pending.emplace_back(split,b);}
    }
    std::vector<std::size_t> result;for(std::size_t i=0;i<keep.size();++i)if(keep[i])result.push_back(i);return result;
}
}
