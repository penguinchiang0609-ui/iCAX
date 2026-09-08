#include "GeometryData/BRepPersistence.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourcePersistenceRegistrationCatalog.h"
#include "ProjectFile/ProjectFile.h"
#include "Database/IRepository.h"
#include "Database/IMetaRegistry.h"
#include <Windows.h>
#include <iostream>
using namespace iCAX::Resource;
using namespace iCAX::GeometryData;
int main(int argc, char** argv) {
    try {
        if (argc != 2 || !LoadLibraryA(argv[1])) throw std::runtime_error("Cannot load BRep plugin");
        // Follow the same module-scoped registration path as ProductRuntime.
        CResourcePersistentPayload saved;
        const auto sceneId = iCAX::Data::GenerateNewUUID();
        const auto projectId = iCAX::Data::GenerateNewUUID();
        const auto directory = std::filesystem::temp_directory_path() / ("brep-project-test-" + iCAX::Data::to_string(projectId));
        if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Cannot create test directory");
        const auto path = directory / "roundtrip.ictd";
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX-BREP-TEST", .ProductID="brep-test", .CurrentFormatVersion="1.0", .nCurrentFormatRevision=1});
        const std::string url = "memory://brep-persistence-integration";
        std::vector<std::uint8_t> expected;
        {
            CResourceLibrary source;
            CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(source, {argv[1]});
            auto model = std::make_shared<BRepModel>();
            model->Metadata.Name = "保存重开测试";
            BRepVertex vertex; vertex.Id = 9007199254740993ULL; vertex.Position = {1,2,3};
            model->Vertices.push_back(vertex);
            BRepShapeRef root; root.Kind = EBRepShapeKind::Vertex; root.Id = vertex.Id;
            root.Location.Matrix.Values[0][3] = 45.0; model->RootShapes.push_back(root);
            expected = Persistence::Serialize(*model);
            CResourceInfo info; info.Key = CResourceKey{url}; info.ResourceTypeID = BRepModel::kResourceTypeName;
            info.Persistence = EResourcePersistenceMode::Embedded; info.nVersion = 1; info.nSchemaVersion = 1;
            source.Set<BRepModel>(url, model, info);
            saved = source.SerializePersistentVersion({url, source.GetVersion(url)});
            auto database = iCAX::Database::GenerateRepository(sceneId, iCAX::Database::CreateMetaRegistry());
            iCAX::ProjectFile::CProjectDocumentInfo document;
            document.ProjectID = projectId; document.MainSceneID = sceneId; document.ProjectName = "BRep project reopen";
            file.Save(path, document, *database, source);
        } // Destroy the entire original resource library before restore.
        CResourceLibrary restored;
        CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(restored, {argv[1]});
        restored.RestorePersistentVersion(saved);
        auto model = restored.Get<BRepModel>(url, saved.Info.nVersion);
        if (!model || Persistence::Serialize(*model) != expected) throw std::runtime_error("Restored BRep mismatch");
        CResourceLibrary reopened;
        CResourcePersistenceRegistrationCatalog::ReplayByModulePaths(reopened, {argv[1]});
        auto database = iCAX::Database::GenerateRepository(sceneId, iCAX::Database::CreateMetaRegistry());
        const auto opened = file.Open(path, *database, reopened);
        auto diskModel = reopened.Get<BRepModel>(url, saved.Info.nVersion);
        if (opened.Info.ProjectID != projectId || !diskModel || Persistence::Serialize(*diskModel) != expected)
            throw std::runtime_error("Project file BRep mismatch");
        std::filesystem::remove(path); std::filesystem::remove(directory);
        std::cout << "Loaded plugin -> module-scoped registration -> project Save -> destroy -> project Open passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
