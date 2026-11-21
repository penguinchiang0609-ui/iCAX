std::shared_ptr<iCAX::Application::IProductUserDataStore> TestFixtureLibraryStore(const Text& databasePath=":memory:") {
    using namespace iCAX::Application;
    CUserDataRecordTypeDescriptor assembly;assembly.RecordType="assembly";assembly.SubjectTypes={"product"};
    CUserDataRecordTypeDescriptor preferences;preferences.RecordType="preferences";preferences.SubjectTypes={"product"};preferences.AllowMultiple=false;
    return std::make_shared<CProductUserDataStore>(std::make_shared<CSqliteUserDataStore>(databasePath),"icax.five-axis-cam",
        std::vector<CUserDataFeatureDescriptor>{{"fixture-library",{assembly,preferences}},{"machine-type-configurations",{preferences}},{"head-configurations",{[] {CUserDataRecordTypeDescriptor d;d.RecordType="configuration";d.SubjectTypes={"product"};return d;}(),preferences}}});
}
Map LibrarySaveAll(Scene& scene,const Text& name) {
    auto request=FixtureRequest(scene);Array ids;for(const auto& f:Read(scene).at("fixtures").To<Array>())ids.push_back(f.To<Map>().at("id"));
    request["fixtureIds"]=ids;request["name"]=name;const auto saved=Invoke(&scene,"CamFixture","LibrarySave",request);
    EXPECT_EQ(saved,Invoke(&scene,"CamFixture","LibrarySave",request));return saved;
}
TEST(FiveAxisFixtureLibrary, ImportedSnapshotDefaultsIndependentCopiesRevisionsAndUndo) {
    auto store=TestFixtureLibraryStore();Scene source;source.FixtureLibraryStore=store;File part,tool(BRepPrimAPI_MakeBox(12,24,36).Shape());
    Imported(source,part);const auto id=ImportedFixture(source,tool);
    auto edit=FixtureRequest(source,id);Array pose{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};pose[3]=82.;pose[7]=-14.;edit["transform"]=pose;Invoke(&source,"CamFixture","Update",edit);
    const auto saved=LibrarySaveAll(source,"固定底座");std::filesystem::remove(tool.Path);
    auto defaults=FixtureRequest(source);defaults["action"]=Text("default");defaults["entryId"]=saved.at("id");defaults["preferencesRevision"]=Text("0");
    Invoke(&source,"CamFixture","LibraryUpdate",defaults);
    Scene target;target.FixtureLibraryStore=store;auto imported=Imported(target,part);
    auto state=Read(target);ASSERT_EQ(1u,state.at("fixtures").To<Array>().size());
    auto fixture=state.at("fixtures").To<Array>()[0].To<Map>();EXPECT_EQ(pose,fixture.at("transform").To<Array>());EXPECT_NE(id,fixture.at("id").To<Text>());
    auto sourceRef=Read(source).at("fixtures").To<Array>()[0].To<Map>().at("geometryRef").To<Map>(),targetRef=fixture.at("geometryRef").To<Map>();
    auto a=source.Resources().Get<iCAX::GeometryData::BRepModel>(sourceRef.at("url").To<Text>(),std::stoull(sourceRef.at("version").To<Text>()));
    auto b=target.Resources().Get<iCAX::GeometryData::BRepModel>(targetRef.at("url").To<Text>(),std::stoull(targetRef.at("version").To<Text>()));
    EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*a),iCAX::GeometryData::Persistence::Serialize(*b));
    edit=FixtureRequest(target,fixture.at("id").To<Text>());auto moved=pose;moved[3]=123.;edit["transform"]=moved;Invoke(&target,"CamFixture","Update",edit);
    EXPECT_EQ(pose,Read(source).at("fixtures").To<Array>()[0].To<Map>().at("transform").To<Array>());
    auto create=FixtureRequest(target);create.erase("setupId");create["placementId"]=imported.at("placementId");create["action"]=Text("create");create["name"]=Text("翻面");create["machineType"]=Text("HeadHead");
    Invoke(&target,"CamSetup","Manage",create);EXPECT_EQ(2u,Read(target).at("fixtures").To<Array>().size());
    ASSERT_TRUE(target.Database().Undo());EXPECT_EQ(1u,Read(target).at("fixtures").To<Array>().size());
    ASSERT_TRUE(target.Database().Redo());EXPECT_EQ(2u,Read(target).at("fixtures").To<Array>().size());
    auto rename=FixtureRequest(source);rename["action"]=Text("rename");rename["entryId"]=saved.at("id");rename["revision"]=saved.at("revision");rename["name"]=Text("常用底座");
    const auto changed=Invoke(&source,"CamFixture","LibraryUpdate",rename);
    rename["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&source,"CamFixture","LibraryUpdate",rename),std::exception);
    auto use=FixtureRequest(target);use["entryId"]=saved.at("id");use["revision"]=saved.at("revision");
    EXPECT_THROW(Invoke(&target,"CamFixture","LibraryUse",use),std::exception);
    auto remove=FixtureRequest(source);remove["action"]=Text("delete");remove["entryId"]=saved.at("id");remove["revision"]=changed.at("revision");
    EXPECT_THROW(Invoke(&source,"CamFixture","LibraryUpdate",remove),std::exception);
    defaults=FixtureRequest(source);defaults["action"]=Text("default");defaults["entryId"]=Text();defaults["preferencesRevision"]=Invoke(&source,"CamFixture","LibraryList",{}).at("preferencesRevision");
    Invoke(&source,"CamFixture","LibraryUpdate",defaults);remove["expected"]=Read(source).at("expected");Invoke(&source,"CamFixture","LibraryUpdate",remove);
    EXPECT_EQ(2u,Read(target).at("fixtures").To<Array>().size());EXPECT_TRUE(Invoke(&source,"CamFixture","LibraryList",{}).at("entries").To<Array>().empty());
}
TEST(FiveAxisFixtureLibrary, GeneratedAndMixedAssemblyCanBeReusedWithoutOriginalProject) {
    auto store=TestFixtureLibraryStore();Scene source;source.FixtureLibraryStore=store;File part(FixtureCurvedSheet()),tool(BRepPrimAPI_MakeBox(8,9,10).Shape());
    Imported(source,part);SetPlateRecipe(source,PlateRecipe(source));const auto candidate=GeneratePlates(source);AdoptPlates(source,candidate);
    const auto protectedID=Read(source).at("fixtures").To<Array>()[0].To<Map>().at("id").To<Text>();
    auto lock=FixtureRequest(source,protectedID);lock["locked"]=true;Invoke(&source,"CamFixture","Update",lock);
    EXPECT_THROW(AdoptPlates(source,candidate),std::exception);
    lock=FixtureRequest(source,protectedID);lock["locked"]=false;Invoke(&source,"CamFixture","Update",lock);
    const auto generated=LibrarySaveAll(source,"曲面支撑");EXPECT_EQ("Generated",generated.at("source").To<Text>());
    ImportedFixture(source,tool);const auto mixed=LibrarySaveAll(source,"完整组合");EXPECT_EQ("Mixed",mixed.at("source").To<Text>());
    Scene target;target.FixtureLibraryStore=store;File other;Imported(target,other);
    auto use=FixtureRequest(target);use["entryId"]=mixed.at("id");use["revision"]=mixed.at("revision");
    const auto result=Invoke(&target,"CamFixture","LibraryUse",use);EXPECT_EQ(result,Invoke(&target,"CamFixture","LibraryUse",use));
    EXPECT_EQ(5u,Read(target).at("fixtures").To<Array>().size());
    ASSERT_TRUE(target.Database().Undo());EXPECT_TRUE(Read(target).at("fixtures").To<Array>().empty());
    ASSERT_TRUE(target.Database().Redo());EXPECT_EQ(5u,Read(target).at("fixtures").To<Array>().size());
    // A reusable mixed assembly remains freely placeable, independently of
    // the original generated support unit from which some parts were copied.
    const auto sourceParts=Read(source).at("fixtures");const Array mixedPose{0.,-1.,0.,15.,1.,0.,0.,-12.,0.,0.,1.,4.,0.,0.,0.,1.};
    auto moveMixed=FixtureRequest(target);moveMixed["unitId"]=result.at("unitId");moveMixed["transform"]=mixedPose;Invoke(&target,"CamFixture","UnitUpdate",moveMixed);
    const auto placed=Read(target);for(const auto& value:placed.at("fixtures").To<Array>())EXPECT_EQ(value.To<Map>().at("transform").To<Array>(),mixedPose);
    const auto changedID=placed.at("fixtures").To<Array>().front().To<Map>().at("id").To<Text>();const auto independentPose=FixtureTransform(30,40,50);MoveFixture(target,changedID,independentPose);
    for(const auto& value:Read(target).at("fixtures").To<Array>()){const auto current=value.To<Map>();for(const auto& old:placed.at("fixtures").To<Array>())if(old.To<Map>().at("id")==current.at("id"))EXPECT_EQ(current.at("transform").To<Array>(),current.at("id").To<Text>()==changedID?independentPose:mixedPose);}
    EXPECT_EQ(sourceParts,Read(source).at("fixtures"));
    auto path=other.Path;path.replace_extension(".i5cam");iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=target.Project;info.MainSceneID=target.GetSceneID();info.ProjectName="复用工装";
    file.Save(path,info,target.Database(),target.Resources());Scene reopened(target.Project,target.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(Read(target).at("fixtures"),Read(reopened).at("fixtures"));std::filesystem::remove(path);
}



TEST(FiveAxisFixtureLibrary, DirectImportIsProductDataAndFixtureLocksAreEnforced) {
    auto store=TestFixtureLibraryStore();Scene scene;scene.FixtureLibraryStore=store;File part,tool(BRepPrimAPI_MakeBox(12,24,36).Shape());Imported(scene,part);
    const auto before=Read(scene);auto request=FixtureRequest(scene);request["sourcePath"]=tool.Utf8();
    const auto task=Invoke(&scene,"CamFixture","LibraryImport",request);EXPECT_EQ(task,Invoke(&scene,"CamFixture","LibraryImport",request));
    const auto done=Wait(scene,task);ASSERT_EQ("Completed",done.at("status").To<Text>());const auto entry=done.at("result").To<Map>();
    EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    std::filesystem::remove(tool.Path);Scene other;other.FixtureLibraryStore=store;Imported(other,part);
    auto use=FixtureRequest(other);use["entryId"]=entry.at("id");use["revision"]=entry.at("revision");
    const auto placed=Invoke(&other,"CamFixture","LibraryUse",use);const auto id=placed.at("fixtureIds").To<Array>()[0].To<Text>();
    auto lock=FixtureRequest(other,id);lock["locked"]=true;Invoke(&other,"CamFixture","Update",lock);
    auto move=FixtureRequest(other,id);move["transform"]=Array{1.,0.,0.,10.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};EXPECT_THROW(Invoke(&other,"CamFixture","Update",move),std::exception);
    EXPECT_THROW(Invoke(&other,"CamFixture","Remove",FixtureRequest(other,id)),std::exception);
    auto off=FixtureRequest(other,id);off["enabled"]=false;Invoke(&other,"CamFixture","Update",off);
    const auto f=Read(other).at("fixtures").To<Array>()[0].To<Map>();EXPECT_TRUE(f.at("locked").To<bool>());EXPECT_FALSE(f.at("enabled").To<bool>());
    EXPECT_THROW(CheckedFixtures(other,2.),std::exception);
}

TEST(FiveAxisFixtureLibrary, UnitsKeepGeneratedPartsParametersAndManufacturingIndependent) {
    Scene source;source.FixtureLibraryStore=TestFixtureLibraryStore();File part(FixtureCurvedSheet()),tool(BRepPrimAPI_MakeBox(8,9,10).Shape());
    Imported(source,part);SetPlateRecipe(source,PlateRecipe(source));AdoptPlates(source,GeneratePlates(source));
    auto state=Read(source);ASSERT_EQ(1u,state.at("fixtureUnits").To<Array>().size());auto unit=state.at("fixtureUnits").To<Array>()[0].To<Map>();
    const auto id=unit.at("id").To<Text>();const auto partIds=unit.at("partIds").To<Array>();ASSERT_EQ(4u,partIds.size());
    const auto unitRequest=[&](Scene& scene,const Text& unitID){auto r=FixtureRequest(scene);r["unitId"]=unitID;return r;};
    auto move=unitRequest(source,id);move["transform"]=Array{0.,-1.,0.,100.,1.,0.,0.,200.,0.,0.,1.,300.,0.,0.,0.,1.};ExpectFixedGeneratedFixtureTransform(source,"UnitUpdate",move);
    const auto pose=FixtureTransform();auto rename=unitRequest(source,id);rename["name"]=Text("固定拼装曲面支撑");const auto receipt=Invoke(&source,"CamFixture","UnitUpdate",rename);EXPECT_EQ(receipt,Invoke(&source,"CamFixture","UnitUpdate",rename));
    EXPECT_EQ(Read(source).at("fixtureUnits").To<Array>().front().To<Map>().at("name").To<Text>(),"固定拼装曲面支撑");
    for(const auto& p:Read(source).at("fixtures").To<Array>())EXPECT_EQ(pose,p.To<Map>().at("transform").To<Array>());
    ASSERT_TRUE(source.Database().Undo());EXPECT_EQ(unit.at("name"),Read(source).at("fixtureUnits").To<Array>().front().To<Map>().at("name"));for(const auto& p:Read(source).at("fixtures").To<Array>())EXPECT_EQ(pose,p.To<Map>().at("transform").To<Array>());ASSERT_TRUE(source.Database().Redo());
    auto lock=unitRequest(source,id);lock["locked"]=true;Invoke(&source,"CamFixture","UnitUpdate",lock);
    rename=unitRequest(source,id);rename["name"]=Text("锁定时禁止更名");EXPECT_THROW(Invoke(&source,"CamFixture","UnitUpdate",rename),std::exception);
    EXPECT_THROW(Invoke(&source,"CamFixture","UnitRemove",unitRequest(source,id)),std::exception);
    auto off=unitRequest(source,id);off["enabled"]=false;Invoke(&source,"CamFixture","UnitUpdate",off);for(const auto& p:Read(source).at("fixtures").To<Array>())EXPECT_FALSE(p.To<Map>().at("enabled").To<bool>());
    off=unitRequest(source,id);off["enabled"]=true;Invoke(&source,"CamFixture","UnitUpdate",off);lock=unitRequest(source,id);lock["locked"]=false;Invoke(&source,"CamFixture","UnitUpdate",lock);
    ImportedFixture(source,tool);EXPECT_EQ(2u,Read(source).at("fixtureUnits").To<Array>().size());
    auto fresh=FixtureRequest(source);fresh["recipe"]=unit.at("design").To<Map>().at("recipe");fresh["newUnit"]=true;Invoke(&source,"CamFixture","SetRecipe",fresh);AdoptPlates(source,GeneratePlates(source));
    EXPECT_EQ(3u,Read(source).at("fixtureUnits").To<Array>().size());for(const auto& partID:partIds){bool found=false;for(const auto& p:Read(source).at("fixtures").To<Array>())if(p.To<Map>().at("id")==partID){found=true;EXPECT_EQ(pose,p.To<Map>().at("transform").To<Array>());}EXPECT_TRUE(found);}
    auto breakdown=FixtureBreakdownDetails(source,unitRequest(source,id));ASSERT_EQ(4u,breakdown.at("parts").To<Array>().size());
    auto save=FixtureRequest(source);save["fixtureIds"]=Array{id};save["name"]=Text("固定拼装的曲面夹具");const auto entry=Invoke(&source,"CamFixture","LibrarySave",save);
    Scene target;target.FixtureLibraryStore=source.FixtureLibraryStore;File other;Imported(target,other);
    auto use=FixtureRequest(target);use["entryId"]=entry.at("id");use["revision"]=entry.at("revision");const auto used=Invoke(&target,"CamFixture","LibraryUse",use);const auto targetID=used.at("unitId").To<Text>();
    ASSERT_EQ(1u,Read(target).at("fixtureUnits").To<Array>().size());auto copied=Read(target).at("fixtureUnits").To<Array>()[0].To<Map>();
    EXPECT_EQ(unit.at("design").To<Map>().at("recipe"),copied.at("design").To<Map>().at("recipe"));EXPECT_EQ(pose,copied.at("transform").To<Array>());
    auto copiedBreakdown=FixtureBreakdownDetails(target,unitRequest(target,targetID));EXPECT_EQ(4u,copiedBreakdown.at("parts").To<Array>().size());
    const auto root=std::filesystem::temp_directory_path()/("icax-unit-"+iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));std::filesystem::create_directories(root);
    auto output=unitRequest(target,targetID);output["destination"]=root.string();const auto delivery=Invoke(&target,"CamFixture","Export",output);EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(delivery.at("directory").To<Text>())/"manifest.json"));
    auto path=other.Path;path.replace_extension(".i5cam");iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=target.Project;info.MainSceneID=target.GetSceneID();info.ProjectName="夹具单元";file.Save(path,info,target.Database(),target.Resources());Scene reopened(target.Project,target.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(Read(target).at("fixtureUnits"),Read(reopened).at("fixtureUnits"));
    Invoke(&target,"CamFixture","UnitRemove",unitRequest(target,targetID));EXPECT_TRUE(Read(target).at("fixtureUnits").To<Array>().empty());EXPECT_TRUE(Read(target).at("fixtures").To<Array>().empty());ASSERT_TRUE(target.Database().Undo());EXPECT_EQ(1u,Read(target).at("fixtureUnits").To<Array>().size());EXPECT_EQ(4u,Read(target).at("fixtures").To<Array>().size());
    std::filesystem::remove(path);ASSERT_TRUE(std::filesystem::equivalent(std::filesystem::temp_directory_path(),root.parent_path()));std::filesystem::remove_all(root);
}
