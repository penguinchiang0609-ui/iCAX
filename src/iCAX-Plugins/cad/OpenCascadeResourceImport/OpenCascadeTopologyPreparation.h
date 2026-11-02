#pragma once

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepTools_ReShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace iCAX::OpenCascade::topology_detail {
struct Preparation final {
    TopoDS_Shape Shape;
    TopoDS_Shape AttemptedShape;
    bool SourceValid=false,Valid=false;
    std::vector<std::string> Diagnostics;
};
inline double Area(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::SurfaceProperties(shape,p,1e-9);return p.Mass();}
inline int Count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {TopTools_IndexedMapOfShape m;TopExp::MapShapes(shape,type,m);return m.Extent();}
inline double Volume(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);double result=0;
    for(int i=1;i<=solids.Extent();++i){GProp_GProps p;BRepGProp::VolumeProperties(solids(i),p,1e-9);result+=p.Mass();}return result;
}
inline double MaximumTolerance(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    double result=0;for(TopExp_Explorer it(shape,type);it.More();it.Next())
        result=std::max(result,type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));
    return result;
}
inline bool HasStatus(const BRepCheck_Analyzer& check,const TopoDS_Face& face,BRepCheck_Status status) {
    const auto result=check.Result(face);if(result.IsNull())return false;
    for(const auto s:result->Status())if(s==status)return true;return false;
}

// Only storage order changes. OCCT's wire containment check probes the midpoint
// of the first usable edge; a tiny tangent-contact edge can return ON instead
// of IN. Starting at the longest edge avoids that ill-conditioned witness.
inline TopoDS_Face RotateWireStorage(const TopoDS_Face& face) {
    BRep_Builder builder;auto result=TopoDS::Face(face.EmptyCopied());
    for(TopoDS_Iterator w(face);w.More();w.Next()) {
        if(w.Value().ShapeType()!=TopAbs_WIRE){builder.Add(result,w.Value());continue;}
        std::vector<TopoDS_Shape> edges;std::size_t start=0;double longest=-1;
        for(TopoDS_Iterator e(w.Value());e.More();e.Next()){
            edges.push_back(e.Value());GProp_GProps p;BRepGProp::LinearProperties(e.Value(),p);
            if(p.Mass()>longest){longest=p.Mass();start=edges.size()-1;}
        }
        auto wire=TopoDS::Wire(w.Value().EmptyCopied());
        for(std::size_t k=0;k<edges.size();++k)builder.Add(wire,edges[(start+k)%edges.size()]);
        builder.Add(result,wire);
    }
    return result;
}

inline bool CollapseLineFace(const TopoDS_Face& face,const TopoDS_Shape& owner,
    const Handle(BRepTools_ReShape)& changes,double tolerance) {
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(face,TopAbs_EDGE,edges);
    if(edges.Extent()!=2)return false;
    // Keep the original representative with the larger declared fit budget:
    // a shared edge must still carry both original curve-on-face encodings.
    // No edge tolerance is increased or newly chosen to make a check pass.
    const bool first=BRep_Tool::Tolerance(TopoDS::Edge(edges(1)))>=BRep_Tool::Tolerance(TopoDS::Edge(edges(2)));
    const auto a=TopoDS::Edge(edges(first?1:2).Oriented(TopAbs_FORWARD)),b=TopoDS::Edge(edges(first?2:1).Oriented(TopAbs_FORWARD));
    if(BRep_Tool::Degenerated(a)||BRep_Tool::Degenerated(b))return false;
    if(BRepAdaptor_Curve(a).GetType()!=GeomAbs_Line||BRepAdaptor_Curve(b).GetType()!=GeomAbs_Line)return false;
    double a0,a1,b0,b1;const auto ca=BRep_Tool::Curve(a,a0,a1),cb=BRep_Tool::Curve(b,b0,b1);
    if(ca.IsNull()||cb.IsNull()||!(a1>a0)||!(b1>b0))return false;
    const bool reverse=ca->Value(a0).Distance(cb->Value(b1))<ca->Value(a0).Distance(cb->Value(b0));
    if(ca->Value(a0).Distance(cb->Value(reverse?b1:b0))>tolerance
        ||ca->Value(a1).Distance(cb->Value(reverse?b0:b1))>tolerance)return false;
    // Retain one original exact 3D line and its vertices. Transfer only the
    // other edge's linear pcurves, with an affine parameter change.
    BRep_Builder builder;auto merged=TopoDS::Edge(a.EmptyCopied());
    for(TopoDS_Iterator v(a);v.More();v.Next())builder.Add(merged,v.Value());
    for(TopExp_Explorer f(owner,TopAbs_FACE);f.More();f.Next()){
        const auto adjacent=TopoDS::Face(f.Current().Oriented(TopAbs_FORWARD));if(adjacent.IsSame(face))continue;
        bool uses=false,usesA=false;TopAbs_Orientation orientationA=TopAbs_FORWARD;
        for(TopExp_Explorer e(adjacent,TopAbs_EDGE);e.More();e.Next()){
            if(e.Current().IsSame(b))uses=true;
            if(e.Current().IsSame(a)){usesA=true;orientationA=e.Current().Orientation();}
        }
        if(!uses)continue;
        double first,last;const auto pc=BRep_Tool::CurveOnSurface(b,adjacent,first,last);
        if(pc.IsNull()||Geom2dAdaptor_Curve(pc,first,last).GetType()!=GeomAbs_Line)return false;
        if(std::abs(first-b0)>1e-12||std::abs(last-b1)>1e-12)return false;
        TColgp_Array1OfPnt2d poles(1,2);poles(1)=pc->Value(reverse?last:first);poles(2)=pc->Value(reverse?first:last);
        TColStd_Array1OfReal knots(1,2);knots(1)=a0;knots(2)=a1;
        TColStd_Array1OfInteger mults(1,2);mults.Init(2);
        Handle(Geom2d_Curve) replacement=new Geom2d_BSplineCurve(poles,knots,mults,1);
        if(usesA){
            // The collapsed strip can border the same periodic face twice.
            // Its shared edge becomes a seam and must retain BOTH pcurves.
            double firstA,lastA;const auto pcA=BRep_Tool::CurveOnSurface(a,adjacent,firstA,lastA);
            if(pcA.IsNull()||std::abs(firstA-a0)>1e-12||std::abs(lastA-a1)>1e-12)return false;
            builder.UpdateEdge(merged,orientationA==TopAbs_FORWARD?pcA:replacement,
                orientationA==TopAbs_FORWARD?replacement:pcA,adjacent,BRep_Tool::Tolerance(a));
        }else builder.UpdateEdge(merged,replacement,adjacent,BRep_Tool::Tolerance(a));
        builder.Range(merged,adjacent,a0,a1);
    }
    TopoDS_Vertex av0,av1,bv0,bv1;TopExp::Vertices(a,av0,av1);TopExp::Vertices(b,bv0,bv1);
    if(av0.IsNull()||av1.IsNull()||bv0.IsNull()||bv1.IsNull())return false;
    if(BRep_Tool::Pnt(bv0).Distance(BRep_Tool::Pnt(reverse?av1:av0))>tolerance
        ||BRep_Tool::Pnt(bv1).Distance(BRep_Tool::Pnt(reverse?av0:av1))>tolerance)return false;
    if(!bv0.IsSame(reverse?av1:av0))changes->Replace(bv0,reverse?av1:av0);
    if(!bv1.IsSame(reverse?av0:av1))changes->Replace(bv1,reverse?av0:av1);
    changes->Replace(a,merged);changes->Replace(b,reverse?merged.Reversed():merged);changes->Remove(face);
    return true;
}

inline Preparation Prepare(const TopoDS_Shape& source,double tolerance=1e-6) {
    Preparation result;result.Shape=source;result.SourceValid=BRepCheck_Analyzer(source).IsValid();result.Valid=result.SourceValid;
    if(result.SourceValid)return result;
    // Copy topology only; every retained surface/3D curve remains the original
    // geometric object. No sewing, all-face fixing or tolerance growth occurs.
    const auto copy=BRepBuilderAPI_Copy(source,false,false).Shape();
    BRepCheck_Analyzer check(copy);TopTools_IndexedMapOfShape faces;TopExp::MapShapes(copy,TopAbs_FACE,faces);
    TopTools_IndexedDataMapOfShapeListOfShape solids,shells;
    TopExp::MapShapesAndAncestors(copy,TopAbs_FACE,TopAbs_SOLID,solids);
    TopExp::MapShapesAndAncestors(copy,TopAbs_FACE,TopAbs_SHELL,shells);
    Handle(BRepTools_ReShape) changes=new BRepTools_ReShape;
    std::vector<TopoDS_Shape> lowerDimensional;
    int collapsedFaces=0;
    for(int i=1;i<=faces.Extent();++i){
        const auto face=TopoDS::Face(faces(i));if(check.IsValid(face))continue;
        if(HasStatus(check,face,BRepCheck_InvalidImbricationOfWires)){
            const auto rotated=RotateWireStorage(face);
            if(BRepCheck_Analyzer(rotated).IsValid()){
                changes->Replace(face,rotated);
                result.Diagnostics.push_back("Topology preparation: face "+std::to_string(i)+" rotated wire storage only; geometry and edge occurrences unchanged");
                continue;
            }
        }
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        // Exact dimension collapse, never an area/size cutoff for small faces.
        if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||!(u0==u1||v0==v1))continue;
        const auto nSolids=solids.Contains(face)?solids.FindFromKey(face).Size():0;
        const auto nShells=shells.Contains(face)?shells.FindFromKey(face).Size():0;
        if(nSolids==0&&nShells==0){
            for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next())lowerDimensional.push_back(e.Current());
            changes->Remove(face);
            ++collapsedFaces;
            result.Diagnostics.push_back("Topology preparation: face "+std::to_string(i)+" has exactly collapsed UV domain; retained its original boundary as lower-dimensional geometry");
        }else if(nSolids==1||(!nSolids&&nShells==1)){
            const auto owner=nSolids?solids.FindFromKey(face).First():shells.FindFromKey(face).First();
            if(CollapseLineFace(face,owner,changes,tolerance)){
                ++collapsedFaces;result.Diagnostics.push_back("Topology preparation: face "+std::to_string(i)+" has exactly collapsed UV domain; replaced its two coincident line edges by one shared original line within its owner");
            }
        }
    }
    auto prepared=changes->Apply(copy);
    if(!lowerDimensional.empty()){
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);if(!prepared.IsNull())builder.Add(compound,prepared);
        for(const auto& edge:lowerDimensional)builder.Add(compound,edge);prepared=compound;
    }
    result.AttemptedShape=prepared;
    if(prepared.IsNull()||Count(prepared,TopAbs_SOLID)!=Count(source,TopAbs_SOLID)||!BRepCheck_Analyzer(prepared).IsValid()){
        result.Diagnostics.push_back("Topology preparation was not committed: complete result did not pass topology checks");return result;
    }
    if(Count(prepared,TopAbs_FACE)!=Count(source,TopAbs_FACE)-collapsedFaces
        ||MaximumTolerance(prepared,TopAbs_EDGE)>MaximumTolerance(source,TopAbs_EDGE)
        ||MaximumTolerance(prepared,TopAbs_VERTEX)>MaximumTolerance(source,TopAbs_VERTEX))return result;
    // Every positive-area face keeps its original supports/trims. Check the
    // complete area and bounds as independent guards before committing.
    GProp_GProps perimeter;BRepGProp::LinearProperties(source,perimeter);
    if(std::abs(Area(source)-Area(prepared))>tolerance*perimeter.Mass())return result;
    if(std::abs(Volume(source)-Volume(prepared))>tolerance*std::abs(Area(source)))return result;
    Bnd_Box before,after;BRepBndLib::AddOptimal(source,before,false,false);BRepBndLib::AddOptimal(prepared,after,false,false);
    double a[6],b[6];before.Get(a[0],a[1],a[2],a[3],a[4],a[5]);after.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
    for(int i=0;i<6;++i)if(std::abs(a[i]-b[i])>tolerance)return result;
    result.Shape=prepared;result.Valid=true;return result;
}
}
