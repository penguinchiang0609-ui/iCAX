#pragma once
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <vector>

namespace iCAX::SheetNesting::Detail
{
struct RectangleDirection
{
    Pose Offset;
    double Width=0,Height=0;
};
struct RectangleDirectionSet
{
    bool Supported=false,Interrupted=false;
    double Area=0;
    std::vector<RectangleDirection> Directions;
};

// Planning descriptions only: the original shape is never replaced. Bounding
// rectangles include every original vertex and final placements still Validate.
inline RectangleDirectionSet FindRectangleDirections(const Part& part,double tolerance,
    const std::function<bool()>& stop)
{
    RectangleDirectionSet output;
    const auto halted=[&]
    {
        if(output.Interrupted)return true;
        if(stop&&stop()){output.Interrupted=true;return true;}
        return false;
    };
    if(halted())return output;
    const auto& shape=part.Geometry;const auto& source=shape.Outer;
    if(shape.Radius>0||!shape.Holes.empty()||source.size()<4)return output;
    const auto length=[](Point a,Point b){return std::hypot(a.X-b.X,a.Y-b.Y);};
    const auto collinear=[&](Point a,Point b,Point c)
    {
        const double dx=c.X-a.X,dy=c.Y-a.Y,d2=dx*dx+dy*dy;
        const double t=d2?std::clamp(((b.X-a.X)*dx+(b.Y-a.Y)*dy)/d2,0.0,1.0):0;
        return length(b,{a.X+t*dx,a.Y+t*dy})<=tolerance;
    };
    std::vector<Point> features;features.reserve(source.size());
    auto end=source.size();if(length(source.front(),source.back())<=tolerance)--end;
    for(std::size_t i=0;i<end;++i)
    {
        if((i&63)==0&&halted())return output;
        const auto p=source[i];
        while(features.size()>1&&collinear(features[features.size()-2],features.back(),p))features.pop_back();
        features.push_back(p);
    }
    std::size_t first=0;bool changed=true;
    while(changed&&features.size()-first>3)
    {
        if(halted())return output;changed=false;
        if(collinear(features.back(),features[first],features[first+1])){++first;changed=true;}
        if(features.size()-first>3&&collinear(features[features.size()-2],features.back(),features[first])){features.pop_back();changed=true;}
    }
    if(first)features.erase(features.begin(),features.begin()+static_cast<std::ptrdiff_t>(first));
    if(features.size()!=4)return output;
    const auto origin=source.front();long double signedArea=0;
    for(std::size_t i=1;i+1<source.size();++i)
    {
        if((i&63)==0&&halted())return output;
        signedArea+=(static_cast<long double>(source[i].X)-origin.X)*(source[i+1].Y-origin.Y)
            -(static_cast<long double>(source[i].Y)-origin.Y)*(source[i+1].X-origin.X);
    }
    output.Area=static_cast<double>(std::abs(signedArea)/2);
    if(!std::isfinite(output.Area)||output.Area<=tolerance*tolerance)return output;
    std::vector<double> angles;std::set<double> seen;
    const auto addAngle=[&](double a)
    {
        if(!std::isfinite(a))return;
        a=NormalizeAngle(a);if(seen.insert(a).second)angles.push_back(a);
    };
    if(part.FreeRotation)
    {
        for(double a:{0.,90.,180.,270.})addAngle(a);
        const auto a=features[0],b=features[1];
        const double direction=-std::atan2(b.Y-a.Y,b.X-a.X)*180/Pi;
        for(int q=0;q<4;++q)addAngle(direction+90*q);
    }
    else if(part.AllowedAnglesDegrees.empty())addAngle(0);
    else for(std::size_t i=0;i<part.AllowedAnglesDegrees.size();++i)
    {
        if((i&63)==0&&halted())return output;
        addAngle(part.AllowedAnglesDegrees[i]);
    }
    for(auto angle:angles)
    {
        if(halted())return output;
        const Pose turn{0,0,angle};
        double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
        for(std::size_t i=0;i<source.size();++i)
        {
            if((i&63)==0&&halted())return output;
            const auto p=Transform(source[i],turn);x0=std::min(x0,p.X);y0=std::min(y0,p.Y);x1=std::max(x1,p.X);y1=std::max(y1,p.Y);
        }
        const double w=x1-x0,h=y1-y0;
        if(!std::isfinite(w)||!std::isfinite(h)||w<=tolerance||h<=tolerance)continue;
        if(std::abs(output.Area-w*h)>2*tolerance*(w+h))continue;
        bool axial=true;
        for(std::size_t i=0;i<source.size();++i)
        {
            if((i&63)==0&&halted())return output;
            const auto a=Transform(source[i],turn),b=Transform(source[(i+1)%source.size()],turn);
            const bool boundary=std::abs(a.X-x0)<=tolerance||std::abs(a.X-x1)<=tolerance||std::abs(a.Y-y0)<=tolerance||std::abs(a.Y-y1)<=tolerance;
            if(!boundary||std::min(std::abs(a.X-b.X),std::abs(a.Y-b.Y))>tolerance){axial=false;break;}
        }
        if(axial)output.Directions.push_back({{-x0,-y0,angle},w,h});
    }
    output.Supported=!output.Directions.empty();
    return output;
}
}
