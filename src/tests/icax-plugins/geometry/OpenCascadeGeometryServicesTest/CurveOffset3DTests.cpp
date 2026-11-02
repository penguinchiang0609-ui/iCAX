#include <gtest/gtest.h>
#include <GeometryServices/CurveOffset3DService.h>
#include <GeometryServices/OffsetCurveTraversal.h>
#include <GeometryData/BRepPersistence.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Windows.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <future>
#include <limits>

using namespace iCAX::GeometryData;
using namespace iCAX::GeometryServices;
using Status = ECurveOffsetStatus;
namespace
{
    std::string LoadProvider()
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const auto path = std::filesystem::path(exe).parent_path() / L"OpenCascadeGeometryServices.dll";
        // Catalog stores callbacks into this module, so keep it loaded for the process lifetime.
        static const HMODULE module = LoadLibraryW(path.c_str());
        if (!module) throw std::runtime_error("Cannot load geometry service DLL");
        return path.string();
    }
    class CurveOffset3D : public ::testing::Test
    {
    protected:
        iCAX::Services::CServiceProvider provider;
        std::shared_ptr<ICurveOffset3DService> service;
        void SetUp() override
        {
            iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(provider, {LoadProvider()});
            service = provider.Resolve<ICurveOffset3DService>();
        }
    };
    DirectionalCurveOffset3 SpaceCurve()
    {
        DirectionalCurveOffset3 law;
        // Noncoplanar cubic: P(u)=(u,u^2,u^3), not a rotated planar contour.
        law.Source.Curve = Bezier3{{{0,0,0},{1.0/3,0,0},{2.0/3,1.0/3,0},{1,1,1}}};
        law.ReferenceDirection = Direction3{0,1,1};
        law.SignedDistance = .17;
        return law;
    }
    void VaryDirection(DirectionalCurveOffset3& law)
    {
        // D(u)=(u,1,1+u^2).
        law.ReferenceDirection = ParametricVectorField3{Bezier3{{{0,1,1},{.5,1,1},{1,1,2}}}};
    }
    double Distance(Point3 a, Point3 b)
    {
        return std::hypot(std::hypot(a.X-b.X,a.Y-b.Y),a.Z-b.Z);
    }
    void Near(Point3 a, Point3 b, double tolerance = 1e-11)
    {
        EXPECT_NEAR(a.X,b.X,tolerance); EXPECT_NEAR(a.Y,b.Y,tolerance); EXPECT_NEAR(a.Z,b.Z,tolerance);
    }
    Point3 AnalyticOffset(double u, double r, bool varying)
    {
        double x = 3*u*u-2*u, y = 1, z = -1;
        if (varying) { x -= 2*u*u*u; y = 1+u*u-3*u*u*u; z = 2*u*u-1; }
        const double n = std::sqrt(x*x+y*y+z*z);
        return {u+r*x/n,u*u+r*y/n,u*u*u+r*z/n};
    }
}

TEST_F(CurveOffset3D, RegistersWithoutCAMOrProductAndUsesProviderLocalSingleton)
{
    EXPECT_EQ(service, provider.Resolve<ICurveOffset3DService>());
    iCAX::Services::CServiceProvider second;
    iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(second, {LoadProvider()});
    EXPECT_NE(service, second.Resolve<ICurveOffset3DService>());
    EXPECT_EQ(nullptr, GetModuleHandleW(L"FiveAxisCAM.dll"));
    EXPECT_EQ(nullptr, GetModuleHandleW(L"ProcessPlanning.dll"));
}

namespace {
    std::vector<DirectionalCurveOffset3> Polygon(const std::vector<Point3>& p,double distance,bool closed)
    {
        std::vector<DirectionalCurveOffset3> laws;
        for(size_t i=1;i<p.size()+(closed?1:0);++i){DirectionalCurveOffset3 l;l.Source.Curve=Segment3{p[i-1],p[i%p.size()]};l.SignedDistance=distance;laws.push_back(l);}return laws;
    }
    ParameterRange PieceRange(const OffsetCurvePiece3& p){if(const auto* l=std::get_if<DirectionalCurveOffset3>(&p.Geometry))return l->Source.Range;return std::get<CurveSegment3>(p.Geometry).Range;}
    void Connected(const ICurveOffset3DService& service,const CompositeOffsetCurve3& c,double tolerance=1e-5)
    {
        ASSERT_FALSE(c.Segments.empty());
        for(size_t i=1;i<c.Segments.size()+(c.Closed?1:0);++i){const auto& a=c.Segments[i-1];const auto& b=c.Segments[i%c.Segments.size()];const auto ar=PieceRange(a),br=PieceRange(b);
            const auto x=service.EvaluatePiece(a,{ar.Reversed?ar.First:ar.Last}),y=service.EvaluatePiece(b,{br.Reversed?br.Last:br.First});
            ASSERT_EQ(Status::SamplesEvaluated,x.Status);ASSERT_EQ(Status::SamplesEvaluated,y.Status);EXPECT_LE(Distance(x.Samples[0].OffsetPoint,y.Samples[0].OffsetPoint),tolerance);}
    }
}

TEST_F(CurveOffset3D, CompositeInnerCornersTrimNativeParametersWithoutFitting)
{
    const auto laws=Polygon({{0,0,0},{10,0,0},{10,10,0},{0,10,0}},1,true);
    const auto r=service->BuildComposite(laws,true);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(4u,r.Curve.Segments.size());EXPECT_EQ(4u,r.TrimmedJoints);EXPECT_EQ(0u,r.AddedConnectors);Connected(*service,r.Curve);
    for(const auto& p:r.Curve.Segments){ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(p.Geometry));const auto& l=std::get<DirectionalCurveOffset3>(p.Geometry);
        EXPECT_NEAR(.1,l.Source.Range.First,1e-8);EXPECT_NEAR(.9,l.Source.Range.Last,1e-8);EXPECT_DOUBLE_EQ(1,l.SignedDistance);}
    EXPECT_DOUBLE_EQ(0,laws[0].Source.Range.First);EXPECT_DOUBLE_EQ(1,laws[0].Source.Range.Last);
}

TEST_F(CurveOffset3D, CompositeOuterCornersHaveExplicitMiterRoundAndBevelConnectors)
{
    const auto laws=Polygon({{0,0,0},{10,0,0},{10,10,0},{0,10,0}},-1,true);
    for(auto join:{EOffsetJoin3::Miter,EOffsetJoin3::Round,EOffsetJoin3::Bevel}){
        CompositeOffsetOptions3 o;o.Join=join;o.Tolerance=1e-4;
        const auto r=service->BuildComposite(laws,true,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
        EXPECT_EQ(join==EOffsetJoin3::Miter?8u:4u,r.AddedConnectors);Connected(*service,r.Curve,o.Tolerance);
        for(const auto& p:r.Curve.Segments)if(!p.SourceSegment){ASSERT_TRUE(std::holds_alternative<CurveSegment3>(p.Geometry));if(join==EOffsetJoin3::Round)EXPECT_TRUE(std::holds_alternative<Circle3>(std::get<CurveSegment3>(p.Geometry).Curve));}
    }
}

TEST_F(CurveOffset3D, CompositeVariableAxisSpatialCurvePreservesExactLawAndNativeU)
{
    auto a=SpaceCurve();VaryDirection(a);auto b=a;a.Source.Range={0,.5,false};b.Source.Range={.5,1,false};
    CompositeOffsetOptions3 o;o.Tolerance=1e-4;const auto r=service->BuildComposite({a,b},false,o);
    ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;ASSERT_EQ(2u,r.Curve.Segments.size());Connected(*service,r.Curve);
    for(size_t i=0;i<2;++i){const auto& p=r.Curve.Segments[i];ASSERT_EQ(i,*p.SourceSegment);const double u=i?.73:.21;const auto v=service->EvaluatePiece(p,{u});ASSERT_EQ(Status::SamplesEvaluated,v.Status);
        Near(v.Samples[0].OffsetPoint,AnalyticOffset(u,a.SignedDistance,true));EXPECT_DOUBLE_EQ(u,v.Samples[0].SourceParameter);}
}

TEST_F(CurveOffset3D, CompositeOpenSelfIntersectionRemovesLoopAndKeepsEndpoints)
{
    const auto laws=Polygon({{0,0,0},{4,4,0},{0,4,0},{4,0,0}},0,false);
    const auto r=service->BuildComposite(laws,false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(1u,r.RemovedLoops.size());ASSERT_EQ(2u,r.Curve.Segments.size());Connected(*service,r.Curve);Connected(*service,r.RemovedLoops[0]);
    EXPECT_NEAR(.5,PieceRange(r.Curve.Segments[0]).Last,1e-8);EXPECT_NEAR(.5,PieceRange(r.Curve.Segments[1]).First,1e-8);
}

TEST_F(CurveOffset3D, CompositeSingleCubicSelfIntersectionTrimsItsOwnParameterDomain)
{
    DirectionalCurveOffset3 l;l.Source.Curve=Bezier3{{{.25,-.045,0},{-1./12,.15166666666666667,0},{-1./12,-.15166666666666667,0},{.25,.045,0}}};
    CompositeOffsetOptions3 o;o.Tolerance=1e-4;
    const auto r=service->BuildComposite({l},false,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(1u,r.RemovedLoops.size());ASSERT_EQ(2u,r.Curve.Segments.size());Connected(*service,r.Curve,o.Tolerance);
    EXPECT_NEAR(.1,PieceRange(r.Curve.Segments[0]).Last,1e-4);EXPECT_NEAR(.9,PieceRange(r.Curve.Segments[1]).First,1e-4);
}

TEST_F(CurveOffset3D, CompositeProjectedCrossingAtDifferentHeightsIsNotAnIntersection)
{
    const auto laws=Polygon({{0,0,0},{4,4,0},{0,4,2},{4,0,2}},0,false);
    const auto r=service->BuildComposite(laws,false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    EXPECT_EQ(3u,r.Curve.Segments.size());EXPECT_TRUE(r.RemovedLoops.empty());Connected(*service,r.Curve);
}

TEST_F(CurveOffset3D, PlanarFastPathIsScopedToEachIndependentTarget)
{
    const auto first=Polygon({{0,0,7},{10,0,7},{10,10,7},{0,10,7}},1,true);
    auto second=Polygon({{21,0,0},{21,10,0},{21,10,10},{21,0,10}},1,true);
    for(auto& law:second)law.ReferenceDirection=Direction3{1,0,0};
    const std::vector<DirectionalCurveOffset3>* targets[]={&first,&second,&first};
    for(const auto* target:targets)
    {
        const auto result=service->BuildComposite(*target,true);
        ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;
        EXPECT_EQ("OCCT/PlanarTarget2D",result.Diagnostic);
        ASSERT_EQ(4u,result.Curve.Segments.size());EXPECT_EQ(4u,result.TrimmedJoints);Connected(*service,result.Curve);
        for(std::size_t i=0;i<4;++i)
        {
            const auto& law=std::get<DirectionalCurveOffset3>(result.Curve.Segments[i].Geometry);
            ASSERT_TRUE(result.Curve.Segments[i].SourceSegment);EXPECT_EQ(i,*result.Curve.Segments[i].SourceSegment);
            EXPECT_NEAR(.1,law.Source.Range.First,1e-8);EXPECT_NEAR(.9,law.Source.Range.Last,1e-8);
            std::vector<double> parameters;for(int k=0;k<=4;++k)parameters.push_back(law.Source.Range.First+(law.Source.Range.Last-law.Source.Range.First)*k/4);
            const auto actual=service->Evaluate(law,parameters);
            const auto expected=service->Evaluate((*target)[i],parameters);
            ASSERT_EQ(Status::SamplesEvaluated,actual.Status);ASSERT_EQ(Status::SamplesEvaluated,expected.Status);
            for(std::size_t k=0;k<actual.Samples.size();++k)Near(actual.Samples[k].OffsetPoint,expected.Samples[k].OffsetPoint,1e-12);
        }
    }
}

namespace
{
    Point3 SlantedPlanarPoint(Point3 p)
    {
        // X=(2,-1,0)/sqrt(5), N=(1,2,2)/3, Y=N cross X.
        const double root=std::sqrt(5.);
        return {13+(2*p.X+2*p.Y/3)/root,-7+(-p.X+4*p.Y/3)/root,19-5*p.Y/(3*root)};
    }
    Placement3 SlantedPlanarFrame()
    {
        const double root=std::sqrt(5.);Placement3 frame;frame.Location=SlantedPlanarPoint({});
        frame.XDirection={2/root,-1/root,0};frame.YDirection={2/(3*root),4/(3*root),-5/(3*root)};frame.ZDirection={1./3,2./3,2./3};return frame;
    }
}

TEST_F(CurveOffset3D, SlantedPlanarAnalyticAndSplineTargetsKeepExactLawsAndNativeDirection)
{
    const auto frame=SlantedPlanarFrame();
    Circle3 circle;circle.Placement=frame;circle.Radius=10;
    Ellipse3 ellipse;ellipse.Placement=frame;ellipse.MajorRadius=10;ellipse.MinorRadius=5;
    Bezier3 bezier{{SlantedPlanarPoint({0,0,0}),SlantedPlanarPoint({3,2,0}),SlantedPlanarPoint({7,2,0}),SlantedPlanarPoint({10,0,0})}};
    BSpline3 spline;spline.Degree=3;spline.Knots={2,3};spline.Multiplicities={4,4};spline.Poles=bezier.Poles;
    NURBS3 nurbs;nurbs.Degree=spline.Degree;nurbs.Knots=spline.Knots;nurbs.Multiplicities=spline.Multiplicities;nurbs.Poles=spline.Poles;nurbs.Weights={1,1.2,.8,1};
    std::vector<Curve3> curves{circle,ellipse,bezier,spline,nurbs};
    for(std::size_t kind=0;kind<curves.size();++kind)for(bool reverse:{false,true})for(double sign:{-1.,1.})
    {
        DirectionalCurveOffset3 law;law.Source.Curve=curves[kind];law.Source.Range=kind<2?ParameterRange{.2,2.7,reverse}:kind==2?ParameterRange{0,1,reverse}:ParameterRange{2,3,reverse};
        law.ReferenceDirection=Direction3{frame.ZDirection.X*sign,frame.ZDirection.Y*sign,frame.ZDirection.Z*sign};law.SignedDistance=.1;
        const auto result=service->BuildComposite({law},false);
        ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<kind<<" "<<result.Diagnostic;EXPECT_EQ("OCCT/PlanarTarget2D",result.Diagnostic);
        ASSERT_EQ(1u,result.Curve.Segments.size());const auto& output=std::get<DirectionalCurveOffset3>(result.Curve.Segments.front().Geometry);
        EXPECT_EQ(law.Source.Curve.index(),output.Source.Curve.index());EXPECT_DOUBLE_EQ(law.Source.Range.First,output.Source.Range.First);EXPECT_DOUBLE_EQ(law.Source.Range.Last,output.Source.Range.Last);EXPECT_EQ(reverse,output.Source.Range.Reversed);
        std::vector<double> parameters;for(int k=0;k<=20;++k)parameters.push_back(law.Source.Range.First+(law.Source.Range.Last-law.Source.Range.First)*k/20);
        const auto expected=service->Evaluate(law,parameters),actual=service->EvaluatePiece(result.Curve.Segments.front(),parameters);
        ASSERT_EQ(Status::SamplesEvaluated,actual.Status);ASSERT_EQ(Status::SamplesEvaluated,expected.Status);
        for(std::size_t k=0;k<actual.Samples.size();++k)
        {
            Near(actual.Samples[k].SourcePoint,expected.Samples[k].SourcePoint,1e-12);Near(actual.Samples[k].OffsetPoint,expected.Samples[k].OffsetPoint,1e-12);
            const auto& p=actual.Samples[k].OffsetPoint;const auto& o=frame.Location;
            EXPECT_NEAR(0,(p.X-o.X)*frame.ZDirection.X+(p.Y-o.Y)*frame.ZDirection.Y+(p.Z-o.Z)*frame.ZDirection.Z,1e-12);
        }
    }
}

TEST_F(CurveOffset3D, SlantedPlanarCornersPreserveNativeTrimAndSpatialConnectors)
{
    const auto frame=SlantedPlanarFrame();
    for(double distance:{1.,-1.})for(bool reverse:{false,true})for(auto join:{EOffsetJoin3::Miter,EOffsetJoin3::Round,EOffsetJoin3::Bevel})
    {
        auto laws=Polygon({SlantedPlanarPoint({0,0,0}),SlantedPlanarPoint({10,0,0}),SlantedPlanarPoint({10,10,0}),SlantedPlanarPoint({0,10,0})},distance,true);
        for(auto& law:laws){law.ReferenceDirection=frame.ZDirection;law.Source.Range.Reversed=reverse;}
        if(reverse)std::reverse(laws.begin(),laws.end());
        CompositeOffsetOptions3 options;options.Join=join;options.Tolerance=1e-4;
        const auto result=service->BuildComposite(laws,true,options);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;
        EXPECT_EQ("OCCT/PlanarTarget2D",result.Diagnostic);Connected(*service,result.Curve,options.Tolerance);
        if(distance>0)
        {
            EXPECT_EQ(4u,result.TrimmedJoints);EXPECT_EQ(0u,result.AddedConnectors);
            for(const auto& piece:result.Curve.Segments){EXPECT_NEAR(.1,PieceRange(piece).First,1e-8);EXPECT_NEAR(.9,PieceRange(piece).Last,1e-8);}
        }
        else EXPECT_EQ(join==EOffsetJoin3::Miter?8u:4u,result.AddedConnectors);
        for(const auto& piece:result.Curve.Segments)if(!piece.SourceSegment)
        {
            const auto range=PieceRange(piece);const auto points=service->EvaluatePiece(piece,{range.First,(range.First+range.Last)*.5,range.Last});ASSERT_EQ(Status::SamplesEvaluated,points.Status);
            for(const auto& point:points.Samples){const auto& p=point.OffsetPoint;const auto& o=frame.Location;EXPECT_NEAR(0,(p.X-o.X)*frame.ZDirection.X+(p.Y-o.Y)*frame.ZDirection.Y+(p.Z-o.Z)*frame.ZDirection.Z,1e-11);}
        }
    }
}

TEST_F(CurveOffset3D, PlanarCertificateRejectsHiddenSplineWarpTiltedAxisAndVariableAxis)
{
    DirectionalCurveOffset3 warped;warped.Source.Curve=Bezier3{{{0,0,0},{3,1,1},{7,1,-1},{10,0,0}}};warped.SignedDistance=.1;
    auto result=service->BuildComposite({warped},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;
    EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);ASSERT_EQ(1u,result.Curve.Segments.size());
    const auto samples=service->EvaluatePiece(result.Curve.Segments.front(),{0,.25,.5,.75,1});ASSERT_EQ(Status::SamplesEvaluated,samples.Status);EXPECT_GT(std::abs(samples.Samples[1].SourcePoint.Z),.1);
    auto tilted=warped;std::get<Bezier3>(tilted.Source.Curve).Poles={{0,0,0},{3,1,0},{7,1,0},{10,0,0}};tilted.ReferenceDirection=Direction3{.2,0,1};
    result=service->BuildComposite({tilted},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);
    const auto spatial=service->EvaluatePiece(result.Curve.Segments.front(),{.25});ASSERT_EQ(Status::SamplesEvaluated,spatial.Status);EXPECT_GT(std::abs(spatial.Samples.front().OffsetPoint.Z),1e-4);
    tilted.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,0,1},{.2,0,1}}}};
    result=service->BuildComposite({tilted},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);
}

TEST_F(CurveOffset3D, PlanarRoundoffAndLargeAnalyticScaleUseSpatialChecks)
{
    DirectionalCurveOffset3 tiny;tiny.Source.Curve=Bezier3{{{0,0,0},{1e-13,0,9e-11}}};
    ASSERT_EQ(Status::SamplesEvaluated,service->Evaluate(tiny,{0,.5,1}).Status);
    auto result=service->BuildComposite({tiny},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;
    EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);ASSERT_EQ(1u,result.Curve.Segments.size());
    const auto actual=service->EvaluatePiece(result.Curve.Segments.front(),{0,.5,1});ASSERT_EQ(Status::SamplesEvaluated,actual.Status);Near(actual.Samples.back().SourcePoint,{1e-13,0,9e-11},0.);
    DirectionalCurveOffset3 longLine;longLine.Source.Curve=Line3{{{0,0,0},{1,0,9e-14}}};longLine.Source.Range={0,1e10,false};
    result=service->BuildComposite({longLine},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);
    Circle3 circle;circle.Radius=1e6;circle.Placement.ZDirection={0,9e-11,1};DirectionalCurveOffset3 tiltedFrame;tiltedFrame.Source={circle,{0,.0001,false}};
    result=service->BuildComposite({tiltedFrame},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,result.Status)<<result.Diagnostic;EXPECT_NE("OCCT/PlanarTarget2D",result.Diagnostic);
}

TEST_F(CurveOffset3D, CompositeRemovesALoopCreatedByOffsettingASimpleSource)
{
    const auto laws=Polygon({{0,0,0},{4,0,0},{4,4,0},{1,4,0},{1,.5,0}},1,false);
    const auto r=service->BuildComposite(laws,false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(1u,r.RemovedLoops.size());Connected(*service,r.Curve);Connected(*service,r.RemovedLoops[0]);
    const auto first=service->EvaluatePiece(r.Curve.Segments.front(),{PieceRange(r.Curve.Segments.front()).First});
    const auto last=service->EvaluatePiece(r.Curve.Segments.back(),{PieceRange(r.Curve.Segments.back()).Last});
    Near(first.Samples[0].OffsetPoint,{0,1,0});Near(last.Samples[0].OffsetPoint,{2,.5,0});
}

TEST_F(CurveOffset3D, CompositeSplitsLowContinuityKnotsAndKeepsOriginalSourceIndex)
{
    DirectionalCurveOffset3 l;BSpline3 c;c.Degree=2;c.Poles={{0,0,0},{2,0,1},{4,1,1},{6,1,2}};c.Knots={2,3,5};c.Multiplicities={3,1,3};
    l.Source.Curve=c;l.Source.Range={2,5,false};l.SignedDistance=.1;
    CompositeOffsetOptions3 o;o.Tolerance=1e-4;const auto r=service->BuildComposite({l},false,o);
    ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;ASSERT_EQ(2u,r.Curve.Segments.size());Connected(*service,r.Curve,o.Tolerance);
    EXPECT_EQ(2,PieceRange(r.Curve.Segments[0]).First);EXPECT_EQ(3,PieceRange(r.Curve.Segments[0]).Last);EXPECT_EQ(5,PieceRange(r.Curve.Segments[1]).Last);
    EXPECT_EQ(0u,*r.Curve.Segments[0].SourceSegment);EXPECT_EQ(0u,*r.Curve.Segments[1].SourceSegment);
    l.Source.Range.Reversed=true;const auto reverse=service->BuildComposite({l},false,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,reverse.Status)<<reverse.Diagnostic;Connected(*service,reverse.Curve,o.Tolerance);
}

TEST_F(CurveOffset3D, CompositeTopologyIsInvariantUnderRigidRotationAndTranslation)
{
    auto laws=Polygon({{0,0,0},{4,0,0},{4,4,0},{1,4,0},{1,.5,0}},1,false);
    const auto a=service->BuildComposite(laws,false);ASSERT_EQ(ECompositeOffsetStatus3::Built,a.Status)<<a.Diagnostic;
    const auto transform=[](Point3 p){return Point3{p.Z+17,p.X-3,p.Y+12};};
    for(auto& law:laws){auto& c=std::get<Segment3>(law.Source.Curve);c.Start=transform(c.Start);c.End=transform(c.End);law.ReferenceDirection=Direction3{1,0,0};}
    const auto b=service->BuildComposite(laws,false);ASSERT_EQ(ECompositeOffsetStatus3::Built,b.Status)<<b.Diagnostic;ASSERT_EQ(a.Curve.Segments.size(),b.Curve.Segments.size());
    EXPECT_EQ(a.RemovedLoops.size(),b.RemovedLoops.size());
    for(size_t i=0;i<a.Curve.Segments.size();++i){const auto ar=PieceRange(a.Curve.Segments[i]),br=PieceRange(b.Curve.Segments[i]);EXPECT_NEAR(ar.First,br.First,1e-10);EXPECT_NEAR(ar.Last,br.Last,1e-10);
        const double u=(ar.First+ar.Last)*.5;const auto p=service->EvaluatePiece(a.Curve.Segments[i],{u}),q=service->EvaluatePiece(b.Curve.Segments[i],{u});Near(transform(p.Samples[0].OffsetPoint),q.Samples[0].OffsetPoint);}
}

TEST_F(CurveOffset3D, CompositeClosedLoopRetentionIsExplicitAndRemovedGeometryIsReturned)
{
    const auto laws=Polygon({{0,0,0},{6,6,0},{0,6,0},{2,0,0}},0,true);
    const auto r=service->BuildComposite(laws,true);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(1u,r.RemovedLoops.size());EXPECT_TRUE(r.Curve.Closed);Connected(*service,r.Curve);Connected(*service,r.RemovedLoops[0]);
    CompositeOffsetOptions3 o;o.LoopRetention=EOffsetLoopRetention3::Start;const auto start=service->BuildComposite(laws,true,o);
    ASSERT_EQ(ECompositeOffsetStatus3::Built,start.Status)<<start.Diagnostic;Connected(*service,start.Curve);
    bool retainsStart=false;for(const auto& p:start.Curve.Segments)if(p.SourceSegment==0&&PieceRange(p).First==0)retainsStart=true;EXPECT_TRUE(retainsStart);
}

TEST_F(CurveOffset3D, CompositeReversingTraversalDoesNotReversePhysicalOffset)
{
    auto laws=Polygon({{0,0,0},{10,0,0},{10,10,0},{0,10,0}},1,true);std::reverse(laws.begin(),laws.end());for(auto& l:laws)l.Source.Range.Reversed=true;
    const auto r=service->BuildComposite(laws,true);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;Connected(*service,r.Curve);
    for(const auto& p:r.Curve.Segments){EXPECT_TRUE(PieceRange(p).Reversed);EXPECT_NEAR(.1,PieceRange(p).First,1e-8);EXPECT_NEAR(.9,PieceRange(p).Last,1e-8);}
}

TEST_F(CurveOffset3D, CompositeVeryShortCornerConnectorsAreNotSelfIntersectionLoops)
{
    auto laws=Polygon({{0,0,0},{10,0,0},{20,.05,0}},-.1,false);CompositeOffsetOptions3 o;o.Tolerance=1e-4;
    const auto r=service->BuildComposite(laws,false,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    EXPECT_TRUE(r.RemovedLoops.empty());EXPECT_GT(r.AddedConnectors,0u);Connected(*service,r.Curve,o.Tolerance);
}

TEST_F(CurveOffset3D, NativeDistanceLineAndEllipticalArcKeepTheirParameterDomains)
{
    DirectionalCurveOffset3 l;l.Source={Line3{{{5,6,7},{1,0,0}}},{2,12,false}};l.SignedDistance=.3;
    const auto r=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    const auto p=service->EvaluatePiece(r.Curve.Segments[0],{5});ASSERT_EQ(Status::SamplesEvaluated,p.Status);Near(p.Samples[0].OffsetPoint,{10,6.3,7});
    EllipseArc3 e;e.Basis.MajorRadius=5;e.Basis.MinorRadius=3;e.StartAngle=.2;e.EndAngle=2.;l.Source={e,{.2,2,false}};
    const auto ellipse=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,ellipse.Status)<<ellipse.Diagnostic;
    EXPECT_DOUBLE_EQ(.2,PieceRange(ellipse.Curve.Segments[0]).First);EXPECT_DOUBLE_EQ(2,PieceRange(ellipse.Curve.Segments[0]).Last);
}

TEST_F(CurveOffset3D, CompositeRejectsCollapseDisconnectionSingularAxisCancellationAndBudgetAtomically)
{
    auto laws=Polygon({{0,0,0},{10,0,0},{10,10,0}},1,false);CompositeOffsetOptions3 o;
    const auto empty=[&](const CompositeOffsetResult3& r){EXPECT_NE(ECompositeOffsetStatus3::Built,r.Status);EXPECT_TRUE(r.Curve.Segments.empty());EXPECT_TRUE(r.RemovedLoops.empty());};
    o.IsCancelled=[]{return true;};auto r=service->BuildComposite(laws,false,o);EXPECT_EQ(ECompositeOffsetStatus3::Cancelled,r.Status);empty(r);
    o={};o.MaxSamples=3;r=service->BuildComposite(laws,false,o);EXPECT_EQ(ECompositeOffsetStatus3::BudgetExceeded,r.Status);empty(r);
    o={};std::get<Segment3>(laws[1].Source.Curve).Start.X=11;r=service->BuildComposite(laws,false,o);EXPECT_EQ(ECompositeOffsetStatus3::DisconnectedSource,r.Status);empty(r);
    auto l=SpaceCurve();l.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,0,1},{0,0,-1}}}};r=service->BuildComposite({l},false);empty(r);
    l={};Circle3 c;c.Radius=1;l.Source.Curve=c;l.Source.Range={0,2*std::acos(-1.),false};l.SignedDistance=2;r=service->BuildComposite({l},true);EXPECT_EQ(ECompositeOffsetStatus3::Collapsed,r.Status);empty(r);
}
TEST_F(CurveOffset3D, TrimmedPeriodicSplineUsesItsActiveSpanBudgetAndRetainsTheExactSource)
{
    BSpline3 spline;spline.Degree=3;spline.Periodic=true;constexpr std::size_t count=256;
    for(std::size_t i=0;i<count;++i){const double angle=2*std::acos(-1.)*i/count;spline.Poles.push_back({20*std::cos(angle),20*std::sin(angle),0});}
    for(std::size_t i=0;i<=count;++i){spline.Knots.push_back(double(i));spline.Multiplicities.push_back(1);}
    DirectionalCurveOffset3 law;law.Source={spline,{.2,.3,false}};law.ReferenceDirection=Direction3{0,0,1};law.SignedDistance=.01;
    CompositeOffsetOptions3 options;options.MaxSamples=512;
    const auto built=service->BuildComposite({law},false,options);ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;ASSERT_EQ(1u,built.Curve.Segments.size());
    const auto& actual=std::get<DirectionalCurveOffset3>(built.Curve.Segments.front().Geometry);
    iCAX::GeometryData::Persistence::Writer originalBytes,actualBytes;originalBytes.Write(law);actualBytes.Write(actual);EXPECT_EQ(originalBytes.Bytes,actualBytes.Bytes);
    const auto before=service->Evaluate(law,{.2,.25,.3}),after=service->Evaluate(actual,{.2,.25,.3});ASSERT_EQ(Status::SamplesEvaluated,before.Status);ASSERT_EQ(Status::SamplesEvaluated,after.Status);
    for(std::size_t i=0;i<before.Samples.size();++i)Near(before.Samples[i].OffsetPoint,after.Samples[i].OffsetPoint,0.);
}

TEST_F(CurveOffset3D, FixedDirectionOffsetsNoncoplanarCubicWithThreeCoordinateChanges)
{
    const auto law = SpaceCurve();
    const auto r = service->Evaluate(law, {0,.13,.37,.71,1});
    ASSERT_EQ(Status::SamplesEvaluated, r.Status) << r.Diagnostic;
    EXPECT_EQ("OCCT/Geom_OffsetCurve", r.Backend);
    EXPECT_EQ(ECurveOffsetCoverage::RequestedParametersOnly, r.Coverage);
    for (const auto& s : r.Samples)
    {
        Near(s.SourcePoint, {s.SourceParameter, std::pow(s.SourceParameter,2), std::pow(s.SourceParameter,3)});
        Near(s.OffsetPoint, AnalyticOffset(s.SourceParameter, law.SignedDistance, false));
        EXPECT_NEAR(law.SignedDistance, Distance(s.SourcePoint,s.OffsetPoint), 1e-12);
    }
    EXPECT_GT(std::abs(r.Samples[2].OffsetPoint.Z-r.Samples[2].SourcePoint.Z), .05);
}

TEST_F(CurveOffset3D, VariableDirectionUsesFieldDerivativeAndAnalyticContact)
{
    auto law = SpaceCurve(); VaryDirection(law);
    std::vector<double> parameters;
    for (int i=1;i<50;++i) parameters.push_back(i/50.0);
    const auto r = service->Evaluate(law, parameters);
    ASSERT_EQ(Status::SamplesEvaluated, r.Status) << r.Diagnostic;
    EXPECT_EQ("OCCT/ParametricDirection", r.Backend);
    for (const auto& s : r.Samples)
    {
        const double u=s.SourceParameter, h=1e-5;
        Near(s.OffsetPoint,AnalyticOffset(u,law.SignedDistance,true));
        const auto a=AnalyticOffset(u-h,law.SignedDistance,true), b=AnalyticOffset(u+h,law.SignedDistance,true);
        EXPECT_NEAR(s.OffsetDerivative.X,(b.X-a.X)/(2*h),2e-8);
        EXPECT_NEAR(s.OffsetDerivative.Y,(b.Y-a.Y)/(2*h),2e-8);
        EXPECT_NEAR(s.OffsetDerivative.Z,(b.Z-a.Z)/(2*h),2e-8);
        const Point3 delta{s.OffsetPoint.X-s.SourcePoint.X,s.OffsetPoint.Y-s.SourcePoint.Y,s.OffsetPoint.Z-s.SourcePoint.Z};
        EXPECT_NEAR(delta.X+2*u*delta.Y+3*u*u*delta.Z,0,1e-12);
        EXPECT_NEAR(u*delta.X+delta.Y+(1+u*u)*delta.Z,0,1e-12);
    }
}

TEST_F(CurveOffset3D, BSplinePreservesNativeDomainAndRejectsExtrapolation)
{
    auto law = SpaceCurve();
    BSpline3 curve;
    curve.Poles=std::get<Bezier3>(law.Source.Curve).Poles;
    curve.Knots={2,5}; curve.Multiplicities={4,4};
    law.Source.Curve=curve; law.Source.Range={2,5,false};
    auto r=service->Evaluate(law,{2,3.5,5});
    ASSERT_EQ(Status::SamplesEvaluated,r.Status) << r.Diagnostic;
    Near(r.Samples[1].OffsetPoint,AnalyticOffset(.5,law.SignedDistance,false));
    EXPECT_EQ(3.5,r.Samples[1].SourceParameter);
    EXPECT_EQ(Status::InvalidInput,service->Evaluate(law,{1.9}).Status);
}

TEST_F(CurveOffset3D, RationalSplineUsesWeightsAndNativeDerivatives)
{
    auto law=SpaceCurve();
    NURBS3 curve;
    curve.Poles=std::get<Bezier3>(law.Source.Curve).Poles;
    curve.Knots={0,1}; curve.Multiplicities={4,4}; curve.Weights={1,2,.5,1.5};
    law.Source.Curve=curve;
    auto r=service->Evaluate(law,{.2,.5,.8});
    ASSERT_EQ(Status::SamplesEvaluated,r.Status) << r.Diagnostic;
    const auto analytic=[&](double u)
    {
        const double b[]={std::pow(1-u,3),3*u*(1-u)*(1-u),3*u*u*(1-u),u*u*u};
        Point3 p{}; double total=0;
        for(int i=0;i<4;++i){const double w=b[i]*curve.Weights[i];total+=w;p.X+=w*curve.Poles[i].X;p.Y+=w*curve.Poles[i].Y;p.Z+=w*curve.Poles[i].Z;}
        return Point3{p.X/total,p.Y/total,p.Z/total};
    };
    for(const auto& s:r.Samples)
    {
        Near(s.SourcePoint,analytic(s.SourceParameter));
        const double h=1e-6;
        const auto a=analytic(s.SourceParameter-h),b=analytic(s.SourceParameter+h);
        const auto delta=Point3{s.OffsetPoint.X-s.SourcePoint.X,s.OffsetPoint.Y-s.SourcePoint.Y,s.OffsetPoint.Z-s.SourcePoint.Z};
        EXPECT_NEAR(delta.X*(b.X-a.X)/(2*h)+delta.Y*(b.Y-a.Y)/(2*h)+delta.Z*(b.Z-a.Z)/(2*h),0,1e-9);
        EXPECT_NEAR(Distance(s.OffsetPoint,s.SourcePoint),law.SignedDistance,1e-12);
    }
}

TEST_F(CurveOffset3D, TraversalReversalKeepsSideAndGeometryReversalRequiresSignChange)
{
    auto law=SpaceCurve(); VaryDirection(law);
    const auto original=service->Evaluate(law,{.2,.7});
    ASSERT_EQ(Status::SamplesEvaluated,original.Status);
    law.Source.Range.Reversed=true;
    const auto traversal=service->Evaluate(law,{.7,.2});
    ASSERT_EQ(Status::SamplesEvaluated,traversal.Status);
    Near(traversal.Samples[1].OffsetPoint,original.Samples[0].OffsetPoint);
    auto& poles=std::get<Bezier3>(law.Source.Curve).Poles;
    auto& field=std::get<Bezier3>(std::get<ParametricVectorField3>(law.ReferenceDirection).Coefficients).Poles;
    std::reverse(poles.begin(),poles.end()); std::reverse(field.begin(),field.end());
    law.SignedDistance=-law.SignedDistance;
    const auto reversed=service->Evaluate(law,{.8,.3});
    ASSERT_EQ(Status::SamplesEvaluated,reversed.Status);
    Near(reversed.Samples[0].OffsetPoint,original.Samples[0].OffsetPoint);
    Near(reversed.Samples[1].OffsetPoint,original.Samples[1].OffsetPoint);
}

TEST_F(CurveOffset3D, RigidMotionRotatesFieldWithoutTranslatingVectors)
{
    auto law=SpaceCurve(); VaryDirection(law);
    const auto a=service->Evaluate(law,{.23,.61});
    ASSERT_EQ(Status::SamplesEvaluated,a.Status);
    const auto rotate=[](Point3 p){return Point3{p.Z,p.X,p.Y};};
    const auto transform=[&](Point3 p){auto q=rotate(p);return Point3{q.X+7,q.Y-4,q.Z+2};};
    for(auto& p:std::get<Bezier3>(law.Source.Curve).Poles)p=transform(p);
    for(auto& p:std::get<Bezier3>(std::get<ParametricVectorField3>(law.ReferenceDirection).Coefficients).Poles)p=rotate(p);
    const auto b=service->Evaluate(law,{.23,.61});
    ASSERT_EQ(Status::SamplesEvaluated,b.Status);
    for(size_t i=0;i<a.Samples.size();++i)Near(transform(a.Samples[i].OffsetPoint),b.Samples[i].OffsetPoint);
}

TEST_F(CurveOffset3D, CircleSignAndCollapseAreExplicit)
{
    DirectionalCurveOffset3 law;
    Circle3 circle; circle.Radius=4;
    law.Source.Curve=circle; law.Source.Range={0,6.283185307179586,false}; law.SignedDistance=.06;
    auto r=service->Evaluate(law,{0,1,3,6.283185307179586});
    ASSERT_EQ(Status::SamplesEvaluated,r.Status) << r.Diagnostic;
    for(const auto& s:r.Samples)EXPECT_NEAR(3.94,std::hypot(s.OffsetPoint.X,s.OffsetPoint.Y),1e-12);
    law.SignedDistance=4;
    EXPECT_EQ(Status::SingularOffset,service->Evaluate(law,{0}).Status);
}

TEST_F(CurveOffset3D, ParallelDirectionsAreRejectedWithoutFallbackOrPartialOutput)
{
    auto law=SpaceCurve(); law.ReferenceDirection=Direction3{1,0,0};
    auto r=service->Evaluate(law,{.5,0});
    EXPECT_EQ(Status::ParallelDirection,r.Status); EXPECT_TRUE(r.Samples.empty());
    EXPECT_EQ(ECurveOffsetCoverage::None,r.Coverage); ASSERT_TRUE(r.FailedParameter); EXPECT_EQ(0,*r.FailedParameter);
    law.ReferenceDirection=Direction3{1,1e-10,0};
    EXPECT_EQ(Status::ParallelDirection,service->Evaluate(law,{0}).Status);
}

TEST_F(CurveOffset3D, RejectsZeroDirectionFieldAndSourceTangent)
{
    auto law=SpaceCurve(); law.ReferenceDirection=Direction3{0,0,0};
    EXPECT_EQ(Status::DegenerateDirection,service->Evaluate(law,{.5}).Status);
    law.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,1,0},{0,-1,0}}}};
    EXPECT_EQ(Status::DegenerateDirection,service->Evaluate(law,{.5}).Status);
    law.ReferenceDirection=Direction3{0,0,1};
    law.Source.Curve=Segment3{{1,2,3},{1,2,3}};
    EXPECT_EQ(Status::DegenerateTangent,service->Evaluate(law,{.5}).Status);
}

TEST_F(CurveOffset3D, UnsplitCornersAndDiscontinuousSplinesRequirePiecewiseEvaluation)
{
    auto law=SpaceCurve(); law.Source.Curve=Polyline3{{{0,0,0},{1,0,1},{2,1,1}},false};
    law.Source.Range={0,2,false};
    EXPECT_EQ(Status::InsufficientContinuity,service->Evaluate(law,{.5}).Status);
    BSpline3 c; c.Degree=2; c.Poles={{0,0,0},{1,0,1},{2,1,1},{3,1,2}}; c.Knots={0,.5,1};c.Multiplicities={3,1,3};
    law.Source.Curve=c;law.Source.Range={0,1,false};
    EXPECT_EQ(Status::InsufficientContinuity,service->Evaluate(law,{.2}).Status);
    c.Periodic=true;law.Source.Curve=c;
    EXPECT_EQ(Status::InvalidInput,service->Evaluate(law,{.2}).Status);
}

TEST_F(CurveOffset3D, InvalidNumbersWeightsAndDirectionDomainsAreRejected)
{
    auto law=SpaceCurve(); law.SignedDistance=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(Status::InvalidInput,service->Evaluate(law,{.2}).Status);
    law=SpaceCurve();
    NURBS3 c;c.Poles=std::get<Bezier3>(law.Source.Curve).Poles;c.Knots={0,1};c.Multiplicities={4,4};c.Weights={1,0,1,1};
    law.Source.Curve=c;
    EXPECT_EQ(Status::InvalidInput,service->Evaluate(law,{.2}).Status);
    law=SpaceCurve();
    BSpline3 field;field.Degree=1;field.Poles={{0,1,0},{0,1,1}};field.Knots={2,3};field.Multiplicities={2,2};
    law.ReferenceDirection=ParametricVectorField3{field};
    EXPECT_EQ(Status::InvalidInput,service->Evaluate(law,{.2}).Status);
}

TEST_F(CurveOffset3D, CancellationAndSampleBudgetReturnNoPartialResult)
{
    const auto law=SpaceCurve();CurveOffsetEvaluationOptions options;options.MaxSamples=1;
    EXPECT_EQ(Status::BudgetExceeded,service->Evaluate(law,{.2,.3},options).Status);
    options.MaxSamples=10; int calls=0; options.IsCancelled=[&]{return ++calls==3;};
    auto r=service->Evaluate(law,{.2,.3},options);
    EXPECT_EQ(Status::Cancelled,r.Status);EXPECT_TRUE(r.Samples.empty());
    options.IsCancelled={};
    EXPECT_EQ(Status::SamplesEvaluated,service->Evaluate(law,{.2,.3},options).Status);
}

TEST_F(CurveOffset3D, SharedServiceEvaluatesIndependentRequestsConcurrently)
{
    std::vector<std::future<CurveOffsetEvaluation3>> tasks;
    for(int i=0;i<8;++i) tasks.push_back(std::async(std::launch::async,[&,i]{auto law=SpaceCurve();law.SignedDistance=.1+i*.01;return service->Evaluate(law,{.37,.72});}));
    for(int i=0;i<8;++i){const auto r=tasks[i].get();ASSERT_EQ(Status::SamplesEvaluated,r.Status);Near(r.Samples[0].OffsetPoint,AnalyticOffset(.37,.1+i*.01,false));}
}

TEST_F(CurveOffset3D, SparseSamplesDoNotCertifyAnUnrequestedInteriorSingularity)
{
    auto law=SpaceCurve();
    // D(u)=(0,(u-.5)^2,0) vanishes between the requested endpoints.
    law.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,.25,0},{0,-.25,0},{0,.25,0}}}};
    const auto ends=service->Evaluate(law,{0,1});
    ASSERT_EQ(Status::SamplesEvaluated,ends.Status);
    EXPECT_EQ(ECurveOffsetCoverage::RequestedParametersOnly,ends.Coverage);
    EXPECT_EQ(Status::DegenerateDirection,service->Evaluate(law,{.5}).Status);
}

namespace {
    BSpline3 PeriodicCubic() {
        BSpline3 c;c.Periodic=true;c.Degree=3;
        c.Poles={{10,0,0},{0,10,2},{-10,0,0},{0,-10,-2}};
        c.Knots={2,3,4,5,6};c.Multiplicities={1,1,1,1,1};return c;
    }
    void CheckOffsetDerivative(const ICurveOffset3DService& service,const DirectionalCurveOffset3& law,double u) {
        constexpr double h=1e-5;
        const auto r=service.Evaluate(law,{u-h,u-h*.5,u,u+h*.5,u+h});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
        const auto& a=r.Samples[0].OffsetPoint;const auto& b=r.Samples[4].OffsetPoint;const auto& d=r.Samples[2].OffsetDerivative;
        const auto& c=r.Samples[1].OffsetPoint;const auto& e=r.Samples[3].OffsetPoint;
        // A C2 spline's offset is C1 at knots; central differences have an O(h)
        // term there. Richardson cancellation tests the common derivative.
        EXPECT_NEAR(d.X,2*(e.X-c.X)/h-(b.X-a.X)/(2*h),2e-7);
        EXPECT_NEAR(d.Y,2*(e.Y-c.Y)/h-(b.Y-a.Y)/(2*h),2e-7);
        EXPECT_NEAR(d.Z,2*(e.Z-c.Z)/h-(b.Z-a.Z)/(2*h),2e-7);
    }
}

TEST_F(CurveOffset3D, PeriodicBSplineKeepsUnwrappedParameterAndSeamDerivatives)
{
    DirectionalCurveOffset3 law;law.Source.Curve=PeriodicCubic();law.Source.Range={-2,10,false};law.SignedDistance=.2;
    const auto r=service->Evaluate(law,{-2,2,6,10,2.37,6.37});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
    Near(r.Samples[1].SourcePoint,{0,20./3,4./3});
    for(int i=1;i<4;++i) {
        Near(r.Samples[0].OffsetPoint,r.Samples[i].OffsetPoint);
        const auto& a=r.Samples[0].OffsetDerivative;const auto& b=r.Samples[i].OffsetDerivative;
        Near({a.X,a.Y,a.Z},{b.X,b.Y,b.Z});
    }
    Near(r.Samples[4].OffsetPoint,r.Samples[5].OffsetPoint);
    EXPECT_DOUBLE_EQ(6.37,r.Samples[5].SourceParameter);
    CheckOffsetDerivative(*service,law,6);CheckOffsetDerivative(*service,law,5.72);
    law.Source.Range={5.5,9.5,true};CompositeOffsetOptions3 o;o.Tolerance=1e-4;
    const auto built=service->BuildComposite({law},true,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;
    ASSERT_EQ(1u,built.Curve.Segments.size());EXPECT_DOUBLE_EQ(5.5,PieceRange(built.Curve.Segments[0]).First);
    EXPECT_DOUBLE_EQ(9.5,PieceRange(built.Curve.Segments[0]).Last);EXPECT_TRUE(PieceRange(built.Curve.Segments[0]).Reversed);Connected(*service,built.Curve);
}

TEST_F(CurveOffset3D, PeriodicNURBSUsesWeightsWithPeriodicToolAxisField)
{
    const auto basis=PeriodicCubic();NURBS3 c;c.Degree=basis.Degree;c.Poles=basis.Poles;c.Knots=basis.Knots;c.Multiplicities=basis.Multiplicities;c.Periodic=true;c.Weights={1,2,.5,1.5};
    auto field=basis;field.Poles={{.1,0,1},{0,.2,1},{-.1,0,1},{0,-.2,1}};
    DirectionalCurveOffset3 law;law.Source.Curve=c;law.Source.Range={4,8,false};law.SignedDistance=.1;law.ReferenceDirection=ParametricVectorField3{field};
    auto r=service->Evaluate(law,{4,6,8});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;Near(r.Samples[0].OffsetPoint,r.Samples[2].OffsetPoint);
    for(const auto& s:r.Samples)EXPECT_NEAR(.1,Distance(s.SourcePoint,s.OffsetPoint),1e-12);
    CheckOffsetDerivative(*service,law,6);CheckOffsetDerivative(*service,law,5.7);
    CompositeOffsetOptions3 o;o.Tolerance=1e-4;auto built=service->BuildComposite({law},true,o);
    ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;Connected(*service,built.Curve);
    law.Source.Range={4.2,4.8,false};const auto weighted=service->Evaluate(law,{4.5});ASSERT_EQ(Status::SamplesEvaluated,weighted.Status);
    c.Weights={1,1,1,1};law.Source.Curve=c;const auto uniform=service->Evaluate(law,{4.5});ASSERT_EQ(Status::SamplesEvaluated,uniform.Status);
    EXPECT_GT(Distance(weighted.Samples[0].SourcePoint,uniform.Samples[0].SourcePoint),.1);
}

TEST_F(CurveOffset3D, PeriodicRationalCircleSplitsRepeatedKnotsAndClosesAcrossSeam)
{
    NURBS3 c;c.Periodic=true;c.Degree=2;c.Knots={0,1,2,3,4};c.Multiplicities={2,2,2,2,2};
    c.Poles={{10,0,0},{10,10,0},{0,10,0},{-10,10,0},{-10,0,0},{-10,-10,0},{0,-10,0},{10,-10,0}};
    const double w=std::sqrt(.5);c.Weights={1,w,1,w,1,w,1,w};
    DirectionalCurveOffset3 law;law.Source.Curve=c;law.Source.Range={3.5,7.5,false};law.SignedDistance=.25;
    EXPECT_EQ(Status::InsufficientContinuity,service->Evaluate(law,{4}).Status);
    CompositeOffsetOptions3 o;o.Tolerance=1e-4;const auto r=service->BuildComposite({law},true,o);
    ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;ASSERT_EQ(5u,r.Curve.Segments.size());Connected(*service,r.Curve);
    for(const auto& p:r.Curve.Segments) {
        ASSERT_TRUE(p.SourceSegment);EXPECT_EQ(0u,*p.SourceSegment);const auto range=PieceRange(p);
        const auto v=service->EvaluatePiece(p,{range.First,(range.First+range.Last)*.5,range.Last});ASSERT_EQ(Status::SamplesEvaluated,v.Status)<<v.Diagnostic;
        for(const auto& s:v.Samples) {
            EXPECT_NEAR(10,std::hypot(s.SourcePoint.X,s.SourcePoint.Y),1e-10);
            EXPECT_NEAR(9.75,std::hypot(s.OffsetPoint.X,s.OffsetPoint.Y),1e-10);
        }
    }
}

TEST_F(CurveOffset3D, PeriodicCornerSeamUsesLeftAndRightDerivativesBeforeJoining)
{
    BSpline3 c;c.Periodic=true;c.Degree=1;c.Poles={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
    c.Knots={2,3,4,5,6};c.Multiplicities={1,1,1,1,1};
    DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={5.5,9.5,false};l.SignedDistance=1;
    auto r=service->BuildComposite({l},true);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    EXPECT_EQ(4u,r.TrimmedJoints);Connected(*service,r.Curve);
    for(const auto& p:r.Curve.Segments)EXPECT_GE(PieceRange(p).First,5.5);
    l.Source.Range.Reversed=true;r=service->BuildComposite({l},true);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;Connected(*service,r.Curve);
}

TEST_F(CurveOffset3D, PeriodicLinearAxisFieldSplitsAtShiftedDerivativeBreaks)
{
    BSpline3 axis;axis.Degree=1;axis.Periodic=true;axis.Knots={0,1,2,3,4};axis.Multiplicities={1,1,1,1,1};axis.Poles={{0,0,1},{0,.2,1},{0,0,1},{0,-.2,1}};
    DirectionalCurveOffset3 l;l.Source.Curve=Line3{};l.Source.Range={3.5,7.5,false};l.ReferenceDirection=ParametricVectorField3{axis};l.SignedDistance=.2;
    // Default Line3 direction is Z, choose X so the axis cannot become parallel.
    std::get<Line3>(l.Source.Curve).Axis.Direction={1,0,0};
    EXPECT_EQ(Status::InsufficientContinuity,service->Evaluate(l,{4.5}).Status);
    const auto r=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    EXPECT_EQ(5u,r.Curve.Segments.size());Connected(*service,r.Curve);
}

TEST_F(CurveOffset3D, NonuniformPeriodicDecimalKnotImagesHaveCorrectOneSidedLimits)
{
    BSpline3 c;c.Periodic=true;c.Degree=1;c.Poles={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};
    c.Knots={.1,.43,.98,1.73,2.2};c.Multiplicities={1,1,1,1,1};
    const double period=c.Knots.back()-c.Knots.front();DirectionalCurveOffset3 l;l.Source.Curve=c;l.SignedDistance=.3;
    for(int turn:{-3,0,4}) {
        const double shift=turn*period;
        l.Source.Range={c.Knots[0]+shift,c.Knots[1]+shift,false};
        const auto a=service->Evaluate(l,{l.Source.Range.First,l.Source.Range.Last});ASSERT_EQ(Status::SamplesEvaluated,a.Status)<<a.Diagnostic;
        Near(a.Samples[0].OffsetPoint,{0,.3,0});Near(a.Samples[1].OffsetPoint,{10,.3,0});
        l.Source.Range={c.Knots[1]+shift,c.Knots[2]+shift,false};
        const auto b=service->Evaluate(l,{l.Source.Range.First,l.Source.Range.Last});ASSERT_EQ(Status::SamplesEvaluated,b.Status)<<b.Diagnostic;
        Near(b.Samples[0].OffsetPoint,{9.7,0,0});Near(b.Samples[1].OffsetPoint,{9.7,10,0});
        l.Source.Range={c.Knots.front()+shift,c.Knots.back()+shift,false};
        const auto built=service->BuildComposite({l},true);ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;Connected(*service,built.Curve);
    }
}

TEST_F(CurveOffset3D, InvalidPeriodicDataAndExcessiveTurnsFailAtomically)
{
    auto c=PeriodicCubic();DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={2,6,false};
    c.Multiplicities.back()=2;l.Source.Curve=c;EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{3}).Status);
    c=PeriodicCubic();c.Poles.pop_back();l.Source.Curve=c;EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{3}).Status);
    c=PeriodicCubic();c.Degree=1;l.Source.Curve=c;l.Source.Range={2,1000002,false};
    const auto r=service->BuildComposite({l},false);EXPECT_EQ(ECompositeOffsetStatus3::BudgetExceeded,r.Status);EXPECT_TRUE(r.Curve.Segments.empty());
}

TEST_F(CurveOffset3D, PolylineInputKeepsPointIndexUAndOneSidedCornerTangents)
{
    DirectionalCurveOffset3 l;l.Source.Curve=Polyline3{{{0,0,0},{10,0,0},{10,10,0}},false};l.SignedDistance=1;l.Source.Range={0,1,false};
    auto a=service->Evaluate(l,{0,.5,1});ASSERT_EQ(Status::SamplesEvaluated,a.Status)<<a.Diagnostic;
    Near(a.Samples.back().OffsetPoint,{10,1,0});
    l.Source.Range={1,2,false};auto b=service->Evaluate(l,{1,1.5,2});ASSERT_EQ(Status::SamplesEvaluated,b.Status)<<b.Diagnostic;Near(b.Samples.front().OffsetPoint,{9,0,0});
    l.Source.Range={0,2,false};const auto r=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(2u,r.Curve.Segments.size());EXPECT_EQ(1u,r.TrimmedJoints);EXPECT_NEAR(.9,PieceRange(r.Curve.Segments[0]).Last,1e-8);EXPECT_NEAR(1.1,PieceRange(r.Curve.Segments[1]).First,1e-8);Connected(*service,r.Curve);
    for(const auto& p:r.Curve.Segments) {EXPECT_EQ(0u,*p.SourceSegment);EXPECT_TRUE(std::holds_alternative<Polyline3>(std::get<DirectionalCurveOffset3>(p.Geometry).Source.Curve));}
}

TEST_F(CurveOffset3D, ClosedPolylineSupportsImplicitAndExplicitClosingEdgesAndReversal)
{
    Polyline3 c{{{0,0,0},{10,0,0},{10,10,0},{0,10,0}},true};
    for(bool explicitClose:{false,true})for(bool reverse:{false,true}) {
        auto curve=c;if(explicitClose)curve.Points.push_back(curve.Points.front());
        DirectionalCurveOffset3 l;l.Source.Curve=curve;l.Source.Range={0,4,reverse};l.SignedDistance=-1;
        CompositeOffsetOptions3 o;o.Join=EOffsetJoin3::Round;o.Tolerance=1e-4;
        const auto r=service->BuildComposite({l},true,o);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
        EXPECT_EQ(4u,r.AddedConnectors);Connected(*service,r.Curve);
    }
}

TEST_F(CurveOffset3D, PolylineVariableAxisUsesGlobalUAndDegenerateLegFails)
{
    DirectionalCurveOffset3 l;l.Source.Curve=Polyline3{{{0,0,0},{10,0,0},{10,10,0}},false};l.Source.Range={1,2,false};l.SignedDistance=.5;
    BSpline3 axis;axis.Degree=1;axis.Knots={0,2};axis.Multiplicities={2,2};axis.Poles={{0,0,1},{1,0,1}};l.ReferenceDirection=ParametricVectorField3{axis};
    auto r=service->Evaluate(l,{1.5});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
    const double scale=std::sqrt(1+.75*.75);Near(r.Samples[0].OffsetPoint,{10-.5/scale,5,.375/scale});CheckOffsetDerivative(*service,l,1.5);
    std::get<Polyline3>(l.Source.Curve).Points[2]={10,0,0};r=service->Evaluate(l,{1.5});EXPECT_EQ(Status::DegenerateTangent,r.Status);EXPECT_TRUE(r.Samples.empty());
}

TEST_F(CurveOffset3D, RayUsesNativeDistanceAndEnforcesFirstParameter)
{
    DirectionalCurveOffset3 l;Ray3 ray;ray.Axis.Location={1,2,3};ray.Axis.Direction={1,0,0};ray.First=5;l.Source.Curve=ray;l.Source.Range={5,9,false};l.SignedDistance=.3;
    auto r=service->Evaluate(l,{5,7,9});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;Near(r.Samples[1].SourcePoint,{8,2,3});Near(r.Samples[1].OffsetPoint,{8,2.3,3});
    const auto built=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;EXPECT_DOUBLE_EQ(5,PieceRange(built.Curve.Segments.front()).First);
    l.Source.Range.First=4.9;EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{7}).Status);
    l.Source.Range={5,std::numeric_limits<double>::infinity(),false};EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{7}).Status);
}

TEST_F(CurveOffset3D, ClothoidMatchesFresnelReferenceAndAnalyticOffsetDerivative)
{
    // k(s)=pi*s: P(1)=(C(1),S(1)), reference Fresnel integral constants.
    Clothoid3 c;c.Length=1;c.StartCurvature=0;c.EndCurvature=std::acos(-1.);
    DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={0,1,false};l.SignedDistance=.1;
    const auto r=service->Evaluate(l,{0,.5,1});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
    Near(r.Samples[2].SourcePoint,{.779893400376822829,.438259147390354766,0},2e-11);
    Near(r.Samples[2].OffsetPoint,{.679893400376822829,.438259147390354766,0},2e-11);
    Near(r.Samples[0].OffsetPoint,{0,.1,0});
    for(const auto& s:r.Samples) {
        const double angle=.5*c.EndCurvature*s.SourceParameter*s.SourceParameter,speed=1-.1*c.EndCurvature*s.SourceParameter;
        EXPECT_NEAR(speed*std::cos(angle),s.OffsetDerivative.X,1e-12);EXPECT_NEAR(speed*std::sin(angle),s.OffsetDerivative.Y,1e-12);
    }
    CheckOffsetDerivative(*service,l,.73);
    c.EndCurvature=-c.EndCurvature;l.Source.Curve=c;const auto negative=service->Evaluate(l,{1});ASSERT_EQ(Status::SamplesEvaluated,negative.Status);
    Near(negative.Samples[0].SourcePoint,{.779893400376822829,-.438259147390354766,0},2e-11);
}

TEST_F(CurveOffset3D, ClothoidLineCircleAndNearConstantCurvatureLimits)
{
    Clothoid3 c;c.Length=10;DirectionalCurveOffset3 l;l.Source.Range={0,10,false};l.SignedDistance=.1;
    for(double k:{0.,1e-14,.2,-.2}) {
        c.StartCurvature=c.EndCurvature=k;l.Source.Curve=c;const auto r=service->Evaluate(l,{0,3,10});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
        if(k==0)Near(r.Samples.back().OffsetPoint,{10,.1,0});
        else {const double a=k*10;Near(r.Samples.back().SourcePoint,{std::sin(a)/k,2*std::sin(a/2)*std::sin(a/2)/k,0});}
        const auto reference=r.Samples.back().OffsetPoint;c.EndCurvature+=1e-13;l.Source.Curve=c;
        const auto nearby=service->Evaluate(l,{10});ASSERT_EQ(Status::SamplesEvaluated,nearby.Status)<<nearby.Diagnostic;Near(nearby.Samples[0].OffsetPoint,reference,2e-11);
    }
}

TEST_F(CurveOffset3D, ClothoidVariableAxisAndSpatialPlacementPreserveNativeLaw)
{
    Clothoid3 c;c.Length=3;c.StartCurvature=-.2;c.EndCurvature=.3;
    c.Placement.Location={7,-4,2};c.Placement.XDirection={0,1,0};c.Placement.YDirection={0,0,1};c.Placement.ZDirection={1,0,0};
    DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={.2,2.8,true};l.SignedDistance=.15;
    BSpline3 axis;axis.Degree=1;axis.Knots={0,3};axis.Multiplicities={2,2};axis.Poles={{1,0,0},{1,0,.2}};l.ReferenceDirection=ParametricVectorField3{axis};
    CheckOffsetDerivative(*service,l,1.3);const auto r=service->BuildComposite({l},false);ASSERT_EQ(ECompositeOffsetStatus3::Built,r.Status)<<r.Diagnostic;
    ASSERT_EQ(1u,r.Curve.Segments.size());const auto& law=std::get<DirectionalCurveOffset3>(r.Curve.Segments[0].Geometry);EXPECT_TRUE(std::holds_alternative<Clothoid3>(law.Source.Curve));EXPECT_TRUE(law.Source.Range.Reversed);
    const auto samples=service->EvaluatePiece(r.Curve.Segments[0],{.2,1.3,2.8});ASSERT_EQ(Status::SamplesEvaluated,samples.Status);
    for(const auto& s:samples.Samples)EXPECT_NEAR(.15,Distance(s.SourcePoint,s.OffsetPoint),1e-12);
}

TEST_F(CurveOffset3D, ClothoidInvalidRangeCuspBudgetAndCancellationAreAtomic)
{
    Clothoid3 c;c.Length=2;c.EndCurvature=1;DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={0,2,false};l.SignedDistance=1;
    auto r=service->Evaluate(l,{0,2});EXPECT_EQ(Status::SingularOffset,r.Status);EXPECT_TRUE(r.Samples.empty());
    l.SignedDistance=.1;CurveOffsetEvaluationOptions o;o.MaxIntegrationEvaluations=8;r=service->Evaluate(l,{0,2},o);EXPECT_EQ(Status::BudgetExceeded,r.Status);EXPECT_TRUE(r.Samples.empty());
    o={};int calls=0;o.IsCancelled=[&]{return ++calls>15;};r=service->Evaluate(l,{0,2},o);EXPECT_EQ(Status::Cancelled,r.Status);EXPECT_TRUE(r.Samples.empty());
    l.Source.Range.Last=2.1;EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{1}).Status);
    l.Source.Range.Last=2;c.Length=0;l.Source.Curve=c;EXPECT_EQ(Status::InvalidInput,service->Evaluate(l,{1}).Status);
}

TEST_F(CurveOffset3D, ClothoidOscillationAndInitialCurvatureMatchIndependentSimpsonIntegral)
{
    Clothoid3 c;c.Length=20;c.StartCurvature=3;c.EndCurvature=-5;
    DirectionalCurveOffset3 l;l.Source.Curve=c;l.Source.Range={0,20,false};l.SignedDistance=.01;
    const auto r=service->Evaluate(l,{3,7.5,20});ASSERT_EQ(Status::SamplesEvaluated,r.Status)<<r.Diagnostic;
    // Independent high-resolution composite Simpson rule, not the production
    // phase-bounded adaptive Gauss quadrature. Also crosses zero curvature.
    for(const auto& sample:r.Samples) {
        constexpr int count=20000;const double step=sample.SourceParameter/count;
        long double x=0,y=0;
        for(int i=0;i<=count;++i) {
            const long double s=i*static_cast<long double>(step),angle=3*s-.2L*s*s;
            const int w=(i==0||i==count)?1:(i%2?4:2);x+=w*std::cos(angle);y+=w*std::sin(angle);
        }
        Near(sample.SourcePoint,{static_cast<double>(x*step/3),static_cast<double>(y*step/3),0},2e-10);
    }
}

TEST_F(CurveOffset3D, SurfaceNormalFieldUsesExactSurfaceDerivativeAndPersists)
{
    const double phi=.3,r=5,d=.2;DirectionalCurveOffset3 law;Circle3 circle;circle.Radius=r*std::cos(phi);circle.Placement.Location.Z=r*std::sin(phi);
    law.Source={circle,{0,6.283185307179586,false}};SphericalSurface3 sphere;sphere.Radius=r;
    law.ReferenceDirection=SurfaceNormalField3{sphere,Line2{{{0,phi},{1,0}}},1};law.SignedDistance=d;
    const auto result=service->Evaluate(law,{0,.7,2.,6.});ASSERT_EQ(Status::SamplesEvaluated,result.Status)<<result.Diagnostic;
    const double radius=r*std::cos(phi)-d*std::sin(phi),z=r*std::sin(phi)+d*std::cos(phi);
    for(const auto& s:result.Samples){const auto u=s.SourceParameter;Near(s.OffsetPoint,{radius*std::cos(u),radius*std::sin(u),z},1e-10);
        EXPECT_NEAR(s.OffsetDerivative.X,-radius*std::sin(u),1e-10);EXPECT_NEAR(s.OffsetDerivative.Y,radius*std::cos(u),1e-10);EXPECT_NEAR(s.OffsetDerivative.Z,0,1e-10);}
    Persistence::Writer writer;writer.Write(law);Persistence::Reader reader(writer.Bytes);DirectionalCurveOffset3 restored;reader.Read(restored);EXPECT_TRUE(reader.End());
    const auto again=service->Evaluate(restored,{.7});ASSERT_EQ(Status::SamplesEvaluated,again.Status);Near(again.Samples.front().OffsetPoint,result.Samples[1].OffsetPoint,1e-12);
    auto bad=law;std::get<SurfaceNormalField3>(bad.ReferenceDirection).Orientation=0;EXPECT_EQ(Status::InvalidInput,service->Evaluate(bad,{.2}).Status);
    std::get<Circle3>(bad.Source.Curve).Radius+=1;bad.ReferenceDirection=law.ReferenceDirection;EXPECT_EQ(Status::InvalidInput,service->Evaluate(bad,{.2}).Status);
}
TEST_F(CurveOffset3D, OffsetTraversalUsesPhysicalLengthForGapOvercutAndReverse)
{
    DirectionalCurveOffset3 law;Circle3 circle;circle.Radius=4;law.Source={circle,{0,6.283185307179586,false}};law.ReferenceDirection=Direction3{0,0,1};law.SignedDistance=.1;
    const auto built=service->BuildComposite({law},true);ASSERT_EQ(ECompositeOffsetStatus3::Built,built.Status)<<built.Diagnostic;
    for(bool reverse:{false,true})for(double extra:{-1.,0.,2.}){
        OffsetTraversal::Evaluator evaluator(*service);const auto perimeter=evaluator.Length(built.Curve);EXPECT_NEAR(2*3.14159265358979323846*3.9,perimeter,1e-8);
        const auto trace=evaluator.Trace(built.Curve,{0,1.2},perimeter+extra,reverse);EXPECT_NEAR(perimeter+extra,evaluator.Length(trace),1e-7);
        const auto samples=evaluator.Sample(trace,.005,.2);const auto& last=samples.Points.back().Value.OffsetPoint;const double angle=1.2+(reverse?-1:1)*(perimeter+extra)/3.9;
        Near(last,{3.9*std::cos(angle),3.9*std::sin(angle),0},1e-8);
        EXPECT_TRUE(std::all_of(trace.Segments.begin(),trace.Segments.end(),[](const auto& s){return s.SourceSegment==0&&std::holds_alternative<DirectionalCurveOffset3>(s.Geometry);}));
    }
    OffsetTraversal::Evaluator cancelled(*service,[]{return true;});EXPECT_THROW(cancelled.Sample(built.Curve),std::exception);
}

TEST_F(CurveOffset3D, TraversalSnapsOnlyEndpointRoundoffOnReversedNativeRanges)
{
    for(const auto range:std::vector<ParameterRange>{{.1,.3,false},{-.7,2.31,true},{2.119435598,6.283185307179586,true}}){
        CompositeOffsetCurve3 curve;curve.Segments.push_back({CurveSegment3{Line3{{{1,2,3},{1,0,0}}},range},{}});
        OffsetTraversal::Evaluator evaluator(*service);const auto samples=evaluator.Sample(curve,.01,.03);ASSERT_GT(samples.Points.size(),1u);
        for(const auto& s:samples.Points){EXPECT_GE(s.Value.SourceParameter,range.First);EXPECT_LE(s.Value.SourceParameter,range.Last);}
        EXPECT_NEAR(range.Last-range.First,evaluator.Length(curve),1e-10);
        EXPECT_THROW(evaluator.At(curve.Segments.front(),range.Last+.001),std::exception);
    }
}
TEST_F(CurveOffset3D, BatchedTraversalRetainsExactSamplesNativeSidesAndFailureChecks)
{
    BSpline3 corner;corner.Degree=1;corner.Poles={{0,0,0},{10,0,0},{10,10,0}};corner.Knots={2,2.37,5};corner.Multiplicities={2,1,2};
    auto law=SpaceCurve();std::vector<OffsetCurvePiece3> pieces{{law,0},{CurveSegment3{corner,{2,5,true}},0}};
    Clothoid3 clothoid;clothoid.Length=2;clothoid.StartCurvature=.1;clothoid.EndCurvature=.2;
    pieces.push_back({CurveSegment3{clothoid,{0,2,false}},0});
    for(const auto& piece:pieces)
    {
        const auto range=OffsetTraversal::Range(piece);const std::vector<double> parameters{range.First,(range.First+range.Last)/2,range.Last};
        OffsetTraversal::Evaluator batch(*service),single(*service);const auto values=batch.AtMany(piece,parameters);ASSERT_EQ(parameters.size(),values.size());
        const auto equal=[](const auto& a,const auto& b){EXPECT_DOUBLE_EQ(a.X,b.X);EXPECT_DOUBLE_EQ(a.Y,b.Y);EXPECT_DOUBLE_EQ(a.Z,b.Z);};
        for(std::size_t i=0;i<values.size();++i){const auto expected=single.At(piece,parameters[i]);EXPECT_DOUBLE_EQ(expected.SourceParameter,values[i].SourceParameter);
            equal(expected.SourcePoint,values[i].SourcePoint);equal(expected.OffsetPoint,values[i].OffsetPoint);equal(expected.SourceDerivative,values[i].SourceDerivative);equal(expected.OffsetDerivative,values[i].OffsetDerivative);
            equal(expected.ReferenceDirection,values[i].ReferenceDirection);equal(expected.OffsetDirection,values[i].OffsetDirection);}
        EXPECT_THROW(batch.AtMany(piece,{range.First,range.Last+.001}),std::exception);
        OffsetTraversal::Evaluator cancelled(*service,[]{return true;});EXPECT_THROW(cancelled.AtMany(piece,parameters),std::exception);
    }
}

TEST_F(CurveOffset3D, NominalTraversalSplitsInternalCornersWithoutChangingCurveIdentity)
{
    BSpline3 spline;spline.Degree=1;spline.Poles={{0,0,0},{10,0,0},{10,10,0}};spline.Knots={2,2.37,5};spline.Multiplicities={2,1,2};
    for(bool reverse:{false,true}){
        CompositeOffsetCurve3 curve;curve.Segments.push_back({CurveSegment3{spline,{2,5,reverse}},7});
        OffsetTraversal::Evaluator evaluator(*service);EXPECT_NEAR(evaluator.Length(curve),20,1e-8);
        OffsetTraversal::Evaluator::Piece bounded(evaluator,curve.Segments[0]);EXPECT_THROW(bounded.Length(1,5),std::invalid_argument);
        const auto samples=evaluator.Sample(curve,.01,.5);int x=0,y=0;
        for(const auto& sample:samples.Points){EXPECT_EQ(sample.PieceIndex,0u);if(sample.Value.SourceParameter==2.37){Near(sample.Value.OffsetPoint,{10,0,0});x+=std::abs(sample.Value.SourceDerivative.Y)<1e-8;y+=std::abs(sample.Value.SourceDerivative.X)<1e-8;}}
        EXPECT_EQ(x,1);EXPECT_EQ(y,1);
        const auto trace=evaluator.Trace(curve,{0,reverse?5.:2.},15,false);EXPECT_NEAR(evaluator.Length(trace),15,1e-8);
        const auto traced=evaluator.Sample(trace,.01,.5);Near(traced.Points.back().Value.OffsetPoint,reverse?Point3{5,0,0}:Point3{10,5,0},1e-8);
        ASSERT_EQ(trace.Segments.size(),1u);EXPECT_EQ(trace.Segments[0].SourceSegment,7u);EXPECT_TRUE(std::holds_alternative<CurveSegment3>(trace.Segments[0].Geometry));
    }
    NURBS3 rational;rational.Degree=2;rational.Poles={{0,0,0},{2,0,0},{4,0,0},{4,2,0},{4,4,0}};rational.Weights={1,.7,1,.8,1};rational.Knots={2,2.37,5};rational.Multiplicities={3,2,3};
    CompositeOffsetCurve3 curve;curve.Segments.push_back({CurveSegment3{rational,{2,5,false}},0});OffsetTraversal::Evaluator evaluator(*service);EXPECT_NEAR(evaluator.Length(curve),8,1e-8);EXPECT_NO_THROW(evaluator.Sample(curve,.01,.25));
}

TEST_F(CurveOffset3D, NominalPeriodicTraversalKeepsUnwrappedKnotsAndClosedBoundary)
{
    BSpline3 spline;spline.Periodic=true;spline.Degree=1;spline.Poles={{0,0,0},{10,0,0},{10,10,0},{0,10,0}};spline.Knots={2,3,4,5,6};spline.Multiplicities={1,1,1,1,1};
    for(bool reverse:{false,true}){CompositeOffsetCurve3 curve;curve.Closed=true;curve.Segments.push_back({CurveSegment3{spline,{5.5,9.5,reverse}},0});OffsetTraversal::Evaluator evaluator(*service);EXPECT_NEAR(evaluator.Length(curve),40,1e-8);const auto samples=evaluator.Sample(curve,.01,.5);Near(samples.Points.front().Value.OffsetPoint,samples.Points.back().Value.OffsetPoint);for(const auto& p:samples.Points){EXPECT_EQ(p.PieceIndex,0u);EXPECT_GE(p.Value.SourceParameter,5.5);EXPECT_LE(p.Value.SourceParameter,9.5);}}
}

TEST_F(CurveOffset3D, PointsOnlyTraversalPreservesEveryExactSampleAndNativeSide)
{
    BSpline3 corner;corner.Degree=1;corner.Poles={{0,0,0},{10,0,0},{10,10,0}};corner.Knots={2,2.37,5};corner.Multiplicities={2,1,2};
    CompositeOffsetCurve3 reversedCorner;
    reversedCorner.Segments.push_back({CurveSegment3{corner,{2,5,true}},7});
    reversedCorner.Segments.push_back({CurveSegment3{Segment3{{0,0,0},{-2,0,0}},{0,1,false}},8});
    DirectionalCurveOffset3 periodic;periodic.Source={PeriodicCubic(),{5.5,9.5,true}};periodic.SignedDistance=.2;
    CompositeOffsetCurve3 periodicCurve;periodicCurve.Closed=true;periodicCurve.Segments.push_back({periodic,3});
    Clothoid3 clothoid;clothoid.Length=2;clothoid.StartCurvature=.1;clothoid.EndCurvature=.2;
    DirectionalCurveOffset3 clothoidLaw;clothoidLaw.Source={clothoid,{0,2,true}};clothoidLaw.SignedDistance=.05;
    CompositeOffsetCurve3 clothoidCurve;clothoidCurve.Segments.push_back({clothoidLaw,9});
    auto spatial=SpaceCurve();VaryDirection(spatial);CompositeOffsetCurve3 spatialCurve;spatialCurve.Segments.push_back({spatial,11});
    const std::vector<CompositeOffsetCurve3> cases{reversedCorner,periodicCurve,clothoidCurve,spatialCurve};
    const auto equal=[](double a,double b){EXPECT_EQ(std::bit_cast<std::uint64_t>(a),std::bit_cast<std::uint64_t>(b));};
    const auto equalVector=[&](const auto& a,const auto& b){equal(a.X,b.X);equal(a.Y,b.Y);equal(a.Z,b.Z);};
    for(std::size_t index=0;index<cases.size();++index)
    {
        SCOPED_TRACE(index);OffsetTraversal::Evaluator full(*service),pointsOnly(*service);
        const auto expected=full.Sample(cases[index],.01,.5),actual=pointsOnly.SamplePoints(cases[index],.01,.5);
        ASSERT_EQ(expected.Points.size(),actual.Points.size());ASSERT_FALSE(actual.Points.empty());
        EXPECT_EQ(cases[index].Segments.size(),expected.Lengths.size());EXPECT_TRUE(actual.Lengths.empty());
        for(std::size_t i=0;i<actual.Points.size();++i)
        {
            SCOPED_TRACE(i);const auto& a=expected.Points[i];const auto& b=actual.Points[i];EXPECT_EQ(a.PieceIndex,b.PieceIndex);
            equal(a.Value.SourceParameter,b.Value.SourceParameter);equalVector(a.Value.SourcePoint,b.Value.SourcePoint);equalVector(a.Value.OffsetPoint,b.Value.OffsetPoint);
            equalVector(a.Value.SourceDerivative,b.Value.SourceDerivative);equalVector(a.Value.OffsetDerivative,b.Value.OffsetDerivative);
            equalVector(a.Value.ReferenceDirection,b.Value.ReferenceDirection);equalVector(a.Value.OffsetDirection,b.Value.OffsetDirection);
        }
    }
}

TEST_F(CurveOffset3D, PointsOnlyTraversalRetainsCancellationAndConnectivityFailures)
{
    CompositeOffsetCurve3 disconnected;
    disconnected.Segments.push_back({CurveSegment3{Segment3{{0,0,0},{1,0,0}},{0,1,false}},0});
    disconnected.Segments.push_back({CurveSegment3{Segment3{{2,0,0},{3,0,0}},{0,1,false}},1});
    auto openSeam=disconnected;openSeam.Segments.resize(1);openSeam.Closed=true;
    const auto failure=[&](const CompositeOffsetCurve3& curve,bool pointsOnly,bool cancel)
    {
        int calls=0;OffsetTraversal::Evaluator evaluator(*service,[&]{return cancel&&++calls>=4;});
        try{if(pointsOnly)(void)evaluator.SamplePoints(curve,.01,.5);else (void)evaluator.Sample(curve,.01,.5);}
        catch(const std::exception& error){return std::string(error.what());}
        ADD_FAILURE()<<"Traversal unexpectedly accepted invalid/cancelled input";return std::string{};
    };
    for(const auto& curve:{disconnected,openSeam})
    {
        const auto expected=failure(curve,false,false);ASSERT_FALSE(expected.empty());EXPECT_EQ(expected,failure(curve,true,false));
    }
    const auto expected=failure(openSeam,false,true);ASSERT_NE(std::string::npos,expected.find("cancelled"));EXPECT_EQ(expected,failure(openSeam,true,true));
}
