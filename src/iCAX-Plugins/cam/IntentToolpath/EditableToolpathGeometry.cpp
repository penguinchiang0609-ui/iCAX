#include "pch.h"
#include "EditableToolpathInternal.h"
#include <bit>
#include <tuple>
#include "GeometryAlgo/GeometryAlgo.h"
#include <numbers>
#include <mutex>

namespace iCAX::CAM::Intent
{
using namespace Editable;
namespace
{
Point3 P(Point2 p){return {p.X,p.Y,0};}
Direction3 D(Direction2 d){return {d.X,d.Y,0};}
Placement3 P(const Placement2& p){Placement3 q;q.Location=P(p.Location);q.XDirection=D(p.XDirection);q.YDirection=D(p.YDirection);return q;}
Curve3 Lift(const Curve2& curve)
{
    return std::visit([](const auto& c)->Curve3{
        using T=std::decay_t<decltype(c)>;
        if constexpr(std::is_same_v<T,Line2>)return Line3{{P(c.Axis.Location),D(c.Axis.Direction)}};
        else if constexpr(std::is_same_v<T,Ray2>)return Ray3{{P(c.Axis.Location),D(c.Axis.Direction)},c.First};
        else if constexpr(std::is_same_v<T,Segment2>)return Segment3{P(c.Start),P(c.End)};
        else if constexpr(std::is_same_v<T,Circle2>)return Circle3{P(c.Placement),c.Radius};
        else if constexpr(std::is_same_v<T,Arc2>)return Arc3{{P(c.Basis.Placement),c.Basis.Radius},c.StartAngle,c.EndAngle,c.CounterClockwise};
        else if constexpr(std::is_same_v<T,Ellipse2>)return Ellipse3{P(c.Placement),c.MajorRadius,c.MinorRadius};
        else if constexpr(std::is_same_v<T,EllipseArc2>)return EllipseArc3{{P(c.Basis.Placement),c.Basis.MajorRadius,c.Basis.MinorRadius},c.StartAngle,c.EndAngle,c.CounterClockwise};
        else if constexpr(std::is_same_v<T,Polyline2>){Polyline3 q;q.Closed=c.Closed;for(auto p:c.Points)q.Points.push_back(P(p));return q;}
        else if constexpr(std::is_same_v<T,Bezier2>){Bezier3 q;for(auto p:c.Poles)q.Poles.push_back(P(p));return q;}
        else if constexpr(std::is_same_v<T,BSpline2>){BSpline3 q;q.Degree=c.Degree;q.Knots=c.Knots;q.Multiplicities=c.Multiplicities;q.Periodic=c.Periodic;for(auto p:c.Poles)q.Poles.push_back(P(p));return q;}
        else if constexpr(std::is_same_v<T,NURBS2>){NURBS3 q;q.Degree=c.Degree;q.Knots=c.Knots;q.Multiplicities=c.Multiplicities;q.Weights=c.Weights;q.Periodic=c.Periodic;for(auto p:c.Poles)q.Poles.push_back(P(p));return q;}
        else return Clothoid3{P(c.Placement),c.StartCurvature,c.EndCurvature,c.Length};
    },curve);
}
void ValidatePlacement(const Placement3& p)
{
    Require(GeometryAlgo::IsValid(p.Location),"TOOLPATH_PLACEMENT_NONFINITE");
    const auto dot=[](Direction3 a,Direction3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;};
    for(const auto d:{p.XDirection,p.YDirection,p.ZDirection})Require(std::isfinite(dot(d,d))&&std::abs(dot(d,d)-1.)<=1e-10,"TOOLPATH_PLACEMENT_NOT_UNIT");
    Require(std::abs(dot(p.XDirection,p.YDirection))<=1e-10&&std::abs(dot(p.XDirection,p.ZDirection))<=1e-10&&std::abs(dot(p.YDirection,p.ZDirection))<=1e-10,"TOOLPATH_PLACEMENT_NOT_ORTHOGONAL");
}
template<class T> void ValidateCurve(const T& c)
{
    if constexpr(requires{c.Placement;})ValidatePlacement(c.Placement);
    if constexpr(requires{c.Axis;})
    {Require(GeometryAlgo::IsValid(c.Axis.Location),"TOOLPATH_AXIS_NONFINITE");const auto& d=c.Axis.Direction;Require(std::isfinite(d.X)&&std::isfinite(d.Y)&&std::isfinite(d.Z)&&std::abs(std::hypot(d.X,d.Y,d.Z)-1.)<=1e-10,"TOOLPATH_AXIS_NOT_UNIT");}
    if constexpr(requires{c.Radius;})Require(std::isfinite(c.Radius)&&c.Radius>0,"TOOLPATH_RADIUS_INVALID");
    if constexpr(requires{c.MajorRadius;c.MinorRadius;})Require(std::isfinite(c.MajorRadius)&&std::isfinite(c.MinorRadius)&&c.MinorRadius>0&&c.MajorRadius>=c.MinorRadius,"TOOLPATH_ELLIPSE_RADII_INVALID");
    if constexpr(requires{c.Basis;})ValidateCurve(c.Basis);
    if constexpr(requires{c.StartAngle;c.EndAngle;})Require(std::isfinite(c.StartAngle)&&std::isfinite(c.EndAngle)&&c.StartAngle!=c.EndAngle,"TOOLPATH_ARC_ANGLES_INVALID");
    if constexpr(requires{c.Start;c.End;})Require(GeometryAlgo::IsValid(c.Start)&&GeometryAlgo::IsValid(c.End)&&Distance(c.Start,c.End)>0,"TOOLPATH_SEGMENT_DEGENERATE");
    if constexpr(requires{c.Points;}){Require(c.Points.size()>=2,"TOOLPATH_POLYLINE_EMPTY");for(const auto p:c.Points)Require(GeometryAlgo::IsValid(p),"TOOLPATH_POINT_NONFINITE");}
    if constexpr(requires{c.Poles;}){Require(c.Poles.size()>=2,"TOOLPATH_CONTROL_POINTS_EMPTY");for(const auto p:c.Poles)Require(GeometryAlgo::IsValid(p),"TOOLPATH_CONTROL_POINT_NONFINITE");}
    if constexpr(requires{c.StartCurvature;c.EndCurvature;c.Length;})Require(std::isfinite(c.StartCurvature)&&std::isfinite(c.EndCurvature)&&std::isfinite(c.Length)&&c.Length>0,"TOOLPATH_CLOTHOID_INVALID");
    if constexpr(std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>){std::string error;const auto valid=GeometryAlgo::Validate(c,error);Require(valid,"TOOLPATH_SPLINE_INVALID: "+error);}
}
void ValidateSurface(const Surface3& value)
{
    std::visit([](const auto& surface)
    {
        using T=std::decay_t<decltype(surface)>;
        if constexpr(requires{surface.Placement;})ValidatePlacement(surface.Placement);
        if constexpr(requires{surface.Radius;})Require(std::isfinite(surface.Radius)&&(std::is_same_v<T,ConicalSurface3>?surface.Radius>=0:surface.Radius>0),"TOOLPATH_SURFACE_RADIUS_INVALID");
        if constexpr(requires{surface.MajorRadius;})Require(std::isfinite(surface.MajorRadius)&&std::isfinite(surface.MinorRadius)&&surface.MajorRadius>0&&surface.MinorRadius>0,"TOOLPATH_TORUS_RADII_INVALID");
        if constexpr(std::is_same_v<T,ConicalSurface3>)Require(std::isfinite(surface.SemiAngle)&&std::abs(surface.SemiAngle)>0&&std::abs(surface.SemiAngle)<std::numbers::pi/2,"TOOLPATH_CONE_ANGLE_INVALID");
        if constexpr(std::is_same_v<T,BSplineSurface3>){std::string error;Require(GeometryAlgo::Validate(surface,error),"TOOLPATH_SPLINE_SURFACE_INVALID: "+error);}
        if constexpr(requires{surface.Basis;})std::visit([](const auto& basis){ValidateCurve(basis);},surface.Basis);
        if constexpr(std::is_same_v<T,RevolutionSurface3>)
        {Require(GeometryAlgo::IsValid(surface.Axis.Location),"TOOLPATH_AXIS_NONFINITE");const auto direction=surface.Axis.Direction;Require(std::isfinite(direction.X)&&std::isfinite(direction.Y)&&std::isfinite(direction.Z)&&std::abs(std::hypot(direction.X,direction.Y,direction.Z)-1.)<=1e-10,"TOOLPATH_AXIS_NOT_UNIT");}
        if constexpr(std::is_same_v<T,ExtrusionSurface3>)
        {const auto direction=surface.Direction;Require(std::isfinite(direction.X)&&std::isfinite(direction.Y)&&std::isfinite(direction.Z)&&std::abs(std::hypot(direction.X,direction.Y,direction.Z)-1.)<=1e-10,"TOOLPATH_AXIS_NOT_UNIT");}
    },value);
}
void ValidateNativeRange(const Curve3& curve,const ParameterRange& range)
{
    Require(std::isfinite(range.First)&&std::isfinite(range.Last)&&range.Last>range.First,"TOOLPATH_NATIVE_RANGE_INVALID");
    std::visit([&](const auto& value)
    {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,Segment3>||std::is_same_v<T,Arc3>||std::is_same_v<T,EllipseArc3>||std::is_same_v<T,Bezier3>||std::is_same_v<T,Polyline3>)
            Require(range.First>=0&&range.Last<=1,"TOOLPATH_RANGE_OUTSIDE_CURVE_DOMAIN");
        else if constexpr(std::is_same_v<T,Ray3>)Require(std::isfinite(value.First)&&range.First>=0,"TOOLPATH_RANGE_OUTSIDE_CURVE_DOMAIN");
        else if constexpr(std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>)
            if(!value.Periodic)Require(!value.Knots.empty()&&range.First>=value.Knots.front()&&range.Last<=value.Knots.back(),"TOOLPATH_RANGE_OUTSIDE_CURVE_DOMAIN");
    },curve);
}
void ValidateOffset(const DirectionalCurveOffset3& offset)
{
    Require(std::isfinite(offset.SignedDistance),"TOOLPATH_OFFSET_DISTANCE_INVALID");
    std::visit([](const auto& direction)
    {
        using T=std::decay_t<decltype(direction)>;
        if constexpr(std::is_same_v<T,Direction3>)Require(std::isfinite(direction.X)&&std::isfinite(direction.Y)&&std::isfinite(direction.Z)&&std::isfinite(std::hypot(direction.X,direction.Y,direction.Z))&&std::hypot(direction.X,direction.Y,direction.Z)>0,"TOOLPATH_OFFSET_DIRECTION_INVALID");
        else if constexpr(std::is_same_v<T,ParametricVectorField3>)std::visit([](const auto& curve){ValidateCurve(curve);},direction.Coefficients);
        else
        {ValidateSurface(direction.Surface);std::visit([](const auto& curve){ValidateCurve(curve);},Lift(direction.Parameters));Require(std::isfinite(direction.Orientation)&&direction.Orientation!=0,"TOOLPATH_OFFSET_DIRECTION_INVALID");}
    },offset.ReferenceDirection);
}
class CGeometry final:public IToolpathGeometryServices
{
    std::shared_ptr<const GeometryAdapter::IGeometryKernelAdapter> m_Adapter;
    Point3 Eval(const Curve3& curve,double t)const
    {
        if(const auto* e=std::get_if<Ellipse3>(&curve))return Local(e->Placement,e->MajorRadius*std::cos(t),e->MinorRadius*std::sin(t));
        if(const auto* e=std::get_if<EllipseArc3>(&curve))
        {const auto sweep=e->EndAngle-e->StartAngle;return Eval(Curve3(e->Basis),e->StartAngle+(e->CounterClockwise?1:-1)*t*sweep);}
        if(const auto* b=std::get_if<Bezier3>(&curve))
        {Require(!b->Poles.empty(),"TOOLPATH_BEZIER_EMPTY");auto p=b->Poles;for(std::size_t n=p.size();n>1;--n)for(std::size_t i=0;i<n-1;++i)p[i]={p[i].X*(1-t)+p[i+1].X*t,p[i].Y*(1-t)+p[i+1].Y*t,p[i].Z*(1-t)+p[i+1].Z*t};return p[0];}
        Point3 p;std::string error;
        if(!GeometryAlgo::TryEvaluate(curve,t,p,error,m_Adapter.get()))throw std::invalid_argument("TOOLPATH_UNSUPPORTED_CURVE: "+error);
        return p;
    }
public:
    explicit CGeometry(std::shared_ptr<const GeometryAdapter::IGeometryKernelAdapter> a):m_Adapter(std::move(a)){}
    Point3 Evaluate(const SEditableGeometry& g,const SEditableSegment& s,double t)const override
    {
        Require(std::isfinite(t),"TOOLPATH_PARAMETER_NONFINITE");
        Require(s.Parameterization=="Canonical"||s.Parameterization=="CadNative","TOOLPATH_PARAMETERIZATION_UNSUPPORTED");
        Require(s.Parameterization=="Canonical","TOOLPATH_UNSUPPORTED_CURVE: CadNative parameterization requires its declared geometry backend");
        Require(!std::holds_alternative<DirectionalCurveOffset3>(s.Curve),"TOOLPATH_UNSUPPORTED_CURVE: DirectionalCurveOffset3 requires an exact offset evaluator");
        if(g.Authority==EGeometryAuthority::Spatial3D)return Eval(std::get<CurveSegment3>(s.Curve).Curve,t);
        const auto p=Eval(Lift(std::get<CurveSegment2>(s.Curve).Curve),t);
        if(g.Authority==EGeometryAuthority::Planar2D)return Local(g.Plane,p.X,p.Y);
        Require(g.SupportSurface.has_value(),"TOOLPATH_UV_SUPPORT_REQUIRED");
        return std::visit([&](const auto& surface)->Point3{
            using T=std::decay_t<decltype(surface)>;
            if constexpr(std::is_same_v<T,PlaneSurface3>)return Local(surface.Placement,p.X,p.Y);
            else if constexpr(std::is_same_v<T,CylindricalSurface3>)return Local(surface.Placement,surface.Radius*std::cos(p.X),surface.Radius*std::sin(p.X),p.Y);
            else if constexpr(std::is_same_v<T,ConicalSurface3>){const auto r=surface.Radius+p.Y*std::sin(surface.SemiAngle);return Local(surface.Placement,r*std::cos(p.X),r*std::sin(p.X),p.Y*std::cos(surface.SemiAngle));}
            else if constexpr(std::is_same_v<T,SphericalSurface3>)return Local(surface.Placement,surface.Radius*std::cos(p.Y)*std::cos(p.X),surface.Radius*std::cos(p.Y)*std::sin(p.X),surface.Radius*std::sin(p.Y));
            else if constexpr(std::is_same_v<T,ToroidalSurface3>){const auto r=surface.MajorRadius+surface.MinorRadius*std::cos(p.Y);return Local(surface.Placement,r*std::cos(p.X),r*std::sin(p.X),surface.MinorRadius*std::sin(p.Y));}
            else throw std::invalid_argument("TOOLPATH_UNSUPPORTED_SURFACE_EVALUATOR");
        },*g.SupportSurface);
    }
    void Validate(const SEditableGeometry& g)const override
    {
        Require(!g.FrameId.empty()&&!g.Segments.empty()&&g.Segments.size()<=1000000,"TOOLPATH_GEOMETRY_REQUIRED");
        Require(std::isfinite(g.Tolerance)&&g.Tolerance>0,"TOOLPATH_TOLERANCE_INVALID");
        if(g.Authority==EGeometryAuthority::Planar2D)ValidatePlacement(g.Plane);
        Require(g.Authority!=EGeometryAuthority::SurfaceUV||g.SupportSurface.has_value(),"TOOLPATH_UV_SUPPORT_REQUIRED");
        if(g.SupportSurface)ValidateSurface(*g.SupportSurface);
        std::set<std::string> ids;
        for(const auto& s:g.Segments)
        {
            Require(!s.Id.empty()&&ids.insert(s.Id).second,"TOOLPATH_SEGMENT_ID_DUPLICATE");
            Require(s.Parameterization=="Canonical"||s.Parameterization=="CadNative","TOOLPATH_PARAMETERIZATION_UNSUPPORTED");
            Require((g.Authority==EGeometryAuthority::Spatial3D)!=std::holds_alternative<CurveSegment2>(s.Curve),"TOOLPATH_AUTHORITY_DIMENSION");
            const auto* offset=std::get_if<DirectionalCurveOffset3>(&s.Curve);
            const auto curve=offset?offset->Source.Curve:g.Authority==EGeometryAuthority::Spatial3D?std::get<CurveSegment3>(s.Curve).Curve:Lift(std::get<CurveSegment2>(s.Curve).Curve);
            std::visit([](const auto& c){ValidateCurve(c);},curve);
            const auto r=Range(s);Require(std::isfinite(r.First)&&std::isfinite(r.Last)&&r.Last>r.First,"TOOLPATH_NATIVE_RANGE_INVALID");
            if(s.Parameterization=="Canonical"&&!offset)ValidateNativeRange(curve,r);
            if(offset)ValidateOffset(*offset);
            for(const auto& source:s.Sources)
            {
                Require(std::isfinite(source.ErrorBound)&&source.ErrorBound>=0&&(source.Precision=="Exact"||source.Precision=="BoundedApproximation"),"TOOLPATH_MAPPING_ERROR_INVALID");
                for(const auto& range:{source.SourceRange,source.OutputRange})Require(std::isfinite(range.First)&&std::isfinite(range.Last)&&range.Last>range.First,"TOOLPATH_MAPPING_RANGE_INVALID");
                Require(source.OutputRange.First>=r.First-1e-12&&source.OutputRange.Last<=r.Last+1e-12,"TOOLPATH_MAPPING_OUTSIDE_SEGMENT");
            }
        }
        std::optional<Point3> previous;Point3 first;
        for(const auto& s:g.Segments)
        {
            const auto r=Range(s);const auto a=Evaluate(g,s,First(r)),b=Evaluate(g,s,Last(r));
            Require(GeometryAlgo::IsValid(a)&&GeometryAlgo::IsValid(b),"TOOLPATH_POINT_NONFINITE");
            if(previous)Require(Distance(*previous,a)<=g.Tolerance,"TOOLPATH_CHAIN_DISCONNECTED");else first=a;
            previous=b;
        }
        if(g.Closed)Require(Distance(first,*previous)<=g.Tolerance,"TOOLPATH_CLOSURE_INVALID");
    }
    void ValidateManufacturing(const std::string& type,const SOperationEvaluation& in,const SOperationCandidate& result)const override
    {
        if(type!="icax.lead.linear")return;
        Require(in.Context.contains("scrapHalfSpace"),"TOOLPATH_LEAD_MATERIAL_CONTEXT_REQUIRED");
        const auto half=MapAt(in.Context,"scrapHalfSpace");const auto point=ArrayAt(half,"point"),normal=ArrayAt(half,"normal");
        Require(point.size()==3&&normal.size()==3,"TOOLPATH_HALFSPACE_INVALID");
        const Point3 q{NumericValue(point[0]),NumericValue(point[1]),NumericValue(point[2])};
        const Vector3 n{NumericValue(normal[0]),NumericValue(normal[1]),NumericValue(normal[2])};
        Require(GeometryAlgo::IsValid(q)&&std::isfinite(n.X)&&std::isfinite(n.Y)&&std::isfinite(n.Z)&&std::hypot(n.X,n.Y,n.Z)>0,"TOOLPATH_HALFSPACE_INVALID");
        for(const auto& output:result.Outputs)for(const auto& segment:output.Value.Geometry.Segments)if(segment.ConstructionRole=="Lead")
        {
            const auto r=Range(segment);for(const auto t:{r.First,r.Last})
            {const auto p=Evaluate(output.Value.Geometry,segment,t);Require(((p.X-q.X)*n.X+(p.Y-q.Y)*n.Y+(p.Z-q.Z)*n.Z)/std::hypot(n.X,n.Y,n.Z)>=-output.Value.Geometry.Tolerance,"TOOLPATH_LEAD_ENTERS_RETAINED_MATERIAL");}
        }
    }
};
}
std::shared_ptr<const IToolpathGeometryServices> CreateToolpathGeometryServices(std::shared_ptr<const GeometryAdapter::IGeometryKernelAdapter> adapter)
{return std::make_shared<CGeometry>(std::move(adapter));}
}

namespace iCAX::CAM::Intent::Editable
{
double Locate(const SPathAnchor& anchor,const SFrozenPathInput& input)
{
    std::vector<double> positions;
    const auto& segments=input.Value.Geometry.Segments;
    for(std::size_t i=0;i<segments.size();++i)
    {
        const auto& s=segments[i];const auto r=Range(s);
        const auto add=[&](double t){if(t>=r.First-1e-12&&t<=r.Last+1e-12)positions.push_back(i+(t-First(r))/(Last(r)-First(r)));};
        if(anchor.Port==input.Binding.Source&&anchor.SegmentId==s.Id)add(anchor.Parameter);
        else for(const auto& source:s.Sources)if(source.Source==anchor.Port&&source.SourceSegmentId==anchor.SegmentId&&source.Occurrence==anchor.Occurrence)
        {
            Require(source.Precision=="Exact","TOOLPATH_NEEDS_REBIND_MAPPING_PRECISION");
            if(anchor.Parameter>=source.SourceRange.First-1e-12&&anchor.Parameter<=source.SourceRange.Last+1e-12)
            {const auto fraction=(anchor.Parameter-First(source.SourceRange))/(Last(source.SourceRange)-First(source.SourceRange));add(First(source.OutputRange)+fraction*(Last(source.OutputRange)-First(source.OutputRange)));}
        }
    }
    std::sort(positions.begin(),positions.end());positions.erase(std::unique(positions.begin(),positions.end(),[](auto a,auto b){return std::abs(a-b)<1e-11;}),positions.end());
    if(input.Value.Geometry.Closed&&positions.size()==2&&positions.front()<1e-11&&std::abs(positions.back()-segments.size())<1e-11)
    {if(anchor.Side=="After"||anchor.Side=="Start")positions.pop_back();else if(anchor.Side=="Before"||anchor.Side=="End")positions.erase(positions.begin());}
    Require(positions.size()==1,"TOOLPATH_NEEDS_REBIND_ANCHOR");return positions.front();
}
SPathAnchor AnchorAt(const SFrozenPathInput& input,double pos)
{
    const auto n=input.Value.Geometry.Segments.size();Require(pos>=0&&pos<=n,"TOOLPATH_POSITION_OUTSIDE");
    const auto i=pos==n?n-1:static_cast<std::size_t>(pos);const auto& s=input.Value.Geometry.Segments[i];const auto r=Range(s);
    return {input.Binding.Source,s.Id,First(r)+(pos-i)*(Last(r)-First(r)),0,pos==0?"Start":pos==n?"End":"",ToolpathFingerprint(input.Value,{"geometry","topology","traversal","frame"})};
}
SPathRange NormalizeRange(SPathRange range,const SFrozenPathInput& input)
{
    if(!range.ExactSpans.empty())return range;
    auto begin=Locate(range.Start,input),end=Locate(range.End,input);const auto n=static_cast<double>(input.Value.Geometry.Segments.size());
    if(range.TraversalDirection<0)std::swap(begin,end);
    if(range.WrapsSeam){Require(input.Value.Geometry.Closed&&begin>end,"TOOLPATH_RANGE_SEAM_INVALID");end+=n;}
    Require(end>begin,"TOOLPATH_RANGE_DIRECTION_INVALID");
    for(auto cursor=begin;cursor<end-1e-12;)
    {
        const auto ordinal=static_cast<std::size_t>(std::floor(cursor));const auto index=ordinal%input.Value.Geometry.Segments.size();const auto stop=std::min(end,std::floor(cursor)+1.);
        const auto& s=input.Value.Geometry.Segments[index];const auto r=Range(s);const auto a=First(r)+(cursor-std::floor(cursor))*(Last(r)-First(r)),b=First(r)+(stop-std::floor(cursor))*(Last(r)-First(r));
        range.ExactSpans.push_back({input.Binding.Source,s.Id,{std::min(a,b),std::max(a,b),a>b},0});cursor=stop;
    }
    return range;
}
std::vector<std::pair<double,double>> RangeWindows(const SPathRange& range,const SFrozenPathInput& input)
{
    const auto selected=range.ExactSpans.empty()?NormalizeRange(range,input):range;std::vector<std::pair<double,double>> windows;
    for(const auto& span:selected.ExactSpans)
    {
        Require(std::isfinite(span.Range.First)&&std::isfinite(span.Range.Last)&&span.Range.Last>span.Range.First,"TOOLPATH_SELECTED_SPAN_INVALID");
        std::vector<std::pair<double,double>> sourceCoverage;
        for(std::size_t i=0;i<input.Value.Geometry.Segments.size();++i)
        {
            const auto& segment=input.Value.Geometry.Segments[i];const auto r=Range(segment);
            const auto add=[&](ParameterRange source,ParameterRange output)
            {
                const auto lo=std::max(source.First,span.Range.First),hi=std::min(source.Last,span.Range.Last);if(hi<=lo+1e-12)return;
                const auto parameter=[&](double u){return First(output)+(u-First(source))/(Last(source)-First(source))*(Last(output)-First(output));};
                const auto a=parameter(lo),b=parameter(hi);const auto position=[&](double t){return i+(t-First(r))/(Last(r)-First(r));};
                windows.emplace_back(std::min(position(a),position(b)),std::max(position(a),position(b)));sourceCoverage.emplace_back(lo,hi);
            };
            if(span.Port==input.Binding.Source&&span.SegmentId==segment.Id)add(r,r);
            else for(const auto& mapping:segment.Sources)if(mapping.Source==span.Port&&mapping.SourceSegmentId==span.SegmentId&&mapping.Occurrence==span.Occurrence)
            {Require(mapping.Precision=="Exact","TOOLPATH_RANGE_MAPPING_PRECISION_UNSUPPORTED");add(mapping.SourceRange,mapping.OutputRange);}
        }
        std::sort(sourceCoverage.begin(),sourceCoverage.end());double cursor=span.Range.First;
        for(const auto& [a,b]:sourceCoverage){Require(a<=cursor+1e-12,"TOOLPATH_NEEDS_REBIND_SPAN_LOSS");cursor=std::max(cursor,b);}
        Require(cursor>=span.Range.Last-1e-12,"TOOLPATH_NEEDS_REBIND_SPAN_LOSS");
    }
    std::sort(windows.begin(),windows.end());std::vector<std::pair<double,double>> merged;
    for(const auto& [a,b]:windows){if(!merged.empty()&&a<=merged.back().second+1e-12)merged.back().second=std::max(merged.back().second,b);else merged.emplace_back(a,b);}return merged;
}
SEditableGeometry Slice(const SFrozenPathInput& input,double begin,double end,const std::string& role)
{
    const auto& original=input.Value.Geometry;auto g=original;g.Segments.clear();g.Closed=false;
    const auto sourceGeometryFingerprint=ToolpathFingerprint(input.Value,{"geometry","topology","traversal","frame"});
    const auto n=original.Segments.size();Require(begin>=0&&end>begin&&end<=2*n&&end-begin<=n+1e-11,"TOOLPATH_SLICE_RANGE");
    for(double cursor=begin;cursor<end-1e-12;)
    {
        const auto occurrence=static_cast<std::size_t>(std::floor(cursor));const auto index=occurrence%n;
        const auto stop=std::min(end,std::floor(cursor)+1.);auto s=original.Segments[index];const auto old=Range(s);
        const double a=First(old)+(cursor-std::floor(cursor))*(Last(old)-First(old));
        const double b=First(old)+(stop-std::floor(cursor))*(Last(old)-First(old));
        ParameterRange r{std::min(a,b),std::max(a,b),a>b};SetRange(s,r);
        if(r.First!=old.First||r.Last!=old.Last)s.Id+="@"+Hash(Map{{"role",role},{"part",std::string(occurrence>=n?"wrapped":"main")}});
        std::vector<SSpanMapping> mappings;
        for(auto source:s.Sources)
        {
            const auto lo=std::max(r.First,source.OutputRange.First),hi=std::min(r.Last,source.OutputRange.Last);if(hi<=lo+1e-12)continue;
            const ParameterRange overlap{lo,hi,r.Reversed};
            const auto mapped=[&](double t){return First(source.SourceRange)+(t-First(source.OutputRange))/(Last(source.OutputRange)-First(source.OutputRange))*(Last(source.SourceRange)-First(source.SourceRange));};
            const auto u=mapped(First(overlap)),v=mapped(Last(overlap));source.SourceRange={std::min(u,v),std::max(u,v),u>v};source.OutputRange=overlap;mappings.push_back(std::move(source));
        }
        s.Sources=std::move(mappings);
        s.Sources.push_back({input.Binding.Source,original.Segments[index].Id,r,r,"Exact",0,0,sourceGeometryFingerprint});
        g.Segments.push_back(std::move(s));cursor=stop;
    }
    return g;
}
void ValidateCandidate(const SOperationEvaluation& in,const SOperationCandidate& candidate,const IToolpathGeometryServices& geometry)
{
    Require(!candidate.Outputs.empty()||candidate.IntentionalEmpty,"TOOLPATH_UNDECLARED_EMPTY_RESULT");
    std::set<std::string> roles;std::set<std::size_t> inheritedInputs;
    for(const auto& output:candidate.Outputs)
    {
        Require(!output.RoleKey.empty()&&roles.insert(output.RoleKey).second,"TOOLPATH_OUTPUT_ROLE_DUPLICATE");geometry.Validate(output.Value.Geometry);
        if(!in.Inputs.empty())for(const auto& segment:output.Value.Geometry.Segments)Require(!segment.Sources.empty()||!segment.ConstructionRole.empty(),"TOOLPATH_CONSTRUCTED_SOURCE_REQUIRED");
        if(output.InheritInput)Require(*output.InheritInput<in.Inputs.size()&&in.Inputs[*output.InheritInput].Binding.Replace
            &&std::count_if(in.Inputs.begin(),in.Inputs.end(),[](const auto& input){return input.Binding.Replace;})==1
            &&inheritedInputs.insert(*output.InheritInput).second,"TOOLPATH_IDENTITY_INHERITANCE_INVALID");
    }
    for(const auto& input:in.Inputs)if(input.Binding.Replace)
    {
        std::vector<std::pair<double,double>> covered;
        const auto currentGeometry=ToolpathFingerprint(input.Value,{"geometry","topology","traversal","frame"});
        std::map<std::string,std::pair<std::size_t,ParameterRange>> directSegments;
        for(std::size_t i=0;i<input.Value.Geometry.Segments.size();++i)directSegments.emplace(input.Value.Geometry.Segments[i].Id,std::pair{i,Range(input.Value.Geometry.Segments[i])});
        for(const auto& output:candidate.Outputs)for(const auto& segment:output.Value.Geometry.Segments)
        {
          // This identity is local to validation, not a persisted fingerprint.
          // Compare its exact fields instead of serializing and hashing every
          // source span. Bit keys preserve the legacy distinction of -0/+0.
          std::set<std::tuple<std::string,std::uint64_t,std::uint64_t,bool>> seen;
          for(const auto& s:segment.Sources)
            if(s.Source==input.Binding.Source&&(s.SourceGeometryFingerprint.empty()||s.SourceGeometryFingerprint==currentGeometry))
            {
                const auto direct=directSegments.find(s.SourceSegmentId);if(direct==directSegments.end())continue;
                const auto identity=std::make_tuple(s.SourceSegmentId,std::bit_cast<std::uint64_t>(s.SourceRange.First),std::bit_cast<std::uint64_t>(s.SourceRange.Last),s.SourceRange.Reversed);
                if(!seen.insert(identity).second)continue;
                const auto [index,r]=direct->second;
                if(!(s.SourceRange.First>=r.First-1e-12&&s.SourceRange.Last<=r.Last+1e-12))Require(false,"TOOLPATH_SOURCE_MAP_OUTSIDE_INPUT: "+segment.Id+" <- "+s.SourceSegmentId+" ["+std::to_string(s.SourceRange.First)+","+std::to_string(s.SourceRange.Last)+"] expected ["+std::to_string(r.First)+","+std::to_string(r.Last)+"]");
                const auto position=[&](double t){return index+(t-First(r))/(Last(r)-First(r));};
                const auto start=position(s.SourceRange.First),end=position(s.SourceRange.Last);
                covered.emplace_back(std::min(start,end),std::max(start,end));
            }
        }
        for(const auto& r:candidate.ExcludedRanges)if(r.Start.Port==input.Binding.Source)
        {const auto windows=RangeWindows(r,input);covered.insert(covered.end(),windows.begin(),windows.end());}
        std::sort(covered.begin(),covered.end());double cursor=0;
        for(const auto [a,b]:covered){Require(a<=cursor+1e-10,"TOOLPATH_UNDECLARED_INPUT_LOSS");Require(candidate.AllowRepeatedCoverage||a>=cursor-1e-10,"TOOLPATH_UNDECLARED_REPEATED_COVERAGE");cursor=std::max(cursor,b);}
        Require(cursor>=input.Value.Geometry.Segments.size()-1e-10,"TOOLPATH_UNDECLARED_INPUT_LOSS");
    }
}
}
