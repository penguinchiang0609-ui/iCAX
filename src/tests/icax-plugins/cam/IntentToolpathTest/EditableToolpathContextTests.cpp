#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathPreview.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include "Database/OperationLog.h"
#include <chrono>
#include <thread>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Data;
using namespace iCAX::Database;
using namespace iCAX::GeometryData;
std::shared_ptr<IRepository> ContextRepository()
{auto registry=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*registry);return GenerateRepository(GenerateNewUUID(),registry);}
struct CFrozenTestContext final:IToolpathOperationContext
{
    const int Version,Value;
    CFrozenTestContext(int version,int value):Version(version),Value(value){}
    std::string Fingerprint()const override{return std::to_string(Version)+"/"+std::to_string(Value);}
};
struct SContextState final
{
    const std::thread::id Owner=std::this_thread::get_id();int Version=1,Value=7;bool Fail=false;
    std::atomic_int FreezeCount=0,EvaluationCount=0,ObservedValue=0;
    std::atomic_bool WrongFreezeThread=false,WorkerEvaluated=false,Hold=false,Started=false;
};
SOperationType ContextType(const std::string& id,const std::shared_ptr<SContextState>& state)
{
    SOperationType type;type.TypeId=id;type.ReadChannels={"geometry","topology","traversal","provenance","frame"};type.WriteChannels={"process"};type.DeclaredOutputRoles={"main"};
    type.FreezeContext=[state](const IntentMap& persistent)->std::shared_ptr<const IToolpathOperationContext>
    {
        ++state->FreezeCount;if(std::this_thread::get_id()!=state->Owner){state->WrongFreezeThread=true;throw std::runtime_error("Context resolved outside the owner thread");}
        if(Text(persistent,"assetKey")!="asset")throw std::runtime_error("Persistent asset key missing");
        if(state->Fail)throw std::runtime_error("TEST_CONTEXT_ASSET_MISSING");
        return std::make_shared<const CFrozenTestContext>(state->Version,state->Value);
    };
    type.Evaluate=[state](const SOperationEvaluation& in,const IToolpathGeometryServices&)
    {
        const auto context=std::dynamic_pointer_cast<const CFrozenTestContext>(in.RuntimeContext);
        if(!context)throw std::runtime_error("Frozen context missing from the worker input");
        ++state->EvaluationCount;state->ObservedValue=context->Value;if(std::this_thread::get_id()!=state->Owner)state->WorkerEvaluated=true;state->Started=true;
        while(state->Hold.load())
        {if(in.Cancelled&&in.Cancelled->load())throw std::runtime_error("Cancelled context test");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        auto value=in.Inputs.at(0).Value;const auto& source=in.Inputs[0];
        for(auto& segment:value.Geometry.Segments){const auto range=Range(segment);segment.Sources.push_back({source.Binding.Source,segment.Id,range,range,"Exact",0,0,ToolpathFingerprint(source.Value,{"geometry","topology","traversal","frame"})});}
        value.Channels["process"]=Map{{"value",context->Value},{"version",context->Version}};SOperationCandidate result;
        result.Outputs.push_back({"main",{},0,std::move(value)});return result;
    };
    return type;
}
struct ContextFixture
{
    std::shared_ptr<IRepository> Db=ContextRepository();std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();
    std::unique_ptr<CEditableToolpathDocument> Document;IntentId Plan;
    explicit ContextFixture(std::shared_ptr<const IToolpathGeometryServices> geometry={}){RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry,std::move(geometry));Plan=Document->CreatePlan("context");}
    Map Command(const std::string& kind)const{return {{"kind",kind},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
    void Await(const std::shared_ptr<CToolpathGeneration>& task)
    {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!task->IsReady()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());}
    void Generate(){auto task=Document->GenerateAsync(Plan);Await(task);ASSERT_TRUE(Document->Publish(task));}
    SResolvedPath Source()
    {SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"context/source");auto command=Command("CreateSource");command["value"]=ToData(value);const auto receipt=Document->Execute(command);Generate();const auto id=ID(At(receipt,"pathId"));for(const auto& path:Document->Resolve(Plan).Paths)if(path.PathId==id)return path;throw std::runtime_error("Source missing");}
    IntentId Add(const std::string& type,const SResolvedPath& source)
    {auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=Map{};command["context"]=Map{{"assetKey",std::string("asset")}};
     command["inputs"]=Array{ToData(SOperationInput{"input",source.Port,true,{}})};command["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};return ID(At(Document->Execute(command),"operationId"));}
};
int ContextValue(const SResolvedPath& path){return At(MapAt(path.Value.Channels,"process"),"value").To<int>();}
struct CReleaseContextGate
{std::shared_ptr<SContextState> State;~CReleaseContextGate(){State->Hold=false;}};
}

TEST(EditableToolpathContext, ContextFreezesOnOwnerAndWorkersReceiveOnlyTheFrozenValue)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.owner",state));const auto source=host.Source();host.Add("test.context.owner",source);host.Generate();
    const auto result=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1,result.Paths.size());EXPECT_EQ(7,ContextValue(result.Paths[0]));
    EXPECT_GT(state->FreezeCount.load(),0);EXPECT_FALSE(state->WrongFreezeThread.load());EXPECT_TRUE(state->WorkerEvaluated.load());EXPECT_EQ(7,state->ObservedValue.load());
}
TEST(EditableToolpathPending, AuthoredDefinitionEditsDoNotFreezeOrEvaluateTheExistingGraph)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.authored",state));
    const auto source=host.Source();host.Add("test.context.authored",source);host.Generate();
    const auto current=host.Document->Resolve(host.Plan).Paths.at(0);const auto producer=host.Document->ReadOperationDefinition(current.Port.OperationId);
    auto command=host.Command("AddOperation");command["typeId"]=std::string("icax.edit.reverse");command["parameters"]=Map{};
    auto input=ToData(SOperationInput{"input",current.Port,true,{}});input["deferred"]=true;input["producerFingerprint"]=At(producer,"definitionFingerprint");command["inputs"]=Array{input};
    const auto freezes=state->FreezeCount.load(),evaluations=state->EvaluationCount.load();
    auto stale=command;auto bad=input;bad["producerFingerprint"]=std::string("stale");stale["inputs"]=Array{bad};EXPECT_THROW(host.Document->Execute(stale),std::exception);
    const auto receipt=host.Document->Execute(command);const auto operation=ID(At(receipt,"operationId"));
    for(int i=0;i<4;++i){auto update=host.Command("UpdateOperation");update["operationId"]=operation;update["parameters"]=Map{};update["expectedOperationFingerprint"]=At(host.Document->ReadOperationDefinition(operation),"definitionFingerprint");host.Document->Execute(update);}
    EXPECT_EQ(freezes,state->FreezeCount.load());EXPECT_EQ(evaluations,state->EvaluationCount.load());
    auto staleUpdate=host.Command("UpdateOperation");staleUpdate["operationId"]=operation;staleUpdate["parameters"]=Map{};staleUpdate["expectedOperationFingerprint"]=std::string("stale");EXPECT_THROW(host.Document->Execute(staleUpdate),std::exception);
    host.Generate();const auto generated=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,generated.Status);ASSERT_EQ(1u,generated.Paths.size());EXPECT_EQ(current.PathId,generated.Paths[0].PathId);
}
TEST(EditableToolpathContext, PublicationMemoRechecksAncestorContextAfterASynchronousPublicationListenerChangesIt)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.listener",state));const auto source=host.Source();host.Add("test.context.listener",source);host.Generate();
    const auto first=host.Add("icax.edit.reverse",host.Document->Resolve(host.Plan).Paths[0]);host.Generate();const auto second=host.Add("icax.edit.reverse",host.Document->Resolve(host.Plan).Paths[0]);host.Generate();
    const auto previous=MapAt(host.Document->ReadOperation(second),"result");state->Value=8;auto task=host.Document->GenerateAsync(host.Plan);host.Await(task);
    struct Listener final:IRepositoryEventListener
    {
        IntentId Target;std::shared_ptr<SContextState> State;bool Armed=true;
        void OnRepositoryChanging(void*,const RepositoryEventArgs&)override{}
        void OnRepositoryChanged(void*,const RepositoryEventArgs& event)override
        {if(!Armed)return;const auto inspect=[&](IntentId entity){if(entity==Target){State->Value=9;Armed=false;}};if(event.pBatch)for(const auto& record:event.pBatch->Records)inspect(record.EntityID);else inspect(event.EntityID);}
    };
    auto listener=std::make_shared<Listener>();listener->Target=NamedID(Key(host.Document->Id())+"/result/"+Key(first));listener->State=state;host.Db->AddObserver(listener);
    ASSERT_TRUE(host.Document->Publish(task));EXPECT_FALSE(listener->Armed);EXPECT_EQ(previous,MapAt(host.Document->ReadOperation(second),"result"));EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    host.Generate();const auto current=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,current.Status);ASSERT_EQ(1u,current.Paths.size());EXPECT_EQ(9,ContextValue(current.Paths[0]));
}
TEST(EditableToolpathContext, FrozenPreviewRunsOnWorkerWithoutReenteringTheOwnerOrPublishing)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.frozen-preview",state));
    const auto source=host.Source();const auto before=host.Document->ExportDocument();const auto geometry=CreateToolpathGeometryServices();
    SOperationEvaluation input;input.OperationId=GenerateNewUUID();input.Context={{"assetKey",std::string("asset")}};
    input.Inputs={{{"input/0",source.Port,true,{}},source.PathId,source.Value,source.Fingerprint}};
    EXPECT_THROW(EvaluateFrozenToolpathOperation(*host.Registry,*geometry,"test.context.frozen-preview",input),std::exception);
    input.RuntimeContext=std::make_shared<const CFrozenTestContext>(1,7);SOperationCandidate candidate;std::exception_ptr failure;
    std::thread worker([&]{try{candidate=EvaluateFrozenToolpathOperation(*host.Registry,*geometry,"test.context.frozen-preview",input);}catch(...){failure=std::current_exception();}});worker.join();
    if(failure)std::rethrow_exception(failure);ASSERT_EQ(1,candidate.Outputs.size());EXPECT_TRUE(state->WorkerEvaluated.load());EXPECT_EQ(0,state->FreezeCount.load());
    const auto expected=host.Document->Preview("test.context.frozen-preview",{}, {source.Port},{{"assetKey",std::string("asset")}});
    EXPECT_EQ(ToData(expected.Outputs.front().Value),ToData(candidate.Outputs.front().Value));EXPECT_EQ(before,host.Document->ExportDocument());
    input.Inputs.front().Fingerprint="forged";EXPECT_THROW(EvaluateFrozenToolpathOperation(*host.Registry,*geometry,"test.context.frozen-preview",input),std::exception);
}
TEST(EditableToolpathContext, FrozenGraphComposesTheSameExactSplitAndAttachmentsAsThePersistentGraph)
{
    ContextFixture host;auto source=host.Source();auto value=source.Value;const auto segment=value.Geometry.Segments.front().Id;
    value.Channels["markers"]=Map{{"attachment",std::string("Points")},{"entries",Array{
        Map{{"id",std::string("A")},{"anchor",ToData(SPathAnchor{source.Port,segment,.25})}},
        Map{{"id",std::string("B")},{"anchor",ToData(SPathAnchor{source.Port,segment,.75})}}}}};
    auto update=host.Command("UpdateSource");update["operationId"]=source.Port.OperationId;update["expectedOperationFingerprint"]=host.Document->ReadOperation(source.Port.OperationId).at("definitionFingerprint");update["value"]=ToData(value);host.Document->Execute(update);host.Generate();source=host.Document->Resolve(host.Plan).Paths.front();
    const auto reverse=GenerateNewUUID(),split=GenerateNewUUID();const auto reversedPort=DeclaredToolpathPort(reverse,"main");
    const Map cuts{{"cuts",Array{Map{{"id",std::string("middle")},{"anchor",ToData(SPathAnchor{reversedPort,segment,.5})}}}}};
    SFrozenToolpathPreviewNode a;a.TypeId="icax.edit.reverse";a.Evaluation.OperationId=reverse;a.Evaluation.Inputs={{{"input/0",source.Port,true,{}},{},{},{}}};
    SFrozenToolpathPreviewNode b;b.TypeId="icax.edit.split";b.Evaluation.OperationId=split;b.Evaluation.Parameters=cuts;b.Evaluation.Inputs={{{"input/0",reversedPort,true,{}},{},{},{}}};
    const auto before=host.Document->ExportDocument();const auto geometry=CreateToolpathGeometryServices();
    const auto preview=EvaluateFrozenToolpathGraph(*host.Registry,*geometry,{{source.Port,{{"source",source.Port,false,{}},source.PathId,source.Value,source.Fingerprint}}},{a,b});
    EXPECT_EQ(before,host.Document->ExportDocument());
    auto first=host.Command("AddOperation");first["operationId"]=reverse;first["typeId"]=a.TypeId;first["parameters"]=Map{};first["inputs"]=Array{ToData(SOperationInput{"input/0",source.Port,true,{}})};first["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};host.Document->Execute(first);host.Generate();
    const auto actualInput=host.Document->Resolve(host.Plan).Paths.front();auto second=host.Command("AddOperation");second["operationId"]=split;second["typeId"]=b.TypeId;second["parameters"]=cuts;second["inputs"]=Array{ToData(SOperationInput{"input/0",actualInput.Port,true,{}})};second["expectedInputs"]=Map{{Key(actualInput.Port),actualInput.Fingerprint}};host.Document->Execute(second);host.Generate();
    const auto result=host.Document->Resolve(host.Plan);ASSERT_EQ(2,result.Paths.size());std::set<std::string> markers;
    for(const auto& path:result.Paths)
    {
        ASSERT_TRUE(preview.contains(path.Port));EXPECT_EQ(ToData(path.Value),ToData(preview.at(path.Port).Value));
        const auto entries=path.Value.Channels.at("markers").To<Map>().at("entries").To<Array>();ASSERT_EQ(1,entries.size());
        const auto entry=entries.front().To<Map>();markers.insert(entry.at("id").To<std::string>());EXPECT_EQ(path.Port,AnchorFromData(entry.at("anchor").To<Map>()).Port);
    }
    EXPECT_EQ((std::set<std::string>{"A","B"}),markers);
}
TEST(EditableToolpathContext, ChangingExternalContextInvalidatesCacheWithoutChangingPersistentParameters)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.version",state));const auto source=host.Source();const auto operation=host.Add("test.context.version",source);host.Generate();
    const auto definition=host.Document->ReadOperation(operation);const auto evaluations=state->EvaluationCount.load();host.Generate();EXPECT_EQ(evaluations,state->EvaluationCount.load());
    ++state->Version;state->Value=19;EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);host.Generate();
    const auto result=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1,result.Paths.size());EXPECT_EQ(19,ContextValue(result.Paths[0]));EXPECT_EQ(evaluations+1,state->EvaluationCount.load());
    EXPECT_EQ(At(definition,"definitionFingerprint"),At(host.Document->ReadOperation(operation),"definitionFingerprint"));EXPECT_FALSE(state->WrongFreezeThread.load());
}
TEST(EditableToolpathContext, RepeatedReadsReuseGeometryChecksAndStillObserveContextFailuresAndVersions)
{
    struct CountingGeometry final:IToolpathGeometryServices
    {
        std::shared_ptr<const IToolpathGeometryServices> Base=CreateToolpathGeometryServices();mutable std::atomic_int Checks=0;
        Point3 Evaluate(const SEditableGeometry& g,const SEditableSegment& s,double u)const override{return Base->Evaluate(g,s,u);}
        void Validate(const SEditableGeometry& g)const override{++Checks;Base->Validate(g);}
        void ValidateManufacturing(const std::string& type,const SOperationEvaluation& in,const SOperationCandidate& out)const override{Base->ValidateManufacturing(type,in,out);}
    };
    auto geometry=std::make_shared<CountingGeometry>();ContextFixture host(geometry);const auto state=std::make_shared<SContextState>();
    host.Registry->Register(ContextType("test.context.read-memo",state));const auto source=host.Source();host.Add("test.context.read-memo",source);host.Generate();
    const auto initial=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,initial.Status);const auto path=initial.Paths.front();
    const auto checks=geometry->Checks.load(),freezes=state->FreezeCount.load();
    EXPECT_EQ(EOutputCompleteness::Complete,host.Document->InspectPlan(host.Plan).Status);
    EXPECT_EQ(path.Fingerprint,host.Document->ReadPath(host.Plan,path.Port).Fingerprint);
    EXPECT_EQ(EOutputCompleteness::Complete,host.Document->Resolve(host.Plan).Status);
    EXPECT_EQ(checks,geometry->Checks.load());EXPECT_GE(state->FreezeCount.load(),freezes+3);
    auto rename=host.Command("SetLabels");rename["pathId"]=path.PathId;rename["name"]=std::string("renamed");rename["userNumber"]=std::string("18");rename["expectedLabelsVersion"]=static_cast<unsigned long long>(path.LabelsVersion);
    host.Document->Execute(rename);const auto editChecks=geometry->Checks.load();const auto renamed=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,renamed.Status);
    EXPECT_EQ("renamed",renamed.Paths.front().Name);EXPECT_EQ(editChecks,geometry->Checks.load());
    state->Fail=true;EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    EXPECT_THROW(host.Document->ReadPath(host.Plan,path.Port),std::invalid_argument);
    state->Fail=false;EXPECT_EQ(EOutputCompleteness::Complete,host.Document->Resolve(host.Plan).Status);
    ++state->Version;EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    host.Generate();EXPECT_EQ(EOutputCompleteness::Complete,host.Document->Resolve(host.Plan).Status);EXPECT_GT(geometry->Checks.load(),checks);
    host.Registry->Unregister("test.context.read-memo");EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
}
TEST(EditableToolpathContext, ReadMemoRejectsRepositoryMutationDuringContextObservation)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto type=ContextType("test.context.read-mutation",state);
    const auto freeze=type.FreezeContext;bool armed=false;const auto external=GenerateNewUUID();
    type.FreezeContext=[&](const Map& parameters){if(armed){armed=false;host.Db->CreateEntity(external);(void)host.Document->Resolve(host.Plan);}return freeze(parameters);};
    host.Registry->Register(std::move(type));const auto source=host.Source();host.Add("test.context.read-mutation",source);host.Generate();
    ASSERT_EQ(EOutputCompleteness::Complete,host.Document->Resolve(host.Plan).Status);
    armed=true;EXPECT_EQ(EOutputCompleteness::Invalid,host.Document->Resolve(host.Plan).Status);EXPECT_FALSE(armed);
    EXPECT_EQ(EOutputCompleteness::Complete,host.Document->Resolve(host.Plan).Status);
}
TEST(EditableToolpathContext, NestedReadMemoCannotHideAFreshContextChange)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto type=ContextType("test.context.nested-read",state);
    const auto freeze=type.FreezeContext;bool armed=false,nestedRead=false;SPortRef port;
    type.FreezeContext=[&](const Map& parameters)
    {
        if(armed){armed=false;const auto nested=host.Document->ReadPath(host.Plan,port);nestedRead=ContextValue(nested)==7;state->Value=9;}
        return freeze(parameters);
    };
    host.Registry->Register(std::move(type));const auto source=host.Source();host.Add("test.context.nested-read",source);host.Generate();
    const auto current=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,current.Status);ASSERT_EQ(1u,current.Paths.size());port=current.Paths.front().Port;
    EXPECT_EQ(7,ContextValue(host.Document->ReadPath(host.Plan,port)));armed=true;
    EXPECT_THROW(host.Document->ReadPath(host.Plan,port),std::invalid_argument);EXPECT_TRUE(nestedRead);EXPECT_FALSE(armed);
    EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);host.Generate();
    EXPECT_EQ(9,ContextValue(host.Document->ReadPath(host.Plan,port)));
}
TEST(EditableToolpathContext, CachedPathValuesStayIndependentOfCallerEditsAndSourceInvalidation)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.read-value",state));
    const auto source=host.Source();host.Add("test.context.read-value",source);host.Generate();
    const auto initial=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,initial.Status);ASSERT_EQ(1u,initial.Paths.size());const auto path=initial.Paths.front();
    const auto expected=ToData(path.Value);auto returned=host.Document->ReadPath(host.Plan,path.Port);
    std::get<Segment3>(std::get<CurveSegment3>(returned.Value.Geometry.Segments.front().Curve).Curve).End.X=999;
    returned.Value.Channels["process"]=Map{{"value",999},{"version",999}};returned.Value.Geometry.Segments.front().Sources.clear();
    EXPECT_EQ(expected,ToData(host.Document->ReadPath(host.Plan,path.Port).Value));
    const auto batch=host.Document->ReadPaths(host.Plan,{path.Port,source.Port,path.Port});ASSERT_EQ(3u,batch.size());
    EXPECT_EQ(expected,ToData(batch[0].Value));EXPECT_EQ(expected,ToData(batch[2].Value));EXPECT_EQ(ToData(source.Value),ToData(batch[1].Value));
    auto changed=source.Value;std::get<Segment3>(std::get<CurveSegment3>(changed.Geometry.Segments.front().Curve).Curve).End.X=20;
    auto update=host.Command("UpdateSource");update["operationId"]=source.Port.OperationId;update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(source.Port.OperationId),"definitionFingerprint");update["value"]=ToData(changed);host.Document->Execute(update);
    EXPECT_THROW(host.Document->ReadPath(host.Plan,path.Port),std::invalid_argument);host.Generate();
    const auto regenerated=host.Document->ReadPath(host.Plan,path.Port);EXPECT_DOUBLE_EQ(20,std::get<Segment3>(std::get<CurveSegment3>(regenerated.Value.Geometry.Segments.front().Curve).Curve).End.X);
    EXPECT_EQ(expected,ToData(path.Value));EXPECT_EQ(7,ContextValue(regenerated));
}
TEST(EditableToolpathContext, UnchangedGenerationRetainsTheRecordAndStillRunsResultValidation)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto type=ContextType("test.context.unchanged-record",state);
    const auto checks=std::make_shared<std::atomic_int>(0);type.ValidateResult=[checks](const SOperationEvaluation&,const SOperationCandidate&){++*checks;};
    host.Registry->Register(std::move(type));const auto source=host.Source();const auto operation=host.Add("test.context.unchanged-record",source);host.Generate();
    const auto before=MapAt(host.Document->ReadOperation(operation),"result");const auto evaluated=state->EvaluationCount.load(),validated=checks->load();
    host.Generate();EXPECT_EQ(evaluated,state->EvaluationCount.load());EXPECT_GT(checks->load(),validated);
    EXPECT_EQ(before,MapAt(host.Document->ReadOperation(operation),"result"));
}
TEST(EditableToolpathContext, ANewerPublishedTicketCannotBeOverwrittenByAnOlderReadyResult)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto type=ContextType("test.context.ticket",state);const auto evaluate=type.Evaluate;
    type.Evaluate=[evaluate,state](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
    {
        auto candidate=evaluate(input,geometry);auto process=MapAt(candidate.Outputs.front().Value.Channels,"process");process["value"]=state->EvaluationCount.load();candidate.Outputs.front().Value.Channels["process"]=std::move(process);return candidate;
    };
    host.Registry->Register(std::move(type));const auto source=host.Source();const auto operation=host.Add("test.context.ticket",source);host.Generate();
    auto older=host.Document->GenerateAsync(host.Plan,1,{operation});host.Await(older);const auto oldValue=state->EvaluationCount.load();
    auto newer=host.Document->GenerateAsync(host.Plan,1,{operation});host.Await(newer);const auto newValue=state->EvaluationCount.load();ASSERT_GT(newValue,oldValue);
    ASSERT_TRUE(host.Document->Publish(newer));const auto published=MapAt(host.Document->ReadOperation(operation),"result");
    const auto current=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,current.Status);ASSERT_EQ(1u,current.Paths.size());EXPECT_EQ(newValue,ContextValue(current.Paths.front()));
    ASSERT_TRUE(host.Document->Publish(older));EXPECT_EQ(published,MapAt(host.Document->ReadOperation(operation),"result"));
    const auto after=host.Document->ReadPath(host.Plan,current.Paths.front().Port);EXPECT_EQ(newValue,ContextValue(after));EXPECT_EQ(current.Paths.front().Fingerprint,after.Fingerprint);
}
TEST(EditableToolpathContext, PublicationRechecksInputsAfterAListenerCommitsASourceEdit)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto type=ContextType("test.context.listener-input",state);
    bool queued=false;int sourceEdits=0;Map update;std::string sourceEditError;const auto freeze=type.FreezeContext;
    type.FreezeContext=[&](const Map& parameters)
    {
        // Derived notifications are read-only. Consume the listener's queued
        // owner edit at the next context observation, after notification ends.
        if(queued){queued=false;try{host.Document->Execute(update);++sourceEdits;}catch(const std::exception& error){sourceEditError=error.what();throw;}}
        return freeze(parameters);
    };
    host.Registry->Register(std::move(type));
    const auto source=host.Source();host.Add("test.context.listener-input",source);host.Generate();
    const auto first=host.Add("icax.edit.reverse",host.Document->Resolve(host.Plan).Paths.front());host.Generate();
    const auto second=host.Add("icax.edit.reverse",host.Document->Resolve(host.Plan).Paths.front());host.Generate();const auto previous=MapAt(host.Document->ReadOperation(second),"result");
    auto changed=source.Value;std::get<Segment3>(std::get<CurveSegment3>(changed.Geometry.Segments.front().Curve).Curve).End.X=20;
    update=host.Command("UpdateSource");update["operationId"]=source.Port.OperationId;update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(source.Port.OperationId),"definitionFingerprint");update["value"]=ToData(changed);
    state->Value=8;auto task=host.Document->GenerateAsync(host.Plan);host.Await(task);
    struct Listener final:IRepositoryEventListener
    {
        IntentId Target;bool* Queued=nullptr;bool Armed=true;
        void OnRepositoryChanging(void*,const RepositoryEventArgs&)override{}
        void OnRepositoryChanged(void*,const RepositoryEventArgs& event)override
        {
            if(!Armed)return;const auto inspect=[&](IntentId entity){if(entity==Target){Armed=false;*Queued=true;}};
            if(event.pBatch)for(const auto& record:event.pBatch->Records)inspect(record.EntityID);else inspect(event.EntityID);
        }
    };
    auto listener=std::make_shared<Listener>();listener->Target=NamedID(Key(host.Document->Id())+"/result/"+Key(first));listener->Queued=&queued;host.Db->AddObserver(listener);
    ASSERT_TRUE(host.Document->Publish(task));ASSERT_EQ(1,sourceEdits)<<"Queued source edit: "<<sourceEditError;EXPECT_FALSE(listener->Armed);EXPECT_FALSE(queued);
    EXPECT_EQ(previous,MapAt(host.Document->ReadOperation(second),"result"));EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    host.Generate();const auto current=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,current.Status);ASSERT_EQ(1u,current.Paths.size());
    EXPECT_DOUBLE_EQ(20,std::get<Segment3>(std::get<CurveSegment3>(current.Paths.front().Value.Geometry.Segments.front().Curve).Curve).End.X);EXPECT_EQ(8,ContextValue(current.Paths.front()));
}
TEST(EditableToolpathContext, LateResultCannotPublishAfterItsFrozenAssetVersionChanges)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.late",state));const auto source=host.Source();const auto operation=host.Add("test.context.late",source);host.Generate();
    state->Hold=true;state->Started=false;CReleaseContextGate release{state};auto task=host.Document->GenerateAsync(host.Plan,4,{operation});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!state->Started&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(state->Started.load());
    ++state->Version;state->Value=23;EXPECT_EQ(7,state->ObservedValue.load());state->Hold=false;host.Await(task);ASSERT_TRUE(host.Document->Publish(task));
    EXPECT_EQ(7,state->ObservedValue.load());EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    host.Generate();const auto result=host.Document->Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1,result.Paths.size());EXPECT_EQ(23,ContextValue(result.Paths[0]));EXPECT_FALSE(state->WrongFreezeThread.load());
}
TEST(EditableToolpathContext, EvaluatorRevisionInvalidatesCachesAndLateResultsWithoutChangingEditableVersions)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();const std::string typeId="test.context.evaluator";
    host.Registry->Register(ContextType(typeId,state));const auto source=host.Source();const auto operation=host.Add(typeId,source);host.Generate();
    const auto before=host.Document->ReadOperation(operation);const auto original=host.Document->Resolve(host.Plan).Paths.at(0);
    EXPECT_EQ("1",Text(MapAt(before,"result"),"evaluationRevision"));
    auto late=host.Document->GenerateAsync(host.Plan,2,{operation});host.Await(late);
    auto next=ContextType(typeId,state);next.EvaluationRevision="2";const auto evaluate=next.Evaluate;
    next.Evaluate=[evaluate](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
    {auto result=evaluate(input,geometry);auto process=MapAt(result.Outputs.at(0).Value.Channels,"process");process["value"]=At(process,"value").To<int>()+1000;result.Outputs[0].Value.Channels["process"]=std::move(process);return result;};
    host.Registry->Unregister(typeId);host.Registry->Register(std::move(next));
    EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
    EXPECT_EQ(At(before,"definitionFingerprint"),At(host.Document->ReadOperation(operation),"definitionFingerprint"));
    ASSERT_TRUE(host.Document->Publish(late));EXPECT_EQ(MapAt(before,"result"),MapAt(host.Document->ReadOperation(operation),"result"));
    auto update=host.Command("UpdateOperation");update["operationId"]=operation;update["parameters"]=Map{};update["expectedOperationFingerprint"]=At(before,"definitionFingerprint");
    EXPECT_NO_THROW(host.Document->Execute(update));host.Generate();const auto current=host.Document->Resolve(host.Plan);
    ASSERT_EQ(EOutputCompleteness::Complete,current.Status);ASSERT_EQ(1u,current.Paths.size());EXPECT_EQ(1007,ContextValue(current.Paths[0]));EXPECT_EQ(original.PathId,current.Paths[0].PathId);
    const auto edited=host.Document->ReadOperation(operation);EXPECT_EQ("1",Text(edited,"algorithmVersion"));EXPECT_EQ("2",Text(MapAt(edited,"result"),"evaluationRevision"));
}
namespace
{
Map ObserveContextCommand(const ContextFixture& host,const SResolvedPath& path,const std::string& type="icax.edit.copy")
{auto command=host.Command("AddOperation");command["typeId"]=type;command["parameters"]=Map{};command["inputs"]=Array{ToData(SOperationInput{"input/0",path.Port,false,{}})};command["outputRoot"]=true;command["expectedInputs"]=Map{{Key(path.Port),path.Fingerprint}};return command;}
}
TEST(EditableToolpathContext, EditBatchMemoRechecksExternalContextBeforeTheNextCommand)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.batch",state));const auto source=host.Source();host.Add("test.context.batch",source);host.Generate();const auto path=host.Document->Resolve(host.Plan).Paths.at(0);
    auto sideEffect=*host.Registry->Find("icax.edit.copy");sideEffect.TypeId="test.context.change-before-validation";sideEffect.ValidateParameters=[state](const Map&){state->Value=9;};host.Registry->Register(std::move(sideEffect));
    const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Document->ExecuteBatch({ObserveContextCommand(host,path),ObserveContextCommand(host,path,"test.context.change-before-validation")}),std::invalid_argument);
    EXPECT_EQ(before,host.Document->ExportDocument());EXPECT_EQ(9,state->Value);EXPECT_EQ(EOutputCompleteness::Incomplete,host.Document->Resolve(host.Plan).Status);
}
TEST(EditableToolpathContext, EditBatchMemoInvalidatesDownstreamAfterAStagedSourceChange)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.staged",state));const auto source=host.Source();host.Add("test.context.staged",source);host.Generate();const auto path=host.Document->Resolve(host.Plan).Paths.at(0);
    auto changed=source.Value;std::get<Segment3>(std::get<CurveSegment3>(changed.Geometry.Segments.at(0).Curve).Curve).End.X=20;
    auto update=host.Command("UpdateSource");update["operationId"]=source.Port.OperationId;update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(source.Port.OperationId),"definitionFingerprint");update["value"]=ToData(changed);
    const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Document->ExecuteBatch({ObserveContextCommand(host,path),update,ObserveContextCommand(host,path)}),std::invalid_argument);EXPECT_EQ(before,host.Document->ExportDocument());
}
TEST(EditableToolpathContext, EditBatchRejectsRepositoryMutationFromASynchronousValidationCallback)
{
    ContextFixture host;const auto source=host.Source();const auto external=GenerateNewUUID();bool armed=true;
    auto sideEffect=*host.Registry->Find("icax.edit.copy");sideEffect.TypeId="test.context.mutate-repository";sideEffect.ValidateParameters=[&](const Map&){if(armed){armed=false;host.Db->CreateEntity(external);}};host.Registry->Register(std::move(sideEffect));
    const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Document->ExecuteBatch({ObserveContextCommand(host,source),ObserveContextCommand(host,source,"test.context.mutate-repository")}),std::invalid_argument);
    EXPECT_TRUE(host.Db->HasEntity(external));EXPECT_EQ(before,host.Document->ExportDocument());EXPECT_FALSE(host.Db->IsWriteInProgress());
}
TEST(EditableToolpathContext, BlockedDependantsPreserveTheOriginalActionableFailure)
{
    ContextFixture host;const auto source=host.Source();auto failure=*host.Registry->Find("icax.edit.reverse");failure.TypeId="test.actionable.failure";
    failure.Evaluate=[](const SOperationEvaluation&,const IToolpathGeometryServices&)->SOperationCandidate{throw std::invalid_argument("TEST_EXACT_REBIND_REQUIRED: select a retained native boundary");};host.Registry->Register(std::move(failure));
    auto first=host.Command("AddOperation");first["typeId"]=std::string("test.actionable.failure");first["parameters"]=Map{};first["inputs"]=Array{ToData(SOperationInput{"input/0",source.Port,true,{}})};first["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};
    const auto next=[&](const Map& parent){auto command=host.Command("AddOperation");command["typeId"]=std::string("icax.edit.reverse");command["parameters"]=Map{};command["inputs"]=Array{Map{{"slot",std::string("input/0")},{"source",Map{{"fromCommand",At(parent,"commandId")},{"field",std::string("port")}}},{"replace",true},{"ranges",Array{}}}};return command;};
    const auto second=next(first),third=next(second);const auto receipts=host.Document->ExecuteBatch({first,second,third});host.Generate();
    for(const auto& receipt:receipts){const auto record=MapAt(host.Document->ReadOperation(ID(At(receipt,"operationId"))),"result");EXPECT_EQ("Blocked",Text(record,"state"));const auto diagnostic=Text(record,"diagnostic");EXPECT_NE(std::string::npos,diagnostic.find("TEST_EXACT_REBIND_REQUIRED"));const auto prefix=diagnostic.find("TOOLPATH_INPUT_NOT_CURRENT");if(prefix!=std::string::npos)EXPECT_EQ(std::string::npos,diagnostic.find("TOOLPATH_INPUT_NOT_CURRENT",prefix+1));}
    const auto result=host.Document->Resolve(host.Plan);EXPECT_EQ(EOutputCompleteness::Incomplete,result.Status);ASSERT_FALSE(result.Diagnostics.empty());for(const auto& diagnostic:result.Diagnostics)EXPECT_NE(std::string::npos,diagnostic.Message.find("TEST_EXACT_REBIND_REQUIRED"));
}
TEST(EditableToolpathContext, OneMissingAssetDoesNotBlockAnUnrelatedBranch)
{
    ContextFixture host;const auto bad=std::make_shared<SContextState>(),good=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.bad",bad));host.Registry->Register(ContextType("test.context.good",good));
    const auto a=host.Source(),b=host.Source();const auto failedOperation=host.Add("test.context.bad",a);host.Add("test.context.good",b);host.Generate();bad->Fail=true;++good->Version;good->Value=31;
    EXPECT_NO_THROW(host.Generate());const auto result=host.Document->Resolve(host.Plan);EXPECT_EQ(EOutputCompleteness::Incomplete,result.Status);bool foundGood=false,foundDiagnostic=false;
    for(const auto& path:result.Paths)if(path.PathId==b.PathId){foundGood=true;EXPECT_EQ(31,ContextValue(path));}
    for(const auto& diagnostic:result.Diagnostics)if(diagnostic.OperationId==failedOperation)foundDiagnostic=true;
    EXPECT_TRUE(foundGood);EXPECT_TRUE(foundDiagnostic);EXPECT_FALSE(bad->WrongFreezeThread.load());EXPECT_FALSE(good->WrongFreezeThread.load());
}
TEST(EditableToolpathContext, PreviewAlsoReceivesFreshOwnerFrozenContext)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();host.Registry->Register(ContextType("test.context.preview",state));const auto source=host.Source();state->Value=37;
    const auto preview=host.Document->Preview("test.context.preview",{},{source.Port},{{"assetKey",std::string("asset")}});ASSERT_EQ(1,preview.Outputs.size());EXPECT_EQ(37,At(MapAt(preview.Outputs[0].Value.Channels,"process"),"value").To<int>());EXPECT_FALSE(state->WrongFreezeThread.load());
}
TEST(EditableToolpathContext, FixedRoleDeclarationsDoNotInventVariableTopologyOutputs)
{
    CToolpathOperationRegistry registry;RegisterStandardToolpathOperations(registry);
    for(const auto* type:{"icax.source","icax.edit.reverse","icax.edit.start","icax.edit.translate","icax.edit.channels","icax.edit.promote","icax.edit.replaceGeometry","icax.lead.linear"})
        EXPECT_EQ((std::set<std::string>{"main"}),registry.Find(type)->DeclaredOutputRoles);
    for(const auto* type:{"icax.edit.split","icax.edit.segment","icax.microjoint","icax.edit.removeRanges","icax.edit.copy"})EXPECT_TRUE(registry.Find(type)->DeclaredOutputRoles.empty());
    EXPECT_EQ((std::set<std::string>{"merged"}),registry.Find("icax.edit.merge")->DeclaredOutputRoles);
    EXPECT_EQ((std::set<std::string>{"bridged"}),registry.Find("icax.edit.bridge")->DeclaredOutputRoles);
    auto invalid=ContextType("test.context.invalid-role",std::make_shared<SContextState>());invalid.DeclaredOutputRoles={""};EXPECT_THROW(registry.Register(std::move(invalid)),std::exception);
}

namespace
{
Map DeferredSource(IntentId plan)
{SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"deferred/source");return {{"kind",std::string("CreateSource")},{"commandId",GenerateNewUUID()},{"planId",plan},{"value",ToData(value)}};}
std::set<IntentId> EntityIds(const std::shared_ptr<IRepository>& repository)
{std::set<IntentId> result;for(const auto& entity:repository->FilterEntities([](const auto&){return true;}))result.insert(entity->GetID());return result;}
}
TEST(EditableToolpathDeferred, FailedFirstBatchDoesNotLeaveDocumentPlanSourceOrHostEntities)
{
    for(const auto failInStage:{false,true})
    {
        auto db=ContextRepository();auto registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*registry);const auto id=GenerateNewUUID(),plan=GenerateNewUUID(),fact=GenerateNewUUID();
        const auto before=EntityIds(db);const auto history=db->GetUndoArray();auto document=CEditableToolpathDocument::PrepareNewDocument(db,id,registry);EXPECT_EQ(before,EntityIds(db));
        const Map createPlan{{"kind",std::string("CreatePlan")},{"commandId",GenerateNewUUID()},{"planId",plan},{"name",std::string("deferred")}};auto source=DeferredSource(plan);if(!failInStage)source["kind"]=std::string("InvalidCommand");
        SEditableToolpathHostEdit edit;edit.Stage=[fact](ITransaction& transaction,const auto&){transaction.CreateEntity(fact);throw std::runtime_error("Host staging failed");};
        EXPECT_THROW(document->ExecuteBatch({createPlan,source},edit),std::exception);EXPECT_EQ(before,EntityIds(db));EXPECT_EQ(history,db->GetUndoArray());EXPECT_FALSE(db->IsWriteInProgress());
        EXPECT_FALSE(db->HasEntity(id));EXPECT_FALSE(db->HasEntity(plan));EXPECT_FALSE(db->HasEntity(fact));
    }
}
TEST(EditableToolpathDeferred, FirstSuccessfulBatchUndoesAndRedoesTheWholeNewDocumentWithStableIdentities)
{
    auto db=ContextRepository();auto registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*registry);const auto id=GenerateNewUUID(),plan=GenerateNewUUID(),path=GenerateNewUUID(),fact=GenerateNewUUID();
    auto document=CEditableToolpathDocument::PrepareNewDocument(db,id,registry);const auto history=db->GetUndoArray();const Map createPlan{{"kind",std::string("CreatePlan")},{"commandId",GenerateNewUUID()},{"planId",plan},{"name",std::string("deferred")}};const auto source=DeferredSource(plan);
    SEditableToolpathHostEdit edit;edit.SourceIdentities[ID(At(source,"commandId"))]={ESourcePathIdentityMode::CreateWithId,path};edit.Stage=[fact](ITransaction& transaction,const auto&){transaction.CreateEntity(fact);};
    const auto receipts=document->ExecuteBatch({createPlan,source},edit);ASSERT_EQ(2,receipts.size());const auto operation=ID(At(receipts[1],"operationId"));EXPECT_EQ(path,ID(At(receipts[1],"pathId")));EXPECT_EQ(history.size()+1,db->GetUndoArray().size());
    for(const auto entity:{id,plan,path,fact,operation})EXPECT_TRUE(db->HasEntity(entity));ASSERT_TRUE(db->Undo());for(const auto entity:{id,plan,path,fact,operation})EXPECT_FALSE(db->HasEntity(entity));
    ASSERT_TRUE(db->Redo());for(const auto entity:{id,plan,path,fact,operation})EXPECT_TRUE(db->HasEntity(entity));
    auto task=document->GenerateAsync(plan);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!task->IsReady()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());ASSERT_TRUE(document->Publish(task));
    const auto result=document->Resolve(plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1,result.Paths.size());EXPECT_EQ(path,result.Paths[0].PathId);
}

namespace
{
Map ChainOperation(IntentId plan,const std::string& type,IntentId producerCommand,Map parameters={},Map context={})
{
    const Map source{{"fromCommand",producerCommand},{"field",std::string("port")}};
    return {{"kind",std::string("AddOperation")},{"commandId",GenerateNewUUID()},{"planId",plan},{"typeId",type},
        {"parameters",std::move(parameters)},{"context",std::move(context)},{"inputs",Array{Map{{"slot",std::string("input")},{"source",source},{"replace",true},{"ranges",Array{}}}}}};
}
void PublishReady(CEditableToolpathDocument& document,IntentId plan)
{
    const auto task=document.GenerateAsync(plan);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!task->IsReady()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());ASSERT_TRUE(document.Publish(task));
}
Map& PendingDefinition(Map& saved,IntentId operation)
{
    for(auto& item:std::get<Array>(saved.at("entities").m_Value))
    {auto& entity=std::get<Map>(item.m_Value);if(ID(At(entity,"id"))!=operation)continue;
     for(auto& component:std::get<Array>(entity.at("components").m_Value)){auto& value=std::get<Map>(component.m_Value);if(Text(value,"class")=="CEditableOperationComponent")return std::get<Map>(std::get<Map>(value.at("properties").m_Value).at("Definition").m_Value);}}
    throw std::runtime_error("Saved operation missing");
}
}
TEST(EditableToolpathPending, FixedMultiLevelGraphCommitsWithoutEvaluationAndReopensBeforeGeneration)
{
    auto db=ContextRepository();auto registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*registry);const auto state=std::make_shared<SContextState>();registry->Register(ContextType("test.context.chain",state));
    std::atomic_int standardEvaluations=0;for(const auto* name:{"icax.source","icax.edit.reverse","icax.edit.translate"})
    {auto type=*registry->Find(name);const auto evaluate=type.Evaluate;registry->Unregister(name);type.Evaluate=[&standardEvaluations,evaluate](const auto& in,const auto& geometry){++standardEvaluations;return evaluate(in,geometry);};registry->Register(std::move(type));}
    const auto documentId=GenerateNewUUID(),plan=GenerateNewUUID();auto document=CEditableToolpathDocument::PrepareNewDocument(db,documentId,registry);
    const Map createPlan{{"kind",std::string("CreatePlan")},{"commandId",GenerateNewUUID()},{"planId",plan},{"name",std::string("pending graph")}};
    const auto source=DeferredSource(plan);const auto reverse=ChainOperation(plan,"icax.edit.reverse",ID(At(source,"commandId")));
    const auto context=ChainOperation(plan,"test.context.chain",ID(At(reverse,"commandId")),{},{{"assetKey",std::string("asset")}});
    const auto translate=ChainOperation(plan,"icax.edit.translate",ID(At(context,"commandId")),{{"delta",Array{2.,3.,4.}}});
    const auto receipts=document->ExecuteBatch({createPlan,source,reverse,context,translate});ASSERT_EQ(5,receipts.size());EXPECT_EQ(0,state->EvaluationCount.load());EXPECT_EQ(0,standardEvaluations.load());
    for(std::size_t i=2;i<receipts.size();++i){const auto pending=MapAt(document->ReadOperation(ID(At(receipts[i],"operationId"))),"result");EXPECT_TRUE(pending.empty()||Text(pending,"state")!="Current");EXPECT_TRUE(receipts[i].contains("port"));EXPECT_TRUE(receipts[i].contains("definitionFingerprint"));}
    const auto saved=document->ExportDocument();auto reopenedDb=ContextRepository();const auto imported=CEditableToolpathDocument::ImportDocument(reopenedDb,saved);CEditableToolpathDocument reopened(reopenedDb,imported,registry);
    EXPECT_EQ(0,state->EvaluationCount.load());EXPECT_EQ(0,standardEvaluations.load());PublishReady(reopened,plan);const auto result=reopened.Resolve(plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(1,result.Paths.size());EXPECT_EQ(1,state->EvaluationCount.load());EXPECT_EQ(3,standardEvaluations.load());
    const auto& path=result.Paths[0];EXPECT_EQ(PortFromData(MapAt(receipts.back(),"port")),path.Port);EXPECT_EQ(7,ContextValue(path));const auto geometry=CreateToolpathGeometryServices();const auto& segment=path.Value.Geometry.Segments.front();const auto range=Range(segment);
    const auto first=geometry->Evaluate(path.Value.Geometry,segment,First(range)),last=geometry->Evaluate(path.Value.Geometry,segment,Last(range));EXPECT_DOUBLE_EQ(12,first.X);EXPECT_DOUBLE_EQ(3,first.Y);EXPECT_DOUBLE_EQ(4,first.Z);EXPECT_DOUBLE_EQ(2,last.X);
    auto corrupted=saved;auto& definition=PendingDefinition(corrupted,ID(At(receipts[2],"operationId")));auto declarations=MapAt(definition,"outputDeclarations");declarations["main"]=ToData(SPortRef{ID(At(receipts[2],"operationId")),GenerateNewUUID()});definition["outputDeclarations"]=declarations;
    auto rejected=ContextRepository();const auto before=EntityIds(rejected);EXPECT_THROW(CEditableToolpathDocument::ImportDocument(rejected,corrupted),std::exception);EXPECT_EQ(before,EntityIds(rejected));
}
TEST(EditableToolpathPending, AVariableOutputCannotBeGuessedBeforeItHasBeenEvaluated)
{
    ContextFixture host;const auto state=std::make_shared<SContextState>();auto variable=ContextType("test.context.variable",state);variable.DeclaredOutputRoles.clear();host.Registry->Register(std::move(variable));const auto source=host.Source();const auto before=host.Document->ExportDocument();
    auto producer=host.Command("AddOperation");producer["typeId"]=std::string("test.context.variable");producer["parameters"]=Map{};producer["context"]=Map{{"assetKey",std::string("asset")}};producer["inputs"]=Array{ToData(SOperationInput{"input",source.Port,true,{}})};producer["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};
    auto consumer=host.Command("AddOperation");consumer["typeId"]=std::string("icax.edit.reverse");consumer["parameters"]=Map{};
    const Map guessed{{"operationId",Map{{"fromCommand",ID(At(producer,"commandId"))},{"field",std::string("operationId")}}},{"portId",GenerateNewUUID()}};
    consumer["inputs"]=Array{Map{{"slot",std::string("input")},{"source",guessed},{"replace",true},{"ranges",Array{}}}};
    EXPECT_THROW(host.Document->ExecuteBatch({producer,consumer}),std::exception);EXPECT_EQ(before,host.Document->ExportDocument());EXPECT_EQ(0,state->EvaluationCount.load());
}
TEST(EditableToolpathPending, PendingInputsKeepSchemaAndExactSelectionPreflightRequirements)
{
    for(const int scenario:{0,1,2})
    {
        ContextFixture host;const auto state=std::make_shared<SContextState>();std::atomic_int normalized=0;auto type=ContextType("test.context.pending-check",state);
        if(scenario==0)type.NormalizeParameters=[&](const auto&,const Map& parameters){++normalized;return parameters;};host.Registry->Register(std::move(type));const auto source=host.Source();const auto before=host.Document->ExportDocument();
        auto reverse=host.Command("AddOperation");reverse["typeId"]=std::string("icax.edit.reverse");reverse["parameters"]=Map{};reverse["inputs"]=Array{ToData(SOperationInput{"input",source.Port,true,{}})};reverse["expectedInputs"]=Map{{Key(source.Port),source.Fingerprint}};
        auto consumer=ChainOperation(host.Plan,"test.context.pending-check",ID(At(reverse,"commandId")),{},{{"assetKey",std::string("asset")}});
        if(scenario==1){auto inputs=ArrayAt(consumer,"inputs");auto binding=inputs[0].To<Map>();binding["ranges"]=Array{ToData(SPathRange{{source.Port,source.Value.Geometry.Segments[0].Id,0.2},{source.Port,source.Value.Geometry.Segments[0].Id,0.8},false})};inputs[0]=binding;consumer["inputs"]=inputs;}
        if(scenario==2)consumer["parameters"]=Map{{"unrecognizedParameter",1}};
        bool rejected=false;std::string error;try{host.Document->ExecuteBatch({reverse,consumer});}catch(const std::exception& failure){rejected=true;error=failure.what();}
        EXPECT_TRUE(rejected);if(scenario==2)EXPECT_NE(std::string::npos,error.find("TOOLPATH_UNKNOWN_PARAMETER"));EXPECT_EQ(before,host.Document->ExportDocument());EXPECT_EQ(0,normalized.load());EXPECT_EQ(0,state->EvaluationCount.load());
    }
}
