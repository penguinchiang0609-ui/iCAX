#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include <chrono>
#include <thread>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Data;
using namespace iCAX::Database;
using namespace iCAX::GeometryData;
std::shared_ptr<IRepository> LabelRepository()
{auto registry=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*registry);return GenerateRepository(GenerateNewUUID(),registry);}
SToolpathValue LabelLine(double length=10)
{SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{length,0,0}},{0,1,false}}},false},"label/source");return value;}
struct LabelHost
{
    std::shared_ptr<IRepository> Db=LabelRepository();std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();std::unique_ptr<CEditableToolpathDocument> Document;IntentId Plan,SourceOperation;
    LabelHost(){RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry);Plan=Document->CreatePlan("labels");}
    Map Command(const std::string& kind)const{return {{"kind",kind},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
    std::vector<SResolvedPath> Paths()const{return Document->Resolve(Plan).Paths;}
    void Await(const std::shared_ptr<CToolpathGeneration>& task)
    {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!task->IsReady()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());}
    void Generate(){auto task=Document->GenerateAsync(Plan);Await(task);ASSERT_TRUE(Document->Publish(task));}
    SResolvedPath Source(const std::string& scope,const std::string& number,const std::string& name="A")
    {auto command=Command("CreateSource");command["value"]=ToData(LabelLine());command["name"]=name;command["userNumber"]=number;command["labelScope"]=scope;const auto reply=Document->Execute(command);SourceOperation=ID(At(reply,"operationId"));Generate();const auto id=ID(At(reply,"pathId"));for(const auto& path:Paths())if(path.PathId==id)return path;throw std::runtime_error("Label source missing");}
    Map Policy(const std::string& scope,const std::string& name={})const
    {Map policy{{"numberScope",scope},{"firstNumber",1u}};if(!name.empty())policy["name"]=name;return policy;}
    Map Operation(const std::string& type,const SResolvedPath& input,const Map& parameters,const Map& policy)
    {auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;command["outputLabelPolicy"]=policy;command["inputs"]=Array{ToData(SOperationInput{"input",input.Port,true,{}})};command["expectedInputs"]=Map{{Key(input.Port),input.Fingerprint}};return Document->Execute(command);}
    Map Microjoint(const SResolvedPath& input,const Map& policy)
    {const auto& segment=input.Value.Geometry.Segments.front();const auto range=Range(segment);const auto a=First(range)+(Last(range)-First(range))*0.4,b=First(range)+(Last(range)-First(range))*0.6;
     const Map gap{{"id",std::string("joint")},{"range",ToData(SPathRange{{input.Port,segment.Id,a},{input.Port,segment.Id,b},false})}};return Operation("icax.microjoint",input,{{"gaps",Array{gap}}},policy);}
    Map Split(const SResolvedPath& input,const Map& policy,const std::string& key="cut")
    {const auto& segment=input.Value.Geometry.Segments.front();const auto range=Range(segment);const Map cut{{"id",key},{"anchor",ToData(SPathAnchor{input.Port,segment.Id,(range.First+range.Last)/2})}};return Operation("icax.edit.split",input,{{"cuts",Array{cut}}},policy);}
    void Rename(const SResolvedPath& input,const std::string& number)
    {auto command=Command("SetLabels");command["pathId"]=input.PathId;command["userNumber"]=number;command["expectedLabelsVersion"]=static_cast<int>(input.LabelsVersion);Document->Execute(command);}
    void Delete(const SResolvedPath& input)
    {auto command=Command("DeletePaths");command["targets"]=Array{ToData(input.Port)};command["expectedInputs"]=Map{{Key(input.Port),input.Fingerprint}};Document->Execute(command);}
};
std::map<IntentId,std::pair<std::string,std::string>> Labels(const std::vector<SResolvedPath>& paths)
{std::map<IntentId,std::pair<std::string,std::string>> result;for(const auto& path:paths)result[path.PathId]={path.Name,path.UserNumber};return result;}
std::set<std::string> Numbers(const std::vector<SResolvedPath>& paths)
{std::set<std::string> result;for(const auto& path:paths)result.insert(path.UserNumber);return result;}
}

TEST(EditableToolpathLabels, MicrojointAndSplitKeepTheirInitialLabelsThroughUpdatesUndoAndReopen)
{
    LabelHost host;const auto source=host.Source("setup-A","40");host.Microjoint(source,host.Policy("setup-A","加工段"));host.Generate();const auto micro=host.Paths();ASSERT_EQ(2,micro.size());EXPECT_EQ((std::set<std::string>{"41","42"}),Numbers(micro));for(const auto& path:micro)EXPECT_EQ("加工段",path.Name);
    const auto split=host.Split(micro[0],host.Policy("setup-A"));host.Generate();const auto initial=Labels(host.Paths());ASSERT_EQ(3,initial.size());for(const auto& path:host.Paths())EXPECT_FALSE(path.Name.empty());
    auto update=host.Command("UpdateSource");update["operationId"]=host.SourceOperation;update["expectedOperationFingerprint"]=At(host.Document->ReadOperation(host.SourceOperation),"definitionFingerprint");update["value"]=ToData(LabelLine(12));host.Document->Execute(update);host.Generate();EXPECT_EQ(initial,Labels(host.Paths()));
    ASSERT_TRUE(host.Db->Undo());host.Generate();EXPECT_EQ(initial,Labels(host.Paths())); // source update
    ASSERT_TRUE(host.Db->Undo());host.Generate();EXPECT_EQ(Labels(micro),Labels(host.Paths())); // split
    ASSERT_TRUE(host.Db->Redo());host.Generate();EXPECT_EQ(initial,Labels(host.Paths()));
    ASSERT_TRUE(host.Db->Redo());host.Generate();EXPECT_EQ(initial,Labels(host.Paths()));
    const auto saved=host.Document->ExportDocument();auto db=LabelRepository();const auto id=CEditableToolpathDocument::ImportDocument(db,saved);CEditableToolpathDocument reopened(db,id,host.Registry);EXPECT_EQ(EOutputCompleteness::Complete,reopened.Resolve(host.Plan).Status);EXPECT_EQ(initial,Labels(reopened.Resolve(host.Plan).Paths));
}
TEST(EditableToolpathLabels, DeletedManualNumbersAndDormantInitialNumbersRemainReserved)
{
    LabelHost host;const auto source=host.Source("setup-A","10");host.Microjoint(source,host.Policy("setup-A","段"));host.Generate();const auto micro=host.Paths();ASSERT_EQ(2,micro.size());EXPECT_EQ((std::set<std::string>{"11","12"}),Numbers(micro));
    host.Rename(micro[0],"90");const auto renamed=host.Paths();for(const auto& path:renamed)if(path.PathId==micro[0].PathId)EXPECT_EQ("段",path.Name);
    SResolvedPath deleted,retained;for(const auto& path:host.Paths())if(path.PathId==micro[0].PathId)deleted=path;else retained=path;host.Delete(deleted);
    host.Split(retained,host.Policy("setup-A"),"first");host.Generate();EXPECT_EQ((std::set<std::string>{"91","92"}),Numbers(host.Paths()));
    ASSERT_TRUE(host.Db->Undo());host.Generate();const auto restored=host.Paths();ASSERT_EQ(1,restored.size());EXPECT_EQ(retained.PathId,restored[0].PathId);
    host.Split(restored[0],host.Policy("setup-A"),"second");host.Generate();EXPECT_EQ((std::set<std::string>{"93","94"}),Numbers(host.Paths()));
}
TEST(EditableToolpathLabels, InheritedIdentityKeepsUserLabelsAndOtherScopesDoNotRaiseTheSequence)
{
    LabelHost host;const auto source=host.Source("setup-A","7","原始名称");host.Operation("icax.edit.reverse",source,{},host.Policy("setup-A","不应覆盖"));host.Generate();const auto reversed=host.Paths();ASSERT_EQ(1,reversed.size());EXPECT_EQ(source.PathId,reversed[0].PathId);EXPECT_EQ("7",reversed[0].UserNumber);EXPECT_EQ("原始名称",reversed[0].Name);
    host.Source("setup-B","1000");SResolvedPath input;for(const auto& path:host.Paths())if(path.PathId==source.PathId)input=path;host.Microjoint(input,host.Policy("setup-A"));host.Generate();EXPECT_EQ((std::set<std::string>{"8","9","1000"}),Numbers(host.Paths()));
}
TEST(EditableToolpathLabels, AUserOverrideCanUndoBackToTheDerivedInitialFacts)
{
    LabelHost host;const auto source=host.Source("setup-A","5");host.Microjoint(source,host.Policy("setup-A","初始名"));host.Generate();const auto initial=host.Paths();ASSERT_EQ(2,initial.size());const auto path=initial[0];
    auto rename=host.Command("SetLabels");rename["pathId"]=path.PathId;rename["name"]=std::string("手工名");rename["expectedLabelsVersion"]=static_cast<unsigned long long>(path.LabelsVersion);host.Document->Execute(rename);
    for(const auto& current:host.Paths())if(current.PathId==path.PathId){EXPECT_EQ("手工名",current.Name);EXPECT_EQ(path.UserNumber,current.UserNumber);}
    ASSERT_TRUE(host.Db->Undo());EXPECT_EQ(Labels(initial),Labels(host.Paths()));ASSERT_TRUE(host.Db->Redo());
    for(const auto& current:host.Paths())if(current.PathId==path.PathId){EXPECT_EQ("手工名",current.Name);EXPECT_EQ(path.UserNumber,current.UserNumber);}
}
TEST(EditableToolpathLabels, AFailedPublicationDoesNotConsumeTheInitialNumberRange)
{
    LabelHost host;const auto source=host.Source("setup-A","50");const auto command=host.Split(source,host.Policy("setup-A"));const auto operation=ID(At(command,"operationId"));const auto definition=host.Document->ReadOperation(operation);const auto candidate=host.Document->Preview("icax.edit.split",MapAt(definition,"parameters"),{source.Port});ASSERT_EQ(2,candidate.Outputs.size());
    const auto collision=NamedID(Key(operation)+"/path/"+candidate.Outputs.front().RoleKey);auto& create=host.Db->BeginTransaction("label collision");create.CreateEntity(collision);ASSERT_TRUE(host.Db->CommitTransaction(create));auto task=host.Document->GenerateAsync(host.Plan);host.Await(task);
    EXPECT_THROW(host.Document->Publish(task),std::exception);EXPECT_FALSE(host.Db->IsWriteInProgress());EXPECT_TRUE(MapAt(host.Document->ReadOperation(operation),"result").empty());
    auto& remove=host.Db->BeginTransaction("remove label collision");remove.DisposeEntity(collision);ASSERT_TRUE(host.Db->CommitTransaction(remove));ASSERT_TRUE(host.Document->Publish(task));EXPECT_EQ((std::set<std::string>{"51","52"}),Numbers(host.Paths()));
    const auto initial=Labels(host.Paths());host.Generate();EXPECT_EQ(initial,Labels(host.Paths()));
}
TEST(EditableToolpathLabels, InvalidNumberingPolicyIsRejectedBeforePublishingDefinitionChanges)
{
    const std::vector<Map> invalid{Map{{"numberScope",std::string("setup")},{"firstNumber",0}},Map{{"numberScope",std::string("setup")},{"firstNumber",-1}},
        Map{{"numberScope",std::string("setup")},{"firstNumber",1.5}},Map{{"numberScope",std::string()},{"firstNumber",1u}},Map{{"numberScope",std::string("setup")},{"firstNumber",std::string("4")}},
        Map{{"numberScope",std::string("setup")},{"firstNumber",1u},{"name",12}},Map{{"numberScope",std::string("setup")},{"firstNumber",1u},{"unknown",true}}};
    for(const auto& policy:invalid){LabelHost host;const auto source=host.Source("setup","5");const auto before=host.Document->ExportDocument();EXPECT_THROW(host.Split(source,policy),std::exception);EXPECT_EQ(before,host.Document->ExportDocument());}
}
