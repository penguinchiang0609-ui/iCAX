#include "pch.h"
#include "CadReferenceService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <Geom_Circle.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <cmath>
#include <unordered_set>

namespace
{
    using namespace iCAX::Geometry;
    RegistrationPoint Point(const gp_Pnt& p) { return {p.X(),p.Y(),p.Z()}; }
    RegistrationPoint Direction(const gp_Dir& p) { return {p.X(),p.Y(),p.Z()}; }
    // Reference queries are scoped to one face. Reconstruct its complete exact
    // dependency closure, including seam pcurves, instead of every part face.
    iCAX::GeometryData::BRepModel ReferenceFace(const iCAX::GeometryData::BRepModel& model,std::uint64_t faceID)
    {
        using namespace iCAX::GeometryData;
        const auto face=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==faceID;});
        if(face==model.Faces.end())throw std::invalid_argument("CAD_REFERENCE_FACE_MISSING");
        BRepModel subset;subset.Faces.push_back(*face);
        subset.RootShapes.push_back({EBRepShapeKind::Face,faceID,face->Orientation});
        std::unordered_set<std::uint64_t> wires(face->WireIds.begin(),face->WireIds.end()),edges,vertices,curves2,curves3;
        for(const auto& wire:model.Wires)if(wires.contains(wire.Id)) {
            subset.Wires.push_back(wire);
            for(const auto& coedge:wire.Coedges){edges.insert(coedge.EdgeId);if(coedge.PCurveStored)curves2.insert(coedge.Curve2Id);}
        }
        for(const auto& edge:model.Edges)if(edges.contains(edge.Id)) {
            subset.Edges.push_back(edge);vertices.insert(edge.StartVertexId);vertices.insert(edge.EndVertexId);curves3.insert(edge.Curve3Id);
        }
        for(const auto& vertex:model.Vertices)if(vertices.contains(vertex.Id))subset.Vertices.push_back(vertex);
        for(const auto& curve:model.Curves2)if(curves2.contains(curve.Id))subset.Curves2.push_back(curve);
        for(const auto& curve:model.Curves3)if(curves3.contains(curve.Id))subset.Curves3.push_back(curve);
        for(const auto& surface:model.Surfaces3)if(surface.Id==face->Surface3Id)subset.Surfaces3.push_back(surface);
        return subset;
    }
    class CCadReferenceService final : public ICadReferenceService
    {
        AUTO_REGIST_SERVICE(ICadReferenceService, CCadReferenceService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        RegistrationPoint EvaluateCurve(const iCAX::GeometryData::CompositeCurve3& path,std::size_t index,double u) const override
        {
            if(index>=path.Segments.size()||!std::isfinite(u))throw std::invalid_argument("CAD_REFERENCE_INVALID_PARAMETER");
            const auto& segment=path.Segments[index];const auto& r=segment.Range;
            const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);
            if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(curve,r))throw std::invalid_argument("CAD_REFERENCE_INVALID_CURVE: "+std::to_string(r.First)+","+std::to_string(r.Last)+" "+(curve.IsNull()?"null":curve->DynamicType()->Name()));
            if(u<r.First||u>r.Last)throw std::invalid_argument("CAD_REFERENCE_INVALID_PARAMETER");
            return Point(curve->Value(u));
        }
        SCurveReferenceResult ResolveCurve(const iCAX::GeometryData::CompositeCurve3& path,const SCurveReferenceQuery& q) const override
        {
            for(auto v:q.NearPoint)if(!std::isfinite(v)||std::abs(v)>1e9)throw std::invalid_argument("CAD_REFERENCE_INVALID_POINT");
            if(!std::isfinite(q.MaximumDistanceMm)||q.MaximumDistanceMm<=0||q.MaximumDistanceMm>100)
                throw std::invalid_argument("CAD_REFERENCE_INVALID_DISTANCE");
            const gp_Pnt probe(q.NearPoint[0],q.NearPoint[1],q.NearPoint[2]);
            SCurveReferenceResult result;double best=std::numeric_limits<double>::infinity();bool ambiguous=false;
            for(std::size_t i=0;i<path.Segments.size();++i) {
                const auto& segment=path.Segments[i];const auto& r=segment.Range;
                const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);
                if(!iCAX::OpenCascade::IsOpenCascadeCurveRangeValid(curve,r))throw std::invalid_argument("CAD_REFERENCE_INVALID_CURVE: "+std::to_string(r.First)+","+std::to_string(r.Last)+" "+(curve.IsNull()?"null":curve->DynamicType()->Name()));
                auto consider=[&](double u) {
                    const auto p=curve->Value(u);const double gap=p.Distance(probe);
                    if(gap>q.MaximumDistanceMm)return;
                    if(gap<best-1e-7){result={Point(p),i,u,gap};best=gap;ambiguous=false;}
                    else if(std::abs(gap-best)<=1e-7) {
                        const gp_Pnt previous(result.Point[0],result.Point[1],result.Point[2]);
                        if(p.Distance(previous)>1e-7)ambiguous=true;
                        else {
                            const auto& previousRange=path.Segments[result.SegmentIndex].Range;
                            const bool atEnd=std::abs(u-r.First)<1e-10||std::abs(u-r.Last)<1e-10;
                            const bool previousAtEnd=std::abs(result.NativeParameter-previousRange.First)<1e-10
                                ||std::abs(result.NativeParameter-previousRange.Last)<1e-10;
                            const bool sameParameter=i==result.SegmentIndex&&std::abs(u-result.NativeParameter)<1e-10;
                            const bool adjacent=i==result.SegmentIndex||i==result.SegmentIndex+1
                                ||(path.Closed&&result.SegmentIndex==0&&i+1==path.Segments.size());
                            // A shared endpoint has one physical anchor. An interior crossing has
                            // multiple source occurrences and must not choose one silently.
                            if(!sameParameter&&!(atEnd&&previousAtEnd&&adjacent))ambiguous=true;
                        }
                    }
                };
                consider(r.Reversed?r.Last:r.First);consider(r.Reversed?r.First:r.Last);
                if(q.Mode==SCurveReferenceQuery::Kind::ClosestPoint) {
                    const auto circle=Handle(Geom_Circle)::DownCast(curve);
                    if(!circle.IsNull()) {
                        const gp_Vec radial(circle->Location(),probe);const gp_Vec axis(circle->Axis().Direction());
                        if(radial.Crossed(axis).Magnitude()<1e-7)throw std::invalid_argument("CAD_REFERENCE_AMBIGUOUS_PROJECTION");
                    }
                    GeomAPI_ProjectPointOnCurve projection(probe,curve,r.First,r.Last);
                    for(int n=1;n<=projection.NbPoints();++n)consider(projection.Parameter(n));
                }
            }
            if(!std::isfinite(best))throw std::invalid_argument("CAD_REFERENCE_NOT_FOUND");
            if(ambiguous)throw std::invalid_argument("CAD_REFERENCE_AMBIGUOUS_PROJECTION");
            return result;
        }
        SCadReferenceResult Resolve(const iCAX::GeometryData::BRepModel& model,const SCadReferenceQuery& q) const override
        {
            for(auto v:q.NearPoint)if(!std::isfinite(v)||std::abs(v)>1e9)throw std::invalid_argument("CAD_REFERENCE_INVALID_POINT");
            if(!std::isfinite(q.MaximumDistanceMm)||q.MaximumDistanceMm<=0||q.MaximumDistanceMm>100)
                throw std::invalid_argument("CAD_REFERENCE_INVALID_DISTANCE");
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(ReferenceFace(model,q.FaceID));
            if(!built.bOK)throw std::runtime_error("CAD_REFERENCE_INVALID_BREP");
            const auto found=built.Faces.find(q.FaceID);
            if(found==built.Faces.end())throw std::invalid_argument("CAD_REFERENCE_FACE_MISSING");
            // Faces in the reconstruction lookup are canonical forward. Query
            // the oriented root occurrence, otherwise reversed faces return the
            // inward surface axis and put a fixed-direction head inside the part.
            auto face=found->second;
            for(TopExp_Explorer occurrence(built.Shape,TopAbs_FACE);occurrence.More();occurrence.Next())
                if(occurrence.Current().IsSame(face)){face=TopoDS::Face(occurrence.Current());break;}
            const gp_Pnt probe(q.NearPoint[0],q.NearPoint[1],q.NearPoint[2]);
            const auto vertex=BRepBuilderAPI_MakeVertex(probe).Vertex();
            auto distance=[&](const TopoDS_Shape& target)->std::pair<double,gp_Pnt>{
                BRepExtrema_DistShapeShape extrema(vertex,target);
                if(!extrema.IsDone()||extrema.NbSolution()<1)throw std::runtime_error("CAD_REFERENCE_PROJECTION_FAILED");
                for(int i=2;i<=extrema.NbSolution();++i)
                    if(extrema.PointOnShape2(i).Distance(extrema.PointOnShape2(1))>1e-7)
                        throw std::invalid_argument("CAD_REFERENCE_AMBIGUOUS_PROJECTION");
                return {extrema.Value(),extrema.PointOnShape2(1)};
            };
            SCadReferenceResult result;result.FaceID=q.FaceID;
            if(q.Mode==SCadReferenceQuery::Kind::SurfacePoint||q.Mode==SCadReferenceQuery::Kind::Plane||q.Mode==SCadReferenceQuery::Kind::CylinderAxis||q.Mode==SCadReferenceQuery::Kind::SurfaceNormal)
            {
                const auto [gap,point]=distance(face);result.DistanceMm=gap;result.Point=Point(point);
                if(gap>q.MaximumDistanceMm)throw std::invalid_argument("CAD_REFERENCE_TOO_FAR");
                if(q.Mode==SCadReferenceQuery::Kind::SurfaceNormal){
                    ShapeAnalysis_Surface analysis(BRep_Tool::Surface(face));const auto uv=analysis.ValueOfUV(point,1e-9);
                    BRepAdaptor_Surface surface(face);gp_Pnt evaluated;gp_Vec du,dv;surface.D1(uv.X(),uv.Y(),evaluated,du,dv);
                    auto normal=du.Crossed(dv);if(normal.SquareMagnitude()<1e-24)throw std::invalid_argument("CAD_REFERENCE_NORMAL_SINGULAR");
                    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();result.Direction=Direction(gp_Dir(normal));
                }
                if(q.Mode==SCadReferenceQuery::Kind::Plane)
                {
                    BRepAdaptor_Surface surface(face);
                    if(surface.GetType()!=GeomAbs_Plane)throw std::invalid_argument("CAD_REFERENCE_REQUIRES_PLANE");
                    auto normal=surface.Plane().Axis().Direction();
                    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
                    result.Direction=Direction(normal);
                }
                if(q.Mode==SCadReferenceQuery::Kind::CylinderAxis) {
                    BRepAdaptor_Surface surface(face);
                    if(surface.GetType()!=GeomAbs_Cylinder)throw std::invalid_argument("CAD_REFERENCE_REQUIRES_CYLINDER");
                    const auto axis=surface.Cylinder().Axis();
                    result.Direction=Direction(axis.Direction());
                    result.Point=Point(axis.Location().Translated(gp_Vec(axis.Direction())*gp_Vec(axis.Location(),probe).Dot(gp_Vec(axis.Direction()))));
                }
                return result;
            }
            double best=std::numeric_limits<double>::infinity();bool ambiguous=false;
            auto consider=[&](SCadReferenceResult candidate) {
                if(candidate.DistanceMm>q.MaximumDistanceMm)return;
                if(candidate.DistanceMm<best-1e-7){result=candidate;best=candidate.DistanceMm;ambiguous=false;}
                else if(std::abs(candidate.DistanceMm-best)<=1e-7)
                {
                    double delta=0;for(int i=0;i<3;++i)delta+=std::abs(candidate.Point[i]-result.Point[i])+std::abs(candidate.Direction[i]-result.Direction[i]);
                    if(delta>1e-7)ambiguous=true;
                }
            };
            if(q.Mode==SCadReferenceQuery::Kind::Vertex)
            {
                for(TopExp_Explorer it(face,TopAbs_VERTEX);it.More();it.Next())
                {
                    const auto p=BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
                    SCadReferenceResult candidate;candidate.FaceID=q.FaceID;candidate.Point=Point(p);candidate.DistanceMm=p.Distance(probe);consider(candidate);
                }
            }
            else
            {
                for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())
                {
                    const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
                    BRepAdaptor_Curve curve(edge);
                    if(q.Mode==SCadReferenceQuery::Kind::Line&&curve.GetType()!=GeomAbs_Line)continue;
                    if(q.Mode==SCadReferenceQuery::Kind::CircleCenter&&curve.GetType()!=GeomAbs_Circle)continue;
                    SCadReferenceResult candidate;candidate.FaceID=q.FaceID;
                    if(q.Mode==SCadReferenceQuery::Kind::Line) {
                        // A bounded straight segment has one closest point. Near
                        // its rim, shape extrema can report tolerance-duplicate
                        // solutions; do not turn those into ambiguous snapping.
                        const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
                        const gp_Vec segment(a,b);const double squared=segment.SquareMagnitude();
                        if(squared<=1e-20)continue;
                        const double t=std::clamp(gp_Vec(a,probe).Dot(segment)/squared,0.,1.);
                        candidate.DistanceMm=probe.Distance(a.Translated(segment*t));
                    } else candidate.DistanceMm=distance(edge).first;
                    if(candidate.DistanceMm>q.MaximumDistanceMm)continue;
                    for(const auto& [id,mapped]:built.Edges)if(edge.IsSame(mapped)){candidate.EdgeID=id;break;}
                    if(!candidate.EdgeID)throw std::runtime_error("CAD_REFERENCE_EDGE_ID_MISSING");
                    if(q.Mode==SCadReferenceQuery::Kind::CircleCenter){candidate.Point=Point(curve.Circle().Location());candidate.Direction=Direction(curve.Circle().Axis().Direction());}
                    else
                    {
                        const double first=curve.FirstParameter(),last=curve.LastParameter();
                        const double length=GCPnts_AbscissaPoint::Length(curve,first,last,1e-8);
                        GCPnts_AbscissaPoint middle(1e-8,curve,length/2,first);
                        if(!middle.IsDone())throw std::runtime_error("CAD_REFERENCE_MIDPOINT_FAILED");
                        candidate.Point=Point(curve.Value(middle.Parameter()));
                        if(q.Mode==SCadReferenceQuery::Kind::Line)candidate.Direction=Direction(curve.Line().Direction());
                    }
                    consider(candidate);
                }
            }
            if(!std::isfinite(best))throw std::invalid_argument("CAD_REFERENCE_NOT_FOUND: click closer to the requested exact feature");
            if(ambiguous)throw std::invalid_argument("CAD_REFERENCE_AMBIGUOUS: choose away from shared endpoints");
            return result;
        }
    };
}
