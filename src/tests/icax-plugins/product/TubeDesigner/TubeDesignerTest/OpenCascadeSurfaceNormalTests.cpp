#include "pch.h"
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {
using iCAX::GeometryData::BRepModel;
using iCAX::GeometryData::Point3;
using iCAX::GeometryData::Vector3;
using iCAX::GeometryData::Triangulation3;

gp_Vec vector(const Vector3& value) { return {value.X,value.Y,value.Z}; }
gp_Pnt point(const Point3& value) { return {value.X,value.Y,value.Z}; }

void expectUnitNormals(const Triangulation3& mesh) {
    ASSERT_FALSE(mesh.Vertices.empty());ASSERT_EQ(mesh.Normals.size(),mesh.Vertices.size());
    for(const auto& normal:mesh.Normals) {
        EXPECT_TRUE(std::isfinite(normal.X)&&std::isfinite(normal.Y)&&std::isfinite(normal.Z));
        EXPECT_NEAR(vector(normal).Magnitude(),1.,1.e-6);
    }
    for(const auto& triangle:mesh.Triangles) {
        const auto a=point(mesh.Vertices.at(triangle[0])),b=point(mesh.Vertices.at(triangle[1])),c=point(mesh.Vertices.at(triangle[2]));
        const auto facet=gp_Vec(a,b).Crossed(gp_Vec(a,c));
        if(facet.SquareMagnitude()<1.e-16)continue;
        const auto outward=vector(mesh.Normals.at(triangle[0]))+vector(mesh.Normals.at(triangle[1]))+vector(mesh.Normals.at(triangle[2]));
        EXPECT_GT(facet.Dot(outward),0.)<<"Triangle winding must follow the oriented surface normals";
    }
}

void expectAnalyticNormals(const BRepModel& model) {
    ASSERT_FALSE(model.Triangulations3.empty());
    for(const auto& record:model.Triangulations3) {
        const auto face=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& value){return value.Triangulation3Id==record.Id;});
        ASSERT_NE(face,model.Faces.end());
        const auto surface=std::find_if(model.Surfaces3.begin(),model.Surfaces3.end(),[&](const auto& value){return value.Id==face->Surface3Id;});
        ASSERT_NE(surface,model.Surfaces3.end());
        const auto sign=face->Orientation==iCAX::GeometryData::ETopologyOrientation::Reversed?-1.:1.;
        const auto& mesh=record.Geometry;expectUnitNormals(mesh);
        ASSERT_EQ(mesh.Normals.size(),mesh.Vertices.size());
        for(std::size_t i=0;i<mesh.Vertices.size();++i) {
            gp_Vec expected;
            if(const auto* cylinder=std::get_if<iCAX::GeometryData::CylindricalSurface3>(&surface->Geometry)) {
                const auto& center=cylinder->Placement.Location;const auto& axis=cylinder->Placement.ZDirection;
                const gp_Vec z(axis.X,axis.Y,axis.Z),offset(gp_Pnt(center.X,center.Y,center.Z),point(mesh.Vertices[i]));
                expected=offset-z.Multiplied(offset.Dot(z));expected.Normalize();
            } else if(const auto* plane=std::get_if<iCAX::GeometryData::PlaneSurface3>(&surface->Geometry)) {
                const auto& z=plane->Placement.ZDirection;expected=gp_Vec(z.X,z.Y,z.Z);
            } else { FAIL()<<"Regression shape must retain its exact plane/cylinder surfaces"; }
            expected.Multiply(sign);
            EXPECT_LT((vector(mesh.Normals[i])-expected).Magnitude(),1.e-6)<<"Face "<<face->Id<<" vertex "<<i;
        }
    }
}

TopoDS_Shape perforatedTube() {
    const auto hollow=BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(10.,1000.).Shape(),BRepPrimAPI_MakeCylinder(8.,1000.).Shape()).Shape();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-20.,0.,250.),gp_Dir(1.,0.,0.)),3.,40.).Shape();
    return BRepAlgoAPI_Cut(hollow,hole).Shape();
}
}

TEST(OpenCascadeSurfaceNormals, LongCylinderAndBooleanHoleUseExactOrientedSurfaceNormals) {
    for(const auto& shape:{BRepPrimAPI_MakeCylinder(10.,1000.).Shape(),perforatedTube()}) {
        const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"Surface normal regression","surface-normal-test",.001);
        expectAnalyticNormals(model);
        const auto meshes=iCAX::OpenCascade::ConvertOpenCascadeShapesToTriangleMeshes({{shape,"Display normal regression","display-normal-test"}},.001,1);
        ASSERT_EQ(meshes.size(),1);expectUnitNormals(meshes[0].Mesh);
    }
}

TEST(OpenCascadeSurfaceNormals, LocatedDisplayInstancesTransformNormalsWithTheirVertices) {
    const auto source=perforatedTube();gp_Trsf location;
    location.SetRotation(gp_Ax1(gp_Pnt(0.,0.,0.),gp_Dir(1.,2.,3.)),.73);
    location.SetTranslationPart(gp_Vec(15.,-24.,71.));
    const auto located=source.Located(TopLoc_Location(location));
    const auto meshes=iCAX::OpenCascade::ConvertOpenCascadeShapesToTriangleMeshes({{source,"Source","source"},{located,"Located","located"}},.001,1);
    ASSERT_EQ(meshes.size(),2);ASSERT_EQ(meshes[0].Mesh.Vertices.size(),meshes[1].Mesh.Vertices.size());
    ASSERT_EQ(meshes[0].Mesh.Normals.size(),meshes[1].Mesh.Normals.size());
    expectUnitNormals(meshes[0].Mesh);expectUnitNormals(meshes[1].Mesh);
    for(std::size_t i=0;i<meshes[0].Mesh.Vertices.size();++i) {
        EXPECT_LT(point(meshes[0].Mesh.Vertices[i]).Transformed(location).Distance(point(meshes[1].Mesh.Vertices[i])),1.e-9);
        EXPECT_LT((vector(meshes[0].Mesh.Normals[i]).Transformed(location)-vector(meshes[1].Mesh.Normals[i])).Magnitude(),1.e-9);
    }
    expectAnalyticNormals(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(located,"Located BRep","located-brep",.001));
}

TEST(OpenCascadeSurfaceNormals, ReversedFacesRetainSeparateOutwardNormalsAndMatchingTriangleWinding) {
    const auto shape=BRepPrimAPI_MakeCylinder(10.,1000.).Shape().Reversed();
    expectAnalyticNormals(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"Reversed","reversed",.001));
    const auto meshes=iCAX::OpenCascade::ConvertOpenCascadeShapesToTriangleMeshes({{shape,"Reversed display","reversed-display"}},.001,1);
    ASSERT_EQ(meshes.size(),1);expectUnitNormals(meshes[0].Mesh);
    bool hardEdge=false;
    const auto& mesh=meshes[0].Mesh;
    for(std::size_t a=0;a<mesh.Vertices.size()&&!hardEdge;++a)for(std::size_t b=a+1;b<mesh.Vertices.size();++b)
        if(point(mesh.Vertices[a]).Distance(point(mesh.Vertices[b]))<1.e-9&&vector(mesh.Normals[a]).Dot(vector(mesh.Normals[b]))<.5) {hardEdge=true;break;}
    EXPECT_TRUE(hardEdge)<<"Coincident cap/cylinder vertices must preserve each face normal instead of being welded";
}

TEST(OpenCascadeSurfaceNormals, RecomputingSurfaceNormalsLeavesSharedTriangulationCacheUntouched) {
    const auto shape=BRepPrimAPI_MakeCylinder(10.,1000.).Shape();BRepMesh_IncrementalMesh mesher(shape,.01);
    std::vector<Handle(Poly_Triangulation)> caches;
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next()) {
        TopLoc_Location location;const auto triangles=BRep_Tool::Triangulation(TopoDS::Face(faces.Current()),location);
        ASSERT_FALSE(triangles.IsNull());triangles->AddNormals();
        for(int i=1;i<=triangles->NbNodes();++i)triangles->SetNormal(i,gp_Dir(1.,0.,0.));
        caches.push_back(triangles);
    }
    expectAnalyticNormals(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"Cached normals","cached-normals",.001));
    for(const auto& triangles:caches)for(int i=1;i<=triangles->NbNodes();++i) {
        const auto normal=triangles->Normal(i);EXPECT_DOUBLE_EQ(normal.X(),1.);EXPECT_DOUBLE_EQ(normal.Y(),0.);EXPECT_DOUBLE_EQ(normal.Z(),0.);
    }
}
