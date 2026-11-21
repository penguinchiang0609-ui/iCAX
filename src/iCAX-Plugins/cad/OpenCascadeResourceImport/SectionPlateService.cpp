#include "pch.h"
#include "SectionPlateService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "Services/ServicesHelper.h"
#include "OpenCascadeTaskExecution.h"
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepFilletAPI_MakeFillet2d.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <gp_Pln.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRep_Tool.hxx>
#include <Contap_Contour.hxx>
#include <BRepTopAdaptor_Tool.hxx>
#include <BRepTopAdaptor_TopolTool.hxx>
#include <Approx_CurveOnSurface.hxx>
#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GeomAPI_ExtremaCurveSurface.hxx>
#include <GeomAPI_IntCS.hxx>
#include <GeomAPI.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <BOPAlgo_Builder.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomProjLib.hxx>
#include <set>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BOPTools_AlgoTools3D.hxx>
#include <IntTools_Context.hxx>
#include <Geom2dHatch_Hatcher.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BezierSurface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopoDS_Solid.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <exception>
#include <utility>
#include <boost/multiprecision/cpp_int.hpp>
#include "SectionPlateFacePartition.inc"

namespace {
using namespace iCAX::Geometry;
namespace GeometryData=iCAX::GeometryData;
using P2=std::array<double,2>;
size_t PlateConcurrency(size_t count,size_t requested){
    if(!count)return 0;
    const auto workers=iCAX::Tasks::CurrentScheduler()==iCAX::OpenCascade::detail::GeometryTaskScheduler()?size_t{1}:iCAX::OpenCascade::detail::GeometryConcurrency(requested?requested:size_t{4});
    return std::min(count,workers);
}
template<class Work> size_t ForPlateIndices(size_t count,size_t requested,Work&& work){
    const auto workers=PlateConcurrency(count,requested);if(!workers)return 0;
    std::atomic_size_t next{0};std::vector<std::exception_ptr> failures(count);
    iCAX::OpenCascade::detail::ForGeometryIndices(workers,workers,[&](size_t worker){
        for(auto i=next.fetch_add(1);i<count;i=next.fetch_add(1))try{work(i,worker);}catch(...){failures[i]=std::current_exception();}
    });
    // Preserve serial error precedence and wait for every reader before its
    // captured geometry leaves scope, including failure and cancellation.
    for(const auto& failure:failures)if(failure)std::rethrow_exception(failure);
    return workers;
}
#include "SectionPlateDirection.inc"
// Keep the original surface and outer boundary, including its pcurves and
// location. Inner trimming wires describe holes, not permission for a support
// to rise through the surface. This is only a construction ceiling: actual
// bearing contacts must still lie on the original, perforated workpiece.
TopoDS_Face SupportSurface(const TopoDS_Face& face) {
    size_t wires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())++wires;
    if(wires==1)return face;
    auto local=face;const auto location=face.Location();local.Location(TopLoc_Location());
    const auto outer=BRepTools::OuterWire(local);
    if(outer.IsNull())throw std::runtime_error("PLATE_SURFACE_BOUNDARY_MISSING");
    auto ceiling=TopoDS::Face(local.EmptyCopied());BRep_Builder builder;builder.Add(ceiling,outer);
    ceiling.Location(location);return ceiling;
}
GeometryData::Tube::CPlanarRegion2 ConstructedProfile(const TopoDS_Face& cap,const std::array<double,16>& m){
    const GeometryData::Placement3 frame{{m[3],m[7],m[11]},{m[0],m[4],m[8]},{m[1],m[5],m[9]},{m[2],m[6],m[10]}};
    const gp_Pln plane(gp_Ax3(gp_Pnt(m[3],m[7],m[11]),gp_Dir(m[2],m[6],m[10]),gp_Dir(m[0],m[4],m[8])));
    GeometryData::Tube::CPlanarRegion2 result;const auto outer=BRepTools::OuterWire(cap);size_t nextHole=0;
    const auto append=[&](const TopoDS_Wire& wire,bool inner){
        BRepBuilderAPI_MakeFace bounded(plane,wire,true);if(!bounded.IsDone())throw std::runtime_error("PLATE_FACE_INVALID");
        GeometryData::Tube::SRegionLoop2 loop;loop.ID=inner?"H"+std::to_string(nextHole++):"O0";
        for(BRepTools_WireExplorer e(BRepTools::OuterWire(bounded.Face()));e.More();e.Next()){
            const auto exact=iCAX::OpenCascade::ExtractPlanarTubeCurves(e.Current(),frame);
            if(exact.size()!=1)throw std::runtime_error("PLATE_PROFILE_CURVE_INVALID");
            loop.Curves.push_back({{},exact.front()});
        }
        if(inner){std::reverse(loop.Curves.begin(),loop.Curves.end());for(auto& curve:loop.Curves)curve.Segment.Range.Reversed=!curve.Segment.Range.Reversed;}
        for(size_t i=0;i<loop.Curves.size();++i)loop.Curves[i].ID=loop.ID+"/C"+std::to_string(i);
        result.Boundaries.push_back(std::move(loop));
    };
    append(outer,false);for(TopExp_Explorer e(cap,TopAbs_WIRE);e.More();e.Next()){const auto wire=TopoDS::Wire(e.Current());if(!wire.IsSame(outer))append(wire,true);}
    return result;
}
double Cross(P2 a,P2 b,P2 c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
std::vector<P2> JointRect(double x0,double y0,double x1,double y1){return {{x0,y0},{x1,y0},{x1,y1},{x0,y1}};}
#include "SectionPlateValidation.inc"
#include "SectionPlateOpenings.inc"
#include "SectionPlateSkeleton.inc"
std::vector<P2> ContactHull(std::vector<P2> points){
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());if(points.size()<3)return {};
    std::vector<P2> hull;for(auto p:points){while(hull.size()>1&&Cross(hull[hull.size()-2],hull.back(),p)<=1e-8)hull.pop_back();hull.push_back(p);}const auto lower=hull.size();
    for(auto i=points.rbegin()+1;i!=points.rend();++i){while(hull.size()>lower&&Cross(hull[hull.size()-2],hull.back(),*i)<=1e-8)hull.pop_back();hull.push_back(*i);}hull.pop_back();return hull;
}
#include "SectionPlateExact.inc"
#include "SectionPlateLowerEnvelope.inc"
#include "SectionPlateExactFabrication.inc"
#include "SectionPlateExactBuild.inc"
class SectionPlateService final:public ISectionPlateService {
    AUTO_REGIST_SERVICE(ISectionPlateService,SectionPlateService)
public:
    void OnLoad()override{}void OnUnload()override{}
    std::vector<std::uint64_t> FindLowerEnvelopeFaces(const SectionPlateInput& in,const std::function<bool()>& cancelled)const override {
        return LowerEnvelopeFaces(PlateDirectionFrame(in.SupportDirection).Align(in),cancelled);
    }
    std::vector<SupportStation> SuggestStations(const SectionPlateInput& original,double spacing)const override {
        const auto in=PlateDirectionFrame(original.SupportDirection).Align(original);
        if(!std::isfinite(spacing)||spacing<=in.Thickness+1e-3)throw std::invalid_argument("PLATE_SPACING_INVALID");
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);if(!built.bOK)throw std::runtime_error("PLATE_BREP_INVALID");
        const auto& m=in.Transform;gp_Trsf tr;tr.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);Bnd_Box box;
        for(auto id:in.FaceIDs){auto f=built.Faces.find(id);if(f==built.Faces.end())throw std::invalid_argument("PLATE_FACE_MISSING");BRepBndLib::AddOptimal(f->second.Moved(TopLoc_Location(tr)),box,false,false);}
        if(box.IsVoid())throw std::invalid_argument("PLATE_INPUT_REQUIRED");double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        const double cx=in.AreaWidth>0?in.AreaX:(x0+x1)/2,cy=in.AreaDepth>0?in.AreaY:(y0+y1)/2;
        const double width=in.AreaWidth>0?in.AreaWidth:(in.BaseShape=="Circle"?std::max(x1-x0,y1-y0):x1-x0)+2*in.Margin,depth=in.BaseShape=="Circle"?width:in.AreaDepth>0?in.AreaDepth:y1-y0+2*in.Margin;
        x0=std::max(x0,cx-width/2+in.Margin);x1=std::min(x1,cx+width/2-in.Margin);y0=std::max(y0,cy-depth/2+in.Margin);y1=std::min(y1,cy+depth/2-in.Margin);
        const auto positions=[&](double first,double last){const double inset=std::max(in.Thickness,(last-first)*.12),span=last-first-2*inset;
            if(span<2*(in.Thickness+.01))throw std::invalid_argument("PLATE_SUPPORT_AREA_TOO_NARROW");int count=std::max(3,static_cast<int>(std::ceil(span/spacing))+1);if(count>32)throw std::invalid_argument("PLATE_STATION_LIMIT");std::vector<double> values;for(int i=0;i<count;++i)values.push_back(first+inset+span*i/(count-1));return values;};
        std::vector<SupportStation> stations;
        const auto append=[&](double coordinate,double from,double to,bool alongX,bool connection=false){
            if(in.BaseShape=="Circle"){const double radius=width/2-in.Margin,delta=std::abs(coordinate-(alongX?cx:cy))+in.Thickness/2;if(delta>=radius)return;const double reach=std::sqrt(radius*radius-delta*delta),center=alongX?cy:cx;from=std::max(from,center-reach);to=std::min(to,center+reach);}
            if(to-from>=in.Thickness)stations.push_back({coordinate,from,to,alongX,connection});};
        if(in.Pattern=="Skeleton"){
            const bool alongX=x1-x0>=y1-y0;
            if(in.CountLayout){
                const auto countFor=[&](size_t requested,double first,double last,double reference){
                    if(requested==1||requested>32)throw std::invalid_argument("PLATE_COUNT_INVALID");
                    const double inset=std::min((last-first)/4,std::max(2*in.Thickness,std::min((last-first)*.08,reference/4)));
                    return requested?requested:std::max(size_t(2),static_cast<size_t>(std::ceil((last-first-2*inset)/reference))+1);
                };
                const auto supports=countFor(in.SupportCount,alongX?x0:y0,alongX?x1:y1,220.);
                const auto connections=countFor(in.ConnectionCount,alongX?y0:x0,alongX?y1:x1,440.);
                if(supports>32||connections>32||supports+connections>40)throw std::invalid_argument("PLATE_COUNT_INVALID");
                const auto addGroups=[&](size_t count,double first,double last,double from,double to,bool axis,bool connection,const std::vector<double>& explicitPositions,double reference){
                    if(!explicitPositions.empty()&&explicitPositions.size()!=count)throw std::invalid_argument("PLATE_POSITION_COUNT_MISMATCH");
                    const double inset=std::min((last-first)/4,std::max(2*in.Thickness,std::min((last-first)*.08,reference/4)));
                    const double span=last-first-2*inset,step=span/(count-1);
                    if(step<=in.Thickness+.001)throw std::invalid_argument("PLATE_SUPPORT_AREA_TOO_NARROW");
                    double previous=-1e100;
                    for(size_t i=0;i<count;++i){
                        const double nominal=explicitPositions.empty()?first+inset+step*i:explicitPositions[i];
                        if(!std::isfinite(nominal)||nominal<first+in.Thickness/2||nominal>last-in.Thickness/2||nominal-previous<=in.Thickness+.001)throw std::invalid_argument("PLATE_POSITION_INVALID");
                        previous=nominal;
                        const auto before=stations.size();append(nominal,from,to,axis,connection);
                        if(stations.size()!=before){stations.back().LayoutIndex=i;stations.back().NominalCoordinate=nominal;}
                    }
                };
                addGroups(supports,alongX?x0:y0,alongX?x1:y1,alongX?y0:x0,alongX?y1:x1,alongX,false,in.SupportPositions,220.);
                addGroups(connections,alongX?y0:x0,alongX?y1:x1,alongX?x0:y0,alongX?x1:y1,!alongX,true,in.ConnectionPositions,440.);
                return stations;
            }
            const auto candidates=[&](double first,double last,double maximum,size_t minimum){
                const double inset=std::min((last-first)/4,std::max(2*in.Thickness,std::min((last-first)*.08,maximum/4)));
                const double span=last-first-2*inset;if(span<2*in.Thickness)throw std::invalid_argument("PLATE_SUPPORT_AREA_TOO_NARROW");
                const auto count=std::max(minimum,static_cast<size_t>(std::ceil(span/(maximum/2)))+1);
                if(count>96)throw std::invalid_argument("PLATE_STATION_LIMIT");
                std::vector<double> values;for(size_t i=0;i<count;++i)values.push_back(first+inset+span*i/(count-1));return values;
            };
            const auto nominal=candidates(alongX?x0:y0,alongX?x1:y1,spacing,2);
            std::vector<double> bearings=nominal;
            // Near alternatives avoid small bores without adding built plates
            // or making a long slot choose only one side of a bearing station.
            if(nominal.size()<=32){const auto offset=std::min(spacing/16,(nominal.back()-nominal.front())/8);
                for(auto coordinate:nominal)for(double shift:{-offset,offset})
                    if(coordinate+shift>(alongX?x0:y0)+in.Thickness&&coordinate+shift<(alongX?x1:y1)-in.Thickness)bearings.push_back(coordinate+shift);
            }
            std::sort(bearings.begin(),bearings.end());bearings.erase(std::unique(bearings.begin(),bearings.end()),bearings.end());
            for(auto coordinate:bearings)append(coordinate,alongX?y0:x0,alongX?y1:x1,alongX);
            for(auto coordinate:candidates(alongX?y0:x0,alongX?y1:x1,2*spacing,5))append(coordinate,alongX?x0:y0,alongX?x1:y1,!alongX,true);
            if(stations.size()>128)throw std::invalid_argument("PLATE_STATION_LIMIT");return stations;
        }
        const auto xs=positions(x0,x1),ys=positions(y0,y1);
        if(in.Pattern=="Staggered"){
            if(in.PostWidth<in.Thickness||in.PostWidth>=spacing)throw std::invalid_argument("PLATE_POST_WIDTH_INVALID");
            for(size_t row=0;row<ys.size();++row)for(auto x:xs){double coordinate=x+(row%2?spacing/2:0);if(coordinate>x1-in.Thickness)continue;append(coordinate,ys[row]-in.PostWidth/2,ys[row]+in.PostWidth/2,true);}
        }else if(in.Pattern=="Grid"){
            for(auto x:xs)append(x,y0,y1,true);
            for(auto y:ys){if(in.JointMode=="TabSlot")append(y,x0,x1,false);else{double from=x0;for(auto x:xs){append(y,from,x-in.Thickness/2,false);from=x+in.Thickness/2;}append(y,from,x1,false);}}
        }else if(in.Pattern=="Parallel"){if(in.AlongX)for(auto x:xs)append(x,y0,y1,true);else for(auto y:ys)append(y,x0,x1,false);}
        else throw std::invalid_argument("PLATE_PATTERN_INVALID");
        if(stations.empty()||stations.size()>128)throw std::invalid_argument("PLATE_STATION_LIMIT");return stations;
    }
    std::vector<SectionPlate> Build(const SectionPlateInput& input,const std::function<bool()>& cancelled)const override {try{return BuildExactPlates(input,cancelled);}catch(const Standard_Failure& failure){if(std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<"fixture OCCT "<<typeid(failure).name()<<": "<<failure.GetMessageString()<<std::endl;throw;}}
};
}
