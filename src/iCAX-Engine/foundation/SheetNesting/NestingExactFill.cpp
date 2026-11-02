#include "SheetNestingExactFill.h"
#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using namespace Detail;
    using namespace Clipper2Lib;
    struct Edge {Point A,B;};
    struct Choice {std::size_t Part;Pose Placement;double Contact,Area;};
    PathD WorldPath(const Shape& shape,Pose pose)
    {
        PathD path;path.reserve(shape.Outer.size());
        for(auto point:shape.Outer){auto p=Transform(point,pose);path.emplace_back(p.X,p.Y);}
        if(!IsPositive(path))std::reverse(path.begin(),path.end());
        return path;
    }
    double SharedLength(Point a,Point b,Point c,Point d,double tolerance)
    {
        const double dx=b.X-a.X,dy=b.Y-a.Y,len=std::hypot(dx,dy);
        if(len<=tolerance)return 0;
        if(std::abs(dx*(c.Y-a.Y)-dy*(c.X-a.X))>tolerance*len||
           std::abs(dx*(d.Y-a.Y)-dy*(d.X-a.X))>tolerance*len)return 0;
        const double c0=((c.X-a.X)*dx+(c.Y-a.Y)*dy)/len;
        const double c1=((d.X-a.X)*dx+(d.Y-a.Y)*dy)/len;
        return std::max(0.0,std::min(len,std::max(c0,c1))-std::max(0.0,std::min(c0,c1)));
    }
    class ExactFillContact final:public IStrategy
    {
    public:
        Result Run(const Problem& p,const Options& o) override
        {
            Result out;out.State=Status::Partial;
            for(const auto& part:p.Parts)if(part.Quantity)out.Remaining.push_back({part.ID,part.Quantity});
            const auto start=std::chrono::steady_clock::now();bool cancelled=false,exhausted=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
            const auto stop=[&]
            {
                if(cancelled||exhausted)return true;
                if(o.Cancel&&o.Cancel()){cancelled=true;return true;}
                if(elapsed()>=o.TimeLimitSeconds||out.Stats.PlacementAttempts>=o.MaxPlacementAttempts){exhausted=true;return true;}
                return false;
            };
            const auto done=[&]{if(cancelled)out.State=Status::Cancelled;out.Stats.ElapsedSeconds=elapsed();out.Stats.PlacementSeconds=out.Stats.ElapsedSeconds;return out;};
            if(stop())return done();
            if(!CheckProblem(p).empty()||p.PartGap!=0||p.Stocks.size()!=1||!p.Stocks[0].Quantity)return done();
            const auto& stock=p.Stocks[0];const double margin=p.EdgeMargin;
            std::size_t demand=0;double area=0;
            for(const auto& part:p.Parts)
            {
                if(part.Quantity&&(part.Material!=stock.Material||part.FreeRotation||part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Geometry.Outer.size()>32))return done();
                demand+=part.Quantity;area+=part.Quantity*Area(part.Geometry);
            }
            const double usable=(stock.Width-2*margin)*(stock.Height-2*margin);
            if(demand==0||demand>20||std::abs(area-usable)>std::max(1e-9,usable*1e-12))return done();
            const PathsD board{{{margin,margin},{stock.Width-margin,margin},{stock.Width-margin,stock.Height-margin},{margin,stock.Height-margin}}};
            std::vector<std::size_t> used(p.Parts.size());std::vector<std::pair<std::size_t,Pose>> placed;
            const auto search=[&](auto&& self)->bool
            {
                if(stop())return false;
                if(placed.size()==demand)
                {
                    Result candidate;candidate.State=Status::Complete;
                    Layout layout{stock.ID,0,stock.Width,stock.Height,{},area};
                    std::vector<std::size_t> instances(p.Parts.size());
                    for(const auto& [i,pose]:placed)layout.Placements.push_back({p.Parts[i].ID,instances[i]++,pose,0,false});
                    candidate.Layouts.push_back(std::move(layout));
                    const auto check=Validate(p,candidate);
                    if(check.Valid&&check.Complete){candidate.Stats=out.Stats;out=std::move(candidate);return true;}
                    return false;
                }
                PathsD occupied;occupied.reserve(placed.size());
                for(const auto& [i,pose]:placed)occupied.push_back(WorldPath(p.Parts[i].Geometry,pose));
                const auto remaining=occupied.empty()?board:Difference(board,occupied,FillRule::NonZero,8);
                bool found=false;Point target{};
                std::vector<Edge> edges;
                for(const auto& path:remaining)
                {
                    if(path.size()<3)continue;
                    for(std::size_t k=0;k<path.size();++k)
                    {
                        Point a{path[k].x,path[k].y},b{path[(k+1)%path.size()].x,path[(k+1)%path.size()].y};
                        edges.push_back({a,b});
                        if(!found||std::tie(a.Y,a.X)<std::tie(target.Y,target.X)){target=a;found=true;}
                    }
                }
                if(!found)return false;
                std::vector<Choice> choices;
                for(std::size_t i=0;i<p.Parts.size()&&!stop();++i)
                {
                    const auto& part=p.Parts[i];if(used[i]>=part.Quantity)continue;
                    const auto angles=CandidateAngles(part,p.Tolerance);if(angles.size()>16)continue;
                    for(const auto angle:angles)for(auto v:part.Geometry.Outer)
                    {
                        if(stop())return false;
                        const auto local=Transform(v,{0,0,angle});Pose pose{target.X-local.X,target.Y-local.Y,angle};
                        ++out.Stats.PlacementAttempts;
                        if(!InsideSheet(part.Geometry,pose,stock.Width,stock.Height,margin,p.Tolerance))continue;
                        bool conflict=false;
                        for(const auto& [j,q]:placed)if(ShapesConflict(part.Geometry,pose,p.Parts[j].Geometry,q,0,p.Tolerance)){conflict=true;break;}
                        if(conflict)continue;
                        const auto path=WorldPath(part.Geometry,pose);double contact=0;
                        for(std::size_t k=0;k<path.size();++k)
                        {
                            const Point a{path[k].x,path[k].y},b{path[(k+1)%path.size()].x,path[(k+1)%path.size()].y};
                            for(const auto& edge:edges)contact+=SharedLength(a,b,edge.A,edge.B,p.Tolerance);
                        }
                        choices.push_back({i,pose,contact,Area(part.Geometry)});
                    }
                }
                std::stable_sort(choices.begin(),choices.end(),[](const Choice& a,const Choice& b)
                {
                    if(a.Contact!=b.Contact)return a.Contact>b.Contact;
                    if(a.Area!=b.Area)return a.Area>b.Area;
                    return std::tie(a.Part,a.Placement.X,a.Placement.Y,a.Placement.AngleDegrees)<std::tie(b.Part,b.Placement.X,b.Placement.Y,b.Placement.AngleDegrees);
                });
                for(const auto& c:choices)
                {
                    if(stop())return false;
                    ++used[c.Part];placed.push_back({c.Part,c.Placement});
                    if(self(self))return true;
                    placed.pop_back();--used[c.Part];
                }
                return false;
            };
            search(search);
            if(out.State!=Status::Complete)out.Diagnostics.push_back("Bounded vertex-contact search did not find a complete layout; no infeasibility proof.");
            return done();
        }
    };
}
StrategyFactory MakeExactFillContactFactory(){return []{return std::make_unique<ExactFillContact>();};}
}
