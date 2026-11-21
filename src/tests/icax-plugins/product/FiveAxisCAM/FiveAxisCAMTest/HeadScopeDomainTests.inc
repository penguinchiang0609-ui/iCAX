namespace head_scope_tests {
    Map Query(Scene& scene,const Text& setupID){return Invoke(&scene,"CamHead","Assignments",{{"setupId",setupID}});}
    Map Scope(Scene& scene,const Text& setupID,const Text& scope){return Query(scene,setupID).at("scopes").To<Map>().at(scope).To<Map>();}
    Map Effective(Scene& scene,const Text& setupID){return Query(scene,setupID).at("effective").To<Map>();}
    Map Setup(Scene& scene,const Text& setupID){for(const auto& value:Read(scene).at("setups").To<Array>()){const auto setup=value.To<Map>();if(setup.at("id").To<Text>()==setupID)return setup;}throw std::runtime_error("Head scope setup missing");}
    Map Request(Scene& scene,const Text& setupID,const Text& scope,const Text& action="set"){
        Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"setupId",setupID},{"scope",scope},{"action",action}};
        if(scope=="Product")request["preferencesRevision"]=Query(scene,setupID).at("productPreferencesRevision");return request;
    }
    Map Set(Scene& scene,const Text& setupID,const Text& scope,const Text& instanceID){auto request=Request(scene,setupID,scope);request["kind"]=Text("Instance");request["instanceId"]=instanceID;return Invoke(&scene,"CamHead","Assign",request);}
    Map None(Scene& scene,const Text& setupID,const Text& scope){auto request=Request(scene,setupID,scope);request["kind"]=Text("None");return Invoke(&scene,"CamHead","Assign",request);}
    Map Inherit(Scene& scene,const Text& setupID,const Text& scope){return Invoke(&scene,"CamHead","Assign",Request(scene,setupID,scope,"inherit"));}
    std::unique_ptr<Scene> NewScene(const std::shared_ptr<iCAX::Application::IProductUserDataStore>& store){
        auto scene=std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),false,true);
        scene->FixtureLibraryStore=store;InitializeScene(*scene);BindFixtureLibraryStore(*scene,store);return scene;
    }
    void CloseScene(std::unique_ptr<Scene>& scene){
        if(!scene)return;
        // The test Scene has no behaviour host. Mirror the production scene's
        // shutdown hooks so its toolpath document releases the repository and
        // the repository-owned runtime can release its product store.
        const auto component=std::dynamic_pointer_cast<CFiveAxisSceneComponent>(scene->Database().GetMetaEntity()->GetComponent(CFiveAxisSceneComponent::S_ClassName));
        if(component){const auto& runtime=component->Runtime;ReleaseMachineJog(*scene,runtime);ReleaseMachinePoseEdit(*scene,runtime);ReleaseTaskProgressPDO(*scene,runtime);ReleaseIntentToolpaths(*scene,runtime);}
        scene.reset();
    }
    Map Cad(Scene& scene){
        Map result;const auto state=Read(scene);for(const auto& value:state.at("workpieces").To<Array>()){
            const auto w=value.To<Map>();Map geometry;for(const auto* key:{"sourceRef","geometryRef","previewRef","importMetadata"})if(w.contains(key))geometry[key]=w.at(key);result[w.at("id").To<Text>()]=geometry;
        }return result;
    }
    Map Machining(Scene& scene){
        Map result;for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))if(const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(entity->GetComponent(CFiveAxisSetupComponent::S_ClassName)))
            result[iCAX::Data::to_string(entity->GetID())]=Map{{"cuts",setup->GetGeneratedToolpaths()},{"rapids",setup->GetGeneratedRapidPaths()}};
        return result;
    }
    void Expect(Scene& scene,const Text& setupID,const Text& source,const Text& instanceID){
        const auto effective=Effective(scene,setupID);EXPECT_EQ(source,effective.at("source").To<Text>());EXPECT_EQ(instanceID,effective.at("instanceId").To<Text>());
        const auto head=Setup(scene,setupID).at("headModel").To<Map>();if(!instanceID.empty()){ASSERT_TRUE(head.contains("instance"));EXPECT_EQ(instanceID,head.at("instance").To<Map>().at("id").To<Text>());}
    }
    void ExpectTCP(Scene& scene,const Text& setupID,double x){const auto head=Setup(scene,setupID).at("headModel").To<Map>();EXPECT_DOUBLE_EQ(x,head.at("tcpLocalPosition").To<Array>().front().To<double>());}
    Text CreatePose(Scene& scene,const Text& placement,const Text& name){
        auto request=GenerateRequest(scene);request.erase("setupId");request["placementId"]=placement;request["action"]=Text("create");request["name"]=name;request["machineType"]=Text("TableTable");
        return Invoke(&scene,"CamSetup","Manage",request).at("setupId").To<Text>();
    }
    struct Fixture {
        std::shared_ptr<iCAX::Application::IProductUserDataStore> Store;
        std::unique_ptr<Scene> Value;
        Text First,Second,Other,FirstPlacement,OtherPlacement;
        std::array<Text,4> Heads;
        Fixture(std::shared_ptr<iCAX::Application::IProductUserDataStore> store=TestFixtureLibraryStore()):Store(std::move(store)),Value(NewScene(Store)){
            auto& scene=*Value;PrepareOrderPart(scene,true,"TableTable");auto state=Read(scene);First=state.at("activeSetupId").To<Text>();FirstPlacement=state.at("activePlacementId").To<Text>();
            (void)fast_rapid_tests::Batch(scene);
            for(size_t i=0;i<Heads.size();++i){
                File model(BRepPrimAPI_MakeBox(2.+i,3.,20.).Shape());const auto imported=Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,model)));
                if(imported.at("status").To<Text>()!="Completed")throw std::runtime_error("Head scope model import failed");
                auto parameters=head_instance_tests::Instance(scene).at("parameters").To<Map>();parameters["tcp"]=Array{static_cast<double>(i+1),0.,0.};head_instance_tests::Apply(scene,parameters);
                Heads[i]=head_instance_tests::Instance(scene).at("id").To<Text>();
            }
            Second=CreatePose(scene,FirstPlacement,"同工件的第二姿态");
            File other(BRepPrimAPI_MakeBox(30.,40.,20.).Shape());const auto placement=Imported(scene,other,"TableTable");OtherPlacement=placement.at("placementId").To<Text>();Other=CreatePose(scene,OtherPlacement,"其他工件的姿态");
            auto activate=GenerateRequest(scene);activate.erase("setupId");activate["placementId"]=FirstPlacement;Invoke(&scene,"CamWorkpiece","Activate",activate);
            Inherit(scene,First,"Setup");Inherit(scene,Second,"Setup");Inherit(scene,Other,"Setup");
        }
    };
    void Save(Scene& scene,const std::filesystem::path& path){
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="四级刀头指定";file.Save(path,info,scene.Database(),scene.Resources());
    }
    std::unique_ptr<Scene> Open(const std::filesystem::path& path,const std::shared_ptr<iCAX::Application::IProductUserDataStore>& store){
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        const auto prepared=file.PrepareOpen(path);const auto info=prepared.Result().Info;auto scene=std::make_unique<Scene>(info.ProjectID,info.MainSceneID,false,true);scene->FixtureLibraryStore=store;
        file.Restore(prepared,scene->Database(),scene->Resources());InitializeScene(*scene);BindFixtureLibraryStore(*scene,store);return scene;
    }
}

TEST(FiveAxisHeadScope, HigherDefaultsOnlyChangeInheritorsAndPreserveSavedCutAndRapidGeometry){
    using namespace head_scope_tests;Fixture f;auto& scene=*f.Value;const auto cad=Cad(scene),paths=Machining(scene);const auto authored=Read(scene).at("intentToolpaths");
    ASSERT_FALSE(paths.at(f.First).To<Map>().at("cuts").To<Map>().empty());ASSERT_FALSE(paths.at(f.First).To<Map>().at("rapids").To<Map>().empty());
    Set(scene,f.First,"Project",f.Heads[0]);for(const auto& id:{f.First,f.Second,f.Other})Expect(scene,id,"Project",f.Heads[0]);
    Set(scene,f.First,"Workpiece",f.Heads[1]);Expect(scene,f.First,"Workpiece",f.Heads[1]);Expect(scene,f.Second,"Workpiece",f.Heads[1]);Expect(scene,f.Other,"Project",f.Heads[0]);
    Set(scene,f.First,"Setup",f.Heads[2]);Set(scene,f.First,"Project",f.Heads[3]);Expect(scene,f.First,"Setup",f.Heads[2]);Expect(scene,f.Second,"Workpiece",f.Heads[1]);Expect(scene,f.Other,"Project",f.Heads[3]);
    Set(scene,f.First,"Workpiece",f.Heads[3]);Expect(scene,f.First,"Setup",f.Heads[2]);Expect(scene,f.Second,"Workpiece",f.Heads[3]);
    Inherit(scene,f.First,"Setup");Expect(scene,f.First,"Workpiece",f.Heads[3]);Inherit(scene,f.First,"Workpiece");Expect(scene,f.First,"Project",f.Heads[3]);Expect(scene,f.Second,"Project",f.Heads[3]);
    EXPECT_EQ(cad,Cad(scene));EXPECT_EQ(paths,Machining(scene));EXPECT_EQ(authored,Read(scene).at("intentToolpaths"));
}

TEST(FiveAxisHeadScope, ExplicitNoneMasksDefaultsAndNewObjectsStartWithInheritance){
    using namespace head_scope_tests;Fixture f;auto& scene=*f.Value;Set(scene,f.First,"Project",f.Heads[0]);Set(scene,f.First,"Workpiece",f.Heads[1]);
    None(scene,f.First,"Setup");EXPECT_EQ("None",Scope(scene,f.First,"Setup").at("mode").To<Text>());Expect(scene,f.First,"Setup",{});
    Set(scene,f.First,"Project",f.Heads[3]);Expect(scene,f.First,"Setup",{});Expect(scene,f.Second,"Workpiece",f.Heads[1]);Expect(scene,f.Other,"Project",f.Heads[3]);
    const auto next=CreatePose(scene,f.FirstPlacement,"从显式未指定姿态创建");EXPECT_EQ("Inherit",Scope(scene,next,"Setup").at("mode").To<Text>());Expect(scene,next,"Workpiece",f.Heads[1]);
    File model(BRepPrimAPI_MakeBox(10.,15.,20.).Shape());const auto placement=Imported(scene,model,"TableTable");const auto added=CreatePose(scene,placement.at("placementId").To<Text>(),"新增工件姿态");Expect(scene,added,"Project",f.Heads[3]);EXPECT_EQ("Inherit",Scope(scene,added,"Workpiece").at("mode").To<Text>());
    Inherit(scene,f.First,"Setup");Expect(scene,f.First,"Workpiece",f.Heads[1]);None(scene,f.First,"Workpiece");for(const auto& id:{f.First,f.Second,next})Expect(scene,id,"Workpiece",{});
    Inherit(scene,f.First,"Workpiece");for(const auto& id:{f.First,f.Second,next})Expect(scene,id,"Project",f.Heads[3]);
}

TEST(FiveAxisHeadScope, LegacyNilDeclarationsKeepInheritingAfterCacheMaterializationAndReopen){
    using namespace head_scope_tests;Fixture f;auto& scene=*f.Value;
    // Legacy archives have no declaration metadata. A nil ID means inherited;
    // a non-nil ID is an existing explicit pose choice and must remain so.
    auto& transaction=scene.Database().BeginTransaction("准备旧工程刀头声明");
    transaction.ModifyComponent(*ID::from_string(f.First),CFiveAxisSetupComponent::S_ClassName,{{"HeadAssignment",Map{}},{"HeadInstanceID",ID()}});
    transaction.ModifyComponent(*ID::from_string(f.Second),CFiveAxisSetupComponent::S_ClassName,{{"HeadAssignment",Map{}},{"HeadInstanceID",*ID::from_string(f.Heads[2])}});
    ASSERT_TRUE(scene.Database().CommitTransaction(transaction));const auto cad=Cad(scene),paths=Machining(scene);
    EXPECT_EQ("Inherit",Scope(scene,f.First,"Setup").at("mode").To<Text>());EXPECT_EQ("Instance",Scope(scene,f.Second,"Setup").at("mode").To<Text>());
    Set(scene,f.First,"Project",f.Heads[0]);Expect(scene,f.First,"Project",f.Heads[0]);Expect(scene,f.Second,"Setup",f.Heads[2]);
    EXPECT_EQ("Inherit",Scope(scene,f.First,"Setup").at("mode").To<Text>());
    Set(scene,f.First,"Project",f.Heads[1]);Expect(scene,f.First,"Project",f.Heads[1]);Expect(scene,f.Second,"Setup",f.Heads[2]);
    EXPECT_EQ("Inherit",Scope(scene,f.First,"Setup").at("mode").To<Text>());
    File marker;auto path=marker.Path;path.replace_extension(".legacy-head-scopes.i5cam");Save(scene,path);auto reopened=Open(path,TestFixtureLibraryStore());
    EXPECT_EQ("Inherit",Scope(*reopened,f.First,"Setup").at("mode").To<Text>());Expect(*reopened,f.Second,"Setup",f.Heads[2]);
    Set(*reopened,f.First,"Project",f.Heads[3]);Expect(*reopened,f.First,"Project",f.Heads[3]);Expect(*reopened,f.Second,"Setup",f.Heads[2]);
    EXPECT_EQ("Inherit",Scope(*reopened,f.First,"Setup").at("mode").To<Text>());EXPECT_EQ(cad,Cad(*reopened));EXPECT_EQ(paths,Machining(*reopened));std::filesystem::remove(path);
}

TEST(FiveAxisHeadScope, DefaultEditsAreUndoableAndPortableProjectReopenPreservesOverridesAndNone){
    using namespace head_scope_tests;Fixture f;auto& scene=*f.Value;Set(scene,f.First,"Project",f.Heads[0]);Set(scene,f.First,"Setup",f.Heads[2]);Set(scene,f.First,"Workpiece",f.Heads[1]);
    const auto before=Query(scene,f.Second),paths=Machining(scene),cad=Cad(scene);const auto history=scene.Database().GetUndoArray().size();
    Set(scene,f.First,"Workpiece",f.Heads[3]);EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());Expect(scene,f.First,"Setup",f.Heads[2]);Expect(scene,f.Second,"Workpiece",f.Heads[3]);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Query(scene,f.Second));ASSERT_TRUE(scene.Database().Redo());Expect(scene,f.Second,"Workpiece",f.Heads[3]);
    None(scene,f.Other,"Setup");const std::array<Map,3> assigned{Query(scene,f.First),Query(scene,f.Second),Query(scene,f.Other)};
    File marker;auto path=marker.Path;path.replace_extension(".head-scopes.i5cam");Save(scene,path);auto reopened=Open(path,TestFixtureLibraryStore());
    const std::array<Text,3> ids{f.First,f.Second,f.Other};for(size_t i=0;i<ids.size();++i)EXPECT_EQ(assigned[i],Query(*reopened,ids[i]));
    EXPECT_EQ(cad,Cad(*reopened));EXPECT_EQ(paths,Machining(*reopened));EXPECT_EQ("None",Scope(*reopened,f.Other,"Setup").at("mode").To<Text>());std::filesystem::remove(path);
}

TEST(FiveAxisHeadScope, ProductDefaultIsCapturedOnlyByFutureProjectsAndSurvivesStoreRecreation){
    using namespace head_scope_tests;File marker;auto databasePath=marker.Path;databasePath.replace_extension(".head-defaults.sqlite");auto frozenPath=marker.Path;frozenPath.replace_extension(".head-product-frozen.i5cam");auto oldPath=marker.Path;oldPath.replace_extension(".head-product-old.i5cam");
    // The store consumes UTF-8. Keep the fixture's Chinese filename instead of
    // converting it through the Windows narrow execution code page.
    const auto databaseUtf8=databasePath.u8string();ASSERT_NE(std::u8string::npos,databaseUtf8.find(u8"五轴导入-"));const Text databaseText(reinterpret_cast<const char*>(databaseUtf8.data()),databaseUtf8.size());
    {
        auto store=TestFixtureLibraryStore(databaseText);const std::weak_ptr<iCAX::Application::IProductUserDataStore> originalStore=store;Fixture f(store);auto& old=*f.Value;const auto oldState=Read(old);const auto oldSnapshot=Query(old,f.First).at("productSnapshot");const auto history=old.Database().GetUndoArray().size();
        Set(old,f.First,"Product",f.Heads[0]);EXPECT_EQ(oldState,Read(old));EXPECT_EQ(history,old.Database().GetUndoArray().size());EXPECT_EQ(oldSnapshot,Query(old,f.First).at("productSnapshot"));
        auto future=NewScene(store);Set(old,f.First,"Product",f.Heads[3]);PrepareOrderPart(*future,true,"TableTable");const auto first=Read(*future).at("activeSetupId").To<Text>();
        EXPECT_EQ("Product",Effective(*future,first).at("source").To<Text>());ExpectTCP(*future,first,1.);EXPECT_EQ("Instance",Scope(*future,first,"Product").at("mode").To<Text>());
        const auto frozen=Query(*future,first).at("productSnapshot");Save(*future,frozenPath);Save(old,oldPath);
        CloseScene(future);CloseScene(f.Value);f.Store.reset();store.reset();ASSERT_TRUE(originalStore.expired())<<"The first SQLite store must be destroyed before recreating it";
        auto recreated=TestFixtureLibraryStore(databaseText);const std::weak_ptr<iCAX::Application::IProductUserDataStore> recreatedStore=recreated;auto newest=NewScene(recreated);PrepareOrderPart(*newest,true,"TableTable");const auto latest=Read(*newest).at("activeSetupId").To<Text>();ExpectTCP(*newest,latest,4.);
        auto reopened=Open(frozenPath,recreated);ExpectTCP(*reopened,first,1.);EXPECT_EQ(frozen,Query(*reopened,first).at("productSnapshot"));
        auto reopenedOld=Open(oldPath,recreated);EXPECT_EQ(oldSnapshot,Query(*reopenedOld,f.First).at("productSnapshot"));EXPECT_TRUE(Effective(*reopenedOld,f.First).at("instanceId").To<Text>().empty());
        CloseScene(reopenedOld);CloseScene(reopened);CloseScene(newest);recreated.reset();ASSERT_TRUE(recreatedStore.expired())<<"The recreated SQLite store must close before removing the temporary database";
    }
    for(const auto& path:{frozenPath,oldPath,databasePath})std::filesystem::remove(path);
}

TEST(FiveAxisHeadScope, LockedInheritorsAndStalePreferenceRevisionsRejectAtomically){
    using namespace head_scope_tests;Fixture f;auto& scene=*f.Value;Set(scene,f.First,"Project",f.Heads[0]);Set(scene,f.First,"Setup",f.Heads[2]);
    const auto locked=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(f.Second))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(locked->SetProperty("Locked",true));
    const auto before=Read(scene),assignments=Query(scene,f.First);const auto history=scene.Database().GetUndoArray();EXPECT_THROW(Set(scene,f.First,"Project",f.Heads[3]),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(assignments,Query(scene,f.First));EXPECT_EQ(history,scene.Database().GetUndoArray());
    ASSERT_TRUE(locked->SetProperty("Locked",false));const auto preferences=Query(scene,f.First).at("productPreferencesRevision");Set(scene,f.First,"Product",f.Heads[0]);
    auto stale=Request(scene,f.First,"Product");stale["preferencesRevision"]=preferences;stale["kind"]=Text("Instance");stale["instanceId"]=f.Heads[3];const auto product=Scope(scene,f.First,"Product");EXPECT_THROW(Invoke(&scene,"CamHead","Assign",stale),std::exception);EXPECT_EQ(product,Scope(scene,f.First,"Product"));
    auto invalid=Request(scene,f.First,"Invalid");invalid["kind"]=Text("None");const auto unchanged=Read(scene);EXPECT_THROW(Invoke(&scene,"CamHead","Assign",invalid),std::exception);EXPECT_EQ(unchanged,Read(scene));
}
