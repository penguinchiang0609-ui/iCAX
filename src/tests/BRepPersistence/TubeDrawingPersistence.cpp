#include <ApplicationContext/ApplicationContext.h>
#include <ApplicationContext/UserDataStore.h>
#include <Product/ProductRuntime.h>
#include <Product/ProductManifest.h>
#include <Product/ProductSDO.h>
#include <Project/Project.h>
#include <Project/ProjectCatalog.h>
#include <SDO/SDOChannelRegistry.h>
#include <SDO/SDOMethod.h>
#include <Data/VariantSerializer.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <Database/IRepository.h>
#include <Database/IEntity.h>
#include <Database/ComponentBase.h>
#include <iostream>
#include <fstream>
#include <thread>
using namespace iCAX::Data;
class MemoryHistory final : public iCAX::Product::IProductDataStore {
    mutable iCAX::Product::CProductData value;
public:
    iCAX::Product::CProductData Load(const std::string&) const override { return value; }
    void Save(const std::string&, const iCAX::Product::CProductData& data) const override { value = data; }
};
ObjectMap Invoke(const std::shared_ptr<iCAX::Project::CProject>& project, const char* method, ObjectMap body = {}) {
    std::cout << "Invoke " << method << std::endl;
    static std::uint64_t sequence = 0;
    auto endpoint = project->GetMainSceneFrontendSDOEndpoint();
    iCAX::Interaction::CSDOFrame frame;
    frame.nCallID = ++sequence; frame.nMethodCode = iCAX::Interaction::MakeSDOMethodCode("TubeDesigner", method);
    frame.nKind = iCAX::Interaction::ESDOFrameKind::Request;
    frame.Payload = iCAX::Product::EncodeProductPayload(Variant(body)); endpoint.Send(frame);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& response : endpoint.Receive()) if (response.nCallID == sequence && response.nKind == iCAX::Interaction::ESDOFrameKind::Response) {
            if (response.nStatus != iCAX::Interaction::EInvocationStatus::Ok)
                throw std::runtime_error(std::string(method) + ": " + std::string(response.Payload.begin(), response.Payload.end()));
            return iCAX::Product::DecodeProductPayload(response.Payload).To<ObjectMap>();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error(std::string(method) + " timed out");
}
void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Source root required");
        const std::filesystem::path source(argv[1]);
        auto definition = iCAX::Product::LoadProductManifest((source / "apps/tube-designer/product.manifest.json").string()).Definition;
        // Test product uses real modules and template, but no UI, shared PDO or user history.
        definition.bEnablePDOHub = false;
        iCAX::Application::CApplicationDescriptor descriptor; descriptor.AppID = "drawing-persistence-test";
        iCAX::Application::CApplicationPaths paths; paths.InstallDirectory = source.string();
        auto app = std::make_shared<iCAX::Application::CApplicationContext>(descriptor, paths, PropertyBag{});
        auto runtime = std::make_shared<iCAX::Product::CProductRuntime>(definition, app,
            std::make_shared<iCAX::Interaction::CSDOChannelRegistry>(),
            std::make_shared<iCAX::Application::CProductUserDataStore>(std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"), definition.ProductID),
            std::make_shared<MemoryHistory>());
        runtime->Start();
        const auto directory = std::filesystem::temp_directory_path() / ("tube-drawing-test-" + to_string(GenerateNewUUID()));
        Expect(std::filesystem::create_directory(directory), "Cannot create test directory");
        const auto path = directory / "roundtrip.ictd";
        auto catalog = runtime->OpenProjectCatalog("test", "memory://test", "test", "memory://test");
        auto project = catalog->GetMainProject();
        Invoke(project, "GeneratePreview", {{"templateId", std::string("single-face-security-window")}});
        runtime->SaveProjectFile(project->GetProjectID(), path.string());
        const auto displaySize = std::filesystem::file_size(path);
        Expect(project->GetMainScene().Resources().GetManifest(false).empty(), "Display resources must not be embedded");
        auto reopen = [&] {
            std::cout << "Close and reopen" << std::endl;
            runtime->CloseProject(project->GetProjectID()); project.reset(); catalog.reset();
            catalog = runtime->OpenProjectFile(path.string()); project = catalog->GetMainProject();
            std::cout << "Opened, rebuilding" << std::endl;
            return Invoke(project,"List").at("tubeDesigner").To<ObjectMap>();
        };
        auto designer = reopen();
        Expect(designer.at("product").To<ObjectMap>().at("quantity").To<unsigned long long>() == 1, "Default quantity must be one");
        const auto productId = designer.at("product").To<ObjectMap>().at("entityId");
        const auto generationId = designer.at("product").To<ObjectMap>().at("activeGenerationRunId");
        designer = Invoke(project, "SetInstanceQuantity", {{"productEntityId", productId}, {"quantity", 3ull}}).at("tubeDesigner").To<ObjectMap>();
        Expect(designer.at("product").To<ObjectMap>().at("activeGenerationRunId") == generationId, "Quantity must not regenerate geometry");
        for (const auto invalid : {0.0, -1.0, 1.5, 1000001.0}) {
            bool rejected = false;
            try { Invoke(project, "SetInstanceQuantity", {{"productEntityId", productId}, {"quantity", invalid}}); }
            catch (const std::exception&) { rejected = true; }
            Expect(rejected, "Invalid quantity accepted");
        }
        Expect(!designer.at("members").To<VariantArray>().empty(), "Display did not rebuild");
        designer = Invoke(project, "Disassemble").at("tubeDesigner").To<ObjectMap>();
        runtime->SaveProjectFile(project->GetProjectID(), path.string());
        Expect(project->GetMainScene().Resources().GetManifest(false).empty(), "Unstaged machining geometry must not be embedded");
        designer = reopen();
        Expect(designer.at("product").To<ObjectMap>().at("quantity").To<unsigned long long>() == 3, "Instance quantity lost on reopen");
        // Disassembly results are intentionally transient. Rebuild them after
        // reopen before validating repeatability or staging a snapshot.
        designer = Invoke(project, "Disassemble").at("tubeDesigner").To<ObjectMap>();
        const auto parts = designer.at("parts").To<VariantArray>();
        for (const auto& value : parts) {
            const auto part = value.To<ObjectMap>();
            Expect(part.at("quantity").To<unsigned long long>() == part.at("unitQuantity").To<unsigned long long>() * 3, "Part production quantity incorrect");
        }
        const auto repeated = Invoke(project, "Disassemble").at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>();
        const auto repeatedCountMessage = std::string("Repeated disassembly changed part count (first=")
            + std::to_string(parts.size()) + ", repeated=" + std::to_string(repeated.size()) + ")";
        Expect(repeated.size() == parts.size(), repeatedCountMessage.c_str());
        for (const auto& value : repeated) {
            const auto part = value.To<ObjectMap>();
            Expect(part.at("quantity").To<unsigned long long>() == part.at("unitQuantity").To<unsigned long long>() * 3, "Disassembly multiplied quantities twice");
        }
        Expect(!parts.empty(), "Disassembly did not rebuild");
        VariantArray selection{parts.front().To<ObjectMap>().at("entityId")};
        const auto staged = Invoke(project,"StageNestingParts",{{"partEntityIds",selection}});
        const auto snapshotIds = staged.at("partEntityIds").To<VariantArray>();
        Expect(snapshotIds.front() != selection.front(), "Nesting must use a new independent part ID");
        runtime->SaveProjectFile(project->GetProjectID(), path.string());
        const auto machiningSize = std::filesystem::file_size(path);
        const auto manifest = project->GetMainScene().Resources().GetManifest(false);
        Expect(manifest.size() == 1 && manifest[0].ResourceTypeID == "geometry.brep", "Only selected machining BRep may be embedded");
        designer = reopen();
        Expect(designer.at("nestingTask").To<ObjectMap>().at("parts").To<VariantArray>().size() == 1, "Nesting task missing after reopen");
        Expect(project->GetMainScene().Resources().GetManifest(false).size() == 1, "Rebuild polluted saved resources");
        const auto nestingGroups = designer.at("nestingGroups").To<VariantArray>();
        Expect(!nestingGroups.empty(), "Independent nesting group missing after reopen");
        const auto nestedGroupParts = nestingGroups.front().To<ObjectMap>().at("parts").To<VariantArray>();
        Expect(!nestedGroupParts.empty(), "Independent nesting part missing after reopen");
        const auto nestedProperties = nestedGroupParts.front().To<ObjectMap>().at("properties").To<ObjectMap>();
        Expect(nestedProperties.at("nesting.source").To<ObjectMap>().at("kind").To<std::string>() == "product-disassembly",
            "Source provenance was not projected from its component slice");
        const auto first = parts.front().To<ObjectMap>();
        const auto profile = first.at("profile").To<ObjectMap>();
        ObjectMap section;
        for (const auto* key : {"id", "kind", "packageVersion", "width", "depth", "diameter", "wallThickness",
            "cornerRadius", "hollow", "contentDigest", "parameters", "contours", "specification"}) {
            if (profile.contains(key)) section[key] = profile.at(key);
        }
        const auto profileKey = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(section));
        ObjectMap nestingRequest{{"parts", VariantArray{ObjectMap{{"partEntityId", snapshotIds.front()}, {"profileKey", profileKey}, {"quantity", first.at("quantity")}}}},
            {"stocks", VariantArray{ObjectMap{{"id", std::string("test-stock")}, {"profileKey", profileKey}, {"length", 6000.0}, {"quantity", -1}}}},
            {"parameters", ObjectMap{{"partGap", 2.0}}}};
        const auto nesting = Invoke(project, "Nest", nestingRequest);
        Expect(!nesting.at("plans").To<VariantArray>().empty(), "Nesting returned no plan");
        std::size_t placed = 0;
        for (const auto& plan : nesting.at("plans").To<VariantArray>()) placed += plan.To<ObjectMap>().at("placements").To<VariantArray>().size();
        Expect(placed == first.at("quantity").To<unsigned long long>(), "Nesting did not multiply production quantity");
        const auto exportDirectory = directory / "exports";
        // Export the nesting-owned snapshot. The original disassembly IDs are
        // transient input records and are intentionally not exportable once
        // staging has detached them from the product lifecycle.
        const auto exported = Invoke(project, "ExportSelected", {{"targetDirectory", exportDirectory.string()}, {"partEntityIds", snapshotIds}});
        const auto exportedGroup = exported.at("exportedGroups").To<VariantArray>().front().To<ObjectMap>();
        Expect(exportedGroup.at("directory").To<std::string>().ends_with("--XX--3"), "Export directory quantity suffix missing");
        const auto workbookPath = exported.at("partListFile").To<std::string>();
        std::ifstream workbook(std::filesystem::path(std::u8string(workbookPath.begin(), workbookPath.end())), std::ios::binary);
        const std::string workbookContent{std::istreambuf_iterator<char>(workbook), {}};
        Expect(workbookContent.find("--XX--3") != std::string::npos, "Workbook link missing quantity directory");
        Expect(workbookContent.find("r=\"J3\" s=\"5\"><v>" + std::to_string(first.at("quantity").To<unsigned long long>()) + "</v>") != std::string::npos, "Workbook production quantity incorrect");
        // Staged/nesting copies are independent persisted parts (nil ProductID)
        // and must export through the same STEP + Excel entry point.
        const auto independentExportDirectory = directory / "independent-exports";
        const auto independentExported = Invoke(project, "ExportSelected", {{"targetDirectory", independentExportDirectory.string()}, {"partEntityIds", snapshotIds}});
        Expect(independentExported.at("exportedCount").To<unsigned long long>() == 1, "Independent nesting part was not exported");
        const auto independentWorkbook = independentExported.at("partListFile").To<std::string>();
        Expect(std::filesystem::exists(std::filesystem::path(std::u8string(independentWorkbook.begin(), independentWorkbook.end()))), "Independent export workbook missing");
        designer = Invoke(project, "SetInstanceQuantity", {{"productEntityId", productId}, {"quantity", 2ull}}).at("tubeDesigner").To<ObjectMap>();
        Expect(designer.at("nestingTask").To<ObjectMap>().at("result").To<ObjectMap>() == nesting, "Source quantity changed independent nesting result");
        Expect(designer.at("parts").To<VariantArray>().front().To<ObjectMap>().at("quantity").To<unsigned long long>() == first.at("unitQuantity").To<unsigned long long>() * 2, "Quantity change compounded old total");
        Expect(Invoke(project, "Nest", nestingRequest).at("plans") == nesting.at("plans"), "Source quantity changed independent nesting demand");
        runtime->SaveProjectFile(project->GetProjectID(), path.string());
        designer = reopen();
        Expect(designer.at("product").To<ObjectMap>().at("quantity").To<unsigned long long>() == 2, "Modified quantity was not persisted");
        // Deleting a nesting copy must not touch the source or its manufacturing parts.
        const auto secondStage = Invoke(project, "StageNestingParts", {{"partEntityIds", selection}});
        const auto secondIds = secondStage.at("partEntityIds").To<VariantArray>();
        designer = Invoke(project, "DeleteNestingParts", {{"partEntityIds", secondIds}}).at("tubeDesigner").To<ObjectMap>();
        Expect(designer.at("parts").To<VariantArray>().size() == parts.size(), "Deleting nesting copy changed source parts");
        Expect(designer.at("product").To<ObjectMap>().at("quantity").To<unsigned long long>() == 2, "Deleting nesting copy changed source instance");
        Expect(designer.at("nestingGroups").To<VariantArray>().size() == 1, "Deleting one batch changed another batch");
        runtime->Stop(); project.reset(); catalog.reset(); runtime.reset();
        for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.is_regular_file()) std::filesystem::remove(entry.path());
        std::cout << "Export verification files: " << exportDirectory << std::endl;
        std::cout << "Real template generate/save/reopen/disassemble/stage/reopen passed; display=" << displaySize << " bytes; staged=" << machiningSize << " bytes\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
