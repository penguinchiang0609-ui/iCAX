#include "pch.h"
#include "CurveEditingService.h"
#include "OpenCascadeBRepBuilder.h"
#include "BoundaryJunctions.h"
#include "Services/ServicesHelper.h"
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <NCollection_Array1.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace
{
    using namespace iCAX::Geometry;
    class CCurveEditingService final : public ICurveEditingService
    {
        AUTO_REGIST_SERVICE(ICurveEditingService,CCurveEditingService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        SCurveMeasure Measure(const iCAX::GeometryData::CurveSegment3& segment) const override
        {
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);const auto& r=segment.Range;
            if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(curve,r))throw std::invalid_argument("CURVE_MEASURE_INVALID_SOURCE");
            GeomAdaptor_Curve adaptor(curve,r.First,r.Last);SCurveMeasure out;
            out.LengthMm=GCPnts_AbscissaPoint::Length(adaptor,r.First,r.Last,1e-9);
            const auto tangent=[&](double u){gp_Pnt p;gp_Vec t;curve->D1(u,p,t);if(t.SquareMagnitude()<1e-20)throw std::invalid_argument("CURVE_MEASURE_TANGENT_UNDEFINED");t.Normalize();if(r.Reversed)t.Reverse();return std::array<double,3>{t.X(),t.Y(),t.Z()};};
            out.FirstTangent=tangent(r.Reversed?r.Last:r.First);out.LastTangent=tangent(r.Reversed?r.First:r.Last);
            const GeomAdaptor_Curve* smooth=&adaptor;
            std::size_t budget=0;const auto curvature=[&](double u){
                if(++budget>16384)throw std::invalid_argument("CURVE_MEASURE_BUDGET");
                gp_Pnt p;gp_Vec d1,d2;smooth->D2(u,p,d1,d2);const auto speed2=d1.SquareMagnitude();
                if(speed2<1e-20)throw std::invalid_argument("CURVE_MEASURE_TANGENT_UNDEFINED");
                const double value=d1.Crossed(d2).Magnitude()/speed2;if(!std::isfinite(value))throw std::invalid_argument("CURVE_MEASURE_INVALID_SOURCE");return value;
            };
            // Allocate the error to the whole interval, refining the largest
            // contributor first. Dividing tolerance at every recursion level
            // can reject a tiny local contribution already below the required
            // total accuracy (especially when a keypoint trims near a knot).
            struct Panel{double A,B,Fa,Fm,Fb,Fl,Fh,Value,Error;int Depth;};
            const auto panel=[&](double a,double b,double fa,double fm,double fb,int depth){
                const auto m=(a+b)/2,l=curvature((a+m)/2),h=curvature((m+b)/2);
                const auto coarse=(b-a)*(fa+4*fm+fb)/6,fine=(m-a)*(fa+4*l+fm)/6+(b-m)*(fm+4*h+fb)/6;
                return Panel{a,b,fa,fm,fb,l,h,fine+(fine-coarse)/15,std::abs(fine-coarse)/15,depth};
            };
            const auto integrate=[&](double a,double b,double eps){
                const auto less=[](const Panel& x,const Panel& y){return x.Error<y.Error;};
                std::priority_queue<Panel,std::vector<Panel>,decltype(less)> pending(less);double value=0,error=0;
                // Several initial panels avoid accidental coarse/fine agreement
                // through cancellation on symmetric rational curve spans.
                for(int i=0;i<4;++i){const auto lo=a+(b-a)*i/4,hi=a+(b-a)*(i+1)/4;
                    auto p=panel(lo,hi,curvature(lo),curvature((lo+hi)/2),curvature(hi),0);pending.push(p);value+=p.Value;error+=p.Error;}
                while(error>eps){const auto p=pending.top();pending.pop();const auto m=(p.A+p.B)/2;
                    if(p.Depth>=32||m==p.A||m==p.B)throw std::invalid_argument("CURVE_MEASURE_UNRESOLVED");
                    const auto l=panel(p.A,m,p.Fa,p.Fl,p.Fm,p.Depth+1),h=panel(m,p.B,p.Fm,p.Fh,p.Fb,p.Depth+1);
                    value+=l.Value+h.Value-p.Value;error=std::max(0.,error+l.Error+h.Error-p.Error);pending.push(l);pending.push(h);
                }
                return value;
            };
            // Split at every B-spline knot. C2 continuity only makes curvature
            // continuous: its derivatives can still jump at simple cubic knots,
            // exhausting Simpson recursion when an interval straddles a knot.
            const auto count=adaptor.NbIntervals(GeomAbs_CN);NCollection_Array1<double> bounds(1,count+1);adaptor.Intervals(bounds,GeomAbs_CN);
            gp_Vec previous;bool first=true;
            for(int i=1;i<=count;++i){const auto a=bounds(i),b=bounds(i+1);if(b-a<=1e-14)continue;
                // A trimmed adaptor chooses this smooth interval's endpoint
                // derivatives, including the two sides of polyline corners.
                GeomAdaptor_Curve interval(curve,a,b);smooth=&interval;gp_Pnt p;gp_Vec begin,end;
                interval.D1(a,p,begin);interval.D1(b,p,end);if(begin.SquareMagnitude()<1e-20||end.SquareMagnitude()<1e-20)throw std::invalid_argument("CURVE_MEASURE_TANGENT_UNDEFINED");begin.Normalize();end.Normalize();
                // atan2 stays accurate at tangent-continuous knots, where acos
                // would accumulate artificial corners from dot-product roundoff.
                if(!first)out.TurnRad+=std::atan2(previous.Crossed(begin).Magnitude(),previous.Dot(begin));
                previous=end;first=false;
                out.TurnRad+=integrate(a,b,1e-8/std::max(count,1));
            }
            return out;
        }
        SBoundaryPath JoinBoundary(const iCAX::GeometryData::BRepModel& model,
            const iCAX::GeometryData::CompositeCurve3& source,const std::vector<SBoundaryCurveSpan>& spans,double maximumJunction) const override
        {
            using namespace iCAX::GeometryData;
            if(source.Segments.empty()||source.Segments.size()>200000||source.Segments.size()!=spans.size()||!std::isfinite(maximumJunction)||maximumJunction<=0)
                throw std::invalid_argument("CURVE_BOUNDARY_INVALID_INPUT");
            SBoundaryPath out;out.Curve.Closed=source.Closed;
            const auto endpoint=[](const CurveSegment3& segment,bool end){
                const auto c=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);const auto& r=segment.Range;
                if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(c,r))throw std::invalid_argument("CURVE_BOUNDARY_INVALID_SOURCE: range="+std::to_string(r.First)+","+std::to_string(r.Last)
                    +" domain="+(c.IsNull()?std::string("missing"):std::to_string(c->FirstParameter())+","+std::to_string(c->LastParameter())));
                return c->Value(end!=r.Reversed?r.Last:r.First);
            };
            for(std::size_t i=0;i<source.Segments.size();++i){
                const auto edge=std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==spans[i].EdgeID;});
                if(edge==model.Edges.end()||edge->Curve3Id!=spans[i].CurveID)throw std::invalid_argument("CURVE_BOUNDARY_SOURCE_MISMATCH");
                out.Curve.Segments.push_back(source.Segments[i]);out.SourceSegments.push_back(i);
                if(!source.Closed&&i+1==source.Segments.size())continue;
                const auto next=(i+1)%source.Segments.size();const auto a=endpoint(source.Segments[i],true),b=endpoint(source.Segments[next],false);
                const auto length=a.Distance(b);if(length<=1e-9)continue;
                if(length>maximumJunction||!Detail::SupportsBoundaryJunction(model,spans[i],spans[next],
                    {a.X(),a.Y(),a.Z()},{b.X(),b.Y(),b.Z()},1e-5))throw std::invalid_argument("CURVE_BOUNDARY_JUNCTION_OUTSIDE_TOLERANCE");
                const auto direction=gp_Vec(a,b).Normalized();
                const auto& before=source.Segments[i].Range;const auto& after=source.Segments[next].Range;
                out.Junctions.push_back({out.Curve.Segments.size(),{i,before.Reversed?before.First:before.Last},{next,after.Reversed?after.Last:after.First},length});
                out.Curve.Segments.push_back({Line3{{{a.X(),a.Y(),a.Z()},{direction.X(),direction.Y(),direction.Z()}}},{0,length,false}});
                out.SourceSegments.emplace_back();
            }
            return out;
        }
        SCurveSamples Sample(const iCAX::GeometryData::CompositeCurve3& source,double chord,double step) const override
        {
            if(source.Segments.empty()||source.Segments.size()>200000||!std::isfinite(chord)||chord<=0||!std::isfinite(step)||step<=0)
                throw std::invalid_argument("CURVE_SAMPLE_INVALID_INPUT");
            SCurveSamples out;
            for(std::size_t index=0;index<source.Segments.size();++index){
                const auto& s=source.Segments[index];const auto& r=s.Range;
                const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(s.Curve);
                if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(curve,r))
                    throw std::invalid_argument("CURVE_SAMPLE_INVALID_SOURCE");
                GeomAdaptor_Curve adaptor(curve,r.First,r.Last);
                const double length=GCPnts_AbscissaPoint::Length(adaptor,r.First,r.Last,1e-8);
                if(!std::isfinite(length)||length<=1e-9||length/step>200000)throw std::invalid_argument("CURVE_SAMPLE_BUDGET_EXCEEDED");
                out.SegmentLengthsMm.push_back(length);
                GCPnts_QuasiUniformDeflection deflection(adaptor,chord,r.First,r.Last);
                if(!deflection.IsDone())throw std::runtime_error("CURVE_SAMPLE_FAILED");
                std::vector<double> parameters{r.First,r.Last};
                const auto append=[&](double u){if(u>r.First&&u<r.Last)parameters.push_back(u);};
                for(int n=1;n<=deflection.NbPoints();++n)append(deflection.Parameter(n));
                // Endpoints already satisfy the arc-length step on short trims.
                // Solving for two equal-abscissa points can fail below the
                // solver's tolerance (e.g. neighboring tool-axis anchors).
                // Keep both exact endpoints and the chord-deflection samples.
                if(length>step){
                    GCPnts_UniformAbscissa uniform(adaptor,static_cast<int>(std::ceil(length/step))+1,r.First,r.Last);
                    if(!uniform.IsDone())throw std::runtime_error("CURVE_SAMPLE_FAILED");
                    for(int n=1;n<=uniform.NbPoints();++n)append(uniform.Parameter(n));
                }
                std::sort(parameters.begin(),parameters.end());
                parameters.erase(std::unique(parameters.begin(),parameters.end(),[&](double a,double b){return std::abs(a-b)<1e-12*(r.Last-r.First);}),parameters.end());
                // Deduplication must retain the source anchors, not a nearby
                // numerical approximation returned by either sampler.
                parameters.front()=r.First;parameters.back()=r.Last;
                if(r.Reversed)std::reverse(parameters.begin(),parameters.end());
                if(out.Points.size()+parameters.size()>200000)throw std::invalid_argument("CURVE_SAMPLE_BUDGET_EXCEEDED");
                for(double u:parameters){gp_Pnt p;gp_Vec t;curve->D1(u,p,t);
                    if(t.SquareMagnitude()<1e-20)throw std::invalid_argument("CURVE_SAMPLE_TANGENT_UNDEFINED");
                    t.Normalize();if(r.Reversed)t.Reverse();
                    out.Points.push_back({index,u,{p.X(),p.Y(),p.Z()},{t.X(),t.Y(),t.Z()}});
                }
            }
            return out;
        }
        SCurveTrace TraceClosed(const iCAX::GeometryData::CompositeCurve3& source,
            SCurveLocation start,double distance,bool reverse) const override
        {
            const auto count=source.Segments.size();
            if(!source.Closed||!count||count>200000||start.SegmentIndex>=count||!std::isfinite(distance)||distance<0)
                throw std::invalid_argument("CURVE_TRACE_INVALID_INPUT");
            SCurveTrace out;out.End=start;std::vector<Handle(Geom_Curve)> curves;
            for(const auto& s:source.Segments){
                const auto c=iCAX::OpenCascade::BuildOpenCascadeCurve(s.Curve);const auto& r=s.Range;
                if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(c,r))
                    throw std::invalid_argument("CURVE_TRACE_INVALID_SOURCE");
                GeomAdaptor_Curve a(c,r.First,r.Last);const auto length=GCPnts_AbscissaPoint::Length(a,r.First,r.Last,1e-9);
                if(!std::isfinite(length)||length<=1e-9)throw std::invalid_argument("CURVE_TRACE_DEGENERATE_SOURCE");
                out.PerimeterMm+=length;curves.push_back(c);
            }
            for(std::size_t i=0;i<count;++i){
                const auto j=(i+1)%count;const auto& a=source.Segments[i].Range;const auto& b=source.Segments[j].Range;
                if(curves[i]->Value(a.Reversed?a.First:a.Last).Distance(curves[j]->Value(b.Reversed?b.Last:b.First))>1e-5)
                    throw std::invalid_argument("CURVE_TRACE_DISCONTINUOUS");
            }
            const auto& sr=source.Segments[start.SegmentIndex].Range;
            if(!std::isfinite(start.NativeParameter)||start.NativeParameter<sr.First||start.NativeParameter>sr.Last)
                throw std::invalid_argument("CURVE_TRACE_OUTSIDE_SOURCE");
            auto at=start;double remaining=distance;std::size_t visits=0,points=0;
            const auto p=curves[at.SegmentIndex]->Value(at.NativeParameter);out.Piece.PreviewPoints.push_back({p.X(),p.Y(),p.Z()});
            while(remaining>1e-9){
                if(++visits>200000)throw std::invalid_argument("CURVE_TRACE_BUDGET_EXCEEDED");
                auto segment=source.Segments[at.SegmentIndex];const auto& r=segment.Range;
                const bool backwards=r.Reversed!=reverse;const double bound=backwards?r.First:r.Last,u=at.NativeParameter;
                GeomAdaptor_Curve adaptor(curves[at.SegmentIndex],r.First,r.Last);
                const double available=std::abs(bound-u)<1e-14?0:GCPnts_AbscissaPoint::Length(adaptor,std::min(u,bound),std::max(u,bound),1e-9);
                if(available>1e-9){
                    const double travel=std::min(remaining,available);double v=bound;
                    if(travel<available-1e-9){
                        GCPnts_AbscissaPoint solver(1e-9,adaptor,backwards?-travel:travel,u);
                        if(!solver.IsDone())throw std::runtime_error("CURVE_TRACE_LENGTH_SOLVE_FAILED");v=solver.Parameter();
                        if(v<r.First-1e-10||v>r.Last+1e-10)throw std::runtime_error("CURVE_TRACE_LENGTH_SOLVE_FAILED");
                        v=std::clamp(v,r.First,r.Last);
                    }
                    segment.Range={std::min(u,v),std::max(u,v),backwards};
                    GCPnts_QuasiUniformDeflection samples(adaptor,0.02,segment.Range.First,segment.Range.Last);
                    if(!samples.IsDone())throw std::runtime_error("CURVE_TRACE_PREVIEW_FAILED");
                    points+=samples.NbPoints();if(points>200000)throw std::invalid_argument("CURVE_TRACE_BUDGET_EXCEEDED");
                    for(int n=2;n<=samples.NbPoints();++n){const auto q=samples.Value(backwards?samples.NbPoints()+1-n:n);out.Piece.PreviewPoints.push_back({q.X(),q.Y(),q.Z()});}
                    out.Piece.Curve.Segments.push_back(segment);out.Piece.SourceSegmentIndices.push_back(at.SegmentIndex);
                    out.End={at.SegmentIndex,v};remaining-=travel;if(remaining<=1e-9)break;
                }
                at.SegmentIndex=(at.SegmentIndex+(reverse?count-1:1))%count;
                const auto& next=source.Segments[at.SegmentIndex].Range;at.NativeParameter=next.Reversed!=reverse?next.Last:next.First;
            }
            return out;
        }
        std::vector<SCurvePiece> Split(const iCAX::GeometryData::CompositeCurve3& source,
            const std::vector<SCurveLocation>& locations) const override
        {
            const auto count=source.Segments.size();
            if(!count||count>200000||locations.empty()||locations.size()>257)
                throw std::invalid_argument("CURVE_SPLIT_SELECTION_INVALID");
            std::vector<Handle(Geom_Curve)> curves;
            for(const auto& s:source.Segments) {
                const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(s.Curve);
                const auto& r=s.Range;
                if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(curve,r))
                    throw std::invalid_argument("CURVE_SPLIT_INVALID_SOURCE");
                curves.push_back(curve);
            }
            // Normalized segment position is only an ordering key, never a replacement
            // curve parameter. Shared endpoints and the closed seam have one identity.
            struct Cut {double Position;SCurveLocation Source;};
            std::vector<Cut> cuts;
            for(const auto& location:locations) {
                if(location.SegmentIndex>=count||!std::isfinite(location.NativeParameter))throw std::invalid_argument("CURVE_SPLIT_LOCATION_INVALID");
                const auto& r=source.Segments[location.SegmentIndex].Range;
                if(location.NativeParameter<r.First||location.NativeParameter>r.Last)throw std::invalid_argument("CURVE_SPLIT_OUTSIDE_SOURCE");
                double t=(location.NativeParameter-r.First)/(r.Last-r.First);if(r.Reversed)t=1-t;
                if(t<1e-12)t=0;else if(1-t<1e-12)t=1;
                double key=static_cast<double>(location.SegmentIndex)+t;
                if(source.Closed&&key==count)key=0;
                if(!source.Closed&&(key==0||key==count))throw std::invalid_argument("CURVE_SPLIT_ENDPOINT");
                auto anchor=location;
                if(t==0)anchor.NativeParameter=r.Reversed?r.Last:r.First;
                if(t==1){anchor.SegmentIndex=(location.SegmentIndex+1)%count;const auto& next=source.Segments[anchor.SegmentIndex].Range;anchor.NativeParameter=next.Reversed?next.Last:next.First;}
                cuts.push_back({key,anchor});
            }
            std::sort(cuts.begin(),cuts.end(),[](const Cut& a,const Cut& b){return a.Position<b.Position;});
            for(std::size_t i=1;i<cuts.size();++i)if(cuts[i].Position-cuts[i-1].Position<1e-12)throw std::invalid_argument("CURVE_SPLIT_DUPLICATE_POINT");
            if(source.Closed){auto end=cuts.front();end.Position+=count;cuts.push_back(end);}
            else {
                const auto& first=source.Segments.front().Range;const auto& last=source.Segments.back().Range;
                cuts.insert(cuts.begin(),Cut{0,{0,first.Reversed?first.Last:first.First}});
                cuts.push_back({static_cast<double>(count),{count-1,last.Reversed?last.First:last.Last}});
            }
            std::vector<SCurvePiece> pieces;
            std::size_t pointBudget=0;
            for(std::size_t p=1;p<cuts.size();++p) {
                SCurvePiece piece;double length=0;
                for(double a=cuts[p-1].Position;a<cuts[p].Position;) {
                    const auto occurrence=static_cast<std::size_t>(std::floor(a)),index=occurrence%count;
                    const auto b=std::min(cuts[p].Position,static_cast<double>(occurrence+1));
                    auto segment=source.Segments[index];const auto r=segment.Range;
                    // Copy exact supplied native parameters at the two trim boundaries;
                    // do not reconstruct them from the floating-point ordering key.
                    const auto u=a>occurrence?cuts[p-1].Source.NativeParameter:(r.Reversed?r.Last:r.First);
                    const auto v=b<occurrence+1?cuts[p].Source.NativeParameter:(r.Reversed?r.First:r.Last);
                    segment.Range.First=std::min(u,v);segment.Range.Last=std::max(u,v);
                    if(segment.Range.First>=segment.Range.Last)throw std::invalid_argument("CURVE_SPLIT_DEGENERATE_PIECE");
                    GeomAdaptor_Curve adaptor(curves[index],segment.Range.First,segment.Range.Last);
                    length+=GCPnts_AbscissaPoint::Length(adaptor,segment.Range.First,segment.Range.Last,1e-8);
                    GCPnts_QuasiUniformDeflection samples(adaptor,0.05,segment.Range.First,segment.Range.Last);
                    if(!samples.IsDone())throw std::runtime_error("CURVE_SPLIT_PREVIEW_FAILED");
                    pointBudget+=samples.NbPoints();if(pointBudget>200000)throw std::invalid_argument("CURVE_SPLIT_BUDGET_EXCEEDED");
                    for(int n=1;n<=samples.NbPoints();++n){const auto q=samples.Value(r.Reversed?samples.NbPoints()+1-n:n);piece.PreviewPoints.push_back({q.X(),q.Y(),q.Z()});}
                    piece.Curve.Segments.push_back(std::move(segment));piece.SourceSegmentIndices.push_back(index);a=b;
                }
                if(length<1e-6)throw std::invalid_argument("CURVE_SPLIT_DEGENERATE_PIECE");
                pieces.push_back(std::move(piece));
            }
            return pieces;
        }
    };
}
