#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include <chrono>
#include <numbers>
#include <thread>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Data;
using namespace iCAX::Database;
using namespace iCAX::GeometryData;
SToolpathValue OffsetCircle()
{
    SToolpathValue value;value.Geometry.Closed=true;
    DirectionalCurveOffset3 law;law.Source={Circle3{Placement3{},10},{0,2*std::numbers::pi,false}};law.SignedDistance=1;law.ReferenceDirection=Direction3{0,0,1};
    value.Geometry.Segments.push_back({"circle",law,{}, {},"CadNative"});return value;
}
// An analytic test backend, deliberately limited to a constant-normal circular
// offset. It proves the generic graph retains an exact law; it is not a general
// offset implementation or a replacement for the product geometry backend.
class CExactCircularBackend final:public IToolpathGeometryServices
{
    std::shared_ptr<const IToolpathGeometryServices> Canonical=CreateToolpathGeometryServices();
public:
    Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter)const override
    {
        if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve))
        {
            const auto* circle=std::get_if<Circle3>(&offset->Source.Curve);const auto* normal=std::get_if<Direction3>(&offset->ReferenceDirection);
            if(!circle||!normal||normal->X!=0||normal->Y!=0||normal->Z!=1)throw std::invalid_argument("TOOLPATH_UNSUPPORTED_CURVE: test offset family");
            return Local(circle->Placement,(circle->Radius-offset->SignedDistance)*std::cos(parameter),(circle->Radius-offset->SignedDistance)*std::sin(parameter));
        }
        if(segment.Parameterization=="CadNative")
        {
            const auto& curve=std::get<CurveSegment3>(segment.Curve).Curve;const auto* polyline=std::get_if<Polyline3>(&curve);
            if(!polyline||parameter<0||parameter>polyline->Points.size()-1)throw std::invalid_argument("TOOLPATH_UNSUPPORTED_CURVE: test native family");
            const auto index=std::min(static_cast<std::size_t>(std::floor(parameter)),polyline->Points.size()-2);const auto fraction=parameter-index;
            const auto a=polyline->Points[index],b=polyline->Points[index+1];return {a.X+(b.X-a.X)*fraction,a.Y+(b.Y-a.Y)*fraction,a.Z+(b.Z-a.Z)*fraction};
        }
        return Canonical->Evaluate(geometry,segment,parameter);
    }
    void Validate(const SEditableGeometry& geometry)const override
    {
        try{Canonical->Validate(geometry);}catch(const std::exception& error){if(!std::string(error.what()).starts_with("TOOLPATH_UNSUPPORTED_CURVE:"))throw;}
        std::optional<Point3> previous;Point3 first;
        for(const auto& segment:geometry.Segments)
        {const auto range=Range(segment);const auto a=Evaluate(geometry,segment,First(range)),b=Evaluate(geometry,segment,Last(range));if(previous&&Distance(*previous,a)>geometry.Tolerance)throw std::invalid_argument("TOOLPATH_CHAIN_DISCONNECTED");if(!previous)first=a;previous=b;}
        if(geometry.Closed&&Distance(first,*previous)>geometry.Tolerance)throw std::invalid_argument("TOOLPATH_CLOSURE_INVALID");
    }
    void ValidateManufacturing(const std::string&,const SOperationEvaluation&,const SOperationCandidate&)const override{}
};
std::shared_ptr<IRepository> ExactRepository()
{auto registry=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*registry);return GenerateRepository(GenerateNewUUID(),registry);}
struct ExactHost
{
    std::shared_ptr<IRepository> Db=ExactRepository();std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();
    std::shared_ptr<CExactCircularBackend> Geometry=std::make_shared<CExactCircularBackend>();std::unique_ptr<CEditableToolpathDocument> Document;IntentId Plan;
    ExactHost(){RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry,Geometry);Plan=Document->CreatePlan("exact offset");}
    Map Command(const std::string& kind)const{return {{"kind",kind},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
    void Generate(){const auto task=Document->GenerateAsync(Plan);while(!task->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(Document->Publish(task));}
    void Add(const std::string& type,const SResolvedPath& input,const Map& parameters)
    {auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;command["inputs"]=Array{ToData(SOperationInput{"input",input.Port,true,{}})};command["expectedInputs"]=Map{{Key(input.Port),input.Fingerprint}};Document->Execute(command);Generate();}
};
}

TEST(EditableToolpathExactGeometry, OffsetLawAndParameterDeclarationSerializeWithoutLoss)
{
    auto value=OffsetCircle();auto& offset=std::get<DirectionalCurveOffset3>(value.Geometry.Segments[0].Curve);
    offset.ReferenceDirection=ParametricVectorField3{Bezier3{{{0,0,1},{0,1,1}}}};
    const auto data=ToData(value);const auto restored=ToolpathFromData(data);EXPECT_EQ(data,ToData(restored));
    ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(restored.Geometry.Segments[0].Curve));EXPECT_EQ("CadNative",restored.Geometry.Segments[0].Parameterization);
    EXPECT_EQ(ExactData(offset),ExactData(std::get<DirectionalCurveOffset3>(restored.Geometry.Segments[0].Curve)));
    auto changed=restored;changed.Geometry.Segments[0].Parameterization="Canonical";EXPECT_NE(ToolpathFingerprint(restored,{"geometry"}),ToolpathFingerprint(changed,{"geometry"}));
}
TEST(EditableToolpathExactGeometry, DefaultEvaluatorCannotGuessNativeOrOffsetGeometry)
{
    const auto services=CreateToolpathGeometryServices();const auto offset=OffsetCircle();
    EXPECT_THROW(services->Evaluate(offset.Geometry,offset.Geometry.Segments[0],0),std::exception);
    EXPECT_THROW(services->Validate(offset.Geometry),std::exception);
    SToolpathValue native;native.Geometry.Segments.push_back({"polyline",CurveSegment3{Polyline3{{{0,0,0},{10,0,0},{10,10,0}},false},{0,2,false}},{},{},"CadNative"});
    try{services->Validate(native.Geometry);FAIL()<<"Native geometry must require an explicit backend";}catch(const std::exception& error){EXPECT_EQ(0,std::string(error.what()).find("TOOLPATH_UNSUPPORTED_CURVE:"));}
    CExactCircularBackend backend;EXPECT_NO_THROW(backend.Validate(native.Geometry));const auto point=backend.Evaluate(native.Geometry,native.Geometry.Segments[0],1.5);EXPECT_DOUBLE_EQ(10,point.X);EXPECT_DOUBLE_EQ(5,point.Y);
}
TEST(EditableToolpathExactGeometry, NativeOffsetCanBeTrimmedReversedAndSavedAsAnExactEditableResult)
{
    ExactHost host;auto source=host.Command("CreateSource");source["value"]=ToData(OffsetCircle());host.Document->Execute(source);host.Generate();
    auto resolved=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,resolved.Status);ASSERT_EQ(1,resolved.Paths.size());const auto original=resolved.Paths[0];
    const auto anchor=[&](double parameter){return SPathAnchor{original.Port,"circle",parameter};};
    const Map gap{{"id",std::string("joint")},{"range",ToData(SPathRange{anchor(0.5),anchor(1.0),false})}};
    host.Add("icax.microjoint",original,{{"gaps",Array{gap}}});
    resolved=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,resolved.Status);ASSERT_EQ(1,resolved.Paths.size());const auto trimmed=resolved.Paths[0];
    ASSERT_EQ(2,trimmed.Value.Geometry.Segments.size());const auto pathId=trimmed.PathId;double retainedRadians=0;
    for(const auto& segment:trimmed.Value.Geometry.Segments){ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(segment.Curve));EXPECT_EQ("CadNative",segment.Parameterization);const auto& law=std::get<DirectionalCurveOffset3>(segment.Curve);EXPECT_DOUBLE_EQ(1,law.SignedDistance);retainedRadians+=law.Source.Range.Last-law.Source.Range.First;}
    EXPECT_NEAR(2*std::numbers::pi-0.5,retainedRadians,1e-12);host.Add("icax.edit.reverse",trimmed,{});
    resolved=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,resolved.Status);ASSERT_EQ(1,resolved.Paths.size());EXPECT_EQ(pathId,resolved.Paths[0].PathId);
    for(const auto& segment:resolved.Paths[0].Value.Geometry.Segments){EXPECT_EQ("CadNative",segment.Parameterization);EXPECT_TRUE(std::get<DirectionalCurveOffset3>(segment.Curve).Source.Range.Reversed);}
    const auto saved=host.Document->ExportDocument();auto db=ExactRepository();const auto document=CEditableToolpathDocument::ImportDocument(db,saved,host.Geometry);CEditableToolpathDocument reopened(db,document,host.Registry,host.Geometry);
    const auto opened=reopened.Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,opened.Status);ASSERT_EQ(1,opened.Paths.size());EXPECT_EQ(ToData(resolved.Paths[0].Value),ToData(opened.Paths[0].Value));
    CEditableToolpathDocument withoutBackend(db,document,host.Registry);EXPECT_EQ(EOutputCompleteness::Incomplete,withoutBackend.Resolve(host.Plan).Status);
}
