#include "pch.h"
#include "GeometryServices/BoundaryLeadService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GeomAPI.hxx>
#include <Geom_Plane.hxx>
#include <GeomProjLib.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <BndLib_Add2dCurve.hxx>
#include <Bnd_Box2d.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <ElSLib.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <limits>

namespace
{
    using namespace iCAX::GeometryData;
    using namespace iCAX::GeometryServices;
    template<class T> const T& Record(const std::vector<T>& records,std::uint64_t id)
    {
        for(const auto& r:records)if(r.Id==id)return r;
        throw std::invalid_argument("LEAD_SOURCE_MISSING");
    }
    int Sign(ETopologyOrientation o)
    {
        if(o==ETopologyOrientation::Forward)return 1;
        if(o==ETopologyOrientation::Reversed)return -1;
        throw std::invalid_argument("LEAD_BOUNDARY_ORIENTATION_INVALID");
    }
    Point3 Point(const gp_Pnt& p){return {p.X(),p.Y(),p.Z()};}
    Direction3 Direction(const gp_Vec& p){return {p.X(),p.Y(),p.Z()};}
    [[noreturn]] void BoundaryMismatch(const BoundaryLeadInput& q,const char* reason,const BRepEdge* edge=nullptr)
    {
        std::ostringstream detail;detail<<std::setprecision(17)<<"LEAD_BOUNDARY_MISMATCH: "<<reason<<" face="<<q.FaceId<<" wire="<<q.WireId<<" edge="<<q.EdgeId<<" coedge="<<q.CoedgeIndex<<" parameter="<<q.NativeParameter;
        if(edge)detail<<" range=["<<edge->Range.First<<","<<edge->Range.Last<<"] sameParameter="<<edge->SameParameter<<" degenerated="<<edge->Degenerated;
        throw std::invalid_argument(detail.str());
    }
    double BoundaryParameter(const BoundaryLeadInput& q,const BRepEdge& edge)
    {
        const auto u=q.NativeParameter,first=edge.Range.First,last=edge.Range.Last;
        const auto roundoff=8*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(first),std::abs(last)});
        if(!std::isfinite(u)||u<first-roundoff||u>last+roundoff)BoundaryMismatch(q,"parameter",&edge);
        return std::clamp(u,first,last);
    }
    double SupportTolerance(const BRepEdge& edge,const BRepFace& face)
    {
        // ShapeAnalysis_Surface compares its projection gap with maxpreci
        // using Precision::Confusion(). Apply the same spatial resolution at
        // the caller boundary, including when the nearest support lies just
        // outside the nominal tolerance. The exact curve point stays intact.
        return std::max({1e-5,edge.Tolerance*5,face.Tolerance*5})+Precision::Confusion();
    }
    void SupportDerivatives(const Handle(Geom_Surface)& surface,const gp_Pnt2d& uv,const gp_Pnt& point,
        double tolerance,gp_Pnt& support,gp_Vec& du,gp_Vec& dv)
    {
        surface->D1(uv.X(),uv.Y(),support,du,dv);
        if(point.Distance(support)<=tolerance)return;
        // SameParameter pcurves on imported spline faces are approximations.
        // Refine UV from that local seed at the exact 3D curve point; the same
        // geometric distance check still rejects a curve outside its support.
        ShapeAnalysis_Surface analysis(surface);
        const auto refined=analysis.NextValueOfUV(uv,point,1e-9,tolerance);
        surface->D1(refined.X(),refined.Y(),support,du,dv);
    }
    // A batch belongs to one immutable model read. Reuse its native geometry,
    // but run the complete occurrence, range and support checks at every point.
    class BoundaryFrameBatch
    {
        const BRepModel& Model;
        std::unordered_map<std::uint64_t,Handle(Geom_Curve)> Curves;
        std::unordered_map<std::uint64_t,Handle(Geom2d_Curve)> PCurves;
        std::unordered_map<std::uint64_t,Handle(Geom_Surface)> Surfaces;
    public:
        explicit BoundaryFrameBatch(const BRepModel& model):Model(model){}
        BoundaryFrame At(const BoundaryLeadInput& q)
        {
            const auto& face=Record(Model.Faces,q.FaceId);const auto& wire=Record(Model.Wires,q.WireId);
            if(std::find(face.WireIds.begin(),face.WireIds.end(),q.WireId)==face.WireIds.end()||q.CoedgeIndex>=wire.Coedges.size())
                BoundaryMismatch(q,"occurrence");
            const auto& coedge=wire.Coedges[q.CoedgeIndex];const auto& edge=Record(Model.Edges,q.EdgeId);
            if(coedge.EdgeId!=edge.Id||!edge.SameParameter||edge.Degenerated)
                BoundaryMismatch(q,"edge/range",&edge);
            const auto parameter=BoundaryParameter(q,edge);
            auto [curve,addCurve]=Curves.try_emplace(edge.Curve3Id);
            if(addCurve)curve->second=iCAX::OpenCascade::BuildOpenCascadeCurve(Record(Model.Curves3,edge.Curve3Id).Geometry);
            auto [pcurve,addPCurve]=PCurves.try_emplace(coedge.Curve2Id);
            if(addPCurve)pcurve->second=iCAX::OpenCascade::BuildOpenCascadeCurve2(Record(Model.Curves2,coedge.Curve2Id).Geometry);
            auto [surface,addSurface]=Surfaces.try_emplace(face.Surface3Id);
            if(addSurface)surface->second=iCAX::OpenCascade::BuildOpenCascadeSurface(Record(Model.Surfaces3,face.Surface3Id).Geometry);
            const auto& c=curve->second;const auto& uv=pcurve->second;const auto& s=surface->second;
            if(c.IsNull()||uv.IsNull()||s.IsNull())throw std::invalid_argument("LEAD_SOURCE_MISSING");
            gp_Pnt p,support;gp_Vec t,du,dv;c->D1(parameter,p,t);
            const auto tolerance=SupportTolerance(edge,face);
            SupportDerivatives(s,uv->Value(parameter),p,tolerance,support,du,dv);
            auto n=du.Crossed(dv);
            if(t.SquareMagnitude()<1e-20||n.SquareMagnitude()<1e-20||p.Distance(support)>tolerance){
                std::ostringstream detail;detail<<std::setprecision(17)<<"LEAD_FRAME_DEGENERATE: face="<<q.FaceId<<" wire="<<q.WireId<<" edge="<<q.EdgeId<<" parameter="<<q.NativeParameter
                    <<" tangent2="<<t.SquareMagnitude()<<" normal2="<<n.SquareMagnitude()<<" supportDistance="<<p.Distance(support)<<" tolerance="<<tolerance;
                throw std::invalid_argument(detail.str());
            }
            t.Normalize();n.Normalize();n*=static_cast<double>(Sign(face.Orientation));if(q.Reversed)t.Reverse();
            return {Point(p),Direction(t),Direction(n)};
        }
    };
    std::optional<Bnd_Box2d> ConservativeBounds(const Handle(Geom2d_Curve)& curve,double first,double last)
    {
        const auto type=Geom2dAdaptor_Curve(curve,first,last).GetType();
        // For these types OCCT uses analytic extrema or the spline control
        // hull. Other curve types use an approximation, unsuitable for culling.
        if(type!=GeomAbs_Line&&type!=GeomAbs_Circle&&type!=GeomAbs_Ellipse&&type!=GeomAbs_Hyperbola
            &&type!=GeomAbs_Parabola&&type!=GeomAbs_BezierCurve&&type!=GeomAbs_BSplineCurve)return {};
        Bnd_Box2d bounds;BndLib_Add2dCurve::Add(curve,first,last,1e-8,bounds);return bounds;
    }
    TopoDS_Face TrimmedFace(const BRepModel& model,const BRepFace& face)
    {
        BRepModel subset;subset.Faces={face};subset.RootShapes={{EBRepShapeKind::Face,face.Id}};subset.Surfaces3={Record(model.Surfaces3,face.Surface3Id)};
        std::set<std::uint64_t> edges,curves,pcurves,vertices;
        for(const auto id:face.WireIds){const auto& w=Record(model.Wires,id);subset.Wires.push_back(w);
            for(const auto& c:w.Coedges){edges.insert(c.EdgeId);if(c.Curve2Id)pcurves.insert(c.Curve2Id);}}
        for(const auto id:edges){const auto& e=Record(model.Edges,id);subset.Edges.push_back(e);if(e.Curve3Id)curves.insert(e.Curve3Id);
            if(e.StartVertexId)vertices.insert(e.StartVertexId);if(e.EndVertexId)vertices.insert(e.EndVertexId);}
        for(const auto id:curves)subset.Curves3.push_back(Record(model.Curves3,id));
        for(const auto id:pcurves)subset.Curves2.push_back(Record(model.Curves2,id));
        for(const auto id:vertices)subset.Vertices.push_back(Record(model.Vertices,id));
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(subset);
        if(!built.bOK||!built.Faces.contains(face.Id))throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
        return built.Faces.at(face.Id);
    }
    TopoDS_Face OpeningFace(const BRepModel& model,const BRepFace& face,std::uint64_t wire)
    {
        const auto source=iCAX::OpenCascade::BuildOpenCascadeSurface(Record(model.Surfaces3,face.Surface3Id).Geometry);
        GeomLib_IsPlanarSurface planar(source,1e-7);Handle(Geom_Surface) surface=source;
        if(planar.IsPlanar())surface=new Geom_Plane(planar.Plan());
        BRepBuilderAPI_MakeWire boundary;
        // Rebuild this region from the exact 3D contour. Imported pcurves can
        // have a different parameter scale even when their 3D loop is valid.
        for(const auto& use:Record(model.Wires,wire).Coedges){
            const auto& record=Record(model.Edges,use.EdgeId);const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(Record(model.Curves3,record.Curve3Id).Geometry);
            BRepBuilderAPI_MakeEdge maker(curve,use.Range.First,use.Range.Last);
            if(!maker.IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening edge");
            auto edge=maker.Edge();if(use.Orientation==ETopologyOrientation::Reversed)edge.Reverse();
            Handle(Geom2d_Curve) pcurve;
            if(planar.IsPlanar())pcurve=GeomAPI::To2d(curve,planar.Plan());
            else {double tolerance=1e-8;pcurve=GeomProjLib::Curve2d(curve,use.Range.First,use.Range.Last,surface,tolerance);
                if(tolerance>1e-7)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening boundary projection");}
            if(pcurve.IsNull())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening boundary projection");
            BRep_Builder builder;builder.UpdateEdge(edge,pcurve,surface,TopLoc_Location(),std::max(1e-8,record.Tolerance));
            builder.Range(edge,surface,TopLoc_Location(),use.Range.First,use.Range.Last);boundary.Add(edge);
            if(!boundary.IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening wire");
        }
        // Inside=true normalizes even a reversed inner wire into its bounded
        // region. The original material face's other openings are excluded.
        BRepBuilderAPI_MakeFace bounded(surface,boundary.Wire(),true);
        if(!bounded.IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
        TopoDS_Edge first,last;BRepCheck_Wire validity(boundary.Wire());
        if(validity.Closed()!=BRepCheck_NoError||validity.SelfIntersect(bounded.Face(),first,last)!=BRepCheck_NoError
            ||!BRepCheck_Analyzer(bounded.Face()).IsValid())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: invalid opening");
        return bounded.Face();
    }
    void RequireInside(const TopoDS_Face& region,const CurveSegment3& segment,const gp_Pnt& anchor,bool insideRegion=true)
    {
        const auto surface=BRep_Tool::Surface(region);const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);
        Handle(Geom2d_Curve) projected;
        if(const auto plane=Handle(Geom_Plane)::DownCast(surface))projected=GeomAPI::To2d(curve,plane->Pln());
        else {double tolerance=1e-8;projected=GeomProjLib::Curve2d(curve,segment.Range.First,segment.Range.Last,surface,tolerance);
            if(tolerance>1e-7)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");}
        if(projected.IsNull())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening projection");
        const auto expected=insideRegion?TopAbs_IN:TopAbs_OUT;
        const auto state=[&](double u){return BRepClass_FaceClassifier(region,projected->Value(u),1e-8).State();};
        const auto middle=(segment.Range.First+segment.Range.Last)/2;
        for(const auto u:{segment.Range.First,middle,segment.Range.Last}){
            const auto classified=state(u);
            if(classified==TopAbs_UNKNOWN)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
            if(classified!=expected&&classified!=TopAbs_ON)throw std::invalid_argument("LEAD_CROSSES_MATERIAL");
        }
        struct Boundary2d
        {
            Handle(Geom2d_Curve) Curve;
            double First,Last;
            std::optional<Bnd_Box2d> Bounds;
        };
        std::vector<Boundary2d> boundaries;
        for(TopExp_Explorer edges(region,TopAbs_EDGE);edges.More();edges.Next()){
            double first,last;const auto boundary=BRep_Tool::CurveOnSurface(TopoDS::Edge(edges.Current()),region,first,last);
            if(boundary.IsNull())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
            boundaries.push_back({boundary,first,last,ConservativeBounds(boundary,first,last)});
        }
        if(const auto circle=Handle(Geom2d_Circle)::DownCast(projected);circle&&state(middle)==expected
            &&BRepClass_FaceClassifier(region,circle->Location(),1e-8).State()==expected){
            // If the complete disk has no boundary closer than its radius and
            // its center is in the region, the entire circle (hence this arc)
            // is contained. Exact point/curve extrema avoid the ill-conditioned
            // tangent circle/spline intersection at an imported hole's seam.
            bool diskContained=true;
            Bnd_Box2d disk;disk.Update(circle->Location().X()-circle->Radius(),circle->Location().Y()-circle->Radius(),
                circle->Location().X()+circle->Radius(),circle->Location().Y()+circle->Radius());disk.Enlarge(1e-8);
            for(const auto& piece:boundaries){
                if(piece.Bounds&&piece.Bounds->IsOut(disk))continue;
                const auto& boundary=piece.Curve;const auto first=piece.First,last=piece.Last;
                Geom2dAPI_ProjectPointOnCurve distance(circle->Location(),boundary,first,last);
                if(!distance.Extrema().IsDone()){diskContained=false;break;}
                auto minimum=std::min(circle->Location().Distance(boundary->Value(first)),circle->Location().Distance(boundary->Value(last)));
                if(distance.NbPoints())minimum=std::min(minimum,distance.LowerDistance());
                if(minimum<circle->Radius()-1e-8){diskContained=false;break;}
            }
            if(diskContained)return;
        }
        Handle(Geom2d_Curve) lead=new Geom2d_TrimmedCurve(projected,segment.Range.First,segment.Range.Last);
        std::vector<double> stations{segment.Range.First,segment.Range.Last};
        const auto station=[&](double u){
            // OCCT unwraps a trimmed periodic arc into its positive domain.
            // Return intersection addresses to the stored native interval.
            if(projected->IsPeriodic())u-=std::round((u-(segment.Range.First+segment.Range.Last)/2)/projected->Period())*projected->Period();
            if(u>=segment.Range.First-1e-9&&u<=segment.Range.Last+1e-9)stations.push_back(std::clamp(u,segment.Range.First,segment.Range.Last));
        };
        // Intersect the complete curves and classify every resulting interval.
        // Endpoint-only or fixed-step tests can miss a narrow concave notch.
        const auto leadBounds=ConservativeBounds(projected,segment.Range.First,segment.Range.Last);
        for(const auto& piece:boundaries){
            if(leadBounds&&piece.Bounds&&leadBounds->IsOut(*piece.Bounds))continue;
            const auto& boundary=piece.Curve;const auto first=piece.First,last=piece.Last;
            Geom2dAPI_InterCurveCurve intersection(lead,new Geom2d_TrimmedCurve(boundary,first,last),1e-8);
            if(!intersection.Intersector().IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening intersections");
            // A tangent circle against a rational spline can be reported as
            // a tiny intersection segment within tolerance at the anchor.
            // Split at its bounds and classify the intervals instead of
            // treating every such contact as a material crossing.
            for(int i=1;i<=intersection.NbPoints();++i)station(intersection.Intersector().Point(i).ParamOnFirst());
            for(int i=1;i<=intersection.NbSegments();++i){Handle(Geom2d_Curve) a,b;intersection.Segment(i,a,b);station(a->FirstParameter());station(a->LastParameter());}
        }
        std::sort(stations.begin(),stations.end());
        bool strictInterior=false;
        const auto inside=[&](double u){BRepClass_FaceClassifier classifier(region,projected->Value(u),1e-8);
            if(classifier.State()==TopAbs_UNKNOWN)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
            strictInterior|=classifier.State()==(insideRegion?TopAbs_IN:TopAbs_OUT);
            if(classifier.State()!=(insideRegion?TopAbs_IN:TopAbs_OUT)&&classifier.State()!=TopAbs_ON&&curve->Value(u).Distance(anchor)>1e-7)throw std::invalid_argument("LEAD_CROSSES_MATERIAL");};
        for(const auto u:stations)inside(u);
        for(std::size_t i=1;i<stations.size();++i)if(stations[i]>stations[i-1]+1e-12)inside((stations[i]+stations[i-1])/2);
        if(!strictInterior)throw std::invalid_argument("LEAD_CROSSES_MATERIAL");
    }
    class CBoundaryLeadService final:public IBoundaryLeadService
    {
        AUTO_REGIST_SERVICE(IBoundaryLeadService,CBoundaryLeadService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        BoundaryFrame Frame(const BRepModel& model,const BoundaryLeadInput& q) const override
        {
            return BoundaryFrameBatch(model).At(q);
        }
        std::vector<BoundaryFrame> Frames(const BRepModel& model,const std::vector<BoundaryLeadInput>& queries) const override
        {
            BoundaryFrameBatch batch(model);std::vector<BoundaryFrame> result;result.reserve(queries.size());
            for(const auto& q:queries)result.push_back(batch.At(q));
            return result;
        }
        BoundaryLeadGeometry Build(const BRepModel& model,const BoundaryLeadInput& q) const override
        {
            constexpr double pi=3.14159265358979323846;
            if(!std::isfinite(q.Length)||q.Length<=0||q.Length>1e5||!std::isfinite(q.Angle)
                ||std::abs(q.Angle)>=pi/2||!std::isfinite(q.NativeParameter))throw std::invalid_argument("LEAD_PARAMETERS_INVALID");
            if(q.Arc&&(!std::isfinite(q.Radius)||q.Radius<=0||q.Radius>1e5||q.Length/q.Radius>pi||q.Angle!=0))
                throw std::invalid_argument("LEAD_ARC_PARAMETERS_INVALID");
            const auto& face=Record(model.Faces,q.FaceId);const auto& wire=Record(model.Wires,q.WireId);
            const auto occurrence=std::find(face.WireIds.begin(),face.WireIds.end(),q.WireId);
            if(occurrence==face.WireIds.end()||q.CoedgeIndex>=wire.Coedges.size())BoundaryMismatch(q,"occurrence");
            const auto& coedge=wire.Coedges[q.CoedgeIndex];const auto& edge=Record(model.Edges,q.EdgeId);
            if(coedge.EdgeId!=edge.Id||!edge.SameParameter||edge.Degenerated)
                BoundaryMismatch(q,"edge/range",&edge);
            const auto parameter=BoundaryParameter(q,edge);
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(Record(model.Curves3,edge.Curve3Id).Geometry);
            const auto uvCurve=iCAX::OpenCascade::BuildOpenCascadeCurve2(Record(model.Curves2,coedge.Curve2Id).Geometry);
            const auto surface=iCAX::OpenCascade::BuildOpenCascadeSurface(Record(model.Surfaces3,face.Surface3Id).Geometry);
            if(curve.IsNull()||uvCurve.IsNull()||surface.IsNull())throw std::invalid_argument("LEAD_SOURCE_MISSING");
            gp_Pnt anchor,support;gp_Vec tangent,du,dv;curve->D1(parameter,anchor,tangent);
            const auto tolerance=SupportTolerance(edge,face);
            SupportDerivatives(surface,uvCurve->Value(parameter),anchor,tolerance,support,du,dv);
            auto normal=du.Crossed(dv);
            if(tangent.SquareMagnitude()<1e-20||normal.SquareMagnitude()<1e-20
                ||anchor.Distance(support)>tolerance)throw std::invalid_argument("LEAD_FRAME_DEGENERATE");
            tangent.Normalize();normal.Normalize();
            const auto wi=static_cast<std::size_t>(occurrence-face.WireIds.begin());
            const auto wireSign=face.WireOrientations.empty()?1:Sign(face.WireOrientations.at(wi));
            // The trim loop is oriented on the canonical forward surface. Face
            // orientation in its owning shell must not be applied a second time.
            auto scrap=tangent.Crossed(normal)*static_cast<double>(Sign(coedge.Orientation)*wireSign*(q.FlipSide?-1:1));
            if(scrap.SquareMagnitude()<1e-20)throw std::invalid_argument("LEAD_FRAME_DEGENERATE");
            scrap.Normalize();if(q.Reversed)tangent.Reverse();
            if(q.Anchor){const auto& a=*q.Anchor;
                anchor=gp_Pnt(a.Point.X,a.Point.Y,a.Point.Z);tangent=gp_Vec(a.Tangent.X,a.Tangent.Y,a.Tangent.Z);scrap=gp_Vec(a.ScrapDirection.X,a.ScrapDirection.Y,a.ScrapDirection.Z);
                if(!std::isfinite(anchor.X())||!std::isfinite(anchor.Y())||!std::isfinite(anchor.Z())
                    ||!std::isfinite(tangent.Magnitude())||!std::isfinite(scrap.Magnitude())
                    ||std::abs(tangent.Magnitude()-1)>1e-9||std::abs(scrap.Magnitude()-1)>1e-9||std::abs(tangent.Dot(scrap))>1e-8)
                    throw std::invalid_argument("LEAD_OFFSET_FRAME_INVALID");
            }
            std::optional<TopoDS_Face> opening;
            if(q.RequireInsideWire||q.RequireOutsideWire){
                if(q.RequireInsideWire&&q.RequireOutsideWire)throw std::invalid_argument("LEAD_PARAMETERS_INVALID");
                if(q.RegionBoundary){
                    if(!q.RegionBoundary->Closed||q.RegionBoundary->Segments.empty()||!q.RegionPoint)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: CAM boundary");
                    BRepBuilderAPI_MakeWire boundary;
                    for(const auto& piece:q.RegionBoundary->Segments){const auto c=iCAX::OpenCascade::BuildOpenCascadeCurve(piece.Curve);BRepBuilderAPI_MakeEdge maker(c,piece.Range.First,piece.Range.Last);
                        if(!maker.IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: CAM edge");auto edge=maker.Edge();if(piece.Range.Reversed)edge.Reverse();boundary.Add(edge);if(!boundary.IsDone())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: CAM wire");}
                    BRepBuilderAPI_MakeFace bounded(boundary.Wire(),true);
                    if(!bounded.IsDone()||!BRepCheck_Analyzer(bounded.Face()).IsValid())throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: CAM region");
                    opening=bounded.Face();
                }else opening=OpeningFace(model,face,q.WireId);
                const auto source=q.RegionPoint?gp_Pnt(q.RegionPoint->X,q.RegionPoint->Y,q.RegionPoint->Z):curve->Value(parameter);
                bool resolved=false;
                for(const auto distance:{1e-5,1e-4,1e-3,1e-2}){
                    const auto a=BRepClass_FaceClassifier(*opening,source.Translated(scrap*distance),1e-8).State();
                    const auto b=BRepClass_FaceClassifier(*opening,source.Translated(scrap*-distance),1e-8).State();
                    const auto expected=q.RequireOutsideWire?TopAbs_OUT:TopAbs_IN,opposite=q.RequireOutsideWire?TopAbs_IN:TopAbs_OUT;
                    // At a polygon corner the opposite probe can lie on its
                    // adjoining edge. The chosen direction still needs a
                    // strict region result; the full lead is checked below.
                    if(a==expected&&(b==opposite||b==TopAbs_ON)){resolved=true;break;}
                    if(b==expected&&(a==opposite||a==TopAbs_ON)){scrap.Reverse();resolved=true;break;}
                }
                if(!resolved)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE: opening side");
            }
            BoundaryLeadGeometry result;result.Anchor=Point(anchor);result.Tangent=Direction(tangent);result.ScrapDirection=Direction(scrap);
            if(!q.Arc){
                const auto delta=(scrap*std::cos(q.Angle)+tangent*((q.Entry?-1.:1.)*std::sin(q.Angle)))*q.Length;
                const auto outside=anchor.Translated(delta),from=q.Entry?outside:anchor,to=q.Entry?anchor:outside;
                const auto direction=gp_Vec(from,to).Normalized();
                CurveSegment3 segment;segment.Curve=Line3{{Point(from),Direction(direction)}};segment.Range={0,q.Length,false};
                result.Curve.Segments.push_back(segment);result.Points={Point(from),Point(to)};
            }else{
                const auto center=anchor.Translated(scrap*q.Radius);const auto x=-scrap;
                const auto y=tangent,z=x.Crossed(y);const auto sweep=q.Length/q.Radius;
                Circle3 circle;circle.Placement.Location=Point(center);circle.Placement.XDirection=Direction(x);
                circle.Placement.YDirection=Direction(y);circle.Placement.ZDirection=Direction(z);circle.Radius=q.Radius;
                CurveSegment3 segment;segment.Curve=circle;segment.Range={q.Entry?-sweep:0,q.Entry?0:sweep,false};
                result.Curve.Segments.push_back(segment);
                const auto steps=std::clamp(static_cast<int>(std::ceil(sweep/.04)),2,256);
                for(int i=0;i<=steps;++i){const auto t=segment.Range.First+sweep*i/steps;
                    result.Points.push_back(Point(center.Translated((x*std::cos(t)+y*std::sin(t))*q.Radius)));}
            }
            // Openings require complete-curve containment in their own loop.
            // Outer trims retain the supporting-face material check below.
            if(opening)RequireInside(*opening,result.Curve.Segments.front(),anchor,!q.RequireOutsideWire);
            if(!q.FlipSide&&!opening){
                const auto trimmed=TrimmedFace(model,face);
                // Reconstruction can repair a planar spline support with an
                // equivalent plane and new UV coordinates. Classification must
                // use that very support, not the source surface's UV system.
                const auto trimmedSurface=BRep_Tool::Surface(trimmed);
                const auto trimmedPlane=Handle(Geom_Plane)::DownCast(trimmedSurface);
                const auto exact=iCAX::OpenCascade::BuildOpenCascadeCurve(result.Curve.Segments.front().Curve);
                const auto& range=result.Curve.Segments.front().Range;
                const auto steps=std::clamp(static_cast<int>(std::ceil(q.Length/.05)),16,2048);
                for(int i=0;i<=steps;++i){
                    const auto point=exact->Value(range.First+(range.Last-range.First)*i/steps);
                    if(point.Distance(anchor)<std::max(1e-7,face.Tolerance*5))continue;
                    double u=0,v=0;
                    // A planar target uses its exact plane coordinates. This
                    // is the same orthogonal projection without constructing
                    // and solving a 3D point/surface extrema problem per step.
                    if(trimmedPlane)ElSLib::Parameters(trimmedPlane->Pln(),point,u,v);
                    else {
                        GeomAPI_ProjectPointOnSurf projection(point,trimmedSurface);
                        if(!projection.IsDone()||projection.NbPoints()==0)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
                        projection.LowerDistanceParameters(u,v);
                    }
                    BRepClass_FaceClassifier classify(trimmed,gp_Pnt2d(u,v),1e-8);
                    if(classify.State()==TopAbs_IN)throw std::invalid_argument("LEAD_CROSSES_MATERIAL");
                    if(classify.State()==TopAbs_UNKNOWN)throw std::invalid_argument("LEAD_TRIM_CHECK_UNAVAILABLE");
                }
            }
            return result;
        }
    };
}
