#include "SheetNestingSeparationRepair.h"
#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    constexpr std::size_t MaxPlaced=64,MaxSpecs=128,MaxVertices=64,MaxTriangles=2048,
        MaxRows=12000,MaxSeeds=24,MaxAngles=16,MaxSweeps=100000,MaxIterations=80,MaxAttempts=2000000;
    struct V{double X=0,Y=0;};
    using Triangle=std::array<V,3>;
    struct Row{std::size_t I,J;double X,Y,B,Correction=0;};
    struct Piece{const Part* Original=nullptr;Placement PlacementValue;std::vector<Triangle> Local;};
    using PartMap=std::map<std::string,const Part*>;
    double Cross(V a,V b,V c){return (b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X);}
    double Length(V p){return std::hypot(p.X,p.Y);}
    std::vector<Triangle> Triangulate(const Part& part,double angle,double scale)
    {
        std::vector<V> points;points.reserve(part.Geometry.Outer.size());
        for(const auto p:part.Geometry.Outer){const auto q=Detail::Transform(p,{0,0,angle});points.push_back({q.X/scale,q.Y/scale});}
        if(points.size()<3)return {};
        if(points.size()>3&&Length({points.front().X-points.back().X,points.front().Y-points.back().Y})<1e-14)points.pop_back();
        std::vector<std::size_t> ids(points.size());std::iota(ids.begin(),ids.end(),0);
        double signedArea=0;for(std::size_t i=0;i<points.size();++i)
        {const auto a=points[i],b=points[(i+1)%points.size()];signedArea+=a.X*b.Y-a.Y*b.X;}
        if(std::abs(signedArea)<1e-18)return {};const double sign=signedArea>0?1.:-1.;
        std::vector<Triangle> result;
        while(ids.size()>3)
        {
            bool cut=false;
            for(std::size_t i=0;i<ids.size();++i)
            {
                const auto ia=ids[(i+ids.size()-1)%ids.size()],ib=ids[i],ic=ids[(i+1)%ids.size()];
                const auto a=points[ia],b=points[ib],c=points[ic];const double turn=sign*Cross(a,b,c);
                if(std::abs(turn)<=1e-16){ids.erase(ids.begin()+static_cast<std::ptrdiff_t>(i));cut=true;break;}
                if(turn<0)continue;bool contains=false;
                for(auto id:ids)if(id!=ia&&id!=ib&&id!=ic)
                {
                    const auto p=points[id];
                    if(sign*Cross(a,b,p)>=-1e-15&&sign*Cross(b,c,p)>=-1e-15&&sign*Cross(c,a,p)>=-1e-15)
                    {contains=true;break;}
                }
                if(contains)continue;result.push_back({a,b,c});ids.erase(ids.begin()+static_cast<std::ptrdiff_t>(i));cut=true;break;
            }
            if(!cut||ids.size()<3)return {};
        }
        result.push_back({points[ids[0]],points[ids[1]],points[ids[2]]});
        double sum=0;for(const auto& t:result)sum+=std::abs(Cross(t[0],t[1],t[2]));
        if(std::abs(sum-std::abs(signedArea))>1e-10*std::max(1.,std::abs(signedArea)))return {};
        return result;
    }
    Clipper2Lib::PathD WorldPath(const Part& part,Pose pose)
    {
        Clipper2Lib::PathD path;path.reserve(part.Geometry.Outer.size());
        for(auto p:part.Geometry.Outer){const auto q=Detail::Transform(p,pose);path.emplace_back(q.X,q.Y);}
        return path;
    }
    double IntersectArea(const Clipper2Lib::PathD& a,const Clipper2Lib::PathD& b)
    {
        try{return std::abs(Clipper2Lib::Area(Clipper2Lib::Intersect(Clipper2Lib::PathsD{a},Clipper2Lib::PathsD{b},Clipper2Lib::FillRule::EvenOdd,8)));}
        catch(...){return std::numeric_limits<double>::infinity();}
    }
    Triangle WorldTriangle(const Triangle& t,const Placement& pose,double scale)
    {
        Triangle out=t;for(auto& v:out){v.X+=pose.Transform.X/scale;v.Y+=pose.Transform.Y/scale;}return out;
    }
    struct Box{double X0,Y0,X1,Y1;};
    Box TriBounds(const Triangle& t)
    {
        Box b{t[0].X,t[0].Y,t[0].X,t[0].Y};
        for(auto p:t){b.X0=std::min(b.X0,p.X);b.Y0=std::min(b.Y0,p.Y);b.X1=std::max(b.X1,p.X);b.Y1=std::max(b.Y1,p.Y);}return b;
    }
    bool Near(Box a,Box b,double pad)
    {return !(a.X0>b.X1+pad||b.X0>a.X1+pad||a.Y0>b.Y1+pad||b.Y0>a.Y1+pad);}
    struct Axis{double Separation;V Normal;};
    std::optional<Axis> ChooseAxis(const Triangle& a,const Triangle& b,bool alternate)
    {
        std::vector<Axis> axes;
        for(const auto* t:{&a,&b})for(int k=0;k<3;++k)
        {
            const auto u=(*t)[k],v=(*t)[(k+1)%3];V n{-(v.Y-u.Y),v.X-u.X};const double size=Length(n);
            if(size<1e-14)continue;n.X/=size;n.Y/=size;
            for(int direction:{1,-1})
            {
                const V d{n.X*direction,n.Y*direction};double maximum=-std::numeric_limits<double>::infinity(),minimum=std::numeric_limits<double>::infinity();
                for(auto p:a)maximum=std::max(maximum,p.X*d.X+p.Y*d.Y);
                for(auto p:b)minimum=std::min(minimum,p.X*d.X+p.Y*d.Y);
                axes.push_back({minimum-maximum,d});
            }
        }
        std::stable_sort(axes.begin(),axes.end(),[](const auto& x,const auto& y){return x.Separation>y.Separation;});
        if(axes.empty())return std::nullopt;
        if(!alternate||axes.front().Separation>=-1e-12)return axes.front();
        const auto first=axes.front().Normal;
        for(const auto& axis:axes)if(Length({first.X-axis.Normal.X,first.Y-axis.Normal.Y})>=1e-9)return axis;
        return axes.front();
    }
    class TriangleSeparationRepair final:public IPostProcessor
    {
    public:
        Result Run(const Problem& problem,const Result& incumbent,const Options& options) override
        {
            const auto started=Clock::now();Result kept=incumbent;Statistics work;work.VerificationSeconds=work.EvaluationSeconds=0;
            bool cancelled=false;std::string reason="finite-candidates-exhausted";std::size_t seedCount=0;
            const auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-started).count();};
            const auto stop=[&]
            {
                if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;
                return cancelled||elapsed()>=options.TimeLimitSeconds||work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts);
            };
            const auto finish=[&]
            {
                if(cancelled)reason="cancelled";else if(elapsed()>=options.TimeLimitSeconds)reason="time-limit";
                else if(work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts))reason="attempt-limit";
                const auto origins=kept.Stats;kept.Stats=work;kept.Stats.CombinationGroups=origins.CombinationGroups;
                kept.Stats.CircleLatticeGroups=origins.CircleLatticeGroups;kept.Stats.GapFilledParts=origins.GapFilledParts;
                kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=elapsed();if(cancelled)kept.State=Status::Cancelled;
                kept.Diagnostics.push_back("repair.triangle-separation: "+reason+"; seeds="+std::to_string(seedCount));return kept;
            };
            if(stop())return finish();
            std::size_t missing=0;for(const auto& q:incumbent.Remaining)missing+=q.Quantity;
            if(incumbent.State!=Status::Partial||incumbent.Layouts.size()!=1||problem.PartGap!=0||missing!=1||
               incumbent.Layouts[0].Placements.size()>MaxPlaced||problem.Parts.size()>MaxSpecs)
            {reason="one-missing-single-sheet-zero-gap-scope";return finish();}
            PartMap parts;
            for(const auto& p:problem.Parts)
            {
                if(stop())return finish();
                if(p.Geometry.Radius>0||!p.Geometry.Holes.empty()||p.Geometry.Outer.size()>MaxVertices)
                {reason="polygon-scope";return finish();}parts.emplace(p.ID,&p);
            }
            const auto vtime=Clock::now();const auto checked=Validate(problem,incumbent);
            work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-vtime).count();
            if(!checked.Valid){reason="invalid-incumbent";return finish();}if(stop())return finish();
            const auto unmet=std::find_if(incumbent.Remaining.begin(),incumbent.Remaining.end(),[](const auto& item){return item.Quantity>0;});
            if(unmet==incumbent.Remaining.end()){reason="unknown-missing";return finish();}
            const auto found=parts.find(unmet->PartID);if(found==parts.end()){reason="unknown-missing";return finish();}
            const auto& missingPart=*found->second;const auto& layout=incumbent.Layouts[0];const double scale=std::max(layout.Width,layout.Height);
            const auto stock=std::find_if(problem.Stocks.begin(),problem.Stocks.end(),[&](const auto& item){return item.ID==layout.StockID;});
            if(stock==problem.Stocks.end()||stock->Material!=missingPart.Material){reason="material-scope";return finish();}
            std::set<std::size_t> used;for(const auto& p:layout.Placements)if(p.PartID==missingPart.ID)used.insert(p.Instance);
            std::size_t instance=0;while(instance<missingPart.Quantity&&used.count(instance))++instance;
            if(instance>=missingPart.Quantity){reason="missing-instance-scope";return finish();}
            struct Seed{Pose Position;double Score;};std::vector<Seed> seeds;
            auto angles=Detail::CandidateAngles(missingPart,problem.Tolerance);if(angles.size()>MaxAngles)angles.resize(MaxAngles);
            std::vector<Clipper2Lib::PathD> fixedPaths;for(const auto& p:layout.Placements)fixedPaths.push_back(WorldPath(*parts.at(p.PartID),p.Transform));
            for(double angle:angles)
            {
                if(stop())break;const auto b=Detail::Bounds(missingPart.Geometry,{0,0,angle});
                const double lx=problem.EdgeMargin-b.X,rx=layout.Width-problem.EdgeMargin-b.X-b.Width;
                const double ly=problem.EdgeMargin-b.Y,ry=layout.Height-problem.EdgeMargin-b.Y-b.Height;
                if(lx>rx||ly>ry)continue;
                for(int xi=0;xi<19&&!stop();++xi)for(int yi=0;yi<19&&!stop();++yi)
                {
                    ++work.PlacementAttempts;Pose pose{lx+(rx-lx)*xi/18.,ly+(ry-ly)*yi/18.,angle};
                    const auto path=WorldPath(missingPart,pose);double score=0;
                    for(const auto& other:fixedPaths){if(stop())break;score+=IntersectArea(path,other);}
                    if(!stop()&&std::isfinite(score))seeds.push_back({pose,score/std::max(Detail::Area(missingPart.Geometry),1e-20)});
                }
            }
            std::stable_sort(seeds.begin(),seeds.end(),[](const auto& a,const auto& b){return a.Score<b.Score;});
            std::vector<Seed> selected;
            for(const auto& seed:seeds)
            {
                bool diverse=true;for(const auto& old:selected)if(seed.Position.AngleDegrees==old.Position.AngleDegrees&&
                    std::hypot(seed.Position.X-old.Position.X,seed.Position.Y-old.Position.Y)<=.04*scale){diverse=false;break;}
                if(diverse)selected.push_back(seed);if(selected.size()>=MaxSeeds)break;
            }
            for(const auto& seed:selected)
            {
                if(stop())break;++seedCount;Result candidate=incumbent;auto& output=candidate.Layouts[0];
                output.Placements.push_back({missingPart.ID,instance,seed.Position,0,false});candidate.Remaining.clear();candidate.State=Status::Complete;
                std::vector<Piece> pieces;std::size_t ntri=0;
                for(const auto& placement:output.Placements)
                {
                    if(stop())break;const auto& part=*parts.at(placement.PartID);auto local=Triangulate(part,placement.Transform.AngleDegrees,scale);
                    if(local.empty()||local.size()>MaxTriangles-ntri){pieces.clear();break;}ntri+=local.size();pieces.push_back({&part,placement,std::move(local)});
                }
                if(pieces.size()!=output.Placements.size())continue;
                double last=std::numeric_limits<double>::infinity();std::size_t stale=0;const auto n=pieces.size();
                for(std::size_t iteration=0;iteration<MaxIterations&&!stop();++iteration)
                {
                    std::vector<Clipper2Lib::PathD> paths;for(const auto& p:pieces)paths.push_back(WorldPath(*p.Original,p.PlacementValue.Transform));
                    double energy=0;
                    for(std::size_t i=0;i<n&&!stop();++i)for(std::size_t j=0;j<i&&!stop();++j)energy+=IntersectArea(paths[i],paths[j]);
                    if(stop()||!std::isfinite(energy))break;energy/=scale*scale;
                    if(energy<=1e-15)
                    {
                        output.Placements.clear();output.MaterialArea=0;
                        for(const auto& p:pieces){output.Placements.push_back(p.PlacementValue);output.MaterialArea+=Detail::Area(p.Original->Geometry);}
                        candidate.GeometryValidated=false;const auto t=Clock::now();const auto finalCheck=Validate(problem,candidate);
                        work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-t).count();
                        if(finalCheck.Valid&&finalCheck.Complete&&!stop()){candidate.GeometryValidated=true;kept=std::move(candidate);reason="validated-complete";return finish();}
                    }
                    if(energy<1e-8)stale=0;else if(energy>=last-1e-13)++stale;else stale=0;
                    if(stale>=8)break;last=energy;
                    std::vector<std::vector<Triangle>> triangles;std::vector<Box> pieceBounds;
                    for(const auto& p:pieces)
                    {
                        std::vector<Triangle> world;for(const auto& t:p.Local)world.push_back(WorldTriangle(t,p.PlacementValue,scale));
                        triangles.push_back(std::move(world));const auto b=Detail::Bounds(p.Original->Geometry,p.PlacementValue.Transform);
                        pieceBounds.push_back({b.X/scale,b.Y/scale,(b.X+b.Width)/scale,(b.Y+b.Height)/scale});
                    }
                    std::vector<Row> rows;rows.reserve(MaxRows);
                    for(std::size_t i=0;i<n&&!stop()&&rows.size()<MaxRows;++i)for(std::size_t j=0;j<i&&!stop()&&rows.size()<MaxRows;++j)
                    {
                        if(!Near(pieceBounds[i],pieceBounds[j],.12))continue;
                        for(std::size_t ia=0;ia<triangles[i].size()&&!stop()&&rows.size()<MaxRows;++ia)
                            for(std::size_t ib=0;ib<triangles[j].size()&&!stop()&&rows.size()<MaxRows;++ib)
                            {
                                const auto& a=triangles[i][ia];const auto& b=triangles[j][ib];if(!Near(TriBounds(a),TriBounds(b),.12))continue;
                                const bool vary=stale&&(i*17+j*11+ia*5+ib+seedCount-1+stale)%(stale==1?5:3)==0;
                                const auto axis=ChooseAxis(a,b,vary);if(axis)rows.push_back({i,j,axis->Normal.X,axis->Normal.Y,axis->Separation-4*problem.Tolerance/scale});
                            }
                    }
                    if(rows.empty()||stop())break;
                    std::vector<double> low(2*n),high(2*n),x(2*n),boxCorrection(2*n),best=x;
                    for(std::size_t i=0;i<n;++i)
                    {
                        low[i]=problem.EdgeMargin/scale-pieceBounds[i].X0;high[i]=(layout.Width-problem.EdgeMargin)/scale-pieceBounds[i].X1;
                        low[n+i]=problem.EdgeMargin/scale-pieceBounds[i].Y0;high[n+i]=(layout.Height-problem.EdgeMargin)/scale-pieceBounds[i].Y1;
                        if(low[i]>high[i]||low[n+i]>high[n+i]){rows.clear();break;}
                    }
                    if(rows.empty())break;
                    const auto solveStarted=Clock::now();double bestScore=std::numeric_limits<double>::infinity();
                    for(std::size_t sweep=0;sweep<MaxSweeps;++sweep)
                    {
                        if(work.PlacementAttempts>=std::min(options.MaxPlacementAttempts,MaxAttempts)||
                           ((sweep&15)==0&&(stop()||std::chrono::duration<double>(Clock::now()-solveStarted).count()>=2)))break;
                        ++work.PlacementAttempts;
                        for(auto& r:rows)
                        {
                            const double norm=2*(r.X*r.X+r.Y*r.Y);if(norm<=0)continue;
                            const double violation=r.X*(x[r.I]-x[r.J])+r.Y*(x[n+r.I]-x[n+r.J])+r.Correction*norm-r.B;
                            const double next=std::max(0.,violation/norm),delta=r.Correction-next;
                            x[r.I]+=delta*r.X;x[r.J]-=delta*r.X;x[n+r.I]+=delta*r.Y;x[n+r.J]-=delta*r.Y;r.Correction=next;
                        }
                        for(std::size_t k=0;k<2*n;++k){const double y=x[k]+boxCorrection[k];x[k]=std::clamp(y,low[k],high[k]);boxCorrection[k]=y-x[k];}
                        double score=0,maximum=0;
                        for(const auto& r:rows){const double v=std::max(0.,r.X*(x[r.I]-x[r.J])+r.Y*(x[n+r.I]-x[n+r.J])-r.B);score+=v*v;maximum=std::max(maximum,v);}
                        if(score<bestScore){bestScore=score;best=x;}if(maximum<=1e-12)break;
                    }
                    if(stop())break;
                    for(std::size_t i=0;i<n;++i)
                    {pieces[i].PlacementValue.Transform.X+=best[i]*scale;pieces[i].PlacementValue.Transform.Y+=best[n+i]*scale;pieces[i].PlacementValue.Group=0;}
                }
            }
            return finish();
        }
    };
}
PostProcessStage MakeTriangleSeparationRepairStage()
{return {"repair.triangle-separation",[]{return std::make_unique<TriangleSeparationRepair>();}};}
}
