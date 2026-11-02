#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <atomic>

namespace
{
using namespace iCAX::CAM::Intent;using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Database;using namespace iCAX::Data;using namespace iCAX::GeometryData;
std::shared_ptr<IRepository> LayerDb(){auto meta=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*meta);return GenerateRepository(GenerateNewUUID(),meta);}
struct LayerHost
{
 std::shared_ptr<IRepository> Db=LayerDb();std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();std::unique_ptr<CEditableToolpathDocument> Doc;IntentId Plan;
 LayerHost(){RegisterStandardToolpathOperations(*Registry);Doc=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry);Plan=Doc->CreatePlan("layers");}
 Map Command(const std::string& kind)const{return {{"kind",kind},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
 std::vector<IntentId> Layers()const{std::vector<std::pair<unsigned long long,IntentId>> sorted;for(const auto& [_,item]:MapAt(Doc->QueryLayers(),"catalog")){const auto layer=item.To<Map>();if(Text(layer,"kind")=="Visible")sorted.emplace_back(UnsignedValue(At(layer,"order")),ID(At(layer,"id")));}std::sort(sorted.begin(),sorted.end());std::vector<IntentId> ids;for(const auto& [_,id]:sorted)ids.push_back(id);return ids;}
 void Await(const std::shared_ptr<CToolpathGeneration>& task){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!task->IsReady()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());}
 void Generate(){auto task=Doc->GenerateAsync(Plan);Await(task);ASSERT_TRUE(Doc->Publish(task));}
 std::vector<SResolvedPath> Paths()const{return Doc->Resolve(Plan).Paths;}
 SResolvedPath Source(){SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"layers/source");auto command=Command("CreateSource");command["value"]=ToData(value);const auto receipt=Doc->Execute(command);Generate();for(const auto& path:Paths())if(path.PathId==ID(At(receipt,"pathId")))return path;throw std::runtime_error("source missing");}
 void Assign(const std::vector<SResolvedPath>& paths,IntentId layer){auto command=Command("AssignPathLayers");Array ids;for(const auto& path:paths)ids.emplace_back(path.PathId);command["pathIds"]=ids;command["expectedPlanFingerprint"]=At(Doc->ReadPlanDefinition(Plan),"definitionFingerprint");command["layers"]=Map{{"visibleLayerId",layer}};Doc->Execute(command);}
 void Update(IntentId layer,const Map& patch){auto command=Command("UpdateLayer");command["layerId"]=layer;command["properties"]=patch;Doc->Execute(command);}
 Map Split(const SResolvedPath& path){auto command=Command("AddOperation");command["typeId"]=std::string("icax.edit.split");command["parameters"]=Map{{"cuts",Array{Map{{"id",std::string("middle")},{"anchor",ToData(SPathAnchor{path.Port,path.Value.Geometry.Segments.front().Id,.5})}}}}};command["inputs"]=Array{ToData(SOperationInput{"input",path.Port,true,{}})};command["expectedInputs"]=Map{{Key(path.Port),path.Fingerprint}};return Doc->Execute(command);}
};
IntentId Visible(const SResolvedPath& path){return ID(At(path.Layers,"visibleLayerId"));}
}
TEST(EditableToolpathLayers, EightDefaultsArePersistentAndNewDefaultsDoNotMoveExistingPaths)
{
 LayerHost h;const auto layers=h.Layers();ASSERT_EQ(8,layers.size());const auto a=h.Source();EXPECT_EQ(layers[0],Visible(a));const auto before=a.Fingerprint;
 auto defaults=h.Command("SetDefaultLayers");defaults["layers"]=Map{{"visibleLayerId",layers[2]}};h.Doc->Execute(defaults);const auto b=h.Source();EXPECT_EQ(layers[2],Visible(b));EXPECT_EQ(layers[0],Visible(h.Doc->ReadPath(h.Plan,a.Port)));
 h.Assign(h.Paths(),layers[4]);for(const auto& path:h.Paths())EXPECT_EQ(layers[4],Visible(path));EXPECT_EQ(before,h.Doc->ReadPath(h.Plan,a.Port).Fingerprint);
 auto db=LayerDb();const auto id=CEditableToolpathDocument::ImportDocument(db,h.Doc->ExportDocument());CEditableToolpathDocument reopened(db,id,h.Registry);EXPECT_EQ(h.Doc->QueryLayers(),reopened.QueryLayers());for(const auto& path:reopened.Resolve(h.Plan).Paths)EXPECT_EQ(layers[4],Visible(path));
}
TEST(EditableToolpathLayers, ColorAndVisibilityDoNotChangeGeometryOrProcessingParticipation)
{
 LayerHost h;const auto a=h.Source();const auto layer=Visible(a);const auto fingerprint=a.Fingerprint;h.Update(layer,{{"color",std::string("#123ABC")},{"visible",false}});const auto paths=h.Paths();ASSERT_EQ(1,paths.size());EXPECT_EQ(fingerprint,paths[0].Fingerprint);EXPECT_EQ("#123ABC",Text(paths[0].Layers,"color"));EXPECT_FALSE(Flag(paths[0].Layers,"visible"));EXPECT_TRUE(paths[0].Participating);
 h.Update(layer,{{"locked",true}});EXPECT_TRUE(h.Paths()[0].Locked);EXPECT_THROW(h.Assign(h.Paths(),h.Layers()[1]),std::invalid_argument);EXPECT_EQ(1,h.Paths().size());h.Update(layer,{{"locked",false}});h.Assign(h.Paths(),h.Layers()[1]);
}
TEST(EditableToolpathLayers, SplitInheritanceAndManualOverridesSurviveRegenerationAndUndo)
{
 LayerHost h;const auto layers=h.Layers();const auto source=h.Source();h.Assign({source},layers[1]);h.Split(h.Paths()[0]);h.Generate();auto paths=h.Paths();ASSERT_EQ(2,paths.size());for(const auto& path:paths)EXPECT_EQ(layers[1],Visible(path));
 h.Assign({paths[0]},layers[3]);const auto changed=paths[0].PathId;h.Generate();for(const auto& path:h.Paths())EXPECT_EQ(path.PathId==changed?layers[3]:layers[1],Visible(path));
 ASSERT_TRUE(h.Db->Undo());for(const auto& path:h.Paths())EXPECT_EQ(layers[1],Visible(path));ASSERT_TRUE(h.Db->Redo());for(const auto& path:h.Paths())EXPECT_EQ(path.PathId==changed?layers[3]:layers[1],Visible(path));
 auto reset=h.Command("ResetPathLayerOverrides");paths=h.Paths();const auto chosen=*std::find_if(paths.begin(),paths.end(),[&](const auto& path){return path.PathId==changed;});reset["targets"]=Array{ToData(chosen.Port)};reset["expectedInputs"]=Map{{Key(chosen.Port),chosen.Fingerprint}};reset["layers"]=Map{{"visibleLayerId",true}};h.Doc->Execute(reset);for(const auto& path:h.Paths())EXPECT_EQ(layers[1],Visible(path));
}
TEST(EditableToolpathLayers, DeleteMigratesAllPlansDefaultsAndSeedsAtomically)
{
 LayerHost h;const auto layers=h.Layers();const auto a=h.Source();const auto other=h.Doc->CreatePlan("other");auto roots=h.Command("SetPlanRoots");roots["planId"]=other;roots["roots"]=Array{ToData(a.Port)};roots["expectedPlanFingerprint"]=At(h.Doc->ReadPlanDefinition(other),"definitionFingerprint");h.Doc->Execute(roots);
 auto remove=h.Command("DeleteLayer");remove["layerId"]=layers[0];EXPECT_THROW(h.Doc->Execute(remove),std::invalid_argument);EXPECT_EQ(8,h.Layers().size());remove["targetLayerId"]=layers[2];h.Doc->Execute(remove);EXPECT_EQ(7,h.Layers().size());EXPECT_EQ(layers[2],Visible(h.Paths()[0]));EXPECT_EQ(layers[2],Visible(h.Doc->Resolve(other).Paths[0]));EXPECT_EQ(layers[2],ID(At(h.Doc->QueryLayers(),"visibleLayerId")));
 ASSERT_TRUE(h.Db->Undo());EXPECT_EQ(8,h.Layers().size());EXPECT_EQ(layers[0],Visible(h.Paths()[0]));ASSERT_TRUE(h.Db->Redo());EXPECT_EQ(7,h.Layers().size());
 auto db=LayerDb();EXPECT_NO_THROW(CEditableToolpathDocument::ImportDocument(db,h.Doc->ExportDocument()));
}
TEST(EditableToolpathLayers, CustomLayersCanBeAddedRecoloredAndDeletedBeyondEight)
{
 LayerHost h;const auto id=GenerateNewUUID();auto create=h.Command("CreateLayer");create["layerId"]=id;create["properties"]=Map{{"name",std::string("自定义")},{"color",std::string("#112233")}};h.Doc->Execute(create);EXPECT_EQ(9,h.Layers().size());h.Update(id,{{"color",std::string("#445566")},{"name",std::string("轮廓")}});
 auto remove=h.Command("DeleteLayer");remove["layerId"]=id;h.Doc->Execute(remove);EXPECT_EQ(8,h.Layers().size());ASSERT_TRUE(h.Db->Undo());EXPECT_EQ("#445566",Text(MapAt(h.Doc->QueryLayers(),"catalog").at(Key(id)).To<Map>(),"color"));
}
TEST(EditableToolpathLayers, GeometryPublicationDoesNotOverwriteAConcurrentLayerChange)
{
 LayerHost h;const auto a=h.Source();h.Split(a);auto task=h.Doc->GenerateAsync(h.Plan);h.Await(task);h.Assign({a},h.Layers()[5]);ASSERT_TRUE(h.Doc->Publish(task));ASSERT_EQ(2,h.Paths().size());for(const auto& path:h.Paths())EXPECT_EQ(h.Layers()[5],Visible(path));
}
TEST(EditableToolpathLayers, InvalidColorsAndDanglingImportReferencesAreRejected)
{
 LayerHost h;const auto a=h.Source();const auto before=h.Doc->ExportDocument();EXPECT_THROW(h.Update(Visible(a),{{"color",std::string("url(evil)")}}),std::invalid_argument);EXPECT_EQ(before,h.Doc->ExportDocument());
 auto saved=before;auto entities=ArrayAt(saved,"entities");for(auto& item:entities){auto entity=item.To<Map>();if(ID(At(entity,"id"))!=h.Doc->Id())continue;auto components=ArrayAt(entity,"components");for(auto& value:components){auto component=value.To<Map>();if(Text(component,"class")!=CEditableDocumentComponent::S_ClassName)continue;auto props=MapAt(component,"properties"),definition=MapAt(props,"Definition"),layers=MapAt(definition,"layers");layers["visibleLayerId"]=GenerateNewUUID();definition["layers"]=layers;props["Definition"]=definition;component["properties"]=props;value=component;}entity["components"]=components;item=entity;}saved["entities"]=entities;auto db=LayerDb();const auto initialCount=db->FilterEntities([](const auto&){return true;}).size();EXPECT_THROW(CEditableToolpathDocument::ImportDocument(db,saved),std::invalid_argument);EXPECT_EQ(initialCount,db->FilterEntities([](const auto&){return true;}).size());
}
TEST(EditableToolpathLayers, CrossLayerMergeUsesAnExplicitTargetAndSnapshotCopyKeepsItsOwnLayer)
{
 LayerHost h;const auto palette=h.Layers();h.Split(h.Source());h.Generate();auto parts=h.Paths();ASSERT_EQ(2,parts.size());h.Assign({parts[0]},palette[1]);h.Assign({parts[1]},palette[2]);parts=h.Paths();
 auto merge=h.Command("AddOperation");merge["typeId"]=std::string("icax.edit.merge");merge["parameters"]=Map{};merge["inputs"]=Array{ToData(SOperationInput{"a",parts[0].Port,true,{}}),ToData(SOperationInput{"b",parts[1].Port,true,{}})};merge["expectedInputs"]=Map{{Key(parts[0].Port),parts[0].Fingerprint},{Key(parts[1].Port),parts[1].Fingerprint}};merge["outputLayers"]=Map{{"visibleLayerId",palette[4]}};h.Doc->Execute(merge);h.Generate();ASSERT_EQ(1,h.Paths().size());const auto merged=h.Paths()[0];EXPECT_EQ(palette[4],Visible(merged));
 auto copy=h.Command("CopyPaths");copy["targets"]=Array{ToData(merged.Port)};copy["expectedInputs"]=Map{{Key(merged.Port),merged.Fingerprint}};h.Doc->Execute(copy);h.Generate();ASSERT_EQ(2,h.Paths().size());for(const auto& path:h.Paths())EXPECT_EQ(palette[4],Visible(path));h.Assign({merged},palette[6]);for(const auto& path:h.Paths())EXPECT_EQ(path.PathId==merged.PathId?palette[6]:palette[4],Visible(path));
}
TEST(EditableToolpathLayers, ClonedPlansRemapMembershipWithoutSharingWritableAssignments)
{
 LayerHost h;const auto palette=h.Layers();const auto original=h.Source();h.Assign({original},palette[3]);const auto copied=h.Doc->CreatePlan("copied");auto clone=h.Command("ClonePlan");clone["planId"]=copied;clone["sourcePlanId"]=h.Plan;clone["expectedSourcePlanFingerprint"]=At(h.Doc->ReadPlan(h.Plan),"graphFingerprint");h.Doc->Execute(clone);auto task=h.Doc->GenerateAsync(copied);h.Await(task);ASSERT_TRUE(h.Doc->Publish(task));auto paths=h.Doc->Resolve(copied).Paths;ASSERT_EQ(1,paths.size());EXPECT_NE(original.PathId,paths[0].PathId);EXPECT_EQ(palette[3],Visible(paths[0]));h.Assign({original},palette[6]);EXPECT_EQ(palette[3],Visible(h.Doc->Resolve(copied).Paths[0]));
 auto db=LayerDb();const auto id=CEditableToolpathDocument::ImportDocument(db,h.Doc->ExportDocument());CEditableToolpathDocument reopened(db,id,h.Registry);EXPECT_EQ(palette[3],Visible(reopened.Resolve(copied).Paths[0]));EXPECT_EQ(palette[6],Visible(reopened.Resolve(h.Plan).Paths[0]));
}
TEST(EditableToolpathLayers, LayerAssignmentQueryTracksTerminalsLocksAndHistory)
{
 LayerHost h;const auto source=h.Source();const auto palette=h.Layers();
 const auto verify=[&]{const auto items=h.Doc->QueryLayerAssignments(h.Plan);std::size_t count=0;
  for(const auto& path:h.Doc->InspectPlan(h.Plan).Paths){if(path.ParticipationReason=="Deleted"||path.ParticipationReason=="Detached")continue;++count;
   ASSERT_TRUE(items.contains(Key(path.PathId)));const auto item=items.at(Key(path.PathId)).To<Map>();EXPECT_EQ(path.Layers,MapAt(item,"layers"));EXPECT_EQ(path.Locked,Flag(item,"locked"));EXPECT_EQ(ToData(path.Port),MapAt(item,"port"));}
  EXPECT_EQ(count,items.size());};
 verify();h.Assign({source},palette[2]);verify();h.Split(h.Paths()[0]);verify();h.Generate();verify();auto paths=h.Paths();ASSERT_EQ(2,paths.size());
 h.Assign({paths[0]},palette[4]);verify();h.Update(palette[4],{{"locked",true}});verify();EXPECT_THROW(h.Assign({paths[0]},palette[1]),std::invalid_argument);
 ASSERT_TRUE(h.Db->Undo());verify();ASSERT_TRUE(h.Db->Redo());verify();h.Update(palette[4],{{"locked",false}});
 auto stale=h.Command("AssignPathLayers");stale["pathIds"]=Array{paths[0].PathId};stale["expectedPlanFingerprint"]=At(h.Doc->ReadPlanDefinition(h.Plan),"definitionFingerprint");stale["layers"]=Map{{"visibleLayerId",palette[1]}};
 h.Assign({paths[1]},palette[3]);EXPECT_THROW(h.Doc->Execute(stale),std::invalid_argument);verify();
}
TEST(EditableToolpathLayers, LayerAssignmentAndReadbackDoNotValidateGeometry)
{
 class CountingGeometry final:public IToolpathGeometryServices{
  std::shared_ptr<const IToolpathGeometryServices> Base=CreateToolpathGeometryServices();
 public:mutable std::atomic<int> Validations{0};
  Point3 Evaluate(const SEditableGeometry& geometry,const SEditableSegment& segment,double parameter)const override{return Base->Evaluate(geometry,segment,parameter);}
  void Validate(const SEditableGeometry& geometry)const override{++Validations;Base->Validate(geometry);}
  void ValidateManufacturing(const std::string& type,const SOperationEvaluation& input,const SOperationCandidate& candidate)const override{Base->ValidateManufacturing(type,input,candidate);}
 };
 LayerHost h;const auto geometry=std::make_shared<CountingGeometry>();h.Doc=std::make_unique<CEditableToolpathDocument>(h.Db,h.Doc->Id(),h.Registry,geometry);
 h.Split(h.Source());h.Generate();const auto paths=h.Paths();const auto palette=h.Layers();geometry->Validations=0;
 h.Assign(paths,palette[3]);auto layers=h.Doc->QueryLayerAssignments(h.Plan);EXPECT_EQ(2,layers.size());EXPECT_EQ(0,geometry->Validations.load());
 for(const auto& path:paths)EXPECT_EQ(palette[3],ID(At(MapAt(layers.at(Key(path.PathId)).To<Map>(),"layers"),"visibleLayerId")));
 h.Update(palette[3],{{"color",std::string("#123456")}});layers=h.Doc->QueryLayerAssignments(h.Plan);EXPECT_EQ(0,geometry->Validations.load());
}
