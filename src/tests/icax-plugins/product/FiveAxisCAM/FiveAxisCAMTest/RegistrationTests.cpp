#include "pch.h"
#include <OpenCascadeResourceImport/RigidRegistrationService.h>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <OpenCascadeResourceImport/CurveEditingService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <GeometryServices/CurveOffset3DService.h>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <cmath>

namespace
{
    using namespace iCAX::Geometry;
    using Request=SRigidRegistrationRequest;
    using P=RegistrationPoint;
    auto Service()
    {
        iCAX::Services::CServiceProvider provider;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
        return provider.Resolve<IRigidRegistrationService>();
    }
    P Apply(const SRigidRegistrationResult& result,P p)
    {
        P r{};for(int i=0;i<3;++i){r[i]=result.Transform[4*i+3];for(int j=0;j<3;++j)r[i]+=result.Transform[4*i+j]*p[j];}return r;
    }
    void Near(P a,P b,double e=1e-8) { for(int i=0;i<3;++i)EXPECT_NEAR(a[i],b[i],e); }
    Request Paired()
    {
        Request q;q.Kind=Request::Method::PairedPoints;
        q.ModelPoints={{0,0,0},{10,0,0},{0,20,0},{0,0,30},{-5,17,9}};
        for(auto p:q.ModelPoints)q.MeasuredPoints.push_back({100-p[1],200+p[0],300+p[2]});
        return q;
    }
}
TEST(RigidRegistration, PointLineAndPlaneFramesHaveExplicitRightHandedOrientation)
{
    const auto service=Service();Request q;q.Origin={10,20,30};q.XReference={10,30,30};q.PlaneReference={0,20,30};
    const auto point=service->Solve(q);ASSERT_TRUE(point.Qualified);
    Near(Apply(point,{10,20,30}),{0,0,0});Near(Apply(point,{10,21,30}),{1,0,0});Near(Apply(point,{9,20,30}),{0,1,0});
    q.Kind=Request::Method::Lines;q.XReference={0,1,0};q.PlaneReference={-1,0,0};
    const auto line=service->Solve(q);EXPECT_EQ(point.Transform,line.Transform);
    q.Kind=Request::Method::Plane;q.PlaneReference={0,0,1};
    EXPECT_EQ(point.Transform,service->Solve(q).Transform);
    q.PlaneReference={0,0,-1};const auto flipped=service->Solve(q);
    Near(Apply(flipped,{9,20,30}),{0,-1,0});Near(Apply(flipped,{10,20,31}),{0,0,-1});
}
TEST(RigidRegistration, WeightedSvdRecoversRigidPoseWithoutScaleAndSupportsPlanarThreePoints)
{
    auto q=Paired();q.Weights={1,2,4,3,2};const auto result=Service()->Solve(q);
    ASSERT_TRUE(result.Qualified);EXPECT_NEAR(result.RmsMm,0,1e-10);
    for(size_t i=0;i<q.ModelPoints.size();++i)Near(Apply(result,q.ModelPoints[i]),q.MeasuredPoints[i]);
    q.ModelPoints.resize(3);q.MeasuredPoints.resize(3);q.Weights.clear();
    EXPECT_TRUE(Service()->Solve(q).Qualified);
    // Double precision large offset should not turn centroid fitting into scaling.
    for(auto& p:q.ModelPoints)for(auto& v:p)v+=1e7;
    for(size_t i=0;i<3;++i){auto p=q.ModelPoints[i];q.MeasuredPoints[i]={100-p[1],200+p[0],300+p[2]};}
    EXPECT_LT(Service()->Solve(q).MaximumResidualMm,1e-6);
}
TEST(RigidRegistration, NoisyPointsReportAllResidualsAndRejectExceededLimit)
{
    auto q=Paired();q.MeasuredPoints[4][0]+=0.5;q.MaximumResidualMm=0.05;
    const auto result=Service()->Solve(q);EXPECT_FALSE(result.Qualified);EXPECT_EQ(q.ModelPoints.size(),result.Residuals.size());
    EXPECT_GT(result.MaximumResidualMm,q.MaximumResidualMm);EXPECT_GT(result.RmsMm,0.01);
    q.MaximumResidualMm=1;EXPECT_TRUE(Service()->Solve(q).Qualified);
}
TEST(RigidRegistration, ReflectionScaleAndPoorSpreadCannotMasqueradeAsAccuratePlacement)
{
    auto q=Paired();for(size_t i=0;i<q.ModelPoints.size();++i){auto p=q.ModelPoints[i];q.MeasuredPoints[i]={-p[0],p[1],p[2]};}
    auto result=Service()->Solve(q);EXPECT_TRUE(result.ReflectionCorrected);EXPECT_FALSE(result.Qualified);
    const auto& m=result.Transform;
    EXPECT_NEAR(1,m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]),1e-10);
    for(size_t i=0;i<q.ModelPoints.size();++i){auto p=q.ModelPoints[i];q.MeasuredPoints[i]={2*p[0],2*p[1],2*p[2]};}
    EXPECT_FALSE(Service()->Solve(q).Qualified);
    q.ModelPoints={{0,0,0},{100,0,0},{200,0.00001,0}};q.MeasuredPoints=q.ModelPoints;
    EXPECT_THROW(Service()->Solve(q),std::invalid_argument);
    q.ModelPoints={{0,0,0},{1e-7,0,0},{0,1e-7,0}};q.MeasuredPoints=q.ModelPoints;
    EXPECT_THROW(Service()->Solve(q),std::invalid_argument);
}
TEST(RigidRegistration, DegenerateDirectionsAndInvalidWeightsOrFramesAreRejected)
{
    auto service=Service();Request q;q.XReference={1,0,0};q.PlaneReference={2,0,0};
    EXPECT_THROW(service->Solve(q),std::invalid_argument);
    q.Kind=Request::Method::Plane;q.PlaneReference={1,0,0};EXPECT_THROW(service->Solve(q),std::invalid_argument);
    q=Paired();q.Weights={1,1,-1,1,1};EXPECT_THROW(service->Solve(q),std::invalid_argument);
    q.Weights.clear();q.ModelPoints[0][0]=std::numeric_limits<double>::quiet_NaN();EXPECT_THROW(service->Solve(q),std::invalid_argument);
}
TEST(CadReference, ProjectsOntoExactCurvedSurfaceInsteadOfDisplayTriangle)
{
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeSphere(30).Shape(),"sphere","test",0.5);
    SCadReferenceQuery q;q.FaceID=model.Faces.front().Id;q.NearPoint={29.8,0,0};
    const auto result=service->Resolve(model,q);Near(result.Point,{30,0,0});EXPECT_NEAR(0.2,result.DistanceMm,1e-7);
    q.Mode=SCadReferenceQuery::Kind::Plane;EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
    q.Mode=SCadReferenceQuery::Kind::SurfacePoint;q.FaceID=99999;EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
}
TEST(CadReference, ExactPlaneLineVertexAndArclengthMidpointStayInSourceCoordinates)
{
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeBox(10,20,30).Shape(),"box","test",0.1);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);SCadReferenceQuery q;
    for(const auto& [id,face]:built.Faces)
    {
        BRepAdaptor_Surface s(face);
        if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(gp_Pnt(5,10,30))<1e-7){q.FaceID=id;break;}
    }
    ASSERT_NE(0u,q.FaceID);q.Mode=SCadReferenceQuery::Kind::Plane;q.NearPoint={5,10,30.05};
    auto result=service->Resolve(model,q);Near(result.Point,{5,10,30});Near(result.Direction,{0,0,1});
    q.Mode=SCadReferenceQuery::Kind::Line;q.NearPoint={4,0.1,30};
    result=service->Resolve(model,q);Near(result.Point,{5,0,30});EXPECT_NEAR(1,std::abs(result.Direction[0]),1e-8);EXPECT_NE(0u,result.EdgeID);
    q.Mode=SCadReferenceQuery::Kind::EdgeMidpoint;Near(service->Resolve(model,q).Point,{5,0,30});
    q.Mode=SCadReferenceQuery::Kind::Vertex;q.NearPoint={0.1,0.1,30};Near(service->Resolve(model,q).Point,{0,0,30});
    q.Mode=SCadReferenceQuery::Kind::Line;q.NearPoint={0,0,30};EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
    for(const double gap:{0.,.00001,.00025,.002}) {
        q.NearPoint={10-gap,10.0016,30};
        const auto line=service->Resolve(model,q);
        Near(line.Point,{10,10,30});
        EXPECT_NEAR(gap,line.DistanceMm,1e-8);
    }
    q.NearPoint={5,10,30};EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
}
TEST(CadReference, CircleCenterUsesExactCurveAndRejectsNonCircularEdges)
{
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeCylinder(10,30).Shape(),"cylinder","test",0.1);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);SCadReferenceQuery q;
    for(const auto& [id,face]:built.Faces){BRepAdaptor_Surface s(face);if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(gp_Pnt(0,0,30))<1e-7){q.FaceID=id;break;}}
    ASSERT_NE(0u,q.FaceID);q.Mode=SCadReferenceQuery::Kind::CircleCenter;q.NearPoint={10,0,30};
    Near(service->Resolve(model,q).Point,{0,0,30});
    q.Mode=SCadReferenceQuery::Kind::Line;EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
}

TEST(CadReference, ScopedFaceRebuildRetainsInnerTrimsAndCircularSeams)
{
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    const auto ring=BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(20,30).Shape(),BRepPrimAPI_MakeCylinder(10,30).Shape()).Shape();
    const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(ring,"ring","test",0.1);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);SCadReferenceQuery q;
    for(const auto& [id,face]:built.Faces){BRepAdaptor_Surface s(face);if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(gp_Pnt(15,0,30))<1e-7){q.FaceID=id;break;}}
    ASSERT_NE(0u,q.FaceID);q.MaximumDistanceMm=1.;q.NearPoint={15,0,30.2};
    Near(service->Resolve(model,q).Point,{15,0,30});
    q.NearPoint={5,0,30};EXPECT_THROW(service->Resolve(model,q),std::invalid_argument);
    q.NearPoint={10.1,0,30};q.Mode=SCadReferenceQuery::Kind::CircleCenter;
    Near(service->Resolve(model,q).Point,{0,0,30});
    for(const auto& [id,face]:built.Faces) {
        BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Cylinder||std::abs(s.Cylinder().Radius()-10)>1e-7)continue;
        q.FaceID=id;q.Mode=SCadReferenceQuery::Kind::SurfacePoint;q.NearPoint={9.8,0,15};
        Near(service->Resolve(model,q).Point,{10,0,15});
    }
}

TEST(CadCurveReference, ExactCircleAndRationalSplineKeepNativeParameters)
{
    using namespace iCAX::GeometryData;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    Circle3 circle;circle.Radius=4;circle.Placement.Location={0,0,10};
    CompositeCurve3 path;path.Segments.push_back({circle,{0,2*std::acos(-1.),true}});path.Closed=true;
    SCurveReferenceQuery q;q.NearPoint={0,4.2,10};
    const auto result=service->ResolveCurve(path,q);Near(result.Point,{0,4,10});
    Near(service->EvaluateCurve(path,0,result.NativeParameter),result.Point);
    EXPECT_NEAR(std::acos(-1.)/2,result.NativeParameter,1e-8);EXPECT_NEAR(.2,result.DistanceMm,1e-8);
    NURBS3 spline;spline.Degree=2;spline.Poles={{0,0,0},{5,0,10},{10,0,0}};
    spline.Weights={1,2,1};spline.Knots={0,1};spline.Multiplicities={3,3};
    path.Segments={{spline,{0,1,false}}};path.Closed=false;q.NearPoint={5,.1,20./3};
    const auto rational=service->ResolveCurve(path,q);Near(rational.Point,{5,0,20./3});EXPECT_NEAR(.5,rational.NativeParameter,1e-8);
    Near(service->EvaluateCurve(path,0,.5),{5,0,20./3});
}
TEST(CadCurveReference, SpatialPolylineAndEndpointsRespectTrimmedIntervals)
{
    using namespace iCAX::GeometryData;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    Polyline3 polyline;polyline.Points={{0,0,0},{10,0,0},{10,10,10},{0,10,20}};
    CompositeCurve3 path;path.Segments={{polyline,{.5,2.5,true}}};
    SCurveReferenceQuery q;q.NearPoint={10.2,5,5};const auto result=service->ResolveCurve(path,q);
    Near(result.Point,{10,5,5});EXPECT_NEAR(1.5,result.NativeParameter,1e-8);
    Near(service->EvaluateCurve(path,0,1.5),{10,5,5});
    EXPECT_THROW(service->EvaluateCurve(path,0,0.),std::invalid_argument);
    EXPECT_THROW(service->EvaluateCurve(path,1,1.5),std::invalid_argument);
    EXPECT_THROW(service->EvaluateCurve(path,0,std::numeric_limits<double>::quiet_NaN()),std::invalid_argument);
    q.Mode=SCurveReferenceQuery::Kind::Endpoint;q.NearPoint={5.1,0,0};
    const auto endpoint=service->ResolveCurve(path,q);Near(endpoint.Point,{5,0,0});EXPECT_DOUBLE_EQ(.5,endpoint.NativeParameter);
    q.Mode=SCurveReferenceQuery::Kind::ClosestPoint;q.NearPoint={0,0,0};EXPECT_THROW(service->ResolveCurve(path,q),std::invalid_argument);
}
TEST(CadCurveReference, SplineEndpointRoundoffKeepsExactNativeBoundsAcrossEvaluationAndEditing)
{
    using namespace iCAX::GeometryData;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto reference=provider.Resolve<ICadReferenceService>();const auto editing=provider.Resolve<ICurveEditingService>();
    // The first two domains come directly from the failing real IGES edges;
    // scaled domains ensure this never becomes a fixed geometric tolerance.
    for(const auto bounds:std::vector<std::pair<double,double>>{{.01456309872,.8400039378},{-.006400045918,-4.4408920989999998e-16},{0,1e-9},{1000,1001}}){
        BSpline3 spline;spline.Degree=1;spline.Poles={{0,0,0},{10,0,0}};
        spline.Knots={bounds.first,bounds.second};spline.Multiplicities={2,2};
        const auto first=std::nextafter(bounds.first,-std::numeric_limits<double>::infinity());
        const auto last=std::nextafter(bounds.second,std::numeric_limits<double>::infinity()),middle=(first+last)/2;
        for(bool reversed:{false,true}){
            CompositeCurve3 path;path.Segments={{spline,{first,last,reversed}}};
            Near(reference->EvaluateCurve(path,0,first),{0,0,0});Near(reference->EvaluateCurve(path,0,last),{10,0,0});
            SCurveReferenceQuery q;q.Mode=SCurveReferenceQuery::Kind::Endpoint;q.NearPoint={10,.01,0};
            EXPECT_EQ(last,reference->ResolveCurve(path,q).NativeParameter);
            q.Mode=SCurveReferenceQuery::Kind::ClosestPoint;q.NearPoint={5,.01,0};Near(reference->ResolveCurve(path,q).Point,{5,0,0});
            const auto samples=editing->Sample(path,.01,1.);ASSERT_GE(samples.Points.size(),2u);
            EXPECT_EQ(reversed?last:first,samples.Points.front().NativeParameter);EXPECT_EQ(reversed?first:last,samples.Points.back().NativeParameter);
            const auto pieces=editing->Split(path,{{0,middle}});ASSERT_EQ(2u,pieces.size());
            EXPECT_EQ(reversed?last:first,reversed?pieces.front().Curve.Segments.front().Range.Last:pieces.front().Curve.Segments.front().Range.First);
            EXPECT_EQ(reversed?first:last,reversed?pieces.back().Curve.Segments.back().Range.First:pieces.back().Curve.Segments.back().Range.Last);
            path.Closed=true;path.Segments.push_back({spline,{first,last,!reversed}});
            EXPECT_NEAR(20,editing->TraceClosed(path,{0,reversed?last:first},12,false).PerimeterMm,1e-7);
        }
        CompositeCurve3 invalid;invalid.Segments={{spline,{first,last,false}}};
        invalid.Segments[0].Range.Last+=1e-8*(bounds.second-bounds.first);
        EXPECT_THROW(reference->EvaluateCurve(invalid,0,first),std::invalid_argument);
        EXPECT_THROW(reference->ResolveCurve(invalid,{}),std::invalid_argument);
        EXPECT_THROW(editing->Sample(invalid,.01,1.),std::invalid_argument);
        EXPECT_THROW(editing->Split(invalid,{{0,middle}}),std::invalid_argument);
        invalid.Closed=true;EXPECT_THROW(editing->TraceClosed(invalid,{0,first},1,false),std::invalid_argument);
    }
}
TEST(CadCurveReference, PeriodicNativeDomainsMayCrossTheSeamWithoutParameterRemapping)
{
    using namespace iCAX::GeometryData;using namespace iCAX::Geometry;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto reference=provider.Resolve<ICadReferenceService>();const auto editing=provider.Resolve<ICurveEditingService>();
    Circle3 circle;circle.Radius=3;const double period=2*std::acos(-1.);CompositeCurve3 curve;curve.Closed=true;
    curve.Segments={{circle,{period-.1,period+.2,false}},{circle,{.2,period-.1,false}}};
    Near(reference->EvaluateCurve(curve,0,period+.2),{3*std::cos(.2),3*std::sin(.2),0});
    const auto samples=editing->Sample(curve,.01,.5);EXPECT_EQ(period-.1,samples.Points.front().NativeParameter);
    const auto trace=editing->TraceClosed(curve,{0,period},0,false);EXPECT_NEAR(3*period,trace.PerimeterMm,1e-8);
    const auto full=editing->TraceClosed(curve,{0,period},trace.PerimeterMm,false);
    EXPECT_EQ(0u,full.End.SegmentIndex);EXPECT_NEAR(period,full.End.NativeParameter,1e-9);
    const auto exact=provider.Resolve<iCAX::GeometryServices::ICurveOffset3DService>()->EvaluatePiece({curve.Segments.front(),{}},{period+.2});
    EXPECT_EQ(ECurveOffsetStatus::SamplesEvaluated,exact.Status)<<exact.Diagnostic;
    curve.Segments.front().Range={1e20,1e20+1e8,false};EXPECT_THROW(reference->EvaluateCurve(curve,0,1e20),std::invalid_argument);
}
TEST(CadCurveReference, AmbiguousAndInvalidQueriesAreRejected)
{
    using namespace iCAX::GeometryData;
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);
    const auto service=provider.Resolve<ICadReferenceService>();
    Line3 a,b;a.Axis.Location={0,-1,0};b.Axis.Location={0,1,0};a.Axis.Direction=b.Axis.Direction={1,0,0};
    CompositeCurve3 path;path.Segments={{a,{0,10,false}},{b,{0,10,false}}};
    SCurveReferenceQuery q;
    const auto ambiguous=[&](){
        try{service->ResolveCurve(path,q);FAIL()<<"Expected ambiguous source projection";}
        catch(const std::invalid_argument& error){EXPECT_STREQ("CAD_REFERENCE_AMBIGUOUS_PROJECTION",error.what());}
    };
    q.NearPoint={5,0,0};ambiguous();
    a.Axis.Location={-5,0,0};b.Axis.Location={0,-5,0};b.Axis.Direction={0,1,0};
    path.Segments={{a,{0,10,false}},{b,{0,10,false}}};q.NearPoint={0,0,0};
    ambiguous();
    Circle3 circle;circle.Radius=1;path.Segments={{circle,{0,2*std::acos(-1.),false}}};q.NearPoint={0,0,0};
    EXPECT_THROW(service->ResolveCurve(path,q),std::invalid_argument);
    q.NearPoint={1,0,0};q.MaximumDistanceMm=0;EXPECT_THROW(service->ResolveCurve(path,q),std::invalid_argument);
    q.MaximumDistanceMm=1;path.Segments[0].Range.Last=std::numeric_limits<double>::infinity();
    EXPECT_THROW(service->ResolveCurve(path,q),std::invalid_argument);
}
