#include "pch.h"
#include "SectionPlateEditingService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "Services/ServicesHelper.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAPI.hxx>
#include <GProp_GProps.hxx>
#include <cmath>

namespace iCAX::Geometry { namespace {
using namespace GeometryData;
constexpr double PlateEditTolerance=1e-7;
void EditRequire(bool ok,const char* reason){if(!ok)throw std::invalid_argument(reason);}
bool HasFace(const TopoDS_Shape& shape){return TopExp_Explorer(shape,TopAbs_FACE).More();}
bool HasSolid(const TopoDS_Shape& shape){return TopExp_Explorer(shape,TopAbs_SOLID).More();}
TopoDS_Shape EditDifference(const TopoDS_Shape& a,const TopoDS_Shape& b){BRepAlgoAPI_Cut op(a,b);EditRequire(op.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");return op.Shape();}
TopoDS_Shape EditCommon(const TopoDS_Shape& a,const TopoDS_Shape& b){BRepAlgoAPI_Common op(a,b);EditRequire(op.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");return op.Shape();}
Placement3 EditFrame(const std::array<double,16>& m){
    for(double x:m)EditRequire(std::isfinite(x),"PLATE_EDIT_FRAME_INVALID");
    const gp_Vec u(m[0],m[4],m[8]),v(m[1],m[5],m[9]),n(m[2],m[6],m[10]);
    EditRequire(std::abs(u.SquareMagnitude()-1)<1e-10&&std::abs(v.SquareMagnitude()-1)<1e-10&&std::abs(n.SquareMagnitude()-1)<1e-10&&std::abs(u.Dot(v))<1e-10&&u.Crossed(v).IsEqual(n,1e-10,1e-10)&&std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)<1e-10,"PLATE_EDIT_FRAME_INVALID");
    return {{m[3],m[7],m[11]},{m[0],m[4],m[8]},{m[1],m[5],m[9]},{m[2],m[6],m[10]}};
}
gp_Pln EditPlane(const Placement3& f){return gp_Pln(gp_Ax3(gp_Pnt(f.Location.X,f.Location.Y,f.Location.Z),gp_Dir(f.ZDirection.X,f.ZDirection.Y,f.ZDirection.Z),gp_Dir(f.XDirection.X,f.XDirection.Y,f.XDirection.Z)));}
TopoDS_Face BoundedEditFace(const TopoDS_Wire& wire,const gp_Pln& plane){
    EditRequire(BRep_Tool::IsClosed(wire),"PLATE_EDIT_LOOP_OPEN");BRepBuilderAPI_MakeFace face(plane,wire,true);
    EditRequire(face.IsDone()&&BRepCheck_Analyzer(face.Face()).IsValid(),"PLATE_EDIT_LOOP_INVALID");
    BRepCheck_Wire validation(wire);TopoDS_Edge a,b;EditRequire(validation.Closed()==BRepCheck_NoError&&validation.SelfIntersect(face.Face(),a,b)==BRepCheck_NoError,"PLATE_EDIT_LOOP_SELF_INTERSECTION");
    GProp_GProps properties;BRepGProp::SurfaceProperties(face.Face(),properties);EditRequire(properties.Mass()>1e-12,"PLATE_EDIT_LOOP_EMPTY");return face.Face();
}
TopoDS_Wire EditWire(const Loop2& loop,const gp_Pln& plane){
    EditRequire(!loop.Segments.empty()&&loop.Segments.size()<=8192,"PLATE_EDIT_LOOP_INVALID");BRepBuilderAPI_MakeWire wire;
    for(const auto& segment:loop.Segments){const auto& range=segment.Range;EditRequire(std::isfinite(range.First)&&std::isfinite(range.Last)&&range.Last>range.First,"PLATE_EDIT_CURVE_RANGE_INVALID");
        const auto curve=OpenCascade::BuildOpenCascadeCurve2(segment.Curve);EditRequire(!curve.IsNull(),"PLATE_EDIT_CURVE_UNSUPPORTED");
        const auto spatial=GeomAPI::To3d(curve,plane);EditRequire(!spatial.IsNull(),"PLATE_EDIT_CURVE_UNSUPPORTED");
        double first=range.First,last=range.Last;
        if(const auto* line=std::get_if<Segment2>(&segment.Curve)){EditRequire(first>=0&&last<=1,"PLATE_EDIT_CURVE_RANGE_INVALID");const double length=std::hypot(line->End.X-line->Start.X,line->End.Y-line->Start.Y);first*=length;last*=length;}
        BRepBuilderAPI_MakeEdge edge(spatial,first,last);EditRequire(edge.IsDone(),"PLATE_EDIT_CURVE_INVALID");auto value=edge.Edge();if(range.Reversed)value.Reverse();wire.Add(value);EditRequire(wire.IsDone(),"PLATE_EDIT_LOOP_DISCONNECTED");
    }return wire.Wire();
}
struct EditPlate {
    TopoDS_Shape Solid;TopoDS_Face Face;TopoDS_Wire Outer;std::vector<TopoDS_Wire> Holes;
    Placement3 Frame;gp_Pln Plane;
};
EditPlate ReadEditPlate(const BRepModel& geometry,const std::array<double,16>& matrix,double thickness){
    EditRequire(std::isfinite(thickness)&&thickness>0&&thickness<10000,"PLATE_EDIT_THICKNESS_INVALID");EditPlate result;result.Frame=EditFrame(matrix);result.Plane=EditPlane(result.Frame);
    const auto built=OpenCascade::BuildOpenCascadeShape(geometry);EditRequire(built.bOK&&!built.Shape.IsNull()&&BRepCheck_Analyzer(built.Shape).IsValid(),"PLATE_EDIT_BREP_INVALID");result.Solid=built.Shape;
    size_t bodies=0;for(TopExp_Explorer e(built.Shape,TopAbs_SOLID);e.More();e.Next())++bodies;EditRequire(bodies==1,"PLATE_EDIT_SINGLE_SOLID_REQUIRED");
    for(TopExp_Explorer e(built.Shape,TopAbs_FACE);e.More();e.Next()){const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane)continue;const auto plane=surface.Plane();
        if(!plane.Axis().Direction().IsParallel(result.Plane.Axis().Direction(),1e-10)||result.Plane.Distance(plane.Location())>PlateEditTolerance)continue;
        EditRequire(result.Face.IsNull(),"PLATE_EDIT_CAP_AMBIGUOUS");result.Outer=BRepTools::OuterWire(face);result.Face=BoundedEditFace(result.Outer,result.Plane);
        BRepBuilderAPI_MakeFace full(result.Plane,result.Outer,true);for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){const auto wire=TopoDS::Wire(w.Current());if(wire.IsSame(result.Outer))continue;const auto filled=BoundedEditFace(wire,result.Plane);const auto normalized=BRepTools::OuterWire(filled);result.Holes.push_back(normalized);full.Add(TopoDS::Wire(normalized.Reversed()));}EditRequire(full.IsDone(),"PLATE_EDIT_CAP_INVALID");result.Face=full.Face();
    }
    EditRequire(!result.Face.IsNull(),"PLATE_EDIT_CAP_MISSING");
    const auto normal=result.Plane.Axis().Direction();const auto replay=BRepPrimAPI_MakePrism(result.Face,gp_Vec(normal)*thickness).Shape();
    EditRequire(!HasSolid(EditDifference(built.Shape,replay))&&!HasSolid(EditDifference(replay,built.Shape)),"PLATE_EDIT_CONSTANT_THICKNESS_REQUIRED");return result;
}
Bnd_Box EditBounds(const TopoDS_Shape& shape){Bnd_Box result;BRepBndLib::AddOptimal(shape,result,false,false);return result;}
bool SameEditRegion(const TopoDS_Face& a,const TopoDS_Face& b){
    double ax0,ay0,az0,ax1,ay1,az1,bx0,by0,bz0,bx1,by1,bz1;EditBounds(a).Get(ax0,ay0,az0,ax1,ay1,az1);EditBounds(b).Get(bx0,by0,bz0,bx1,by1,bz1);
    if(std::max({std::abs(ax0-bx0),std::abs(ay0-by0),std::abs(az0-bz0),std::abs(ax1-bx1),std::abs(ay1-by1),std::abs(az1-bz1)})>PlateEditTolerance)return false;
    return !HasFace(EditDifference(a,b))&&!HasFace(EditDifference(b,a));
}
std::vector<std::array<double,2>> EditPreview(const TopoDS_Wire& wire,const Placement3& f){
    std::vector<std::array<double,2>> points;const gp_Pnt origin(f.Location.X,f.Location.Y,f.Location.Z);const gp_Vec u(f.XDirection.X,f.XDirection.Y,f.XDirection.Z),v(f.YDirection.X,f.YDirection.Y,f.YDirection.Z);
    for(BRepTools_WireExplorer e(wire);e.More();e.Next()){BRepAdaptor_Curve curve(e.Current());GCPnts_QuasiUniformDeflection display(curve,.05,curve.FirstParameter(),curve.LastParameter());EditRequire(display.IsDone(),"PLATE_EDIT_PREVIEW_FAILED");const int count=display.NbPoints();const bool reverse=e.Current().Orientation()==TopAbs_REVERSED;
        for(int i=1;i<count;++i){const auto p=display.Value(reverse?count+1-i:i);const gp_Vec d(origin,p);points.push_back({d.Dot(u),d.Dot(v)});}
    }return points;
}
Tube::CPlanarRegion2 EditProfile(const TopoDS_Face& face,const Placement3& frame){
    Tube::CPlanarRegion2 result;const auto outer=BRepTools::OuterWire(face);const auto plane=EditPlane(frame);size_t nextHole=0;
    const auto append=[&](const TopoDS_Wire& wire,bool inner){
        // MakeFace(Inside=true) establishes the finite material side using
        // exact topology. Read each coedge in wire order; no sampled signed
        // area decides manufacturing direction or curve connectivity.
        const auto bounded=BoundedEditFace(wire,plane);Tube::SRegionLoop2 loop;
        for(BRepTools_WireExplorer e(BRepTools::OuterWire(bounded));e.More();e.Next()){const auto exact=OpenCascade::ExtractPlanarTubeCurves(e.Current(),frame);EditRequire(exact.size()==1,"PLATE_EDIT_CURVE_INVALID");loop.Curves.push_back({{},exact.front()});}
        loop.ID=inner?"H"+std::to_string(nextHole++):"O0";
        if(inner){std::reverse(loop.Curves.begin(),loop.Curves.end());for(auto& curve:loop.Curves)curve.Segment.Range.Reversed=!curve.Segment.Range.Reversed;}
        for(size_t i=0;i<loop.Curves.size();++i)loop.Curves[i].ID=loop.ID+"/C"+std::to_string(i);result.Boundaries.push_back(std::move(loop));
    };
    append(outer,false);for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next()){const auto wire=TopoDS::Wire(e.Current());if(!wire.IsSame(outer))append(wire,true);}return result;
}
Loop2 EditPolygonLoop(const std::vector<std::array<double,2>>& points){
    EditRequire(points.size()>=3,"PLATE_EDIT_PROTECTION_INVALID");Loop2 result;
    for(size_t i=0;i<points.size();++i){const auto a=points[i],b=points[(i+1)%points.size()];const double length=std::hypot(b[0]-a[0],b[1]-a[1]);EditRequire(length>1e-12,"PLATE_EDIT_PROTECTION_INVALID");result.Segments.push_back({Segment2{{a[0],a[1]},{b[0],b[1]}},{0,1,false}});}return result;
}
Loop2 EditExactLoop(const TopoDS_Wire& wire,const Placement3& frame){const auto bounded=BoundedEditFace(wire,EditPlane(frame));Loop2 result;for(BRepTools_WireExplorer e(BRepTools::OuterWire(bounded));e.More();e.Next()){const auto exact=OpenCascade::ExtractPlanarTubeCurves(e.Current(),frame);EditRequire(exact.size()==1,"PLATE_EDIT_PROTECTION_INVALID");result.Segments.push_back(exact.front());}return result;}
class SectionPlateEditingService final:public ISectionPlateEditingService {
    AUTO_REGIST_SERVICE(ISectionPlateEditingService,SectionPlateEditingService)
public:
    void OnLoad()override{}void OnUnload()override{}
    Tube::CPlanarRegion2 ReadProfile(const BRepModel& geometry,const std::array<double,16>& frame,double thickness)const override{
        const auto original=ReadEditPlate(geometry,frame,thickness);return EditProfile(original.Face,original.Frame);
    }
    std::vector<Loop2> ReadProtection(const SectionPlate& current,const std::vector<SectionPlate>& assembly,double band,bool entireCrossingPosts)const override{
        EditRequire(std::isfinite(band)&&band>=0&&band<10000,"PLATE_EDIT_PROTECTION_INVALID");band=std::max(band,PlateEditTolerance*100);const auto frame=EditFrame(current.Frame);const auto plane=EditPlane(frame);std::vector<Loop2> result;
        const auto expand=[&](const std::vector<std::array<double,2>>& boundary){const auto loop=EditPolygonLoop(boundary);const auto face=BoundedEditFace(EditWire(loop,plane),plane);BRepOffsetAPI_MakeOffset offset(face,GeomAbs_Intersection);offset.Perform(band);EditRequire(offset.IsDone(),"PLATE_EDIT_PROTECTION_OFFSET_FAILED");for(TopExp_Explorer e(offset.Shape(),TopAbs_WIRE);e.More();e.Next())result.push_back(EditExactLoop(TopoDS::Wire(e.Current()),frame));};
        double bottom=1e100,top=-1e100;for(auto p:current.Outline){bottom=std::min(bottom,p[1]);top=std::max(top,p[1]);}
        for(const auto& joint:current.Joints){auto boundary=joint.Boundary;if(entireCrossingPosts&&(joint.Type=="LapTop"||joint.Type=="LapBottom")){double first=1e100,last=-1e100;for(auto p:boundary){first=std::min(first,p[0]);last=std::max(last,p[0]);}boundary={{first,bottom},{last,bottom},{last,top},{first,top}};}expand(boundary);}
        if(!current.Base){result.push_back(EditPolygonLoop({{current.Span[0],0},{current.Span[1],0},{current.Span[1],band},{current.Span[0],band}}));}
        else for(const auto& support:assembly)if(!support.Base){
            std::vector<std::array<double,2>> footprint;const auto& a=support.Frame;const auto& b=current.Frame;
            for(auto p:std::array<std::array<double,2>,4>{{{support.Span[0],0},{support.Span[1],0},{support.Span[1],support.Thickness},{support.Span[0],support.Thickness}}}){
                const std::array<double,3> world{a[0]*p[0]+a[2]*p[1]+a[3]-b[3],a[4]*p[0]+a[6]*p[1]+a[7]-b[7],a[8]*p[0]+a[10]*p[1]+a[11]-b[11]};
                footprint.push_back({world[0]*b[0]+world[1]*b[4]+world[2]*b[8],world[0]*b[1]+world[1]*b[5]+world[2]*b[9]});
            }expand(footprint);
        }return result;
    }
    SectionPlateHoleEditResult EditHoles(const SectionPlateHoleEditInput& input,const std::function<bool()>& cancelled)const override{
        const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("PLATE_EDIT_CANCELLED");};check();
        EditRequire(std::isfinite(input.MinimumWeb)&&input.MinimumWeb>=0&&input.MinimumWeb<10000,"PLATE_EDIT_MINIMUM_WEB_INVALID");EditRequire(input.Holes.size()<=8192&&input.ProtectedMaterial.size()<=8192&&input.ProtectedVoids.size()<=8192,"PLATE_EDIT_LOOP_LIMIT");
        const auto original=ReadEditPlate(input.OriginalGeometry,input.Frame,input.Thickness);const auto exterior=BoundedEditFace(original.Outer,original.Plane);
        std::vector<TopoDS_Face> holes,oldHoles;std::vector<TopoDS_Wire> wires;std::vector<bool> changed,crossing;bool normalize=false;
        for(const auto& wire:original.Holes)oldHoles.push_back(BoundedEditFace(wire,original.Plane));
        for(const auto& loop:input.Holes){check();const auto face=BoundedEditFace(EditWire(loop,original.Plane),original.Plane);EditRequire(HasFace(EditCommon(face,exterior)),"PLATE_EDIT_HOLE_OUTSIDE");
            bool same=false;for(const auto& old:oldHoles)if(SameEditRegion(face,old)){same=true;break;}
            BRepExtrema_DistShapeShape edgeDistance(BRepTools::OuterWire(face),original.Outer);EditRequire(edgeDistance.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");
            const bool cross=HasFace(EditDifference(face,exterior))||edgeDistance.Value()<=PlateEditTolerance;
            holes.push_back(face);wires.push_back(BRepTools::OuterWire(face));changed.push_back(!same);crossing.push_back(cross);normalize|=cross;
        }
        const auto minimum=[&](double actual){if(input.MinimumWeb>0&&actual<input.MinimumWeb-PlateEditTolerance)throw std::invalid_argument("PLATE_EDIT_MINIMUM_WEB: "+std::to_string(actual)+" / "+std::to_string(input.MinimumWeb)+" mm");};
        for(size_t i=0;i<holes.size();++i){check();if(changed[i]&&!crossing[i]){BRepExtrema_DistShapeShape distance(wires[i],original.Outer);EditRequire(distance.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");minimum(distance.Value());}
            for(size_t j=0;j<i;++j){const bool overlap=!EditBounds(holes[i]).IsOut(EditBounds(holes[j]))&&HasFace(EditCommon(holes[i],holes[j]));BRepExtrema_DistShapeShape distance(wires[i],wires[j]);EditRequire(distance.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");
                if(overlap||distance.Value()<=PlateEditTolerance){normalize=true;continue;}if(changed[i]||changed[j])minimum(distance.Value());}
        }
        TopoDS_Face material;
        if(normalize){TopoDS_Shape cut=exterior;for(const auto& hole:holes){check();cut=EditDifference(cut,hole);}size_t count=0;for(TopExp_Explorer e(cut,TopAbs_FACE);e.More();e.Next()){++count;material=TopoDS::Face(e.Current());}EditRequire(count==1,"PLATE_EDIT_MATERIAL_DISCONNECTED");}
        else{BRepBuilderAPI_MakeFace face(original.Plane,original.Outer,true);for(const auto& wire:wires)face.Add(TopoDS::Wire(wire.Reversed()));EditRequire(face.IsDone(),"PLATE_EDIT_MATERIAL_DISCONNECTED");material=face.Face();}
        EditRequire(BRepCheck_Analyzer(material).IsValid(),"PLATE_EDIT_MATERIAL_DISCONNECTED");
        for(const auto& loop:input.ProtectedMaterial){check();const auto protectedFace=BoundedEditFace(EditWire(loop,original.Plane),original.Plane);const auto retained=EditCommon(protectedFace,original.Face);EditRequire(HasFace(retained)&&!HasFace(EditDifference(retained,material)),"PLATE_EDIT_PROTECTED_MATERIAL");}
        for(const auto& loop:input.ProtectedVoids){check();const auto protectedFace=BoundedEditFace(EditWire(loop,original.Plane),original.Plane);EditRequire(!HasFace(EditCommon(protectedFace,material)),"PLATE_EDIT_PROTECTED_VOID");}
        check();const auto solid=BRepPrimAPI_MakePrism(material,gp_Vec(original.Plane.Axis().Direction())*input.Thickness).Shape();EditRequire(BRepCheck_Analyzer(solid).IsValid(),"PLATE_EDIT_SOLID_INVALID");size_t bodies=0;for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++bodies;EditRequire(bodies==1,"PLATE_EDIT_MATERIAL_DISCONNECTED");
        SectionPlateHoleEditResult result;const auto outer=BRepTools::OuterWire(material);result.OutlineChanged=!SameEditRegion(BoundedEditFace(outer,original.Plane),exterior);result.Profile=EditProfile(material,original.Frame);result.Geometry=OpenCascade::ConvertOpenCascadeShapeToBRep(solid,"Edited section plate","generated",.05);
        result.PreviewOutline=EditPreview(outer,original.Frame);
        for(TopExp_Explorer e(material,TopAbs_WIRE);e.More();e.Next()){const auto wire=TopoDS::Wire(e.Current());if(wire.IsSame(outer))continue;const auto region=BoundedEditFace(wire,original.Plane);std::vector<size_t> contributors;size_t equal=holes.size();
            for(size_t i=0;i<holes.size();++i)if(HasFace(EditCommon(region,holes[i]))){contributors.push_back(i);if(SameEditRegion(region,holes[i]))equal=i;}
            EditRequire(!contributors.empty(),"PLATE_EDIT_PROFILE_MAPPING_INVALID");if(result.OutlineChanged){BRepExtrema_DistShapeShape distance(wire,outer);EditRequire(distance.IsDone(),"PLATE_EDIT_BOOLEAN_FAILED");if(equal==holes.size()||changed[equal])minimum(distance.Value());}
            result.HoleInputIndices.push_back(std::move(contributors));result.EqualInput.push_back(equal);result.PreviewHoles.push_back(EditPreview(wire,original.Frame));
        }return result;
    }
};
}}
