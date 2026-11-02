#pragma once
#include <coal/distance.h>
#include <coal/BVH/BVH_model.h>
#include <coal/BV/OBBRSS.h>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkTriangleFilter.h>
#include <functional>
#include <limits>

// Shared geometry backend. No product identifiers or scene state.
namespace iCAX::FixtureMesh {
using BVH=coal::BVHModel<coal::OBBRSS>;
// Dense trimmed CAD can exceed one million triangles at the requested
// deflection. Include Coal's owned arrays and BVH nodes in the memory budget.
inline constexpr size_t MaximumMeshWorkingBytes=2ull*1024*1024*1024;
inline void CheckMeshBudget(size_t points,size_t triangles){
    const auto bytes=static_cast<long double>(points)*2*sizeof(coal::Vec3s)
        +static_cast<long double>(triangles)*(2*sizeof(coal::Triangle)+sizeof(unsigned))
        +(triangles?2*static_cast<long double>(triangles)-1:0)*sizeof(coal::BVNode<coal::OBBRSS>);
    if(points>std::numeric_limits<unsigned>::max()||triangles>std::numeric_limits<unsigned>::max()/2
       ||bytes>MaximumMeshWorkingBytes)throw std::runtime_error("PLATE_MESH_LIMIT");
}
struct Mesh {
    std::vector<coal::Vec3s> Points;
    std::vector<coal::Triangle> Triangles;
    std::shared_ptr<BVH> Tree;
    double Allowance=0;
    void Validate()const{
        if(Points.empty()||Triangles.empty())throw std::runtime_error("PLATE_MESH_EMPTY");
        CheckMeshBudget(Points.size(),Triangles.size());
    }
    void Prepare(){
        Validate();if(Tree)return;
        Tree=std::make_shared<BVH>();
        if(Tree->beginModel(static_cast<unsigned>(Triangles.size()),static_cast<unsigned>(Points.size()))!=coal::BVH_OK||
           Tree->addSubModel(Points,Triangles)!=coal::BVH_OK||Tree->endModel()!=coal::BVH_OK)throw std::runtime_error("PLATE_MESH_BUILD_FAILED");
    }
    vtkSmartPointer<vtkPolyData> PolyData()const {
        auto data=vtkSmartPointer<vtkPolyData>::New();auto points=vtkSmartPointer<vtkPoints>::New();points->SetDataTypeToDouble();
        for(const auto& p:Points)points->InsertNextPoint(p.x(),p.y(),p.z());
        auto cells=vtkSmartPointer<vtkCellArray>::New();for(const auto& t:Triangles){vtkIdType ids[]{static_cast<vtkIdType>(t[0]),static_cast<vtkIdType>(t[1]),static_cast<vtkIdType>(t[2])};cells->InsertNextCell(3,ids);}
        data->SetPoints(points);data->SetPolys(cells);return data;
    }
};
inline Mesh Extrude(const std::vector<std::array<double,2>>& loop,const std::array<double,16>& m,double thickness) {
    auto data=vtkSmartPointer<vtkPolyData>::New();auto points=vtkSmartPointer<vtkPoints>::New();points->SetDataTypeToDouble();
    auto cells=vtkSmartPointer<vtkCellArray>::New();const vtkIdType n=static_cast<vtkIdType>(loop.size());
    for(double z:{0.,thickness})for(auto p:loop)points->InsertNextPoint(m[0]*p[0]+m[1]*p[1]+m[2]*z+m[3],m[4]*p[0]+m[5]*p[1]+m[6]*z+m[7],m[8]*p[0]+m[9]*p[1]+m[10]*z+m[11]);
    std::vector<vtkIdType> front,back;for(vtkIdType i=0;i<n;++i){front.push_back(n-1-i);back.push_back(n+i);}cells->InsertNextCell(n,front.data());cells->InsertNextCell(n,back.data());
    for(vtkIdType i=0;i<n;++i){vtkIdType j=(i+1)%n,quad[]{i,j,n+j,n+i};cells->InsertNextCell(4,quad);}
    data->SetPoints(points);data->SetPolys(cells);auto filter=vtkSmartPointer<vtkTriangleFilter>::New();filter->SetInputData(data);filter->PassLinesOff();filter->PassVertsOff();filter->Update();
    auto triangles=filter->GetOutput();Mesh mesh;
    for(vtkIdType i=0;i<triangles->GetNumberOfPoints();++i){double p[3];triangles->GetPoint(i,p);mesh.Points.emplace_back(p[0],p[1],p[2]);}
    vtkIdType count;const vtkIdType* ids;auto polys=triangles->GetPolys();polys->InitTraversal();
    while(polys->GetNextCell(count,ids)){if(count!=3)throw std::runtime_error("PLATE_TRIANGULATION_FAILED");mesh.Triangles.emplace_back(static_cast<unsigned>(ids[0]),static_cast<unsigned>(ids[1]),static_cast<unsigned>(ids[2]));}
    // VTK may remove collinear polygon vertices. Counting n-2 triangles would
    // reject valid caps; verify the actual cap coverage and side area instead.
    double polygonArea=0,perimeter=0,frontArea=0,backArea=0,sideArea=0;
    for(size_t i=0;i<loop.size();++i){auto a=loop[i],b=loop[(i+1)%loop.size()];polygonArea+=a[0]*b[1]-a[1]*b[0];perimeter+=std::hypot(b[0]-a[0],b[1]-a[1]);}polygonArea=std::abs(polygonArea)/2;
    for(const auto& t:mesh.Triangles){const auto& a=mesh.Points[t[0]];const auto& b=mesh.Points[t[1]];const auto& c=mesh.Points[t[2]];double area=(b-a).cross(c-a).norm()/2;
        const auto depth=[&](const coal::Vec3s& p){return (p.x()-m[3])*m[2]+(p.y()-m[7])*m[6]+(p.z()-m[11])*m[10];};
        if(std::abs(depth(a))+std::abs(depth(b))+std::abs(depth(c))<1e-6)frontArea+=area;
        else if(std::abs(depth(a)-thickness)+std::abs(depth(b)-thickness)+std::abs(depth(c)-thickness)<1e-6)backArea+=area;else sideArea+=area;
    }
    const auto close=[](double a,double b){return std::abs(a-b)<=std::max(1e-6,std::abs(b)*1e-7);};
    if(polygonArea<=0||!close(frontArea,polygonArea)||!close(backArea,polygonArea)||!close(sideArea,perimeter*thickness))throw std::runtime_error("PLATE_TRIANGULATION_FAILED");
    mesh.Prepare();return mesh;
}
inline Mesh Collect(const TopoDS_Shape& shape,double deflection,const std::function<bool()>& cancelled) {
    const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("CAD_CLEARANCE_CANCELLED");};check();
    Mesh mesh;bool linear=true;double tolerance=0;
    for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next()){check();BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));linear&=c.GetType()==GeomAbs_Line;tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));}
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()){
        // Surface kind does not depend on trimming bounds. Computing UVBounds
        // here projects every boundary edge, allocating heavily on holed caps.
        check();auto face=TopoDS::Face(e.Current());linear=linear&&BRepAdaptor_Surface(face,false).GetType()==GeomAbs_Plane;tolerance=std::max(tolerance,BRep_Tool::Tolerance(face));
        TopLoc_Location location;const auto triangles=BRep_Tool::Triangulation(face,location);
        if(triangles.IsNull())throw std::runtime_error("PLATE_MESH_MISSING_FACE");
        if(!triangles->HasUVNodes())throw std::runtime_error("PLATE_MESH_UV_MISSING");
        TopLoc_Location surfaceLocation;const auto surface=BRep_Tool::Surface(face,surfaceLocation);
        const auto offset=static_cast<unsigned>(mesh.Points.size());
        CheckMeshBudget(mesh.Points.size()+triangles->NbNodes(),mesh.Triangles.size()+triangles->NbTriangles());
        for(int i=1;i<=triangles->NbNodes();++i){if(i%4096==0)check();const auto uv=triangles->UVNode(i);auto p=surface->Value(uv.X(),uv.Y()).Transformed(surfaceLocation.Transformation());mesh.Points.emplace_back(p.X(),p.Y(),p.Z());}
        for(int i=1;i<=triangles->NbTriangles();++i){if(i%4096==0)check();int a,b,c;triangles->Triangle(i).Get(a,b,c);auto p=mesh.Points[offset+a-1],q=mesh.Points[offset+b-1],r=mesh.Points[offset+c-1];
            if((q-p).cross(r-p).squaredNorm()<1e-24)continue;mesh.Triangles.emplace_back(offset+a-1,offset+b-1,offset+c-1);}
    }
    // Engineering allowance, recorded with the result. This is not a proof
    // of an arbitrary CAD surface's Hausdorff error. Linear planar solids are exact.
    mesh.Allowance=linear?0:2*deflection+tolerance;
    // Clipping only needs PolyData. A distance query builds its BVH on demand.
    check();mesh.Validate();return mesh;
}
inline Mesh Tessellate(const TopoDS_Shape& shape,double deflection,const std::function<bool()>& cancelled,bool parallel=true) {
    if(cancelled&&cancelled())throw std::runtime_error("CAD_CLEARANCE_CANCELLED");
    BRepTools::Clean(shape);IMeshTools_Parameters parameters;
    parameters.Deflection=parameters.DeflectionInterior=deflection;
    parameters.Angle=parameters.AngleInterior=.1;
    parameters.ForceFaceDeflection=true;
    parameters.EnableControlSurfaceDeflectionAllSurfaces=true;
    parameters.InParallel=parallel;
    BRepMesh_IncrementalMesh mesher(shape,parameters);
    if(!mesher.IsDone())throw std::runtime_error("PLATE_MESH_FAILED");
    return Collect(shape,deflection,cancelled);
}
struct DistanceResult {double Distance=0;std::array<double,3> First{},Second{};};
inline DistanceResult Measure(Mesh& a,Mesh& b) {
    a.Prepare();b.Prepare();
    coal::DistanceRequest request(true,false,0,0);request.gjk_tolerance=1e-8;
    coal::DistanceResult result;coal::distance(a.Tree.get(),coal::Transform3s(),b.Tree.get(),coal::Transform3s(),request,result);
    if(!std::isfinite(result.min_distance))throw std::runtime_error("PLATE_DISTANCE_UNRESOLVED");
    DistanceResult out;out.Distance=std::max(0.,result.min_distance);
    if(out.Distance>0)for(int i=0;i<3;++i){out.First[i]=result.nearest_points[0][i];out.Second[i]=result.nearest_points[1][i];}
    return out;
}
}
