// Exercise the public SDO boundary, which validates the authoritative saved
// profile key separately from the lower-level STEP/workbook writer.
namespace nesting_profile_key_export_acceptance {

std::filesystem::path testDirectory() {
    const auto root=std::filesystem::temp_directory_path()/
        ("icax-nesting-profile-key-"+iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
    if(!std::filesystem::create_directory(root))
        throw std::runtime_error("Cannot allocate nesting export acceptance directory");
    return root;
}

std::filesystem::path utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(text.data()),text.size()));
}

std::string pathText(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read nesting export acceptance artifact");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}

ObjectMap partSnapshot(const ObjectMap& response,const std::string& id) {
    const auto designer=response.at("tubeDesigner").To<ObjectMap>();
    for(const auto& value:designer.at("nestingGroups").To<VariantArray>()) {
        const auto group=value.To<ObjectMap>();
        for(const auto& item:group.at("parts").To<VariantArray>()) {
            const auto candidate=item.To<ObjectMap>();
            if(candidate.at("entityId").To<std::string>()==id)return candidate;
        }
    }
    throw std::runtime_error("Nesting export acceptance part is missing from the UI snapshot");
}

std::string profileKey(const ObjectMap& profile) {
    // The native snapshot owns this identity. Client labels/serialization must
    // not supply a second grouping rule or replace the authoritative curves.
    return profile.at("sectionIdentity").To<std::string>();
}

struct Fixture {
    Scene scene;
    std::string partId,key,revision,planId;
    ObjectMap plan;
    std::filesystem::path root=testDirectory();

    Fixture() {
        const auto added=invoke(scene,"AddNestingStandardPart",ObjectMap{
            {"profile",dxfProfile()},{"length",1000.0},{"quantity",2ull}});
        partId=added.at("partEntityId").To<std::string>();
        key=profileKey(partSnapshot(added,partId).at("profile").To<ObjectMap>());
        if(key.size()<=160 || key.size()>65536)
            throw std::runtime_error("Fixture must contain a real long serialized section key");
        const auto solved=invoke(scene,"Nest",ObjectMap{
            {"parts",VariantArray{ObjectMap{{"partEntityId",partId},{"profileKey",key},{"quantity",2ull}}}},
            {"stocks",VariantArray{ObjectMap{{"id",std::string("stock-key-export")},
                {"profileKey",key},{"length",6000.0},{"quantity",1ll}}}},
            {"parameters",ObjectMap{{"partGap",3.0}}}});
        revision=solved.at("revision").To<std::string>();
        const auto catalog=invoke(scene,"ReadNestingResult",ObjectMap{{"revision",revision}});
        const auto rows=catalog.at("rows").To<VariantArray>();
        if(rows.size()!=1)throw std::runtime_error("Nesting export fixture must use exactly one stock");
        planId=rows.front().To<VariantArray>().front().To<std::string>();
        plan=invoke(scene,"ReadNestingResult",ObjectMap{{"revision",revision},{"planId",planId}});
        if(plan.at("profileKey").To<std::string>()!=key)
            throw std::runtime_error("Nesting solve changed the serialized profile identity");
    }

    ObjectMap exportRequest(VariantArray selected) const {
        return {{"targetDirectory",pathText(root)},{"revision",revision},{"plans",selected}};
    }

    ObjectMap nestingRequest(const std::string& submittedKey) const {
        return {{"parts",VariantArray{ObjectMap{{"partEntityId",partId},{"profileKey",submittedKey},
                    {"sectionIdentity",submittedKey},{"quantity",2ull}}}},
            {"stocks",VariantArray{ObjectMap{{"id",std::string("stock-key-export")},
                    {"profileKey",submittedKey},{"length",6000.0},{"quantity",1ll}}}},
            {"parameters",ObjectMap{{"partGap",3.0}}}};
    }
};

void checkFiles(const ObjectMap& response,const TopoDS_Shape& source) {
    EXPECT_EQ(response.at("exportedCount").To<unsigned long long>(),1ull);
    const auto files=response.at("exportedFiles").To<VariantArray>();
    ASSERT_EQ(files.size(),1u);
    const auto step=utf8Path(files.front().To<std::string>());
    ASSERT_TRUE(std::filesystem::is_regular_file(step));
    STEPControl_Reader reader;
    ASSERT_EQ(reader.ReadFile(pathText(step).c_str()),IFSelect_RetDone);
    ASSERT_GT(reader.TransferRoots(),0);
    const auto result=reader.OneShape();
    ASSERT_FALSE(result.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(result).IsValid());
    EXPECT_NEAR(volume(result),2.0*volume(source),1e-3);
    int solids=0;
    for(TopExp_Explorer explorer(result,TopAbs_SOLID);explorer.More();explorer.Next())++solids;
    EXPECT_EQ(solids,2);
    const auto workbook=utf8Path(response.at("partListFile").To<std::string>());
    const auto content=readBytes(workbook);
    EXPECT_EQ(content.substr(0,4),std::string("PK\x03\x04",4));
    EXPECT_NE(content.find("TubeDesigner 下料排样清单"),std::string::npos);
    EXPECT_NE(content.find("stock-001.step"),std::string::npos);
}

TEST(TubeDesignerNestingExportSDO, LongSerializedProfileKeyExportsAfterProjectSaveAndReopen) {
    Fixture fixture;
    ASSERT_GT(fixture.key.size(),160u);
    EXPECT_NE(fixture.key.find("\"contours\""),std::string::npos);
    ASSERT_EQ(fixture.plan.at("placements").To<VariantArray>().size(),2u);
    const auto source=shape(fixture.scene,part(fixture.scene,fixture.partId));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=fixture.scene.project;info.MainSceneID=fixture.scene.GetSceneID();
    info.ProjectName="长截面标识排样导出回归";
    const auto projectFile=fixture.root/"saved-nesting.ictd";
    file.Save(projectFile,info,fixture.scene.Database(),fixture.scene.Resources());
    Scene reopened(fixture.scene.project,fixture.scene.GetSceneID(),false);
    file.Open(projectFile,reopened.Database(),reopened.Resources());
    const auto restored=invoke(reopened,"ReadNestingResult",ObjectMap{
        {"revision",fixture.revision},{"planId",fixture.planId}});
    EXPECT_EQ(restored.at("profileKey").To<std::string>(),fixture.key);
    // Both public request forms validate against the same saved source state.
    checkFiles(invoke(reopened,"ExportNesting",fixture.exportRequest(VariantArray{fixture.planId})),source);
    checkFiles(invoke(reopened,"ExportNesting",fixture.exportRequest(VariantArray{restored})),source);
    std::cout<<"[nesting-profile-key-export] bytes="<<fixture.key.size()
        <<" project="<<projectFile.string()<<std::endl;
}

TEST(TubeDesignerNestingExportSDO, LongProfileKeyStillRejectsChangedSnapshotsAndSourceSections) {
    Fixture fixture;
    const auto rejectsFor=[&](const ObjectMap& request,const std::string& reason) {
        try {
            invoke(fixture.scene,"ExportNesting",request);
            FAIL()<<"A changed nesting export was accepted";
        } catch(const std::exception& error) {
            EXPECT_NE(std::string(error.what()).find(reason),std::string::npos)<<error.what();
            EXPECT_EQ(std::string(error.what()).find("field is too long"),std::string::npos);
        }
    };
    const auto unchanged=invoke(fixture.scene,"ReadNestingResult",ObjectMap{
        {"revision",fixture.revision},{"planId",fixture.planId}});
    auto submitted=fixture.plan;
    submitted["profileKey"]=fixture.key+" ";
    submitted["sectionIdentity"]=fixture.key+" ";
    rejectsFor(fixture.exportRequest(VariantArray{submitted}),"排样结果或零件数据已改变");
    EXPECT_TRUE(std::filesystem::is_empty(fixture.root));
    try {
        invoke(fixture.scene,"Nest",fixture.nestingRequest(fixture.key+"x"));
        FAIL()<<"A client-forged nesting identity was accepted";
    } catch(const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("截面数据已改变或分组不匹配"),std::string::npos)<<error.what();
    }
    EXPECT_EQ(invoke(fixture.scene,"ReadNestingResult",ObjectMap{
        {"revision",fixture.revision},{"planId",fixture.planId}}),unchanged);
    const auto profile=std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeProfileComponent>(
        part(fixture.scene,fixture.partId)->GetEntity()->GetComponent(
            iCAX::TubeDesigner::CTubeProfileComponent::S_ClassName));
    ASSERT_TRUE(profile);
    const auto originalParameters=profile->GetParameters();
    auto changedParameters=originalParameters;
    changedParameters["width"]=100.0;
    ASSERT_TRUE(profile->SetParameters(changedParameters));
    rejectsFor(fixture.exportRequest(VariantArray{fixture.planId}),"零件几何或截面已变化");
    EXPECT_TRUE(std::filesystem::is_empty(fixture.root));
    ASSERT_TRUE(profile->SetParameters(originalParameters));
    // Change actual closed section geometry while keeping the declared width,
    // source labels and the previously submitted identity unchanged.
    const auto originalContours=profile->GetContours();
    const auto differentSection=systemProfile("rect",{{"width",127.0},{"depth",83.0},
        {"wallThickness",2.0},{"cornerRadius",4.0},{"innerRadius",2.0}});
    ASSERT_NE(originalContours,ObjectMap({{"loops",differentSection.at("contours")}}));
    ASSERT_TRUE(profile->SetContours(ObjectMap{{"loops",differentSection.at("contours")}}));
    rejectsFor(fixture.exportRequest(VariantArray{fixture.planId}),"零件几何或截面已变化");
    EXPECT_TRUE(std::filesystem::is_empty(fixture.root));
    EXPECT_THROW(invoke(fixture.scene,"Nest",fixture.nestingRequest(fixture.key)),std::exception);
    ASSERT_TRUE(profile->SetContours(originalContours));
    // A different real BRep with the same profile must still invalidate the
    // saved resource/version authority, independently of section grouping.
    const auto root=std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
        fixture.scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName));
    ASSERT_TRUE(root);
    const auto savedTask=root->GetNestingTask();
    const auto changed=invoke(fixture.scene,"AddNestingStandardPart",ObjectMap{
        {"profile",dxfProfile()},{"length",1100.0},{"quantity",2ull}});
    // Adding a catalog part normally invalidates the task. Restore the saved
    // task to isolate its source-geometry check from this ordinary invalidation.
    ASSERT_TRUE(root->SetNestingTask(savedTask));
    const auto replacement=part(fixture.scene,changed.at("partEntityId").To<std::string>());
    const auto original=part(fixture.scene,fixture.partId);
    const auto originalResource=original->GetManufacturingGeometryResourceID();
    const auto originalVersion=original->GetManufacturingGeometryResourceVersion();
    ASSERT_NE(originalResource,replacement->GetManufacturingGeometryResourceID());
    EXPECT_GT(volume(shape(fixture.scene,replacement)),volume(shape(fixture.scene,original)));
    ASSERT_TRUE(original->SetManufacturingGeometryResourceID(replacement->GetManufacturingGeometryResourceID()));
    ASSERT_TRUE(original->SetManufacturingGeometryResourceVersion(replacement->GetManufacturingGeometryResourceVersion()));
    rejectsFor(fixture.exportRequest(VariantArray{fixture.planId}),"零件几何或截面已变化");
    EXPECT_TRUE(std::filesystem::is_empty(fixture.root));
    ASSERT_TRUE(original->SetManufacturingGeometryResourceID(originalResource));
    ASSERT_TRUE(original->SetManufacturingGeometryResourceVersion(originalVersion));
    EXPECT_EQ(invoke(fixture.scene,"ReadNestingResult",ObjectMap{
        {"revision",fixture.revision},{"planId",fixture.planId}}),unchanged);
    checkFiles(invoke(fixture.scene,"ExportNesting",fixture.exportRequest(VariantArray{fixture.planId})),
        shape(fixture.scene,part(fixture.scene,fixture.partId)));
}

}
