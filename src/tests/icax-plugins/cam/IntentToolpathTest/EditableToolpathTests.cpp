#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathPreview.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include "Database/OperationLog.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <numbers>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Data;
using namespace iCAX::Database;
using namespace iCAX::GeometryData;
std::shared_ptr<IRepository> Repository()
{auto meta=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*meta);return GenerateRepository(GenerateNewUUID(),meta);}
struct Host
{
    std::shared_ptr<IRepository> Db=Repository();
    std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();
    std::unique_ptr<CEditableToolpathDocument> Document;
    IntentId Plan;
    Host(){RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry);Plan=Document->CreatePlan("test");}
    Map Command(std::string kind)const{return {{"kind",std::move(kind)},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
    void Generate(std::size_t workers=4)
    {auto job=Document->GenerateAsync(Plan,workers);while(!job->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(Document->Publish(job));}
    Map Source(const SToolpathValue& value)
    {auto c=Command("CreateSource");c["value"]=ToData(value);c["name"]=std::string("A");return Document->Execute(c);}
    std::vector<SResolvedPath> Paths()const{return Document->Resolve(Plan).Paths;}
    Map Expected(const std::vector<SPortRef>& ports)const
    {Map fingerprints;for(const auto& p:Paths())for(const auto& target:ports)if(p.Port==target)fingerprints[Key(target)]=p.Fingerprint;return fingerprints;}
    Map Add(const std::string& type,const std::vector<SPortRef>& ports,const Map& params,const Map& context={},bool replace=true)
    {auto c=Command("AddOperation");c["typeId"]=type;c["parameters"]=params;c["context"]=context;Array inputs;for(std::size_t i=0;i<ports.size();++i)inputs.emplace_back(Map{{"slot","input/"+std::to_string(i)},{"source",ToData(ports[i])},{"replace",replace},{"ranges",Array{}}});c["inputs"]=inputs;c["expectedInputs"]=Expected(ports);c["outputRoot"]=!replace;return Document->Execute(c);}
    Map Update(IntentId operation,const SToolpathValue& value)
    {auto c=Command("UpdateSource");c["operationId"]=operation;c["value"]=ToData(value);c["expectedOperationFingerprint"]=At(Document->ReadOperation(operation),"definitionFingerprint");return Document->Execute(c);}
    void Delete(const std::vector<SPortRef>& ports)
    {auto c=Command("DeletePaths");Array targets;for(const auto& p:ports)targets.emplace_back(ToData(p));c["targets"]=targets;c["expectedInputs"]=Expected(ports);Document->Execute(c);}
};
SToolpathValue Line(double end=10,bool planar=false)
{SToolpathValue value;if(planar)value.Geometry=MakeEditableGeometry(CompositeCurve2{{{Segment2{{0,0},{end,0}},{0,1,false}}},false},"edge",Placement3{});
 else value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{end,0,0}},{0,1,false}}},false},"edge");return value;}
SToolpathValue Circle(bool planar=false)
{SToolpathValue v;if(planar){Circle2 c;c.Placement.XDirection={1,0};c.Radius=10;v.Geometry=MakeEditableGeometry(CompositeCurve2{{{c,{0,2*std::numbers::pi,false}}},true},"circle",Placement3{});}
 else {Circle3 c;c.Radius=10;v.Geometry=MakeEditableGeometry(CompositeCurve3{{{c,{0,2*std::numbers::pi,false}}},true},"circle");}return v;}
SPathAnchor Anchor(const SResolvedPath& p,double parameter){return {p.Port,p.Value.Geometry.Segments.front().Id,parameter};}
Map Gap(const SResolvedPath& p,double start,double end,const std::string& id="J")
{return {{"id",id},{"range",ToData(SPathRange{Anchor(p,start),Anchor(p,end),false})}};}
void Complete(const Host& host,std::size_t count)
{const auto r=host.Document->Resolve(host.Plan);for(const auto& d:r.Diagnostics)ADD_FAILURE()<<d.Message;EXPECT_EQ(EOutputCompleteness::Complete,r.Status);EXPECT_EQ(count,r.Paths.size());}
}

#include "FingerprintCompatibility.inc"
TEST(EditableToolpath, FingerprintsRetainThePersistedProjectionAcrossAuthoritiesMasksAndEscapedFields)
{
    const auto port=SourceToolpathPort(GenerateNewUUID());const std::string escaped="标识\\\"\n\t\r\b\f";
    std::vector<SToolpathValue> values{Line(),Line(10,true),Circle(),Circle(true)};
    auto uv=Line(10,true);uv.Geometry.Authority=EGeometryAuthority::SurfaceUV;uv.Geometry.SupportSurface=PlaneSurface3{};values.push_back(uv);
    auto offset=Circle();DirectionalCurveOffset3 law;law.Source=std::get<CurveSegment3>(offset.Geometry.Segments[0].Curve);law.ReferenceDirection=Direction3{0,0,1};law.SignedDistance=.05;
    offset.Geometry.Segments[0].Curve=law;offset.Geometry.Segments[0].Parameterization="CadNative";values.push_back(offset);
    const std::vector<std::set<std::string>> masks{{"*"},{},{"geometry"},{"provenance"},{"frame"},{"topology"},{"traversal"},
        {escaped},{"geometry","provenance",escaped},{"geometry","topology","traversal","frame"},{"missing-channel"}};
    for(auto& value:values)
    {
        value.Geometry.FrameId=escaped;value.Geometry.Meaning=escaped;
        for(auto& segment:value.Geometry.Segments)
        {
            segment.Id+=escaped;segment.ConstructionRole=escaped;
            segment.Sources.push_back({port,escaped,{-0.,1.,true},{-0.,1.,false},"Exact",0.,1ULL<<40,escaped});
        }
        value.Channels[escaped]=Map{{"string",escaped},{"nested",Array{Map{{"a",-0.},{"b",std::numeric_limits<double>::max()},
            {"c",std::numeric_limits<double>::infinity()},{"d",std::numeric_limits<double>::quiet_NaN()},{"uuid",port.OperationId}}}}};
        for(const auto& mask:masks)EXPECT_EQ(LegacyPersistedFingerprint(value,mask),ToolpathFingerprint(value,mask));
    }
}
TEST(EditableToolpath, CoverageDeduplicationRetainsSignedZeroAndTraversalDistinctions)
{
    const auto port=SourceToolpathPort(GenerateNewUUID());const auto value=Line();const auto geometry=CreateToolpathGeometryServices();
    SFrozenPathInput path{{"input",port,true,{}},GenerateNewUUID(),value,ToolpathFingerprint(value)};
    SOperationEvaluation input{GenerateNewUUID(),{path},{},{},{}};
    for(const int kind:{0,1,2})
    {
        CToolpathOperationRegistry registry;SOperationType type;type.TypeId="test.coverage-deduplication";type.ReadChannels={"*"};
        type.WriteChannels={"*"};type.PropagateUnwrittenChannels=false;type.ParameterSchema={{"fields",Map{}}};
        type.Evaluate=[kind](const SOperationEvaluation& evaluation,const IToolpathGeometryServices&)
        {
            const auto& source=evaluation.Inputs.front();auto output=source.Value;auto& segment=output.Geometry.Segments.front();const ParameterRange range{0.,1.,false};
            segment.Sources={{source.Binding.Source,segment.Id,range,range,"Exact",0.}};auto duplicate=segment.Sources.front();
            if(kind==1)duplicate.SourceRange.First=-0.;if(kind==2)duplicate.SourceRange.Reversed=true;
            segment.Sources.push_back(duplicate);SOperationCandidate candidate;candidate.Outputs.push_back({"main",{},std::size_t(0),std::move(output)});return candidate;
        };
        registry.Register(type);
        if(kind==0)EXPECT_NO_THROW(EvaluateFrozenToolpathOperation(registry,*geometry,type.TypeId,input));
        else EXPECT_THROW(EvaluateFrozenToolpathOperation(registry,*geometry,type.TypeId,input),std::invalid_argument);
    }
}
TEST(EditableToolpath, PlanDefinitionReadPreservesPlanWithoutCapturingResultGraph)
{
    Host host;host.Source(Circle());host.Generate();auto full=host.Document->ReadPlan(host.Plan);
    ASSERT_TRUE(full.contains("graphFingerprint"));full.erase("graphFingerprint");
    EXPECT_EQ(full,host.Document->ReadPlanDefinition(host.Plan));
    EXPECT_FALSE(host.Document->ReadPlanDefinition(host.Plan).contains("graphFingerprint"));
    EXPECT_THROW(host.Document->ReadPlanDefinition(GenerateNewUUID()),std::exception);
}
TEST(EditableToolpath, OperationDefinitionReadPreservesDefinitionWithoutCapturingResult)
{
    Host host;const auto source=host.Source(Circle());host.Generate();const auto id=ID(At(source,"operationId"));
    auto full=host.Document->ReadOperation(id);ASSERT_TRUE(full.contains("result"));full.erase("result");
    EXPECT_EQ(full,host.Document->ReadOperationDefinition(id));
    EXPECT_FALSE(host.Document->ReadOperationDefinition(id).contains("result"));
    EXPECT_THROW(host.Document->ReadOperationDefinition(GenerateNewUUID()),std::exception);
}
TEST(EditableToolpath, OrderConstraintsUsePortsWithoutGeometryEvaluationAndKeepHistory)
{
    class CountingGeometry final:public IToolpathGeometryServices
    {
        std::shared_ptr<const IToolpathGeometryServices> Base=CreateToolpathGeometryServices();
    public:
        mutable std::atomic<int> Validations{0};
        Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter)const override{return Base->Evaluate(geometry,segment,parameter);}
        void Validate(const SEditableGeometry& geometry)const override{++Validations;Base->Validate(geometry);}
        void ValidateManufacturing(const std::string& type,const SOperationEvaluation& input,const SOperationCandidate& candidate)const override{Base->ValidateManufacturing(type,input,candidate);}
    };
    Host host;auto geometry=std::make_shared<CountingGeometry>();host.Document=std::make_unique<CEditableToolpathDocument>(host.Db,host.Document->Id(),host.Registry,geometry);
    host.Source(Line());host.Source(Circle());host.Generate();const auto paths=host.Paths();ASSERT_EQ(2u,paths.size());
    const auto before=host.Document->ReadPlanDefinition(host.Plan);const auto history=host.Db->GetUndoArray().size();
    const auto request=[&](const Array& members){auto command=host.Command("SetExecutionConstraints");command["constraints"]=Map{{"blocks",Array{Map{{"id",std::string("manual")},{"members",members},{"atomic",false}}}}};return command;};
    const auto command=request({ToData(paths[1].Port),ToData(paths[0].Port)});geometry->Validations=0;
    const auto receipt=host.Document->Execute(command);EXPECT_EQ(0,geometry->Validations.load());EXPECT_FALSE(receipt.contains("resultValidity"));
    EXPECT_EQ(history+1,host.Db->GetUndoArray().size());EXPECT_EQ(receipt,host.Document->Execute(command));EXPECT_EQ(history+1,host.Db->GetUndoArray().size());
    const auto ordered=host.Document->ReadPlanDefinition(host.Plan);ASSERT_TRUE(host.Db->Undo());EXPECT_EQ(before,host.Document->ReadPlanDefinition(host.Plan));
    ASSERT_TRUE(host.Db->Redo());EXPECT_EQ(ordered,host.Document->ReadPlanDefinition(host.Plan));
    EXPECT_THROW(host.Document->Execute(request({ToData(SourceToolpathPort(GenerateNewUUID()))})),std::invalid_argument);
    EXPECT_THROW(host.Document->Execute(request({GenerateNewUUID()})),std::invalid_argument);
    EXPECT_EQ(ordered,host.Document->ReadPlanDefinition(host.Plan));
    EXPECT_NO_THROW(host.Document->Execute(request({paths[0].PathId,paths[1].PathId})));
    const auto stored=MapAt(host.Document->ReadPlanDefinition(host.Plan),"executionConstraints");
    EXPECT_EQ(Array({ToData(paths[0].Port),ToData(paths[1].Port)}),ArrayAt(ArrayAt(stored,"blocks").front().To<Map>(),"members"));
    const auto after=host.Paths();ASSERT_EQ(paths.size(),after.size());for(std::size_t i=0;i<paths.size();++i)EXPECT_EQ(ToData(paths[i].Value),ToData(after[i].Value));
}
TEST(EditableToolpath, SourceRoundTripUsesExactDatabaseValues)
{
    Host host;const auto value=Circle();host.Source(value);host.Generate();Complete(host,1);EXPECT_EQ(ToData(value).at("geometry"),ToData(host.Paths()[0].Value).at("geometry"));
    const auto snapshot=host.Document->ExportDocument();const auto encoded=VariantSerializer::Serialize(snapshot);auto db=Repository();const auto id=CEditableToolpathDocument::ImportDocument(db,VariantSerializer::Deserialize(encoded).To<Map>());
    CEditableToolpathDocument reopened(db,id,host.Registry);const auto r=reopened.Resolve(host.Plan);ASSERT_EQ(1u,r.Paths.size());EXPECT_EQ(host.Paths()[0].PathId,r.Paths[0].PathId);EXPECT_EQ(host.Paths()[0].Fingerprint,r.Paths[0].Fingerprint);
}
TEST(EditableToolpath, MicrojointLeadDeleteSourceUpdateUndoAndReopen)
{
    for(const bool planar:{false,true})
    {
        Host host;const auto source=host.Source(Line(10,planar));host.Generate();const auto a=host.Paths()[0];
        host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();Complete(host,2);auto children=host.Paths();
        std::sort(children.begin(),children.end(),[](const auto& x,const auto& y){return Range(x.Value.Geometry.Segments.front()).First<Range(y.Value.Geometry.Segments.front()).First;});const auto b=children[0],c=children[1];
        host.Add("icax.lead.linear",{c.Port},{{"atStart",true},{"offset",Array{0.,-2.,0.}}},{{"scrapHalfSpace",Map{{"point",Array{0.,0.,0.}},{"normal",Array{0.,-1.,0.}}}}});host.Generate();Complete(host,2);
        host.Delete({b.Port});Complete(host,1);const auto cId=host.Paths()[0].PathId;EXPECT_EQ(c.PathId,cId);
        host.Update(ID(At(source,"operationId")),Line(20,planar));host.Generate();Complete(host,1);EXPECT_EQ(cId,host.Paths()[0].PathId);EXPECT_EQ(2u,host.Paths()[0].Value.Geometry.Segments.size());
        ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,1);ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,2);
        ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,1);ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,1);
        auto db=Repository();const auto id=CEditableToolpathDocument::ImportDocument(db,host.Document->ExportDocument());CEditableToolpathDocument reopen(db,id,host.Registry);const auto r=reopen.Resolve(host.Plan);EXPECT_EQ(EOutputCompleteness::Complete,r.Status);ASSERT_EQ(1u,r.Paths.size());EXPECT_EQ(cId,r.Paths[0].PathId);
    }
}
TEST(EditableToolpath, ClosedMicrojointTopologyIsNotFixedToTwoOutputs)
{
    for(const bool planar:{false,true})for(const int count:{1,2})
    {Host host;host.Source(Circle(planar));host.Generate();const auto a=host.Paths()[0];Array gaps{Gap(a,.5,1.,"J1")};if(count==2)gaps.emplace_back(Gap(a,3.,3.5,"J2"));host.Add("icax.microjoint",{a.Port},{{"gaps",gaps}});host.Generate();Complete(host,count);for(const auto& p:host.Paths())EXPECT_FALSE(p.Value.Geometry.Closed);}
}
TEST(EditableToolpath, DeletingEveryOutputDoesNotRestoreConsumedInput)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto m=host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();std::vector<SPortRef> ports;for(const auto& p:host.Paths())ports.push_back(p.Port);host.Delete(ports);
    auto r=host.Document->Resolve(host.Plan);EXPECT_TRUE(r.IntentionalEmpty);EXPECT_EQ(EOutputCompleteness::Complete,r.Status);EXPECT_TRUE(r.Paths.empty());
    auto c=host.Command("RemoveOperation");const auto id=ID(At(m,"operationId"));c["operationId"]=id;c["expectedOperationFingerprint"]=At(host.Document->ReadOperation(id),"definitionFingerprint");host.Document->Execute(c);Complete(host,1);EXPECT_EQ(a.PathId,host.Paths()[0].PathId);
}
TEST(EditableToolpath, SplitMergeResultIsEmittedOnce)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}});host.Generate();Complete(host,2);auto parts=host.Paths();
    std::sort(parts.begin(),parts.end(),[](const auto& x,const auto& y){return Range(x.Value.Geometry.Segments.front()).First<Range(y.Value.Geometry.Segments.front()).First;});host.Add("icax.edit.merge",{parts[0].Port,parts[1].Port},{});host.Generate();Complete(host,1);EXPECT_NE(a.PathId,host.Paths()[0].PathId);
}
TEST(EditableToolpath, MissingPluginRetainsDataAndBlocksCompleteOutput)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto op=host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();host.Registry->Unregister("icax.microjoint");
    const auto r=host.Document->Resolve(host.Plan);EXPECT_EQ(EOutputCompleteness::Incomplete,r.Status);EXPECT_TRUE(r.Paths.empty());const auto data=host.Document->ReadOperation(ID(At(op,"operationId")));EXPECT_TRUE(MapAt(data,"result").contains("outputs"));EXPECT_TRUE(MapAt(data,"parameters").contains("gaps"));
}
TEST(EditableToolpath, LateGenerationCannotPublishAcrossUndoRedo)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto op=host.Add("icax.edit.reverse",{a.Port},{});auto job=host.Document->GenerateAsync(host.Plan);
    while(!job->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(host.Db->Undo());ASSERT_TRUE(host.Db->Redo());EXPECT_TRUE(host.Document->Publish(job));EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);host.Generate();Complete(host,1);
}
TEST(EditableToolpath, IdempotentCommandRemainsAcknowledgedAfterUndo)
{
    Host host;auto command=host.Command("CreateSource");command["value"]=ToData(Line());const auto first=host.Document->Execute(command);ASSERT_TRUE(host.Db->Undo());const auto second=host.Document->Execute(command);EXPECT_EQ(first,second);Complete(host,0);
    command["value"]=ToData(Line(20));
    EXPECT_THROW(host.Document->Execute(command),std::invalid_argument);
}
TEST(EditableToolpath, DerivedPublicationRejectsDefinitionMutationAtomically)
{
    Host host;const auto before=host.Document->ExportDocument();auto& tx=host.Db->BeginDerivedTransaction(host.Document->Id(),"invalid result");tx.ModifyComponent(host.Plan,CEditablePlanComponent::S_ClassName,{{"Definition",Map{}}});std::string error;EXPECT_FALSE(host.Db->CommitTransaction(tx,error));EXPECT_EQ(before,host.Document->ExportDocument());
}

TEST(EditableToolpath, ReversingUpstreamPreservesPhysicalMicrojointAndOutputIdentities)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();const auto before=host.Paths();ASSERT_EQ(2u,before.size());
    auto reversed=Line();auto& segment=reversed.Geometry.Segments[0];auto r=Range(segment);r.Reversed=true;SetRange(segment,r);host.Update(ID(At(source,"operationId")),reversed);host.Generate();Complete(host,2);
    std::set<IntentId> original,updated;for(const auto& p:before)original.insert(p.PathId);for(const auto& p:host.Paths()){updated.insert(p.PathId);const auto range=Range(p.Value.Geometry.Segments[0]);EXPECT_TRUE(range.Reversed);EXPECT_TRUE(range.Last<=.4+1e-12||range.First>=.6-1e-12);}EXPECT_EQ(original,updated);
}
TEST(EditableToolpath, PointAndIntervalChannelsMapToOnlyTheirActualOutputs)
{
    Host host;auto value=Line();const auto source=host.Source(value);host.Generate();const auto a=host.Paths()[0];
    const Array eventsData{Map{{"id",std::string("event")},{"anchor",ToData(Anchor(a,.8))}}};
    const Array poseData{Map{{"range",ToData(SPathRange{Anchor(a,.2),Anchor(a,.9),false})},{"value",Map{{"tilt",15.}}}}};
    value.Channels["events"]=Map{{"attachment",std::string("Points")},{"schemaVersion",1u},{"entries",eventsData}};
    value.Channels["pose"]=Map{{"attachment",std::string("Intervals")},{"schemaVersion",1u},{"entries",poseData}};
    host.Update(ID(At(source,"operationId")),value);host.Generate();const auto current=host.Paths()[0];host.Add("icax.microjoint",{current.Port},{{"gaps",Array{Gap(current,.4,.6)}}});host.Generate();Complete(host,2);
    std::size_t events=0,intervals=0;SPortRef right;
    for(const auto& p:host.Paths()){const auto count=ArrayAt(MapAt(p.Value.Channels,"events"),"entries").size();events+=count;intervals+=ArrayAt(MapAt(p.Value.Channels,"pose"),"entries").size();if(count)right=p.Port;}EXPECT_EQ(1u,events);EXPECT_EQ(2u,intervals);
    host.Add("icax.edit.reverse",{right},{});host.Generate();Complete(host,2);events=0;for(const auto& p:host.Paths())events+=ArrayAt(MapAt(p.Value.Channels,"events"),"entries").size();EXPECT_EQ(1u,events);
}
TEST(EditableToolpath, DenseOverlappingIntervalsRetainOrderValuesAndExactReversedDomains)
{
    Host host;CompositeCurve3 curve;constexpr std::size_t count=32;
    for(std::size_t i=0;i<count;++i)curve.Segments.push_back({Segment3{{double(i),0,0},{double(i+1),0,0}},{0,1,false}});
    SToolpathValue value;value.Geometry=MakeEditableGeometry(curve,"dense");
    const auto source=host.Source(value);host.Generate();const auto original=host.Paths().front();Array intervals;
    for(std::size_t i=0;i+1<count;++i)
        intervals.emplace_back(Map{{"range",ToData(SPathRange{{original.Port,value.Geometry.Segments[i].Id,.25},
            {original.Port,value.Geometry.Segments[i+1].Id,.75},false})},{"value",Map{{"index",static_cast<unsigned int>(i)}}}});
    value.Channels["intervals"]=Map{{"attachment",std::string("Intervals")},{"entries",intervals}};
    host.Update(ID(At(source,"operationId")),value);host.Generate();
    host.Add("icax.edit.reverse",{host.Paths().front().Port},{});host.Generate();Complete(host,1);
    const auto output=host.Paths().front();const auto mapped=ArrayAt(MapAt(output.Value.Channels,"intervals"),"entries");
    ASSERT_EQ(2*(count-1),mapped.size());
    for(std::size_t i=0;i+1<count;++i)
    {
        const auto upper=mapped[2*i].To<Map>(),lower=mapped[2*i+1].To<Map>();
        EXPECT_EQ(Map({{"index",static_cast<unsigned int>(i)}}),MapAt(upper,"value"));EXPECT_EQ(MapAt(upper,"value"),MapAt(lower,"value"));
        const auto a=RangeFromData(MapAt(upper,"range")),b=RangeFromData(MapAt(lower,"range"));
        EXPECT_EQ(output.Port,a.Start.Port);EXPECT_EQ(output.Port,a.End.Port);EXPECT_EQ(output.Port,b.Start.Port);EXPECT_EQ(output.Port,b.End.Port);
        EXPECT_EQ(value.Geometry.Segments[i+1].Id,a.Start.SegmentId);EXPECT_EQ(a.Start.SegmentId,a.End.SegmentId);
        EXPECT_EQ(value.Geometry.Segments[i].Id,b.Start.SegmentId);EXPECT_EQ(b.Start.SegmentId,b.End.SegmentId);
        EXPECT_DOUBLE_EQ(.75,a.Start.Parameter);EXPECT_DOUBLE_EQ(0.,a.End.Parameter);
        EXPECT_DOUBLE_EQ(1.,b.Start.Parameter);EXPECT_DOUBLE_EQ(.25,b.End.Parameter);
    }
}
TEST(EditableToolpath, DuplicateProvenanceRoutesDoNotMultiplyAuthoredIntervals)
{
    const auto port=SourceToolpathPort(GenerateNewUUID());auto value=Line();const auto id=value.Geometry.Segments.front().Id;
    const auto interval=[&](double first,double last,const std::string& label)
    {return Map{{"range",ToData(SPathRange{{port,id,first},{port,id,last},false})},{"value",label}};};
    value.Channels["intervals"]=Map{{"attachment",std::string("Intervals")},{"entries",Array{
        interval(0.,1.,"same"),interval(0.,1.,"same"),interval(0.,1.,"different"),interval(.125,.75,"partial")}}};
    SFrozenPathInput path{{"input",port,true,{}},GenerateNewUUID(),value,ToolpathFingerprint(value)};
    SOperationEvaluation input{GenerateNewUUID(),{path},{},{},{}};CToolpathOperationRegistry registry;
    SOperationType type;type.TypeId="test.repeated-interval-routes";type.ReadChannels={"*"};type.ParameterSchema={{"fields",Map{}}};
    type.Evaluate=[](const SOperationEvaluation& evaluation,const IToolpathGeometryServices&)
    {
        const auto& source=evaluation.Inputs.front();auto output=source.Value;const auto original=output.Geometry.Segments.front();
        output.Geometry.Segments.clear();
        for(const bool second:{false,true})
        {
            auto segment=original;segment.Id+=second?"/second":"/first";SetRange(segment,{second?.5:0.,second?1.:.5,false});segment.Sources.clear();
            const std::vector<ParameterRange> ranges=second?std::vector<ParameterRange>{{.5,1.,false}}:
                std::vector<ParameterRange>{{0.,.25,false},{.25,.5,false}};
            for(const auto& range:ranges)for(int route=0;route<4;++route)
                segment.Sources.push_back({source.Binding.Source,original.Id,range,range,"Exact",0.});
            output.Geometry.Segments.push_back(std::move(segment));
        }
        return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(output)}}};
    };
    registry.Register(type);const auto geometry=CreateToolpathGeometryServices();
    const auto result=EvaluateFrozenToolpathOperation(registry,*geometry,type.TypeId,input);
    ASSERT_EQ(1u,result.Outputs.size());const auto& output=result.Outputs.front();
    const auto entries=ArrayAt(MapAt(output.Value.Channels,"intervals"),"entries");ASSERT_EQ(12u,entries.size());
    const std::array<std::pair<double,double>,3> full{{{0.,.25},{.25,.5},{.5,1.}}},partial{{{.125,.25},{.25,.5},{.5,.75}}};
    for(std::size_t entry=0;entry<4;++entry)for(std::size_t piece=0;piece<3;++piece)
    {
        const auto mapped=entries[entry*3+piece].To<Map>();const auto range=RangeFromData(MapAt(mapped,"range"));
        EXPECT_EQ(entry<2?"same":entry==2?"different":"partial",Text(mapped,"value"));
        EXPECT_EQ(output.Value.Geometry.Segments[piece==2?1:0].Id,range.Start.SegmentId);EXPECT_EQ(range.Start.SegmentId,range.End.SegmentId);
        const auto expected=(entry==3?partial:full)[piece];EXPECT_DOUBLE_EQ(expected.first,range.Start.Parameter);EXPECT_DOUBLE_EQ(expected.second,range.End.Parameter);
    }
}
TEST(EditableToolpath, IndependentBranchesRunTogetherAndUnchangedGeometryIsReused)
{
    Host host;auto calls=std::make_shared<std::atomic<int>>(0),active=std::make_shared<std::atomic<int>>(0),peak=std::make_shared<std::atomic<int>>(0);
    SOperationType type;type.TypeId="test.registered.geometry";type.ReadChannels={"geometry"};type.ParameterSchema={{"fields",Map{}}};
    type.Evaluate=[calls,active,peak](const SOperationEvaluation& input,const IToolpathGeometryServices&)
    {
        ++*calls;const auto concurrent=++*active;auto maximum=peak->load();while(maximum<concurrent&&!peak->compare_exchange_weak(maximum,concurrent)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(25));auto value=input.Inputs[0].Value;
        for(auto& segment:value.Geometry.Segments){const auto r=Range(segment);segment.Sources.push_back({input.Inputs[0].Binding.Source,segment.Id,r,r,"Exact",0});}
        --*active;return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};
    };host.Registry->Register(std::move(type));
    const auto first=host.Source(Line(10));host.Source(Line(20));host.Generate();const auto paths=host.Paths();ASSERT_EQ(2u,paths.size());
    const auto firstOp=host.Add("test.registered.geometry",{paths[0].Port},{});host.Add("test.registered.geometry",{paths[1].Port},{});host.Generate(4);Complete(host,2);EXPECT_EQ(2,calls->load());EXPECT_GE(peak->load(),2);
    auto changed=Line(10);changed.Channels["process"]=Map{{"power",300.}};host.Update(ID(At(first,"operationId")),changed);host.Generate(4);Complete(host,2);EXPECT_EQ(2,calls->load());
    bool propagated=false;for(const auto& path:host.Paths())if(path.Value.Channels.contains("process"))propagated=Number(MapAt(path.Value.Channels,"process"),"power")==300.;EXPECT_TRUE(propagated);
    auto task=host.Document->GenerateAsync(host.Plan,4,{ID(At(firstOp,"operationId"))});while(!task->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));EXPECT_TRUE(host.Document->Publish(task));EXPECT_EQ(3,calls->load());
}
TEST(EditableToolpath, BridgeIsExplicitAndGeometricEditsRemainLinked)
{
    Host host;const auto source=host.Source(Line(3));auto second=Line(3);std::get<CurveSegment3>(second.Geometry.Segments[0].Curve).Curve=Segment3{{7,0,0},{10,0,0}};host.Source(second);host.Generate();const auto paths=host.Paths();ASSERT_EQ(2u,paths.size());
    SToolpathValue connector=Line(4);connector.Geometry.Segments[0].Id="connector";std::get<CurveSegment3>(connector.Geometry.Segments[0].Curve).Curve=Segment3{{3,0,0},{7,0,0}};
    host.Add("icax.edit.bridge",{paths[0].Port,paths[1].Port},{{"mode",std::string("NoCut")},{"connectors",Array{ToData(connector)}}});host.Generate();Complete(host,1);EXPECT_EQ("BridgeNoCut",host.Paths()[0].Value.Geometry.Segments[1].ConstructionRole);
    const auto merged=host.Paths()[0];host.Add("icax.edit.translate",{merged.Port},{{"delta",Array{0.,5.,0.}}});host.Generate();Complete(host,1);const auto geometry=CreateToolpathGeometryServices();const auto p=host.Paths()[0];EXPECT_DOUBLE_EQ(5.,geometry->Evaluate(p.Value.Geometry,p.Value.Geometry.Segments[0],0).Y);
}
TEST(EditableToolpath, PagingAndLabelsDoNotExposeGeometryOrLoseNumbers)
{
    Host host;host.Source(Line());host.Generate();const auto path=host.Paths()[0];const auto before=host.Document->QueryPathSummaries(host.Plan,0,1);const auto summary=host.Document->QuerySummary(host.Plan);EXPECT_LT(VariantSerializer::Serialize(summary).size(),1024u);EXPECT_FALSE(ArrayAt(before,"items")[0].To<Map>().contains("geometry"));
    auto edit=host.Command("SetLabels");edit["pathId"]=path.PathId;edit["name"]=std::string("B");edit["userNumber"]=std::string("125");edit["expectedLabelsVersion"]=static_cast<unsigned long long>(path.LabelsVersion);host.Document->Execute(edit);host.Generate();Complete(host,1);EXPECT_EQ("125",host.Paths()[0].UserNumber);
    EXPECT_THROW(host.Document->QueryPathSummaries(host.Plan,0,1,Text(before,"fingerprint")),std::invalid_argument);
    ASSERT_TRUE(host.Db->Undo());EXPECT_EQ("A",host.Paths()[0].Name);ASSERT_TRUE(host.Db->Redo());EXPECT_EQ("125",host.Paths()[0].UserNumber);
}

TEST(EditableToolpath, FailedPublicationLeavesNoPartialResultAndCanBeRetried)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];
    const auto command=host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}});
    const auto operation=ID(At(command,"operationId"));
    const auto candidate=host.Document->Preview("icax.edit.split",{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}},{a.Port});
    ASSERT_FALSE(candidate.Outputs.empty());
    const auto collision=NamedID(Key(operation)+"/path/"+candidate.Outputs.front().RoleKey);
    auto& create=host.Db->BeginTransaction("collision fixture");create.CreateEntity(collision);ASSERT_TRUE(host.Db->CommitTransaction(create));
    auto task=host.Document->GenerateAsync(host.Plan);while(!task->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto before=host.Document->ExportDocument();
    EXPECT_THROW(host.Document->Publish(task),std::invalid_argument);
    EXPECT_FALSE(host.Db->IsWriteInProgress());EXPECT_EQ(before,host.Document->ExportDocument());
    EXPECT_TRUE(MapAt(host.Document->ReadOperation(operation),"result").empty());
    auto& remove=host.Db->BeginTransaction("remove collision fixture");remove.DisposeEntity(collision);ASSERT_TRUE(host.Db->CommitTransaction(remove));
    ASSERT_TRUE(task->IsReady());ASSERT_TRUE(host.Document->Publish(task));Complete(host,2);
    EXPECT_TRUE(host.Document->Publish(task));
}

TEST(EditableToolpath, IntrinsicGeometryReadSetIncludesFrameEvenForNarrowChannelDeclaration)
{
    Host host;auto calls=std::make_shared<std::atomic<int>>(0);SOperationType type;
    type.TypeId="test.reads.frame";type.ReadChannels={"geometry"};type.ParameterSchema={{"fields",Map{}}};
    type.Evaluate=[calls](const SOperationEvaluation& in,const IToolpathGeometryServices&)
    {
        ++*calls;auto value=in.Inputs[0].Value;
        for(auto& segment:value.Geometry.Segments){const auto r=Range(segment);segment.Sources.push_back({in.Inputs[0].Binding.Source,segment.Id,r,r,"Exact",0});}
        value.Channels["frameObserved"]=value.Geometry.FrameId;
        return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};
    };type.WriteChannels={"frameObserved"};host.Registry->Register(std::move(type));
    const auto source=host.Source(Line());host.Generate();host.Add("test.reads.frame",{host.Paths()[0].Port},{});host.Generate();EXPECT_EQ(1,calls->load());
    auto changed=Line();changed.Geometry.FrameId="AnotherWorkpiece";host.Update(ID(At(source,"operationId")),changed);host.Generate();Complete(host,1);
    EXPECT_EQ(2,calls->load());EXPECT_EQ("AnotherWorkpiece",host.Paths()[0].Value.Channels.at("frameObserved").To<std::string>());
}

TEST(EditableToolpath, DroppingCancelledGenerationDoesNotWaitForAlgorithm)
{
    Host host;auto entered=std::make_shared<std::promise<void>>();auto started=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();auto gate=release->get_future().share();
    SOperationType type;type.TypeId="test.blocked.worker";type.ParameterSchema={{"fields",Map{}}};
    type.Evaluate=[entered,gate](const SOperationEvaluation& in,const IToolpathGeometryServices&)
    {
        entered->set_value();gate.wait();auto value=in.Inputs[0].Value;
        for(auto& segment:value.Geometry.Segments){const auto r=Range(segment);segment.Sources.push_back({in.Inputs[0].Binding.Source,segment.Id,r,r,"Exact",0});}
        return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};
    };host.Registry->Register(std::move(type));host.Source(Line());host.Generate();host.Add("test.blocked.worker",{host.Paths()[0].Port},{});
    auto task=host.Document->GenerateAsync(host.Plan);ASSERT_EQ(std::future_status::ready,started.wait_for(std::chrono::seconds(5)));
    auto watchdog=std::async(std::launch::async,[release]{std::this_thread::sleep_for(std::chrono::milliseconds(400));release->set_value();});
    task->Cancel();const auto before=std::chrono::steady_clock::now();task.reset();
    EXPECT_LT(std::chrono::steady_clock::now()-before,std::chrono::milliseconds(200));watchdog.get();
    EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
}

TEST(EditableToolpath, DerivedBranchesRemainEditableThroughSplitMergeUndoAndReopen)
{
    for(const bool planar:{false,true})
    {
        Host host;const auto source=host.Source(Line(10,planar));host.Generate();const auto a=host.Paths()[0];
        host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();auto parts=host.Paths();
        std::sort(parts.begin(),parts.end(),[](const auto& x,const auto& y){return Range(x.Value.Geometry.Segments.front()).First<Range(y.Value.Geometry.Segments.front()).First;});
        const auto b=parts[0],c=parts[1];host.Add("icax.lead.linear",{c.Port},{{"atStart",true},{"offset",Array{0.,-2.,0.}}},{{"scrapHalfSpace",Map{{"point",Array{0.,0.,0.}},{"normal",Array{0.,-1.,0.}}}}});host.Generate();
        host.Add("icax.edit.split",{b.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(b,.2))}}}}});host.Generate();Complete(host,3);
        std::vector<SResolvedPath> children;for(const auto& p:host.Paths())if(p.PathId!=c.PathId)children.push_back(p);
        std::sort(children.begin(),children.end(),[](const auto& x,const auto& y){return Range(x.Value.Geometry.Segments.front()).First<Range(y.Value.Geometry.Segments.front()).First;});
        ASSERT_EQ(2u,children.size());host.Add("icax.edit.merge",{children[0].Port,children[1].Port},{});host.Generate();Complete(host,2);
        SResolvedPath merged;for(const auto& p:host.Paths())if(p.PathId!=c.PathId)merged=p;
        auto label=host.Command("SetLabels");label["pathId"]=c.PathId;label["userNumber"]=std::string("C-125");host.Document->Execute(label);
        host.Delete({merged.Port});Complete(host,1);host.Update(ID(At(source,"operationId")),Line(20,planar));host.Generate();Complete(host,1);
        EXPECT_EQ(c.PathId,host.Paths()[0].PathId);EXPECT_EQ("C-125",host.Paths()[0].UserNumber);
        const auto geometry=CreateToolpathGeometryServices();const auto current=host.Paths()[0];
        EXPECT_DOUBLE_EQ(12.,geometry->Evaluate(current.Value.Geometry,current.Value.Geometry.Segments[1],.6).X);
        for(int repeat=0;repeat<3;++repeat)
        {
            ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,1);ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,2);
            ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,1);ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,1);
            EXPECT_EQ(c.PathId,host.Paths()[0].PathId);EXPECT_EQ("C-125",host.Paths()[0].UserNumber);
        }
        auto db=Repository();const auto document=CEditableToolpathDocument::ImportDocument(db,host.Document->ExportDocument());
        CEditableToolpathDocument reopened(db,document,host.Registry);const auto result=reopened.Resolve(host.Plan);
        ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1u,result.Paths.size());EXPECT_EQ(c.PathId,result.Paths[0].PathId);EXPECT_EQ("C-125",result.Paths[0].UserNumber);
    }
}

TEST(EditableToolpath, GroupOwnershipAndExecutionBlocksFollowDerivedOutputs)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];
    auto group=host.Command("Group");group["groupId"]=std::string("G");group["members"]=Array{a.PathId};host.Document->Execute(group);
    host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();Complete(host,2);
    auto result=host.Document->Resolve(host.Plan);ASSERT_EQ(1u,ArrayAt(result.ExecutionConstraints,"blocks").size());
    std::set<IntentId> members;for(const auto& p:result.Paths){EXPECT_EQ("G",p.GroupId);members.insert(p.PathId);}
    const auto block=ArrayAt(result.ExecutionConstraints,"blocks")[0].To<Map>();EXPECT_EQ(2u,ArrayAt(block,"members").size());
    EXPECT_EQ(2u,ArrayAt(result.Organization.at("G").To<Map>(),"members").size());
    const auto chosen=result.Paths[0];host.Add("icax.edit.reverse",{chosen.Port},{});host.Generate();Complete(host,2);
    result=host.Document->Resolve(host.Plan);for(const auto& p:result.Paths){EXPECT_TRUE(members.contains(p.PathId));EXPECT_EQ("G",p.GroupId);}
    EXPECT_EQ(2u,ArrayAt(ArrayAt(result.ExecutionConstraints,"blocks")[0].To<Map>(),"members").size());
}

TEST(EditableToolpath, StableBoundaryIdentitySurvivesMultipleRoleAliasesAndDeletion)
{
    Host host;SOperationType type;type.TypeId="test.role.alias";type.ParameterSchema={{"fields",Map{{"role",Map{{"type",std::string("string")},{"required",true}}}}}};
    type.Evaluate=[](const SOperationEvaluation& in,const IToolpathGeometryServices&)
    {auto value=in.Inputs[0].Value;for(auto& segment:value.Geometry.Segments){const auto r=Range(segment);segment.Sources.push_back({in.Inputs[0].Binding.Source,segment.Id,r,r,"Exact",0});}
     return SOperationCandidate{{SOutputCandidate{Text(in.Parameters,"role"),{"same-physical-feature"},std::size_t(0),std::move(value)}}};};
    host.Registry->Register(std::move(type));host.Source(Line());host.Generate();const auto op=host.Add("test.role.alias",{host.Paths()[0].Port},{{"role",std::string("r1")}});host.Generate();const auto first=host.Paths()[0];
    host.Delete({first.Port});Complete(host,0);
    for(const auto role:{"r2","r3","r1"})
    {auto command=host.Command("UpdateOperation");const auto id=ID(At(op,"operationId"));command["operationId"]=id;command["parameters"]=Map{{"role",std::string(role)}};command["expectedOperationFingerprint"]=At(host.Document->ReadOperation(id),"definitionFingerprint");host.Document->Execute(command);host.Generate();Complete(host,0);}
    auto restore=host.Command("RestoreOutputs");restore["targets"]=Array{ToData(first.Port)};restore["expectedInputs"]=Map{{Key(first.Port),host.Document->InspectPlan(host.Plan).Paths.at(0).Fingerprint}};host.Document->Execute(restore);Complete(host,1);EXPECT_EQ(first.PathId,host.Paths()[0].PathId);EXPECT_EQ(first.Port,host.Paths()[0].Port);
}

TEST(EditableToolpath, PreviewUsesTheSameNormalizationAndChannelProjectionAsGeneration)
{
    Host host;SOperationType type;type.TypeId="test.normalized.preview";type.ReadChannels={"geometry"};
    type.ParameterSchema={{"fields",Map{{"normalized",Map{{"type",std::string("boolean")},{"required",false}}}}}};
    type.NormalizeParameters=[](const std::vector<SFrozenPathInput>&,const Map& parameters){auto result=parameters;result["normalized"]=true;return result;};
    type.Evaluate=[](const SOperationEvaluation& in,const IToolpathGeometryServices&)
    {Require(Flag(in.Parameters,"normalized"),"TEST_NORMALIZATION_REQUIRED");Require(in.Inputs[0].Value.Channels.empty(),"TEST_UNDECLARED_CHANNEL_VISIBLE");auto value=in.Inputs[0].Value;
     for(auto& segment:value.Geometry.Segments){const auto r=Range(segment);segment.Sources.push_back({in.Inputs[0].Binding.Source,segment.Id,r,r,"Exact",0});}
     return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};};host.Registry->Register(std::move(type));
    auto source=Line();source.Channels["process"]=Map{{"power",300.}};host.Source(source);host.Generate();const auto port=host.Paths()[0].Port;
    const auto preview=host.Document->Preview("test.normalized.preview",{},{port});ASSERT_EQ(1u,preview.Outputs.size());EXPECT_TRUE(preview.Outputs[0].Value.Channels.contains("process"));
    host.Add("test.normalized.preview",{port},{});host.Generate();Complete(host,1);EXPECT_EQ(ToData(preview.Outputs[0].Value),ToData(host.Paths()[0].Value));
}

TEST(EditableToolpath, SharedSourceUpdatesBothPlansWithoutSharingTheirOperations)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto root=host.Paths()[0];const auto firstPlan=host.Plan;
    host.Add("icax.edit.reverse",{root.Port},{});host.Generate();Complete(host,1);
    const auto secondPlan=host.Document->CreatePlan("second");host.Plan=secondPlan;
    auto roots=host.Command("SetPlanRoots");roots["roots"]=Array{ToData(root.Port)};roots["expectedPlanFingerprint"]=At(host.Document->ReadPlan(secondPlan),"definitionFingerprint");host.Document->Execute(roots);Complete(host,1);
    const auto current=host.Paths()[0];host.Add("icax.microjoint",{current.Port},{{"gaps",Array{Gap(current,.4,.6)}}});host.Generate();Complete(host,2);
    std::set<IntentId> secondIds;for(const auto& p:host.Paths()){secondIds.insert(p.PathId);EXPECT_NE(root.PathId,p.PathId);}
    host.Plan=firstPlan;host.Update(ID(At(source,"operationId")),Line(20));host.Generate();Complete(host,1);EXPECT_EQ(root.PathId,host.Paths()[0].PathId);EXPECT_TRUE(Range(host.Paths()[0].Value.Geometry.Segments[0]).Reversed);
    host.Plan=secondPlan;EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(secondPlan).Status);host.Generate();Complete(host,2);
    for(const auto& p:host.Paths())EXPECT_TRUE(secondIds.contains(p.PathId));
    auto db=Repository();const auto id=CEditableToolpathDocument::ImportDocument(db,host.Document->ExportDocument());CEditableToolpathDocument reopened(db,id,host.Registry);
    EXPECT_EQ(1u,reopened.Resolve(firstPlan).Paths.size());EXPECT_EQ(2u,reopened.Resolve(secondPlan).Paths.size());
}

TEST(EditableToolpath, RemovingSharedRootRejectsLateGenerationWithoutThrowing)
{
    Host host;host.Source(Line());host.Generate();auto task=host.Document->GenerateAsync(host.Plan);
    while(!task->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto roots=host.Command("SetPlanRoots");roots["roots"]=Array{};roots["expectedPlanFingerprint"]=At(host.Document->ReadPlan(host.Plan),"definitionFingerprint");host.Document->Execute(roots);
    EXPECT_TRUE(host.Document->Publish(task));Complete(host,0);
}

TEST(EditableToolpath, DeletingSharedSourceChecksDependentsInEveryPlan)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto root=host.Paths()[0];const auto firstPlan=host.Plan;
    host.Plan=host.Document->CreatePlan("dependent");auto roots=host.Command("SetPlanRoots");roots["roots"]=Array{ToData(root.Port)};roots["expectedPlanFingerprint"]=At(host.Document->ReadPlan(host.Plan),"definitionFingerprint");host.Document->Execute(roots);
    host.Add("icax.edit.reverse",{root.Port},{});host.Generate();Complete(host,1);host.Plan=firstPlan;
    auto erase=host.Command("DeleteSource");const auto operation=ID(At(source,"operationId"));erase["operationId"]=operation;erase["expectedOperationFingerprint"]=At(host.Document->ReadOperation(operation),"definitionFingerprint");
    const auto before=host.Document->ExportDocument();
    EXPECT_THROW(host.Document->Execute(erase),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
    erase["dependencyPolicy"]=std::string("KeepInvalid");EXPECT_NO_THROW(host.Document->Execute(erase));
}

TEST(EditableToolpath, RebindRejectsUnobservedAndStaleInputsBeforeChangingDefinition)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto operation=host.Add("icax.edit.reverse",{a.Port},{});host.Generate();
    const auto id=ID(At(operation,"operationId"));auto rebind=host.Command("Rebind");rebind["operationId"]=id;rebind["expectedOperationFingerprint"]=At(host.Document->ReadOperation(id),"definitionFingerprint");
    rebind["inputs"]=At(host.Document->ReadOperation(id),"inputs");const auto before=host.Document->ExportDocument();
    EXPECT_THROW(host.Document->Execute(rebind),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
    rebind["expectedInputs"]=Map{{Key(a.Port),a.Fingerprint}};host.Update(ID(At(source,"operationId")),Line(20));host.Generate();
    const auto changed=host.Document->ExportDocument();
    EXPECT_THROW(host.Document->Execute(rebind),std::invalid_argument);EXPECT_EQ(changed,host.Document->ExportDocument());
    const auto sourceRecord=MapAt(host.Document->ReadOperation(ID(At(source,"operationId"))),"result");
    rebind["expectedInputs"]=Map{{Key(a.Port),Text(ArrayAt(sourceRecord,"outputs")[0].To<Map>(),"fingerprint")}};
    EXPECT_NO_THROW(host.Document->Execute(rebind));host.Generate();Complete(host,1);
}

TEST(EditableToolpath, ExtensionChannelNamesCannotMaskIntrinsicGeometryChanges)
{
    auto value=Line();for(const auto name:{"geometry","topology","provenance","frame","extensionChannels"})value.Channels[name]=std::string("constant");
    const auto original=ToolpathFingerprint(value);value.Geometry.FrameId="DifferentFrame";EXPECT_NE(original,ToolpathFingerprint(value));
    const auto frameChanged=ToolpathFingerprint(value);std::get<CurveSegment3>(value.Geometry.Segments[0].Curve).Range.Reversed=true;EXPECT_NE(frameChanged,ToolpathFingerprint(value));
}

TEST(EditableToolpath, GenericOperationCommandsCannotBypassSourceParameterizationChecks)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto operation=ID(At(source,"operationId"));const auto before=host.Document->ExportDocument();
    auto update=host.Command("UpdateOperation");update["operationId"]=operation;update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(operation),"definitionFingerprint");update["parameters"]=Map{{"value",ToData(Circle())}};
    EXPECT_THROW(host.Document->Execute(update),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
    update["kind"]=std::string("Rebind");update["inputs"]=Array{};
    EXPECT_THROW(host.Document->Execute(update),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
}

TEST(EditableToolpath, NativeJournalRestoresDefinitionsDerivedIdentitiesDeletionAndReceipts)
{
    Host host;host.Document.reset();host.Db=Repository();
    const auto journal=(std::filesystem::temp_directory_path()/("icax-intent-"+Key(GenerateNewUUID())+".log")).string();
    struct Cleanup{std::string Path;~Cleanup(){std::error_code error;std::filesystem::remove(Path,error);}} cleanup{journal};
    host.Db->OpenOperationLog(journal,true,"ICAX_INTENT_NATIVE_TEST",1);
    host.Document=std::make_unique<CEditableToolpathDocument>(host.Db,IntentId{},host.Registry);host.Plan=host.Document->CreatePlan("journal");
    auto create=host.Command("CreateSource");create["value"]=ToData(Line());const auto receipt=host.Document->Execute(create);host.Generate();const auto a=host.Paths()[0];
    host.Add("icax.microjoint",{a.Port},{{"gaps",Array{Gap(a,.4,.6)}}});host.Generate();const auto parts=host.Paths();host.Delete({parts[0].Port});
    auto labels=host.Command("SetLabels");labels["pathId"]=parts[1].PathId;labels["userNumber"]=std::string("retained-125");host.Document->Execute(labels);
    host.Update(ID(At(receipt,"operationId")),Line(20));host.Generate();ASSERT_TRUE(host.Db->Undo());host.Generate();ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,1);
    const auto document=host.Document->Id();const auto expected=host.Document->ExportDocument();const auto fingerprint=host.Document->Resolve(host.Plan).Fingerprint;
    host.Db->CloseOperationLog();auto recovered=Repository();recovered->ReplayOperationLog(journal,"ICAX_INTENT_NATIVE_TEST",1);
    CEditableToolpathDocument reopened(recovered,document,host.Registry);const auto result=reopened.Resolve(host.Plan);
    ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1u,result.Paths.size());EXPECT_EQ(fingerprint,result.Fingerprint);EXPECT_EQ(expected,reopened.ExportDocument());
    EXPECT_EQ(parts[1].PathId,result.Paths[0].PathId);EXPECT_EQ("retained-125",result.Paths[0].UserNumber);EXPECT_EQ(receipt,reopened.Execute(create));
}

TEST(EditableToolpath, PreviewRejectsStaleSourceAndDownstreamValues)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.edit.reverse",{a.Port},{});host.Generate();const auto b=host.Paths()[0];
    host.Update(ID(At(source,"operationId")),Line(20));
    EXPECT_THROW(host.Document->Preview("icax.edit.reverse",{},{a.Port}),std::invalid_argument);
    EXPECT_THROW(host.Document->Preview("icax.edit.reverse",{},{b.Port}),std::invalid_argument);
    host.Generate();EXPECT_NO_THROW(host.Document->Preview("icax.edit.reverse",{},{b.Port}));
}

TEST(EditableToolpath, OwnPathLockProtectsEveryUpstreamMutationAndPreservesLabels)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];
    const auto split=host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}});host.Generate();const auto child=host.Paths()[0];
    auto labels=host.Command("SetLabels");labels["pathId"]=child.PathId;labels["name"]=std::string("kept name");labels["userNumber"]=std::string("29");host.Document->Execute(labels);
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{child.PathId};lock["locked"]=true;host.Document->Execute(lock);
    EXPECT_THROW(host.Update(ID(At(source,"operationId")),Line(20)),std::invalid_argument);
    for(const auto* kind:{"UpdateOperation","Rebind","RemoveOperation","SetOperationEnabled"})
    {auto c=host.Command(kind);c["operationId"]=At(split,"operationId");c["expectedOperationFingerprint"]=At(host.Document->ReadOperation(ID(At(split,"operationId"))),"definitionFingerprint");c["enabled"]=false;
    EXPECT_THROW(host.Document->Execute(c),std::invalid_argument);}
    auto deletion=host.Command("DeleteSource");deletion["operationId"]=At(source,"operationId");deletion["expectedOperationFingerprint"]=At(host.Document->ReadOperation(ID(At(source,"operationId"))),"definitionFingerprint");deletion["dependencyPolicy"]=std::string("KeepInvalid");
    EXPECT_THROW(host.Document->Execute(deletion),std::invalid_argument);
    EXPECT_THROW(host.Add("icax.edit.reverse",{child.Port},{}),std::invalid_argument);
    labels["commandId"]=GenerateNewUUID();labels["expectedLabelsVersion"]=host.Document->ReadPath(host.Plan,child.Port).LabelsVersion;
    EXPECT_THROW(host.Document->Execute(labels),std::invalid_argument);
    lock["commandId"]=GenerateNewUUID();lock["locked"]=false;host.Document->Execute(lock);const auto unlocked=host.Document->ReadPath(host.Plan,child.Port);
    EXPECT_FALSE(unlocked.OwnLocked);EXPECT_EQ("kept name",unlocked.Name);EXPECT_EQ("29",unlocked.UserNumber);EXPECT_NO_THROW(host.Update(ID(At(source,"operationId")),Line(20)));
}

TEST(EditableToolpath, SharedSourceMutationChecksLocksInEveryPlan)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto other=host.Document->CreatePlan("other");
    auto roots=host.Command("SetPlanRoots");roots["planId"]=other;roots["roots"]=Array{ToData(a.Port)};roots["expectedPlanFingerprint"]=At(host.Document->ReadPlan(other),"definitionFingerprint");host.Document->Execute(roots);
    auto group=host.Command("Group");group["planId"]=other;group["groupId"]=std::string("other-lock");group["members"]=Array{a.PathId};group["locked"]=true;host.Document->Execute(group);
    EXPECT_THROW(host.Update(ID(At(source,"operationId")),Line(20)),std::invalid_argument);
}

TEST(EditableToolpath, PendingDeclaredOutputCanBeDisabledAndLockedInOneAtomicBatch)
{
    Host host;const auto createId=GenerateNewUUID(),reverseId=GenerateNewUUID();
    auto create=host.Command("CreateSource");create["commandId"]=createId;create["value"]=ToData(Line());create["name"]=std::string("pending");create["userNumber"]=std::string("73");
    auto reverse=host.Command("AddOperation");reverse["commandId"]=reverseId;reverse["typeId"]=std::string("icax.edit.reverse");reverse["parameters"]=Map{};reverse["inputs"]=Array{Map{{"slot",std::string("path")},{"source",Map{{"fromCommand",createId}}},{"replace",true},{"ranges",Array{}}}};
    auto disable=host.Command("SetParticipation");disable["targets"]=Array{Map{{"fromCommand",reverseId}}};disable["enabled"]=false;
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{Map{{"fromCommand",createId},{"field",std::string("pathId")}}};lock["locked"]=true;
    const auto replies=host.Document->ExecuteBatch({create,reverse,disable,lock});const auto path=ID(At(replies[0],"pathId"));
    auto inspection=host.Document->InspectPlan(host.Plan);ASSERT_EQ(1u,inspection.Paths.size());const auto& pending=inspection.Paths[0];
    EXPECT_EQ(path,pending.PathId);EXPECT_EQ("Pending",pending.ResultState);EXPECT_TRUE(pending.OwnLocked);EXPECT_FALSE(pending.OwnParticipating);EXPECT_EQ("Disabled",pending.ParticipationReason);EXPECT_EQ("pending",pending.Name);EXPECT_EQ("73",pending.UserNumber);
    for(const auto& d:inspection.Diagnostics)EXPECT_EQ(std::string::npos,d.Message.find("NEEDS_REBIND"));
    EXPECT_TRUE(host.Document->Resolve(host.Plan).IntentionalEmpty);host.Generate();inspection=host.Document->InspectPlan(host.Plan);ASSERT_EQ(1u,inspection.Paths.size());EXPECT_EQ(path,inspection.Paths[0].PathId);EXPECT_TRUE(inspection.Paths[0].OwnLocked);EXPECT_EQ("Current",inspection.Paths[0].ResultState);
    ASSERT_TRUE(host.Db->Undo());EXPECT_TRUE(host.Document->InspectPlan(host.Plan).Paths.empty());ASSERT_TRUE(host.Db->Redo());EXPECT_EQ(path,host.Document->InspectPlan(host.Plan).Paths[0].PathId);
}

TEST(EditableToolpath, PendingTargetsDoNotRequireAStaleOutputAndRetainRevisionAndLockChecks)
{
    for(const auto& kind:{std::string("DeletePaths"),std::string("SetParticipation")})
    {
        Host host;host.Source(Line());host.Generate();const auto source=host.Paths()[0];const auto operation=host.Add("icax.edit.reverse",{source.Port},{});
        const auto port=PortFromData(MapAt(operation,"port"));auto request=host.Command(kind);request["targets"]=Array{ToData(port)};request["allowStaleTargets"]=true;if(kind=="SetParticipation")request["enabled"]=false;
        request["expectedProducers"]=Map{{Key(port),At(operation,"definitionFingerprint")}};
        const auto before=host.Document->ExportDocument(),planBefore=host.Document->ReadPlan(host.Plan);auto wrong=request;wrong["commandId"]=GenerateNewUUID();wrong["expectedProducers"]=Map{{Key(port),std::string("obsolete producer")}};
        EXPECT_THROW(host.Document->Execute(wrong),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
        const auto result=host.Document->Execute(request);EXPECT_EQ(result,host.Document->Execute(request));EXPECT_TRUE(host.Document->Resolve(host.Plan).IntentionalEmpty);
        const auto after=host.Document->ExportDocument(),planAfter=host.Document->ReadPlan(host.Plan);ASSERT_TRUE(host.Db->Undo());EXPECT_EQ(planBefore,host.Document->ReadPlan(host.Plan));
        const auto restored=host.Document->InspectPlan(host.Plan);ASSERT_EQ(restored.Paths.size(),1u);EXPECT_EQ(restored.Paths[0].Port,port);EXPECT_EQ(restored.Paths[0].PathId,source.PathId);EXPECT_TRUE(restored.Paths[0].OwnParticipating);
        ASSERT_TRUE(host.Db->Redo());EXPECT_EQ(planAfter,host.Document->ReadPlan(host.Plan));EXPECT_TRUE(host.Document->Resolve(host.Plan).IntentionalEmpty);
        auto db=Repository();const auto id=CEditableToolpathDocument::ImportDocument(db,after);CEditableToolpathDocument reopened(db,id,host.Registry);EXPECT_TRUE(reopened.Resolve(host.Plan).IntentionalEmpty);
    }
    Host locked;locked.Source(Line());locked.Generate();const auto source=locked.Paths()[0];const auto operation=locked.Add("icax.edit.reverse",{source.Port},{});const auto port=PortFromData(MapAt(operation,"port"));
    auto lock=locked.Command("SetPathLocked");lock["pathIds"]=Array{source.PathId};lock["locked"]=true;locked.Document->Execute(lock);
    auto erase=locked.Command("DeletePaths");erase["targets"]=Array{ToData(port)};erase["allowStaleTargets"]=true;erase["expectedProducers"]=Map{{Key(port),At(operation,"definitionFingerprint")}};EXPECT_THROW(locked.Document->Execute(erase),std::invalid_argument);
}

TEST(EditableToolpath, CopyNeverFreezesStaleOrPendingInspectionGeometry)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.edit.reverse",{a.Port},{});host.Generate();const auto b=host.Paths()[0];
    host.Update(ID(At(source,"operationId")),Line(20));auto copy=host.Command("CopyPaths");copy["targets"]=Array{ToData(b.Port)};copy["expectedInputs"]=Map{{Key(b.Port),b.Fingerprint}};copy["allowStaleTargets"]=true;
    EXPECT_THROW(host.Document->Execute(copy),std::invalid_argument);const auto stale=host.Document->InspectPlan(host.Plan);ASSERT_EQ(1u,stale.Paths.size());EXPECT_EQ("Stale",stale.Paths[0].ResultState);
    host.Generate();copy["commandId"]=GenerateNewUUID();copy["expectedInputs"]=host.Expected({b.Port});EXPECT_NO_THROW(host.Document->Execute(copy));
    Host pending;pending.Source(Line());pending.Generate();const auto p=pending.Paths()[0];const auto operation=pending.Add("icax.edit.reverse",{p.Port},{});auto pendingCopy=pending.Command("CopyPaths");pendingCopy["targets"]=Array{At(operation,"port")};pendingCopy["expectedProducers"]=Map{{Key(PortFromData(MapAt(operation,"port"))),At(operation,"definitionFingerprint")}};
    EXPECT_THROW(pending.Document->Execute(pendingCopy),std::invalid_argument);
}

TEST(EditableToolpath, ReadPathValidatesConsumedIntermediateResultsAgainstTheWholeGraph)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.edit.reverse",{a.Port},{});host.Generate();const auto b=host.Paths()[0];host.Add("icax.edit.translate",{b.Port},{{"delta",Array{2.,0.,0.}}});host.Generate();
    EXPECT_EQ(a.Fingerprint,host.Document->ReadPath(host.Plan,a.Port).Fingerprint);EXPECT_EQ(b.Fingerprint,host.Document->ReadPath(host.Plan,b.Port).Fingerprint);
    const auto other=host.Document->CreatePlan("empty");
    EXPECT_THROW(host.Document->ReadPath(other,b.Port),std::invalid_argument);
    host.Update(ID(At(source,"operationId")),Line(20));
    EXPECT_THROW(host.Document->ReadPath(host.Plan,a.Port),std::invalid_argument);
    EXPECT_THROW(host.Document->ReadPath(host.Plan,b.Port),std::invalid_argument);host.Generate();EXPECT_NO_THROW(host.Document->ReadPath(host.Plan,b.Port));
}
TEST(EditableToolpath, BatchReadsMatchIndividualPortsWithLabelsGroupsAndParticipation)
{
    Host host;host.Source(Line());host.Generate();const auto source=host.Paths().at(0);auto group=host.Command("Group");group["groupId"]=std::string("read-batch");group["members"]=Array{source.PathId};host.Document->Execute(group);
    host.Add("icax.edit.split",{source.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(source,.4))}}}}});host.Generate();const auto split=host.Paths();ASSERT_EQ(2u,split.size());
    const auto reverse=host.Add("icax.edit.reverse",{split[0].Port},{});host.Generate();const auto reversed=host.Document->ReadPath(host.Plan,DeclaredToolpathPort(ID(At(reverse,"operationId")),"main"));
    auto label=host.Command("SetLabels");label["pathId"]=reversed.PathId;label["name"]=std::string("batch row");label["userNumber"]=std::string("812");label["expectedLabelsVersion"]=static_cast<unsigned long long>(reversed.LabelsVersion);host.Document->Execute(label);
    auto disable=host.Command("SetParticipation");disable["targets"]=Array{ToData(reversed.Port)};disable["expectedInputs"]=Map{{Key(reversed.Port),reversed.Fingerprint}};disable["enabled"]=false;host.Document->Execute(disable);
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{reversed.PathId};lock["locked"]=true;host.Document->Execute(lock);
    group["commandId"]=GenerateNewUUID();group["enabled"]=false;group["locked"]=true;host.Document->Execute(group);
    const std::vector<SPortRef> ports{source.Port,split[0].Port,reversed.Port,split[1].Port,reversed.Port};const auto batch=host.Document->ReadPaths(host.Plan,ports);ASSERT_EQ(ports.size(),batch.size());
    const auto view=[](const SResolvedPath& path){return Map{{"path",path.PathId},{"port",ToData(path.Port)},{"name",path.Name},{"number",path.UserNumber},{"fingerprint",path.Fingerprint},{"value",ToData(path.Value)},
        {"labels",static_cast<unsigned long long>(path.LabelsVersion)},{"group",path.GroupId},{"participating",path.Participating},{"locked",path.Locked},{"state",path.ResultState},{"ownLocked",path.OwnLocked},{"ownParticipating",path.OwnParticipating},{"reason",path.ParticipationReason}};};
    for(std::size_t i=0;i<ports.size();++i)EXPECT_EQ(view(host.Document->ReadPath(host.Plan,ports[i])),view(batch[i]));
    EXPECT_EQ("read-batch",batch[2].GroupId);EXPECT_EQ("batch row",batch[2].Name);EXPECT_TRUE(batch[2].OwnLocked);EXPECT_FALSE(batch[2].OwnParticipating);EXPECT_EQ("Disabled",batch[2].ParticipationReason);
}
TEST(EditableToolpath, BatchReadsRejectAStaleRequestedPortAndKeepUnrelatedCurrentPortsReadable)
{
    Host host;const auto first=host.Source(Line());const auto second=host.Source(Line(20));host.Generate();SResolvedPath a,b;
    for(const auto& path:host.Paths())if(path.Port.OperationId==ID(At(first,"operationId")))a=path;else b=path;
    host.Update(ID(At(first,"operationId")),Line(30));const auto current=host.Document->ReadPaths(host.Plan,{b.Port});ASSERT_EQ(1u,current.size());EXPECT_EQ(b.Fingerprint,current[0].Fingerprint);
    EXPECT_THROW(host.Document->ReadPaths(host.Plan,{b.Port,a.Port}),std::invalid_argument);
    EXPECT_THROW(host.Document->ReadPaths(host.Plan,{SPortRef{ID(At(second,"operationId")),GenerateNewUUID()}}),std::invalid_argument);
    const auto other=host.Document->CreatePlan("batch empty");EXPECT_THROW(host.Document->ReadPaths(other,{b.Port}),std::invalid_argument);EXPECT_TRUE(host.Document->ReadPaths(other,{}).empty());
}
TEST(EditableToolpath, BatchReadsIncludeConsumedReplaceAndReadOnlyAncestors)
{
    Host host;const auto first=host.Source(Line());host.Source(Line(20));host.Generate();SResolvedPath selected,reference;
    for(const auto& path:host.Paths())if(path.Port.OperationId==ID(At(first,"operationId")))selected=path;else reference=path;
    host.Add("icax.edit.reverse",{reference.Port},{});host.Generate();SOperationType type;type.TypeId="test.batch-read-reference";type.MinimumInputs=2;type.MaximumInputs=2;type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    type.Evaluate=[](const SOperationEvaluation& input,const IToolpathGeometryServices&){auto value=input.Inputs[0].Value;for(auto& segment:value.Geometry.Segments){const auto range=Range(segment);segment.Sources.push_back({input.Inputs[0].Binding.Source,segment.Id,range,range,"Exact",0});}return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};};host.Registry->Register(std::move(type));
    auto add=host.Command("AddOperation");add["typeId"]=std::string("test.batch-read-reference");add["parameters"]=Map{};add["inputs"]=Array{ToData(SOperationInput{"selected",selected.Port,true,{}}),ToData(SOperationInput{"reference",reference.Port,false,{}})};add["expectedInputs"]=Map{{Key(selected.Port),selected.Fingerprint},{Key(reference.Port),reference.Fingerprint}};
    const auto operation=host.Document->Execute(add);host.Generate();const auto output=DeclaredToolpathPort(ID(At(operation,"operationId")),"main");const auto paths=host.Document->ReadPaths(host.Plan,{selected.Port,reference.Port,output});ASSERT_EQ(3u,paths.size());
    EXPECT_EQ(selected.Fingerprint,paths[0].Fingerprint);EXPECT_EQ(reference.Fingerprint,paths[1].Fingerprint);EXPECT_EQ(selected.PathId,paths[2].PathId);for(const auto& path:paths)EXPECT_EQ("Current",path.ResultState);
}

TEST(EditableToolpath, InspectionConstraintsKeepDisabledChildrenAndUseTheCommonExpansion)
{
    Host host;host.Source(Line());host.Source(Line(20));host.Generate();const auto originals=host.Paths();const auto a=originals[0],b=originals[1];
    host.Add("icax.edit.segment",{a.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}});host.Generate();auto current=host.Paths();SResolvedPath child;for(const auto& p:current)if(p.Port.OperationId!=b.Port.OperationId){child=p;break;}
    auto order=host.Command("SetExecutionConstraints");order["constraints"]=Map{{"blocks",Array{Map{{"id",std::string("all")},{"members",Array{ToData(a.Port),ToData(b.Port)}},{"atomic",true}}}}};host.Document->Execute(order);
    auto disable=host.Command("SetParticipation");disable["targets"]=Array{ToData(child.Port)};disable["expectedInputs"]=host.Expected({child.Port});disable["enabled"]=false;host.Document->Execute(disable);
    const auto inspection=host.Document->InspectPlan(host.Plan);EXPECT_EQ(3u,inspection.Paths.size());EXPECT_EQ(2u,host.Paths().size());
    for(const auto& data:ArrayAt(inspection.DisplayConstraints,"blocks")){const auto block=data.To<Map>();if(Text(block,"id")=="all")EXPECT_EQ(3u,ArrayAt(block,"members").size());}
    for(const auto& data:ArrayAt(inspection.ExecutionConstraints,"blocks")){const auto block=data.To<Map>();if(Text(block,"id")=="all")EXPECT_EQ(2u,ArrayAt(block,"members").size());}
    host.Delete({b.Port});const auto after=host.Document->InspectPlan(host.Plan);for(const auto& p:after.Paths)if(p.Port==b.Port){EXPECT_EQ("Deleted",p.ParticipationReason);EXPECT_FALSE(p.OwnParticipating);}
}

TEST(EditableToolpath, SplitChildrenInheritDisabledAndLockedGroupsAndOwnUnlockRemainsIndependent)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];auto group=host.Command("Group");group["groupId"]=std::string("G");group["members"]=Array{a.PathId};host.Document->Execute(group);
    host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(a,.4))}}}}});host.Generate();const auto child=host.Paths()[0];
    group["commandId"]=GenerateNewUUID();group["enabled"]=false;group["locked"]=true;host.Document->Execute(group);const auto inspected=host.Document->InspectPlan(host.Plan);ASSERT_EQ(2u,inspected.Paths.size());for(const auto& p:inspected.Paths){EXPECT_EQ("G",p.GroupId);EXPECT_TRUE(p.Locked);EXPECT_FALSE(p.Participating);EXPECT_TRUE(p.OwnParticipating);}
    auto own=host.Command("SetPathLocked");own["pathIds"]=Array{child.PathId};own["locked"]=true;host.Document->Execute(own);own["commandId"]=GenerateNewUUID();own["locked"]=false;host.Document->Execute(own);const auto unlocked=host.Document->ReadPath(host.Plan,child.Port);EXPECT_FALSE(unlocked.OwnLocked);EXPECT_TRUE(unlocked.Locked);
    EXPECT_THROW(host.Update(ID(At(source,"operationId")),Line(20)),std::invalid_argument);
}

TEST(EditableToolpath, SplitCanRetainOnlyItsOriginalFirstPhysicalChildAcrossReverseAndUndo)
{
    Host host;const auto source=host.Source(Line());host.Generate();const auto a=host.Paths()[0];auto labels=host.Command("SetLabels");labels["pathId"]=a.PathId;labels["userNumber"]=std::string("41");labels["expectedLabelsVersion"]=a.LabelsVersion;host.Document->Execute(labels);
    host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K1")},{"anchor",ToData(Anchor(a,.3))}},Map{{"id",std::string("K2")},{"anchor",ToData(Anchor(a,.7))}}}},{"retainFirstIdentity",true}});host.Generate();Complete(host,3);
    const auto verify=[&](){std::size_t count=0;for(const auto& p:host.Paths())if(p.PathId==a.PathId){++count;EXPECT_EQ("41",p.UserNumber);EXPECT_DOUBLE_EQ(0.,Range(p.Value.Geometry.Segments.front()).First);EXPECT_DOUBLE_EQ(.3,Range(p.Value.Geometry.Segments.front()).Last);}EXPECT_EQ(1u,count);};verify();
    auto reversed=Line();SetRange(reversed.Geometry.Segments[0],{0,1,true});host.Update(ID(At(source,"operationId")),reversed);host.Generate();Complete(host,3);verify();
    ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,3);verify();ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,1);EXPECT_EQ(a.PathId,host.Paths()[0].PathId);
    ASSERT_TRUE(host.Db->Redo());host.Generate();Complete(host,3);verify();
}

TEST(EditableToolpath, DisabledGroupAllowsProcessingEditsAndStillEmitsNoMachiningPaths)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];auto group=host.Command("Group");group["groupId"]=std::string("disabled");group["members"]=Array{a.PathId};group["enabled"]=false;host.Document->Execute(group);
    auto reverse=host.Command("AddOperation");reverse["typeId"]=std::string("icax.edit.reverse");reverse["parameters"]=Map{};reverse["inputs"]=Array{Map{{"slot",std::string("path")},{"source",ToData(a.Port)},{"replace",true},{"ranges",Array{}}}};reverse["expectedInputs"]=Map{{Key(a.Port),a.Fingerprint}};
    EXPECT_NO_THROW(host.Document->Execute(reverse));host.Generate();Complete(host,0);const auto inspected=host.Document->InspectPlan(host.Plan);ASSERT_EQ(1u,inspected.Paths.size());EXPECT_EQ("disabled",inspected.Paths[0].GroupId);EXPECT_FALSE(inspected.Paths[0].Participating);EXPECT_EQ("Current",inspected.Paths[0].ResultState);
}

TEST(EditableToolpath, InsertBeforeRewiresTheExistingConsumerInOneUndoStep)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto consumer=host.Add("icax.edit.translate",{a.Port},{{"delta",Array{1.,0.,0.}}});host.Generate();const auto b=host.Paths()[0];host.Add("icax.edit.reverse",{b.Port},{});host.Generate();const auto terminal=host.Paths()[0];
    auto insertion=host.Command("AddOperation");insertion["typeId"]=std::string("icax.edit.reverse");insertion["parameters"]=Map{};insertion["inputs"]=Array{Map{{"slot",std::string("path")},{"source",ToData(a.Port)},{"replace",true},{"ranges",Array{}}}};insertion["expectedInputs"]=Map{{Key(a.Port),a.Fingerprint}};insertion["insertBefore"]=Map{{"operationId",At(consumer,"operationId")},{"inputSlot",std::string("input/0")},{"expectedOperationFingerprint",At(host.Document->ReadOperation(ID(At(consumer,"operationId"))),"definitionFingerprint")}};
    const auto result=host.Document->Execute(insertion);const auto rewired=host.Document->ReadOperation(ID(At(consumer,"operationId")));EXPECT_EQ(PortFromData(MapAt(result,"port")),PortFromData(MapAt(ArrayAt(rewired,"inputs")[0].To<Map>(),"source")));host.Generate();Complete(host,1);EXPECT_EQ(terminal.PathId,host.Paths()[0].PathId);
    ASSERT_TRUE(host.Db->Undo());const auto restored=host.Document->ReadOperation(ID(At(consumer,"operationId")));EXPECT_EQ(a.Port,PortFromData(MapAt(ArrayAt(restored,"inputs")[0].To<Map>(),"source")));host.Generate();Complete(host,1);EXPECT_EQ(terminal.PathId,host.Paths()[0].PathId);
    auto wrong=insertion;wrong["commandId"]=GenerateNewUUID();wrong["insertBefore"]=Map{{"operationId",At(consumer,"operationId")},{"inputSlot",std::string("missing")},{"expectedOperationFingerprint",At(host.Document->ReadOperation(ID(At(consumer,"operationId"))),"definitionFingerprint")}};const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Document->Execute(wrong),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{terminal.PathId};lock["locked"]=true;host.Document->Execute(lock);insertion["commandId"]=GenerateNewUUID();EXPECT_THROW(host.Document->Execute(insertion),std::invalid_argument);
}

TEST(EditableToolpath, InitializationHostTransactionCanSkipUserUndoHistory)
{
    Host host;const auto history=host.Db->GetUndoArray().size();auto source=host.Command("CreateSource");source["value"]=ToData(Line());SEditableToolpathHostEdit edit;edit.RecordUndo=false;host.Document->Execute(source,edit);EXPECT_EQ(history,host.Db->GetUndoArray().size());host.Generate();Complete(host,1);
}

TEST(EditableToolpath, ClonePlanCopiesAnIndependentEditableGraphWithLabelsPoliciesAndExactAnchors)
{
    Host host;const auto oldSetup=GenerateNewUUID(),newSetup=GenerateNewUUID(),oldPlacement=GenerateNewUUID(),newPlacement=GenerateNewUUID();auto value=Line();value.Geometry.FrameId="placement/"+Key(oldPlacement);value.Channels["owner"]=Map{{"setupId",oldSetup},{"placementId",oldPlacement}};
    const auto source=host.Source(value);host.Generate();const auto a=host.Paths()[0];auto group=host.Command("Group");group["groupId"]=std::string("G");group["members"]=Array{a.PathId};host.Document->Execute(group);
    const auto channel=host.Add("icax.edit.channels",{a.Port},{{"patch",Map{{"owner",Map{{"setupId",oldSetup},{"placementId",oldPlacement},{"sourceOperationId",At(source,"operationId")}}}}}});host.Generate();const auto edited=host.Paths()[0];
    host.Add("icax.edit.split",{edited.Port},{{"cuts",Array{Map{{"id",std::string("K")},{"anchor",ToData(Anchor(edited,.4))}}}},{"retainFirstIdentity",true}});host.Generate();const auto originalPaths=host.Paths();ASSERT_EQ(2u,originalPaths.size());
    auto disable=host.Command("SetParticipation");disable["targets"]=Array{ToData(originalPaths[0].Port)};disable["expectedInputs"]=host.Expected({originalPaths[0].Port});disable["enabled"]=false;host.Document->Execute(disable);
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{originalPaths[1].PathId};lock["locked"]=true;host.Document->Execute(lock);
    const auto target=host.Document->CreatePlan("copy");auto clone=host.Command("ClonePlan");clone["planId"]=target;clone["sourcePlanId"]=host.Plan;clone["expectedSourcePlanFingerprint"]=At(host.Document->ReadPlan(host.Plan),"graphFingerprint");clone["labelScope"]=std::string("copied-setup");
    SEditableToolpathHostEdit edit;auto& transform=edit.PlanClones[ID(At(clone,"commandId"))];transform.ExternalIdentityRemap={{oldSetup,newSetup},{oldPlacement,newPlacement}};transform.Evidence={{"targetSetup",newSetup},{"targetPlacement",newPlacement}};transform.TransformSource=[](IntentId,IntentId,const SToolpathValue& remapped){return SPlanCloneSourceTransform{remapped,true};};
    const auto result=host.Document->Execute(clone,edit);const auto copiedSource=ID(At(MapAt(result,"operationIds"),Key(ID(At(source,"operationId")))));EXPECT_NE(ID(At(source,"operationId")),copiedSource);
    const auto sourceValue=ToolpathFromData(MapAt(MapAt(host.Document->ReadOperation(copiedSource),"parameters"),"value"));EXPECT_EQ(1u,sourceValue.Geometry.Segments.size());EXPECT_EQ("placement/"+Key(newPlacement),sourceValue.Geometry.FrameId);EXPECT_EQ(newSetup,ID(At(MapAt(sourceValue.Channels,"owner"),"setupId")));
    auto pending=host.Document->InspectPlan(target);ASSERT_EQ(2u,pending.Paths.size());for(const auto& p:pending.Paths){EXPECT_EQ("Pending",p.ResultState);EXPECT_EQ("G",p.GroupId);EXPECT_TRUE(p.Value.Geometry.Segments.empty());}
    auto db=Repository();const auto imported=CEditableToolpathDocument::ImportDocument(db,host.Document->ExportDocument());CEditableToolpathDocument reopened(db,imported,host.Registry);EXPECT_EQ(2u,reopened.InspectPlan(target).Paths.size());
    auto job=host.Document->GenerateAsync(target);while(!job->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(host.Document->Publish(job));const auto ready=host.Document->InspectPlan(target);ASSERT_EQ(EOutputCompleteness::Complete,ready.Status);ASSERT_EQ(2u,ready.Paths.size());EXPECT_EQ(1u,host.Document->Resolve(target).Paths.size());
    for(const auto& p:ready.Paths){EXPECT_EQ("Current",p.ResultState);EXPECT_EQ("G",p.GroupId);EXPECT_EQ(newSetup,ID(At(MapAt(p.Value.Channels,"owner"),"setupId")));EXPECT_EQ(copiedSource,ID(At(MapAt(p.Value.Channels,"owner"),"sourceOperationId")));}
    EXPECT_EQ(result,host.Document->Execute(clone,edit));auto changed=edit;changed.PlanClones[ID(At(clone,"commandId"))].Evidence["different"]=true;EXPECT_THROW(host.Document->Execute(clone,changed),std::invalid_argument);
    ASSERT_TRUE(host.Db->Undo());EXPECT_TRUE(host.Document->InspectPlan(target).Paths.empty());ASSERT_TRUE(host.Db->Redo());EXPECT_EQ(2u,host.Document->InspectPlan(target).Paths.size());
}

TEST(EditableToolpath, ClonePlanRejectsFalseReframeCertificatesBeforeAnyWrite)
{
    Host host;host.Source(Line());host.Generate();const auto target=host.Document->CreatePlan("copy");auto clone=host.Command("ClonePlan");clone["planId"]=target;clone["sourcePlanId"]=host.Plan;clone["expectedSourcePlanFingerprint"]=At(host.Document->ReadPlan(host.Plan),"graphFingerprint");SEditableToolpathHostEdit edit;
    edit.PlanClones[ID(At(clone,"commandId"))].TransformSource=[](IntentId,IntentId,const SToolpathValue& source){auto changed=source;std::get<Segment3>(std::get<CurveSegment3>(changed.Geometry.Segments[0].Curve).Curve).End.X=30.;return SPlanCloneSourceTransform{changed,true};};
    const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Document->Execute(clone,edit),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
}

TEST(EditableToolpath, ClonePendingInspectionDoesNotReviveRetiredOutputIdentities)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];const auto split=host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("K1")},{"anchor",ToData(Anchor(a,.3))}},Map{{"id",std::string("K2")},{"anchor",ToData(Anchor(a,.7))}}}}});host.Generate();Complete(host,3);
    auto update=host.Command("UpdateOperation");update["operationId"]=At(split,"operationId");update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(ID(At(split,"operationId"))),"definitionFingerprint");update["parameters"]=Map{{"cuts",Array{Map{{"id",std::string("K3")},{"anchor",ToData(Anchor(a,.5))}}}}};host.Document->Execute(update);host.Generate();Complete(host,2);
    const auto target=host.Document->CreatePlan("copy");auto clone=host.Command("ClonePlan");clone["planId"]=target;clone["sourcePlanId"]=host.Plan;clone["expectedSourcePlanFingerprint"]=At(host.Document->ReadPlan(host.Plan),"graphFingerprint");host.Document->Execute(clone);const auto pending=host.Document->InspectPlan(target);ASSERT_EQ(2u,pending.Paths.size());for(const auto& p:pending.Paths)EXPECT_EQ("Pending",p.ResultState);
}

TEST(EditableToolpath, DetachedIdentityAppearsOnlyOnItsNewSourceInInspection)
{
    Host host;host.Source(Line());host.Generate();const auto a=host.Paths()[0];host.Add("icax.edit.reverse",{a.Port},{});host.Generate();const auto b=host.Paths()[0];auto constraints=host.Command("SetExecutionConstraints");constraints["constraints"]=Map{{"blocks",Array{Map{{"id",std::string("kept")},{"members",Array{ToData(b.Port)}},{"atomic",true}}}}};host.Document->Execute(constraints);
    auto detach=host.Command("Detach");detach["targets"]=Array{ToData(b.Port)};detach["expectedInputs"]=host.Expected({b.Port});host.Document->Execute(detach);host.Generate();const auto inspection=host.Document->InspectPlan(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,inspection.Status);ASSERT_EQ(1u,inspection.Paths.size());EXPECT_EQ(b.PathId,inspection.Paths[0].PathId);EXPECT_NE(b.Port,inspection.Paths[0].Port);ASSERT_EQ(1u,ArrayAt(inspection.DisplayConstraints,"blocks").size());EXPECT_EQ(b.PathId,ID(ArrayAt(ArrayAt(inspection.DisplayConstraints,"blocks")[0].To<Map>(),"members")[0]));
}

TEST(EditableToolpath, OneReplaceAndOneReadOnlyInputRetainTheSelectedIdentityAndInvalidateTogether)
{
    Host host;SOperationType type;type.TypeId="test.replace-with-reference";type.MinimumInputs=2;type.MaximumInputs=2;type.ParameterSchema={{"fields",Map{}}};type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    type.Evaluate=[](const SOperationEvaluation& input,const IToolpathGeometryServices&)
    {
        auto value=input.Inputs[0].Value;const auto& reference=std::get<Segment3>(std::get<CurveSegment3>(input.Inputs[1].Value.Geometry.Segments[0].Curve).Curve);const auto delta=reference.End.X/10.;
        for(auto& segment:value.Geometry.Segments){const auto native=Range(segment);segment.Sources.push_back({input.Inputs[0].Binding.Source,segment.Id,native,native,"Exact",0});auto& curve=std::get<Segment3>(std::get<CurveSegment3>(segment.Curve).Curve);curve.Start.X+=delta;curve.End.X+=delta;}
        return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};
    };host.Registry->Register(std::move(type));host.Source(Line());const auto referenceSource=host.Source(Line(20));host.Generate();const auto originals=host.Paths();SResolvedPath selected,reference;for(const auto& p:originals)if(p.Port.OperationId==ID(At(referenceSource,"operationId")))reference=p;else selected=p;
    auto add=host.Command("AddOperation");add["typeId"]=std::string("test.replace-with-reference");add["parameters"]=Map{};add["inputs"]=Array{ToData(SOperationInput{"selected",selected.Port,true,{}}),ToData(SOperationInput{"nominal",reference.Port,false,{}})};add["expectedInputs"]=host.Expected({selected.Port,reference.Port});const auto operation=host.Document->Execute(add);host.Generate();Complete(host,2);
    const auto output=DeclaredToolpathPort(ID(At(operation,"operationId")),"main");auto current=host.Document->ReadPath(host.Plan,output);EXPECT_EQ(selected.PathId,current.PathId);EXPECT_DOUBLE_EQ(2.,std::get<Segment3>(std::get<CurveSegment3>(current.Value.Geometry.Segments[0].Curve).Curve).Start.X);
    host.Update(ID(At(referenceSource,"operationId")),Line(30));EXPECT_THROW(host.Document->ReadPath(host.Plan,output),std::invalid_argument);host.Generate();current=host.Document->ReadPath(host.Plan,output);EXPECT_EQ(selected.PathId,current.PathId);EXPECT_DOUBLE_EQ(3.,std::get<Segment3>(std::get<CurveSegment3>(current.Value.Geometry.Segments[0].Curve).Curve).Start.X);
    auto lock=host.Command("SetPathLocked");lock["pathIds"]=Array{current.PathId};lock["locked"]=true;host.Document->Execute(lock);EXPECT_THROW(host.Update(ID(At(referenceSource,"operationId")),Line(40)),std::invalid_argument);
}

TEST(EditableToolpath, ClosedComplementaryArcsHaveDistinctStablePhysicalIdentities)
{
    for(const bool planar:{false,true})for(const auto* type:{"icax.edit.split","icax.edit.segment"})
    {
        Host host;const auto source=host.Source(Circle(planar));host.Generate();const auto a=host.Paths()[0];
        Map parameters{{"cuts",Array{Map{{"id",std::string("A")},{"anchor",ToData(Anchor(a,.5))}},Map{{"id",std::string("B")},{"anchor",ToData(Anchor(a,2.5))}}}}};if(std::string(type)=="icax.edit.split")parameters["retainFirstIdentity"]=true;
        host.Add(type,{a.Port},parameters);host.Generate();Complete(host,2);IntentId shortArc,longArc;
        for(const auto& path:host.Paths()){if(path.Value.Geometry.Segments.size()==1)shortArc=path.PathId;else longArc=path.PathId;}
        ASSERT_FALSE(shortArc.is_nil());ASSERT_FALSE(longArc.is_nil());EXPECT_NE(shortArc,longArc);if(std::string(type)=="icax.edit.split")EXPECT_EQ(a.PathId,longArc);
        const auto verify=[&](){Complete(host,2);for(const auto& path:host.Paths())if(path.Value.Geometry.Segments.size()==1){EXPECT_EQ(shortArc,path.PathId);EXPECT_NEAR(.5,Range(path.Value.Geometry.Segments[0]).First,1e-12);EXPECT_NEAR(2.5,Range(path.Value.Geometry.Segments[0]).Last,1e-12);}else EXPECT_EQ(longArc,path.PathId);};
        auto reversed=Circle(planar);auto range=Range(reversed.Geometry.Segments[0]);range.Reversed=true;SetRange(reversed.Geometry.Segments[0],range);host.Update(ID(At(source,"operationId")),reversed);host.Generate();verify();
        ASSERT_TRUE(host.Db->Undo());host.Generate();verify();ASSERT_TRUE(host.Db->Undo());host.Generate();Complete(host,1);ASSERT_TRUE(host.Db->Redo());host.Generate();verify();
        auto db=Repository();const auto document=CEditableToolpathDocument::ImportDocument(db,host.Document->ExportDocument());CEditableToolpathDocument reopened(db,document,host.Registry);const auto reopenedPaths=reopened.Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,reopenedPaths.Status);ASSERT_EQ(2u,reopenedPaths.Paths.size());for(const auto& path:reopenedPaths.Paths)EXPECT_TRUE(path.PathId==shortArc||path.PathId==longArc);
    }
}

TEST(EditableToolpath, SdkIntegerCoordinatesAnchorsAndOccurrencesUsePublicOperationsWithoutNarrowing)
{
    Host host;host.Source(Circle());host.Generate();const auto a=host.Paths()[0];auto first=ToData(Anchor(a,1)),last=ToData(Anchor(a,3));first["parameter"]=1;last["parameter"]=3;first["occurrence"]=0;last["occurrence"]=0;
    const auto range=CaptureToolpathRange(a,AnchorFromData(first),AnchorFromData(last));auto data=ToData(range);auto spans=ArrayAt(data,"spans");auto span=spans[0].To<Map>();span["occurrence"]=0;auto native=MapAt(span,"range");native["first"]=1;native["last"]=3;span["range"]=native;spans[0]=span;data["spans"]=spans;
    const auto decoded=RangeFromData(data);ASSERT_EQ(1u,decoded.ExactSpans.size());EXPECT_EQ(0u,decoded.ExactSpans[0].Occurrence);EXPECT_DOUBLE_EQ(1.,decoded.ExactSpans[0].Range.First);
    host.Add("icax.edit.split",{a.Port},{{"cuts",Array{Map{{"id",std::string("A")},{"anchor",first}},Map{{"id",std::string("B")},{"anchor",last}}}}});host.Generate();Complete(host,2);
    Host translation;translation.Source(Line());translation.Generate();translation.Add("icax.edit.translate",{translation.Paths()[0].Port},{{"delta",Array{4,0,0}}});translation.Generate();EXPECT_DOUBLE_EQ(4.,std::get<Segment3>(std::get<CurveSegment3>(translation.Paths()[0].Value.Geometry.Segments[0].Curve).Curve).Start.X);
    translation.Add("icax.lead.linear",{translation.Paths()[0].Port},{{"atStart",true},{"offset",Array{0,-1,0}}},{{"scrapHalfSpace",Map{{"point",Array{0,100,0}},{"normal",Array{0,-1,0}}}}});translation.Generate();Complete(translation,1);
    auto huge=first;huge["occurrence"]=std::numeric_limits<unsigned long long>::max();EXPECT_EQ(std::numeric_limits<unsigned long long>::max(),AnchorFromData(huge).Occurrence);
    for(const Variant invalid:{Variant(true),Variant(-1),Variant(.5),Variant(9007199254740992.)}){auto bad=first;bad["occurrence"]=invalid;EXPECT_THROW(AnchorFromData(bad),std::invalid_argument);}
}

TEST(EditableToolpath, NumericSchemaAcceptsSdkIntegersAndRejectsBooleansAndUnsafeIntegerMeasurements)
{
    Host host;SOperationType type;type.TypeId="test.numeric-value";type.WriteChannels={"distance"};type.ParameterSchema={{"fields",Map{{"distance",Map{{"type",std::string("number")},{"required",true},{"minimum",0},{"maximum",10u}}}}}};type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    type.Evaluate=[](const SOperationEvaluation& input,const IToolpathGeometryServices&){auto value=input.Inputs[0].Value;for(auto& segment:value.Geometry.Segments){const auto range=Range(segment);segment.Sources.push_back({input.Inputs[0].Binding.Source,segment.Id,range,range,"Exact",0});}value.Channels["distance"]=Number(input.Parameters,"distance");return SOperationCandidate{{SOutputCandidate{"main",{},std::size_t(0),std::move(value)}}};};
    host.Registry->Register(std::move(type));host.Source(Line());host.Generate();const auto a=host.Paths()[0];EXPECT_THROW(host.Add("test.numeric-value",{a.Port},{{"distance",true}}),std::invalid_argument);
    EXPECT_THROW(host.Add("test.numeric-value",{a.Port},{{"distance",9007199254740992ULL}}),std::invalid_argument);
    host.Add("test.numeric-value",{a.Port},{{"distance",4}});host.Generate();EXPECT_DOUBLE_EQ(4.,host.Paths()[0].Value.Channels.at("distance").To<double>());
}

TEST(EditableToolpath, OnePublicationValidatesAnUnchangedAncestorOnceInsteadOfTheWholeGraphPerNode)
{
    class CountingGeometry final:public IToolpathGeometryServices
    {
        std::shared_ptr<const IToolpathGeometryServices> Base=CreateToolpathGeometryServices();
    public:
        mutable std::atomic<int> Validations{0};
        Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter)const override{return Base->Evaluate(geometry,segment,parameter);}
        void Validate(const SEditableGeometry& geometry)const override{++Validations;Base->Validate(geometry);}
        void ValidateManufacturing(const std::string& type,const SOperationEvaluation& input,const SOperationCandidate& candidate)const override{Base->ValidateManufacturing(type,input,candidate);}
    };
    Host host;auto geometry=std::make_shared<CountingGeometry>();host.Document=std::make_unique<CEditableToolpathDocument>(host.Db,host.Document->Id(),host.Registry,geometry);host.Source(Line());host.Generate();
    for(int i=0;i<6;++i){host.Add("icax.edit.reverse",{host.Paths()[0].Port},{});host.Generate();}
    auto job=host.Document->GenerateAsync(host.Plan);while(!job->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));geometry->Validations=0;ASSERT_TRUE(host.Document->Publish(job));
    EXPECT_LE(geometry->Validations.load(),7) << "The seven-node graph must not be decoded and geometry-validated seven times.";Complete(host,1);
}
TEST(EditableToolpath, OneEditBatchValidatesItsUnchangedSharedAncestorOnlyOnce)
{
    class CountingGeometry final:public IToolpathGeometryServices
    {
        std::shared_ptr<const IToolpathGeometryServices> Base=CreateToolpathGeometryServices();
    public:
        mutable std::atomic<int> Validations{0};
        Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter)const override{return Base->Evaluate(geometry,segment,parameter);}
        void Validate(const SEditableGeometry& geometry)const override{++Validations;Base->Validate(geometry);}
        void ValidateManufacturing(const std::string& type,const SOperationEvaluation& input,const SOperationCandidate& candidate)const override{Base->ValidateManufacturing(type,input,candidate);}
    };
    Host host;auto geometry=std::make_shared<CountingGeometry>();host.Document=std::make_unique<CEditableToolpathDocument>(host.Db,host.Document->Id(),host.Registry,geometry);
    host.Source(Line());host.Generate();const auto source=host.Paths().at(0);std::vector<Map> commands;
    for(int i=0;i<6;++i){auto command=host.Command("AddOperation");command["typeId"]=std::string("icax.edit.copy");command["parameters"]=Map{};
        command["inputs"]=Array{ToData(SOperationInput{"input/0",source.Port,false,{}})};command["outputRoot"]=true;command["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};commands.push_back(std::move(command));}
    geometry->Validations=0;EXPECT_EQ(6u,host.Document->ExecuteBatch(commands).size());EXPECT_EQ(1,geometry->Validations.load());
    host.Generate();Complete(host,7);
}
