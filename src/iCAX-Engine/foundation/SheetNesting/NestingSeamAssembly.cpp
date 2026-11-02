#include "SheetNesting.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Link {std::size_t Other;Pose Relative;Point AnchorCorner,OtherCorner;};
    double Length(Point a,Point b){return std::hypot(a.X-b.X,a.Y-b.Y);}
    std::vector<Point> Ring(const Shape& s,double tolerance,const std::function<bool()>& stop)
    {
        std::vector<Point> r;r.reserve(s.Outer.size());
        const auto collinear=[&](Point a,Point b,Point c)
        {
            const double dx=c.X-a.X,dy=c.Y-a.Y,l2=dx*dx+dy*dy;
            const double t=l2?std::clamp(((b.X-a.X)*dx+(b.Y-a.Y)*dy)/l2,0.0,1.0):0;
            return Length(b,{a.X+t*dx,a.Y+t*dy})<=tolerance;
        };
        std::size_t end=s.Outer.size();
        if(end>1&&Length(s.Outer.front(),s.Outer.back())<=tolerance)--end;
        for(std::size_t i=0;i<end;++i)
        {
            if((i&63)==0&&stop())return {};
            const auto point=s.Outer[i];
            while(r.size()>1&&collinear(r[r.size()-2],r.back(),point))r.pop_back();
            r.push_back(point);
        }
        std::size_t first=0;bool changed=true;
        while(changed&&r.size()-first>3)
        {
            if(stop())return {};changed=false;
            if(collinear(r.back(),r[first],r[first+1])){++first;changed=true;}
            if(r.size()-first>3&&collinear(r[r.size()-2],r.back(),r[first])){r.pop_back();changed=true;}
        }
        if(first)r.erase(r.begin(),r.begin()+first);
        if(r.empty())return r;
        const auto origin=r.front();long double area=0;
        for(std::size_t i=1;i+1<r.size();++i)
        {
            if((i&63)==0&&stop())return {};
            area+=(static_cast<long double>(r[i].X)-origin.X)*(r[i+1].Y-origin.Y)-(static_cast<long double>(r[i].Y)-origin.Y)*(r[i+1].X-origin.X);
        }
        if(area<0)std::reverse(r.begin(),r.end());
        return r;
    }
}
Result RunSeamAssemblyIf(const Problem& p,const Options& o,const std::function<bool(const Result&)>& accept)
{
    Result result;for(const auto& a:p.Parts)if(a.Quantity)result.Remaining.push_back({a.ID,a.Quantity});result.State=Status::Partial;
    const auto begin=std::chrono::steady_clock::now();bool cancelled=false,exhausted=false;
    const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();};
    const auto stop=[&]{if(cancelled||exhausted)return true;if(o.Cancel&&o.Cancel()){cancelled=true;return true;}if(elapsed()>=o.TimeLimitSeconds||result.Stats.PlacementAttempts>=o.MaxPlacementAttempts){exhausted=true;return true;}return false;};
    const auto stoppedResult=[&]()
    {result.State=cancelled?Status::Cancelled:Status::Partial;result.Stats.ElapsedSeconds=elapsed();return result;};
    if(stop())return stoppedResult();
    const auto n=p.Parts.size();
    if(!o.EnableCombinations||p.PartGap>0||n<2||n>o.MaxAssemblyMembers)
    {result.Diagnostics.push_back("Seam assembly requires zero gap, enabled combinations, and 2..MaxAssemblyMembers one-instance specifications of one material.");return result;}
    for(const auto& a:p.Parts)if(a.Quantity!=1||a.Geometry.Radius>0||a.Material!=p.Parts[0].Material)return result;
    std::vector<std::vector<Point>> rings;std::vector<double> areas;
    for(const auto& a:p.Parts)
    {
        if(stop())return stoppedResult();
        auto ring=Ring(a.Geometry,p.Tolerance,stop);
        if(stop())return stoppedResult();
        rings.push_back(std::move(ring));areas.push_back(Area(a.Geometry));
    }
    std::vector<std::vector<Link>> links(n);
    for(std::size_t i=0;i<n&&!stop();++i)for(std::size_t j=0;j<n&&!stop();++j)if(i!=j)
    {
        const auto& a=rings[i];const auto& b=rings[j];std::set<std::tuple<long long,long long,long long>> seen;
        for(std::size_t ai=0;ai<a.size()&&!stop();++ai)for(std::size_t bi=0;bi<b.size()&&!stop();++bi)
        {
            const auto ac=a[ai],ap=a[(ai+a.size()-1)%a.size()],an=a[(ai+1)%a.size()];
            const auto bc=b[bi],bp=b[(bi+b.size()-1)%b.size()],bn=b[(bi+1)%b.size()];
            const double l1=Length(ac,ap),l2=Length(ac,an);
            if(l1<=p.Tolerance||l2<=p.Tolerance)continue;
            const long double cross=static_cast<long double>(ap.X-ac.X)*(an.Y-ac.Y)-static_cast<long double>(ap.Y-ac.Y)*(an.X-ac.X);
            if(std::abs(cross)<=p.Tolerance*std::max(l1,l2))continue;
            const long double dot=static_cast<long double>(ap.X-ac.X)*(an.X-ac.X)+static_cast<long double>(ap.Y-ac.Y)*(an.Y-ac.Y);
            // Rectangular outside corners need two length matches. Non-orthogonal
            // seam kinks can survive later subdivision with shorter incident edges.
            if(std::abs(dot)<=p.Tolerance*std::max(l1,l2)&&
               (std::abs(l1-Length(bc,bn))>p.Tolerance*4||std::abs(l2-Length(bc,bp))>p.Tolerance*4))continue;
            const double angle=NormalizeAngle((std::atan2(ap.Y-ac.Y,ap.X-ac.X)-std::atan2(bn.Y-bc.Y,bn.X-bc.X))*180/Pi);
            const auto br=Transform(bc,{0,0,angle});Pose relative{ac.X-br.X,ac.Y-br.Y,angle};
            const auto bpr=Transform(bp,relative);const double bpl=Length(bpr,ac);
            if(bpl<=p.Tolerance||Length({ac.X+(bpr.X-ac.X)*l2/bpl,ac.Y+(bpr.Y-ac.Y)*l2/bpl},an)>p.Tolerance*4)continue;
            const auto key=std::tuple(std::llround(relative.X/p.Tolerance),std::llround(relative.Y/p.Tolerance),std::llround(angle*1e6));
            if(!seen.insert(key).second)continue;++result.Stats.PlacementAttempts;
            // Link candidates are not placements. Defer the actual material predicate to
            // the world-frame fits() call, then validate the complete original layout.
            links[i].push_back({j,relative,ac,bc});
        }
    }
    result.Stats.PreparationSeconds=elapsed();
    std::vector<std::size_t> seeds(n);for(std::size_t i=0;i<n;++i)seeds[i]=i;
    std::stable_sort(seeds.begin(),seeds.end(),[&](auto a,auto b){return links[a].size()!=links[b].size()?links[a].size()>links[b].size():areas[a]>areas[b];});
    for(std::size_t si=0;si<p.Stocks.size()&&!stop();++si)
    {
        const auto& stock=p.Stocks[si];if(!stock.Quantity||stock.Material!=p.Parts[0].Material)continue;
        double total=0;for(double a:areas)total+=a;
        if(total>(stock.Width-2*p.EdgeMargin)*(stock.Height-2*p.EdgeMargin)+p.Tolerance*(stock.Width+stock.Height))continue;
        for(auto seed:seeds)for(auto angle:CandidateAngles(p.Parts[seed],p.Tolerance))
        {
            if(stop())break;
            std::vector<Pose> poses(n);std::vector<bool> used(n);poses[seed]={0,0,angle};used[seed]=true;
            const auto fits=[&](std::size_t next,Pose pose)
            {
                if(!AngleAllowed(p.Parts[next],pose.AngleDegrees,p.Tolerance))return false;
                double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
                for(std::size_t k=0;k<n;++k)if(used[k]||k==next)
                {
                    const auto box=Bounds(p.Parts[k].Geometry,k==next?pose:poses[k]);x0=std::min(x0,box.X);y0=std::min(y0,box.Y);x1=std::max(x1,box.X+box.Width);y1=std::max(y1,box.Y+box.Height);
                    if(k!=next&&ShapesConflict(p.Parts[k].Geometry,poses[k],p.Parts[next].Geometry,pose,p.PartGap,p.Tolerance))return false;
                }
                return x1-x0<=stock.Width-2*p.EdgeMargin+p.Tolerance&&y1-y0<=stock.Height-2*p.EdgeMargin+p.Tolerance;
            };
            std::function<bool(std::size_t)> search=[&](std::size_t count)
            {
                if(stop())return false;
                if(count==n)
                {
                    auto candidate=result;candidate.Layouts.clear();candidate.Remaining.clear();candidate.State=Status::Complete;
                    double x0=std::numeric_limits<double>::infinity(),y0=x0;
                    for(std::size_t k=0;k<n;++k){const auto box=Bounds(p.Parts[k].Geometry,poses[k]);x0=std::min(x0,box.X);y0=std::min(y0,box.Y);}
                    Layout layout{stock.ID,0,stock.Width,stock.Height,{},total};
                    for(std::size_t k=0;k<n;++k){auto pose=poses[k];pose.X+=p.EdgeMargin-x0;pose.Y+=p.EdgeMargin-y0;layout.Placements.push_back({p.Parts[k].ID,0,pose,1,false});}
                    candidate.Layouts.push_back(std::move(layout));
                    if(Validate(p,candidate).Valid&&(!accept||accept(candidate))){result=std::move(candidate);return true;}return false;
                }
                std::size_t selected=n;std::vector<Pose> choices;
                for(std::size_t next=0;next<n&&!stop();++next)if(!used[next])
                {
                    std::vector<Pose> candidates;std::set<std::tuple<long long,long long,long long>> seen;
                    for(std::size_t k=0;k<n&&!stop();++k)if(used[k])for(const auto& link:links[k])if(link.Other==next)
                    {
                        const double angle=NormalizeAngle(poses[k].AngleDegrees+link.Relative.AngleDegrees);
                        const auto target=Transform(link.AnchorCorner,poses[k]);
                        const auto source=Transform(link.OtherCorner,{0,0,angle});
                        const Pose pose{target.X-source.X,target.Y-source.Y,angle};
                        const auto key=std::tuple(std::llround(pose.X/p.Tolerance),std::llround(pose.Y/p.Tolerance),std::llround(pose.AngleDegrees*1e6));
                        if(!seen.insert(key).second)continue;if(stop())break;++result.Stats.PlacementAttempts;
                        if(fits(next,pose))candidates.push_back(pose);
                    }
                    if(!candidates.empty()&&(selected==n||candidates.size()<choices.size())){selected=next;choices=std::move(candidates);}
                }
                if(selected==n)return false;
                used[selected]=true;for(auto pose:choices){poses[selected]=pose;if(search(count+1))return true;}used[selected]=false;return false;
            };
            if(search(1)){result.Stats.ElapsedSeconds=elapsed();result.Stats.PlacementSeconds=result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds;return result;}
        }
    }
    if(cancelled)result.State=Status::Cancelled;
    result.Stats.ElapsedSeconds=elapsed();result.Stats.PlacementSeconds=result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds;
    result.Diagnostics.push_back("Finite two-edge seam assembly did not find a complete one-sheet layout; no infeasibility proof.");
    return result;
}

Result RunSeamAssembly(const Problem& p,const Options& o)
{ return RunSeamAssemblyIf(p,o,{}); }

}
