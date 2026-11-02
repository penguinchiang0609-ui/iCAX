#include <gtest/gtest.h>
#include <OpenCascadeResourceImport/CadSnapFeatureService.h>
#include <OpenCascadeResourceImport/CadBRepTransformService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <Geom_BezierSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <Windows.h>
#include <filesystem>
#include <future>
#include <set>
#include <map>
using namespace iCAX::Geometry;
namespace {
class SnapFeatures:public ::testing::Test {
protected:
    iCAX::Services::CServiceProvider provider;
    std::shared_ptr<ICadSnapFeatureService> service;
    void SetUp() override {
        wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
        const auto path=std::filesystem::path(exe).parent_path()/L"OpenCascadeResourceImport.dll";
        static const auto module=LoadLibraryW(path.c_str());ASSERT_NE(nullptr,module);
        iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(provider,{path.string()});
        service=provider.Resolve<ICadSnapFeatureService>();ASSERT_NE(nullptr,service);
    }
    auto Model(const TopoDS_Shape& shape){return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"snap-test","test");}
    auto Count(const SCadSnapFeatures& c,ESnapFeatureKind k){return std::count_if(c.Features.begin(),c.Features.end(),[&](auto& f){return f.BoxKind.empty()&&f.Kind==k;});}
};
}
TEST_F(SnapFeatures, BoxHasTopologyPointsStraightEdgesMidpointsAndPlanesWithoutProduct){
    const auto model=Model(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto c=service->Analyze(model);
    EXPECT_EQ(8,Count(c,ESnapFeatureKind::Endpoint));EXPECT_EQ(12,Count(c,ESnapFeatureKind::Line));EXPECT_EQ(12,Count(c,ESnapFeatureKind::Midpoint));EXPECT_EQ(6,std::count_if(c.Features.begin(),c.Features.end(),[](const auto& f){return f.Kind==ESnapFeatureKind::Plane&&!f.Derived;}));
    std::set<std::string> ids;for(auto& f:c.Features){EXPECT_TRUE(ids.insert(f.Id).second);if(f.BoxKind.empty())EXPECT_FALSE(f.FaceIds.empty());}
    EXPECT_EQ(nullptr,GetModuleHandleW(L"FiveAxisCAM.dll"));
}

TEST_F(SnapFeatures, DerivedPlanesCentresAndAxesComeFromExactCurvesAndSurfaces){
    const auto cylinder=service->Analyze(Model(BRepPrimAPI_MakeCylinder(7,30).Shape()));
    EXPECT_EQ(2,std::count_if(cylinder.Features.begin(),cylinder.Features.end(),[](const auto& f){return f.Id.starts_with("curve-plane:");}));
    const auto wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(gp_Pnt(4,5,6),gp_Dir(1,1,1)),12,7))).Wire();
    const auto ellipse=service->Analyze(Model(BRepBuilderAPI_MakeFace(wire).Shape()));
    bool plane=false,center=false,direction=false;for(const auto& f:ellipse.Features){if(f.Id.starts_with("curve-plane:")){plane=true;EXPECT_NEAR(1/std::sqrt(3.),f.Direction[2],1e-8);}if(f.Id.starts_with("ellipse-center:")){center=true;EXPECT_EQ((std::array<double,3>{4,5,6}),f.Point);}if(f.Id.starts_with("ellipse-major:"))direction=true;}EXPECT_TRUE(plane&&center&&direction);
    const auto cone=service->Analyze(Model(BRepPrimAPI_MakeCone(10,5,20).Shape()));EXPECT_TRUE(std::any_of(cone.Features.begin(),cone.Features.end(),[](const auto& f){return f.Label=="圆锥轴";}));EXPECT_TRUE(std::any_of(cone.Features.begin(),cone.Features.end(),[](const auto& f){return f.Id.starts_with("cone-apex:");}));
    const auto torus=service->Analyze(Model(BRepPrimAPI_MakeTorus(20,5).Shape()));EXPECT_TRUE(std::any_of(torus.Features.begin(),torus.Features.end(),[](const auto& f){return f.Label=="圆环轴";}));
}

TEST_F(SnapFeatures, SharedConstructionBuildsFramesFromAnchorsAndRejectsDegeneracy){
    SCadSnapFeature a{},b{},c{};a.Kind=b.Kind=c.Kind=ESnapFeatureKind::CircleCenter;a.Id="a";b.Id="b";c.Id="c";a.Point={2,3,4};b.Point={12,3,4};c.Point={2,13,4};
    const auto plane=service->Construct(ESnapConstruction::ThreePointPlane,{a,b,c});EXPECT_EQ((std::array<double,3>{0,0,1}),plane.Direction);EXPECT_EQ(a.Point,plane.Point);
    EXPECT_EQ((std::array<double,3>{1,0,0}),service->Construct(ESnapConstruction::PlaneU,{plane}).Direction);EXPECT_EQ((std::array<double,3>{0,1,0}),service->Construct(ESnapConstruction::PlaneV,{plane}).Direction);
    EXPECT_EQ(a.Point,service->Construct(ESnapConstruction::PlaneCenter,{plane}).Point);EXPECT_EQ((std::array<double,3>{1,0,0}),service->Construct(ESnapConstruction::TwoPointDirection,{a,b}).Direction);
    EXPECT_EQ(plane.Direction,service->Construct(ESnapConstruction::PlaneNormal,{plane}).Direction);
    EXPECT_THROW(service->Construct(ESnapConstruction::PlaneNormal,{a}),std::invalid_argument);
    EXPECT_THROW(service->Construct(ESnapConstruction::TwoPointDirection,{a,a}),std::invalid_argument);c.Point={22,3,4};
    EXPECT_THROW(service->Construct(ESnapConstruction::ThreePointPlane,{a,b,c}),std::invalid_argument);
    EXPECT_THROW(service->Construct(ESnapConstruction::PlaneU,{a}),std::invalid_argument);
}

TEST_F(SnapFeatures, CylindricalFaceCentreUsesTrimmedSurfaceRatherThanAxisOrigin){
    const gp_Ax2 frame(gp_Pnt(17,-23,41),gp_Dir(1,2,3));
    const auto catalog=service->Analyze(Model(BRepPrimAPI_MakeCylinder(frame,7,30).Shape()));
    int found=0;
    for(const auto& f:catalog.Features)if(f.Id.starts_with("cylinder-center:")){
        ++found;EXPECT_EQ(ESnapFeatureKind::Center,f.Kind);EXPECT_TRUE(f.Derived);ASSERT_EQ(1u,f.FaceIds.size());
        const auto expected=frame.Location().Translated(gp_Vec(frame.Direction())*15);
        EXPECT_NEAR(expected.X(),f.Point[0],1e-7);EXPECT_NEAR(expected.Y(),f.Point[1],1e-7);EXPECT_NEAR(expected.Z(),f.Point[2],1e-7);
        EXPECT_EQ("圆柱面轴心",f.Label);
    }
    EXPECT_EQ(1,found);
}

TEST_F(SnapFeatures, SpatialContourIsNotAdvertisedAsPlanar){
    TColgp_Array2OfPnt poles(1,2,1,2);poles(1,1)=gp_Pnt(0,0,0);poles(1,2)=gp_Pnt(10,0,0);poles(2,1)=gp_Pnt(0,10,0);poles(2,2)=gp_Pnt(10,10,5);
    const auto result=service->Analyze(Model(BRepBuilderAPI_MakeFace(new Geom_BezierSurface(poles),1e-7).Shape()));EXPECT_EQ(0,Count(result,ESnapFeatureKind::Plane));
}
TEST_F(SnapFeatures, SphereCentreIsAnalyticAndNotBoundingBoxOrFaceSample){
    const auto model=Model(BRepPrimAPI_MakeSphere(gp_Pnt(17,-31,43),8).Shape());const auto c=service->Analyze(model);
    ASSERT_EQ(1,Count(c,ESnapFeatureKind::SphereCenter));
    for(auto& f:c.Features)if(f.Kind==ESnapFeatureKind::SphereCenter){EXPECT_NEAR(17,f.Point[0],1e-9);EXPECT_NEAR(-31,f.Point[1],1e-9);EXPECT_NEAR(43,f.Point[2],1e-9);EXPECT_NEAR(8,f.Radius,1e-9);}
}
TEST_F(SnapFeatures, CylinderCircleCentresAndAxisCarryExactSources){
    const auto model=Model(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(4,5,6),gp_Dir(1,1,1)),7,30).Shape());const auto c=service->Analyze(model);
    EXPECT_EQ(2,Count(c,ESnapFeatureKind::CircleCenter));EXPECT_EQ(1,Count(c,ESnapFeatureKind::Axis));
    for(auto& f:c.Features)if(f.Kind==ESnapFeatureKind::CircleCenter||f.Kind==ESnapFeatureKind::Axis){EXPECT_NEAR(7,f.Radius,1e-8);EXPECT_NEAR(1/std::sqrt(3.),f.Direction[0],1e-8);EXPECT_FALSE(f.FaceIds.empty());}
}
TEST_F(SnapFeatures, RepeatedConcurrentAnalysisIsDeterministicAndLeavesInputUnchanged){
    const auto model=Model(BRepPrimAPI_MakeBox(10,20,30).Shape());const auto count=model.Vertices.size();
    auto pending=std::async(std::launch::async,[&]{return service->Analyze(model);});const auto a=service->Analyze(model),b=pending.get();
    ASSERT_EQ(a.Features.size(),b.Features.size());for(size_t i=0;i<a.Features.size();++i){EXPECT_EQ(a.Features[i].Id,b.Features[i].Id);EXPECT_EQ(a.Features[i].Point,b.Features[i].Point);}EXPECT_EQ(count,model.Vertices.size());
}

TEST_F(SnapFeatures, RigidFrameKeepsExactSolidsAndAnalyticSnapFeatures){
    const auto transform=provider.Resolve<ICadBRepTransformService>();ASSERT_NE(nullptr,transform);
    const std::array<double,16> matrix{0,-1,0,17, 0,0,-1,-23, 1,0,0,41, 0,0,0,1};
    for(const auto& shape:{BRepPrimAPI_MakeBox(40,60,20).Shape(),BRepPrimAPI_MakeCylinder(7,30).Shape(),BRepPrimAPI_MakeSphere(gp_Pnt(4,5,6),8).Shape()}){
        const auto source=Model(shape),result=transform->Transform(source,matrix);
        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(result);ASSERT_TRUE(rebuilt.bOK);EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
        GProp_GProps a,b;BRepGProp::VolumeProperties(shape,a);BRepGProp::VolumeProperties(rebuilt.Shape,b);
        EXPECT_NEAR(a.Mass(),b.Mass(),1e-6);const auto p=a.CentreOfMass(),q=b.CentreOfMass();
        EXPECT_NEAR(17-p.Y(),q.X(),1e-7);EXPECT_NEAR(-23-p.Z(),q.Y(),1e-7);EXPECT_NEAR(41+p.X(),q.Z(),1e-7);
        const auto before=service->Analyze(source),after=service->Analyze(result);ASSERT_EQ(before.Features.size(),after.Features.size());
        for(size_t i=0;i<before.Features.size();++i){const auto& f=before.Features[i];const auto& g=after.Features[i];EXPECT_EQ(f.Id,g.Id);if(!f.BoxKind.empty())continue;EXPECT_NEAR(17-f.Point[1],g.Point[0],1e-7);EXPECT_NEAR(-23-f.Point[2],g.Point[1],1e-7);EXPECT_NEAR(41+f.Point[0],g.Point[2],1e-7);EXPECT_NEAR(f.Radius,g.Radius,1e-7);}
        const auto unchanged=service->Analyze(source);for(size_t i=0;i<before.Features.size();++i)EXPECT_EQ(before.Features[i].Point,unchanged.Features[i].Point);
        ASSERT_EQ(source.Faces.size(),result.Faces.size());for(size_t i=0;i<source.Faces.size();++i){EXPECT_EQ(source.Faces[i].Id,result.Faces[i].Id);EXPECT_EQ(source.Faces[i].WireIds,result.Faces[i].WireIds);}
    }
}

TEST_F(SnapFeatures, RigidFrameConjugatesOccurrenceLocation){
    const auto transform=provider.Resolve<ICadBRepTransformService>();ASSERT_NE(nullptr,transform);
    auto source=Model(BRepPrimAPI_MakeBox(4,6,2).Shape());ASSERT_FALSE(source.RootShapes.empty());
    source.RootShapes[0].Location.Matrix.Values[0][3]=12;source.RootShapes[0].Location.Matrix.Values[2][3]=-9;
    const std::array<double,16> matrix{0,-1,0,17, 0,0,-1,-23, 1,0,0,41, 0,0,0,1};
    const auto before=iCAX::OpenCascade::BuildOpenCascadeShape(source),after=iCAX::OpenCascade::BuildOpenCascadeShape(transform->Transform(source,matrix));ASSERT_TRUE(before.bOK);ASSERT_TRUE(after.bOK);
    GProp_GProps a,b;BRepGProp::VolumeProperties(before.Shape,a);BRepGProp::VolumeProperties(after.Shape,b);const auto p=a.CentreOfMass(),q=b.CentreOfMass();
    EXPECT_NEAR(a.Mass(),b.Mass(),1e-7);EXPECT_NEAR(17-p.Y(),q.X(),1e-7);EXPECT_NEAR(-23-p.Z(),q.Y(),1e-7);EXPECT_NEAR(41+p.X(),q.Z(),1e-7);
}

TEST_F(SnapFeatures, RigidFrameRejectsScaleReflectionAndNonFiniteInput){
    const auto transform=provider.Resolve<ICadBRepTransformService>();ASSERT_NE(nullptr,transform);const auto model=Model(BRepPrimAPI_MakeBox(4,6,2).Shape());
    const std::array<double,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(double x:{2.,-1.,std::numeric_limits<double>::quiet_NaN()}){auto m=identity;m[0]=x;
    EXPECT_THROW(transform->Transform(model,m),std::invalid_argument);}
}

TEST_F(SnapFeatures, OrientedBoxSuppliesFiniteFacesEdgesCornersAndCentres){
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(2,3,4),gp_Dir(1,2,3)),.63);
    const auto model=Model(BRepBuilderAPI_Transform(BRepPrimAPI_MakeBox(40,60,20).Shape(),rotation,true).Shape());
    const auto catalog=service->Analyze(model);std::map<std::string,double> volumes;
    EXPECT_FALSE(std::any_of(catalog.Features.begin(),catalog.Features.end(),[](const auto& f){return f.BoxKind=="AABB";}));
    for(const std::string kind:{"OBB"}){
        int planes=0,edges=0,corners=0,midpoints=0,centres=0;double volume=1;
        for(const auto& f:catalog.Features)if(f.BoxKind==kind){
            EXPECT_TRUE(f.Derived);EXPECT_TRUE(f.FaceIds.empty());EXPECT_TRUE(f.EdgeIds.empty());EXPECT_EQ(0u,f.EdgeId);
            for(double x:f.Point)EXPECT_TRUE(std::isfinite(x));
            planes+=f.Kind==ESnapFeatureKind::Plane;edges+=f.Kind==ESnapFeatureKind::Line;corners+=f.Kind==ESnapFeatureKind::Endpoint;midpoints+=f.Kind==ESnapFeatureKind::Midpoint;centres+=f.Kind==ESnapFeatureKind::Center;
            if(f.Kind==ESnapFeatureKind::Line){ASSERT_EQ(2u,f.EdgeSamples.size());double d=0;for(int j=0;j<3;++j)d+=(f.EdgeSamples[0][j]-f.EdgeSamples[1][j])*(f.EdgeSamples[0][j]-f.EdgeSamples[1][j]);EXPECT_GT(d,1.);}
            if(f.Kind!=ESnapFeatureKind::Plane)continue;ASSERT_EQ(4u,f.PlaneCorners.size());
            for(const auto& p:f.PlaneCorners){double distance=0;for(int j=0;j<3;++j)distance+=(p[j]-f.Point[j])*f.Direction[j];EXPECT_NEAR(0,distance,1e-7);}
            for(const auto& p:model.Vertices){const double distance=(p.Position.X-f.Point[0])*f.Direction[0]+(p.Position.Y-f.Point[1])*f.Direction[1]+(p.Position.Z-f.Point[2])*f.Direction[2];EXPECT_LE(distance,1e-6);}
            if(f.Id.ends_with(":1")){const auto other=std::find_if(catalog.Features.begin(),catalog.Features.end(),[&](const auto& g){return g.Id==f.Id.substr(0,f.Id.size()-1)+"-1";});ASSERT_NE(catalog.Features.end(),other);
                double size=0;for(int j=0;j<3;++j)size+=(f.Point[j]-other->Point[j])*f.Direction[j];volume*=size;}
            EXPECT_EQ(f.Direction,service->Construct(ESnapConstruction::PlaneNormal,{f}).Direction);
        }
        EXPECT_EQ(6,planes);EXPECT_EQ(12,edges);EXPECT_EQ(8,corners);EXPECT_EQ(12,midpoints);EXPECT_EQ(7,centres);volumes[kind]=volume;
    }
    EXPECT_NEAR(40*60*20,volumes["OBB"],.01);
}

TEST_F(SnapFeatures, BoundsIncludeCurvedExtremaAndFlatInputHasNoInvalidDirections){
    const auto sphere=service->Analyze(Model(BRepPrimAPI_MakeSphere(gp_Pnt(17,-31,43),8).Shape()));
    for(const auto& f:sphere.Features)if(f.BoxKind=="OBB"&&f.Id.find("face-center:")!=std::string::npos){const auto& p=f.Point;EXPECT_NEAR(8,std::hypot(p[0]-17,p[1]+31,p[2]-43),1e-6);}
    const auto flat=service->Analyze(Model(BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),0,40,0,20).Shape()));
    for(const auto& f:flat.Features)if(!f.BoxKind.empty()){
        for(double x:f.Point)EXPECT_TRUE(std::isfinite(x));
        if(f.Kind==ESnapFeatureKind::Plane||f.Kind==ESnapFeatureKind::Line){double n=0;for(double x:f.Direction)n+=x*x;EXPECT_NEAR(1,n,1e-7);}
    }
}
