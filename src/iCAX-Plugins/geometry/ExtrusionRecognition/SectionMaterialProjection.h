#pragma once
#include "SectionProjectionMultiplicity.h"
#include "OpenCascadeResourceImport/OpenCascadeFaceIntegration.h"
#include "BRepBuilderAPI_MakeWire.hxx"
#include "BRepBuilderAPI_MakeEdge.hxx"
#include "BRep_Builder.hxx"
#include "BRepTopAdaptor_FClass2d.hxx"
#include "Geom2dConvert.hxx"
#include "GeomConvert.hxx"
#include "Geom_TrimmedCurve.hxx"
#include <Geom_Plane.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include "BRepAlgoAPI_Fuse.hxx"
#include "GeomLib_IsPlanarSurface.hxx"
#include "BRepTools_WireExplorer.hxx"
#include "TopTools_IndexedMapOfShape.hxx"
#include <TopTools_ListOfShape.hxx>
#include "gp_Pln.hxx"
#include <BRepLib.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepCheck_Result.hxx>
#include "functional"
#include "string"
#include "utility"
namespace iCAX::ExtrusionRecognition::section_detail {
inline TopoDS_Face CanonicalPlanarTrimFace(const TopoDS_Face& original) {
    const auto source=TopoDS::Face(original.Oriented(TopAbs_FORWARD));const auto support=BRep_Tool::Surface(source);
    if(support.IsNull()||GeomAdaptor_Surface(support).GetType()==GeomAbs_Plane)return {};
    double u,U,v,V;BRepTools::UVBounds(source,u,U,v,V);const auto center=support->Value((u+U)/2,(v+V)/2);
    const double scale=std::max({1.,std::abs(center.X()),std::abs(center.Y()),std::abs(center.Z())});
    GeomLib_IsPlanarSurface proof(support,256*std::numeric_limits<double>::epsilon()*scale);if(!proof.IsPlanar())return {};
    const auto plane=proof.Plan();const Handle(Geom_Surface) target=new Geom_Plane(plane);gp_Trsf local;local.SetTransformation(plane.Position());
    BRep_Builder builder;TopoDS_Face result;builder.MakeFace(result,target,Precision::Confusion());
    for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More();wi.Next()) {
        BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer it(TopoDS::Wire(wi.Current()),source);it.More();it.Next()) {
            const auto edge=it.Current();if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,source))return {};
            double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,source,a,b);if(pc.IsNull())return {};
            auto lift=BRepBuilderAPI_MakeEdge(pc,support,a,b).Edge();if(!BRepLib::BuildCurve3d(lift,1e-8))return {};
            const auto curve=BRep_Tool::Curve(lift,a,b);auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,a,b));spline->Transform(local);
            NCollection_Array1<gp_Pnt2d> poles(1,spline->NbPoles());NCollection_Array1<double> weights(1,spline->NbPoles()),knots(1,spline->NbKnots());NCollection_Array1<int> mult(1,spline->NbKnots());
            for(int i=1;i<=spline->NbPoles();++i){const auto p=spline->Pole(i);if(std::abs(p.Z())>Precision::Confusion())return {};poles(i)=gp_Pnt2d(p.X(),p.Y());weights(i)=spline->Weight(i);}
            for(int i=1;i<=spline->NbKnots();++i){knots(i)=spline->Knot(i);mult(i)=spline->Multiplicity(i);}
            const Handle(Geom2d_Curve) mapped=new Geom2d_BSplineCurve(poles,weights,knots,mult,spline->Degree(),spline->IsPeriodic());
            BRepBuilderAPI_MakeEdge make(mapped,target,spline->FirstParameter(),spline->LastParameter());if(!make.IsDone())return {};
            auto native=make.Edge();if(!BRepLib::BuildCurve3d(native,1e-8))return {};native.Orientation(edge.Orientation());wire.Add(native);
        }
        if(!wire.IsDone())return {};builder.Add(result,wire.Wire());
    }
    result.Orientation(original.Orientation());if(!BRepCheck_Analyzer(result,true).IsValid())return {};
    const double before=iCAX::OpenCascade::integration_detail::RefinedArea(original),after=iCAX::OpenCascade::integration_detail::RefinedArea(result);
    if(std::abs(before-after)>1e-9*std::max(1.,before))return {};return result;
}
inline bool NeedsPreciseTrimTopology(const TopoDS_Face& face,double tolerance)
{
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())
        if(BRep_Tool::Tolerance(TopoDS::Edge(it.Current()))>tolerance)return true;
    for(TopExp_Explorer it(face,TopAbs_VERTEX);it.More();it.Next())
        if(BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()))>tolerance)return true;
    return false;
}

inline TopoDS_Face PreciseTrimAnalysisFace(const TopoDS_Face& original,double tolerance,std::string& rejection)
{
    const auto source=TopoDS::Face(original.Oriented(TopAbs_FORWARD));BRep_Builder builder;
    TopLoc_Location location;const auto surface=BRep_Tool::Surface(source,location);
    TopoDS_Face result;builder.MakeFace(result,surface,location,Precision::Confusion());
    TopTools_IndexedMapOfShape vertices,edges;TopExp::MapShapes(source,TopAbs_VERTEX,vertices);TopExp::MapShapes(source,TopAbs_EDGE,edges);
    struct STrim {Handle(Geom_Curve) Curve;Handle(Geom2d_Curve) PCurve;double First=0,Last=0,Error=0;};
    std::vector<STrim> trims(edges.Extent()+1);std::vector<gp_Pnt> points(vertices.Extent()+1);
    std::vector<bool> present(vertices.Extent()+1,false);std::vector<double> errors(vertices.Extent()+1,Precision::Confusion());
    for(int i=1;i<=edges.Extent();++i) {
        const auto edge=TopoDS::Edge(edges(i).Oriented(TopAbs_FORWARD));
        if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,source)) {rejection="A high-tolerance seam/degenerate trim needs an explicit parameter map";return {};}
        double first,last;const auto pc=BRep_Tool::CurveOnSurface(edge,source,first,last);
        if(pc.IsNull()){rejection="Source trim has no surface parameter curve";return {};}
        const auto copy=Handle(Geom2d_Curve)::DownCast(pc->Copy());TopoDS_Edge lift;builder.MakeEdge(lift);
        builder.UpdateEdge(lift,copy,source,Precision::Confusion());builder.Range(lift,source,first,last);
        if(!BRepLib::BuildCurve3d(lift,Precision::Confusion())){rejection="Cannot reconstruct an original surface trim";return {};}
        BRepLib_CheckCurveOnSurface check(lift,source);check.Perform();
        if(!check.IsDone()||check.MaxDistance()>Precision::Confusion()){rejection="Original surface-trim lift exceeds native precision";return {};}
        double a,b;const auto curve=BRep_Tool::Curve(lift,a,b);
        if(curve.IsNull()||a!=first||b!=last){rejection="Surface-trim parameter range changed during lifting";return {};}
        trims[i]={curve,copy,first,last,check.MaxDistance()};TopoDS_Vertex v0,v1;TopExp::Vertices(edge,v0,v1);
        for(int end=0;end<2;++end) {
            const auto p=curve->Value(end?last:first);const int at=vertices.FindIndex(end?v1:v0);
            if(!present[at]){present[at]=true;points[at]=p;}
            errors[at]=std::max(errors[at],p.Distance(points[at])+check.MaxDistance()+Precision::Confusion());
            if(errors[at]>tolerance){rejection="Original surface-trim vertex disagreement="+std::to_string(errors[at])+" vertex="+std::to_string(at);return {};}
        }
    }
    std::vector<TopoDS_Vertex> targetVertices(vertices.Extent()+1);std::vector<TopoDS_Edge> targetEdges(edges.Extent()+1);
    for(int i=1;i<=vertices.Extent();++i)builder.MakeVertex(targetVertices[i],points[i],errors[i]);
    for(int i=1;i<=edges.Extent();++i) {
        const auto edge=TopoDS::Edge(edges(i).Oriented(TopAbs_FORWARD));TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);
        const auto& trim=trims[i];BRepBuilderAPI_MakeEdge make(trim.Curve,targetVertices[vertices.FindIndex(a)],targetVertices[vertices.FindIndex(b)],trim.First,trim.Last);
        if(!make.IsDone()){rejection="Cannot reconstruct a consistent surface-trim edge";return {};}
        targetEdges[i]=make.Edge();builder.UpdateEdge(targetEdges[i],trim.PCurve,result,std::max(Precision::Confusion(),trim.Error));
        builder.Range(targetEdges[i],result,trim.First,trim.Last);builder.SameParameter(targetEdges[i],true);builder.SameRange(targetEdges[i],true);
    }
    for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More();wi.Next()) {
        BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer ei(TopoDS::Wire(wi.Current()),source);ei.More();ei.Next()) {
            auto edge=targetEdges[edges.FindIndex(ei.Current())];edge.Orientation(ei.Current().Orientation());wire.Add(edge);
        }
        if(!wire.IsDone()){rejection="Original surface trims do not form a consistent wire";return {};}
        builder.Add(result,wire.Wire());
    }
    result.Orientation(original.Orientation());
    if(!BRepCheck_Analyzer(result).IsValid()){rejection="Reconstructed original surface-trim topology is invalid";return {};}
    return result;
}

// Original faces stay unchanged. Only their derived planar material regions are united.
// Projection contracts distances: derive endpoint precision in YZ instead of inheriting axial tolerances.
inline TopoDS_Face ProjectSingleValuedFace(const TopoDS_Face& source,double tolerance,std::string* rejection=nullptr) {
    const auto fail=[&](const std::string& reason){if(rejection)*rejection=reason;return TopoDS_Face{};};
    BRep_Builder topology;TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(source,TopAbs_VERTEX,vertices);
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(source,TopAbs_EDGE,edges);
    struct SProjectedTrim {Handle(Geom_BSplineCurve) Curve;bool Collapsed=false;};
    std::vector<SProjectedTrim> trims(edges.Extent()+1);
    std::vector<TopoDS_Vertex> projectedVertices(vertices.Extent()+1);
    std::vector<gp_Pnt> anchors(vertices.Extent()+1);
    std::vector<bool> anchored(vertices.Extent()+1,false);
    std::vector<double> endpointErrors(vertices.Extent()+1,0.0);
    std::vector<int> parent(vertices.Extent()+1);
    for(int i=1;i<=vertices.Extent();++i)parent[i]=i;
    const auto root=[&](int vertex){
        while(parent[vertex]!=vertex)vertex=parent[vertex];
        return vertex;
    };
    for(int index=1;index<=edges.Extent();++index) {
        const auto edge=TopoDS::Edge(edges(index).Oriented(TopAbs_FORWARD));
        if(BRep_Tool::Degenerated(edge))continue;
        double first,last;const auto pc=BRep_Tool::CurveOnSurface(edge,source,first,last);
        if(pc.IsNull())return fail("Original trim has no surface parameter curve");
        // A face is trimmed on its surface. An independently toleranced 3D
        // edge may disagree with that trim (including in original IGES).
        // Lift the original pcurve on the unchanged support in a NEW edge;
        // never inherit its potentially large axial 3D edge tolerance.
        TopoDS_Edge lifted;topology.MakeEdge(lifted);topology.UpdateEdge(lifted,pc,source,Precision::Confusion());
        topology.Range(lifted,source,first,last);
        if(!BRepLib::BuildCurve3d(lifted,Precision::Confusion()))return fail("Original surface trim cannot be lifted at native precision");
        BRepLib_CheckCurveOnSurface check(lifted,source);check.Perform();
        if(!check.IsDone()||check.MaxDistance()>Precision::Confusion())
            return fail("Lifted surface trim exceeds native representation precision");
        double curveFirst,curveLast;const auto curve=BRep_Tool::Curve(lifted,curveFirst,curveLast);
        if(curve.IsNull())return fail("Lifted surface trim has no finite 3D curve");
        const auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,curveFirst,curveLast));
        const auto projected=Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
        for(int i=1;i<=projected->NbPoles();++i) {
            const auto p=projected->Pole(i);
            projected->SetPole(i,gp_Pnt(p.Y(),p.Z(),0));
        }
        bool collapsed=true;
        const auto origin=projected->Value(projected->FirstParameter());
        for(int i=1;i<=projected->NbPoles()&&collapsed;++i)
            collapsed=projected->Weight(i)>0&&projected->Pole(i).Distance(origin)<=Precision::Confusion();
        trims[index]={projected,collapsed};
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);
        if(a.IsNull()||b.IsNull())return fail("Original trim has no finite endpoints");
        // A generator can project to a point even when its full 3D trim
        // encloses material on the source patch. Its projected vertex is
        // shared by the neighboring trims, which still form a finite wire.
        if(collapsed)parent[root(vertices.FindIndex(b))]=root(vertices.FindIndex(a));
        for(int end=0;end<2;++end) {
            const auto vertex=end?b:a;const auto p=curve->Value(end?curveLast:curveFirst);
            const gp_Pnt flat(p.Y(),p.Z(),0);const int at=vertices.FindIndex(vertex);
            if(!anchored[at]){anchors[at]=flat;anchored[at]=true;}
            endpointErrors[at]=std::max(endpointErrors[at],flat.Distance(anchors[at])+check.MaxDistance());
        }
    }
    for(int i=1;i<=vertices.Extent();++i) {
        const int owner=root(i);
        if(i==owner||!anchored[i])continue;
        if(!anchored[owner]){anchors[owner]=anchors[i];anchored[owner]=true;}
        endpointErrors[owner]=std::max(endpointErrors[owner],anchors[owner].Distance(anchors[i])+endpointErrors[i]);
    }
    for(int i=1;i<=vertices.Extent();++i) {
        if(!anchored[i]||root(i)!=i)continue;
        const double precision=endpointErrors[i]+Precision::Confusion();
        if(precision>tolerance)return fail("Projected surface-trim endpoint disagreement="+std::to_string(precision));
        topology.MakeVertex(projectedVertices[i],anchors[i],precision);
    }
    const auto sourceOuter=BRepTools::OuterWire(source);TopoDS_Wire outer;std::vector<TopoDS_Wire> holes;
    for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More();wi.Next()) {
        if(wi.Current().Orientation()==TopAbs_INTERNAL||wi.Current().Orientation()==TopAbs_EXTERNAL)continue;
        BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer ei(TopoDS::Wire(wi.Current()),source);ei.More();ei.Next()) {
            const auto edge=ei.Current();if(BRep_Tool::Degenerated(edge))continue;
            const auto& trim=trims[edges.FindIndex(edge)];
            if(trim.Collapsed)continue;
            const auto& projected=trim.Curve;
            if(projected.IsNull())return fail("A projected trim support is missing");
            TopoDS_Vertex a,b;TopExp::Vertices(TopoDS::Edge(edge.Oriented(TopAbs_FORWARD)),a,b);
            BRepBuilderAPI_MakeEdge make(projected,projectedVertices[root(vertices.FindIndex(a))],
                projectedVertices[root(vertices.FindIndex(b))],projected->FirstParameter(),projected->LastParameter());
            if(!make.IsDone())return fail("Projected trim edge construction error="+std::to_string(make.Error()));
            auto piece=make.Edge();if(edge.Orientation()==TopAbs_REVERSED)piece.Reverse();wire.Add(piece);
        }
        if(!wire.IsDone())return fail("Projected trim wire construction error="+std::to_string(wire.Error()));
        if(wi.Current().IsSame(sourceOuter))outer=wire.Wire();else holes.push_back(wire.Wire());
    }
    if(outer.IsNull())return fail("Projected trim has no outer wire");
    const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));BRepBuilderAPI_MakeFace bounded(plane,outer,false);
    if(!bounded.IsDone())return fail("Projected trim cannot form a finite face");
    // Finite/infinite orientation is a native topology construction test.
    // A work-level boundary allowance can erase a narrow, valid strip in
    // the classifier's polygonal approximation. Retain native precision;
    // the later source/cavity checks still use the configured work precision.
    const bool reverse=BRepTopAdaptor_FClass2d(bounded.Face(),Precision::Confusion()).PerformInfinitePoint()==TopAbs_IN;
    if(reverse){outer.Reverse();bounded=BRepBuilderAPI_MakeFace(plane,outer,false);}
    for(auto hole:holes){if(reverse)hole.Reverse();bounded.Add(hole);}
    BRepCheck_Analyzer validation(bounded.Face());
    if(!validation.IsValid()) {
        std::string reason="Projected surface-trim face is invalid";
        for(TopAbs_ShapeEnum type:{TopAbs_FACE,TopAbs_WIRE,TopAbs_EDGE}) {
            int index=0;
            for(TopExp_Explorer it(bounded.Face(),type);it.More();it.Next()) {
                ++index;const auto& result=validation.Result(it.Current());
                for(const auto status:result->Status())if(status!=BRepCheck_NoError)
                    reason+=" type="+std::to_string(type)+" index="+std::to_string(index)+" status="+std::to_string(status);
                result->InitContextIterator();for(;result->MoreShapeInContext();result->NextShapeInContext())
                    for(const auto status:result->StatusOnShape())if(status!=BRepCheck_NoError)
                        reason+=" context-type="+std::to_string(type)+" index="+std::to_string(index)+" status="+std::to_string(status);
            }
        }
        return fail(reason);
    }
    return bounded.Face();
}

inline TopoDS_Shape UniteMaterialProjections(const std::vector<std::pair<std::uint64_t,TopoDS_Face>>& source,
    double tolerance,const std::function<void(const std::string&)>& progress,std::vector<std::string>& diagnostics)
{
    std::vector<std::pair<double,TopoDS_Face>> regions;
    for(const auto& [id,original]:source) {
        auto face=original;
        if(NeedsPreciseTrimTopology(face,tolerance)) {
            std::string reason;face=PreciseTrimAnalysisFace(original,tolerance,reason);
            if(face.IsNull())throw std::runtime_error("Cannot build precise analysis trims for face="+std::to_string(id)+": "+reason);
        }
        if(progress)progress("projecting trimmed material face="+std::to_string(id));
        const GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face),tolerance);
        std::vector<TopoDS_Face> patches;
        if(planar.IsPlanar()) {
            if(std::abs(planar.Plan().Axis().Direction().X())<=Precision::Angular())continue;
            patches.push_back(face);
        } else patches=SingleValuedMaterialPatches(face,tolerance);
        for(const auto& patch:patches) {
            std::string reason;
            const auto projected=ProjectSingleValuedFace(patch,tolerance,&reason);
            if(projected.IsNull()) {
                diagnostics.push_back("No valid single-valued material footprint from face="+std::to_string(id)+": "+reason);continue;
            }
            const double area=iCAX::OpenCascade::integration_detail::RefinedArea(projected);
            if(area>0)regions.emplace_back(area,projected);
        }
    }
    std::stable_sort(regions.begin(),regions.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    TopoDS_Shape result;
    for(const auto& [area,face]:regions) {
        if(result.IsNull()){result=face;continue;}
        TopTools_ListOfShape arguments,tools;arguments.Append(result);tools.Append(face);
        BRepAlgoAPI_Fuse combine;combine.SetArguments(arguments);combine.SetTools(tools);
        combine.SetNonDestructive(true);combine.Build();
        if(!combine.IsDone())throw std::runtime_error("Exact projected material-region union failed");
        combine.SimplifyResult(true,true);result=combine.Shape();
        if(!BRepCheck_Analyzer(result).IsValid())throw std::runtime_error("Projected material union has invalid topology");
    }
    diagnostics.push_back("United "+std::to_string(regions.size())+" bounded material footprints at measured projected endpoint precision");
    return result;
}

}
