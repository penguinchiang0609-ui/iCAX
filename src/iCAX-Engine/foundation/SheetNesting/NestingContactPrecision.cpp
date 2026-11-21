#include "SheetNestingContactPrecision.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace iCAX::SheetNesting {
namespace {
using namespace Detail;
using Triangle=std::array<Point,3>;
struct Row {std::size_t I,J;double Nx,Ny,B,Correction=0;};
long double cross(Point a,Point b,Point c){return (static_cast<long double>(b.X)-a.X)*(static_cast<long double>(c.Y)-a.Y)-(static_cast<long double>(b.Y)-a.Y)*(static_cast<long double>(c.X)-a.X);}
struct Box {double X0,Y0,X1,Y1;};
Box box(const Triangle& a){Box b{a[0].X,a[0].Y,a[0].X,a[0].Y};for(auto p:a){b.X0=std::min(b.X0,p.X);b.Y0=std::min(b.Y0,p.Y);b.X1=std::max(b.X1,p.X);b.Y1=std::max(b.Y1,p.Y);}return b;}
bool far(Box a,Box b,double c){return a.X0>b.X1+c||b.X0>a.X1+c||a.Y0>b.Y1+c||b.Y0>a.Y1+c;}
bool triangulate(const Shape& shape,Pose pose,std::vector<Triangle>& triangles)
{
    std::vector<Point> ring;for(auto p:shape.Outer)ring.push_back(Transform(p,pose));
    if(ring.size()<3||ring.size()>256)return false;
    // Drop coincident consecutive vertices and exactly collinear points only;
    // no tolerance-based replacement may remove a real concavity.
    bool changed=true;while(changed&&ring.size()>3){changed=false;
        for(std::size_t i=0;i<ring.size();++i){const auto a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];
            const long double between=(static_cast<long double>(b.X)-a.X)*(static_cast<long double>(b.X)-c.X)+
                (static_cast<long double>(b.Y)-a.Y)*(static_cast<long double>(b.Y)-c.Y);
            if((a.X==b.X&&a.Y==b.Y)||(cross(a,b,c)==0&&between<=0)){ring.erase(ring.begin()+i);changed=true;break;}}}
    long double signedArea=0;for(std::size_t i=0;i<ring.size();++i)signedArea+=static_cast<long double>(ring[i].X)*ring[(i+1)%ring.size()].Y-static_cast<long double>(ring[(i+1)%ring.size()].X)*ring[i].Y;
    if(signedArea==0)return false;if(signedArea<0)std::reverse(ring.begin(),ring.end());
    while(ring.size()>3){bool found=false;
        for(std::size_t i=0;i<ring.size();++i){const auto prev=(i+ring.size()-1)%ring.size(),next=(i+1)%ring.size();
            const auto a=ring[prev],b=ring[i],c=ring[next];if(cross(a,b,c)<=0)continue;bool inside=false;
            for(std::size_t j=0;j<ring.size();++j)if(j!=prev&&j!=i&&j!=next&&cross(a,b,ring[j])>=0&&cross(b,c,ring[j])>=0&&cross(c,a,ring[j])>=0){inside=true;break;}
            if(inside)continue;triangles.push_back({a,b,c});ring.erase(ring.begin()+i);found=true;break;}
        if(!found)return false;
    }
    triangles.push_back({ring[0],ring[1],ring[2]});
    long double total=0;for(const auto& triangle:triangles)total+=std::abs(cross(triangle[0],triangle[1],triangle[2]))*.5L;
    const long double original=std::abs(signedArea)*.5L;
    return std::abs(total-original)<=std::max(1e-10L,original*1e-12L);
}
std::array<double,3> separation(const Triangle& a,const Triangle& b)
{
    double best=-std::numeric_limits<double>::infinity(),bx=0,by=0;
    for(const auto* tri:{&a,&b})for(std::size_t i=0;i<3;++i){
        const double dx=(*tri)[(i+1)%3].X-(*tri)[i].X,dy=(*tri)[(i+1)%3].Y-(*tri)[i].Y,length=std::hypot(dx,dy);if(length==0)continue;
        for(int sign:{-1,1}){const double nx=sign*(-dy/length),ny=sign*(dx/length);double maxA=-std::numeric_limits<double>::infinity(),minB=-maxA;
            for(auto p:a)maxA=std::max(maxA,p.X*nx+p.Y*ny);for(auto p:b)minB=std::min(minB,p.X*nx+p.Y*ny);
            const double gap=minB-maxA;if(gap>best){best=gap;bx=nx;by=ny;}}
    }
    return {best,bx,by};
}
}
ContactPrecisionResult RefineContactPrecision(const Problem& p,const Result& input,const ContactPrecisionOptions& opt)
{
    ContactPrecisionResult out;out.Output=input;const auto start=std::chrono::steady_clock::now();
    const auto stop=[&]{return (opt.Cancel&&opt.Cancel())||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=opt.TimeLimitSeconds;};
    if(!std::isfinite(opt.Clearance)||!std::isfinite(opt.MaxAxisShift)||!std::isfinite(opt.TimeLimitSeconds)||opt.Clearance<=0||opt.MaxAxisShift<=0||opt.TimeLimitSeconds<=0||!opt.MaxSweeps){out.State=ContactPrecisionStatus::InvalidInput;out.Diagnostic="Invalid refinement budget or dimensions.";return out;}
    const auto valid=Validate(p,input);if(!valid.Valid){out.State=ContactPrecisionStatus::InvalidInput;out.Diagnostic="Input layout failed native validation.";return out;}
    if(input.Layouts.size()>1)
    {
        auto combined=input;bool changed=false;std::size_t sweeps=0;
        for(std::size_t board=0;board<input.Layouts.size();++board)
        {
            if(stop()||sweeps>=opt.MaxSweeps){out.State=ContactPrecisionStatus::LimitReached;out.Diagnostic="Whole-layout precision budget exhausted; original retained.";return out;}
            auto local=input;local.Layouts={input.Layouts[board]};local.Remaining.clear();std::map<std::string,std::size_t> counts;
            for(const auto& q:local.Layouts[0].Placements)++counts[q.PartID];
            for(const auto& part:p.Parts)if(counts[part.ID]<part.Quantity)local.Remaining.push_back({part.ID,part.Quantity-counts[part.ID]});
            local.State=local.Remaining.empty()?Status::Complete:Status::Partial;
            auto budget=opt;budget.TimeLimitSeconds-=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();budget.MaxSweeps-=sweeps;
            const auto refined=RefineContactPrecision(p,local,budget);
            if(refined.State!=ContactPrecisionStatus::Applied&&refined.State!=ContactPrecisionStatus::AlreadySeparated)
            {out.State=refined.State;out.Diagnostic="Whole-layout precision repair retained its original: "+refined.Diagnostic;return out;}
            combined.Layouts[board]=refined.Output.Layouts[0];changed|=refined.State==ContactPrecisionStatus::Applied;
            out.Constraints+=refined.Constraints;sweeps+=refined.Sweeps;out.MaximumShift=std::max(out.MaximumShift,refined.MaximumShift);out.MaximumResidual=std::max(out.MaximumResidual,refined.MaximumResidual);
        }
        if(!Validate(p,combined).Valid){out.State=ContactPrecisionStatus::Infeasible;return out;}
        out.Sweeps=sweeps;out.State=changed?ContactPrecisionStatus::Applied:ContactPrecisionStatus::AlreadySeparated;out.Output=std::move(combined);out.Diagnostic="Original identities and nominal clearances verified on every physical sheet.";return out;
    }
    if(input.Layouts.size()!=1){out.Diagnostic="A nonempty polygon layout is required.";return out;}
    const auto& old=input.Layouts[0];const auto n=old.Placements.size();if(n==0||n>128){out.Diagnostic="Placement count outside supported range.";return out;}
    std::map<std::string,std::size_t> ids;for(std::size_t i=0;i<p.Parts.size();++i){const auto& shape=p.Parts[i].Geometry;if(shape.Radius>0||!shape.Holes.empty()){out.Diagnostic="Circles and holes are unsupported.";return out;}ids.emplace(p.Parts[i].ID,i);}
    std::vector<std::vector<Triangle>> shapes(n);std::vector<Box> bounds(n);std::vector<double> low(2*n),high(2*n),v(2*n),correction(2*n),best=v;
    const auto stockIt=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==old.StockID;});
    if(stockIt==p.Stocks.end()){out.State=ContactPrecisionStatus::InvalidInput;return out;}
    const auto& stock=*stockIt;const double unit=opt.Clearance,required=p.PartGap+unit;
    for(std::size_t i=0;i<n;++i){if(stop()){out.State=ContactPrecisionStatus::LimitReached;return out;}
        const auto it=ids.find(old.Placements[i].PartID);if(it==ids.end()||!triangulate(p.Parts[it->second].Geometry,old.Placements[i].Transform,shapes[i])){out.Diagnostic="Polygon triangulation unsupported.";return out;}
        const auto b=Bounds(p.Parts[it->second].Geometry,old.Placements[i].Transform);bounds[i]={b.X,b.Y,b.X+b.Width,b.Y+b.Height};
        low[i]=std::max(-opt.MaxAxisShift,p.EdgeMargin-b.X)/unit;high[i]=std::min(opt.MaxAxisShift,stock.Width-p.EdgeMargin-b.X-b.Width)/unit;
        low[n+i]=std::max(-opt.MaxAxisShift,p.EdgeMargin-b.Y)/unit;high[n+i]=std::min(opt.MaxAxisShift,stock.Height-p.EdgeMargin-b.Y-b.Height)/unit;
        if(low[i]>high[i]||low[n+i]>high[n+i]){out.State=ContactPrecisionStatus::Infeasible;out.Diagnostic="No permitted board-bound translation.";return out;}}
    const double cutoff=p.PartGap+2*std::sqrt(2.)*opt.MaxAxisShift+2*unit;std::vector<Row> rows;
    for(std::size_t i=0;i<n&&!stop();++i)for(std::size_t j=0;j<i&&!stop();++j){if(far(bounds[i],bounds[j],cutoff))continue;
        for(const auto& a:shapes[i])for(const auto& b:shapes[j]){if(stop()){out.State=ContactPrecisionStatus::LimitReached;out.Diagnostic="Constraint generation interrupted.";return out;}if(far(box(a),box(b),cutoff))continue;const auto s=separation(a,b);
            if(s[0]<cutoff){rows.push_back({i,j,s[1],s[2],(s[0]-required)/unit,0});if(rows.size()>12000){out.State=ContactPrecisionStatus::LimitReached;out.Diagnostic="Contact constraint limit exceeded.";return out;}}}}
    if(stop()){out.State=ContactPrecisionStatus::LimitReached;out.Diagnostic="Constraint generation interrupted.";return out;}
    out.Constraints=rows.size();if(rows.empty()){out.State=ContactPrecisionStatus::AlreadySeparated;out.Diagnostic="No nearby polygon pairs.";return out;}
    double bestResidual=std::numeric_limits<double>::infinity();
    for(;out.Sweeps<opt.MaxSweeps&&!stop();++out.Sweeps){
        for(auto& r:rows){const double norm=2*(r.Nx*r.Nx+r.Ny*r.Ny);if(norm<=0)continue;
            const double viol=r.Nx*(v[r.I]-v[r.J])+r.Ny*(v[n+r.I]-v[n+r.J])+r.Correction*norm-r.B;
            const double next=std::max(0.,viol/norm),delta=r.Correction-next;
            v[r.I]+=delta*r.Nx;v[r.J]-=delta*r.Nx;v[n+r.I]+=delta*r.Ny;v[n+r.J]-=delta*r.Ny;r.Correction=next;}
        for(std::size_t k=0;k<2*n;++k){const double y=v[k]+correction[k];v[k]=std::clamp(y,low[k],high[k]);correction[k]=y-v[k];}
        double residual=0;for(const auto& r:rows)residual=std::max(residual,std::max(0.,r.Nx*(v[r.I]-v[r.J])+r.Ny*(v[n+r.I]-v[n+r.J])-r.B));
        if(residual<bestResidual){bestResidual=residual;best=v;}if(residual<=1e-9){++out.Sweeps;break;}
    }
    out.MaximumResidual=bestResidual;if(bestResidual>1e-9){out.State=stop()?ContactPrecisionStatus::LimitReached:ContactPrecisionStatus::Infeasible;out.Diagnostic="Conservative contact constraints did not converge; original layout retained.";return out;}
    auto candidate=input;
    for(std::size_t i=0;i<n;++i){const double x=best[i]*unit,y=best[n+i]*unit;auto& pose=candidate.Layouts[0].Placements[i].Transform;pose.X+=x;pose.Y+=y;out.MaximumShift=std::max(out.MaximumShift,std::hypot(x,y));}
    if(!Validate(p,candidate).Valid){out.State=ContactPrecisionStatus::Infeasible;out.Diagnostic="Refined layout failed native validation; original retained.";return out;}
    for(std::size_t i=0;i<n;++i)
    {
        const auto& q=candidate.Layouts[0].Placements[i];const auto geometry=AxisSafety::Make(p.Parts[ids.at(q.PartID)].Geometry,q.Transform);
        if(AxisSafety::Containment(geometry,stock.Width,stock.Height,p.EdgeMargin)!=AxisSafety::Verdict::Safe)
        {out.State=ContactPrecisionStatus::Infeasible;out.Diagnostic="Nominal board clearance was not proved; original retained.";return out;}
        for(std::size_t j=0;j<i;++j)
        {
            const auto& other=candidate.Layouts[0].Placements[j];
            if(AxisSafety::Pair(geometry,AxisSafety::Make(p.Parts[ids.at(other.PartID)].Geometry,other.Transform),p.PartGap,p.AllowCommonEdges,p.Tolerance)!=AxisSafety::Verdict::Safe)
            {out.State=ContactPrecisionStatus::Infeasible;out.Diagnostic="Nominal original-material distance was not proved; original retained.";return out;}
        }
    }
    out.State=ContactPrecisionStatus::Applied;out.Output=std::move(candidate);out.Diagnostic="Conservative polygon separation applied; independent exact geometry validation remains advisable.";return out;
}
namespace {
class ContactPrecisionStage final : public IPostProcessor {
    ContactPrecisionOptions Settings;
public:
    explicit ContactPrecisionStage(ContactPrecisionOptions settings):Settings(std::move(settings)){}
    Result Run(const Problem& p,const Result& incumbent,const Options& budget) override {
        auto local=Settings;local.TimeLimitSeconds=std::min(local.TimeLimitSeconds,budget.TimeLimitSeconds);
        const auto priorCancel=local.Cancel;local.Cancel=[priorCancel,budget]{return (priorCancel&&priorCancel())||(budget.Cancel&&budget.Cancel());};
        auto refined=RefineContactPrecision(p,incumbent,local);auto result=std::move(refined.Output);
        // Report stage-only work; preserve constructor-origin placement provenance.
        Statistics stats;stats.CombinationGroups=incumbent.Stats.CombinationGroups;
        stats.CircleLatticeGroups=incumbent.Stats.CircleLatticeGroups;stats.GapFilledParts=incumbent.Stats.GapFilledParts;
        result.Stats=stats;result.Diagnostics.push_back("contact precision: "+refined.Diagnostic+
            "; constraints="+std::to_string(refined.Constraints)+", sweeps="+std::to_string(refined.Sweeps));
        return result;
    }
};
}
PostProcessStage MakeContactPrecisionStage(ContactPrecisionOptions options)
{
    return {"contact-precision",[options]{return std::make_unique<ContactPrecisionStage>(options);},options.MaxAxisShift};
}
}
