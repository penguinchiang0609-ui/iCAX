#include "pch.h"
#include "GeometryFeatureAnalysisService.h"
#include "GeometryBoundarySweep.h"
#include "BoundaryJunctions.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include "Task/TaskStatus.h"
#include <BRepAdaptor_Curve.hxx>
#include <gp_Elips.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_BezierCurve.hxx>
#include <GeomConvert_CurveToAnaCurve.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Face.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <Message_ProgressIndicator.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <set>
#include <limits>

namespace
{
    using namespace iCAX::Geometry;
    constexpr double Pi=std::numbers::pi;
    class CPatchProgress final:public Message_ProgressIndicator
    {
        std::function<bool()> m_Cancelled;
    public:
        explicit CPatchProgress(std::function<bool()> cancelled):m_Cancelled(std::move(cancelled)){}
    protected:
        bool UserBreak() override{return m_Cancelled&&m_Cancelled();}
        void Show(const Message_ProgressScope&,bool) override{}
    };
    std::array<double,3> Point(gp_Pnt p){return {p.X(),p.Y(),p.Z()};}
    std::array<double,3> Direction(gp_Dir p){return {p.X(),p.Y(),p.Z()};}
    struct Segment
    {
        GeomAbs_CurveType Type;
        gp_Pnt Start,End;
        gp_Circ Circle;
        gp_Elips Ellipse;
        double Sweep=0, Length=0, SourceDeviationUpperMm=0;
    };
    double Binomial(int n,int k){double value=1;for(int i=1;i<=k;++i)value=value*(n-k+i)/i;return value;}
    void RecoverEllipse(const BRepAdaptor_Curve& curve,bool reversed,Segment& segment,const SGeometryAnalysisOptions& options)
    {
        if(segment.Type!=GeomAbs_BSplineCurve)return;
        try{
            const auto spline=curve.BSpline();double first,last,deviation;
            const auto candidate=Handle(Geom_Ellipse)::DownCast(GeomConvert_CurveToAnaCurve::ComputeEllipse(spline,options.DistanceToleranceMm,curve.FirstParameter(),curve.LastParameter(),first,last,deviation));if(candidate.IsNull())return;
            const auto ellipse=candidate->Elips();const auto center=ellipse.Location();const auto x=ellipse.XAxis().Direction(),y=ellipse.YAxis().Direction(),normal=ellipse.Axis().Direction();double upper=0;
            GeomConvert_BSplineCurveToBezierCurve spans(spline,curve.FirstParameter(),curve.LastParameter(),1e-12);
            for(int span=1;span<=spans.NbArcs();++span){const auto bezier=spans.Arc(span);const auto degree=bezier->Degree();if(degree<1||degree>25)return;
                std::vector<double> a(degree+1),b(degree+1),w(degree+1);double minimum=std::numeric_limits<double>::infinity(),plane=0;
                for(int i=0;i<=degree;++i){w[i]=bezier->Weight(i+1);if(w[i]<=0||!std::isfinite(w[i]))return;minimum=std::min(minimum,w[i]);const gp_Vec p(center,bezier->Pole(i+1));a[i]=p.Dot(gp_Vec(x))*w[i]/ellipse.MajorRadius();b[i]=p.Dot(gp_Vec(y))*w[i]/ellipse.MinorRadius();plane=std::max(plane,std::abs(p.Dot(gp_Vec(normal))*w[i]));}
                double residual=0;for(int k=0;k<=2*degree;++k){double coefficient=0;for(int i=std::max(0,k-degree);i<=std::min(degree,k);++i){const auto j=k-i;coefficient+=Binomial(degree,i)*Binomial(degree,j)/Binomial(2*degree,k)*(a[i]*a[j]+b[i]*b[j]-w[i]*w[j]);}residual=std::max(residual,std::abs(coefficient));}
                const auto delta=residual/(minimum*minimum);if(!std::isfinite(delta)||delta>=1)return;upper=std::max(upper,plane/minimum+ellipse.MajorRadius()*delta/(1+std::sqrt(1-delta)));
            }
            // Bernstein convex-hull bounds prove every native parameter on
            // every rational span, independently of the fitted candidate.
            if(upper>options.DistanceToleranceMm)return;segment.Type=GeomAbs_Ellipse;segment.Ellipse=ellipse;segment.Sweep=(last-first)*(reversed?-1:1);segment.SourceDeviationUpperMm=upper;
        }catch(const Standard_Failure&){}
    }
    void Preview(const BRepAdaptor_Curve& curve,bool reversed,SAnalyticBoundary& boundary)
    {
        GCPnts_QuasiUniformDeflection samples(curve,0.05,curve.FirstParameter(),curve.LastParameter());
        if(!samples.IsDone())throw std::runtime_error("GEOMETRY_BOUNDARY_PREVIEW_FAILED");
        if(boundary.PreviewPoints.size()+samples.NbPoints()>200000)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
        for(int n=1;n<=samples.NbPoints();++n)
            boundary.PreviewPoints.push_back(Point(samples.Value(reversed?samples.NbPoints()+1-n:n)));
    }
    bool Parallel(gp_Dir a,gp_Dir b,double angle){return a.IsParallel(b,angle);}
    double SegmentDistance(gp_Pnt p,gp_Pnt a,gp_Pnt b)
    {
        const gp_Vec ab(a,b);const double t=std::clamp(gp_Vec(a,p).Dot(ab)/ab.SquareMagnitude(),0.,1.);
        return p.Distance(a.Translated(ab*t));
    }
    // Face outer wires cease to describe the exterior after a hole crosses a
    // face seam: both that hole and the exterior contain original outer uses.
    // Establish nesting on exact projected curves for one connected patch.
    // Projection is evidence only; machining retains every original 3D span.
    bool ClassifyProjectedPatch(const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built,
        std::vector<SAnalyticBoundary>& boundaries,const std::vector<size_t>& indices,
        const SGeometryAnalysisOptions& options)
    {
        if(indices.size()<2)return false;
        const auto check=[&]{if(options.IsCancelled&&options.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};
        gp_Vec axis;gp_Pnt origin;
        for(const auto index:indices){
            const auto& boundary=boundaries[index];if(!boundary.TopologyClosed||boundary.Spans.empty())return false;
            const auto& points=boundary.PreviewPoints;if(points.size()<3)return false;
            const gp_Pnt base(points.front()[0],points.front()[1],points.front()[2]);gp_Vec area;
            for(size_t i=1;i<points.size();++i)area+=gp_Vec(base,gp_Pnt(points[i-1][0],points[i-1][1],points[i-1][2])).Crossed(
                gp_Vec(base,gp_Pnt(points[i][0],points[i][1],points[i][2])));
            // Samples choose a projection direction; they never classify loops.
            if(area.SquareMagnitude()>axis.SquareMagnitude()){axis=area;origin=base;}
        }
        if(axis.Magnitude()<=options.DistanceToleranceMm*options.DistanceToleranceMm)return false;
        try{
            const gp_Dir direction(axis);const gp_Pln plane(origin,direction);occ::handle<Geom_Plane> surface=new Geom_Plane(plane);
            struct Loop{TopoDS_Face Face;TopoDS_Wire Wire;gp_Pnt Probe;bool Reversed;};std::vector<Loop> loops;
            for(const auto index:indices){
                check();const auto& boundary=boundaries[index];std::vector<TopoDS_Edge> edges;std::vector<gp_Pnt> starts,ends;
                for(const auto& span:boundary.Spans){
                    check();TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(built.Edges.at(span.EdgeID),location,first,last);
                    if(curve.IsNull())return false;
                    if(!location.IsIdentity())curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));
                    const auto projected=GeomProjLib::ProjectOnPlane(new Geom_TrimmedCurve(curve,span.First,span.Last),surface,direction,true);
                    if(projected.IsNull())return false;
                    BRepBuilderAPI_MakeEdge maker(projected,span.First,span.Last);if(!maker.IsDone())return false;
                    auto edge=maker.Edge();if(span.Reversed)edge.Reverse();edges.push_back(edge);
                    starts.push_back(projected->Value(span.Reversed?span.Last:span.First));ends.push_back(projected->Value(span.Reversed?span.First:span.Last));
                }
                BRepBuilderAPI_MakeWire wire;
                for(size_t i=0;i<edges.size();++i){
                    wire.Add(edges[i]);if(!wire.IsDone())return false;
                    const auto next=(i+1)%edges.size();
                    // TopologyClosed already verified this exact source junction
                    // against its shared CAD vertex tolerance (including scale).
                    if(ends[i].Distance(starts[next])>Precision::Confusion()){
                        BRepBuilderAPI_MakeEdge connector(ends[i],starts[next]);if(!connector.IsDone())return false;wire.Add(connector.Edge());
                        if(!wire.IsDone())return false;
                    }
                }
                BRepBuilderAPI_MakeFace face(plane,wire.Wire(),true);if(!face.IsDone())return false;
                BRepCheck_Wire validation(wire.Wire());TopoDS_Edge a,b;
                if(validation.Closed()!=BRepCheck_NoError||validation.SelfIntersect(face.Face(),a,b)!=BRepCheck_NoError
                    ||!BRepCheck_Analyzer(face.Face()).IsValid())return false;
                const auto normalized=BRepTools::OuterWire(face.Face());
                if(!normalized.IsSame(wire.Wire()))return false;
                loops.push_back({face.Face(),normalized,starts.front(),normalized.Orientation()!=wire.Wire().Orientation()});
            }
            for(size_t outer=0;outer<loops.size();++outer){
                check();bool contains=true;
                for(size_t inner=0;inner<loops.size();++inner)if(inner!=outer){
                    if(loops[inner].Reversed==loops[outer].Reversed
                        ||BRepClass_FaceClassifier(loops[outer].Face,loops[inner].Probe,Precision::Confusion()).State()!=TopAbs_IN){contains=false;break;}
                }
                if(!contains)continue;
                BRepBuilderAPI_MakeFace region(loops[outer].Face);
                for(size_t inner=0;inner<loops.size();++inner)if(inner!=outer)region.Add(TopoDS::Wire(loops[inner].Wire.Reversed()));
                if(!region.IsDone())continue;
                BRepCheck_Face validation(region.Face());
                // A point-containment check alone is insufficient: reject any
                // crossing, touching, nested holes or coincident projected loops.
                if(validation.IntersectWires()!=BRepCheck_NoError||validation.ClassifyWires()!=BRepCheck_NoError
                    ||!BRepCheck_Analyzer(region.Face()).IsValid())continue;
                for(size_t i=0;i<indices.size();++i){auto& boundary=boundaries[indices[i]];
                    boundary.RegionRelation=i==outer?"OuterLoop":"InnerLoop";boundary.Valid=true;
                    std::erase(boundary.Diagnostics,"PATCH_SELF_INTERSECTION_UNRESOLVED");
                }
                return true;
            }
        }catch(const Standard_Failure&){return false;}
        return false;
    }
    void ClassifyCircle(const std::vector<Segment>& edges,SAnalyticBoundary& out,const SGeometryAnalysisOptions& options)
    {
        if(edges.empty())return;
        const auto& circle=edges.front().Circle;
        double signedSweep=0,totalSweep=0,deviation=0;
        for(const auto& edge:edges)
        {
            if(edge.Type!=GeomAbs_Circle||edge.Circle.Location().Distance(circle.Location())>options.DistanceToleranceMm
                ||std::abs(edge.Circle.Radius()-circle.Radius())>options.DistanceToleranceMm
                ||!Parallel(edge.Circle.Axis().Direction(),circle.Axis().Direction(),options.AngleToleranceRad))return;
            const double sweep=edge.Sweep*(edge.Circle.Axis().Direction().Dot(circle.Axis().Direction())<0?-1:1);
            signedSweep+=sweep;totalSweep+=std::abs(sweep);
            const double angle=std::acos(std::clamp(std::abs(edge.Circle.Axis().Direction().Dot(circle.Axis().Direction())),0.,1.));
            deviation=std::max(deviation,edge.Circle.Location().Distance(circle.Location())
                +std::abs(edge.Circle.Radius()-circle.Radius())+2*edge.Circle.Radius()*std::sin(angle/2));
        }
        if(std::abs(std::abs(signedSweep)-2*Pi)>options.AngleToleranceRad||std::abs(totalSweep-2*Pi)>options.AngleToleranceRad)return;
        out.GeometryKind="Circle";out.Classification="Analytic";out.Center=Point(circle.Location());
        out.Normal=Direction(circle.Axis().Direction());out.XDirection=Direction(circle.XAxis().Direction());out.RadiusMm=circle.Radius();
        out.StandardDeviationUpperMm=deviation;out.StandardBoundProven=true;
    }
    void ClassifyEllipse(const std::vector<Segment>& edges,SAnalyticBoundary& out,const SGeometryAnalysisOptions& o)
    {
        if(edges.empty()||edges.front().Type!=GeomAbs_Ellipse)return;const auto& ellipse=edges.front().Ellipse;double sweep=0,total=0,deviation=0;
        for(const auto& edge:edges){if(edge.Type!=GeomAbs_Ellipse)return;const auto& e=edge.Ellipse;
            const auto center=e.Location().Distance(ellipse.Location()),major=std::abs(e.MajorRadius()-ellipse.MajorRadius()),minor=std::abs(e.MinorRadius()-ellipse.MinorRadius());
            if(center>o.DistanceToleranceMm||major>o.DistanceToleranceMm||minor>o.DistanceToleranceMm||!Parallel(e.Axis().Direction(),ellipse.Axis().Direction(),o.AngleToleranceRad)||!Parallel(e.XAxis().Direction(),ellipse.XAxis().Direction(),o.AngleToleranceRad))return;
            const auto angle=std::acos(std::clamp(std::abs(e.Axis().Direction().Dot(ellipse.Axis().Direction())),0.,1.))+std::acos(std::clamp(std::abs(e.XAxis().Direction().Dot(ellipse.XAxis().Direction())),0.,1.));deviation=std::max(deviation,edge.SourceDeviationUpperMm+center+major+minor+2*ellipse.MajorRadius()*std::sin(angle/2));sweep+=edge.Sweep*(e.Axis().Direction().Dot(ellipse.Axis().Direction())<0?-1:1);total+=std::abs(edge.Sweep);}
        if(std::abs(std::abs(sweep)-2*Pi)>o.AngleToleranceRad||std::abs(total-2*Pi)>o.AngleToleranceRad)return;out.GeometryKind="Ellipse";out.Classification="Analytic";out.Center=Point(ellipse.Location());out.Normal=Direction(ellipse.Axis().Direction());out.XDirection=Direction(ellipse.XAxis().Direction());out.LengthMm=2*ellipse.MajorRadius();out.WidthMm=2*ellipse.MinorRadius();out.StandardDeviationUpperMm=deviation;out.StandardBoundProven=true;
    }
    void ClassifyRectangle(std::vector<Segment> edges,SAnalyticBoundary& out,const SGeometryAnalysisOptions& o)
    {
        for(const auto& e:edges)if(e.Type!=GeomAbs_Line)return;
        const auto original=edges;
        bool merged=true;
        while(merged&&edges.size()>2)
        {
            merged=false;
            for(size_t i=0;i<edges.size();++i)
            {
                const auto j=(i+1)%edges.size();const gp_Vec a(edges[i].Start,edges[i].End),b(edges[j].Start,edges[j].End);
                if(a.Magnitude()<=o.DistanceToleranceMm||b.Magnitude()<=o.DistanceToleranceMm)return;
                if(a.Angle(b)<=o.AngleToleranceRad)
                {
                    if(j==0){edges[0].Start=edges[i].Start;edges[0].Length+=edges[i].Length;edges.pop_back();}
                    else {edges[i].End=edges[j].End;edges[i].Length+=edges[j].Length;edges.erase(edges.begin()+j);}
                    merged=true;break;
                }
            }
        }
        if(edges.size()!=4)return;
        const gp_Vec x(edges[0].Start,edges[0].End),y(edges[1].Start,edges[1].End);
        if(x.Magnitude()<=o.DistanceToleranceMm||y.Magnitude()<=o.DistanceToleranceMm)return;
        const auto cross=x.Crossed(y);if(cross.Magnitude()<=o.DistanceToleranceMm*o.DistanceToleranceMm)return;
        const gp_Dir normal(cross);
        for(int i=0;i<4;++i)
        {
            const gp_Vec a(edges[i].Start,edges[i].End),b(edges[(i+1)%4].Start,edges[(i+1)%4].End);
            if(a.Magnitude()<=o.DistanceToleranceMm||b.Magnitude()<=o.DistanceToleranceMm)return;
            if(std::abs(a.Angle(b)-Pi/2)>o.AngleToleranceRad||a.Crossed(b).Dot(gp_Vec(normal))<=0)return;
            if(std::abs(gp_Vec(edges[0].Start,edges[i].Start).Dot(gp_Vec(normal)))>o.DistanceToleranceMm)return;
            if(std::abs(a.Magnitude()-edges[(i+2)%4].Length)>o.DistanceToleranceMm)return;
        }
        out.GeometryKind="Rectangle";out.Classification="Analytic";
        const gp_Vec exactX=gp_Vec(gp_Dir(x))*x.Magnitude(),exactY=gp_Vec(normal.Crossed(gp_Dir(x)))*y.Magnitude();
        std::array<gp_Pnt,4> corners{edges[0].Start,edges[0].Start.Translated(exactX),edges[0].Start.Translated(exactX+exactY),edges[0].Start.Translated(exactY)};
        double deviation=0;
        for(const auto& segment:original)
        {
            double best=std::numeric_limits<double>::infinity();
            for(size_t i=0;i<4;++i)best=std::min(best,std::max(SegmentDistance(segment.Start,corners[i],corners[(i+1)%4]),SegmentDistance(segment.End,corners[i],corners[(i+1)%4])));
            deviation=std::max(deviation,best);
        }
        out.Center=Point(corners[0].Translated((exactX+exactY)*.5));
        out.Normal=Direction(normal);out.XDirection=Direction(gp_Dir(x));out.WidthMm=x.Magnitude();out.LengthMm=y.Magnitude();
        out.StandardDeviationUpperMm=deviation;out.StandardBoundProven=true;
    }
    bool MergeSlotSegments(const Segment& a,const Segment& b,Segment& merged,const SGeometryAnalysisOptions& o)
    {
        if(a.Type!=b.Type||a.End.Distance(b.Start)>o.DistanceToleranceMm)return false;
        merged=a;merged.End=b.End;merged.Length+=b.Length;
        if(a.Type==GeomAbs_Line){
            const gp_Vec first(a.Start,a.End),second(b.Start,b.End);
            if(first.Magnitude()<=o.DistanceToleranceMm||second.Magnitude()<=o.DistanceToleranceMm
                ||first.Angle(second)>o.AngleToleranceRad)return false;
            // Distance to a segment is convex: endpoints bound each entire source line.
            merged.SourceDeviationUpperMm=std::max(a.SourceDeviationUpperMm+SegmentDistance(a.End,a.Start,b.End),
                b.SourceDeviationUpperMm+SegmentDistance(b.Start,a.Start,b.End));
            return true;
        }
        if(a.Type!=GeomAbs_Circle||a.Circle.Location().Distance(b.Circle.Location())>o.DistanceToleranceMm
            ||std::abs(a.Circle.Radius()-b.Circle.Radius())>o.DistanceToleranceMm
            ||!Parallel(a.Circle.Axis().Direction(),b.Circle.Axis().Direction(),o.AngleToleranceRad))return false;
        const double nextSweep=b.Sweep*(a.Circle.Axis().Direction().Dot(b.Circle.Axis().Direction())<0?-1.:1.);
        if(a.Sweep*nextSweep<=0||std::abs(a.Sweep+nextSweep)>2*Pi+o.AngleToleranceRad)return false;
        merged.Sweep+=nextSweep;
        const auto predictedStart=gp_Vec(a.Circle.Location(),a.Start).Rotated(a.Circle.Axis(),a.Sweep);
        const gp_Vec actualStart(b.Circle.Location(),b.Start);
        const auto predictedTangent=gp_Vec(a.Circle.Axis().Direction()).Crossed(predictedStart)*(a.Sweep<0?-1.:1.);
        const auto actualTangent=gp_Vec(b.Circle.Axis().Direction()).Crossed(actualStart)*(b.Sweep<0?-1.:1.);
        // Compare the two complete circular parameterizations, not display samples.
        // Triangle inequality bounds center + cosine basis + sine basis for all t.
        const double nextDeviation=a.Circle.Location().Distance(b.Circle.Location())
            +(actualStart-predictedStart).Magnitude()+(actualTangent-predictedTangent).Magnitude();
        merged.SourceDeviationUpperMm=std::max(a.SourceDeviationUpperMm,b.SourceDeviationUpperMm+nextDeviation);
        return true;
    }
    void ClassifySlot(const std::vector<Segment>& source,SAnalyticBoundary& out,const SGeometryAnalysisOptions& o)
    {
        // STEP may split straight sides and semicircles into any number of edges.
        // Normalize only this analytic view; source spans, trims and topology stay intact.
        std::vector<Segment> edges;
        for(const auto& edge:source){
            Segment merged;
            if(!edges.empty()&&MergeSlotSegments(edges.back(),edge,merged,o))edges.back()=merged;
            else edges.push_back(edge);
        }
        if(edges.size()>1){Segment merged;if(MergeSlotSegments(edges.back(),edges.front(),merged,o)){
            edges.front()=merged;edges.pop_back();
        }}
        if(edges.size()!=4)return;
        std::vector<const Segment*> arcs,lines;
        for(const auto& edge:edges)
        {
            if(edge.Type==GeomAbs_Circle)arcs.push_back(&edge);
            else if(edge.Type==GeomAbs_Line)lines.push_back(&edge);else return;
        }
        if(arcs.size()!=2||lines.size()!=2)return;
        const auto& a=*arcs[0];const auto& b=*arcs[1];const double r=a.Circle.Radius();
        if(std::abs(r-b.Circle.Radius())>o.DistanceToleranceMm
            ||std::abs(std::abs(a.Sweep)-Pi)>o.AngleToleranceRad||std::abs(std::abs(b.Sweep)-Pi)>o.AngleToleranceRad
            ||!Parallel(a.Circle.Axis().Direction(),b.Circle.Axis().Direction(),o.AngleToleranceRad))return;
        const gp_Vec axis(a.Circle.Location(),b.Circle.Location());const double length=axis.Magnitude();
        if(length<=o.DistanceToleranceMm)return;
        const gp_Dir x(axis),normal=a.Circle.Axis().Direction();
        if(std::abs(axis.Dot(gp_Vec(normal)))>o.DistanceToleranceMm)return;
        for(auto line:lines)
        {
            const gp_Vec direction(line->Start,line->End);
            if(direction.Magnitude()<=o.DistanceToleranceMm||std::abs(direction.Magnitude()-length)>o.DistanceToleranceMm
                ||!Parallel(gp_Dir(direction),x,o.AngleToleranceRad))return;
            bool one=false,two=false;
            for(const auto p:{line->Start,line->End})
            {
                if(std::abs(p.Distance(a.Circle.Location())-r)<=o.DistanceToleranceMm
                    &&std::abs(gp_Vec(a.Circle.Location(),p).Dot(gp_Vec(x)))<=o.DistanceToleranceMm)one=true;
                if(std::abs(p.Distance(b.Circle.Location())-r)<=o.DistanceToleranceMm
                    &&std::abs(gp_Vec(b.Circle.Location(),p).Dot(gp_Vec(x)))<=o.DistanceToleranceMm)two=true;
            }
            if(!one||!two)return;
        }
        // Inward semicircles can also form a simple loop, but not a stadium.
        const gp_Vec aStart(a.Circle.Location(),a.Start),bStart(b.Circle.Location(),b.Start);
        const gp_Vec aTangent=gp_Vec(a.Circle.Axis().Direction()).Crossed(aStart)*(a.Sweep<0?-1.:1.);
        const gp_Vec bTangent=gp_Vec(b.Circle.Axis().Direction()).Crossed(bStart)*(b.Sweep<0?-1.:1.);
        if(aTangent.Dot(gp_Vec(x))>=0||bTangent.Dot(gp_Vec(x))<=0)return;
        const gp_Dir exactX(gp_Vec(x)-gp_Vec(normal)*gp_Vec(x).Dot(gp_Vec(normal)));
        const gp_Dir exactY(normal.Crossed(exactX));
        const gp_Pnt c1=a.Circle.Location(),c2=c1.Translated(gp_Vec(exactX)*length);
        double deviation=0;
        for(size_t i=0;i<2;++i)
        {
            const auto& arc=*arcs[i];const auto center=i?c2:c1;
            const gp_Vec start(arc.Circle.Location(),arc.Start);
            const gp_Vec targetStart=gp_Vec(exactY)*(start.Dot(gp_Vec(exactY))<0?-1.:1.);
            const gp_Vec tangent=gp_Vec(arc.Circle.Axis().Direction()).Crossed(start.Normalized())*(arc.Sweep<0?-1.:1.);
            const gp_Vec targetTangent=gp_Vec(exactX)*(i?1.:-1.);
            deviation=std::max(deviation,arc.SourceDeviationUpperMm+center.Distance(arc.Circle.Location())+std::abs(arc.Circle.Radius()-r)
                +arc.Circle.Radius()*((start.Normalized()-targetStart).Magnitude()+(tangent-targetTangent).Magnitude())
                +arc.Circle.Radius()*std::abs(std::abs(arc.Sweep)-Pi));
        }
        for(auto line:lines)
        {
            const auto y=gp_Vec(exactY)*(gp_Vec(c1,line->Start).Dot(gp_Vec(exactY))<0?-r:r);
            deviation=std::max(deviation,line->SourceDeviationUpperMm+std::max(SegmentDistance(line->Start,c1.Translated(y),c2.Translated(y)),SegmentDistance(line->End,c1.Translated(y),c2.Translated(y))));
        }
        out.GeometryKind="Slot";out.Classification="Analytic";
        out.Center=Point(gp_Pnt((c1.XYZ()+c2.XYZ())/2));
        out.Normal=Direction(normal);out.XDirection=Direction(exactX);out.WidthMm=2*r;out.LengthMm=length+2*r;
        out.StandardDeviationUpperMm=deviation;out.StandardBoundProven=true;
    }
    class CGeometryFeatureAnalysisService final : public IGeometryFeatureAnalysisService
    {
        AUTO_REGIST_SERVICE(IGeometryFeatureAnalysisService,CGeometryFeatureAnalysisService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        SAnalyticBoundary AnalyzeEdgeLoop(const iCAX::GeometryData::BRepModel& model,
            const std::vector<SBoundaryCurveSpan>& selected,const SGeometryAnalysisOptions& o) const override
        {
            if(selected.empty())throw std::invalid_argument("GEOMETRY_LOOP_SELECTION_REQUIRED");
            if(!std::isfinite(o.DistanceToleranceMm)||o.DistanceToleranceMm<=0||!o.MaximumEdgeUses)
                throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_OPTIONS");
            if(selected.size()>o.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
            const auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
            if(!built.bOK)throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_BREP");
            std::set<std::uint64_t> ids;std::vector<gp_Pnt> nodes;
            std::vector<std::pair<size_t,size_t>> ends;
            const auto node=[&](const gp_Pnt& point){
                size_t found=nodes.size();
                for(size_t i=0;i<nodes.size();++i)if(point.Distance(nodes[i])<=o.DistanceToleranceMm){
                    if(found!=nodes.size())throw std::invalid_argument("GEOMETRY_LOOP_AMBIGUOUS_JUNCTION");found=i;
                }
                if(found==nodes.size())nodes.push_back(point);return found;
            };
            for(const auto& span:selected){
                check();
                if(!ids.insert(span.EdgeID).second||!built.Edges.contains(span.EdgeID)||!built.Faces.contains(span.FaceID))
                    throw std::invalid_argument("GEOMETRY_LOOP_EDGE_USE_INVALID");
                const auto edge=std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==span.EdgeID;});
                const auto wire=std::find_if(model.Wires.begin(),model.Wires.end(),[&](const auto& w){return w.Id==span.WireID;});
                const auto face=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==span.FaceID;});
                if(edge==model.Edges.end()||wire==model.Wires.end()||face==model.Faces.end()||edge->Degenerated
                    ||std::find(face->WireIds.begin(),face->WireIds.end(),span.WireID)==face->WireIds.end()
                    ||span.CoedgeIndex>=wire->Coedges.size()||wire->Coedges[span.CoedgeIndex].EdgeId!=span.EdgeID
                    ||span.CurveID!=edge->Curve3Id||span.First!=edge->Range.First||span.Last!=edge->Range.Last)
                    throw std::invalid_argument("GEOMETRY_LOOP_EDGE_USE_INVALID");
                BRepAdaptor_Curve curve(built.Edges.at(span.EdgeID));
                const auto a=node(curve.Value(span.First)),b=node(curve.Value(span.Last));ends.emplace_back(a,b);
            }
            std::vector<std::vector<size_t>> incident(nodes.size());
            for(size_t i=0;i<ends.size();++i){incident[ends[i].first].push_back(i);incident[ends[i].second].push_back(i);}
            for(const auto& edges:incident){
                if(edges.size()>2)throw std::invalid_argument("GEOMETRY_LOOP_BRANCH");
                if(edges.size()!=2)throw std::invalid_argument("GEOMETRY_LOOP_OPEN");
            }
            SAnalyticBoundary boundary;std::vector<Segment> segments;std::vector<bool> used(selected.size());
            auto current=ends.front().first;const auto start=current;
            for(size_t n=0;n<selected.size();++n){
                check();const auto found=std::find_if(incident[current].begin(),incident[current].end(),[&](size_t i){return !used[i];});
                if(found==incident[current].end())throw std::invalid_argument("GEOMETRY_LOOP_DISCONNECTED");
                const auto i=*found;used[i]=true;auto span=selected[i];span.Reversed=ends[i].first!=current;
                current=span.Reversed?ends[i].first:ends[i].second;boundary.Spans.push_back(span);
                BRepAdaptor_Curve curve(built.Edges.at(span.EdgeID));Segment segment;
                segment.Type=curve.GetType();segment.Start=curve.Value(span.Reversed?span.Last:span.First);segment.End=curve.Value(span.Reversed?span.First:span.Last);
                segment.Length=GCPnts_AbscissaPoint::Length(curve,span.First,span.Last,o.DistanceToleranceMm);
                if(segment.Length<=o.DistanceToleranceMm)throw std::invalid_argument("GEOMETRY_LOOP_ZERO_LENGTH");
                if(segment.Type==GeomAbs_Circle){segment.Circle=curve.Circle();segment.Sweep=(span.Last-span.First)*(span.Reversed?-1.:1.);}
                if(segment.Type==GeomAbs_Ellipse){segment.Ellipse=curve.Ellipse();segment.Sweep=(span.Last-span.First)*(span.Reversed?-1.:1.);}
                RecoverEllipse(curve,span.Reversed,segment,o);
                boundary.PerimeterMm+=segment.Length;segments.push_back(segment);Preview(curve,span.Reversed,boundary);
            }
            if(current!=start)throw std::invalid_argument("GEOMETRY_LOOP_OPEN");
            boundary.Closed=boundary.TopologyClosed=true;boundary.FaceID=boundary.Spans.front().FaceID;
            ClassifyCircle(segments,boundary,o);if(boundary.GeometryKind=="GeneralContour")ClassifyEllipse(segments,boundary,o);if(boundary.GeometryKind=="GeneralContour")ClassifyRectangle(segments,boundary,o);
            if(boundary.GeometryKind=="GeneralContour")ClassifySlot(segments,boundary,o);
            boundary.Valid=boundary.GeometryKind!="GeneralContour";
            if(!boundary.Valid)boundary.Diagnostics.push_back("LOOP_SELF_INTERSECTION_UNRESOLVED");
            return boundary;
        }
        std::vector<SBoundaryTermination> AnalyzeBoundaryTerminations(const iCAX::GeometryData::BRepModel& model,
            const std::vector<std::uint64_t>& selected,const std::vector<SAnalyticBoundary>& boundaries,const SGeometryAnalysisOptions& options) const override
        {return Detail::AnalyzeBoundaryTerminations(model,selected,boundaries,options);}
        SBoundarySweepAnalysis AnalyzeBoundarySweep(const iCAX::GeometryData::BRepModel& model,std::uint64_t faceID,
            const std::vector<std::uint64_t>& boundaryEdges,const SGeometryAnalysisOptions& options) const override
        {return Detail::AnalyzeBoundarySweep(*this,model,faceID,boundaryEdges,options);}
        SFacePatchAnalysis AnalyzePatch(const iCAX::GeometryData::BRepModel& model,const std::vector<std::uint64_t>& selected,const SGeometryAnalysisOptions& o) const override
        {
            if(selected.empty()||std::set<std::uint64_t>(selected.begin(),selected.end()).size()!=selected.size())
                throw std::invalid_argument("GEOMETRY_PATCH_SELECTION_REQUIRED");
            if(!std::isfinite(o.DistanceToleranceMm)||o.DistanceToleranceMm<=0||!std::isfinite(o.AngleToleranceRad)
                ||o.AngleToleranceRad<=0||o.AngleToleranceRad>=0.1||o.MaximumEdgeUses==0)
                throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_OPTIONS");
            auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);check();
            if(!built.bOK||!BRepCheck_Analyzer(built.Shape).IsValid())throw std::invalid_argument("GEOMETRY_PATCH_INVALID_BREP");
            using namespace iCAX::GeometryData;
            std::map<std::uint64_t,const BRepFace*> faces;std::map<std::uint64_t,const BRepWire*> wires;std::map<std::uint64_t,const BRepEdge*> edges;
            for(const auto& value:model.Faces)faces.emplace(value.Id,&value);
            for(const auto& value:model.Wires)wires.emplace(value.Id,&value);
            for(const auto& value:model.Edges)edges.emplace(value.Id,&value);
            std::map<std::uint64_t,bool> reversedFace;
            for(TopExp_Explorer explorer(built.Shape,TopAbs_FACE);explorer.More();explorer.Next())
                for(const auto id:selected)if(built.Faces.contains(id)&&explorer.Current().IsSame(built.Faces.at(id)))
                {
                    if(reversedFace.contains(id))throw std::invalid_argument("GEOMETRY_PATCH_FACE_USE_AMBIGUOUS");
                    reversedFace[id]=explorer.Current().Orientation()==TopAbs_REVERSED;
                }
            struct Use{SBoundaryCurveSpan Span;std::uint64_t Start,End;};
            std::vector<Use> uses;std::map<std::uint64_t,std::vector<size_t>> byEdge;
            std::map<std::uint64_t,std::uint64_t> parent;
            const auto root=[&](std::uint64_t id){while(parent.contains(id)&&parent.at(id)!=id)id=parent.at(id);return id;};
            SFacePatchAnalysis result;result.SelectedFaces=selected;
            std::size_t useBudget=0;
            for(const auto id:selected)
            {
                check();if(!faces.contains(id)||!reversedFace.contains(id))throw std::invalid_argument("GEOMETRY_PATCH_FACE_MISSING");
                const auto& face=*faces.at(id);
                for(size_t w=0;w<face.WireIds.size();++w)
                {
                    if(!wires.contains(face.WireIds[w]))throw std::invalid_argument("GEOMETRY_PATCH_WIRE_MISSING");
                    const auto& wire=*wires.at(face.WireIds[w]);
                    const bool reverseWire=face.WireOrientations.size()==face.WireIds.size()&&face.WireOrientations[w]==ETopologyOrientation::Reversed;
                    for(size_t c=0;c<wire.Coedges.size();++c)
                    {
                        check();if(++useBudget>o.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
                        const auto& coedge=wire.Coedges[c];
                        if(coedge.Orientation!=ETopologyOrientation::Forward&&coedge.Orientation!=ETopologyOrientation::Reversed)
                            throw std::invalid_argument("GEOMETRY_PATCH_UNORIENTED_EDGE_USE");
                        if(!edges.contains(coedge.EdgeId))throw std::invalid_argument("GEOMETRY_PATCH_EDGE_MISSING");
                        const auto& edge=*edges.at(coedge.EdgeId);
                        auto start=coedge.StartVertexId,end=coedge.EndVertexId;
                        if(!start||!end)throw std::invalid_argument("GEOMETRY_PATCH_VERTEX_MISSING");
                        if(reverseWire!=reversedFace.at(id))std::swap(start,end);
                        if(edge.Degenerated)
                        {
                            // A declared degenerate edge is a topological connection,
                            // not a zero-length cut and not an approximate gap weld.
                            parent[root(start)]=root(end);++result.DegenerateUses;continue;
                        }
                        const bool reversed=(coedge.Orientation==ETopologyOrientation::Reversed)^reverseWire^reversedFace.at(id);
                        uses.push_back({{edge.Id,edge.Curve3Id,edge.Range.First,edge.Range.Last,reversed,id,wire.Id,c},start,end});
                        byEdge[edge.Id].push_back(uses.size()-1);
                    }
                }
            }
            std::vector<bool> removed(uses.size(),false);
            std::vector<std::pair<size_t,size_t>> joinedUses;
            std::map<std::uint64_t,std::vector<std::pair<std::uint64_t,bool>>> orientationLinks;
            const auto linkOrientation=[&](std::uint64_t a,std::uint64_t b,bool flip){
                orientationLinks[a].emplace_back(b,flip);orientationLinks[b].emplace_back(a,flip);
            };
            for(const auto& [id,indices]:byEdge)
            {
                if(indices.size()>2)throw std::invalid_argument("GEOMETRY_PATCH_NONMANIFOLD_EDGE");
                if(indices.size()==2)
                {
                    const auto& a=uses[indices[0]];const auto& b=uses[indices[1]];
                    if(a.Span.Reversed==b.Span.Reversed)throw std::invalid_argument("GEOMETRY_PATCH_ORIENTATION_CONFLICT");
                    if(a.Span.FaceID==b.Span.FaceID&&!BRep_Tool::IsClosed(built.Edges.at(id),built.Faces.at(a.Span.FaceID)))
                        throw std::invalid_argument("GEOMETRY_PATCH_DUPLICATE_EDGE_USE");
                    removed[indices[0]]=removed[indices[1]]=true;result.CancelledInternalUses+=2;
                    joinedUses.emplace_back(indices[0],indices[1]);
                    linkOrientation(a.Span.FaceID,b.Span.FaceID,false);
                }
            }
            // IGES faces may carry separate copies of the same boundary curve.
            // Use the kernel's full-edge sewing correspondence only as adjacency
            // evidence: retain the original BRep, curves and source-use identities.
            // Cutting is disabled so a partial overlap cannot erase a whole edge.
            if(selected.size()>1&&std::count(removed.begin(),removed.end(),false)>1)
            {
                BRepBuilderAPI_Sewing sewing(o.DistanceToleranceMm,true,false,false,false);
                sewing.SetMaxTolerance(o.DistanceToleranceMm);sewing.SetLocalTolerancesMode(false);
                for(const auto id:selected)sewing.Add(built.Faces.at(id));
                occ::handle<Message_ProgressIndicator> progress=new CPatchProgress(o.IsCancelled);
                sewing.Perform(progress->Start());check();
                if(sewing.NbMultipleEdges())throw std::invalid_argument("GEOMETRY_PATCH_NONMANIFOLD_EDGE");
                NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> sourceEdges;std::vector<std::uint64_t> edgeIDs{0};
                for(const auto& [id,edge]:built.Edges){sourceEdges.Add(edge);edgeIDs.push_back(id);}
                for(int n=1;n<=sewing.NbContigousEdges();++n)
                {
                    check();const auto& pair=sewing.ContigousEdgeCouple(n);if(pair.Size()!=2)continue;
                    const auto ai=sourceEdges.FindIndex(pair.First()),bi=sourceEdges.FindIndex(pair.Last());
                    if(!ai||!bi||ai==bi)continue;
                    const auto aid=edgeIDs[ai],bid=edgeIDs[bi];
                    if(!byEdge.contains(aid)||!byEdge.contains(bid)||byEdge.at(aid).size()!=1||byEdge.at(bid).size()!=1)continue;
                    const auto aidx=byEdge.at(aid).front(),bidx=byEdge.at(bid).front();
                    const auto& a=uses[aidx];const auto& b=uses[bidx];
                    if(removed[aidx]||removed[bidx]||a.Span.FaceID==b.Span.FaceID)continue;
                    BRepAdaptor_Curve ca(built.Edges.at(aid)),cb(built.Edges.at(bid));
                    const auto startA=ca.Value(a.Span.Reversed?a.Span.Last:a.Span.First),endA=ca.Value(a.Span.Reversed?a.Span.First:a.Span.Last);
                    const auto startB=cb.Value(b.Span.Reversed?b.Span.Last:b.Span.First),endB=cb.Value(b.Span.Reversed?b.Span.First:b.Span.Last);
                    const bool oppositeEnds=startA.Distance(endB)<=o.DistanceToleranceMm&&endA.Distance(startB)<=o.DistanceToleranceMm;
                    const bool sameEnds=startA.Distance(startB)<=o.DistanceToleranceMm&&endA.Distance(endB)<=o.DistanceToleranceMm;
                    if(!oppositeEnds&&!sameEnds)continue;
                    // Independent IGES faces need not arrive with consistent
                    // orientation. Closed edges also need tangent directions.
                    gp_Pnt p;gp_Vec da,db;ca.D1((a.Span.First+a.Span.Last)/2,p,da);cb.D1((b.Span.First+b.Span.Last)/2,p,db);
                    if(a.Span.Reversed)da.Reverse();if(b.Span.Reversed)db.Reverse();
                    const bool flip=da.Dot(db)>0;
                    if((flip&&!sameEnds)||(!flip&&!oppositeEnds))continue;
                    const auto inward=[&](const Use& use,const gp_Vec& tangent){
                        const auto& face=built.Faces.at(use.Span.FaceID);double first,last;
                        const auto pc=BRep_Tool::CurveOnSurface(built.Edges.at(use.Span.EdgeID),face,first,last);
                        if(pc.IsNull())throw std::invalid_argument("GEOMETRY_PATCH_PCURVE_MISSING");
                        const auto uv=pc->Value((first+last)/2);gp_Pnt point;gp_Vec du,dv;
                        BRepAdaptor_Surface(face).D1(uv.X(),uv.Y(),point,du,dv);
                        auto normal=du.Crossed(dv);if(reversedFace.at(use.Span.FaceID))normal.Reverse();
                        return normal.Crossed(tangent);
                    };
                    const auto ia=inward(a,da),ib=inward(b,db);
                    if(ia.SquareMagnitude()<1e-24||ib.SquareMagnitude()<1e-24)continue;
                    // Reversed duplicate sheets have opposite edge directions but
                    // still occupy the same side of the seam. Do not erase them.
                    if(ia.Dot(ib)>0&&ia.IsParallel(ib,o.AngleToleranceRad))continue;
                    removed[aidx]=removed[bidx]=true;result.CancelledInternalUses+=2;
                    joinedUses.emplace_back(aidx,bidx);
                    linkOrientation(a.Span.FaceID,b.Span.FaceID,flip);
                }
            }
            // Orient only this temporary boundary graph. Stored surfaces, their
            // machining-side normals, IDs and exact curve parameters stay intact.
            std::map<std::uint64_t,bool> faceFlips;
            for(const auto seed:selected)if(!faceFlips.contains(seed)){
                faceFlips[seed]=false;std::vector<std::uint64_t> pending{seed};
                while(!pending.empty()){
                    check();const auto face=pending.back();pending.pop_back();
                    for(const auto& [other,flip]:orientationLinks[face]){
                        const bool value=faceFlips.at(face)^flip;
                        if(faceFlips.contains(other)){
                            if(faceFlips.at(other)!=value)throw std::invalid_argument("GEOMETRY_PATCH_ORIENTATION_CONFLICT");
                        }else{faceFlips[other]=value;pending.push_back(other);}
                    }
                }
            }
            for(auto& use:uses)if(faceFlips.at(use.Span.FaceID)){
                use.Span.Reversed=!use.Span.Reversed;std::swap(use.Start,use.End);
            }
            for(const auto& [a,b]:joinedUses){
                parent[root(uses[a].Start)]=root(uses[b].End);parent[root(uses[a].End)]=root(uses[b].Start);
            }
            std::map<std::uint64_t,size_t> outgoing,incoming;
            for(size_t i=0;i<uses.size();++i)if(!removed[i])
            {
                uses[i].Start=root(uses[i].Start);uses[i].End=root(uses[i].End);
                if(!outgoing.emplace(uses[i].Start,i).second||!incoming.emplace(uses[i].End,i).second)
                    throw std::invalid_argument("GEOMETRY_PATCH_BOUNDARY_BRANCH");
            }
            std::map<std::uint64_t,SFaceBoundaryAnalysis> analyzedFaces;
            while(true)
            {
                size_t seed=uses.size();
                for(size_t i=0;i<uses.size();++i)if(!removed[i]&&!incoming.contains(uses[i].Start)){seed=i;break;}
                if(seed==uses.size())for(size_t i=0;i<uses.size();++i)if(!removed[i]){seed=i;break;}
                if(seed==uses.size())break;
                SAnalyticBoundary boundary;std::vector<Segment> segments;const auto start=uses[seed].Start;auto current=seed;
                while(true)
                {
                    check();if(removed[current])throw std::invalid_argument("GEOMETRY_PATCH_REPEATED_TRAVERSAL");
                    removed[current]=true;const auto& use=uses[current];boundary.Spans.push_back(use.Span);
                    const auto& edge=built.Edges.at(use.Span.EdgeID);BRepAdaptor_Curve curve(edge);
                    const auto first=curve.FirstParameter(),last=curve.LastParameter();Segment segment;
                    segment.Type=curve.GetType();segment.Start=curve.Value(use.Span.Reversed?last:first);segment.End=curve.Value(use.Span.Reversed?first:last);
                    segment.Length=GCPnts_AbscissaPoint::Length(curve,first,last,o.DistanceToleranceMm);
                    if(segment.Type==GeomAbs_Circle){segment.Circle=curve.Circle();segment.Sweep=(last-first)*(use.Span.Reversed?-1.:1.);}
                    if(segment.Type==GeomAbs_Ellipse){segment.Ellipse=curve.Ellipse();segment.Sweep=(last-first)*(use.Span.Reversed?-1.:1.);}
                    RecoverEllipse(curve,use.Span.Reversed,segment,o);
                    boundary.PerimeterMm+=segment.Length;segments.push_back(segment);
                    Preview(curve,use.Span.Reversed,boundary);
                    if(use.End==start){boundary.Closed=true;break;}
                    if(!outgoing.contains(use.End))break;
                    current=outgoing.at(use.End);
                }
                bool connected=true;
                for(size_t i=0;i<segments.size();++i)
                {
                    connected&=segments[i].Length>o.DistanceToleranceMm;
                    if(i+1<segments.size()||boundary.Closed){const auto next=(i+1)%segments.size();
                        connected&=Detail::SupportsBoundaryJunction(model,boundary.Spans[i],boundary.Spans[next],
                            {segments[i].End.X(),segments[i].End.Y(),segments[i].End.Z()},
                            {segments[next].Start.X(),segments[next].Start.Y(),segments[next].Start.Z()},o.DistanceToleranceMm);
                    }
                }
                if(!connected)boundary.Diagnostics.push_back("BOUNDARY_GAP_OR_ZERO_LENGTH");
                else if(boundary.Closed)
                {
                    boundary.TopologyClosed=true;
                    ClassifyCircle(segments,boundary,o);if(boundary.GeometryKind=="GeneralContour")ClassifyEllipse(segments,boundary,o);if(boundary.GeometryKind=="GeneralContour")ClassifyRectangle(segments,boundary,o);
                    if(boundary.GeometryKind=="GeneralContour")ClassifySlot(segments,boundary,o);
                    boundary.Valid=boundary.GeometryKind!="GeneralContour";
                    // An unchanged face wire keeps its exact source identity and
                    // validated geometry, including nonstandard engraved outlines.
                    if(std::all_of(boundary.Spans.begin(),boundary.Spans.end(),[&](const auto& span){
                        return span.FaceID==boundary.Spans.front().FaceID&&span.WireID==boundary.Spans.front().WireID;
                    })) {
                        const auto faceID=boundary.Spans.front().FaceID;
                        if(!analyzedFaces.contains(faceID))analyzedFaces.emplace(faceID,AnalyzeBuiltFace(model,faceID,o,built));
                        for(const auto& original:analyzedFaces.at(faceID).Boundaries) {
                            std::set<std::uint64_t> left,right;
                            for(const auto& span:boundary.Spans)left.insert(span.EdgeID);
                            for(const auto& span:original.Spans)right.insert(span.EdgeID);
                            if(original.Valid&&left==right){
                                auto spans=std::move(boundary.Spans);auto preview=std::move(boundary.PreviewPoints);
                                boundary=original;boundary.Spans=std::move(spans);boundary.PreviewPoints=std::move(preview);break;
                            }
                        }
                    }
                    if(!boundary.Valid)boundary.Diagnostics.push_back("PATCH_SELF_INTERSECTION_UNRESOLVED");
                }
                else boundary.Diagnostics.push_back("OPEN_TRIM_REQUIRES_RETAINED_REGION");
                result.Boundaries.push_back(std::move(boundary));
            }
            // Removing shared edges joins face regions. An exterior cycle is
            // proven only when it consists entirely of original outer-wire uses
            // and is the sole such cycle in that connected surface component.
            // Multiple exterior cycles (for example a periodic band) stay unknown.
            std::map<std::uint64_t,std::uint64_t> faceParents;
            for(const auto id:selected)faceParents[id]=id;
            const auto faceRoot=[&](std::uint64_t id){while(faceParents.at(id)!=id)id=faceParents.at(id);return id;};
            for(const auto& [a,b]:joinedUses)
                faceParents[faceRoot(uses[a].Span.FaceID)]=faceRoot(uses[b].Span.FaceID);
            std::set<std::uint64_t> components;for(const auto id:selected)components.insert(faceRoot(id));
            result.ConnectedComponents=components.size();
            std::map<std::uint64_t,std::vector<size_t>> exteriorCycles;
            for(size_t i=0;i<result.Boundaries.size();++i){
                const auto& boundary=result.Boundaries[i];if(!boundary.TopologyClosed||boundary.Spans.empty())continue;
                const bool allOuter=std::all_of(boundary.Spans.begin(),boundary.Spans.end(),[&](const auto& span){
                    const auto outer=BRepTools::OuterWire(built.Faces.at(span.FaceID));
                    for(TopExp_Explorer e(outer,TopAbs_EDGE);e.More();e.Next())if(e.Current().IsSame(built.Edges.at(span.EdgeID)))return true;
                    return false;
                });
                if(allOuter)exteriorCycles[faceRoot(boundary.Spans.front().FaceID)].push_back(i);
            }
            for(const auto& [_,cycles]:exteriorCycles)if(cycles.size()==1)result.Boundaries[cycles.front()].RegionRelation="OuterLoop";
            std::map<std::uint64_t,std::vector<size_t>> componentBoundaries;
            for(size_t i=0;i<result.Boundaries.size();++i)if(!result.Boundaries[i].Spans.empty())
                componentBoundaries[faceRoot(result.Boundaries[i].Spans.front().FaceID)].push_back(i);
            for(const auto& [_,indices]:componentBoundaries)ClassifyProjectedPatch(built,result.Boundaries,indices,o);
            result.Complete=true;return result;
        }
        SFaceBoundaryAnalysis AnalyzeFace(const iCAX::GeometryData::BRepModel& model,std::uint64_t id,const SGeometryAnalysisOptions& o) const override
        {
            if(!std::isfinite(o.DistanceToleranceMm)||o.DistanceToleranceMm<=0||!std::isfinite(o.AngleToleranceRad)
                ||o.AngleToleranceRad<=0||o.AngleToleranceRad>=0.1||o.MaximumEdgeUses==0)
                throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_OPTIONS");
            auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);check();
            if(!built.bOK)throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_BREP");
            return AnalyzeBuiltFace(model,id,o,built);
        }
    private:
        SFaceBoundaryAnalysis AnalyzeBuiltFace(const iCAX::GeometryData::BRepModel& model,std::uint64_t id,
            const SGeometryAnalysisOptions& o,const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built) const
        {
            auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
            const auto found=built.Faces.find(id);if(found==built.Faces.end())throw std::invalid_argument("GEOMETRY_ANALYSIS_FACE_MISSING");
            const auto& face=found->second;
            if(!BRepCheck_Analyzer(face).IsValid())throw std::invalid_argument("GEOMETRY_ANALYSIS_INVALID_FACE");
            const auto outer=BRepTools::OuterWire(face);
            SFaceBoundaryAnalysis result;result.FaceID=id;result.Planar=BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane;
            std::unordered_map<std::uint64_t,const iCAX::GeometryData::BRepEdge*> records;
            for(const auto& edge:model.Edges)records.emplace(edge.Id,&edge);
            std::size_t visited=0;
            for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next())
            {
                check();const auto wire=TopoDS::Wire(wires.Current());SAnalyticBoundary boundary;boundary.FaceID=id;
                boundary.RegionRelation=result.Planar?(wire.IsSame(outer)?"OuterLoop":"InnerLoop"):"Unknown";
                BRepCheck_Wire validation(wire);TopoDS_Edge crossingA,crossingB;
                boundary.Closed=validation.Closed()==BRepCheck_NoError;
                if(!boundary.Closed||validation.SelfIntersect(face,crossingA,crossingB)!=BRepCheck_NoError)
                {boundary.Diagnostics.push_back("BOUNDARY_INVALID_OR_SELF_INTERSECTING");result.Boundaries.push_back(std::move(boundary));continue;}
                bool seam=false,degenerate=false;std::size_t count=0;
                for(TopExp_Explorer edges(wire,TopAbs_EDGE);edges.More();edges.Next())
                {
                    if(++visited>o.MaximumEdgeUses)throw std::runtime_error("GEOMETRY_ANALYSIS_BUDGET_EXHAUSTED");
                    ++count;const auto edge=TopoDS::Edge(edges.Current());
                    seam|=BRep_Tool::IsClosed(edge,face);degenerate|=BRep_Tool::Degenerated(edge);
                }
                if(seam||degenerate)
                {
                    boundary.Diagnostics.push_back(seam?"PERIODIC_WIRE_REQUIRES_PATCH_GRAPH":"DEGENERATE_WIRE_REQUIRES_PATCH_GRAPH");
                    result.Boundaries.push_back(std::move(boundary));continue;
                }
                std::vector<Segment> segments;
                for(BRepTools_WireExplorer edges(wire,face);edges.More();edges.Next())
                {
                    check();const auto edge=edges.Current();std::uint64_t edgeID=0;
                    for(const auto& [key,value]:built.Edges)if(value.IsSame(edge)){edgeID=key;break;}
                    if(!edgeID||!records.contains(edgeID))throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_MAPPING_MISSING");
                    BRepAdaptor_Curve curve(edge);const bool reversed=edge.Orientation()==TopAbs_REVERSED;
                    const double first=curve.FirstParameter(),last=curve.LastParameter();
                    Segment segment;segment.Type=curve.GetType();segment.Start=curve.Value(reversed?last:first);segment.End=curve.Value(reversed?first:last);
                    segment.Length=GCPnts_AbscissaPoint::Length(curve,first,last,o.DistanceToleranceMm);
                    if(segment.Type==GeomAbs_Circle){segment.Circle=curve.Circle();segment.Sweep=(last-first)*(reversed?-1.:1.);}
                    if(segment.Type==GeomAbs_Ellipse){segment.Ellipse=curve.Ellipse();segment.Sweep=(last-first)*(reversed?-1.:1.);}
                    RecoverEllipse(curve,reversed,segment,o);
                    boundary.PerimeterMm+=segment.Length;segments.push_back(segment);
                    SBoundaryCurveSpan span{edgeID,records.at(edgeID)->Curve3Id,first,last,reversed,id};
                    Preview(curve,reversed,boundary);
                    const auto faceRecord=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==id;});
                    if(faceRecord==model.Faces.end())throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_MAPPING_MISSING");
                    for(const auto wireID:faceRecord->WireIds)
                    {
                        const auto sourceWire=std::find_if(model.Wires.begin(),model.Wires.end(),[&](const auto& w){return w.Id==wireID;});
                        if(sourceWire==model.Wires.end())continue;
                        for(size_t c=0;c<sourceWire->Coedges.size();++c)if(sourceWire->Coedges[c].EdgeId==edgeID)
                        {if(span.WireID)throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_USE_AMBIGUOUS");span.WireID=wireID;span.CoedgeIndex=c;}
                    }
                    if(!span.WireID)throw std::runtime_error("GEOMETRY_ANALYSIS_SOURCE_MAPPING_MISSING");
                    boundary.Spans.push_back(span);
                }
                if(segments.empty()||segments.size()!=count){boundary.Diagnostics.push_back("BOUNDARY_INCOMPLETE_TRAVERSAL");result.Boundaries.push_back(std::move(boundary));continue;}
                bool connected=true;
                for(size_t i=0;i<segments.size();++i)
                    connected&=segments[i].Length>o.DistanceToleranceMm&&segments[i].End.Distance(segments[(i+1)%segments.size()].Start)<=o.DistanceToleranceMm;
                if(!connected){boundary.Diagnostics.push_back("BOUNDARY_GAP_OR_ZERO_LENGTH");result.Boundaries.push_back(std::move(boundary));continue;}
                boundary.Valid=true;
                boundary.TopologyClosed=true;
                ClassifyCircle(segments,boundary,o);
                if(boundary.GeometryKind=="GeneralContour")ClassifyEllipse(segments,boundary,o);
                if(boundary.GeometryKind=="GeneralContour")ClassifyRectangle(segments,boundary,o);
                if(boundary.GeometryKind=="GeneralContour")ClassifySlot(segments,boundary,o);
                if(boundary.GeometryKind=="GeneralContour")boundary.Diagnostics.push_back("STANDARD_SHAPE_NOT_PROVEN");
                result.Boundaries.push_back(std::move(boundary));
            }
            result.Complete=true;check();return result;
        }
    };
}
