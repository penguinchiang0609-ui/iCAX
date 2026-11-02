#include "pch.h"
#include "SectionPlateService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "FixtureMeshGeometry.h"
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
#include <set>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
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
#include <vtkClipPolyData.h>
#include <vtkPlane.h>
#include <vtkStaticCellLocator.h>
#include <vtkIdList.h>
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <iostream>
#include <chrono>
#include <mutex>
#include <exception>
#include <utility>

namespace {
using namespace iCAX::Geometry;
namespace GeometryData=iCAX::GeometryData;
using P2=std::array<double,2>;
using namespace Clipper2Lib;
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
size_t PlateMeshConcurrency(std::initializer_list<vtkPolyData*> sources,size_t count,size_t requested){
    size_t bytes=0;for(auto source:sources)if(source)bytes+=static_cast<size_t>(source->GetActualMemorySize())*1024;
    return std::max(size_t{1},std::min(PlateConcurrency(count,requested),std::max(size_t{1},size_t{256*1024*1024}/std::max(bytes,size_t{1}))));
}
struct PlateSlabSource {
    const iCAX::FixtureMesh::Mesh& Mesh;
    vtkSmartPointer<vtkStaticCellLocator> Locator=vtkSmartPointer<vtkStaticCellLocator>::New();
    std::array<double,6> Bounds{};
    PlateSlabSource(const iCAX::FixtureMesh::Mesh& mesh,vtkPolyData* data):Mesh(mesh){data->BuildCells();data->ComputeBounds();data->GetBounds(Bounds.data());Locator->SetDataSet(data);Locator->BuildLocator();}
    vtkSmartPointer<vtkPolyData> Region(bool alongX,double station,double thickness,std::vector<vtkIdType>& pointMap,const std::function<void()>& check)const{
        auto bounds=Bounds;const int axis=alongX?0:2;const double padding=std::max(1e-7,64*std::numeric_limits<double>::epsilon()*(std::abs(station)+thickness+1));
        bounds[axis]=station-thickness/2-padding;bounds[axis+1]=station+thickness/2+padding;
        auto candidates=vtkSmartPointer<vtkIdList>::New();Locator->FindCellsWithinBounds(bounds.data(),candidates);
        const auto count=candidates->GetNumberOfIds();if(count>1)std::sort(candidates->GetPointer(0),candidates->GetPointer(0)+count);
        auto data=vtkSmartPointer<vtkPolyData>::New();auto points=vtkSmartPointer<vtkPoints>::New();points->SetDataTypeToDouble();auto cells=vtkSmartPointer<vtkCellArray>::New();
        if(pointMap.size()<Mesh.Points.size())pointMap.resize(Mesh.Points.size(),-1);
        auto touched=vtkSmartPointer<vtkIdList>::New();
        struct Reset {std::vector<vtkIdType>& Map;vtkIdList* Touched;~Reset(){auto* map=Map.data();for(vtkIdType i=0;i<Touched->GetNumberOfIds();++i)map[Touched->GetId(i)]=-1;}} reset{pointMap,touched};
        auto* mapping=pointMap.data();const auto* triangles=Mesh.Triangles.data();const auto* vertices=Mesh.Points.data();
        for(vtkIdType i=0;i<count;++i){if(i%512==0)check();const auto& triangle=triangles[candidates->GetId(i)];vtkIdType ids[3];
            for(int k=0;k<3;++k){const auto original=triangle[k];auto& mapped=mapping[original];if(mapped<0){const auto& p=vertices[original];const auto id=points->InsertNextPoint(p.x(),p.y(),p.z());touched->InsertNextId(original);mapped=id;}ids[k]=mapped;}
            cells->InsertNextCell(3,ids);
        }
        data->SetPoints(points);data->SetPolys(cells);return data;
    }
};
// Mesh only the CAD strips that can meet a requested plate. The strip caps
// stay farther away than the accepted contact band. The base uses the exact
// support-direction bound and mass properties use the original CAD surfaces.
iCAX::FixtureMesh::Mesh PlateBandMesh(const TopoDS_Shape& shape,const SectionPlateInput& in,
    double x0,double y0,double z0,double x1,double y1,double z1,double deflection,bool bearingsOnly,const std::function<void()>& check,
    const std::vector<std::pair<TopoDS_Face,bool>>* sourceFaces=nullptr,iCAX::FixtureMesh::Mesh* selectedMesh=nullptr,iCAX::FixtureMesh::Mesh* otherMesh=nullptr){
    iCAX::FixtureMesh::Mesh merged;
    const auto append=[](iCAX::FixtureMesh::Mesh& target,const iCAX::FixtureMesh::Mesh& source){
        const auto offset=target.Points.size();iCAX::FixtureMesh::CheckMeshBudget(offset+source.Points.size(),target.Triangles.size()+source.Triangles.size());
        target.Points.insert(target.Points.end(),source.Points.begin(),source.Points.end());
        for(const auto& t:source.Triangles)target.Triangles.emplace_back(offset+t[0],offset+t[1],offset+t[2]);target.Allowance=std::max(target.Allowance,source.Allowance);
    };
    std::set<std::pair<bool,double>> visited;
    const double padding=std::max(in.Gap,4*deflection+.002),extent=1.;
    for(const auto& station:in.Stations){
        if(bearingsOnly&&station.Connection)continue;
        if(!visited.emplace(station.AlongX,station.Coordinate).second)continue;check();
        const double half=in.Thickness/2+padding,c=station.Coordinate;
        const auto box=BRepPrimAPI_MakeBox(gp_Pnt(station.AlongX?c-half:x0-extent,station.AlongX?y0-extent:c-half,z0-extent),
            station.AlongX?2*half:x1-x0+2*extent,station.AlongX?y1-y0+2*extent:2*half,z1-z0+2*extent).Shape();
        BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(shape);tools.Append(box);common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
        if(!common.IsDone())throw std::runtime_error("PLATE_BREP_CLIP_FAILED");const auto clipped=common.Shape();
        if(!TopExp_Explorer(clipped,TopAbs_FACE).More())continue;
        auto mesh=iCAX::FixtureMesh::Tessellate(clipped,deflection,[&]{check();return false;},false);append(merged,mesh);
        if(sourceFaces&&!station.Connection){
            TopTools_IndexedMapOfShape actual,added;TopExp::MapShapes(clipped,TopAbs_FACE,actual);
            BRep_Builder builder;TopoDS_Compound selected,other;builder.MakeCompound(selected);builder.MakeCompound(other);bool hasSelected=false,hasOther=false;
            for(const auto& [source,isSelected]:*sourceFaces){
                const auto add=[&](const TopoDS_Shape& face){if(face.ShapeType()!=TopAbs_FACE||!actual.Contains(face)||added.Contains(face))return;added.Add(face);builder.Add(isSelected?selected:other,face);(isSelected?hasSelected:hasOther)=true;};
                const auto& modified=common.Modified(source);for(const auto& face:modified)add(face);if(modified.IsEmpty())add(source);
            }
            if(hasSelected)append(*selectedMesh,iCAX::FixtureMesh::Collect(selected,deflection,[&]{check();return false;}));
            if(hasOther)append(*otherMesh,iCAX::FixtureMesh::Collect(other,deflection,[&]{check();return false;}));
        }
    }
    if(!merged.Triangles.empty())merged.Validate();return merged;
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
TopoDS_Shape Solid(const std::vector<P2>& loop,const std::array<double,16>& m,double thickness,const std::vector<std::vector<P2>>& holes={},const std::vector<SectionPlateJoint>* joints=nullptr,double radius=0,std::vector<P2>* roundedOutline=nullptr,GeometryData::Tube::CPlanarRegion2* profile=nullptr) {
    const auto wire=[&](const std::vector<P2>& points){BRepBuilderAPI_MakePolygon poly;for(auto p:points)poly.Add(gp_Pnt(m[0]*p[0]+m[1]*p[1]+m[3],m[4]*p[0]+m[5]*p[1]+m[7],m[8]*p[0]+m[9]*p[1]+m[11]));poly.Close();if(!poly.IsDone())throw std::runtime_error("PLATE_OUTLINE_INVALID");return poly.Wire();};
    BRepBuilderAPI_MakeFace face(wire(loop));if(!face.IsDone())throw std::runtime_error("PLATE_FACE_INVALID");
    auto cap=face.Face();
    if(joints&&radius>0){
        BRepFilletAPI_MakeFillet2d rounding(cap);TopTools_IndexedMapOfShape vertices;TopExp::MapShapes(cap,TopAbs_VERTEX,vertices);std::set<int> rounded;
        for(const auto& joint:*joints)if(joint.Type=="BaseTenon"){
            double left=INFINITY,right=-INFINITY,bottom=INFINITY;for(auto p:joint.Boundary){left=std::min(left,p[0]);right=std::max(right,p[0]);bottom=std::min(bottom,p[1]);}
            for(double x:{left,right}){
                const gp_Pnt target(m[0]*x+m[1]*bottom+m[3],m[4]*x+m[5]*bottom+m[7],m[8]*x+m[9]*bottom+m[11]);int found=0;
                for(int i=1;i<=vertices.Extent();++i)if(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))).Distance(target)<2e-6){found=i;break;}
                if(!found)throw std::runtime_error("PLATE_TENON_RADIUS_FAILED");
                if(rounded.insert(found).second){rounding.AddFillet(TopoDS::Vertex(vertices(found)),radius);if(rounding.Status()!=ChFi2d_IsDone)throw std::runtime_error("PLATE_TENON_RADIUS_FAILED");}
            }
        }
        if(!rounded.empty()){
            rounding.Build();if(!rounding.IsDone())throw std::runtime_error("PLATE_TENON_RADIUS_FAILED");cap=TopoDS::Face(rounding.Shape());
            if(roundedOutline){std::vector<P2> points;
                for(BRepTools_WireExplorer e(BRepTools::OuterWire(cap));e.More();e.Next()){
                    const auto edge=e.Current();BRepAdaptor_Curve curve(edge);GCPnts_QuasiUniformDeflection samples(curve,.005,curve.FirstParameter(),curve.LastParameter());
                    if(!samples.IsDone())throw std::runtime_error("PLATE_TENON_RADIUS_FAILED");
                    const bool reverse=edge.Orientation()==TopAbs_REVERSED;
                    for(int i=1;i<samples.NbPoints();++i){const auto p=samples.Value(reverse?samples.NbPoints()-i+1:i);const double x=p.X()-m[3],y=p.Y()-m[7],z=p.Z()-m[11];points.push_back({x*m[0]+y*m[4]+z*m[8],x*m[1]+y*m[5]+z*m[9]});}
                }*roundedOutline=std::move(points);
            }
        }
    }
    // Round the outer insertion tips before introducing interior wires.
    // Fillet2d can disconnect hole boundaries when it rebuilds a perforated face.
    // Adding untouched holes afterward preserves their exact manufacturing loops.
    if(!holes.empty()){
        BRepBuilderAPI_MakeFace perforated(cap);
        for(const auto& hole:holes)perforated.Add(TopoDS::Wire(wire(hole).Reversed()));
        if(!perforated.IsDone())throw std::runtime_error("PLATE_FACE_INVALID");
        cap=perforated.Face();
    }
    auto solid=BRepPrimAPI_MakePrism(cap,gp_Vec(m[2]*thickness,m[6]*thickness,m[10]*thickness)).Shape();
    if(!BRepCheck_Analyzer(solid,true).IsValid())throw std::runtime_error("PLATE_SOLID_INVALID");
    if(profile)*profile=ConstructedProfile(cap,m);
    return solid;
}
std::vector<P2> Normalize(const std::vector<P2>& input,bool simplify=true) {
    PathD path;for(auto p:input)path.emplace_back(p[0],p[1]);
    auto loops=Union(PathsD{path},FillRule::NonZero,6);
    if(loops.size()!=1||loops.front().size()<3)throw std::runtime_error("PLATE_OUTLINE_INVALID");
    auto loop=simplify?SimplifyPath(loops.front(),.00001):loops.front();if(!IsPositive(loop))std::reverse(loop.begin(),loop.end());
    // Persist a canonical start: bottom left then bottom right. DXF footprints
    // use this construction provenance rather than guessing a CAD face index.
    auto begin=std::min_element(loop.begin(),loop.end(),[](auto a,auto b){return a.y<b.y-1e-7||(std::abs(a.y-b.y)<1e-7&&a.x<b.x);});
    std::rotate(loop.begin(),begin,loop.end());std::vector<P2> result;for(auto p:loop)result.push_back({p.x,p.y});return result;
}
// Every selected section segment blocks material above it. Unioning those
// blockers selects the lowest reachable branch, including coincident face
// seams. Missing intervals get a low connecting web rather than an invented
// contact edge spanning a hole. Clipper owns all polygon topology operations.
struct SupportSection {
    std::vector<std::array<P2,2>> Segments;
    std::vector<std::array<double,2>> Gaps;
    double First=0,Last=0,Ceiling=0;
    explicit SupportSection(std::vector<std::array<P2,2>> segments):Segments(std::move(segments)) {
        std::sort(Segments.begin(),Segments.end(),[](const auto&a,const auto&b){return a[0][0]<b[0][0];});
        First=Segments.front()[0][0];Last=Segments.front()[1][0];Ceiling=-1e100;
        for(const auto& segment:Segments){
            if(segment[0][0]>Last+1e-5)Gaps.push_back({Last,segment[0][0]});
            Last=std::max(Last,segment[1][0]);
            Ceiling=std::max({Ceiling,segment[0][1],segment[1][1]});
        }
    }
    std::vector<P2> Outline(double floor,double minimumWeb,double retreat,const std::function<void()>& check)const {
        const double ceiling=Ceiling-floor+1.;PathsD blockers;
        for(const auto& segment:Segments){
            const auto a=segment[0],b=segment[1];
            blockers.push_back({{a[0],a[1]-floor-retreat},{b[0],b[1]-floor-retreat},{b[0],ceiling},{a[0],ceiling}});
        }
        for(const auto& gap:Gaps)blockers.push_back({{gap[0],minimumWeb},{gap[1],minimumWeb},{gap[1],ceiling},{gap[0],ceiling}});
        // Filled curved surfaces can contribute tens of thousands of mutually
        // overlapping projected edges. A balanced union discards hidden edges
        // early instead of constructing all pairwise intersections at once.
        std::vector<PathsD> groups;for(size_t i=0;i<blockers.size();i+=128){check();const auto last=std::min(i+128,blockers.size());groups.push_back(Union(PathsD(blockers.begin()+i,blockers.begin()+last),FillRule::NonZero,6));}
        while(groups.size()>1){std::vector<PathsD> next;for(size_t i=0;i<groups.size();i+=2){check();auto joined=std::move(groups[i]);if(i+1<groups.size())joined.insert(joined.end(),std::make_move_iterator(groups[i+1].begin()),std::make_move_iterator(groups[i+1].end()));next.push_back(Union(joined,FillRule::NonZero,6));}groups=std::move(next);}
        auto material=Difference(PathsD{{{First,0.},{Last,0.},{Last,ceiling},{First,ceiling}}},groups.front(),FillRule::NonZero,6);
        if(material.size()!=1)throw std::runtime_error("PLATE_OUTLINE_INVALID");
        std::vector<P2> loop;for(auto p:material.front())loop.push_back({p.x,p.y});return Normalize(loop);
    }
};
struct SlabEdge {std::array<P2,2> Profile;std::array<double,3> First,Last;double NormalZ;};
vtkSmartPointer<vtkPolyData> ClipSlab(vtkPolyData* mesh,bool alongX,double station,double thickness) {
    auto lower=vtkSmartPointer<vtkPlane>::New(),upper=vtkSmartPointer<vtkPlane>::New();
    lower->SetOrigin(alongX?station-thickness/2:0,alongX?0:station-thickness/2,0);lower->SetNormal(alongX?1:0,alongX?0:1,0);
    upper->SetOrigin(alongX?station+thickness/2:0,alongX?0:station+thickness/2,0);upper->SetNormal(alongX?-1:0,alongX?0:-1,0);
    auto a=vtkSmartPointer<vtkClipPolyData>::New(),b=vtkSmartPointer<vtkClipPolyData>::New();
    a->SetInputData(mesh);a->SetClipFunction(lower);a->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION);
    b->SetInputConnection(a->GetOutputPort());b->SetClipFunction(upper);b->SetOutputPointsPrecision(vtkAlgorithm::DOUBLE_PRECISION);b->Update();
    auto output=vtkSmartPointer<vtkPolyData>::New();output->ShallowCopy(b->GetOutput());return output;
}
iCAX::FixtureMesh::Mesh SlabMesh(vtkPolyData* mesh,bool alongX,double station,double thickness) {
    auto triangulator=vtkSmartPointer<vtkTriangleFilter>::New();triangulator->SetInputData(ClipSlab(mesh,alongX,station,thickness));
    triangulator->PassLinesOff();triangulator->PassVertsOff();triangulator->Update();const auto data=triangulator->GetOutput();
    iCAX::FixtureMesh::Mesh result;
    for(vtkIdType i=0;i<data->GetNumberOfPoints();++i){double p[3];data->GetPoint(i,p);result.Points.emplace_back(p[0],p[1],p[2]);}
    auto cells=data->GetPolys();vtkIdType n;const vtkIdType* ids;cells->InitTraversal();
    while(cells->GetNextCell(n,ids)){if(n!=3)throw std::runtime_error("PLATE_TRIANGULATION_FAILED");
        const auto& a=result.Points[ids[0]];const auto& b=result.Points[ids[1]];const auto& c=result.Points[ids[2]];
        if((b-a).cross(c-a).squaredNorm()>1e-24)result.Triangles.emplace_back(static_cast<unsigned>(ids[0]),static_cast<unsigned>(ids[1]),static_cast<unsigned>(ids[2]));}
    result.Prepare();return result;
}
std::vector<SlabEdge> SlabEdges(vtkPolyData* mesh,bool alongX,double station,double thickness) {
    if(!mesh->GetNumberOfCells())return {};
    const int u=alongX?1:0;auto output=ClipSlab(mesh,alongX,station,thickness);
    auto cells=output->GetPolys();vtkIdType n;const vtkIdType* ids;cells->InitTraversal();std::vector<SlabEdge> edges;
    while(cells->GetNextCell(n,ids)){
        if(n<3)continue;double p[3],q[3],r[3];output->GetPoint(ids[0],p);output->GetPoint(ids[1],q);output->GetPoint(ids[2],r);
        auto normal=(coal::Vec3s(q[0]-p[0],q[1]-p[1],q[2]-p[2])).cross(coal::Vec3s(r[0]-p[0],r[1]-p[1],r[2]-p[2]));
        const double nz=normal.norm()>1e-12?std::abs(normal.z())/normal.norm():0;
        for(vtkIdType i=0;i<n;++i){std::array<double,3> first,last;output->GetPoint(ids[i],first.data());output->GetPoint(ids[(i+1)%n],last.data());
            if(first[u]>last[u])std::swap(first,last);if(last[u]-first[u]<1e-7)continue;
            SlabEdge edge;edge.Profile={P2{first[u],first[2]},P2{last[u],last[2]}};edge.First=first;edge.Last=last;edge.NormalZ=nz;edges.push_back(edge);}
    }return edges;
}
double Cross(P2 a,P2 b,P2 c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
PathD JointPath(const std::vector<P2>& loop){PathD path;for(auto p:loop)path.emplace_back(p[0],p[1]);return path;}
std::vector<P2> JointRect(double x0,double y0,double x1,double y1){return {{x0,y0},{x1,y0},{x1,y1},{x0,y1}};}
#include "SectionPlateClearance.inc"
#include "SectionPlateJoints.inc"
#include "SectionPlateWindows.inc"
#include "SectionPlateOpenings.inc"
#include "SectionPlateLowerEnvelope.inc"
#include "SectionPlateSkeleton.inc"
std::vector<P2> ContactHull(std::vector<P2> points){
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());if(points.size()<3)return {};
    std::vector<P2> hull;for(auto p:points){while(hull.size()>1&&Cross(hull[hull.size()-2],hull.back(),p)<=1e-8)hull.pop_back();hull.push_back(p);}const auto lower=hull.size();
    for(auto i=points.rbegin()+1;i!=points.rend();++i){while(hull.size()>lower&&Cross(hull[hull.size()-2],hull.back(),*i)<=1e-8)hull.pop_back();hull.push_back(*i);}hull.pop_back();return hull;
}
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
    std::vector<SectionPlate> Build(const SectionPlateInput& input,const std::function<bool()>& cancelled)const override {
        const PlateDirectionFrame directionFrame(input.SupportDirection);auto in=directionFrame.Align(input);
        const auto started=std::chrono::steady_clock::now();
        std::mutex traceMutex,cancellationMutex;const bool tracing=std::getenv("ICAX_FIXTURE_TRACE")!=nullptr;
        const auto trace=[&](const auto& text){if(tracing){std::lock_guard lock(traceMutex);std::cerr<<"fixture "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms "<<text<<std::endl;}};
        const auto isCancelled=[&]{std::lock_guard lock(cancellationMutex);return cancelled&&cancelled();};
        const auto check=[&]{if(isCancelled())throw std::runtime_error("PLATE_CANCELLED");};
        const bool skeleton=in.Automatic&&in.Pattern=="Skeleton";
        if(skeleton&&!in.CountLayout&&(!std::isfinite(in.MaximumSupportSpan)||in.MaximumSupportSpan<=in.Thickness+.001))throw std::invalid_argument("PLATE_SPACING_INVALID");
        if(skeleton&&in.CountLayout&&(in.SupportCount<2||in.ConnectionCount<2||in.SupportCount>32||in.ConnectionCount>32||in.SupportCount+in.ConnectionCount>40))throw std::invalid_argument("PLATE_COUNT_INVALID");
        if(!in.Geometry||in.FaceIDs.empty()||in.Stations.empty()||in.Stations.size()>128)throw std::invalid_argument("PLATE_INPUT_REQUIRED");
        for(double n:{in.Thickness,in.BaseThickness,in.BaseDrop,in.Margin,in.Gap,in.MinimumWeb})if(!std::isfinite(n)||n<=0||n>10000)throw std::invalid_argument("PLATE_DIMENSION_INVALID");
        if(in.Gap<.01||in.BaseDrop<in.MinimumWeb+in.Gap)throw std::invalid_argument("PLATE_DIMENSION_INVALID");
        ValidateJoints(in);
        ValidateWindows(in);
        ValidateKeepOuts(in);
        if(in.AvoidOpeningBoundaries&&(!std::isfinite(in.OpeningBoundaryMargin)||in.OpeningBoundaryMargin<0||in.OpeningBoundaryMargin>10000))throw std::invalid_argument("PLATE_OPENING_MARGIN_INVALID");
        if(!std::isfinite(in.OpeningReliefDepth)||in.OpeningReliefDepth<0||in.OpeningReliefDepth>1000)throw std::invalid_argument("PLATE_OPENING_DEPTH_INVALID");
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
        if(!built.bOK||built.Shape.IsNull())throw std::runtime_error("PLATE_BREP_INVALID");
        trace("brep ready");
        const auto& m=in.Transform;gp_Trsf tr;tr.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);
        const TopLoc_Location location(tr);const auto work=built.Shape.Moved(location);
        Bnd_Box box;BRepBndLib::AddOptimal(work,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        const double floor=z0-in.BaseDrop;
        double openingPlaneDeviationUpper=0;size_t openingNonExitBoundaries=0;
        const auto openingCounts=AddOpeningBoundaryKeepOuts(in,built,location,floor-in.BaseThickness,openingPlaneDeviationUpper,openingNonExitBoundaries,check);
        trace("opening keep-outs ready");
        ValidateKeepOuts(in);
        BRep_Builder builder;TopoDS_Compound supports,surfaces,others;builder.MakeCompound(supports);builder.MakeCompound(surfaces);builder.MakeCompound(others);
        bool curvedSupport=false;
        for(auto id:in.FaceIDs){check();const auto f=built.Faces.find(id);if(f==built.Faces.end())throw std::invalid_argument("PLATE_FACE_MISSING");const auto face=TopoDS::Face(f->second.Moved(location));curvedSupport|=BRepAdaptor_Surface(face,false).GetType()!=GeomAbs_Plane;builder.Add(supports,face);builder.Add(surfaces,SupportSurface(face));}
        bool hasOthers=false;for(const auto& [id,face]:built.Faces)if(std::find(in.FaceIDs.begin(),in.FaceIDs.end(),id)==in.FaceIDs.end()){builder.Add(others,face.Moved(location));hasOthers=true;}
        const double deflection=std::min(.005,in.Gap/10);
        const bool bands=skeleton&&in.CountLayout;
        const double workDeflection=deflection;
        trace("work mesh starting");
        iCAX::FixtureMesh::Mesh workMesh;if(!bands)workMesh=iCAX::FixtureMesh::Tessellate(work,workDeflection,cancelled);
        trace("work mesh "+std::to_string(workMesh.Triangles.size()));
        auto workData=workMesh.PolyData();
        // These faces share the workpiece's already meshed TShapes.
        std::optional<iCAX::FixtureMesh::Mesh> contactMesh,otherMesh;vtkSmartPointer<vtkPolyData> contactData=workData,otherData;
        iCAX::FixtureMesh::Mesh supportMesh;
        if(bands){std::vector<std::pair<TopoDS_Face,bool>> sourceFaces;for(const auto& [id,face]:built.Faces)sourceFaces.emplace_back(TopoDS::Face(face.Moved(location)),std::find(in.FaceIDs.begin(),in.FaceIDs.end(),id)!=in.FaceIDs.end());
            otherMesh.emplace();contactMesh=PlateBandMesh(work,in,x0,y0,z0,x1,y1,z1,deflection,false,check,&sourceFaces,&supportMesh,&*otherMesh);contactData=contactMesh->PolyData();otherData=otherMesh->PolyData();
        }else {supportMesh=iCAX::FixtureMesh::Collect(supports,deflection,cancelled);if(hasOthers){otherMesh=iCAX::FixtureMesh::Collect(others,deflection,cancelled);otherData=otherMesh->PolyData();}}
        trace("support mesh "+std::to_string(supportMesh.Triangles.size()));
        auto supportData=supportMesh.PolyData();
        auto surfaceMesh=bands?PlateBandMesh(surfaces,in,x0,y0,z0,x1,y1,z1,deflection,true,check):iCAX::FixtureMesh::Tessellate(surfaces,deflection,cancelled);
        trace("surface mesh "+std::to_string(surfaceMesh.Triangles.size()));
        auto surfaceData=surfaceMesh.PolyData();
        const double contactAllowance=contactMesh?contactMesh->Allowance:workMesh.Allowance;
        trace("contact strips ready");
        GProp_GProps mass;bool volume=false;for(TopExp_Explorer e(work,TopAbs_SOLID);e.More();e.Next()){BRepGProp::VolumeProperties(work,mass);volume=std::abs(mass.Mass())>1e-9;break;}
        auto center=mass.CentreOfMass();if(!volume&&bands){BRepGProp::SurfaceProperties(work,mass);if(mass.Mass()<=0)throw std::runtime_error("PLATE_MESH_EMPTY");center=mass.CentreOfMass();}
        else if(!volume){coal::Vec3s weighted=coal::Vec3s::Zero();double area=0;
            for(const auto& triangle:workMesh.Triangles){const auto& a=workMesh.Points[triangle[0]];const auto& b=workMesh.Points[triangle[1]];const auto& c=workMesh.Points[triangle[2]];double weight=(b-a).cross(c-a).norm();weighted+=(a+b+c)*(weight/3);area+=weight;}
            if(area<=0)throw std::runtime_error("PLATE_MESH_EMPTY");weighted/=area;center.SetCoord(weighted.x(),weighted.y(),weighted.z());}
        std::vector<SectionPlate> result;std::vector<TopoDS_Shape> solids;
        std::map<size_t,PathsD> keepOutSlices,bearingRegions;size_t rejectedStations=0;
        std::map<std::pair<bool,double>,std::vector<SlabEdge>> selectedSlices,surfaceSlices,otherSlices;
        const double cx=in.AreaWidth>0?in.AreaX:(x0+x1)/2,cy=in.AreaDepth>0?in.AreaY:(y0+y1)/2;
        const double areaWidth=in.AreaWidth>0?in.AreaWidth:(in.BaseShape=="Circle"?std::max(x1-x0,y1-y0):x1-x0)+2*in.Margin,areaDepth=in.BaseShape=="Circle"?areaWidth:in.AreaDepth>0?in.AreaDepth:y1-y0+2*in.Margin;
        std::vector<std::pair<bool,double>> sliceKeys;
        for(const auto& station:in.Stations){if(!std::isfinite(station.Coordinate))throw std::invalid_argument("PLATE_STATION_OVERLAP");if(skeleton&&station.Connection)continue;const auto key=std::make_pair(station.AlongX,station.Coordinate);
            if(selectedSlices.try_emplace(key).second){surfaceSlices.try_emplace(key);otherSlices.try_emplace(key);sliceKeys.push_back(key);}}
        const PlateSlabSource selectedSource(supportMesh,supportData),surfaceSource(surfaceMesh,surfaceData);
        std::unique_ptr<PlateSlabSource> otherSource;if(hasOthers)otherSource=std::make_unique<PlateSlabSource>(*otherMesh,otherData);
        const auto sliceWorkers=PlateMeshConcurrency({supportData,surfaceData,otherData},sliceKeys.size(),in.MaximumConcurrency);
        std::vector<std::vector<vtkIdType>> sliceMaps(sliceWorkers);
        ForPlateIndices(sliceKeys.size(),sliceWorkers,[&](size_t i,size_t worker){check();const auto key=sliceKeys[i];auto& mapping=sliceMaps[worker];
            selectedSlices.at(key)=SlabEdges(selectedSource.Region(key.first,key.second,in.Thickness,mapping,check),key.first,key.second,in.Thickness);
            surfaceSlices.at(key)=SlabEdges(surfaceSource.Region(key.first,key.second,in.Thickness,mapping,check),key.first,key.second,in.Thickness);
            if(hasOthers)otherSlices.at(key)=SlabEdges(otherSource->Region(key.first,key.second,in.Thickness,mapping,check),key.first,key.second,in.Thickness);
        });
        trace("slices complete workers="+std::to_string(sliceWorkers));sliceMaps.clear();
        struct StationPreparation {std::optional<SectionPlate> Plate;PathsD Forbidden,Bearing;bool HasForbidden=false,HasBearing=false;size_t Rejected=0;};
        std::vector<StationPreparation> preparedStations(in.Stations.size());
        // These short Clipper operations allocate frequently: concurrent
        // profiles contend on the CRT heap (6 s serial versus 15 s at four
        // workers on the real grid). Parallelize slicing and solid work instead.
        ForPlateIndices(in.Stations.size(),1,[&](size_t stationIndex,size_t){
            auto& prepared=preparedStations[stationIndex];
            const auto& placement=in.Stations[stationIndex];const double station=placement.Coordinate;const bool alongX=placement.AlongX;
            check();if(!std::isfinite(station))throw std::invalid_argument("PLATE_STATION_OVERLAP");
            const double delta=std::abs(station-(alongX?cx:cy))+in.Thickness/2,radius=(alongX?areaWidth:areaDepth)/2-in.Margin;
            if(delta>=radius){if(in.Automatic)return;throw std::invalid_argument("PLATE_SUPPORT_OUTSIDE_BASE");}
            const double centerU=alongX?cy:cx,reach=in.BaseShape=="Circle"?std::sqrt(radius*radius-delta*delta):(alongX?areaDepth:areaWidth)/2-in.Margin;
            const double from=std::max(placement.From,centerU-reach),to=std::min(placement.To,centerU+reach);
            if(skeleton&&placement.Connection){
                SectionPlate plate;plate.Connection=true;plate.StationIndex=stationIndex;plate.Thickness=in.Thickness;plate.MeshDeflection=deflection;plate.ClearanceAllowance=contactAllowance;
                plate.Frame=alongX?std::array<double,16>{0,0,1,station-in.Thickness/2,1,0,0,0,0,1,0,floor,0,0,0,1}:std::array<double,16>{1,0,0,0,0,0,-1,station+in.Thickness/2,0,1,0,floor,0,0,0,1};
                const double height=z0-floor-in.Gap-contactAllowance-2*deflection-.00001;
                if(height<in.MinimumWeb)throw std::runtime_error("PLATE_MINIMUM_WEB");
                plate.Outline=JointRect(from,0,to,height);plate.Span={from,to};
                auto forbidden=PlateKeepOutProjection(in,plate,check);
                try{ApplyPlateKeepOuts(in,plate,forbidden);}catch(const std::runtime_error&){prepared.Rejected=1;return;}
                prepared.Forbidden=std::move(forbidden);prepared.HasForbidden=true;prepared.Plate=std::move(plate);return;
            }
            const auto& slab=std::as_const(selectedSlices).at({alongX,station});check();
            std::vector<SlabEdge> clipped;for(auto edge:slab){const auto& profile=edge.Profile;const double first=std::max(profile[0][0],from),last=std::min(profile[1][0],to);if(last-first<1e-7)continue;
                const double a=(first-profile[0][0])/(profile[1][0]-profile[0][0]),b=(last-profile[0][0])/(profile[1][0]-profile[0][0]);SlabEdge part=edge;
                for(int k=0;k<3;++k){part.First[k]=edge.First[k]+a*(edge.Last[k]-edge.First[k]);part.Last[k]=edge.First[k]+b*(edge.Last[k]-edge.First[k]);}part.Profile={P2{first,part.First[2]},P2{last,part.Last[2]}};clipped.push_back(part);}
            auto selected=std::move(clipped);std::vector<std::array<P2,2>> segments;
            for(const auto& edge:std::as_const(surfaceSlices).at({alongX,station})){
                auto p=edge.Profile;const double first=std::max(p[0][0],from),last=std::min(p[1][0],to);if(last-first<1e-7)continue;
                const double slope=(p[1][1]-p[0][1])/(p[1][0]-p[0][0]);segments.push_back({P2{first,p[0][1]+slope*(first-p[0][0])},P2{last,p[0][1]+slope*(last-p[0][0])}});
            }
            if(segments.empty()){if(in.Automatic)return;throw std::runtime_error("PLATE_STATION_NO_SUPPORT");}
            const SupportSection section(std::move(segments));
            if(section.Last-section.First<in.Thickness){if(in.Automatic)return;throw std::runtime_error("PLATE_SUPPORT_TOO_NARROW");}
            SectionPlate plate;plate.StationIndex=stationIndex;plate.Thickness=in.Thickness;plate.MeshDeflection=deflection;plate.ClearanceAllowance=contactAllowance;plate.ReliefCount=section.Gaps.size();
            // Curved tessellation chords may lie above a concave surface.
            // Record the numerical retreat separately from manufacturing gap.
            plate.TopRetreat=curvedSupport?2*deflection:0;
            plate.Frame=alongX?std::array<double,16>{0,0,1,station-in.Thickness/2,1,0,0,0,0,1,0,floor,0,0,0,1}
                :std::array<double,16>{1,0,0,0,0,0,-1,station+in.Thickness/2,0,1,0,floor,0,0,0,1};
            auto blockers=section.Segments;for(const auto& edge:selected)blockers.push_back(edge.Profile);
            if(hasOthers)for(const auto& edge:std::as_const(otherSlices).at({alongX,station})){auto segment=edge.Profile;for(auto& p:segment)p[1]-=in.Gap+contactAllowance;blockers.push_back(segment);}
            SupportSection envelope(std::move(blockers));envelope.First=section.First;envelope.Last=section.Last;envelope.Gaps=section.Gaps;
            plate.Outline=envelope.Outline(floor,in.MinimumWeb,plate.TopRetreat,check);plate.Span={section.First,section.Last};
            if(skeleton&&in.CountLayout){
                // The top follows the actual bearing span. Its low connecting
                // web can still reach both rails of a tapered part, without
                // moving a requested station or inventing a contact surface.
                const double height=z0-floor-in.Gap-contactAllowance-2*deflection-.00001;
                if(height<in.MinimumWeb)throw std::runtime_error("PLATE_MINIMUM_WEB");
                JointProfile(plate,PathsD{JointPath(JointRect(from,0,to,height))},{});plate.Span={from,to};
            }
            trace("outline "+std::to_string(stationIndex)+" "+std::to_string(plate.Outline.size()));
            if(in.Automatic&&in.Pattern=="Staggered"&&in.JointMode=="TabSlot"){
                // The contact patch may taper at the part boundary. Keep the
                // requested low foot width for its tenon; never invent a high
                // contact edge across that missing patch. This band is below
                // the global workpiece minimum by the validated base drop.
                if(to-from<in.TenonWidth+2*in.JointWeb+in.JointClearance-1e-6)return;
                JointProfile(plate,PathsD{JointPath(JointRect(from,0,to,in.MinimumWeb))},{});plate.Span={from,to};
            }
            if(!in.KeepOuts.empty()){
                auto forbidden=PlateKeepOutProjection(in,plate,check);
                try{ApplyPlateKeepOuts(in,plate,forbidden);}catch(const std::runtime_error&){if(!in.Automatic)throw;prepared.Rejected=1;return;}
                prepared.Forbidden=std::move(forbidden);prepared.HasForbidden=true;
                if(plate.KeepOutReliefCount){prepared.Bearing=PlateBearingRegion(plate,in.MinimumWeb);prepared.HasBearing=true;}
            }
            UpdatePlateContacts(plate,selected,in.MinimumWeb,prepared.HasBearing?&prepared.Bearing:nullptr);
            if(plate.Contacts.empty()){if(in.Automatic){if(plate.KeepOutReliefCount)prepared.Rejected=1;return;}throw std::runtime_error("PLATE_SELECTED_SUPPORT_OBSTRUCTED");}
            prepared.Plate=std::move(plate);
        });
        for(size_t i=0;i<preparedStations.size();++i){auto& prepared=preparedStations[i];rejectedStations+=prepared.Rejected;if(prepared.HasForbidden)keepOutSlices.emplace(i,std::move(prepared.Forbidden));if(prepared.HasBearing)bearingRegions.emplace(i,std::move(prepared.Bearing));if(prepared.Plate)result.push_back(std::move(*prepared.Plate));}
        trace("profiles complete workers=1");
        auto skeletonCandidates=skeleton?result:std::vector<SectionPlate>{};
        if(skeleton)result=SelectSkeletonPlates(in,skeletonCandidates,x0,y0,x1,y1);
        check();SectionPlate base;base.Base=true;base.Thickness=in.BaseThickness;base.MeshDeflection=bands?0:workDeflection;base.ClearanceAllowance=workMesh.Allowance;
        base.Frame={1,0,0,0,0,1,0,0,0,0,1,floor-in.BaseThickness,0,0,0,1};
        PathsD border;if(in.BaseShape=="Rectangle")border=PathsD{{{cx-areaWidth/2,cy-areaDepth/2},{cx+areaWidth/2,cy-areaDepth/2},{cx+areaWidth/2,cy+areaDepth/2},{cx-areaWidth/2,cy+areaDepth/2}}};
        else if(in.BaseShape=="Circle"){const int count=std::max(192,(static_cast<int>(std::ceil(std::acos(-1.)/std::acos(1-deflection/(areaWidth/2))))+3)/4*4);PathD circle;for(int i=0;i<count;++i){double angle=2*std::acos(-1.)*i/count;circle.emplace_back(cx+areaWidth/2*std::cos(angle),cy+areaWidth/2*std::sin(angle));}border.push_back(circle);}else throw std::invalid_argument("PLATE_BASE_SHAPE_INVALID");
        if(border.size()!=1)throw std::runtime_error("PLATE_BASE_INVALID");std::vector<P2> baseLoop;for(auto p:border[0])baseLoop.push_back({p.x,p.y});base.Outline=Normalize(baseLoop);
        const auto baseForbidden=PlateKeepOutProjection(in,base,check);
        if(in.SolidBase){if(!Intersect(PlateMaterial(base),baseForbidden,FillRule::NonZero,6).empty())throw std::runtime_error("PLATE_BASE_PHYSICAL_CONFLICT");}
        else ApplyPlateKeepOuts(in,base,baseForbidden);
        if(in.JointMode=="Weld"&&!in.KeepOuts.empty()){
            auto& available=skeleton?skeletonCandidates:result;
            for(auto i=available.begin();i!=available.end();){try{CheckPlateFoot(in,*i,base);++i;}catch(const std::runtime_error&){if(!in.Automatic)throw;++rejectedStations;i=available.erase(i);}}
            if(skeleton)result=SelectSkeletonPlates(in,skeletonCandidates,x0,y0,x1,y1);
        }
        if(in.JointMode=="TabSlot"||skeleton){
            // Retry from untouched profiles after removing an infeasible
            // station, so neither half-laps nor obsolete base holes survive.
            const auto pristineBase=base;auto pristine=result;
            for(;;){result=pristine;base=pristineBase;
            std::vector<PathsD> allowed;for(const auto& p:result)allowed.push_back(Union(PathsD{JointPath(p.Outline)},
                PathsD{JointPath(JointRect(p.Span[0]-1,-in.BaseThickness-1,p.Span[1]+1,0))},FillRule::NonZero,6));
            try{ApplyPlateJoints(in,result,base,keepOutSlices,check);}catch(const PlateStationError& error){
                if(!in.Automatic||(!skeleton&&in.KeepOuts.empty()))throw;
                const auto before=pristine.size();if(skeleton){skeletonCandidates.erase(std::remove_if(skeletonCandidates.begin(),skeletonCandidates.end(),[&](const auto& p){return p.StationIndex==error.StationIndex;}),skeletonCandidates.end());pristine=SelectSkeletonPlates(in,skeletonCandidates,x0,y0,x1,y1);}
                else pristine.erase(std::remove_if(pristine.begin(),pristine.end(),[&](const auto& p){return p.StationIndex==error.StationIndex;}),pristine.end());
                if(pristine.empty()||(!skeleton&&pristine.size()==before))throw std::runtime_error("PLATE_KEEPOUT_NO_SUPPORT");++rejectedStations;continue;
            }
            // Joinery may remove material or add tenons below the seat. It may
            // never add material above the already checked surface envelope.
            for(size_t i=0;i<result.size();++i)if(const auto spill=Difference(PathsD{JointPath(result[i].Outline)},allowed[i],FillRule::NonZero,6);!spill.empty()){
                if(std::getenv("ICAX_FIXTURE_TRACE")){const auto precision=std::cerr.precision(17);const auto bounds=GetBounds(spill);
                    std::cerr<<"fixture joint spill station="<<result[i].StationIndex<<" bounds="<<bounds.left<<','<<bounds.top<<','<<bounds.right<<','<<bounds.bottom<<" area="<<Area(spill)<<std::endl;
                    for(const auto& path:spill){std::cerr<<"fixture joint spill path area="<<Area(path)<<" vertices=";for(const auto& point:path)std::cerr<<'['<<point.x<<','<<point.y<<']';std::cerr<<std::endl;}std::cerr.precision(precision);}
                throw std::runtime_error("PLATE_JOINT_ABOVE_SUPPORT");}
            break;}
        }
        trace("joints complete");
        // Every job owns its plate, native shape, mesh and distance query. The
        // layout and spatial index are read-only; each clipping result is local.
        const auto plateLayout=result;const PlateSlabSource workSource(contactMesh?*contactMesh:workMesh,contactData);const auto plateWorkers=PlateMeshConcurrency({contactData},result.size()+1,in.MaximumConcurrency);std::vector<std::vector<vtkIdType>> workMaps(plateWorkers);solids.resize(result.size()+1);
        std::exception_ptr baseFailure;const auto buildBase=[&]{
        ApplyPlateWindows(in,base,plateLayout,check);
        if(in.JointMode=="Weld")for(const auto& plate:plateLayout)CheckPlateFoot(in,plate,base);
        CheckPlateKeepOuts(base,baseForbidden);base.RejectedStationCount=rejectedStations;
        base.OpeningBoundaryCount=openingCounts.first;base.UnclassifiedOpeningFaceCount=openingCounts.second;
        base.OpeningPlaneDeviationUpper=openingPlaneDeviationUpper;
        base.OpeningNonExitBoundaryCount=openingNonExitBoundaries;
        auto bottom=Solid(base.Outline,base.Frame,in.BaseThickness,base.Holes);
        if(bands)base.Distance=in.BaseDrop; // Entire base lies below the exact workpiece Z bound.
        else {auto bottomMesh=iCAX::FixtureMesh::Tessellate(bottom,deflection,isCancelled,false);base.Distance=std::max(0.,iCAX::FixtureMesh::Measure(bottomMesh,workMesh).Distance-workMesh.Allowance);}
        if(base.Distance+1e-6<in.Gap)throw std::runtime_error("PLATE_BASE_CLEARANCE");
        solids[plateLayout.size()]=std::move(bottom);trace("base prepared");
        };
        ForPlateIndices(result.size()+1,plateWorkers,[&](size_t job,size_t worker){if(!job){try{buildBase();}catch(...){baseFailure=std::current_exception();}return;}const auto i=job-1;auto& plate=result[i];check();ApplyPlateWindows(in,plate,plateLayout,check);const auto& station=in.Stations[plate.StationIndex];const auto bearing=std::as_const(bearingRegions).find(plate.StationIndex);if(!plate.Connection)UpdatePlateContacts(plate,std::as_const(selectedSlices).at({station.AlongX,station.Coordinate}),in.MinimumWeb,bearing==bearingRegions.end()?nullptr:&bearing->second);
            if(!plate.Connection&&plate.Contacts.empty())throw std::runtime_error("PLATE_JOINT_CONTACT_LOST");
            if(!in.KeepOuts.empty())CheckPlateKeepOuts(plate,keepOutSlices.at(plate.StationIndex));
            auto candidate=Solid(plate.Outline,plate.Frame,in.Thickness,plate.Holes,in.JointMode=="TabSlot"?&plate.Joints:nullptr,in.TenonRadius,&plate.Outline,in.JointMode=="TabSlot"&&in.TenonRadius>0?&plate.ExactProfile:nullptr);auto mesh=iCAX::FixtureMesh::Tessellate(candidate,deflection,isCancelled,false);
            const double contactLimit=2*plate.TopRetreat+.001;
            // Geometry outside this expanded slab is farther than contactLimit
            // from every point of this constant-thickness plate. If Coal finds
            // an accepted distance inside, it is also the global mesh minimum.
            // This preserves exact distance queries without comparing a tall
            // narrow plate against every remote part of a dense workpiece.
            auto localWork=SlabMesh(workSource.Region(station.AlongX,station.Coordinate,in.Thickness+2*(contactLimit+.001),workMaps[worker],check),station.AlongX,station.Coordinate,in.Thickness+2*(contactLimit+.001));
            trace("distance "+std::to_string(plate.StationIndex)+" "+std::to_string(mesh.Triangles.size())+" / "+std::to_string(localWork.Triangles.size()));
            plate.Distance=iCAX::FixtureMesh::Measure(mesh,localWork).Distance;
            trace("distance done "+std::to_string(plate.Distance));
            if(plate.Connection){plate.ClearanceAllowance=contactAllowance+mesh.Allowance;plate.Distance=std::max(0.,plate.Distance-plate.ClearanceAllowance);if(plate.Distance+1e-6<in.Gap)throw std::runtime_error("PLATE_CONNECTION_CLEARANCE");}
            else if(plate.Distance>contactLimit)throw std::runtime_error("PLATE_SUPPORT_NO_CONTACT");solids[i]=std::move(candidate);});
        if(baseFailure)std::rethrow_exception(baseFailure);
        trace("plates complete workers="+std::to_string(plateWorkers));workMaps.clear();
        if(skeleton)RecordSkeletonCoverage(in,result,base,x0,y0,x1,y1);
        std::vector<P2> contacts;size_t bearingPlates=0;for(const auto& p:result){if(!p.Contacts.empty())++bearingPlates;for(auto q:p.Contacts)contacts.push_back({q[0],q[1]});}
        base.SupportHull=ContactHull(std::move(contacts));base.CenterOfMass={center.X(),center.Y(),center.Z()};base.CenterMethod=volume?"UniformSolid":"UniformSheet";
        base.SupportMargin=-1.;if(base.SupportHull.size()>=3){base.SupportMargin=1e100;for(size_t i=0;i<base.SupportHull.size();++i){const auto a=base.SupportHull[i],b=base.SupportHull[(i+1)%base.SupportHull.size()];base.SupportMargin=std::min(base.SupportMargin,Cross(a,b,{center.X(),center.Y()})/std::hypot(b[0]-a[0],b[1]-a[1]));}}
        base.Supported=bearingPlates>=2&&base.SupportMargin>=in.Thickness;
        if(!in.KeepOuts.empty()&&!base.Supported)throw std::runtime_error("PLATE_KEEPOUT_INSUFFICIENT_SUPPORT");
        result.push_back(std::move(base));
        trace("base complete");
        ForPlateIndices(result.size(),in.MaximumConcurrency,[&](size_t i,size_t){check();result[i].Geometry=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(solids[i].Moved(TopLoc_Location(directionFrame.ToWorld)),"Section plate","generated",.05);directionFrame.Restore(result[i]);});
        trace("complete");
        return result;
    }
};
}
