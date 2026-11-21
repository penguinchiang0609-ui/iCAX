namespace operation_tests {
    Map Request(Scene& scene){return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")}};}
    Map ActivateRequest(Scene& scene,const Map& part){auto request=Request(scene);request["placementId"]=part.at("placementId");return request;}
    Map Create(Scene& scene,const Text& workpiece,const Text& name,const Text& machineType="HeadHead"){auto r=Request(scene);r["action"]=Text("create");r["workpieceId"]=workpiece;r["name"]=name;r["machineType"]=machineType;return Invoke(&scene,"CamOperation","Manage",r);}
    Map Reference(Scene& scene,const Text& operation,const Text& workpiece){auto r=Request(scene);r["action"]=Text("reference");r["setupId"]=operation;r["workpieceId"]=workpiece;return Invoke(&scene,"CamOperation","Manage",r);}
    Map Query(Scene& scene,const Text& operation){return Invoke(&scene,"CamOperation","MachineAssignments",{{"setupId",operation}});}
    Map AssignmentRequest(Scene& scene,const Text& operation,const Text& scope,const Text& action="set"){
        auto r=Request(scene);r["setupId"]=operation;r["scope"]=scope;r["action"]=action;
        if(scope=="Product")r["preferencesRevision"]=Query(scene,operation).at("productPreferencesRevision");return r;
    }
    Map Set(Scene& scene,const Text& operation,const Text& scope,const Text& type){auto r=AssignmentRequest(scene,operation,scope);r["machineType"]=type;return Invoke(&scene,"CamOperation","AssignMachineType",r);}
    Map Inherit(Scene& scene,const Text& operation,const Text& scope){return Invoke(&scene,"CamOperation","AssignMachineType",AssignmentRequest(scene,operation,scope,"inherit"));}
    void Expect(Scene& scene,const Text& operation,const Text& source,const Text& type){const auto e=Query(scene,operation).at("effective").To<Map>();EXPECT_EQ(source,e.at("source").To<Text>());EXPECT_EQ(type,e.at("machineType").To<Text>());}
    Map Operation(Scene& scene,const Text& id){for(const auto& value:Read(scene).at("operations").To<Array>()){const auto op=value.To<Map>();if(op.at("id").To<Text>()==id)return op;}throw std::runtime_error("Missing operation");}
    Map Placement(Scene& scene,const Text& id){for(const auto& value:Read(scene).at("placements").To<Array>()){const auto p=value.To<Map>();if(p.at("id").To<Text>()==id)return p;}throw std::runtime_error("Missing placement");}
    Array Multiply(const Array& a,const Array& b){iCAX::CAM::Motion::Matrix left{},right{};for(int i=0;i<16;++i){left[i]=a[i].To<double>();right[i]=b[i].To<double>();}Array out;for(const auto n:iCAX::CAM::Motion::Multiply(left,right))out.emplace_back(n);return out;}
    void ExpectNative(Scene& scene,const Text& entityID,const Array& expected){
        PumpTasks(scene);const auto entity=scene.Database().GetEntity(*ID::from_string(entityID));const auto t=std::dynamic_pointer_cast<iCAX::Transform::CTransformComponent>(entity->GetComponent(iCAX::Transform::CTransformComponent::S_ClassName));ASSERT_TRUE(t);
        const auto m=iCAX::Transform::MakeLocalMatrix(t->GetPositionX(),t->GetPositionY(),t->GetPositionZ(),t->GetYawRadians(),t->GetPitchRadians(),t->GetRollRadians(),t->GetScaleX(),t->GetScaleY(),t->GetScaleZ());for(int i=0;i<16;++i)EXPECT_NEAR(expected[i].To<double>(),m(i/4,i%4),1e-8)<<i;
    }
    auto BRepBytes(Scene& scene,const Map& part){const auto ref=part.at("geometryRef").To<Map>();const auto& value=ref.at("version");const auto version=value.Is<Text>()?std::stoull(value.To<Text>()):value.Is<UInt>()?value.To<UInt>():static_cast<UInt>(value.To<long long>());const auto shape=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),version);if(!shape)throw std::runtime_error("Missing operation geometry");return iCAX::GeometryData::Persistence::Serialize(*shape);}
    struct Fixture {
        std::shared_ptr<iCAX::Application::IProductUserDataStore> Store;
        std::unique_ptr<Scene> Value;
        File A{BRepPrimAPI_MakeBox(20,30,10).Shape()},B{BRepPrimAPI_MakeBox(35,45,15).Shape()};
        Map PartA,PartB,First,Second,Other;
        explicit Fixture(std::shared_ptr<iCAX::Application::IProductUserDataStore> store=TestFixtureLibraryStore()):Store(std::move(store)),Value(head_scope_tests::NewScene(Store)){
            PartA=ImportedWorkpiece(*Value,A);PartB=ImportedWorkpiece(*Value,B);
            First=Create(*Value,PartA.at("workpieceId").To<Text>(),"工序 10");Second=Create(*Value,PartA.at("workpieceId").To<Text>(),"工序 20");Other=Create(*Value,PartB.at("workpieceId").To<Text>(),"工序 30");
        }
        ~Fixture(){head_scope_tests::CloseScene(Value);}
        Text ID(const Map& op)const{return op.at("setupId").To<Text>();}
    };
}

TEST(FiveAxisOperation, ReferencesSharedCadWithIndependentSourcePlacementsAndGlobalSequence){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;const auto cad=head_scope_tests::Cad(scene);auto state=Read(scene);
    ASSERT_EQ(3u,state.at("operations").To<Array>().size());ASSERT_EQ(5u,state.at("placements").To<Array>().size());
    for(const auto& part:{f.PartA,f.PartB}){const auto source=Placement(scene,part.at("placementId").To<Text>());EXPECT_TRUE(source.at("setupId").To<Text>().empty());EXPECT_EQ(part.at("workpieceId"),source.at("workpieceId"));}
    EXPECT_NE(f.PartA.at("placementId"),f.First.at("placementId"));EXPECT_NE(f.First.at("placementId"),f.Second.at("placementId"));
    EXPECT_EQ(f.PartA.at("workpieceId"),Operation(scene,f.ID(f.First)).at("workpieceId"));EXPECT_EQ(f.PartA.at("workpieceId"),Operation(scene,f.ID(f.Second)).at("workpieceId"));
    auto reorder=Request(scene);reorder["setupIds"]=Array{f.First.at("setupId"),f.Other.at("setupId"),f.Second.at("setupId")};Invoke(&scene,"CamSetup","SetSequence",reorder);
    state=Read(scene);const auto operations=state.at("operations").To<Array>();EXPECT_EQ(f.Other.at("setupId"),operations[1].To<Map>().at("id"));EXPECT_EQ(cad,head_scope_tests::Cad(scene));
    // Creating from the old public setup entry also preserves the source CAD
    // view; the workpiece page cannot inherit an operation's transform.
    auto legacy=Request(scene);legacy["placementId"]=f.PartA.at("placementId");legacy["action"]=Text("create");legacy["name"]=Text("旧入口创建");legacy["machineType"]=Text("HeadTable");const auto created=Invoke(&scene,"CamSetup","Manage",legacy);
    EXPECT_NE(f.PartA.at("placementId"),created.at("placementId"));EXPECT_TRUE(Placement(scene,f.PartA.at("placementId").To<Text>()).at("setupId").To<Text>().empty());
}

TEST(FiveAxisOperation, EmptyOperationCanSelectAnotherWorkpieceWithoutEditingItsGeometryOrOtherOperations){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;const auto cad=head_scope_tests::Cad(scene),beforeOther=Operation(scene,f.ID(f.Second));const auto id=f.ID(f.First);
    Set(scene,id,"Project","HeadHead");Set(scene,id,"Workpiece","HeadTable");Set(scene,f.ID(f.Other),"Workpiece","TableTable");
    const auto poseBefore=Placement(scene,f.First.at("placementId").To<Text>()).at("transform");const auto changed=Reference(scene,id,f.PartB.at("workpieceId").To<Text>());
    EXPECT_EQ(f.PartB.at("workpieceId"),changed.at("workpieceId"));Expect(scene,id,"Setup","HeadHead");EXPECT_EQ(poseBefore,Placement(scene,f.First.at("placementId").To<Text>()).at("transform"));
    EXPECT_EQ(f.PartA.at("workpieceId"),Operation(scene,f.ID(f.Second)).at("workpieceId"));EXPECT_EQ(cad,head_scope_tests::Cad(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(f.PartA.at("workpieceId"),Operation(scene,id).at("workpieceId"));Expect(scene,id,"Setup","HeadHead");ASSERT_TRUE(scene.Database().Redo());Expect(scene,id,"Setup","HeadHead");
}

TEST(FiveAxisOperation, CreateUsesExistingDefaultFixtureAtomicallyWithoutChangingCadOrOtherOperations){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;File tool(BRepPrimAPI_MakeBox(6,7,8).Shape());const auto originalFixture=ImportedFixture(scene,tool);
    const auto entry=LibrarySaveAll(scene,"工序默认工装");auto defaults=FixtureRequest(scene);defaults["action"]=Text("default");defaults["entryId"]=entry.at("id");defaults["preferencesRevision"]=Invoke(&scene,"CamFixture","LibraryList",{}).at("preferencesRevision");Invoke(&scene,"CamFixture","LibraryUpdate",defaults);
    const auto before=Read(scene),cad=head_scope_tests::Cad(scene);const auto created=Create(scene,f.PartA.at("workpieceId").To<Text>(),"带默认工装的工序");const auto after=Read(scene);
    ASSERT_EQ(before.at("fixtures").To<Array>().size()+1,after.at("fixtures").To<Array>().size());ASSERT_EQ(before.at("operations").To<Array>().size()+1,after.at("operations").To<Array>().size());
    Map added;for(const auto& value:after.at("fixtures").To<Array>()){const auto fixture=value.To<Map>();if(fixture.at("setupId")==created.at("setupId"))added=fixture;}
    ASSERT_FALSE(added.empty());EXPECT_NE(originalFixture,added.at("id").To<Text>());EXPECT_EQ(cad,head_scope_tests::Cad(scene));
    for(const auto& value:before.at("fixtures").To<Array>()){const auto fixtures=after.at("fixtures").To<Array>();EXPECT_TRUE(std::find(fixtures.begin(),fixtures.end(),value)!=fixtures.end());}
    const auto history=scene.Database().GetUndoArray().size();ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(before.at("operations"),Read(scene).at("operations"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(after.at("operations"),Read(scene).at("operations"));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}

TEST(FiveAxisOperation, WorkpieceCopyCopiesOnlySourceCadAndDoesNotDuplicateItsOperations){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;const auto before=Read(scene);auto r=Request(scene);r["placementId"]=f.First.at("placementId");const auto copied=Invoke(&scene,"CamWorkpiece","Copy",r);const auto after=Read(scene);
    EXPECT_EQ(before.at("operations").To<Array>().size(),after.at("operations").To<Array>().size());EXPECT_EQ(before.at("workpieces").To<Array>().size()+1,after.at("workpieces").To<Array>().size());
    EXPECT_TRUE(copied.at("setupId").To<Text>().empty());EXPECT_TRUE(Placement(scene,copied.at("placementId").To<Text>()).at("setupId").To<Text>().empty());
    EXPECT_NE(f.PartA.at("workpieceId"),copied.at("workpieceId"));
}

TEST(FiveAxisOperation, CreatedMachineTypesStayFixedAndPreserveCadAndGeneratedGeometry){
    using namespace operation_tests;head_scope_tests::Fixture f;auto& scene=*f.Value;const auto cad=head_scope_tests::Cad(scene),paths=head_scope_tests::Machining(scene);const auto cuts=Read(scene).at("intentToolpaths");
    const auto generated=head_scope_tests::Setup(scene,f.First).at("generatedGeometry").To<Map>();ASSERT_EQ("Current",generated.at("status").To<Text>());
    const auto expectGeometry=[&]{const auto current=head_scope_tests::Setup(scene,f.First).at("generatedGeometry").To<Map>();EXPECT_EQ("Current",current.at("status").To<Text>());EXPECT_EQ(generated,current);EXPECT_EQ(paths,head_scope_tests::Machining(scene));
        for(const auto& value:current.at("operations").To<Array>())EXPECT_EQ("Current",value.To<Map>().at("status").To<Text>());
        for(const auto& value:current.at("transitions").To<Array>()){const auto rapid=value.To<Map>();if(rapid.contains("geometryStatus"))EXPECT_NE("Stale",rapid.at("geometryStatus").To<Text>());}};
    Set(scene,f.First,"Project","HeadHead");expectGeometry();Set(scene,f.First,"Workpiece","HeadTable");expectGeometry();EXPECT_FALSE(Set(scene,f.First,"Setup","TableTable").at("changed").To<bool>());expectGeometry();Set(scene,f.First,"Project","TableTable");expectGeometry();
    Expect(scene,f.First,"Setup","TableTable");Expect(scene,f.Second,"Setup","TableTable");Expect(scene,f.Other,"Setup","TableTable");
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Inherit(scene,f.First,"Setup"),std::exception);
    EXPECT_THROW(Set(scene,f.First,"Setup","HeadHead"),std::exception);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());expectGeometry();
    EXPECT_EQ(cad,head_scope_tests::Cad(scene));EXPECT_EQ(paths,head_scope_tests::Machining(scene));EXPECT_EQ(cuts,Read(scene).at("intentToolpaths"));
    auto ref=Request(scene);ref["action"]=Text("reference");ref["setupId"]=f.First;ref["workpieceId"]=Operation(scene,f.Other).at("workpieceId");const auto unchanged=Read(scene);EXPECT_THROW(Invoke(&scene,"CamOperation","Manage",ref),std::exception);EXPECT_EQ(unchanged,Read(scene));
}

TEST(FiveAxisOperation, PoseMachineTypeAndWorkpieceReferencesSurviveUndoRedoAndPortableReopen){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;const auto id=f.ID(f.First);Set(scene,id,"Project","HeadTable");
    auto pose=PlacementRequest(scene,f.First);Invoke(&scene,"CamPlacement","SetTransform",pose);const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    EXPECT_FALSE(Set(scene,id,"Setup","HeadHead").at("changed").To<bool>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());Expect(scene,id,"Setup","HeadHead");ASSERT_TRUE(scene.Database().Redo());Expect(scene,id,"Setup","HeadHead");
    File marker;auto saved=marker.Path;saved.replace_extension(".operations.i5cam");head_scope_tests::Save(scene,saved);auto reopened=head_scope_tests::Open(saved,f.Store);
    EXPECT_EQ(Read(scene).at("operations"),Read(*reopened).at("operations"));EXPECT_EQ(Read(scene).at("placements"),Read(*reopened).at("placements"));EXPECT_EQ(head_scope_tests::Cad(scene),head_scope_tests::Cad(*reopened));
    head_scope_tests::CloseScene(reopened);std::filesystem::remove(saved);
}

TEST(FiveAxisOperation, FixedOperationsIgnoreChangedDefaultsAndRejectInvalidTypesStalePreferencesAndReusedCommands){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;const auto first=f.ID(f.First),second=f.ID(f.Second);Set(scene,first,"Project","HeadHead");
    auto lock=Request(scene);lock["placementId"]=f.Second.at("placementId");lock["locked"]=true;Invoke(&scene,"CamSetup","SetLocked",lock);
    EXPECT_NO_THROW(Set(scene,first,"Project","TableTable"));Expect(scene,first,"Setup","HeadHead");Expect(scene,second,"Setup","HeadHead");
    lock=Request(scene);lock["placementId"]=f.Second.at("placementId");lock["locked"]=false;Invoke(&scene,"CamSetup","SetLocked",lock);
    auto invalid=AssignmentRequest(scene,first,"Setup");invalid["machineType"]=Text("Robot");const auto unchanged=Read(scene);EXPECT_THROW(Invoke(&scene,"CamOperation","AssignMachineType",invalid),std::exception);EXPECT_EQ(unchanged,Read(scene));
    auto product=AssignmentRequest(scene,first,"Product");product["machineType"]=Text("HeadHead");const auto result=Invoke(&scene,"CamOperation","AssignMachineType",product);EXPECT_EQ(result,Invoke(&scene,"CamOperation","AssignMachineType",product));
    auto stale=AssignmentRequest(scene,first,"Product");stale["preferencesRevision"]=product.at("preferencesRevision");stale["machineType"]=Text("TableTable");EXPECT_THROW(Invoke(&scene,"CamOperation","AssignMachineType",stale),std::exception);
    auto reused=product;reused["machineType"]=Text("TableTable");EXPECT_THROW(Invoke(&scene,"CamOperation","AssignMachineType",reused),std::exception);
}

TEST(FiveAxisOperation, ProductMachineDefaultAffectsOnlyFutureProjectsAndIsFrozenAcrossReopen){
    using namespace operation_tests;auto store=TestFixtureLibraryStore();Fixture f(store);auto& old=*f.Value;const auto first=f.ID(f.First);const auto before=Read(old);const auto history=old.Database().GetUndoArray().size();
    Set(old,first,"Product","HeadTable");EXPECT_EQ(before,Read(old));EXPECT_EQ(history,old.Database().GetUndoArray().size());
    auto future=head_scope_tests::NewScene(store);File body(BRepPrimAPI_MakeBox(12,13,14).Shape());const auto part=ImportedWorkpiece(*future,body);const auto op=Create(*future,part.at("workpieceId").To<Text>(),"未来工序","HeadTable");const auto id=op.at("setupId").To<Text>();Expect(*future,id,"Setup","HeadTable");
    EXPECT_EQ(Text("HeadTable"),Query(*future,id).at("productSnapshot").To<Map>().at("machineType").To<Text>());
    Set(old,first,"Product","TableTable");Expect(*future,id,"Setup","HeadTable");Expect(old,first,"Setup","HeadHead");
    File marker;auto saved=marker.Path;saved.replace_extension(".frozen-machine-type.i5cam");head_scope_tests::Save(*future,saved);auto reopened=head_scope_tests::Open(saved,store);Expect(*reopened,id,"Setup","HeadTable");
    EXPECT_EQ(Text("HeadTable"),Query(*reopened,id).at("productSnapshot").To<Map>().at("machineType").To<Text>());
    auto newest=head_scope_tests::NewScene(store);const auto newPart=ImportedWorkpiece(*newest,body);const auto newOp=Create(*newest,newPart.at("workpieceId").To<Text>(),"最新工序","TableTable");Expect(*newest,newOp.at("setupId").To<Text>(),"Setup","TableTable");
    head_scope_tests::CloseScene(newest);head_scope_tests::CloseScene(reopened);head_scope_tests::CloseScene(future);std::filesystem::remove(saved);
}

TEST(FiveAxisOperation, BothCreationEntrypointsRequireExplicitValidMachineTypeAtomically){
    using namespace operation_tests;Fixture f;auto& scene=*f.Value;
    for(const auto legacy:{false,true})for(const auto type:{Text(),Text("Robot")}){
        auto r=Request(scene);r["action"]=Text("create");r["name"]=Text("缺少机型");if(legacy)r["placementId"]=f.PartA.at("placementId");else r["workpieceId"]=f.PartA.at("workpieceId");if(!type.empty())r["machineType"]=type;
        const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();EXPECT_THROW(Invoke(&scene,legacy?"CamSetup":"CamOperation","Manage",r),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    }
    auto ref=Request(scene);ref["action"]=Text("reference");ref["setupId"]=f.First.at("setupId");ref["workpieceId"]=f.PartB.at("workpieceId");ref["machineType"]=Text("TableTable");EXPECT_THROW(Invoke(&scene,"CamOperation","Manage",ref),std::exception);
    const auto created=Create(scene,f.PartA.at("workpieceId").To<Text>(),"固定 4+1","HeadTable");Expect(scene,created.at("setupId").To<Text>(),"Setup","HeadTable");
    const auto copied=Invoke(&scene,"CamSetup","Copy",ActivateRequest(scene,created));Expect(scene,copied.at("setupId").To<Text>(),"Setup","HeadTable");
}

TEST(FiveAxisOperation, RotaryToolingMovesWholeAssemblyWithLocalCadUnchangedAndSurvivesCopyUndoAndReopen){
    using namespace operation_tests;auto store=TestFixtureLibraryStore();auto scene=head_scope_tests::NewScene(store);File body(BRepPrimAPI_MakeBox(20,30,10).Shape()),tool(BRepPrimAPI_MakeBox(6,8,3).Shape());
    const auto ids=Imported(*scene,body,"HeadTable");const auto setup=ids.at("setupId").To<Text>(),placement=ids.at("placementId").To<Text>();const auto fixture=ImportedFixture(*scene,tool);
    auto edit=FixtureRequest(*scene,fixture);edit["visible"]=false;edit["enabled"]=false;edit["locked"]=true;Invoke(scene.get(),"CamFixture","Update",edit);
    const auto before=Read(*scene),cad=head_scope_tests::Cad(*scene);const auto local=Placement(*scene,placement).at("transform").To<Array>();const auto part=before.at("fixtures").To<Array>().front().To<Map>();const auto bytes=BRepBytes(*scene,part);
    const Array delta{0.,-1.,0.,17.,1.,0.,0.,23.,0.,0.,1.,31.,0.,0.,0.,1.};const auto effective=Multiply(delta,local);auto move=PlacementRequest(*scene,ids);move["transform"]=effective;
    const auto receipt=Invoke(scene.get(),"CamPlacement","SetTransform",move);EXPECT_EQ(Text("Assembly"),receipt.at("motionScope").To<Text>());EXPECT_EQ(delta,Operation(*scene,setup).at("assemblyTransform").To<Array>());EXPECT_EQ(local,receipt.at("statePatch").To<Map>().at("placement").To<Map>().at("transform").To<Array>());
    EXPECT_EQ(before.at("placements"),Read(*scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(*scene).at("fixtures"));EXPECT_EQ(cad,head_scope_tests::Cad(*scene));EXPECT_EQ(bytes,BRepBytes(*scene,Read(*scene).at("fixtures").To<Array>().front().To<Map>()));
    ExpectNative(*scene,placement,effective);ExpectNative(*scene,fixture,Multiply(delta,part.at("transform").To<Array>()));
    ASSERT_TRUE(scene->Database().Undo());ExpectNative(*scene,placement,local);ASSERT_TRUE(scene->Database().Redo());ExpectNative(*scene,placement,effective);
    const auto copied=Invoke(scene.get(),"CamSetup","Copy",ActivateRequest(*scene,ids));EXPECT_EQ(delta,Operation(*scene,copied.at("setupId").To<Text>()).at("assemblyTransform").To<Array>());ExpectNative(*scene,copied.at("placementId").To<Text>(),effective);
    File marker;auto path=marker.Path;path.replace_extension(".assembly.i5cam");head_scope_tests::Save(*scene,path);auto reopened=head_scope_tests::Open(path,store);EXPECT_EQ(Read(*scene).at("operations"),Read(*reopened).at("operations"));EXPECT_EQ(Read(*scene).at("fixtures"),Read(*reopened).at("fixtures"));ExpectNative(*reopened,placement,effective);ExpectNative(*reopened,fixture,Multiply(delta,part.at("transform").To<Array>()));
    head_scope_tests::CloseScene(reopened);head_scope_tests::CloseScene(scene);std::filesystem::remove(path);
}

TEST(FiveAxisOperation, HeadHeadAndRotaryWithoutToolingKeepRelativeWorkpiecePlacement){
    using namespace operation_tests;for(const auto type:{Text("HeadHead"),Text("HeadTable"),Text("TableTable")}){
        Scene scene;File body(BRepPrimAPI_MakeBox(20,30,10).Shape()),tool(BRepPrimAPI_MakeBox(6,8,3).Shape());const auto ids=Imported(scene,body,type);if(type=="HeadHead")ImportedFixture(scene,tool);
        const auto fixtures=Read(scene).at("fixtures");auto move=PlacementRequest(scene,ids);const auto desired=FixtureTransform(10,20,30);move["transform"]=desired;const auto result=Invoke(&scene,"CamPlacement","SetTransform",move);
        EXPECT_EQ(Text("Workpiece"),result.at("motionScope").To<Text>());EXPECT_EQ(desired,Placement(scene,ids.at("placementId").To<Text>()).at("transform").To<Array>());EXPECT_EQ(FixtureTransform(),Operation(scene,ids.at("setupId").To<Text>()).at("assemblyTransform").To<Array>());EXPECT_EQ(fixtures,Read(scene).at("fixtures"));ExpectNative(scene,ids.at("placementId").To<Text>(),desired);
    }
}

TEST(FiveAxisOperation, AssemblyPlacementPreservesExactSupportDesignAndRegeneratesInLocalInstallationFrame){
    using namespace operation_tests;Scene scene;File body(FixtureCurvedSheet());const auto ids=Imported(scene,body,"TableTable");SetPlateRecipe(scene,PlateRecipe(scene));const auto first=GeneratePlates(scene);AdoptPlates(scene,first);
    const auto before=Read(scene),design=FixtureQueryDesign(scene);const auto local=Placement(scene,ids.at("placementId").To<Text>()).at("transform").To<Array>();const auto parts=first.at("parts").To<Array>();ASSERT_FALSE(parts.empty());
    const Array delta{1.,0.,0.,9.,0.,0.,-1.,11.,0.,1.,0.,13.,0.,0.,0.,1.};auto move=PlacementRequest(scene,ids);move["transform"]=Multiply(delta,local);EXPECT_EQ(Text("Assembly"),Invoke(&scene,"CamPlacement","SetTransform",move).at("motionScope").To<Text>());
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));const auto after=FixtureQueryDesign(scene);EXPECT_EQ(design.at("assembly"),after.at("assembly"));EXPECT_EQ(Text("Current"),after.at("assembly").To<Map>().at("geometryValidity").To<Text>());
    const auto regenerated=GeneratePlates(scene).at("parts").To<Array>();ASSERT_EQ(parts.size(),regenerated.size());for(size_t i=0;i<parts.size();++i){EXPECT_EQ(parts[i].To<Map>().at("role"),regenerated[i].To<Map>().at("role"));EXPECT_EQ(BRepBytes(scene,parts[i].To<Map>()),BRepBytes(scene,regenerated[i].To<Map>()));}
}

TEST(FiveAxisOperation, PhysicalMachineSnapshotAndNativeProjectionComposeRegistrationAndAssemblyExactlyOnce){
    using namespace operation_tests;Scene scene;File body(BRepPrimAPI_MakeBox(20,30,10).Shape()),tool(BRepPrimAPI_MakeBox(6,8,3).Shape());const auto ids=Imported(scene,body,"TableTable");const auto fixture=ImportedFixture(scene,tool);const auto machine=CreateProductPlanMachine(scene,true);
    const auto setup=ids.at("setupId").To<Text>();const auto before=ReadMachineSimulation(scene,{{"setupId",setup}});const auto local=Placement(scene,ids.at("placementId").To<Text>()).at("transform").To<Array>();const Array delta{0.,-1.,0.,7.,1.,0.,0.,9.,0.,0.,1.,11.,0.,0.,0.,1.};
    auto move=PlacementRequest(scene,ids);move["transform"]=Multiply(delta,local);Invoke(&scene,"CamPlacement","SetTransform",move);const auto after=ReadMachineSimulation(scene,{{"setupId",setup}});
    EXPECT_EQ(before.at("configuration"),after.at("configuration"));EXPECT_NE(before.at("modelKey"),after.at("modelKey"));const auto registration=before.at("configuration").To<Map>().at("placement").To<Array>();EXPECT_EQ(Multiply(registration,delta),after.at("effectivePlacement").To<Array>());ExpectNative(scene,ids.at("placementId").To<Text>(),Multiply(Multiply(registration,delta),local));ExpectNative(scene,fixture,Multiply(registration,delta));
}
