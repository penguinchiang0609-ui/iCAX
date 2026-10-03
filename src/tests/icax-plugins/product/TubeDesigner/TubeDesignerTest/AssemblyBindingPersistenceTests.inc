TEST(ProductAssemblyBindingsPersistence, ProductChoicesAndAppliedSnapshotStayIndependentAfterReopen)
{
    using iCAX::TubeDesigner::CProductInstanceComponent;
    using iCAX::TubeDesigner::CGenerationRunComponent;
    Scene scene;
    const auto productID = iCAX::Data::GenerateNewUUID();
    const auto runID = iCAX::Data::GenerateNewUUID();
    scene.Database().BeginLoadBaseline();
    const auto product = scene.Database().CreateEntity(productID)
        ->AddComponent<CProductInstanceComponent>();
    const auto run = scene.Database().CreateEntity(runID)
        ->AddComponent<CGenerationRunComponent>();
    ASSERT_TRUE(product);
    ASSERT_TRUE(run);
    EXPECT_TRUE(product->GetAssemblyBindings().empty());
    EXPECT_TRUE(run->GetAppliedAssemblyBindings().empty());
    EXPECT_TRUE(run->GetAssemblyManufacturingModel().empty());
    const ObjectMap parameters{{"width", 1200.0}, {"height", 900.0}};
    const ObjectMap manufacturingModel{{"schema",std::string("icax.neutral-model")},
        {"parameters",parameters},{"geometry",VariantArray{ObjectMap{{"key",std::string("original-stock")}}}}};
    const ObjectMap bindings{{"binding-1", ObjectMap{
        {"schemaVersion", 1ull}, {"generationRunId", iCAX::Data::to_string(runID)},
        {"templateId", std::string("mechanical-fastener")},
        {"templateDigest", std::string("frozen-assembly-package")},
        {"participants", VariantArray{ObjectMap{
            {"role", std::string("partA")}, {"itemKey", std::string("frame.left")},
            {"anchor", ObjectMap{{"kind", std::string("side")},
                {"station", 137.5}, {"face", std::string("top")}}}}}},
        {"features", VariantArray{ObjectMap{
            {"id", std::string("hole-a")},
            {"toolSnapshot", ObjectMap{{"resolution", std::string("frozen")},
                {"digest", std::string("original-tool")}}},
            {"frozenCut", ObjectMap{{"url", std::string("scene://assembly/cut-1")},
                {"version", 3ull}, {"coordinateSpace", std::string("part")}}}}}}
    }}};
    ASSERT_TRUE(product->SetParameters(parameters));
    ASSERT_TRUE(product->SetActiveGenerationRunID(runID));
    ASSERT_TRUE(product->SetAssemblyBindings(bindings));
    ASSERT_TRUE(run->SetProductID(productID));
    ASSERT_TRUE(run->SetAppliedAssemblyBindings(bindings));
    ASSERT_TRUE(run->SetAssemblyManufacturingModel(manufacturingModel));
    scene.Database().EndLoadBaseline();

    const auto directory = std::filesystem::current_path() / "tmp/product-assembly-bindings";
    std::filesystem::create_directories(directory);
    const auto path = directory / "component-snapshots.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1",
        .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;
    info.MainSceneID=scene.GetSceneID();
    info.ProjectName="产品装配加工快照";
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto restoredProduct = std::dynamic_pointer_cast<CProductInstanceComponent>(
        reopened.Database().GetEntity(productID)->GetComponent(CProductInstanceComponent::S_ClassName));
    const auto restoredRun = std::dynamic_pointer_cast<CGenerationRunComponent>(
        reopened.Database().GetEntity(runID)->GetComponent(CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(restoredProduct);
    ASSERT_TRUE(restoredRun);
    EXPECT_EQ(parameters, restoredProduct->GetParameters());
    EXPECT_EQ(bindings, restoredProduct->GetAssemblyBindings());
    EXPECT_EQ(bindings, restoredRun->GetAppliedAssemblyBindings());
    EXPECT_EQ(manufacturingModel, restoredRun->GetAssemblyManufacturingModel());

    auto undo = reopened.Database().BeginUndoCommand("Change assembly binding draft");
    auto& transaction = reopened.Database().BeginTransaction("Remove current binding choice");
    transaction.ModifyComponent(productID, CProductInstanceComponent::S_ClassName,
        {{CProductInstanceComponent::PropertyName_AssemblyBindings, ObjectMap{}}});
    std::string error;
    ASSERT_TRUE(reopened.Database().CommitTransaction(transaction, error)) << error;
    undo->End();
    EXPECT_TRUE(restoredProduct->GetAssemblyBindings().empty());
    EXPECT_EQ(parameters, restoredProduct->GetParameters());
    EXPECT_EQ(bindings, restoredRun->GetAppliedAssemblyBindings());
    ASSERT_TRUE(reopened.Database().Undo());
    EXPECT_EQ(bindings, restoredProduct->GetAssemblyBindings());
    EXPECT_EQ(bindings, restoredRun->GetAppliedAssemblyBindings());
}
