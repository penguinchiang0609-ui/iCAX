#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
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
std::shared_ptr<IRepository> ImportRepository()
{auto meta=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*meta);return GenerateRepository(GenerateNewUUID(),meta);}
Map& MutableMap(Variant& value){return std::get<Map>(value.m_Value);}
Array& MutableArray(Variant& value){return std::get<Array>(value.m_Value);}
struct ImportFixture
{
    std::shared_ptr<IRepository> Db=ImportRepository();
    std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();
    std::unique_ptr<CEditableToolpathDocument> Document;
    IntentId Plan,Source;
    ImportFixture()
    {
        RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry);Plan=Document->CreatePlan("import");
        SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"edge");
        const auto reply=Document->Execute({{"kind",std::string("CreateSource")},{"commandId",GenerateNewUUID()},{"planId",Plan},{"value",ToData(value)},{"name",std::string("source")}});Source=ID(At(reply,"operationId"));Generate();
    }
    void Generate()
    {auto task=Document->GenerateAsync(Plan);while(!task->IsReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));Document->Publish(task);}
    Map Saved()const{return Document->ExportDocument();}
};
Map& ComponentProperties(Map& saved,const std::string& classSuffix)
{
    for(auto& value:MutableArray(saved.at("entities")))for(auto& component:MutableArray(MutableMap(value).at("components")))
    {auto& data=MutableMap(component);if(Text(data,"class")==classSuffix)return MutableMap(data.at("properties"));}
    throw std::runtime_error("test component missing");
}
Map& DerivedRecord(Map& saved,const std::string& kind)
{
    for(auto& value:MutableArray(saved.at("entities")))for(auto& component:MutableArray(MutableMap(value).at("components")))
    {auto& data=MutableMap(component);if(Text(data,"class")!="CEditableDerivedComponent")continue;auto& record=MutableMap(MutableMap(data.at("properties")).at("Data"));if(Text(record,"kind")==kind)return record;}
    throw std::runtime_error("test derived record missing");
}
void RejectWithoutWrites(const Map& saved)
{
    auto db=ImportRepository();
    const auto snapshot=[&]
    {Map entities;for(const auto& entity:db->FilterEntities([](const auto&){return true;}))
        {Map components;for(const auto& name:entity->GetComponentClasses()){const auto component=entity->GetComponent(name);components[name]=Map{{"properties",component->GetProperties()},{"enabled",component->IsEnable()}};}entities[Key(entity->GetID())]=std::move(components);}return entities;};
    const auto before=snapshot();EXPECT_THROW(CEditableToolpathDocument::ImportDocument(db,saved),std::exception);
    EXPECT_EQ(before,snapshot());EXPECT_FALSE(db->IsWriteInProgress());
}
class CUnavailableImportGeometry final:public IToolpathGeometryServices
{
public:
    Point3 Evaluate(const SEditableGeometry&,const SEditableSegment&,double)const override{throw std::invalid_argument("TOOLPATH_UNSUPPORTED_CURVE: missing backend");}
    void Validate(const SEditableGeometry&)const override{throw std::invalid_argument("TOOLPATH_UNSUPPORTED_CURVE: missing backend");}
    void ValidateManufacturing(const std::string&,const SOperationEvaluation&,const SOperationCandidate&)const override{}
};
}

TEST(EditableToolpathImport, ValidSourceAndDormantUndoCachesRoundTrip)
{
    ImportFixture fixture;const auto geometry=CreateToolpathGeometryServices();auto saved=fixture.Saved();
    EXPECT_NO_THROW(ValidateToolpathImport(saved,*geometry));
    auto db=ImportRepository();const auto id=CEditableToolpathDocument::ImportDocument(db,saved);CEditableToolpathDocument reopened(db,id,fixture.Registry);
    EXPECT_EQ(EOutputCompleteness::Complete,reopened.Resolve(fixture.Plan).Status);
    ASSERT_TRUE(fixture.Db->Undo());saved=fixture.Saved();EXPECT_NO_THROW(ValidateToolpathImport(saved,*geometry));
}
TEST(EditableToolpathImport, RejectsMissingDocumentPlanAndSourceListBeforeMutation)
{
    ImportFixture fixture;auto saved=fixture.Saved();auto& document=MutableMap(ComponentProperties(saved,"CEditableDocumentComponent").at("Definition"));
    document["plans"]=Array{};RejectWithoutWrites(saved);
    saved=fixture.Saved();MutableMap(ComponentProperties(saved,"CEditableDocumentComponent").at("Definition"))["sources"]=Array{};RejectWithoutWrites(saved);
}
TEST(EditableToolpathImport, RejectsCorruptSavedResultGeometryAndFingerprintBeforeMutation)
{
    ImportFixture fixture;auto saved=fixture.Saved();auto& record=DerivedRecord(saved,"operationResult");auto& output=MutableMap(MutableArray(record.at("outputs")).front());
    auto value=ToolpathFromData(MapAt(output,"value"));value.Geometry.Tolerance=-1;output["value"]=ToData(value);RejectWithoutWrites(saved);
    saved=fixture.Saved();auto& other=MutableMap(MutableArray(DerivedRecord(saved,"operationResult").at("outputs")).front());other["fingerprint"]=std::string("invalid");RejectWithoutWrites(saved);
}
TEST(EditableToolpathImport, RejectsOwnerAndPortIdentityCorruptionBeforeMutation)
{
    ImportFixture fixture;auto saved=fixture.Saved();ComponentProperties(saved,"CEditableDerivedComponent")["Owner"]=GenerateNewUUID();RejectWithoutWrites(saved);
    saved=fixture.Saved();auto& result=DerivedRecord(saved,"operationResult");auto& output=MutableMap(MutableArray(result.at("outputs")).front());output["portId"]=GenerateNewUUID();RejectWithoutWrites(saved);
}
TEST(EditableToolpathImport, RejectsCurrentResultsWithoutAnEvaluatorRevisionBeforeMutation)
{
    ImportFixture fixture;auto saved=fixture.Saved();DerivedRecord(saved,"operationResult").erase("evaluationRevision");RejectWithoutWrites(saved);
    saved=fixture.Saved();DerivedRecord(saved,"operationResult")["evaluationRevision"]=std::string();RejectWithoutWrites(saved);
}
TEST(EditableToolpathImport, RejectsUnknownRootAndGroupCycleBeforeMutation)
{
    ImportFixture fixture;auto saved=fixture.Saved();auto& plan=MutableMap(ComponentProperties(saved,"CEditablePlanComponent").at("Definition"));
    plan["roots"]=Array{ToData(SPortRef{fixture.Source,GenerateNewUUID()})};RejectWithoutWrites(saved);
    saved=fixture.Saved();auto& secondPlan=MutableMap(ComponentProperties(saved,"CEditablePlanComponent").at("Definition"));secondPlan["groups"]=Map{{"a",Map{{"members",Array{}},{"parentGroupId",std::string("b")}}},{"b",Map{{"members",Array{}},{"parentGroupId",std::string("a")}}}};RejectWithoutWrites(saved);
}
TEST(EditableToolpathImport, MissingAlgorithmKeepsExactSavedValues)
{
    ImportFixture fixture;auto saved=fixture.Saved();auto& source=MutableMap(ComponentProperties(saved,"CEditableOperationComponent").at("Definition"));source["algorithmVersion"]=std::string("unavailable");
    const auto geometry=CreateToolpathGeometryServices();EXPECT_NO_THROW(ValidateToolpathImport(saved,*geometry));auto db=ImportRepository();const auto id=CEditableToolpathDocument::ImportDocument(db,saved);
    CEditableToolpathDocument reopened(db,id,fixture.Registry);EXPECT_EQ(EOutputCompleteness::Incomplete,reopened.Resolve(fixture.Plan).Status);EXPECT_EQ(saved,reopened.ExportDocument());
}
TEST(EditableToolpathImport, MissingGeometryBackendPreservesValuesButDoesNotHideStructuralDamage)
{
    ImportFixture fixture;auto saved=fixture.Saved();CUnavailableImportGeometry unavailable;
    EXPECT_NO_THROW(ValidateToolpathImport(saved,unavailable));
    auto& source=MutableMap(ComponentProperties(saved,"CEditableOperationComponent").at("Definition"));
    auto& parameters=MutableMap(source.at("parameters"));auto value=ToolpathFromData(MapAt(parameters,"value"));
    value.Geometry.Segments.push_back(value.Geometry.Segments.front());parameters["value"]=ToData(value);
    EXPECT_THROW(ValidateToolpathImport(saved,unavailable),std::exception);
}
