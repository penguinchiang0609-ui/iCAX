#include "SheetNestingPlaneEvaluation.h"
#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace iCAX::SheetNesting
{
PlaneArrayAkwardResult ComputePlaneArrayAkward(PlaneArrayContainerKind kind,
    const std::vector<PlaneArrayPitchTemplate>& templates)
{
    PlaneArrayAkwardResult result;
    if(kind!=PlaneArrayContainerKind::Single&&kind!=PlaneArrayContainerKind::SameSpecificationPair)
    {result.Diagnostic="Unsupported source array-container family.";return result;}
    for(std::size_t i=0;i<templates.size();++i)
    {
        const auto& item=templates[i];
        // No candidate means no contribution, even though the template is in
        // the container. Earlier pitches are deliberately not scored.
        if(item.OrderedPitches.empty())continue;
        if(!std::isfinite(item.Length)||!std::isfinite(item.Height)||item.Length<=0||item.Height<=0)
        {result.Diagnostic="Array-template dimensions must be finite and positive.";return result;}
        const auto pitch=item.OrderedPitches.back();
        if(!std::isfinite(pitch.X)||!std::isfinite(pitch.Y)||pitch.X<0||pitch.Y<0)
        {result.Diagnostic="The source final pitch must be finite and nonnegative.";return result;}
        const double q=item.Length*item.Height+.25*(item.Length*item.Length+item.Height*item.Height);
        const double t=pitch.X*pitch.Y+.25*(pitch.X*pitch.X+pitch.Y*pitch.Y);
        const double value=q*(q+t);
        if(!std::isfinite(q)||!std::isfinite(t)||!std::isfinite(value))
        {result.Diagnostic="Array-container weight overflowed.";return result;}
        result.HasEligibleTemplate=true;
        // This is the original sequential tolerance update, not min(value).
        // Sorting or using a relative tolerance would change its semantics.
        if(value<result.K-PlaneArrayAkwardTolerance)
        {result.K=value;result.SelectedTemplate=i;}
    }
    result.Valid=true;return result;
}

PlaneArrayEvaluation EvaluatePlaneArray(std::size_t count,double X,double Y,
    double W,double H,std::optional<double> K)
{
    PlaneArrayEvaluation result;
    if(!count||!std::isfinite(X)||!std::isfinite(Y)||!std::isfinite(W)||!std::isfinite(H)||
        X<=0||Y<=0||W<=0||H<=0||X>W||Y>H)
    {result.Diagnostic="Array evaluation requires a nonempty count and a finite contained positive-area envelope.";return result;}
    if(K&&(!std::isfinite(*K)||*K<0))
    {result.Diagnostic="An explicit array-container weight must be finite and nonnegative.";return result;}
    const double sum=W+H;
    if(!std::isfinite(sum))
    {result.Diagnostic="Array-evaluation region dimensions overflowed.";return result;}
    const double ratio=W/sum,alpha=.5*(1-ratio),beta=.5*ratio;
    if(alpha<=0||beta<=0)
    {result.Diagnostic="Array-evaluation aspect ratio exceeded arithmetic precision.";return result;}
    const double denominatorX=X+std::pow(W-X,alpha);
    const double denominatorY=Y+std::pow(H-Y,beta);
    if(!std::isfinite(denominatorX)||!std::isfinite(denominatorY)||denominatorX<=0||denominatorY<=0)
    {result.Diagnostic="Array-evaluation denominator is not finite and positive.";return result;}
    const double score=static_cast<double>(count)*(1/denominatorX)*(1/denominatorY);
    if(!std::isfinite(score)||score<=0)
    {result.Diagnostic="Array-evaluation score exceeded arithmetic range.";return result;}
    result.E=score;
    if(K)
    {
        const double weighted=*K*score;
        if(!std::isfinite(weighted))
        {result.Diagnostic="Weighted array-evaluation score overflowed.";return result;}
        result.P=weighted;
    }
    result.Valid=true;return result;
}

namespace
{
using namespace Clipper2Lib;
constexpr int SpacePrecision=8;

PathsD SpaceBoolean(ClipType operation,const PathsD& subject,const PathsD& clip={},
    FillRule fill=FillRule::NonZero)
{
    if(subject.empty())return {};
    if(operation==ClipType::Intersection&&clip.empty())return {};
    ClipperD engine(SpacePrecision);engine.AddSubject(subject);
    if(!clip.empty())engine.AddClip(clip);
    PathsD result;
    if(!engine.Execute(operation,fill,result)||engine.ErrorCode())
        throw std::runtime_error("Local-space scoring polygon operation failed.");
    return result;
}

PathsD SpaceMaterial(const Problem& problem,const std::vector<Placement>& placements,bool& circleProxy)
{
    PathsD material;
    for(const auto& placed:placements)
    {
        // Resolve only the supplied actual placement. No quantity, unplaced
        // geometry, future orientation or possible subsequent fit is read.
        const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
            [&](const Part& value){return value.ID==placed.PartID;});
        if(part==problem.Parts.end())throw std::runtime_error("Local-space scoring references an unknown original.");
        const auto pose=placed.Transform;
        if(!std::isfinite(pose.X)||!std::isfinite(pose.Y)||!std::isfinite(pose.AngleDegrees))
            throw std::runtime_error("Local-space scoring pose is not finite.");
        const auto& shape=part->Geometry;PathsD rings;
        if(!std::isfinite(shape.Radius)||shape.Radius<0)
            throw std::runtime_error("Local-space scoring radius is invalid.");
        if(shape.Radius>0)
        {
            const auto center=Detail::Transform(shape.Center,pose);
            if(!std::isfinite(center.X)||!std::isfinite(center.Y))
                throw std::runtime_error("Local-space scoring disk center is not finite.");
            auto& ring=rings.emplace_back();ring.reserve(64);
            // A disk has no material orientation. Keep its marked scoring
            // proxy in world directions, independent of an arbitrary angle.
            for(unsigned i=0;i<64;++i)
            {
                const auto angle=2*Detail::Pi*double(i)/64;
                const double x=center.X+shape.Radius*std::cos(angle),y=center.Y+shape.Radius*std::sin(angle);
                if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Local-space scoring disk proxy overflowed.");
                ring.emplace_back(x,y);
            }
            circleProxy=true;
        }
        else
        {
            const auto append=[&](const std::vector<iCAX::SheetNesting::Point>& source)
            {
                if(source.size()<3)throw std::runtime_error("Local-space scoring ring has no area.");
                auto& ring=rings.emplace_back();ring.reserve(source.size());
                for(const auto point:source)
                {
                    const auto world=Detail::Transform(point,pose);
                    if(!std::isfinite(world.X)||!std::isfinite(world.Y))
                        throw std::runtime_error("Local-space scoring contour is not finite.");
                    ring.emplace_back(world.X,world.Y);
                }
            };
            append(shape.Outer);for(const auto& hole:shape.Holes)append(hole);
        }
        // Normalize each original with EvenOdd before combining materials.
        // A global EvenOdd union would incorrectly cancel overlapping pieces.
        auto original=SpaceBoolean(ClipType::Union,rings,{},FillRule::EvenOdd);
        if(original.empty())throw std::runtime_error("Local-space scoring original has no represented material.");
        material.insert(material.end(),std::make_move_iterator(original.begin()),std::make_move_iterator(original.end()));
    }
    return SpaceBoolean(ClipType::Union,material);
}

PathsD SpaceBuffered(const PathsD& material,double distance,const PathsD& domain)
{
    if(material.empty())return {};
    PathsD buffered=material;
    if(distance>0)
    {
        // Use the offset engine directly so a failed scaling/offset cannot be
        // confused with an empty obstacle and produce a usable score.
        constexpr double scale=1e8;int error=0;
        for(const auto& path:material)for(const auto point:path)
            if(std::max(std::abs(point.x),std::abs(point.y))*scale+distance*scale>=max_coord)
                throw std::runtime_error("Local-space scoring buffered coordinates exceeded range.");
        auto integer=ScalePaths<int64_t,double>(material,scale,error);
        if(error)throw std::runtime_error("Local-space scoring offset coordinates exceeded range.");
        const double arcTolerance=std::max(1e-8,std::min(.001,distance*.01));
        ClipperOffset offset(2.,arcTolerance*scale);
        offset.AddPaths(integer,JoinType::Round,EndType::Polygon);
        Paths64 expanded;offset.Execute(distance*scale,expanded);
        if(offset.ErrorCode()||expanded.empty())throw std::runtime_error("Local-space scoring material offset failed.");
        buffered=ScalePaths<double,int64_t>(expanded,1/scale,error);
        if(error)throw std::runtime_error("Local-space scoring offset output exceeded range.");
    }
    return SpaceBoolean(ClipType::Intersection,buffered,domain);
}

double SpacePerimeter(const PathsD& paths)
{
    // MSVC's long double has double precision. Compensate the positive sum
    // so path/vertex ordering does not accumulate an artificial contact score.
    long double total=0,correction=0;
    for(const auto& path:paths)if(path.size()>1)for(std::size_t i=0;i<path.size();++i)
    {
        const auto& a=path[i];const auto& b=path[(i+1)%path.size()];
        const auto adjusted=static_cast<long double>(std::hypot(a.x-b.x,a.y-b.y))-correction;
        const auto next=total+adjusted;correction=(next-total)-adjusted;total=next;
    }
    const auto value=double(total);
    if(!std::isfinite(value))throw std::runtime_error("Local-space scoring perimeter overflowed.");
    return value;
}

long double SpaceRingArea(const PathD& path)
{
    if(path.size()<3)return 0;
    const auto origin=path.front();long double area=0;
    for(std::size_t i=1;i+1<path.size();++i)
    {
        const long double ax=static_cast<long double>(path[i].x)-origin.x;
        const long double ay=static_cast<long double>(path[i].y)-origin.y;
        const long double bx=static_cast<long double>(path[i+1].x)-origin.x;
        const long double by=static_cast<long double>(path[i+1].y)-origin.y;
        area+=ax*by-ay*bx;
    }
    return std::abs(area)/2;
}
}

struct PlanePlacementSpaceEvaluator::Impl
{
    const Problem* Source=nullptr;
    PathsD Domain,FixedMaterial,FixedBuffered;
    double Buffer=0,FixedPerimeter=0;
    bool Valid=false,CircleProxyUsed=false;
    std::string Diagnostic;
};

PlanePlacementSpaceEvaluator::PlanePlacementSpaceEvaluator(const Problem& problem,const Layout& layout,Rect domain)
{
    std::shared_ptr<Impl> cache;
    try
    {
        cache=std::make_shared<Impl>();cache->Source=&problem;
        const double right=domain.X+domain.Width,top=domain.Y+domain.Height;
        if(!std::isfinite(domain.X)||!std::isfinite(domain.Y)||!std::isfinite(domain.Width)||
            !std::isfinite(domain.Height)||domain.Width<=0||domain.Height<=0||
            !std::isfinite(right)||!std::isfinite(top)||right<=domain.X||top<=domain.Y||
            !std::isfinite(domain.Width*domain.Height))
            throw std::runtime_error("Local-space scoring requires an explicit finite positive-area domain.");
        if(!std::isfinite(problem.PartGap)||problem.PartGap<0||!std::isfinite(problem.Tolerance)||problem.Tolerance<0)
            throw std::runtime_error("Local-space scoring gap or tolerance is invalid.");
        cache->Buffer=problem.PartGap/2+4*problem.Tolerance;
        if(!std::isfinite(cache->Buffer)||!std::isfinite(cache->Buffer*1e8))
            throw std::runtime_error("Local-space scoring buffer overflowed.");
        cache->Domain=SpaceBoolean(ClipType::Union,PathsD{{{domain.X,domain.Y},{right,domain.Y},{right,top},{domain.X,top}}});
        if(cache->Domain.empty())throw std::runtime_error("Local-space scoring domain is below polygon precision.");
        cache->FixedMaterial=SpaceMaterial(problem,layout.Placements,cache->CircleProxyUsed);
        cache->FixedBuffered=SpaceBuffered(cache->FixedMaterial,cache->Buffer,cache->Domain);
        cache->FixedPerimeter=SpacePerimeter(cache->FixedBuffered);cache->Valid=true;
    }
    catch(const std::exception& error){if(cache)cache->Diagnostic=error.what();}
    catch(...){if(cache)cache->Diagnostic="Local-space scoring cache failed.";}
    Cached=std::move(cache);
}

bool PlanePlacementSpaceEvaluator::Ready()const noexcept
{return Cached&&Cached->Valid;}

PlanePlacementSpaceEvaluation PlanePlacementSpaceEvaluator::Evaluate(const std::vector<Placement>& additions)const
{
    PlanePlacementSpaceEvaluation result;
    if(!Ready())
    {result.Diagnostic=Cached?Cached->Diagnostic:"Local-space scoring cache is unavailable.";return result;}
    result.CircleProxyUsed=Cached->CircleProxyUsed;
    try
    {
        const auto material=SpaceMaterial(*Cached->Source,additions,result.CircleProxyUsed);
        const auto buffered=SpaceBuffered(material,Cached->Buffer,Cached->Domain);
        auto all=Cached->FixedBuffered;all.insert(all.end(),buffered.begin(),buffered.end());
        const auto merged=SpaceBoolean(ClipType::Union,all);
        const double addedPerimeter=SpacePerimeter(buffered),mergedPerimeter=SpacePerimeter(merged);
        const auto difference=static_cast<long double>(Cached->FixedPerimeter)+addedPerimeter-mergedPerimeter;
        // Disconnected rounded polygons have exactly zero shared boundary,
        // but independently rounded perimeter sums can leave a few positive
        // ULPs after subtraction. Remove only scale-relative arithmetic noise;
        // no geometric gap/tolerance or material acceptance rule is relaxed.
        const auto roundoff=32*std::numeric_limits<double>::epsilon()*
            (static_cast<long double>(Cached->FixedPerimeter)+addedPerimeter+mergedPerimeter);
        result.Attachment=difference>roundoff?double(difference/2):0.;
        ClipperD engine(SpacePrecision);engine.AddSubject(Cached->Domain);engine.AddClip(merged);
        PolyTreeD free;
        if(!engine.Execute(ClipType::Difference,FillRule::NonZero,free)||engine.ErrorCode())
            throw std::runtime_error("Local-space scoring remaining-domain operation failed.");
        result.FreePerimeter=SpacePerimeter(PolyTreeToPathsD(free));
        std::vector<const PolyPathD*> pending;
        for(std::size_t i=0;i<free.Count();++i)pending.push_back(free.Child(i));
        long double total=0;
        while(!pending.empty())
        {
            const auto* node=pending.back();pending.pop_back();
            for(std::size_t i=0;i<node->Count();++i)pending.push_back(node->Child(i));
            if(node->IsHole()||node->Polygon().size()<3)continue;
            long double net=SpaceRingArea(node->Polygon());
            for(std::size_t i=0;i<node->Count();++i)if(node->Child(i)->IsHole())net-=SpaceRingArea(node->Child(i)->Polygon());
            const auto area=double(std::max(0.L,net));if(area<=0)continue;
            double lx=std::numeric_limits<double>::infinity(),ly=lx,ux=-lx,uy=-lx;
            for(const auto point:node->Polygon())
            {lx=std::min(lx,point.x);ly=std::min(ly,point.y);ux=std::max(ux,point.x);uy=std::max(uy,point.y);}
            const auto envelope=(ux-lx)*(uy-ly);
            if(!std::isfinite(area)||!std::isfinite(envelope)||envelope<=0)
                throw std::runtime_error("Local-space scoring component area overflowed.");
            const auto regular=area*(area/envelope);
            if(area>result.LargestFreeArea||(area==result.LargestFreeArea&&regular>result.LargestFreeRegularArea))
            {result.LargestFreeArea=area;result.LargestFreeRegularArea=regular;}
            total+=area;++result.FreeComponents;
        }
        result.FreeArea=double(total);result.FragmentFreeArea=std::max(0.,result.FreeArea-result.LargestFreeArea);
        if(!std::isfinite(result.FreeArea)||!std::isfinite(result.LargestFreeRegularArea)||!std::isfinite(result.Attachment))
            throw std::runtime_error("Local-space scoring result overflowed.");
        result.Valid=true;
    }
    catch(const std::exception& error){result.Diagnostic=error.what();}
    catch(...){result.Diagnostic="Local-space scoring candidate failed.";}
    return result;
}
}
