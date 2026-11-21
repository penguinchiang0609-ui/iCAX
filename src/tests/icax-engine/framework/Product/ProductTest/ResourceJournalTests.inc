namespace
{
    void JournalSetBlob(iCAX::Resource::CResourceLibrary& resources, const std::string& url,
        uint64_t version, uint8_t value)
    {
        auto blob = std::make_shared<iCAX::Resource::CBinaryResource>();
        blob->Content = {value, 2, 3}; blob->nVersion = version;
        iCAX::Resource::CResourceInfo info;
        info.ResourceTypeID = iCAX::Resource::CBinaryResource::kResourceTypeName;
        info.Persistence = iCAX::Resource::EResourcePersistenceMode::Embedded;
        info.nVersion = version; info.nSchemaVersion = 1;
        resources.Set(url, blob, info);
    }

    bool JournalCreateEntity(iCAX::Database::IRepository& db, const iCAX::Data::uuid& id)
    {
        auto& transaction = db.BeginTransaction("Journal resource edit");
        transaction.CreateEntity(id);
        std::string error;
        return db.CommitTransaction(transaction, error);
    }
}

TEST(ProductResourceJournalTest, FreshRuntimeRestoresAllResourceVersionsAndPlainECDoesNotRewriteBundle)
{
    const auto path = MakeTempProjectPath();
    auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Resource recovery", path.string(), "Resource recovery", path.string());
    auto project = catalog->GetMainProject();
    AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); auto& db = project->MainSceneDatabase(); auto& resources = project->MainSceneResources();
    const auto url = resources.MakeNamedResourceURL("journal/blob");
    const auto first = iCAX::Data::GenerateNewUUID(), second = iCAX::Data::GenerateNewUUID();
    // Both versions are created before the first checkpoint. A batch can
    // contain intermediate resource references as well as the final one.
    auto& batch = db.BeginTransaction("Two versions before the first checkpoint");
    JournalSetBlob(resources, url, 1, 11); batch.CreateEntity(first);
    JournalSetBlob(resources, url, 2, 22); ASSERT_TRUE(db.CommitTransaction(batch));
    const auto checkpoint = path.string() + ".log.resources";
    ASSERT_TRUE(std::filesystem::exists(checkpoint));
    const auto bytes = AutoSaveReadBytes(checkpoint);
    const auto stamp = std::filesystem::last_write_time(checkpoint);
    ASSERT_TRUE(JournalCreateEntity(db, iCAX::Data::GenerateNewUUID()));
    EXPECT_EQ(bytes, AutoSaveReadBytes(checkpoint)); EXPECT_EQ(stamp, std::filesystem::last_write_time(checkpoint));
    // A full archive contains the current version, while the pool can also
    // retain earlier Undo versions. The next EC edit must checkpoint those
    // versions even though no new resource was added after Save.
    runtime->SaveProjectFile(project->GetProjectID(), path.string());
    EXPECT_FALSE(std::filesystem::exists(checkpoint));
    ASSERT_TRUE(JournalCreateEntity(db, iCAX::Data::GenerateNewUUID()));
    ASSERT_TRUE(std::filesystem::exists(checkpoint));
    JournalSetBlob(resources, url, 3, 33); ASSERT_TRUE(JournalCreateEntity(db, second));
    // Neither current EC nor the live pool retains the previous URL now.
    // Journal closure must still carry its earlier versions for replay.
    resources.Clear(); ASSERT_TRUE(JournalCreateEntity(db, iCAX::Data::GenerateNewUUID()));
    ASSERT_TRUE(runtime->CloseProjectCatalog(catalog->GetCatalogID())); runtime->Stop();
    project.reset(); catalog.reset(); runtime.reset();
    runtime = MakeRuntime(); runtime->Start(); catalog = runtime->OpenProjectFile(path.string());
    project = catalog->GetMainProject(); project->Stop();
    EXPECT_TRUE(project->MainSceneDatabase().HasEntity(first)); EXPECT_TRUE(project->MainSceneDatabase().HasEntity(second));
    auto v1 = project->MainSceneResources().Get<iCAX::Resource::CBinaryResource>(url, 1);
    auto v2 = project->MainSceneResources().Get<iCAX::Resource::CBinaryResource>(url, 2);
    auto v3 = project->MainSceneResources().Get<iCAX::Resource::CBinaryResource>(url, 3);
    ASSERT_TRUE(v1); ASSERT_TRUE(v2); ASSERT_TRUE(v3);
    EXPECT_EQ(11, v1->Content[0]); EXPECT_EQ(22, v2->Content[0]); EXPECT_EQ(33, v3->Content[0]);
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path);
}

TEST(ProductResourceJournalTest, CheckpointIOFailureRollsBackEntityAndDoesNotAppendAnyECRecord)
{
    const auto path = MakeTempProjectPath(); auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("I/O failure", path.string(), "I/O failure", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); auto& db = project->MainSceneDatabase();
    const auto journal = path.string() + ".log", checkpoint = journal + ".resources";
    ASSERT_TRUE(db.FlushOperationLog()); const auto before = AutoSaveReadBytes(journal);
    const auto id = iCAX::Data::GenerateNewUUID();
    JournalSetBlob(project->MainSceneResources(), project->MainSceneResources().MakeNamedResourceURL("journal/failing"), 1, 42);
    ASSERT_TRUE(std::filesystem::create_directory(checkpoint));
    EXPECT_FALSE(JournalCreateEntity(db, id)); EXPECT_FALSE(db.HasEntity(id));
    ASSERT_TRUE(db.FlushOperationLog()); EXPECT_EQ(before, AutoSaveReadBytes(journal));
    ASSERT_TRUE(std::filesystem::remove(checkpoint));
    EXPECT_TRUE(JournalCreateEntity(db, id)); EXPECT_TRUE(db.HasEntity(id));
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path);
}

TEST(ProductResourceJournalTest, WrongJournalIdentityIsRejectedBeforeRegisteringRecoveredProject)
{
    const auto path = MakeTempProjectPath(); auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Identity", path.string(), "Identity", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); const auto url = project->MainSceneResources().MakeNamedResourceURL("journal/identity");
    JournalSetBlob(project->MainSceneResources(), url, 1, 42);
    ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    const auto checkpoint = path.string() + ".log.resources";
    auto document = iCAX::ProjectFile::CProjectFileCodec::Read(checkpoint).Document;
    document.Info.ProjectSettings["journalId"] = std::string("another-session");
    iCAX::ProjectFile::CProjectFileCodec::WriteAtomic(checkpoint, document, iCAX::ProjectFile::EProjectFileEncoding::Binary);
    runtime->CloseProjectCatalog(catalog->GetCatalogID()); runtime->Stop(); project.reset(); catalog.reset(); runtime.reset();
    runtime = MakeRuntime(); runtime->Start();
    EXPECT_THROW(runtime->OpenProjectFile(path.string()), std::runtime_error);
    EXPECT_TRUE(runtime->GetProjectCatalogs().empty()); runtime->Stop(); RemoveProjectFiles(path);
}

TEST(ProductResourceJournalTest, TruncateRetiresOnlyCurrentGenerationAndDiscardRestoresSavedSnapshot)
{
    const auto path = MakeTempProjectPath(); auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Discard", path.string(), "Discard", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); const auto url = project->MainSceneResources().MakeNamedResourceURL("journal/discard");
    JournalSetBlob(project->MainSceneResources(), url, 1, 42);
    ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    const auto checkpoint = path.string() + ".log.resources";
    const auto oldID = iCAX::ProjectFile::CProjectFileCodec::Read(checkpoint).Document.Info.ProjectSettings.at("journalId").To<std::string>();
    project->OpenQuickSaveLog(true, "ICAX_ROBOT.QuickSaveLog", 1);
    EXPECT_FALSE(std::filesystem::exists(checkpoint));
    JournalSetBlob(project->MainSceneResources(), url, 2, 43);
    const auto unsaved = iCAX::Data::GenerateNewUUID();
    ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), unsaved));
    const auto newID = iCAX::ProjectFile::CProjectFileCodec::Read(checkpoint).Document.Info.ProjectSettings.at("journalId").To<std::string>();
    EXPECT_NE(oldID, newID);
    ASSERT_TRUE(runtime->CloseProjectCatalog(catalog->GetCatalogID(), true));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".log"));
    EXPECT_FALSE(std::filesystem::exists(checkpoint));
    catalog = runtime->OpenProjectFile(path.string()); project = catalog->GetMainProject(); project->Stop();
    EXPECT_FALSE(project->MainSceneDatabase().HasEntity(unsaved));
    EXPECT_FALSE(project->MainSceneResources().Contains(url));
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path);
}

TEST(ProductResourceJournalTest, RealDiscardFileLockLeavesLiveProjectAndRestoresItsJournal)
{
    const auto path = MakeTempProjectPath(); auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Locked discard", path.string(), "Locked discard", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); const auto url = project->MainSceneResources().MakeNamedResourceURL("journal/locked-discard");
    JournalSetBlob(project->MainSceneResources(), url, 1, 42);
    const auto id = iCAX::Data::GenerateNewUUID(); ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), id)); project->Start();
    const auto checkpoint = std::filesystem::path(path.string() + ".log.resources");
    const auto journalBefore = AutoSaveReadBytes(path.string() + ".log"), checkpointBefore = AutoSaveReadBytes(checkpoint);
    const HANDLE blocker = CreateFileW(checkpoint.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, blocker);
    EXPECT_THROW(runtime->CloseProjectCatalog(catalog->GetCatalogID(), true), std::filesystem::filesystem_error);
    EXPECT_EQ(1u, runtime->GetProjectCatalogs().size()); EXPECT_TRUE(project->IsRunning()); EXPECT_TRUE(project->IsQuickSaveLogOpen());
    EXPECT_EQ(journalBefore, AutoSaveReadBytes(path.string() + ".log")); EXPECT_EQ(checkpointBefore, AutoSaveReadBytes(checkpoint));
    CloseHandle(blocker);
    project->Stop(); EXPECT_TRUE(project->MainSceneDatabase().HasEntity(id));
    EXPECT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path);
}

TEST(ProductResourceJournalTest, SaveAsRejectsAnotherJournalBeforeOverwritingTargetArchive)
{
    const auto sourcePath = MakeTempProjectPath(), targetPath = MakeTempProjectPath();
    auto runtime = MakeRuntime(); runtime->Start();
    auto source = runtime->OpenProjectCatalog("Source", sourcePath.string(), "Source", sourcePath.string());
    auto target = runtime->OpenProjectCatalog("Target", targetPath.string(), "Target", targetPath.string());
    AutoSaveCall(runtime, source->GetMainProject(), iCAX::Project::kProjectSaveMethodCode);
    AutoSaveCall(runtime, target->GetMainProject(), iCAX::Project::kProjectSaveMethodCode);
    const auto archive = AutoSaveReadBytes(targetPath), journal = AutoSaveReadBytes(targetPath.string() + ".log");
    EXPECT_THROW(runtime->SaveProjectFile(source->GetMainProject()->GetProjectID(), targetPath.string()), std::runtime_error);
    EXPECT_EQ(archive, AutoSaveReadBytes(targetPath)); EXPECT_EQ(journal, AutoSaveReadBytes(targetPath.string() + ".log"));
    EXPECT_EQ(sourcePath.string(), source->GetMainProject()->GetProjectPath());
    runtime->CloseProjectCatalog(source->GetCatalogID(), true); runtime->CloseProjectCatalog(target->GetCatalogID(), true);
    runtime->Stop(); RemoveProjectFiles(sourcePath); RemoveProjectFiles(targetPath);
}

TEST(ProductResourceJournalTest, SaveAsLockedSourceBundleRestoresTheOriginalPairAndRemainsEditable)
{
    const auto path = MakeTempProjectPath(), newPath = MakeTempProjectPath();
    auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Source", path.string(), "Source", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); const auto url = project->MainSceneResources().MakeNamedResourceURL("journal/locked-save-as");
    JournalSetBlob(project->MainSceneResources(), url, 1, 42);
    ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID())); project->Start();
    const auto checkpoint = std::filesystem::path(path.string() + ".log.resources");
    const auto oldJournal = AutoSaveReadBytes(path.string() + ".log"), oldBundle = AutoSaveReadBytes(checkpoint);
    const HANDLE blocker = CreateFileW(checkpoint.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); ASSERT_NE(INVALID_HANDLE_VALUE, blocker);
    EXPECT_THROW(runtime->SaveProjectFile(project->GetProjectID(), newPath.string()), std::filesystem::filesystem_error);
    CloseHandle(blocker);
    EXPECT_EQ(path.string(), project->GetProjectPath()); EXPECT_TRUE(project->IsQuickSaveLogOpen()); EXPECT_TRUE(project->IsRunning());
    EXPECT_EQ(oldJournal, AutoSaveReadBytes(path.string() + ".log")); EXPECT_EQ(oldBundle, AutoSaveReadBytes(checkpoint));
    project->Stop(); EXPECT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path); RemoveProjectFiles(newPath);
}

TEST(ProductResourceJournalTest, OrphanedOldBundleIsIsolatedBeforeANewJournalAndNeverReplayed)
{
    const auto path = MakeTempProjectPath(); auto runtime = MakeRuntime(); runtime->Start();
    auto catalog = runtime->OpenProjectCatalog("Orphan", path.string(), "Orphan", path.string());
    auto project = catalog->GetMainProject(); AutoSaveCall(runtime, project, iCAX::Project::kProjectSaveMethodCode);
    project->Stop(); const auto url = project->MainSceneResources().MakeNamedResourceURL("journal/orphan");
    JournalSetBlob(project->MainSceneResources(), url, 1, 42);
    ASSERT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    const auto checkpoint = path.string() + ".log.resources";
    const auto retired = AutoSaveReadBytes(checkpoint);
    ASSERT_TRUE(runtime->CloseProjectCatalog(catalog->GetCatalogID(), true));
    // Reproduce a historical leftover without deleting/suppressing a live
    // journal. Its old generation must not prevent the new scene editing.
    { std::ofstream stream(checkpoint, std::ios::binary); stream.write(retired.data(), retired.size()); }
    catalog = runtime->OpenProjectFile(path.string()); project = catalog->GetMainProject(); project->Stop();
    EXPECT_FALSE(project->MainSceneResources().Contains(url)); EXPECT_FALSE(std::filesystem::exists(checkpoint));
    JournalSetBlob(project->MainSceneResources(), url, 1, 43);
    EXPECT_TRUE(JournalCreateEntity(project->MainSceneDatabase(), iCAX::Data::GenerateNewUUID()));
    EXPECT_TRUE(std::filesystem::exists(checkpoint));
    runtime->CloseProjectCatalog(catalog->GetCatalogID(), true); runtime->Stop(); RemoveProjectFiles(path);
    const auto prefix = path.filename().string() + ".log.resources.orphan-";
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
        if (entry.path().filename().string().starts_with(prefix)) std::filesystem::remove(entry.path());
}
