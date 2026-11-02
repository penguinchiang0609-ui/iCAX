TEST(CamRuntimeMachineDefinitionTest, CatalogUpgradeAddsNewDefaultsOnceWithoutRestoringDeletedOrDisabledEntries)
{
    CTestApplicationContext app;CTestProductContext product;CTestProjectContext project;CTestSceneContext scene;
    const auto folder=std::filesystem::current_path()/"output/tests/gantry-heads-20261101/catalog";std::filesystem::create_directories(folder);
    WriteTextFile(folder/"simple.urdf",R"(<robot name="catalog upgrade fixture"><link name="base"><visual><geometry><box size=".1 .1 .1"/></geometry></visual></link></robot>)");
    const auto oldID=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()),newID=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    auto& definition=const_cast<iCAX::Product::CProductDefinition&>(product.GetDefinition());auto capability=definition.Capabilities.at("machineDefinition").To<ObjectMap>();
    capability["defaultCatalogVersion"]=2ull;capability["defaults"]=VariantArray{
        ObjectMap{{"id",oldID},{"name",std::string("Earlier default deleted by user")},{"sourcePath",std::string("output/tests/gantry-heads-20261101/catalog/simple.urdf")}},
        ObjectMap{{"id",newID},{"name",std::string("New head default")},{"sourcePath",std::string("output/tests/gantry-heads-20261101/catalog/simple.urdf")},{"introducedInCatalogVersion",2ull}}};
    definition.Capabilities["machineDefinition"]=capability;
    auto settings=product.GetSettings();settings.Set("machineDefinitionsInitialized",true);settings.Set("machineDefinitions",VariantArray{});product.ReplaceSettings(settings);
    const auto list=[&]{return DecodeResponseObject(iCAX::CAM::SDO::HandleListMachineDefinitions(MakeSDOCall("MachineDefinition","List",{}),app,&product,&project,&scene)).at("definitions").To<VariantArray>();};
    auto values=list();ASSERT_EQ(1u,values.size());EXPECT_EQ(newID,values.front().To<ObjectMap>().at("id").To<std::string>());
    EXPECT_EQ(2ull,product.GetSettings().Get("machineDefinitionsCatalogVersion").To<unsigned long long>());
    iCAX::CAM::SDO::HandleSetMachineDefinitionEnabled(MakeSDOCall("MachineDefinition","SetEnabled",{{"machineDefinitionId",newID},{"enabled",false}}),app,&product,&project,&scene);
    values=list();ASSERT_EQ(1u,values.size());EXPECT_FALSE(values.front().To<ObjectMap>().at("enabled").To<bool>());
    iCAX::CAM::SDO::HandleDeleteMachineDefinition(MakeSDOCall("MachineDefinition","Delete",{{"machineDefinitionId",newID}}),app,&product,&project,&scene);
    EXPECT_TRUE(list().empty());EXPECT_TRUE(list().empty());
    // Fresh installations still receive all shipped defaults, including v1.
    CTestProductContext fresh("catalog-fresh-test");const_cast<iCAX::Product::CProductDefinition&>(fresh.GetDefinition()).Capabilities["machineDefinition"]=capability;
    const auto seeded=DecodeResponseObject(iCAX::CAM::SDO::HandleListMachineDefinitions(MakeSDOCall("MachineDefinition","List",{}),app,&fresh,&project,&scene)).at("definitions").To<VariantArray>();EXPECT_EQ(2u,seeded.size());
}
