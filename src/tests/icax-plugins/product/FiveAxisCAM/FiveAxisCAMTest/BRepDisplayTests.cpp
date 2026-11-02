#include "pch.h"
#include <GeometryServices/BRepDisplayService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_Array1.hxx>
#include <cmath>
#include <set>
#include <GeometryData/BRepPersistence.h>

namespace {
    auto DisplayService() {
        iCAX::Services::CServiceProvider provider;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        return provider.Resolve<iCAX::GeometryServices::IBRepDisplayService>();
    }
    auto DisplayModel(const TopoDS_Shape& shape) {
        return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"display test","test",0.5);
    }
}
TEST(BRepDisplay, BoxHasTwelveUniqueTopologicalEdgesWithoutTriangleDiagonals) {
    auto model=DisplayModel(BRepPrimAPI_MakeBox(10,20,30).Shape());
    model.Triangulations3.clear(); // Topology output cannot depend on display tessellation.
    const auto result=DisplayService()->BuildEdges(model);
    ASSERT_EQ(result.Edges.size(),12u);
    std::set<std::uint64_t> ids;
    for(const auto& edge:result.Edges) {
        EXPECT_TRUE(ids.insert(edge.EdgeId).second);
        EXPECT_TRUE(edge.Feature);
        ASSERT_EQ(edge.Points.size(),2u);
        const auto& a=edge.Points.front(); const auto& b=edge.Points.back();
        EXPECT_EQ((a.X!=b.X)+(a.Y!=b.Y)+(a.Z!=b.Z),1);
    }
}
TEST(BRepDisplay, CylinderSeparatesPeriodicSeamAndSamplesCircularRimsByDeflection) {
    const auto model=DisplayModel(BRepPrimAPI_MakeCylinder(10,30).Shape());
    const auto result=DisplayService()->BuildEdges(model);
    ASSERT_EQ(result.Edges.size(),3u);
    int seam=0,rims=0;
    for(const auto& e:result.Edges) {
        if(!e.Feature) {++seam;EXPECT_EQ(e.Points.size(),2u);continue;}
        ++rims; ASSERT_GT(e.Points.size(),20u);
        for(std::size_t i=1;i<e.Points.size();++i) {
            const auto& a=e.Points[i-1]; const auto& b=e.Points[i];
            EXPECT_NEAR(std::hypot(a.X,a.Y),10,1e-8);
            EXPECT_LE(10-std::hypot((a.X+b.X)/2,(a.Y+b.Y)/2),0.051);
        }
    }
    EXPECT_EQ(seam,1);EXPECT_EQ(rims,2);
}
TEST(BRepDisplay, SpatialBezierUsesSourceCurvatureAndHonorsBudgetsAndCancellation) {
    NCollection_Array1<gp_Pnt> poles(1,4);
    poles.SetValue(1,{0,0,0});poles.SetValue(2,{10,25,10});
    poles.SetValue(3,{20,-15,30});poles.SetValue(4,{30,0,40});
    Handle(Geom_BezierCurve) curve=new Geom_BezierCurve(poles);
    const auto model=DisplayModel(BRepBuilderAPI_MakeEdge(curve).Edge());
    auto service=DisplayService();const auto result=service->BuildEdges(model);
    ASSERT_EQ(result.Edges.size(),1u);const auto& pts=result.Edges[0].Points;
    ASSERT_GT(pts.size(),10u); EXPECT_EQ(pts.front().X,0);EXPECT_NEAR(pts.back().Z,40,1e-10);
    for(int j=0;j<=1000;++j) {
        const auto p=curve->Value(j/1000.0);double distance=1e30;
        for(std::size_t i=1;i<pts.size();++i) {
            const gp_Pnt a(pts[i-1].X,pts[i-1].Y,pts[i-1].Z),b(pts[i].X,pts[i].Y,pts[i].Z);
            const gp_Vec ab(a,b),ap(a,p);const auto t=std::clamp(ap.Dot(ab)/ab.SquareMagnitude(),0.0,1.0);
            distance=std::min(distance,p.Distance(a.Translated(ab*t)));
        }
        EXPECT_LE(distance,0.051);
    }
    iCAX::GeometryServices::BRepDisplayOptions options;options.MaxPoints=2;
    EXPECT_THROW(service->BuildEdges(model,options),std::runtime_error);
    options={};options.IsCancelled=[] {return true;};
    EXPECT_THROW(service->BuildEdges(model,options),std::runtime_error);
    options={};options.Deflection=0;
    EXPECT_THROW(service->BuildEdges(model,options),std::invalid_argument);
    auto invalid=model;invalid.Edges.front().Curve3Id=UINT64_MAX;
    EXPECT_THROW(service->BuildEdges(invalid),std::invalid_argument);
}

TEST(BRepDisplay, RefinedMeshFollowsSurfaceAndRetainsCadTopologyAndFaceIdentities) {
    const auto model=DisplayModel(BRepPrimAPI_MakeCylinder(100,30).Shape());
    auto input=model;
    for(auto& face:input.Faces)face.Tolerance=.25;
    for(auto& edge:input.Edges)edge.Tolerance=.25;
    // A stale or displaced display mesh is not a source of CAD surface data.
    for(auto& record:input.Triangulations3)for(auto& p:record.Geometry.Vertices){p.X+=2;p.Z-=3;}
    const auto before=iCAX::GeometryData::Persistence::Serialize(input);
    auto service=DisplayService();iCAX::GeometryServices::BRepDisplayOptions options;options.Deflection=.001;
    auto refined=service->RefineMesh(input,options);
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(input));
    size_t originalTriangles=0,triangles=0;double maxChordError=0;
    for(const auto& r:model.Triangulations3)originalTriangles+=r.Geometry.Triangles.size();
    ASSERT_EQ(refined.Faces.size(),model.Faces.size());
    for(const auto& face:refined.Faces){
        const auto found=std::find_if(refined.Triangulations3.begin(),refined.Triangulations3.end(),[&](const auto& r){return r.Id==face.Triangulation3Id;});
        ASSERT_NE(found,refined.Triangulations3.end());const auto& mesh=found->Geometry;triangles+=mesh.Triangles.size();
        ASSERT_EQ(mesh.Normals.size(),mesh.Vertices.size());
        for(auto id:mesh.TriangleFaceIds)EXPECT_EQ(id,face.Id);
        for(const auto& p:mesh.Vertices){EXPECT_LE(std::hypot(p.X,p.Y),100.0000001);EXPECT_GE(p.Z,-1e-7);EXPECT_LE(p.Z,30.0000001);}
        for(const auto& triangle:mesh.Triangles){const auto a=mesh.Vertices[triangle[0]],b=mesh.Vertices[triangle[1]],c=mesh.Vertices[triangle[2]];
            if(std::max({a.Z,b.Z,c.Z})-std::min({a.Z,b.Z,c.Z})<1e-7)continue;
            maxChordError=std::max(maxChordError,100-std::hypot((a.X+b.X+c.X)/3,(a.Y+b.Y+c.Y)/3));}
    }
    EXPECT_GT(triangles,originalTriangles);
    EXPECT_LE(maxChordError,.0011);RecordProperty("maximum_chord_error_mm",std::to_string(maxChordError));
    // Replacing the derived triangulations must account for the entire change.
    refined.Triangulations3=input.Triangulations3;
    EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(refined),before);
    options.MaxPoints=3;EXPECT_THROW(service->RefineMesh(model,options),std::runtime_error);
    options={};options.IsCancelled=[] {return true;};EXPECT_THROW(service->RefineMesh(model,options),std::runtime_error);
    options={};options.Deflection=0;EXPECT_THROW(service->RefineMesh(model,options),std::invalid_argument);
}
