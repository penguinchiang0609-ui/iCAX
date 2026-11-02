#include "pch.h"
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <GeometryServices/BoundaryLeadService.h>
#include <GeometryData/BRepPersistence.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <Precision.hxx>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace
{
template<class T> const T& SupportRecord(const std::vector<T>& records,std::uint64_t id)
{
    const auto found=std::find_if(records.begin(),records.end(),[&](const auto& record){return record.Id==id;});
    if(found==records.end())throw std::runtime_error("Missing support record");
    return *found;
}
}

TEST(BoundarySupportProjection, PillarSplineBoundaryKeepsItsExactCurvePoint)
{
    const auto path=std::filesystem::path(__FILE__).parent_path()/u8"../../../../../../TestData/FA/汽车/04_Pillar_Reinforcement_900x300_t1p4.step";
    ASSERT_TRUE(std::filesystem::is_regular_file(path));
    iCAX::Services::CServiceProvider provider;
    iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto importer=provider.Resolve<iCAX::CAD::ICadBRepImportService>();
    const auto service=provider.Resolve<iCAX::GeometryServices::IBoundaryLeadService>();
    const auto loaded=importer->Load({.SourcePath=path,.IncludeDisplayMesh=false});
    const auto& model=loaded.Geometry;
    const auto original=iCAX::GeometryData::Persistence::Serialize(model);
    const auto& face=SupportRecord(model.Faces,1);
    const auto& wire=SupportRecord(model.Wires,1);
    const auto& edge=SupportRecord(model.Edges,3);
    const auto occurrence=std::find_if(wire.Coedges.begin(),wire.Coedges.end(),[](const auto& c){return c.EdgeId==3;});
    ASSERT_NE(wire.Coedges.end(),occurrence);
    const auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(SupportRecord(model.Curves3,edge.Curve3Id).Geometry);
    const auto pcurve=iCAX::OpenCascade::BuildOpenCascadeCurve2(SupportRecord(model.Curves2,occurrence->Curve2Id).Geometry);
    const auto surface=iCAX::OpenCascade::BuildOpenCascadeSurface(SupportRecord(model.Surfaces3,face.Surface3Id).Geometry);
    const auto tolerance=std::max({1e-5,edge.Tolerance*5,face.Tolerance*5});
    iCAX::GeometryServices::BoundaryLeadInput query;query.FaceId=face.Id;query.WireId=wire.Id;query.EdgeId=edge.Id;
    query.CoedgeIndex=std::distance(wire.Coedges.begin(),occurrence);query.NativeParameter=.826171875;
    const auto point=curve->Value(query.NativeParameter);const auto seed=pcurve->Value(query.NativeParameter);
    ShapeAnalysis_Surface analysis(surface);const auto uv=analysis.NextValueOfUV(seed,point,1e-9,tolerance);
    GeomAPI_ProjectPointOnSurf projection(point,surface,1e-10);
    ASSERT_GT(projection.NbPoints(),0);
    std::cout<<std::setprecision(17)<<"PILLAR_SUPPORT seedDistance="<<point.Distance(surface->Value(seed.X(),seed.Y()))
        <<" localDistance="<<point.Distance(surface->Value(uv.X(),uv.Y()))<<" minimumDistance="<<projection.LowerDistance()
        <<" tolerance="<<tolerance<<" edgeTolerance="<<edge.Tolerance<<" faceTolerance="<<face.Tolerance<<std::endl;
    ASSERT_GT(projection.LowerDistance(),tolerance);
    ASSERT_LT(projection.LowerDistance(),tolerance+Precision::Confusion());
    const auto frame=service->Frame(model,query);
    EXPECT_DOUBLE_EQ(point.X(),frame.Point.X);EXPECT_DOUBLE_EQ(point.Y(),frame.Point.Y);EXPECT_DOUBLE_EQ(point.Z(),frame.Point.Z);
    const auto batch=service->Frames(model,{query,query});ASSERT_EQ(2u,batch.size());
    EXPECT_DOUBLE_EQ(frame.Normal.X,batch[1].Normal.X);EXPECT_DOUBLE_EQ(frame.Normal.Y,batch[1].Normal.Y);EXPECT_DOUBLE_EQ(frame.Normal.Z,batch[1].Normal.Z);
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(model));
}

TEST(BoundarySupportProjection, KernelResolutionDoesNotAdmitDetachedCurves)
{
    using namespace iCAX::GeometryData;using namespace iCAX::GeometryServices;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<IBoundaryLeadService>();
    BRepModel model;model.Surfaces3.push_back({1,PlaneSurface3{}});
    model.Curves3.push_back({1,Line3{{{0,0,0},{1,0,0}}}});model.Curves2.push_back({1,Line2{{{0,0},{1,0}}}});
    BRepEdge edge;edge.Id=edge.Curve3Id=1;edge.Range={0,10,false};model.Edges.push_back(edge);
    BRepCoedge coedge;coedge.EdgeId=coedge.Curve2Id=1;coedge.Range=edge.Range;
    BRepWire wire;wire.Id=1;wire.Coedges={coedge};model.Wires.push_back(wire);
    BRepFace face;face.Id=face.Surface3Id=1;face.WireIds={1};model.Faces.push_back(face);
    BoundaryLeadInput query;query.FaceId=query.WireId=query.EdgeId=1;query.NativeParameter=4;query.Length=1;
    for(const auto distance:{1e-5+Precision::Confusion()*.5,1e-5+Precision::Confusion()*2,.01}){
        std::get<Line3>(model.Curves3.front().Geometry).Axis.Location.Z=distance;
        if(distance<1e-5+Precision::Confusion()){
            EXPECT_DOUBLE_EQ(distance,service->Frame(model,query).Point.Z);
            EXPECT_NO_THROW(service->Frames(model,{query,query}));
            EXPECT_NO_THROW(service->Build(model,query));
        }else{
            EXPECT_THROW(service->Frame(model,query),std::invalid_argument);
            EXPECT_THROW(service->Frames(model,{query}),std::invalid_argument);
            EXPECT_THROW(service->Build(model,query),std::invalid_argument);
        }
    }
}
