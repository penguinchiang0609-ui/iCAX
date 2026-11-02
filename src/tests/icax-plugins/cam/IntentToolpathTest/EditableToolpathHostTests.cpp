#include <gtest/gtest.h>
#include "IntentToolpath/EditableToolpath.h"
#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/ComponentHelper.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include <chrono>
#include <thread>

namespace
{
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::Database;
using namespace iCAX::Data;
using namespace iCAX::GeometryData;

bool HostMarkerEqual(const int& a, const int& b) { return a == b; }
PropertyValue HostMarkerToData(const int& value) { return value; }
int HostMarkerFromData(const PropertyValue& value) { return value.To<int>(); }
class CHostOwnedPathComponent final : public CComponentBase
{
    DECLARE_ICAX_COMPONENT(CHostOwnedPathComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CHostOwnedPathComponent)
    DECLARED_ICAX_FIELD(CHostOwnedPathComponent, int, Marker, 0,
        HostMarkerEqual, HostMarkerToData, HostMarkerFromData)
};

struct HostFixture final
{
    std::shared_ptr<IRepository> Repository;
    std::shared_ptr<CToolpathOperationRegistry> Registry = std::make_shared<CToolpathOperationRegistry>();
    std::unique_ptr<CEditableToolpathDocument> Document;
    IntentId Plan;
    HostFixture()
    {
        auto meta = CreateMetaRegistry(); CMetaRegistrationCatalog::ReplayAll(*meta);
        Repository = GenerateRepository(GenerateNewUUID(), meta);
        RegisterStandardToolpathOperations(*Registry);
        Document = std::make_unique<CEditableToolpathDocument>(Repository, IntentId{}, Registry);
        Plan = Document->CreatePlan("host transaction");
    }
    IntentMap Source(double length = 10) const
    {
        SToolpathValue value;
        value.Geometry = MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{length,0,0}},{0,1,false}}},false}, "host/source");
        return {{"kind", std::string("CreateSource")}, {"commandId", GenerateNewUUID()}, {"planId", Plan},
            {"name", std::string("host path")}, {"value", ToData(value)}};
    }
    void Generate()
    {
        auto task = Document->GenerateAsync(Plan);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!task->IsReady() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(task->IsReady()); ASSERT_TRUE(Document->Publish(task));
    }
};
}

TEST(EditableToolpathHost, MultipleSourcesAndProductComponentsCommitAsOneUndoStep)
{
    HostFixture host;
    const std::vector<IntentMap> commands{host.Source(10), host.Source(20)};
    const IntentId first = GenerateNewUUID(), second = GenerateNewUUID();
    SEditableToolpathHostEdit edit;
    edit.UndoName = "Create product cuts";
    edit.SourceIdentities[ID(At(commands[0], "commandId"))] = {ESourcePathIdentityMode::CreateWithId, first};
    edit.SourceIdentities[ID(At(commands[1], "commandId"))] = {ESourcePathIdentityMode::CreateWithId, second};
    int preflightCalls = 0, stageCalls = 0;
    edit.Preflight = [&](const auto& receipts) {
        ++preflightCalls; EXPECT_EQ(2u, receipts.size());
        EXPECT_FALSE(host.Repository->HasEntity(first)); EXPECT_FALSE(host.Repository->HasEntity(second));
    };
    edit.Stage = [&](ITransaction& tx, const auto& receipts) {
        ++stageCalls;
        for (std::size_t i = 0; i < receipts.size(); ++i)
            tx.AttachComponent(ID(At(receipts[i], "pathId")), CHostOwnedPathComponent::S_ClassName, {{"Marker", static_cast<int>(i + 1)}});
    };
    const auto before = host.Repository->GetUndoArray();
    const auto receipts = host.Document->ExecuteBatch(commands, edit);
    ASSERT_EQ(2u, receipts.size()); EXPECT_EQ(first, ID(At(receipts[0], "pathId"))); EXPECT_EQ(second, ID(At(receipts[1], "pathId")));
    EXPECT_EQ(before.size() + 1, host.Repository->GetUndoArray().size());
    EXPECT_EQ(1, preflightCalls); EXPECT_EQ(1, stageCalls);
    EXPECT_EQ(1, host.Repository->GetEntity(first)->GetComponent<CHostOwnedPathComponent>()->GetMarker());
    EXPECT_EQ(2, host.Repository->GetEntity(second)->GetComponent<CHostOwnedPathComponent>()->GetMarker());
    host.Generate();
    auto result = host.Document->Resolve(host.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(2u, result.Paths.size());
    EXPECT_TRUE(host.Repository->Undo());
    EXPECT_FALSE(host.Repository->HasEntity(first)); EXPECT_FALSE(host.Repository->HasEntity(second));
    EXPECT_TRUE(host.Document->Resolve(host.Plan).Paths.empty());
    ASSERT_TRUE(host.Repository->Redo()); host.Generate();
    result = host.Document->Resolve(host.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(2u, result.Paths.size());
    std::set<IntentId> identities; for (const auto& path : result.Paths) identities.insert(path.PathId);
    EXPECT_EQ((std::set<IntentId>{first, second}), identities);
    // Durable command acknowledgements survive undo/redo without re-running
    // the product adapter or creating a second history step.
    const auto replayBefore = host.Repository->GetUndoArray();
    EXPECT_EQ(receipts, host.Document->ExecuteBatch(commands, edit));
    EXPECT_EQ(1, preflightCalls); EXPECT_EQ(1, stageCalls);
    EXPECT_EQ(replayBefore, host.Repository->GetUndoArray());
}

TEST(EditableToolpathHost, ARejectedBatchDoesNotPublishAnyEarlierSourceOrProductFact)
{
    HostFixture host; auto first = host.Source(); auto second = host.Source();
    second["kind"] = std::string("InvalidCommand");
    const auto before = host.Document->ExportDocument(); const auto history = host.Repository->GetUndoArray();
    SEditableToolpathHostEdit edit; int callbacks = 0;
    edit.Preflight = [&](const auto&) { ++callbacks; };
    edit.Stage = [&](ITransaction&, const auto&) { ++callbacks; };
    EXPECT_THROW(host.Document->ExecuteBatch({first, second}, edit), std::exception);
    EXPECT_EQ(0, callbacks); EXPECT_EQ(before, host.Document->ExportDocument());
    EXPECT_EQ(history, host.Repository->GetUndoArray()); EXPECT_FALSE(host.Repository->IsWriteInProgress());
}

TEST(EditableToolpathHost, ProductStagingAndCommitFailuresRollbackTheWholeBatch)
{
    for (const bool failDuringCommit : {false, true})
    {
        HostFixture host; const auto command = host.Source(); const auto path = GenerateNewUUID();
        const auto before = host.Document->ExportDocument(); const auto history = host.Repository->GetUndoArray();
        SEditableToolpathHostEdit edit;
        edit.SourceIdentities[ID(At(command, "commandId"))] = {ESourcePathIdentityMode::CreateWithId, path};
        edit.Stage = [&](ITransaction& tx, const auto&) {
            tx.AttachComponent(path, CHostOwnedPathComponent::S_ClassName,
                failDuringCommit ? PropertySet{{"MissingProperty", 1}} : PropertySet{{"Marker", 1}});
            if (!failDuringCommit) throw std::runtime_error("Product staging rejected");
        };
        EXPECT_THROW(host.Document->Execute(command, edit), std::exception);
        EXPECT_EQ(before, host.Document->ExportDocument()); EXPECT_EQ(history, host.Repository->GetUndoArray());
        EXPECT_FALSE(host.Repository->HasEntity(path)); EXPECT_FALSE(host.Repository->IsWriteInProgress());
    }
}

TEST(EditableToolpathHost, AdoptingAnExistingProductEntityKeepsItsIdentityAndUndoRestoresItsComponents)
{
    HostFixture host; const auto path = GenerateNewUUID();
    auto entity = host.Repository->CreateEntity(path); auto product = entity->AddComponent<CHostOwnedPathComponent>();
    ASSERT_TRUE(product->SetMarker(5));
    const auto command = host.Source();
    SEditableToolpathHostEdit edit;
    edit.SourceIdentities[ID(At(command, "commandId"))] = {ESourcePathIdentityMode::AdoptExisting, path};
    edit.Stage = [&](ITransaction& tx, const auto& receipts) {
        ASSERT_EQ(1u, receipts.size()); EXPECT_EQ(path, ID(At(receipts[0], "pathId")));
        tx.ModifyComponent(path, CHostOwnedPathComponent::S_ClassName, {{"Marker", 9}});
    };
    const auto receipt = host.Document->Execute(command, edit);
    EXPECT_EQ(path, ID(At(receipt, "pathId"))); EXPECT_EQ(entity, host.Repository->GetEntity(path));
    EXPECT_EQ(9, product->GetMarker()); host.Generate();
    const auto result = host.Document->Resolve(host.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(1u, result.Paths.size()); EXPECT_EQ(path, result.Paths[0].PathId);
    ASSERT_TRUE(host.Repository->Undo());
    EXPECT_EQ(entity, host.Repository->GetEntity(path)); EXPECT_EQ(product, entity->GetComponent<CHostOwnedPathComponent>());
    EXPECT_EQ(5, product->GetMarker()); EXPECT_FALSE(entity->HasComponent(CEditableDerivedComponent::S_ClassName));
    ASSERT_TRUE(host.Repository->Redo()); host.Generate();
    EXPECT_EQ(9, product->GetMarker()); ASSERT_EQ(1u, host.Document->Resolve(host.Plan).Paths.size());
    EXPECT_EQ(path, host.Document->Resolve(host.Plan).Paths[0].PathId);
}

TEST(EditableToolpathHost, ReusingACommandWithAnotherHostIdentityIsRejected)
{
    HostFixture host; const auto command = host.Source();
    SEditableToolpathHostEdit edit;
    edit.SourceIdentities[ID(At(command, "commandId"))] = {ESourcePathIdentityMode::CreateWithId, GenerateNewUUID()};
    host.Document->Execute(command, edit);
    edit.SourceIdentities[ID(At(command, "commandId"))].PathEntityId = GenerateNewUUID();
    EXPECT_THROW(host.Document->Execute(command, edit), std::invalid_argument);
}

namespace
{
struct HostCommandChain final
{
    IntentId Plan = GenerateNewUUID(), CreatePlan = GenerateNewUUID(), CreateSource = GenerateNewUUID(), AddOperation = GenerateNewUUID();
    std::vector<IntentMap> Commands;
    explicit HostCommandChain(const HostFixture& host)
    {
        IntentMap plan{{"kind", std::string("CreatePlan")}, {"commandId", CreatePlan}, {"planId", Plan}, {"name", std::string("batch plan")}};
        auto source = host.Source(); source["commandId"] = CreateSource;
        const IntentMap planReference{{"fromCommand", CreatePlan}, {"field", std::string("planId")}};
        source["planId"] = planReference;
        const IntentMap sourceReference{{"fromCommand", CreateSource}, {"field", std::string("port")}};
        IntentMap reverse{{"kind", std::string("AddOperation")}, {"commandId", AddOperation}, {"planId", planReference},
            {"typeId", std::string("icax.edit.reverse")}, {"parameters", IntentMap{}},
            {"inputs", IntentArray{IntentMap{{"slot", std::string("input/0")}, {"source", sourceReference}, {"replace", true}, {"ranges", IntentArray{}}}}}};
        Commands = {std::move(plan), std::move(source), std::move(reverse)};
    }
};
}

TEST(EditableToolpathHost, ANewPlanSourceAndOperationPreflightTogetherWithoutPublishingVirtualResults)
{
    HostFixture host; HostCommandChain chain(host); const auto path = GenerateNewUUID();
    SEditableToolpathHostEdit edit;
    edit.SourceIdentities[chain.CreateSource] = {ESourcePathIdentityMode::CreateWithId, path};
    edit.Preflight = [&](const auto& receipts) {
        EXPECT_EQ(3u, receipts.size());
        EXPECT_FALSE(host.Repository->HasEntity(chain.Plan)); EXPECT_FALSE(host.Repository->HasEntity(path));
    };
    const auto receipts = host.Document->ExecuteBatch(chain.Commands, edit);
    ASSERT_EQ(3u, receipts.size());
    EXPECT_EQ(chain.Plan, ID(At(receipts[0], "planId")));
    EXPECT_EQ("Queued", Text(receipts[1], "taskState")); EXPECT_EQ("Queued", Text(receipts[2], "taskState"));
    EXPECT_TRUE(MapAt(host.Document->ReadOperation(ID(At(receipts[1], "operationId"))), "result").empty());
    EXPECT_TRUE(MapAt(host.Document->ReadOperation(ID(At(receipts[2], "operationId"))), "result").empty());
    EXPECT_EQ(EOutputCompleteness::Incomplete, host.Document->Resolve(chain.Plan).Status);
    host.Plan = chain.Plan; host.Generate();
    const auto result = host.Document->Resolve(chain.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(1u, result.Paths.size());
    EXPECT_EQ(path, result.Paths[0].PathId); EXPECT_TRUE(Range(result.Paths[0].Value.Geometry.Segments[0]).Reversed);
}

TEST(EditableToolpathHost, AWholePlanSourceOperationChainUndoesOnceAndNeverRepeatsProductStaging)
{
    HostFixture host; HostCommandChain chain(host); const auto path = GenerateNewUUID();
    SEditableToolpathHostEdit edit;
    edit.SourceIdentities[chain.CreateSource] = {ESourcePathIdentityMode::CreateWithId, path};
    int stages = 0;
    edit.Stage = [&](ITransaction& tx, const auto& receipts) {
        ++stages; EXPECT_EQ(3u, receipts.size());
        tx.AttachComponent(path, CHostOwnedPathComponent::S_ClassName, {{"Marker", 17}});
    };
    const auto before = host.Repository->GetUndoArray();
    const auto receipts = host.Document->ExecuteBatch(chain.Commands, edit);
    ASSERT_EQ(3u, receipts.size()); EXPECT_EQ(1, stages);
    EXPECT_EQ(before.size() + 1, host.Repository->GetUndoArray().size());
    host.Plan = chain.Plan; host.Generate();
    ASSERT_TRUE(host.Repository->Undo());
    EXPECT_FALSE(host.Repository->HasEntity(chain.Plan)); EXPECT_FALSE(host.Repository->HasEntity(path));
    EXPECT_FALSE(host.Repository->HasEntity(ID(At(receipts[1], "operationId"))));
    EXPECT_FALSE(host.Repository->HasEntity(ID(At(receipts[2], "operationId"))));
    const auto undoBeforeReplay = host.Repository->GetUndoArray(), redoBeforeReplay = host.Repository->GetRedoArray();
    EXPECT_EQ(receipts, host.Document->ExecuteBatch(chain.Commands, edit)); EXPECT_EQ(1, stages);
    EXPECT_EQ(undoBeforeReplay, host.Repository->GetUndoArray()); EXPECT_EQ(redoBeforeReplay, host.Repository->GetRedoArray());
    EXPECT_FALSE(host.Repository->HasEntity(path));
    ASSERT_TRUE(host.Repository->Redo()); host.Generate();
    const auto result = host.Document->Resolve(chain.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(1u, result.Paths.size()); EXPECT_EQ(path, result.Paths[0].PathId);
    EXPECT_EQ(17, host.Repository->GetEntity(path)->GetComponent<CHostOwnedPathComponent>()->GetMarker()); EXPECT_EQ(1, stages);
}

TEST(EditableToolpathHost, ProductCannotKeepAnAcceptedReceiptAfterInvalidatingAnotherReceiptOwner)
{
    HostFixture host; const std::vector<IntentMap> commands{host.Source(10), host.Source(20)};
    const auto firstReceipt = NamedID(Key(host.Document->Id()) + "/receipt/" + Key(ID(At(commands[0], "commandId"))));
    const auto lastReceipt = NamedID(Key(host.Document->Id()) + "/receipt/" + Key(ID(At(commands[1], "commandId"))));
    const auto before = host.Document->ExportDocument();
    const auto history = host.Repository->GetUndoArray();
    SEditableToolpathHostEdit edit;
    edit.Stage = [&](ITransaction& tx, const auto&) {
        tx.AttachComponent(lastReceipt, CHostOwnedPathComponent::S_ClassName, {{"Marker", 99}});
    };
    EXPECT_THROW(host.Document->ExecuteBatch(commands, edit), std::invalid_argument);
    EXPECT_FALSE(host.Repository->HasEntity(firstReceipt)); EXPECT_FALSE(host.Repository->HasEntity(lastReceipt));
    EXPECT_EQ(before, host.Document->ExportDocument()); EXPECT_EQ(history, host.Repository->GetUndoArray());
    EXPECT_FALSE(host.Repository->IsWriteInProgress());
}

TEST(EditableToolpathHost, ConsecutiveLabelEditsInOneBatchKeepBothChanges)
{
    HostFixture host; const auto source = host.Document->Execute(host.Source()); host.Generate();
    const auto path = host.Document->Resolve(host.Plan).Paths.at(0);
    IntentMap rename{{"kind", std::string("SetLabels")}, {"commandId", GenerateNewUUID()}, {"planId", host.Plan},
        {"pathId", path.PathId}, {"expectedLabelsVersion", static_cast<unsigned long long>(path.LabelsVersion)}, {"name", std::string("renamed")}};
    auto renumber = rename; renumber["commandId"] = GenerateNewUUID(); renumber.erase("name"); renumber["userNumber"] = std::string("725");
    host.Document->ExecuteBatch({rename, renumber});
    const auto updated = host.Document->Resolve(host.Plan).Paths.at(0);
    EXPECT_EQ("renamed", updated.Name); EXPECT_EQ("725", updated.UserNumber);
    ASSERT_TRUE(host.Repository->Undo());
    const auto restored = host.Document->Resolve(host.Plan).Paths.at(0);
    EXPECT_EQ(path.Name, restored.Name); EXPECT_EQ(path.UserNumber, restored.UserNumber);
}

TEST(EditableToolpathHost, ANewSourceCanBeLabeledAndGroupedInItsCreationBatch)
{
    HostFixture host; const auto source = host.Source();
    const IntentMap reference{{"fromCommand", ID(At(source, "commandId"))}, {"field", std::string("pathId")}};
    IntentMap labels{{"kind", std::string("SetLabels")}, {"commandId", GenerateNewUUID()}, {"planId", host.Plan},
        {"pathId", reference}, {"expectedLabelsVersion", 0ull}, {"name", std::string("new labeled path")}, {"userNumber", std::string("845")}};
    IntentMap group{{"kind", std::string("Group")}, {"commandId", GenerateNewUUID()}, {"planId", host.Plan},
        {"groupId", std::string("new group")}, {"members", IntentArray{reference}}};
    host.Document->ExecuteBatch({source, labels, group}); host.Generate();
    const auto result = host.Document->Resolve(host.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete, result.Status); ASSERT_EQ(1u, result.Paths.size());
    EXPECT_EQ("new labeled path", result.Paths[0].Name); EXPECT_EQ("845", result.Paths[0].UserNumber); EXPECT_EQ("new group", result.Paths[0].GroupId);
}
