#pragma once
#include "SheetNestingConstraint.h"
#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/time_limit.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>


namespace iCAX::SheetNesting::ConstraintKernel
{
    namespace Sat = operations_research::sat;
    using Clock = std::chrono::steady_clock;
    using Integer = std::int64_t;
    inline bool ValidSolverSettings(const ConstraintCompletionSettings& settings)
    {
        return settings.CoordinateScale>=1&&settings.CoordinateScale<=1000000&&settings.Workers>=1&&settings.Workers<=64&&
            settings.MaxConflicts>=1&&settings.MaxInstances>=1&&settings.MaxInstances<=128&&settings.MaxHintCandidates<=65536;
    }
    struct IPoint
    {
        Integer X = 0, Y = 0;
        bool operator==(const IPoint&) const = default;
        bool operator<(const IPoint& b) const { return std::tie(X,Y)<std::tie(b.X,b.Y); }
    };
    using Ring = std::vector<IPoint>;
    struct Axis { Integer A, B, C; };
    struct Atom { Integer X,Y,Width,Height; };
    struct Direction
    {
        double Angle = 0;
        Rect OriginalBounds;
        Integer Width = 0, Height = 0;
        Ring Polygon;
        std::vector<Ring> Pieces;
        std::vector<Atom> Rectangles;
    };
    struct Type
    {
        std::size_t Original = 0;
        std::vector<Direction> Directions;
        std::array<int,4> QuarterToDirection{-1,-1,-1,-1};
        // Equivalent normalized material may share a Direction, while the
        // original source origins at those actual quarters remain different.
        std::array<Rect,4> QuarterBounds;
    };
    struct Hint { int Direction = -1; Integer X = 0, Y = 0; };
    struct Item
    {
        std::size_t TypeIndex = 0, Instance = 0;
        Sat::IntVar X,Y;
        std::vector<Sat::BoolVar> Present;
    };
    struct Cell
    {
        Ring Polygon;
        std::vector<Axis> Axes;
        IPoint Low,High;
    };
    struct CellHint
    {
        std::size_t I,J,AI,AJ;
        const Cell* Geometry;
        std::vector<Sat::BoolVar> Alternatives;
    };
    struct Segment { Point A,B;std::size_t Owner; };

    inline Integer Cross(IPoint a,IPoint b,IPoint c)
    { return (b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X); }
    inline Integer Area2(const Ring& ring)
    {
        Integer value=0;
        for(std::size_t i=0;i<ring.size();++i)
        {const auto a=ring[i],b=ring[(i+1)%ring.size()];value+=a.X*b.Y-a.Y*b.X;}
        return value;
    }
    inline Ring Hull(Ring points)
    {
        std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
        if(points.size()<3)return points;
        Ring result;
        for(auto p:points){while(result.size()>1&&Cross(result[result.size()-2],result.back(),p)<=0)result.pop_back();result.push_back(p);}
        const auto lower=result.size();
        for(auto it=points.rbegin()+1;it!=points.rend();++it)
        {while(result.size()>lower&&Cross(result[result.size()-2],result.back(),*it)<=0)result.pop_back();result.push_back(*it);}
        result.pop_back();return result;
    }
    inline Ring CanonicalRing(Ring ring)
    {
        if(ring.size()>1&&ring.front()==ring.back())ring.pop_back();
        if(Area2(ring)<0)std::reverse(ring.begin(),ring.end());
        const auto first=std::min_element(ring.begin(),ring.end());std::rotate(ring.begin(),first,ring.end());
        return ring;
    }
    // Linear convex Minkowski edge merge, in the same angular ordering used by
    // the shared NFP kernel. Integer input retains exact lattice predicates.
    inline Ring ConvexDifference(Ring a,Ring b)
    {
        for(auto& p:b){p.X=-p.X;p.Y=-p.Y;}
        const auto start=[](Ring& ring)
        {const auto it=std::min_element(ring.begin(),ring.end(),[](auto x,auto y){return std::tie(x.Y,x.X)<std::tie(y.Y,y.X);});std::rotate(ring.begin(),it,ring.end());};
        start(a);start(b);Ring out;out.reserve(a.size()+b.size());std::size_t i=0,j=0;
        IPoint p{a[0].X+b[0].X,a[0].Y+b[0].Y};
        const auto half=[](IPoint v){return v.Y<0||(v.Y==0&&v.X<0);};
        while(i<a.size()||j<b.size())
        {
            out.push_back(p);IPoint da{},db{};bool takeA=false,takeB=false;
            if(i<a.size())da={a[(i+1)%a.size()].X-a[i].X,a[(i+1)%a.size()].Y-a[i].Y};
            if(j<b.size())db={b[(j+1)%b.size()].X-b[j].X,b[(j+1)%b.size()].Y-b[j].Y};
            if(i==a.size())takeB=true;
            else if(j==b.size())takeA=true;
            else if(half(da)!=half(db)){takeA=half(da)<half(db);takeB=!takeA;}
            else{const Integer cross=da.X*db.Y-da.Y*db.X;takeA=cross>=0;takeB=cross<=0;}
            if(takeA){p.X+=da.X;p.Y+=da.Y;++i;}
            if(takeB){p.X+=db.X;p.Y+=db.Y;++j;}
        }
        return Hull(std::move(out));
    }
    inline Cell MakeCell(const Ring& a,const Ring& b)
    {
        Cell out;out.Polygon=ConvexDifference(a,b);
        out.Low=out.High=out.Polygon.front();
        for(const auto p:out.Polygon)
        {out.Low.X=std::min(out.Low.X,p.X);out.Low.Y=std::min(out.Low.Y,p.Y);
            out.High.X=std::max(out.High.X,p.X);out.High.Y=std::max(out.High.Y,p.Y);}
        for(std::size_t i=0;i<out.Polygon.size();++i)
        {
            const auto a0=out.Polygon[i],a1=out.Polygon[(i+1)%out.Polygon.size()];
            const Integer dx=a1.X-a0.X,dy=a1.Y-a0.Y;
            Axis edge{-dy,dx,dx*a0.Y-dy*a0.X};
            const auto divisor=std::gcd(std::gcd(std::abs(edge.A),std::abs(edge.B)),std::abs(edge.C));
            if(divisor){edge.A/=divisor;edge.B/=divisor;edge.C/=divisor;out.Axes.push_back(edge);}
        }
        return out;
    }
    inline int Quarter(double angle)
    {
        const double normalized=Detail::NormalizeAngle(angle),turn=std::round(normalized/90.);
        if(std::abs(normalized-turn*90.)>32*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(normalized)))return -1;
        return static_cast<int>(turn)%4;
    }
    inline Point Rotate(Point p,int q)
    {
        switch(q){case 0:return p;case 1:return {-p.Y,p.X};case 2:return {-p.X,-p.Y};default:return {p.Y,-p.X};}
    }
    inline bool Lossless(double value,Integer scale,Integer& output)
    {
        const long double scaled=static_cast<long double>(value)*scale;
        if(!std::isfinite(value)||std::abs(scaled)>10000000.L)return false;
        const long double rounded=std::round(scaled);
        if(std::abs(scaled-rounded)>32*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(scaled)))return false;
        output=static_cast<Integer>(rounded);return true;
    }
    inline bool Inside(const Ring& ring,IPoint p)
    {
        for(std::size_t i=0;i<ring.size();++i)if(Cross(ring[i],ring[(i+1)%ring.size()],p)<0)return false;
        return true;
    }
    inline bool ContainsCell(const Cell& outer,const Cell& inner)
    {
        if(outer.Low.X>inner.Low.X||outer.Low.Y>inner.Low.Y||outer.High.X<inner.High.X||outer.High.Y<inner.High.Y)return false;
        // Both operands are convex integer polygons: containing every vertex
        // certifies containment of the complete forbidden cell without epsilon.
        return std::all_of(inner.Polygon.begin(),inner.Polygon.end(),[&](IPoint p){return Inside(outer.Polygon,p);});
    }
    inline std::vector<Atom> Inscribed(const Direction& d,Integer unit,const std::function<bool()>& stop)
    {
        std::map<std::pair<Integer,Integer>,std::pair<Integer,Integer>> active;
        std::vector<Atom> output;
        for(Integer y=0;y+unit<=d.Height;y+=unit)
        {
            if(stop())return {};
            std::vector<std::pair<Integer,Integer>> spans;
            for(Integer x=0;x+unit<=d.Width;x+=unit)
            {
                if((x/unit%128)==0&&stop())return {};
                if(!Inside(d.Polygon,{x,y})||!Inside(d.Polygon,{x+unit,y})||
                   !Inside(d.Polygon,{x,y+unit})||!Inside(d.Polygon,{x+unit,y+unit}))continue;
                if(!spans.empty()&&spans.back().second==x)spans.back().second=x+unit;
                else spans.emplace_back(x,x+unit);
            }
            std::set<std::pair<Integer,Integer>> current(spans.begin(),spans.end());
            for(auto it=active.begin();it!=active.end();)
                if(!current.contains(it->first))
                {output.push_back({it->first.first,it->second.first,it->first.second-it->first.first,it->second.second-it->second.first});it=active.erase(it);}
                else ++it;
            for(auto span:spans)
            {auto [it,inserted]=active.try_emplace(span,std::pair(y,y+unit));if(!inserted)it->second.second=y+unit;}
        }
        for(const auto& [span,range]:active)output.push_back({span.first,range.first,span.second-span.first,range.second-range.first});
        return output;
    }
    inline double IntersectionArea(std::vector<Point> subject,const std::vector<Point>& clip)
    {
        const auto cross=[](Point a,Point b,Point p){return (b.X-a.X)*(p.Y-a.Y)-(b.Y-a.Y)*(p.X-a.X);};
        for(std::size_t i=0;i<clip.size()&&!subject.empty();++i)
        {
            const auto a=clip[i],b=clip[(i+1)%clip.size()];std::vector<Point> next;
            auto previous=subject.back();double old=cross(a,b,previous);
            for(auto current:subject)
            {
                const double now=cross(a,b,current);
                if((old>=0)!=(now>=0))
                {const double t=old/(old-now);next.push_back({previous.X+t*(current.X-previous.X),previous.Y+t*(current.Y-previous.Y)});}
                if(now>=0)next.push_back(current);previous=current;old=now;
            }
            subject=std::move(next);
        }
        double area=0;for(std::size_t i=0;i<subject.size();++i)
        {const auto a=subject[i],b=subject[(i+1)%subject.size()];area+=a.X*b.Y-a.Y*b.X;}
        return std::abs(area)*.5;
    }

    inline bool PrepareType(const Part& part,std::size_t pi,Integer scale,bool inscribed,NfpCache& geometryCache,
        const std::function<bool()>& stop,const std::function<double()>& remaining,Type& type,
        std::string& diagnostic,std::size_t& preparedPieces,std::size_t& rectangleAtoms)
    {
        NfpBuildOptions decompositionBudget;decompositionBudget.Cancel=stop;
        decompositionBudget.TimeLimitSeconds=std::max(0.,remaining());
        const auto decomposition=geometryCache.Decompose(part.Geometry,decompositionBudget);
        if(decomposition.State!=NfpStatus::Ready||!decomposition.Decomposition)
            {diagnostic="shared convex decomposition unavailable or budget stopped";return false;}
        type.Original=pi;std::map<Ring,int> orientations;
        auto angles=part.AllowedAnglesDegrees;if(angles.empty())angles.push_back(0);
        for(double angle:angles)
        {
            if(stop()){diagnostic="budget during direction preparation";return false;}
            const int quarter=Quarter(angle);if(quarter<0){diagnostic="unsupported non-quarter angle";return false;}
            std::vector<Point> rotated;for(auto q:part.Geometry.Outer)rotated.push_back(Rotate(q,quarter));
            double lx=rotated[0].X,ly=rotated[0].Y,rx=lx,ty=ly;
            for(auto q:rotated){lx=std::min(lx,q.X);ly=std::min(ly,q.Y);rx=std::max(rx,q.X);ty=std::max(ty,q.Y);}
            Direction direction;direction.Angle=quarter*90.;direction.OriginalBounds={lx,ly,rx-lx,ty-ly};
            type.QuarterBounds[quarter]=direction.OriginalBounds;
            if(!Lossless(rx-lx,scale,direction.Width)||!Lossless(ty-ly,scale,direction.Height)){diagnostic="source dimensions are not lossless at this scale";return false;}
            Ring ring;
            for(auto q:rotated)
            {IPoint point;if(!Lossless(q.X-lx,scale,point.X)||!Lossless(q.Y-ly,scale,point.Y)){diagnostic="source vertices are not lossless at this scale";return false;}ring.push_back(point);}
            if(ring.size()>1&&ring.front()==ring.back())ring.pop_back();
            direction.Polygon=CanonicalRing(ring);
            Integer pieceArea=0;
            for(const auto& sourcePiece:decomposition.Decomposition->Pieces)
            {
                if(stop()){diagnostic="budget during convex-piece conversion";return false;}
                Ring piece;
                for(const auto point:sourcePiece)
                {
                    // The shared ear-clipping decomposition uses only
                    // original vertices. Map them back to the already
                    // losslessly converted source ring, avoiding any
                    // add/subtract-origin rounding in solver geometry.
                    std::size_t source=0;
                    while(source<part.Geometry.Outer.size()&&
                        (part.Geometry.Outer[source].X-decomposition.SourceOrigin.X!=point.X||
                         part.Geometry.Outer[source].Y-decomposition.SourceOrigin.Y!=point.Y))++source;
                    if(source==part.Geometry.Outer.size()){diagnostic="decomposition vertex is not an original source vertex";return false;}
                    IPoint converted;
                    if(!Lossless(rotated[source].X-lx,scale,converted.X)||!Lossless(rotated[source].Y-ly,scale,converted.Y))
                        {diagnostic="decomposition vertices are not lossless at this scale";return false;}
                    piece.push_back(converted);
                }
                piece=CanonicalRing(std::move(piece));
                if(piece.size()<3||Area2(piece)<=0||Area2(Hull(piece))!=Area2(piece)){diagnostic="invalid shared convex decomposition";return false;}
                pieceArea+=Area2(piece);direction.Pieces.push_back(std::move(piece));
            }
            if(pieceArea!=Area2(direction.Polygon)){diagnostic="decomposition changed original material area on lattice";return false;}
            if(const auto it=orientations.find(direction.Polygon);it!=orientations.end())type.QuarterToDirection[quarter]=it->second;
            else
            {
                const int index=static_cast<int>(type.Directions.size());type.QuarterToDirection[quarter]=index;
                orientations.emplace(direction.Polygon,index);
                if(inscribed)
                {
                    // Unit source cells are merged into disjoint interior
                    // rectangles. Limit raster work without changing safety.
                    Integer unit=scale;
                    while((direction.Width/unit+1)*(direction.Height/unit+1)>10000)unit*=2;
                    // Convex pieces have disjoint interiors. A rectangle
                    // whose four corners lie in one convex piece is
                    // entirely in that piece, so atoms extracted from
                    // different pieces cannot overlap in positive area.
                    for(const auto& piece:direction.Pieces)
                    {
                        if(stop()){diagnostic="budget during interior rectangle preparation";return false;}
                        Direction convex;convex.Width=direction.Width;convex.Height=direction.Height;convex.Polygon=piece;
                        auto atoms=Inscribed(convex,unit,stop);
                        direction.Rectangles.insert(direction.Rectangles.end(),atoms.begin(),atoms.end());
                    }
                }
                preparedPieces+=direction.Pieces.size();rectangleAtoms+=direction.Rectangles.size();
                type.Directions.push_back(std::move(direction));
            }
        }
        return true;
    }

    inline std::vector<Cell> PairCells(const Direction& a,const Direction& b,const std::function<bool()>& stop,std::size_t& built)
    {
        std::vector<Cell> cells;
        for(const auto& fixed:a.Pieces)for(const auto& moving:b.Pieces)
        {
            if(stop())return cells;auto cell=MakeCell(fixed,moving);++built;bool redundant=false;
            for(auto it=cells.begin();it!=cells.end();)
            {
                if(stop())return cells;
                if(ContainsCell(*it,cell)){redundant=true;break;}
                if(ContainsCell(cell,*it))it=cells.erase(it);else ++it;
            }
            if(!redundant)cells.push_back(std::move(cell));
        }
        return cells;
    }

    inline Sat::CpSolverResponse SolveBounded(const Sat::CpModelProto& proto,
        const ConstraintCompletionSettings& settings,const Options& options,double seconds,
        const std::function<bool()>& pollCancel,const std::function<bool()>& deadline,bool firstSolution=true)
    {
        Sat::SatParameters parameters;parameters.set_max_time_in_seconds(seconds);
        parameters.set_num_search_workers(settings.Workers);parameters.set_max_number_of_conflicts(settings.MaxConflicts);
        parameters.set_random_seed(static_cast<int>(options.Seed&0x7fffffff));parameters.set_stop_after_first_solution(firstSolution);
        // The nested repair solve in this pinned version can outlive its parent
        // deadline. Ordinary hints remain available, with no nested repair.
        parameters.set_repair_hint(false);
        if(settings.CompleteSeparationHints)parameters.set_hint_conflict_limit(std::min<std::int64_t>(100000,settings.MaxConflicts));
        parameters.set_log_search_progress(false);parameters.set_use_timetabling_in_no_overlap_2d(true);
        parameters.set_use_energetic_reasoning_in_no_overlap_2d(true);parameters.set_use_area_energetic_reasoning_in_no_overlap_2d(true);
        Sat::Model solver;solver.Add(Sat::NewSatParameters(parameters));std::atomic<bool> externalStop{false};std::exception_ptr cancelFailure;
        solver.GetOrCreate<operations_research::TimeLimit>()->RegisterExternalBooleanAsLimit(&externalStop);
        std::jthread monitor([&](std::stop_token token)
        {
            try
            {
                while(!token.stop_requested())
                {if(pollCancel()||deadline()){externalStop.store(true);break;}
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            }
            catch(...){cancelFailure=std::current_exception();externalStop.store(true);}
        });
        auto response=Sat::SolveCpModel(proto,&solver);monitor.request_stop();monitor.join();
        if(cancelFailure)std::rethrow_exception(cancelFailure);return response;
    }
}
