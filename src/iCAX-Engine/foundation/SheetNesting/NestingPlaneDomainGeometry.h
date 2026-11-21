#pragma once
#include "SheetNestingPlaneDomains.h"
#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>

namespace iCAX::SheetNesting::Detail::PlaneDomain
{
    inline Clipper2Lib::PathsD Material(const Shape& s,Pose pose={})
    {
        Clipper2Lib::PathsD paths;
        const auto append=[&](const std::vector<Point>& ring){Clipper2Lib::PathD p;for(auto v:ring){v=Transform(v,pose);p.emplace_back(v.X,v.Y);}paths.push_back(std::move(p));};
        append(s.Outer);for(const auto& h:s.Holes)append(h);
        return Clipper2Lib::Union(paths,Clipper2Lib::FillRule::EvenOdd,8);
    }
    inline Clipper2Lib::PathsD Allowed(const Shape& boundary,const std::vector<Shape>& obstacles,double margin)
    {
        auto paths=Material(boundary);
        for(const auto& obstacle:obstacles)paths=Clipper2Lib::Difference(paths,Material(obstacle),Clipper2Lib::FillRule::EvenOdd,8);
        if(margin>0)paths=Clipper2Lib::InflatePaths(paths,-margin,Clipper2Lib::JoinType::Miter,Clipper2Lib::EndType::Polygon,2.,8);
        return paths;
    }
    inline bool Inside(const Shape& shape,Pose pose,const Clipper2Lib::PathsD& domain,double tolerance)
    {
        if(domain.empty())return false;
        if(shape.Radius>0)
        {
            const auto center=Transform(shape.Center,pose);int parity=0;
            for(const auto& ring:domain)
            {
                // Analytic disk containment: center inside material and at
                // least Radius from every boundary segment (including holes).
                bool inside=false;
                for(std::size_t i=0;i<ring.size();++i)
                {
                    const auto a=ring[i],b=ring[(i+1)%ring.size()];
                    if((a.y>center.Y)!=(b.y>center.Y))if(center.X<(b.x-a.x)*(center.Y-a.y)/(b.y-a.y)+a.x)inside=!inside;
                    const double dx=b.x-a.x,dy=b.y-a.y,d2=dx*dx+dy*dy;
                    const double t=d2?std::clamp(((center.X-a.x)*dx+(center.Y-a.y)*dy)/d2,0.,1.):0.;
                    if(std::hypot(center.X-a.x-t*dx,center.Y-a.y-t*dy)+tolerance<shape.Radius)return false;
                }
                parity+=inside;
            }
            return parity%2==1;
        }
        auto outside=Clipper2Lib::Difference(Material(shape,pose),domain,Clipper2Lib::FillRule::EvenOdd,8);
        return std::abs(Clipper2Lib::Area(outside))<=tolerance*tolerance;
    }
    struct Slabs {std::vector<Shape> Blockers;std::vector<Rect> Inner;};
    inline Slabs Decompose(const Clipper2Lib::PathsD& allowed,double width,double height,double tolerance)
    {
        std::vector<double> xs{0,width};
        for(const auto& path:allowed)for(const auto& p:path)if(p.x>0&&p.x<width)xs.push_back(p.x);
        std::sort(xs.begin(),xs.end());xs.erase(std::unique(xs.begin(),xs.end(),[&](double a,double b){return std::abs(a-b)<=tolerance*.1;}),xs.end());
        Slabs result;
        struct Edge{double Mid,Left,Right;};
        for(std::size_t xi=1;xi<xs.size();++xi)
        {
            const double l=xs[xi-1],r=xs[xi],m=(l+r)/2;if(r-l<=tolerance)continue;
            std::vector<Edge> hits;
            for(const auto& path:allowed)for(std::size_t i=0;i<path.size();++i)
            {
                const auto a=path[i],b=path[(i+1)%path.size()];
                if((a.x<=m&&b.x>m)||(b.x<=m&&a.x>m))
                {const auto at=[&](double x){return a.y+(x-a.x)*(b.y-a.y)/(b.x-a.x);};hits.push_back({at(m),at(l),at(r)});}
            }
            std::sort(hits.begin(),hits.end(),[](auto a,auto b){return a.Mid<b.Mid;});
            Edge bottom{0,0,0};
            const auto blocker=[&](Edge low,Edge high)
            {
                if(high.Mid-low.Mid<=tolerance)return;
                Shape s;s.Outer={{l,low.Left},{r,low.Right},{r,high.Right},{l,high.Left}};
                s.Outer=MaterialRing(s.Outer);std::string error;if(ShapeValid(s,tolerance,error))result.Blockers.push_back(std::move(s));
            };
            for(std::size_t i=0;i+1<hits.size();i+=2)
            {
                blocker(bottom,hits[i]);
                const double lo=std::max(hits[i].Left,hits[i].Right),hi=std::min(hits[i+1].Left,hits[i+1].Right);
                if(hi-lo>tolerance)result.Inner.push_back({l,lo,r-l,hi-lo});
                bottom=hits[i+1];
            }
            blocker(bottom,{height,height,height});
        }
        return result;
    }
}
