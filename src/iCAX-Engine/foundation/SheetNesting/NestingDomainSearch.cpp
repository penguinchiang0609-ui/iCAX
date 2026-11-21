#include "SheetNestingDomainSearch.h"
#include "SheetNestingNfp.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace iCAX::SheetNesting {
namespace {
using namespace Detail;
using namespace Clipper2Lib;
struct Segment { Point A,B; std::size_t Owner; bool Contact=false; };
struct Candidate {std::size_t Index; Pose P; double Score;std::vector<std::size_t> Blockers;};
struct Choice {std::size_t Index; double FreeArea=0, Area=0, Tall=0;std::vector<Candidate> Fits;};
bool Near(Rect a,Rect b,double clearance)
{
    return a.X+a.Width+clearance>=b.X&&b.X+b.Width+clearance>=a.X&&
        a.Y+a.Height+clearance>=b.Y&&b.Y+b.Height+clearance>=a.Y;
}
std::vector<double> DistinctAngles(const Part& part,double tolerance,const std::function<bool()>& stop)
{
    const auto angles=CandidateAngles(part,tolerance);std::vector<double> output;
    std::set<std::vector<std::pair<long long,long long>>> seen;
    for(const auto angle:angles)
    {
        if(stop())break;
        const auto bounds=Bounds(part.Geometry,{0,0,angle});
        if(bounds.Width>1e8||bounds.Height>1e8){output.push_back(angle);continue;}
        const double unit=std::max(1e-10,std::min(1e-8,tolerance*.01));
        std::vector<std::pair<long long,long long>> ring;
        for(const auto point:part.Geometry.Outer){const auto q=Transform(point,{0,0,angle});
            ring.emplace_back(std::llround((q.X-bounds.X)/unit),std::llround((q.Y-bounds.Y)/unit));}
        if(ring.empty()){output.push_back(angle);continue;}
        auto canonical=ring;
        for(std::size_t i=1;i<ring.size();++i)
        {
            if(stop())break;
            auto rotated=ring;std::rotate(rotated.begin(),rotated.begin()+i,rotated.end());
            if(rotated<canonical)canonical=std::move(rotated);
        }
        if(seen.insert(std::move(canonical)).second)output.push_back(angle);
    }
    return output;
}
class DomainSearch final : public IStrategy {
    DomainOrderPolicy Policy;
    const Problem* P=nullptr; const Options* O=nullptr; std::chrono::steady_clock::time_point Start;
    NfpCache Cache; std::vector<NfpPreparedShape> Prepared;std::vector<std::vector<double>> Angles;
    std::vector<std::size_t> Used, BestUsed;Layout Board,Best;std::size_t Attempts=0,Nodes=0,Queries=0,Intersections=0,DegenerateDomains=0,RootBranches=0;bool Cancelled=false,QueryTruncated=false;
    double ActiveDeadline=std::numeric_limits<double>::infinity();
    std::size_t ActiveAttemptLimit=std::numeric_limits<std::size_t>::max();
    double elapsed() const {return std::chrono::duration<double>(std::chrono::steady_clock::now()-Start).count();}
    bool lifeStop()
    {
        if(Cancelled)return true;
        if(O->Cancel&&O->Cancel()){Cancelled=true;return true;}
        const double seconds=elapsed();
        return !std::isfinite(O->TimeLimitSeconds)||O->TimeLimitSeconds<=0||seconds>=O->TimeLimitSeconds||
            seconds>=ActiveDeadline;
    }
    bool stop(){return lifeStop()||Attempts>=O->MaxPlacementAttempts||Attempts>=ActiveAttemptLimit;}
    std::vector<Candidate> query(std::size_t index,double angle,double& area,std::size_t maxBlockers=0) {
        std::vector<Candidate> fits;const auto& p=*P;const auto& stock=p.Stocks[0];const auto& part=p.Parts[index];
        if(stop())return fits;
        const auto b=Bounds(part.Geometry,{0,0,angle});const double lx=p.EdgeMargin-b.X,ly=p.EdgeMargin-b.Y;
        const double rx=stock.Width-p.EdgeMargin-b.X-b.Width,ty=stock.Height-p.EdgeMargin-b.Y-b.Height;
        if(rx<lx-p.Tolerance||ty<ly-p.Tolerance)return fits;
        std::vector<Point> origins;std::set<std::pair<long long,long long>> seen;
        const double scale=std::max(1e-8,p.Tolerance*.25);
        const auto emit=[&](Point q) {
            if(!std::isfinite(q.X)||!std::isfinite(q.Y)||q.X<lx-p.Tolerance||q.X>rx+p.Tolerance||q.Y<ly-p.Tolerance||q.Y>ty+p.Tolerance)return;
            const auto key=std::pair(std::llround(q.X/scale),std::llround(q.Y/scale));if(seen.insert(key).second)origins.push_back(q);
        };
        emit({lx,ly});emit({rx,ly});emit({lx,ty});emit({rx,ty});
        std::vector<Segment> edges;
        auto addEdge=[&](Point a,Point c,std::size_t owner,bool contact=false){if(edges.size()<2048&&std::hypot(c.X-a.X,c.Y-a.Y)>1e-14)edges.push_back({a,c,owner,contact});};
        addEdge({lx,ly},{rx,ly},0);addEdge({rx,ly},{rx,ty},0);addEdge({rx,ty},{lx,ty},0);addEdge({lx,ty},{lx,ly},0);
        PathsD forb;
        struct Obstacle {std::size_t PartIndex;Pose Transform;Rect Box;AxisSafety::Geometry Material;};
        std::vector<Obstacle> obstacles;
        double obstacleRoundoff=0;
        for(std::size_t placed=0;placed<Board.Placements.size()&&!stop();++placed){
            const auto& x=Board.Placements[placed];auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& y){return y.ID==x.PartID;});
            if(it==p.Parts.end()){QueryTruncated=true;return {};}
            const auto si=static_cast<std::size_t>(it-p.Parts.begin());NfpBuildOptions options;options.TimeLimitSeconds=std::max(0.,O->TimeLimitSeconds-elapsed());options.Cancel=[&]{return stop();};
            const auto nfp=Cache.Get(Prepared[si],x.Transform.AngleDegrees,Prepared[index],angle,options);
            if(nfp.State!=NfpStatus::Ready||!nfp.Region){QueryTruncated=true;return {};}
            auto material=AxisSafety::Make(it->Geometry,x.Transform);
            obstacleRoundoff=std::max(obstacleRoundoff,material.Roundoff);
            obstacles.push_back({si,x.Transform,Bounds(it->Geometry,x.Transform),std::move(material)});
            for(const auto& loop:nfp.Region->Loops){std::vector<Point> path;path.reserve(loop.size());
                for(const auto v:loop){auto q=Transform(v,nfp.Transform);q.X+=x.Transform.X;q.Y+=x.Transform.Y;path.push_back(q);emit(q);}
                auto& pathD=forb.emplace_back();for(auto q:path)pathD.emplace_back(q.X,q.Y);
                for(std::size_t j=0;j<path.size();++j)addEdge(path[j],path[(j+1)%path.size()],placed+1);
            }
            // A regularized forbidden-region union may swallow a touch-only
            // zero-width channel. Retain real parallel-edge contact loci even
            // when they lie inside that union; all candidates are verified
            // against original part geometry below.
            const auto& stationary=it->Geometry.Outer;const auto& moving=part.Geometry.Outer;
            for(std::size_t ai=0;ai<stationary.size()&&!stop()&&edges.size()<2048;++ai)
            {
                const auto a=Transform(stationary[ai],x.Transform),b2=Transform(stationary[(ai+1)%stationary.size()],x.Transform);
                const double ax=b2.X-a.X,ay=b2.Y-a.Y,al=std::hypot(ax,ay);if(al==0)continue;
                for(std::size_t bi=0;bi<moving.size()&&!stop()&&edges.size()<2048;++bi)
                {
                    const auto c=Transform(moving[bi],{0,0,angle}),d=Transform(moving[(bi+1)%moving.size()],{0,0,angle});
                    const double bx=d.X-c.X,by=d.Y-c.Y,bl=std::hypot(bx,by);if(bl==0||std::abs(ax*by-ay*bx)>1e-10*al*bl)continue;
                    const Point endpoints[]={{a.X-c.X,a.Y-c.Y},{a.X-d.X,a.Y-d.Y},{b2.X-c.X,b2.Y-c.Y},{b2.X-d.X,b2.Y-d.Y}};
                    std::size_t first=0,last=0;double longest=0;
                    for(std::size_t m=0;m<4;++m)for(std::size_t n=0;n<m;++n){const double length=std::hypot(endpoints[m].X-endpoints[n].X,endpoints[m].Y-endpoints[n].Y);if(length>longest){longest=length;first=m;last=n;}}
                    if(longest==0)continue;const auto x0=endpoints[first],x1=endpoints[last];
                    if(std::max(x0.X,x1.X)<lx-p.Tolerance||std::min(x0.X,x1.X)>rx+p.Tolerance||std::max(x0.Y,x1.Y)<ly-p.Tolerance||std::min(x0.Y,x1.Y)>ty+p.Tolerance)continue;
                    addEdge(x0,x1,placed+1,true);emit(x0);emit(x1);emit({(x0.X+x1.X)*.5,(x0.Y+x1.Y)*.5});
                }
            }
        }
        ++Queries;
        // Intersections partition real contact loci into intervals. Their
        // midpoints expose touch-only free intervals swallowed by NFP unions.
        std::vector<std::vector<double>> cuts(edges.size());
        for(std::size_t i=0;i<edges.size();++i)if(edges[i].Contact)cuts[i]={0,1};
        for(std::size_t i=0;i<edges.size()&&!stop();++i)for(std::size_t j=0;j<i;++j){
            const auto& a=edges[i];const auto& b2=edges[j];if(a.Owner==b2.Owner&&!a.Contact&&!b2.Contact)continue;
            const double ax=a.B.X-a.A.X,ay=a.B.Y-a.A.Y,bx=b2.B.X-b2.A.X,by=b2.B.Y-b2.A.Y;
            const double cross=ax*by-ay*bx;const double dx=b2.A.X-a.A.X,dy=b2.A.Y-a.A.Y;
            if(std::abs(cross)<=1e-12*std::max(1.,std::hypot(ax,ay)*std::hypot(bx,by)))
            {
                // Collinear overlap also contributes interval endpoints.
                if(std::abs(dx*ay-dy*ax)<=p.Tolerance*std::hypot(ax,ay))
                {
                    const auto addProjection=[&](std::size_t index,const Segment& segment,Point point)
                    {
                        const double x=segment.B.X-segment.A.X,y=segment.B.Y-segment.A.Y,den=x*x+y*y;if(den<=0)return;
                        const double t=((point.X-segment.A.X)*x+(point.Y-segment.A.Y)*y)/den;
                        if(t>=-1e-9&&t<=1+1e-9){emit(point);if(segment.Contact)cuts[index].push_back(std::clamp(t,0.,1.));}
                    };
                    addProjection(i,a,b2.A);addProjection(i,a,b2.B);addProjection(j,b2,a.A);addProjection(j,b2,a.B);
                }
                continue;
            }
            const double t=(dx*by-dy*bx)/cross,u=(dx*ay-dy*ax)/cross;
            if(t>=-1e-9&&t<=1+1e-9&&u>=-1e-9&&u<=1+1e-9){++Intersections;emit({a.A.X+t*ax,a.A.Y+t*ay});
                if(a.Contact)cuts[i].push_back(std::clamp(t,0.,1.));if(b2.Contact)cuts[j].push_back(std::clamp(u,0.,1.));}
        }
        for(std::size_t i=0;i<edges.size()&&!stop();++i)if(edges[i].Contact){
            auto& values=cuts[i];std::sort(values.begin(),values.end());
            values.erase(std::unique(values.begin(),values.end(),[](double x,double y){return std::abs(x-y)<1e-10;}),values.end());
            for(std::size_t j=1;j<values.size();++j){const auto t=(values[j-1]+values[j])*.5;
                emit({edges[i].A.X+t*(edges[i].B.X-edges[i].A.X),edges[i].A.Y+t*(edges[i].B.Y-edges[i].A.Y)});}
        }
        if(rx-lx>p.Tolerance&&ty-ly>p.Tolerance){
            PathsD domain{{{lx,ly},{rx,ly},{rx,ty},{lx,ty}}};
            auto free=forb.empty()?domain:Difference(domain,Union(forb,FillRule::NonZero,8),FillRule::NonZero,8);
            area+=std::abs(Area(free));
            for(const auto& loop:free)for(std::size_t j=0;j<loop.size();++j){const auto a=loop[j],c=loop[(j+1)%loop.size()];emit({a.x,a.y});emit({(a.x+c.x)*.5,(a.y+c.y)*.5});}
        } else ++DegenerateDomains;
        // In one-dimensional domains, intersections partition feasible intervals;
        // sample their interiors as well as exact boundary contacts.
        if(rx-lx<=p.Tolerance||ty-ly<=p.Tolerance){
            std::sort(origins.begin(),origins.end(),[&](Point a,Point b2){return rx-lx<=p.Tolerance?a.Y<b2.Y:a.X<b2.X;});
            const auto original=origins;for(std::size_t i=1;i<original.size();++i)emit({(original[i-1].X+original[i].X)*.5,(original[i-1].Y+original[i].Y)*.5});
        }
        const auto tryPose=[&](Pose pose,bool retainBlocked,bool& retryable)
        {
            retryable=false;
            if(stop())return false;
            ++Attempts;
            if(!InsideSheet(part.Geometry,pose,stock.Width,stock.Height,p.EdgeMargin,p.Tolerance))return false;
            const auto material=AxisSafety::Make(part.Geometry,pose);
            const auto containment=AxisSafety::Containment(material,stock.Width,stock.Height,p.EdgeMargin);
            if(containment==AxisSafety::Verdict::Uncertain){QueryTruncated=true;return false;}
            if(containment==AxisSafety::Verdict::Blocked){retryable=true;return false;}
            const auto box=Bounds(part.Geometry,pose);
            std::vector<std::size_t> blockers;
            bool nativeBlocked=false,nominalBlocked=false;
            for(std::size_t k=0;k<obstacles.size();++k)
            {
                if(lifeStop())return false;
                const auto& obstacle=obstacles[k];
                if(!Near(box,obstacle.Box,p.Tolerance+material.Roundoff+obstacle.Material.Roundoff))continue;
                const bool conflict=ShapesConflict(p.Parts[obstacle.PartIndex].Geometry,obstacle.Transform,
                    part.Geometry,pose,0,p.Tolerance,p.AllowCommonEdges);
                const auto verdict=AxisSafety::Pair(material,obstacle.Material,0,p.AllowCommonEdges,p.Tolerance);
                if(verdict==AxisSafety::Verdict::Uncertain){QueryTruncated=true;return false;}
                nativeBlocked=nativeBlocked||conflict;
                nominalBlocked=nominalBlocked||verdict==AxisSafety::Verdict::Blocked;
                if(conflict||verdict==AxisSafety::Verdict::Blocked)blockers.push_back(k);
            }
            retryable=nominalBlocked&&!nativeBlocked;
            if(blockers.empty()||(retainBlocked&&blockers.size()<=maxBlockers))
                fits.push_back({index,pose,box.Y+box.X*.0001,blockers});
            return blockers.empty();
        };
        constexpr std::array<Point,8> directions{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
        for(const auto q:origins)
        {
            if(stop())break;
            const Pose proposal{q.X,q.Y,angle};
            bool retryable=false;
            bool found=tryPose(proposal,true,retryable);
            if(found||!retryable)continue;
            const auto material=AxisSafety::Make(part.Geometry,proposal);
            const auto box=Bounds(part.Geometry,proposal);
            const double magnitude=std::max({1.,std::abs(q.X),std::abs(q.Y),stock.Width,stock.Height,
                std::abs(box.X),std::abs(box.Y),std::abs(box.X+box.Width),std::abs(box.Y+box.Height)});
            const double retreat=std::max(8*(1e-8+std::numeric_limits<double>::epsilon()*magnitude),
                2*(material.Roundoff+obstacleRoundoff));
            // Preserve exact collapsed contacts first. Numerical retreats are
            // actual new poses after event deduplication, sharing every limit.
            for(unsigned level=0;level<3&&!found;++level)
            {
                const double distance=std::ldexp(retreat,int(level)*2);
                for(const auto direction:directions)
                {
                    if(stop())break;
                    bool ignored=false;
                    found=tryPose({q.X+direction.X*distance,q.Y+direction.Y*distance,angle},false,ignored);
                    if(found)break;
                }
                if(stop())break;
            }
        }
        return fits;
    }
    bool visit(double deadline,std::size_t attemptLimit){
        struct Restore {double& Deadline;double OldDeadline;std::size_t& Limit;std::size_t OldLimit;
            ~Restore(){Deadline=OldDeadline;Limit=OldLimit;}} guard{ActiveDeadline,ActiveDeadline,ActiveAttemptLimit,ActiveAttemptLimit};
        ActiveDeadline=std::min(ActiveDeadline,deadline);ActiveAttemptLimit=std::min(ActiveAttemptLimit,attemptLimit);
        ++Nodes;const auto placed=Board.Placements.size();if(placed>Best.Placements.size()){Best=Board;BestUsed=Used;}
        if(placed==std::accumulate(P->Parts.begin(),P->Parts.end(),std::size_t(0),[](std::size_t a,const Part& b){return a+b.Quantity;}))return true;
        if(stop())return false;
        std::vector<Choice> choices;
        for(std::size_t i=0;i<P->Parts.size()&&!stop();++i){if(Used[i]>=P->Parts[i].Quantity)continue;
            Choice c;c.Index=i;c.Area=Area(P->Parts[i].Geometry);
            for(const auto angle:Angles[i])c.Tall=std::max(c.Tall,Bounds(P->Parts[i].Geometry,{0,0,angle}).Height);
            for(double angle:Angles[i]){if(stop())break;auto fits=query(i,angle,c.FreeArea);c.Fits.insert(c.Fits.end(),fits.begin(),fits.end());}
            if(!c.Fits.empty()){std::stable_sort(c.Fits.begin(),c.Fits.end(),[](const auto& a,const auto& b){return a.Score<b.Score;});choices.push_back(std::move(c));}
        }
        if(stop()||choices.empty())return false;
        // Candidate scarcity is a heuristic only; branch over multiple remaining specifications.
        std::stable_sort(choices.begin(),choices.end(),[&](const Choice& a,const Choice& b){
            if(Policy==DomainOrderPolicy::FewestPlacements&&a.Fits.size()!=b.Fits.size())return a.Fits.size()<b.Fits.size();
            if(Policy==DomainOrderPolicy::AreaFirst&&a.Area!=b.Area)return a.Area>b.Area;
            if(Policy==DomainOrderPolicy::TallFirst&&a.Tall!=b.Tall)return a.Tall>b.Tall;
            const double aa=a.FreeArea/std::max<std::size_t>(1,Angles[a.Index].size());
            const double bb=b.FreeArea/std::max<std::size_t>(1,Angles[b.Index].size());
            if(aa!=bb)return aa<bb;
            if(a.Fits.size()!=b.Fits.size())return a.Fits.size()<b.Fits.size();return a.Area>b.Area;});
        const std::size_t partLimit=std::min<std::size_t>(3,choices.size());
        const bool root=Board.Placements.empty();
        std::vector<std::vector<std::size_t>> selectedByPart(partLimit);
        for(std::size_t k=0;k<partLimit&&!stop();++k){const auto& c=choices[k];
            // Mix low-left and widely separated feasible candidates to avoid a single local contact cluster.
            std::vector<std::size_t> selected;std::set<double> representedAngles;
            // Preserve at least one real placement per allowed direction before
            // spending the remaining slots on spatial diversity.
            for(std::size_t j=0;j<c.Fits.size();++j)
                if(representedAngles.insert(NormalizeAngle(c.Fits[j].P.AngleDegrees)).second)selected.push_back(j);
            const std::size_t count=std::min<std::size_t>(std::max<std::size_t>(24,selected.size()),c.Fits.size());
            while(selected.size()<count){std::size_t best=c.Fits.size();double dist=-1;
                for(std::size_t j=0;j<c.Fits.size();++j){if(std::find(selected.begin(),selected.end(),j)!=selected.end())continue;
                    double minimum=1e100;for(auto prev:selected){double dx=(c.Fits[j].P.X-c.Fits[prev].P.X)/P->Stocks[0].Width,dy=(c.Fits[j].P.Y-c.Fits[prev].P.Y)/P->Stocks[0].Height;
                        double d=dx*dx+dy*dy+(c.Fits[j].P.AngleDegrees==c.Fits[prev].P.AngleDegrees?0:.03);minimum=std::min(minimum,d);}
                    if(minimum>dist){dist=minimum;best=j;}}
                if(best==c.Fits.size())break;selected.push_back(best);
            }
            selectedByPart[k]=std::move(selected);
        }
        // Interleave specifications at each depth, so an early specification
        // cannot consume all root work before any other has been attempted.
        for(std::size_t round=0;!stop();++round){bool any=false;
            for(std::size_t k=0;k<partLimit&&!stop();++k){
                if(round>=selectedByPart[k].size())continue;any=true;
                const auto& c=choices[k];const auto j=selectedByPart[k][round];
                const auto& part=P->Parts[c.Index];const auto pose=c.Fits[j].P;
                std::size_t childLimit=ActiveAttemptLimit;
                if(root){++RootBranches;const auto quota=std::max<std::size_t>(1,O->MaxPlacementAttempts/128);
                    childLimit=Attempts+std::min(quota,ActiveAttemptLimit-Attempts);}
                Board.Placements.push_back({part.ID,Used[c.Index],pose,0,false});++Used[c.Index];Board.MaterialArea+=c.Area;
                if(visit(ActiveDeadline,childLimit))return true;
                Board.MaterialArea-=c.Area;--Used[c.Index];Board.Placements.pop_back();
            }
            if(!any)break;
        }
        return false;
    }
public:
    explicit DomainSearch(DomainOrderPolicy policy=DomainOrderPolicy::FreeArea):Policy(policy){}
    Detail::PlacementDomainQueryResult Probe(const Problem& p,const Layout& layout,std::size_t partIndex,std::size_t maxBlockers,const Options& o) {
        Start=std::chrono::steady_clock::now();P=&p;O=&o;Board=layout;Attempts=Nodes=Queries=Intersections=DegenerateDomains=RootBranches=0;Cancelled=QueryTruncated=false;ActiveDeadline=o.TimeLimitSeconds;ActiveAttemptLimit=o.MaxPlacementAttempts;
        Detail::PlacementDomainQueryResult result;
        const auto finish=[&]{result.Attempts=Attempts;result.Truncated=QueryTruncated||stop();return result;};
        if(stop())return finish();
        if(p.Stocks.size()!=1||partIndex>=p.Parts.size()||p.PartGap!=0||p.Tolerance<=0||maxBlockers>5)return result;
        if(!CheckProblem(p).empty()){result.Truncated=true;return result;}
        for(const auto& x:layout.Placements)
            if(std::none_of(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==x.PartID;}))return result;
        Prepared.resize(p.Parts.size());
        for(std::size_t i=0;i<p.Parts.size()&&!stop();++i)
        {
            NfpBuildOptions build;build.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());
            build.Cancel=[&]{return stop();};Prepared[i]=Cache.Prepare(p.Parts[i].Geometry,build);
            if(!Prepared[i]){QueryTruncated=true;return finish();}
        }
        double area=0;
        for(double angle:DistinctAngles(p.Parts[partIndex],p.Tolerance,[&]{return stop();}))
        {
            if(stop())break;
            for(auto& c:query(partIndex,angle,area,maxBlockers))result.Candidates.push_back({c.P,std::move(c.Blockers)});
        }
        return finish();
    }
    Result Run(const Problem& p,const Options& o) override {
        Start=std::chrono::steady_clock::now();P=&p;O=&o;Attempts=Nodes=Queries=Intersections=DegenerateDomains=RootBranches=0;Cancelled=QueryTruncated=false;ActiveDeadline=o.TimeLimitSeconds;ActiveAttemptLimit=o.MaxPlacementAttempts;Result result;result.State=Status::Partial;
        for(const auto& part:p.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
        const auto errors=CheckProblem(p);if(!errors.empty()){result.State=Status::InvalidInput;result.Diagnostics=errors;return result;}
        if(p.Stocks.size()!=1||p.Stocks[0].Quantity==0||p.PartGap!=0||p.Tolerance<=0||p.Parts.empty()){
            result.Diagnostics.push_back("Domain search supports one stock and zero-gap polygons only.");return result;}
        for(const auto& part:p.Parts)if(part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Material!=p.Stocks[0].Material){
            result.Diagnostics.push_back("Domain search supports polygons of one material without holes.");return result;}
        Used.assign(p.Parts.size(),0);BestUsed=Used;Prepared.resize(p.Parts.size());Angles.resize(p.Parts.size());
        Board={p.Stocks[0].ID,0,p.Stocks[0].Width,p.Stocks[0].Height,{},0};Best=Board;
        for(std::size_t i=0;i<p.Parts.size()&&!stop();++i){NfpBuildOptions build;build.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-elapsed());build.Cancel=[&]{return stop();};
            Prepared[i]=Cache.Prepare(p.Parts[i].Geometry,build);Angles[i]=DistinctAngles(p.Parts[i],p.Tolerance,[&]{return stop();});if(!Prepared[i]){result.Diagnostics.push_back("Unsupported or cancelled NFP shape preparation.");break;}}
        bool ready=std::all_of(Prepared.begin(),Prepared.end(),[](const auto& v){return bool(v);});if(ready&&!stop())visit(o.TimeLimitSeconds,o.MaxPlacementAttempts);
        if(!Best.Placements.empty())result.Layouts.push_back(Best);result.Remaining.clear();
        for(std::size_t i=0;i<p.Parts.size();++i)if(BestUsed[i]<p.Parts[i].Quantity)result.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-BestUsed[i]});
        if(Cancelled)result.State=Status::Cancelled;else if(result.Remaining.empty())result.State=Status::Complete;
        result.Stats.ElapsedSeconds=elapsed();result.Stats.PlacementAttempts=Attempts;
        result.Diagnostics.push_back("Domain search nodes="+std::to_string(Nodes)+", queries="+std::to_string(Queries)+", edge intersections="+std::to_string(Intersections)+", degenerate domains="+std::to_string(DegenerateDomains)+", root branches="+std::to_string(RootBranches));
        result.Diagnostics.push_back("Bounded candidates and part ordering are heuristic; incomplete is not infeasibility.");return result;
    }
};
}
StrategyFactory MakeDomainSearchFactory(DomainOrderPolicy policy){return [policy]{return std::make_unique<DomainSearch>(policy);};}
namespace Detail {
namespace {
class PlacementDomainProbe final : public IPlacementDomainProbe {
    DomainSearch Worker;
public:
    PlacementDomainQueryResult Find(const Problem& p,const Layout& layout,std::size_t partIndex,std::size_t maxBlockers,const Options& o) override
    {return Worker.Probe(p,layout,partIndex,maxBlockers,o);}
};
}
std::unique_ptr<IPlacementDomainProbe> MakePlacementDomainProbe(){return std::make_unique<PlacementDomainProbe>();}
std::vector<Pose> ProbeDomainCandidates(const Problem& p,const Layout& layout,std::size_t partIndex,const Options& o)
{auto result=DomainSearch().Probe(p,layout,partIndex,0,o);std::vector<Pose> out;for(const auto& c:result.Candidates)out.push_back(c.Transform);return out;}
}
}
