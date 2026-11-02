#include "ProcessPlanningService.h"
#include "Services/ServicesHelper.h"
#include "Task/TaskStatus.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace iCAX::CAM::Planning
{
    std::uint32_t GetProcessPlanningContractVersion() noexcept{return 1;}
    namespace
    {
        using namespace iCAX::GeometryData;
        using V=std::array<double,3>;
        constexpr double Tau=2*std::numbers::pi;
        V Vec(const auto& p){return {p.X,p.Y,p.Z};}
        V Add(V a,V b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
        V Mul(V a,double s){return {a[0]*s,a[1]*s,a[2]*s};}
        double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
        V Cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
        double Norm(V v){return std::hypot(v[0],v[1],v[2]);}
        Point3 Point(V v){return {v[0],v[1],v[2]};}
        void Require(bool condition,const char* error){if(!condition)throw std::invalid_argument(error);}
        void Finite(double value){Require(std::isfinite(value),"PROCESS_NUMBER_REQUIRED");}
        void CheckCancelled(const SProcessCompileInput& in)
        {if(in.IsCancelled&&in.IsCancelled())throw iCAX::Tasks::TaskCanceledException();}
        double Start(const ParameterRange& r){return r.Reversed?r.Last:r.First;}
        double End(const ParameterRange& r){return r.Reversed?r.First:r.Last;}
        V Radial(const Circle3& c,double t)
        {return Add(Mul(Vec(c.Placement.XDirection),std::cos(t)),Mul(Vec(c.Placement.YDirection),std::sin(t)));}
        Point3 At(const Circle3& c,double t){return Point(Add(Vec(c.Placement.Location),Mul(Radial(c,t),c.Radius)));}
        Point3 Endpoint(const CurveSegment3& segment,bool end)
        {
            const double t=end?End(segment.Range):Start(segment.Range);
            if(const auto* circle=std::get_if<Circle3>(&segment.Curve))return At(*circle,t);
            const auto line=std::get<Segment3>(segment.Curve);return Point(Add(Vec(line.Start),Mul(Add(Vec(line.End),Mul(Vec(line.Start),-1)),t)));
        }
        std::pair<double,double> ArcDistanceRange(const Circle3& circle,const ParameterRange& range,Point3 center)
        {
            const auto delta=Add(Vec(circle.Placement.Location),Mul(Vec(center),-1));
            const auto distance=[&](double t){return Norm(Add(delta,Mul(Radial(circle,t),circle.Radius)));};
            double low=std::min(distance(range.First),distance(range.Last)),high=std::max(distance(range.First),distance(range.Last));
            const auto angle=std::atan2(Dot(delta,Vec(circle.Placement.YDirection)),Dot(delta,Vec(circle.Placement.XDirection)));
            for(const double critical:{angle,angle+std::numbers::pi}) {
                const double t=critical+Tau*std::ceil((range.First-critical)/Tau);
                if(t<=range.Last){const auto r=distance(t);low=std::min(low,r);high=std::max(high,r);}
            }
            return {low,high};
        }
        void ValidateLead(const SLeadPolicy& lead)
        {
            Finite(lead.LengthMm);Finite(lead.AngleRad);Finite(lead.RadiusMm);
            Require(lead.Kind=="None"||lead.Kind=="Line"||lead.Kind=="Arc","PROCESS_LEAD_KIND_UNSUPPORTED");
            Require(lead.Placement=="ScrapSide","PROCESS_LEAD_PLACEMENT_UNSUPPORTED");
            Require(lead.LengthMm>=0&&std::abs(lead.AngleRad)<=std::numbers::pi,"PROCESS_LEAD_INVALID");
            Require(lead.Kind=="None"?lead.LengthMm==0:lead.LengthMm>0,"PROCESS_LEAD_INVALID");
            if(lead.Kind=="Arc") {
                Require(lead.RadiusMm>0&&lead.LengthMm/lead.RadiusMm<=std::numbers::pi,"PROCESS_ARC_LEAD_RANGE_INVALID");
                Require(lead.AngleRad==0,"PROCESS_ARC_LEAD_ANGLE_UNSUPPORTED");
            }
        }
        void ValidateTechnology(const SLaserProcess& p)
        {
            for(const auto v:{p.FeedMmPerSecond,p.PowerW,p.GasPressureBar,p.StandOffMm,p.FocusOffsetMm,p.PierceSeconds,p.PierceFootprintRadiusMm})Finite(v);
            Require(p.FeedMmPerSecond>0&&p.PowerW>0&&p.GasPressureBar>=0&&p.StandOffMm>=0&&p.PierceSeconds>=0
                &&p.PierceFootprintRadiusMm>=0&&!p.Gas.empty(),"PROCESS_PROFILE_INCOMPLETE");
        }
        class CCamProcessPlanningService final:public ICamProcessPlanningService
        {
            AUTO_REGIST_SERVICE(ICamProcessPlanningService,CCamProcessPlanningService)
        public:
            void OnLoad() override{}
            void OnUnload() override{}
            SProcessCompileResult Compile(const SProcessCompileInput& in) const override
            {
                CheckCancelled(in);ValidateTechnology(in.Technology);ValidateLead(in.LeadIn);ValidateLead(in.LeadOut);
                Require(!in.IntentID.empty()&&!in.SourceSpanID.empty()&&in.Frame=="Workpiece","PROCESS_SOURCE_REQUIRED");
                Require(in.NominalCurve.Closed&&in.NominalCurve.Segments.size()==1,"PROCESS_CURVE_FAMILY_UNSUPPORTED");
                const auto& original=in.NominalCurve.Segments.front();
                const auto* nominal=std::get_if<Circle3>(&original.Curve);Require(nominal,"PROCESS_CURVE_FAMILY_UNSUPPORTED");
                const auto& place=nominal->Placement;const auto x=Vec(place.XDirection),y=Vec(place.YDirection),z=Vec(place.ZDirection);
                for(auto v:{Vec(place.Location),x,y,z})for(auto n:v)Finite(n);
                Finite(nominal->Radius);Finite(in.KerfWidthMm);Finite(in.OvercutLengthMm);Finite(in.GapLengthMm);Finite(in.PositionToleranceMm);Finite(in.LeadTiltRad);Finite(in.SideTiltRad);
                Finite(original.Range.First);Finite(original.Range.Last);
                Require(nominal->Radius>0&&in.KerfWidthMm>=0&&in.OvercutLengthMm>=0&&in.GapLengthMm>=0&&!(in.OvercutLengthMm>0&&in.GapLengthMm>0)&&in.PositionToleranceMm>0&&in.MaximumSegments>0&&in.MaximumSegments<=100000,"PROCESS_PARAMETERS_INVALID");
                const auto frameError=std::max({std::abs(Dot(x,x)-1),std::abs(Dot(y,y)-1),std::abs(Dot(z,z)-1),std::abs(Dot(x,y)),Norm(Add(Cross(x,y),Mul(z,-1)))});
                Require(frameError<=1e-12,"PROCESS_CIRCLE_FRAME_INVALID");
                Require(original.Range.Last>original.Range.First&&std::abs(original.Range.Last-original.Range.First-Tau)<=1e-12,"PROCESS_FULL_CIRCLE_REQUIRED");
                Require(in.RetainedSide=="Interior"||in.RetainedSide=="Exterior","PROCESS_RETAINED_SIDE_INVALID");
                Require(in.KerfOwner=="Cam","PROCESS_EXECUTOR_COMPENSATION_UNSUPPORTED");
                Require(in.MeasurementSection=="Surface","PROCESS_MEASUREMENT_SECTION_UNSUPPORTED");
                Require(in.PoseInitialization=="SurfaceNormal"&&in.LeadTiltRad==0&&in.SideTiltRad==0,"PROCESS_POSE_COUPLING_UNSUPPORTED");
                Require(in.OutwardSign==1||in.OutwardSign==-1,"PROCESS_SIDE_INVALID");
                Circle3 circle=*nominal;const double sign=in.RetainedSide=="Exterior"?-1:1;
                circle.Radius+=sign*in.KerfWidthMm/2;Require(std::isfinite(circle.Radius)&&circle.Radius>in.PositionToleranceMm,"OFFSET_TOPOLOGY_CHANGED");
                double start=in.StartParameter.value_or(Start(original.Range));Finite(start);
                Require(start>=original.Range.First&&start<=original.Range.Last,"PROCESS_START_OUTSIDE_SOURCE");
                const auto forward=original.Range.Reversed?-1.:1.;
                if(start==End(original.Range))start=Start(original.Range);
                SProcessCompileResult out;out.Path.Version=1;out.Events.Version=1;out.AssumedToolDirection={z[0]*in.OutwardSign,z[1]*in.OutwardSign,z[2]*in.OutwardSign};out.CompensatedRadiusMm=circle.Radius;
                out.BoundaryRadialErrorMm=std::abs(circle.Radius-sign*in.KerfWidthMm/2-nominal->Radius);
                Require(out.BoundaryRadialErrorMm<=in.PositionToleranceMm,"PROCESS_NUMERIC_TOLERANCE_EXCEEDED");
                const auto add=[&](Curve3 curve,ParameterRange range,const char* role,std::optional<ParameterRange> source,std::uint64_t occurrence,double length) {
                    CheckCancelled(in);Require(out.Spans.size()<in.MaximumSegments,"PROCESS_SEGMENT_BUDGET");
                    Finite(length);Require(length>0,"PROCESS_DEGENERATE_SEGMENT");
                    const auto i=out.Spans.size();out.Path.Curve.Segments.push_back({std::move(curve),range});
                    out.Spans.push_back({in.IntentID+":"+std::to_string(i),role,source?in.SourceSpanID:"",i,occurrence,source,length});out.TotalLengthMm+=length;
                };
                const auto leadPoint=[&](const SLeadPolicy& lead,double at,bool entry) {
                    const auto radial=Radial(circle,at);const auto tangent=Mul(Add(Mul(x,-std::sin(at)),Mul(y,std::cos(at))),forward);
                    const auto delta=Add(Mul(radial,sign*std::cos(lead.AngleRad)),Mul(tangent,(entry?-1.:1.)*std::sin(lead.AngleRad)));
                    const auto p=At(circle,at);return Point(Add(Vec(p),Mul(delta,lead.LengthMm)));
                };
                const auto localScrapLine=[&](Point3 a,Point3 b) {
                    // Radial extrema over the whole line, not sampled points.
                    const auto va=Add(Vec(a),Mul(Vec(place.Location),-1)),vb=Add(Vec(b),Mul(Vec(place.Location),-1)),v=Add(vb,Mul(va,-1));
                    const auto t=Dot(v,v)>0?std::clamp(-Dot(va,v)/Dot(v,v),0.,1.):0.;
                    return sign<0?std::max(Norm(va),Norm(vb))<=circle.Radius+in.PositionToleranceMm
                        :Norm(Add(va,Mul(v,t)))>=circle.Radius-in.PositionToleranceMm;
                };
                const auto makeLead=[&](const SLeadPolicy& policy,double at,bool entry) {
                    CurveSegment3 lead;
                    if(policy.Kind=="Line") {
                        const auto p=At(circle,at),other=leadPoint(policy,at,entry);
                        const auto a=entry?other:p,b=entry?p:other;
                        Require(localScrapLine(a,b),"PROCESS_LEAD_ENTERS_RETAINED_MATERIAL");lead={Segment3{a,b},{0,1,false}};
                    } else {
                        const auto radial=Radial(circle,at),u=Mul(radial,-sign),v=Cross(z,u);const auto point=At(circle,at);
                        Circle3 arc;arc.Radius=policy.RadiusMm;arc.Placement.Location=Point(Add(Vec(point),Mul(radial,sign*arc.Radius)));
                        arc.Placement.XDirection={u[0],u[1],u[2]};arc.Placement.YDirection={v[0],v[1],v[2]};arc.Placement.ZDirection=place.ZDirection;
                        const double direction=-sign*forward,sweep=policy.LengthMm/arc.Radius;
                        const double a=entry?-direction*sweep:0,b=entry?0:direction*sweep;
                        lead={arc,{std::min(a,b),std::max(a,b),direction<0}};
                        const auto [low,high]=ArcDistanceRange(arc,lead.Range,place.Location);
                        Require(sign<0?high<=circle.Radius+in.PositionToleranceMm:low>=circle.Radius-in.PositionToleranceMm,"PROCESS_LEAD_ENTERS_RETAINED_MATERIAL");
                    }
                    return lead;
                };
                Point3 pierce=At(circle,start);
                if(in.LeadIn.Kind!="None") {
                    const auto lead=makeLead(in.LeadIn,start,true);pierce=Endpoint(lead,false);
                    add(lead.Curve,lead.Range,"LeadIn",{},0,in.LeadIn.LengthMm);
                }
                const double pierceDistance=Norm(Add(Vec(pierce),Mul(Vec(place.Location),-1))),footprint=in.Technology.PierceFootprintRadiusMm;
                Require(sign<0?pierceDistance+footprint<=nominal->Radius+in.PositionToleranceMm:pierceDistance-footprint>=nominal->Radius-in.PositionToleranceMm,"PROCESS_PIERCE_ENTERS_RETAINED_MATERIAL");
                double at=start;std::uint64_t occurrence=0;
                const auto appendArc=[&](double radians,const char* role) {
                    double remaining=radians;
                    while(remaining>0) {
                        CheckCancelled(in);const double bound=End(original.Range),available=std::abs(bound-at);
                        if(available<=1e-14){at=Start(original.Range);++occurrence;continue;}
                        const double travel=std::min(remaining,available),end=at+forward*travel;
                        ParameterRange range{std::min(at,end),std::max(at,end),original.Range.Reversed};
                        add(circle,range,role,range,occurrence,travel*circle.Radius);
                        const double next=remaining-travel;Require(next<remaining,"PROCESS_NUMERIC_PROGRESS_FAILED");remaining=next;at=end;
                    }
                };
                Require(in.GapLengthMm<Tau*circle.Radius-in.PositionToleranceMm,"PROCESS_GAP_EXCEEDS_CONTOUR");
                appendArc(Tau-in.GapLengthMm/circle.Radius,"Cut");if(in.OvercutLengthMm>0)appendArc(in.OvercutLengthMm/circle.Radius,"Overcut");
                if(in.LeadOut.Kind!="None") {
                    const auto lead=makeLead(in.LeadOut,at,false);add(lead.Curve,lead.Range,"LeadOut",{},occurrence,in.LeadOut.LengthMm);
                }
                out.Path.Curve.Closed=in.LeadIn.Kind=="None"&&in.LeadOut.Kind=="None"&&in.OvercutLengthMm==0&&in.GapLengthMm==0;
                const auto event=[&](bool end,const char* kind,iCAX::Data::ObjectMap parameters={}) {
                    const auto index=end?out.Spans.size()-1:0;const auto& range=out.Path.Curve.Segments[index].Range;
                    out.Events.Events.push_back({index,end?End(range):Start(range),kind,std::move(parameters)});
                };
                const auto& t=in.Technology;
                event(false,"ProcessSelect",{{"feedMmPerSecond",t.FeedMmPerSecond},{"laserPowerW",t.PowerW},{"focusOffsetMm",t.FocusOffsetMm}});
                event(false,"GasOn",{{"gas",t.Gas},{"pressureBar",t.GasPressureBar}});
                event(false,"HeightFollow",{{"heightMm",t.StandOffMm}});
                event(false,"Pierce",{{"durationSeconds",t.PierceSeconds},{"footprintRadiusMm",t.PierceFootprintRadiusMm}});
                event(false,"CutOn");event(true,"CutOff");event(true,"GasOff");
                CheckCancelled(in);return out;
            }
        };
    }
}
