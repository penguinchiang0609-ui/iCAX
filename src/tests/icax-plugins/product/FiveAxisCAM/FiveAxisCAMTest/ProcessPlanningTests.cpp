#include "pch.h"
#include "../../../../../iCAX-Plugins/cam/ProcessPlanning/ProcessPlanningService.h"
#include "Services/ServiceProvider.h"
#include "Services/ServiceRegistrationCatalog.h"
#include <numbers>

namespace
{
    using namespace iCAX::CAM::Planning;
    using namespace iCAX::GeometryData;
    constexpr double Tau=2*std::numbers::pi;
    SProcessCompileInput CircleInput()
    {
        SProcessCompileInput in;in.IntentID="intent";in.SourceSpanID="circle-source";in.NominalCurve.Closed=true;
        Circle3 circle;circle.Radius=10;circle.Placement.Location={100,-200,30};
        in.NominalCurve.Segments.push_back({circle,{0,Tau,false}});in.KerfWidthMm=.2;
        in.Technology={20,1000,2,1,0,.1,.15,"N2"};in.LeadIn={"Line","ScrapSide",2,0};return in;
    }
    std::shared_ptr<ICamProcessPlanningService> Compiler()
    {
        if(GetProcessPlanningContractVersion()!=1)throw std::runtime_error("Unexpected process ABI");
        iCAX::Services::CServiceProvider services;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        return services.Resolve<ICamProcessPlanningService>();
    }
    Point3 Endpoint(const CurveSegment3& s,bool end)
    {
        const auto t=end?(s.Range.Reversed?s.Range.First:s.Range.Last):(s.Range.Reversed?s.Range.Last:s.Range.First);
        if(const auto* line=std::get_if<Segment3>(&s.Curve))return {line->Start.X+(line->End.X-line->Start.X)*t,line->Start.Y+(line->End.Y-line->Start.Y)*t,line->Start.Z+(line->End.Z-line->Start.Z)*t};
        const auto c=std::get<Circle3>(s.Curve);const auto& p=c.Placement;
        return {p.Location.X+c.Radius*(p.XDirection.X*std::cos(t)+p.YDirection.X*std::sin(t)),
            p.Location.Y+c.Radius*(p.XDirection.Y*std::cos(t)+p.YDirection.Y*std::sin(t)),
            p.Location.Z+c.Radius*(p.XDirection.Z*std::cos(t)+p.YDirection.Z*std::sin(t))};
    }
    double Distance(Point3 a,Point3 b){return std::hypot(a.X-b.X,a.Y-b.Y,a.Z-b.Z);}
}
TEST(ProcessPlanning, ExactCircleCompensationPreservesFinishedDiameterAndEmitsOrderedEvents)
{
    auto in=CircleInput();const auto out=Compiler()->Compile(in);ASSERT_EQ(2u,out.Path.Curve.Segments.size());
    const auto circle=std::get<Circle3>(out.Path.Curve.Segments[1].Curve);
    EXPECT_DOUBLE_EQ(20.,2*circle.Radius+in.KerfWidthMm);EXPECT_DOUBLE_EQ(9.9,out.CompensatedRadiusMm);
    EXPECT_EQ("LeadIn",out.Spans[0].Role);EXPECT_FALSE(out.Spans[0].SourceRange);EXPECT_EQ("circle-source",out.Spans[1].SourceSpanID);
    EXPECT_NEAR(2+Tau*9.9,out.TotalLengthMm,1e-12);EXPECT_EQ("LocalCircularBoundary",out.Scope);EXPECT_FALSE(out.RemainingChecks.empty());
    const std::vector<std::string> kinds{"ProcessSelect","GasOn","HeightFollow","Pierce","CutOn","CutOff","GasOff"};
    ASSERT_EQ(kinds.size(),out.Events.Events.size());for(size_t i=0;i<kinds.size();++i)EXPECT_EQ(kinds[i],out.Events.Events[i].Kind);
    EXPECT_EQ(0u,out.Events.Events[0].SegmentIndex);EXPECT_EQ(1u,out.Events.Events[5].SegmentIndex);
    EXPECT_EQ(0.,out.Events.Events[4].SegmentParameter);EXPECT_EQ(Tau,out.Events.Events[5].SegmentParameter);
    EXPECT_EQ(0.,Distance(Endpoint(out.Path.Curve.Segments[0],true),Endpoint(out.Path.Curve.Segments[1],false)));
    EXPECT_EQ(in.NominalCurve.Segments.size(),1u);EXPECT_DOUBLE_EQ(10.,std::get<Circle3>(in.NominalCurve.Segments[0].Curve).Radius);
}
TEST(ProcessPlanning, SourceAnchoredStartReverseAndOvercutRetainExactCircleAndOccurrences)
{
    auto in=CircleInput();in.StartParameter=1.2;in.NominalCurve.Segments[0].Range.Reversed=true;
    in.OvercutLengthMm=5;in.LeadOut={"Line","ScrapSide",1,0};const auto out=Compiler()->Compile(in);
    ASSERT_EQ(5u,out.Spans.size());EXPECT_EQ("Cut",out.Spans[1].Role);EXPECT_EQ("Cut",out.Spans[2].Role);EXPECT_EQ("Overcut",out.Spans[3].Role);
    EXPECT_TRUE(out.Spans[1].SourceRange->Reversed);EXPECT_EQ(0u,out.Spans[1].Occurrence);EXPECT_EQ(1u,out.Spans[2].Occurrence);
    EXPECT_NEAR(5,out.Spans[3].LengthMm,1e-12);EXPECT_NEAR(3+Tau*9.9+5,out.TotalLengthMm,1e-12);
    for(size_t i=1;i<out.Path.Curve.Segments.size();++i)EXPECT_LT(Distance(Endpoint(out.Path.Curve.Segments[i-1],true),Endpoint(out.Path.Curve.Segments[i],false)),1e-10);
    EXPECT_TRUE(std::holds_alternative<Circle3>(out.Path.Curve.Segments[3].Curve));
    EXPECT_EQ(1.,out.Events.Events[5].SegmentParameter); // lead-out endpoint, not nominal-source end
}
TEST(ProcessPlanning, CompensationRetainedSideAndWorldOrientationAreIndependentOfTraversal)
{
    auto in=CircleInput();in.RetainedSide="Interior";in.OutwardSign=-1;auto c=std::get<Circle3>(in.NominalCurve.Segments[0].Curve);
    c.Placement.XDirection={0,1,0};c.Placement.YDirection={0,0,1};c.Placement.ZDirection={1,0,0};in.NominalCurve.Segments[0].Curve=c;
    const auto out=Compiler()->Compile(in);EXPECT_DOUBLE_EQ(10.1,out.CompensatedRadiusMm);EXPECT_DOUBLE_EQ(20.,2*out.CompensatedRadiusMm-in.KerfWidthMm);
    EXPECT_DOUBLE_EQ(-1,out.AssumedToolDirection.X);EXPECT_DOUBLE_EQ(0,out.AssumedToolDirection.Z);
    const auto pierce=Endpoint(out.Path.Curve.Segments[0],false);EXPECT_NEAR(100,pierce.X,1e-12);EXPECT_NEAR(-187.9,pierce.Y,1e-12);
    in.NominalCurve.Segments[0].Range.Reversed=true;EXPECT_DOUBLE_EQ(out.CompensatedRadiusMm,Compiler()->Compile(in).CompensatedRadiusMm);
}
TEST(ProcessPlanning, WholeLeadAndPierceFootprintMustRemainInLocalScrap)
{
    auto in=CircleInput();in.LeadIn.AngleRad=std::numbers::pi;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.LeadIn.LengthMm=21;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.LeadIn={};EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument); // pierce footprint exceeds half kerf
    in.Technology.PierceFootprintRadiusMm=.1;EXPECT_NO_THROW(Compiler()->Compile(in));
    in=CircleInput();in.RetainedSide="Interior";in.LeadIn.AngleRad=std::numbers::pi;in.LeadIn.LengthMm=25;
    EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument); // both endpoints outside, but entire line crosses retained disk
}
TEST(ProcessPlanning, MissingTechnologyInvalidPoseAndOffsetCollapseRejectWithoutPartialResult)
{
    auto in=CircleInput();in.Technology.PowerW=std::numeric_limits<double>::quiet_NaN();EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.KerfWidthMm=20;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.LeadTiltRad=.1;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.KerfOwner="Executor";EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.NominalCurve.Closed=false;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.LeadIn.Kind="Arc";EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.StartParameter=10;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.NominalCurve.Segments[0].Range.Last=std::numbers::pi;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
}
TEST(ProcessPlanning, OvercutIsBoundedCancelableAndNeverBecomesTangentExtension)
{
    auto in=CircleInput();in.OvercutLengthMm=130;const auto out=Compiler()->Compile(in);double length=0;
    for(const auto& s:out.Spans)if(s.Role=="Overcut"){length+=s.LengthMm;EXPECT_TRUE(std::holds_alternative<Circle3>(out.Path.Curve.Segments[s.SegmentIndex].Curve));}
    EXPECT_NEAR(130,length,1e-10);in.MaximumSegments=2;EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();int checks=0;in.IsCancelled=[&]{return ++checks>=3;};EXPECT_THROW(Compiler()->Compile(in),std::runtime_error);
}
TEST(ProcessPlanning, TangentialArcLeadsAreExactAndCheckFullSweeps)
{
    auto in=CircleInput();in.LeadIn={"Arc","ScrapSide",std::numbers::pi,0,2};in.LeadOut=in.LeadIn;
    for(const auto* retained:{"Interior","Exterior"})for(const bool reversed:{false,true}) {
        in.RetainedSide=retained;in.NominalCurve.Segments[0].Range.Reversed=reversed;
        const auto result=Compiler()->Compile(in);ASSERT_EQ(3u,result.Spans.size());
        for(size_t i=1;i<result.Spans.size();++i)EXPECT_LT(Distance(Endpoint(result.Path.Curve.Segments[i-1],true),Endpoint(result.Path.Curve.Segments[i],false)),1e-10);
        const auto& entry=result.Path.Curve.Segments.front();const auto& exit=result.Path.Curve.Segments.back();
        EXPECT_TRUE(std::holds_alternative<Circle3>(entry.Curve));EXPECT_TRUE(std::holds_alternative<Circle3>(exit.Curve));
        EXPECT_DOUBLE_EQ(2.,std::get<Circle3>(entry.Curve).Radius);
        // Independent dense probes verify the analytic full-arc extrema checks.
        for(const auto& lead:{entry,exit})for(int j=0;j<=100;++j) {
            auto point=lead;point.Range.First=point.Range.Last=lead.Range.First+(lead.Range.Last-lead.Range.First)*j/100.;
            const double distance=Distance(Endpoint(point,false),std::get<Circle3>(in.NominalCurve.Segments[0].Curve).Placement.Location);
            if(in.RetainedSide=="Exterior")EXPECT_LE(distance,result.CompensatedRadiusMm+1e-10);
            else EXPECT_GE(distance,result.CompensatedRadiusMm-1e-10);
        }
    }
    in.RetainedSide="Exterior";in.LeadIn.RadiusMm=20;in.LeadIn.LengthMm=50;
    EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
    in=CircleInput();in.LeadIn={"Arc","ScrapSide",2,.1,2};EXPECT_THROW(Compiler()->Compile(in),std::invalid_argument);
}

TEST(ProcessPlanning, GapStopsEarlyAndAttachesLeadOutAtTheActualCutEnd)
{
    auto in=CircleInput();in.GapLengthMm=2;in.LeadOut={"Line","ScrapSide",1,0};
    const auto out=Compiler()->Compile(in);double length=0;for(const auto& s:out.Spans)if(s.Role=="Cut")length+=s.LengthMm;
    EXPECT_NEAR(2*std::numbers::pi*out.CompensatedRadiusMm-2,length,1e-9);EXPECT_FALSE(out.Path.Curve.Closed);
    EXPECT_EQ("LeadOut",out.Spans.back().Role);const auto& prior=out.Path.Curve.Segments[out.Spans[out.Spans.size()-2].SegmentIndex];
    const auto circle=std::get<iCAX::GeometryData::Circle3>(prior.Curve);const double end=prior.Range.Reversed?prior.Range.First:prior.Range.Last;
    const auto exit=std::get<iCAX::GeometryData::Segment3>(out.Path.Curve.Segments.back().Curve);
    EXPECT_NEAR(circle.Placement.Location.X+circle.Radius*std::cos(end),exit.Start.X,1e-9);
    EXPECT_NEAR(circle.Placement.Location.Y+circle.Radius*std::sin(end),exit.Start.Y,1e-9);
    in.OvercutLengthMm=1;EXPECT_THROW(Compiler()->Compile(in),std::exception);
    in.OvercutLengthMm=0;in.GapLengthMm=1000;EXPECT_THROW(Compiler()->Compile(in),std::exception);
}
