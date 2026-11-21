namespace head_machine_compatibility_tests {
    const std::array<Text,3> MachineTypes{"TableTable","HeadTable","HeadHead"};
    Map Request(Scene& scene,const Text& setup={}){
        Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")}};
        if(!setup.empty())request["setupId"]=setup;return request;
    }
    Array Identity(){Array values;for(const auto value:iCAX::CAM::Motion::Identity())values.emplace_back(value);return values;}
    Map Node(const Text& name,int parent,const Text& type){
        return {{"name",name},{"parent",parent},{"origin",Identity()},{"type",type},{"joint",name},{"axis",Array{1.,0.,0.}},
            {"lower",type=="fixed"?0.:-std::numbers::pi},{"upper",type=="fixed"?0.:std::numbers::pi},{"position",0.},{"collisionBoxes",Array{}}};
    }
    Map Model(int rotations,bool branch=false){
        Array nodes{Node("mount",-1,"fixed")},names;
        for(int index=0;index<rotations;++index){const auto name="R"+std::to_string(index+1);nodes.emplace_back(Node(name,index,index?"continuous":"revolute"));names.emplace_back(name);}
        const auto tool=static_cast<int>(nodes.size());nodes.emplace_back(Node("laser",tool-1,"fixed"));
        if(branch){nodes.emplace_back(Node("unrelated rotary branch",0,"revolute"));names.emplace_back(Text("unrelated rotary branch"));}
        return {{"kind",Text("Imported")},{"name",Text("Declared miniature cutting head")},{"motionType",Text(rotations||branch?"Articulated":"Rigid")},
            {"rotaryAxisCount",static_cast<UInt>(rotations+(branch?1:0))},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},
            {"beamLocalDirection",Array{0.,0.,-1.}},{"collisionSource",Text("HeadDescription")},{"collisionBoxes",Array{}},
            {"assembly",Map{{"nodes",nodes},{"tool",tool},{"tcp",Array{0.,0.,0.}},{"beam",Array{0.,0.,-1.}},
                {"kind",Text("Declared")},{"headAxes",names},{"workpieceAxes",Array{}}}}};
    }
    Map Parameters(const Map& model){
        Array axes;
        if(model.contains("assembly")){
            const auto assembly=model.at("assembly").To<Map>();const auto nodes=assembly.at("nodes").To<Array>();std::set<int> chain;
            for(int index=assembly.at("tool").To<int>();index>=0;index=nodes[index].To<Map>().at("parent").To<int>())chain.insert(index);
            for(std::size_t index=0;index<nodes.size();++index){const auto node=nodes[index].To<Map>();if(!chain.contains(static_cast<int>(index))||node.at("type").To<Text>()=="fixed")continue;
                axes.emplace_back(Map{{"index",static_cast<UInt>(index)},{"name",node.at("joint")},{"target",Text("Head")},{"velocityDegS",60.},
                    {"origin",node.at("origin")},{"axis",node.at("axis")},{"zeroOffsetDeg",0.},{"sign",1.},{"lowerDeg",-180.},{"upperDeg",180.}});
            }
        }
        return {{"tcpMode",Text("Coordinates")},{"tcp",Array{0.,0.,0.}},{"beam",Array{0.,0.,-1.}},
            {"gaugeOrigin",Array{0.,0.,0.}},{"lengthMm",0.},{"axes",axes}};
    }
    Text SeedInstance(Scene& scene,const Map& model){
        // Seed a declared model snapshot; all selection/configuration assertions
        // below still call the production SDO instead of a test classifier.
        const auto id=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        const auto job=std::dynamic_pointer_cast<CFiveAxisJobComponent>(scene.Database().GetMetaEntity()->GetComponent(CFiveAxisJobComponent::S_ClassName));
        if(!job)throw std::runtime_error("Missing test job");auto instances=job->GetHeadInstances();
        instances[id]=Map{{"id",id},{"name",model.at("name")},{"model",model},{"parameters",Parameters(model)},
            {"profile",Map{}},{"binding",Map{}},{"calibrationConfirmed",true},{"revision",UInt(1)}};
        if(!job->SetProperty("HeadInstances",instances))throw std::runtime_error("Cannot seed declared head snapshot");return id;
    }
    Map Create(Scene& scene,const Text& workpiece,const Text& machine){
        auto request=Request(scene);request["action"]=Text("create");request["workpieceId"]=workpiece;request["machineType"]=machine;request["name"]="Operation "+machine;
        return Invoke(&scene,"CamOperation","Manage",request);
    }
    void Select(Scene& scene,const Text& setup,const Text& instance){
        auto request=Request(scene,setup);request["action"]=Text("select");request["instanceId"]=instance;Invoke(&scene,"CamHead","Instance",request);
    }
    void Apply(Scene& scene,const Text& setup,const Map& parameters){
        auto request=Request(scene,setup);request["action"]=Text("configure");request["parameters"]=parameters;Invoke(&scene,"CamHead","Instance",request);
    }
    Map Instance(Scene& scene,const Text& id){for(const auto& value:Read(scene).at("headInstances").To<Array>()){const auto instance=value.To<Map>();if(instance.at("id").To<Text>()==id)return instance;}throw std::runtime_error("Head instance missing");}
    void ExpectTypes(const Map& value,const Text& machine){ASSERT_TRUE(value.contains("machineTypes"));EXPECT_EQ((Array{machine}),value.at("machineTypes").To<Array>());}
    void ExpectUnassigned(Scene& scene,const Text& setup){EXPECT_TRUE(head_scope_tests::Setup(scene,setup).at("headInstanceId").To<Text>().empty());}
    template<class Action> void Reject(Scene& scene,Action&& action,const Text& code="CAM_HEAD_MACHINE_TYPE_MISMATCH"){
        const auto before=Read(scene);try{action();FAIL()<<"Expected "<<code;}catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find(code))<<error.what();}
        EXPECT_EQ(before,Read(scene));
    }
    struct Fixture {
        std::shared_ptr<iCAX::Application::IProductUserDataStore> Store=TestFixtureLibraryStore();
        std::unique_ptr<Scene> Value=head_scope_tests::NewScene(Store);
        File Stock{BRepPrimAPI_MakeBox(40,50,20).Shape()};Map Part;
        Fixture(){Part=ImportedWorkpiece(*Value,Stock);}
        ~Fixture(){head_scope_tests::CloseScene(Value);}
        Text Workpiece()const{return Part.at("workpieceId").To<Text>();}
    };
}

TEST(FiveAxisHeadMachineCompatibility, DeclaredToolBranchCountsTwoOneAndZeroAndRejectsWrongExplicitSelectionsAtomically){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;const auto cad=head_scope_tests::Cad(scene);
    std::array<Text,3> heads;for(int count=0;count<3;++count)heads[count]=SeedInstance(scene,Model(count));
    for(int count=0;count<3;++count){SCOPED_TRACE(MachineTypes[count]);const auto operation=Create(scene,fixture.Workpiece(),MachineTypes[count]);const auto setup=operation.at("setupId").To<Text>();
        ExpectTypes(Instance(scene,heads[count]),MachineTypes[count]);Select(scene,setup,heads[count]);
        const auto head=head_scope_tests::Setup(scene,setup).at("headModel").To<Map>();ExpectTypes(head,MachineTypes[count]);
        EXPECT_EQ(heads[count],head.at("instance").To<Map>().at("id").To<Text>());
        for(int other=0;other<3;++other)if(other!=count)Reject(scene,[&]{Select(scene,setup,heads[other]);});
    }
    EXPECT_EQ(cad,head_scope_tests::Cad(scene));
}

TEST(FiveAxisHeadMachineCompatibility, UnspecifiedAndUnknownSnapshotsAreNotTreatedAsCompatibleFixedHeads){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    auto unknown=Model(0);unknown.erase("assembly");unknown.erase("rotaryAxisCount");unknown.erase("motionType");const auto head=SeedInstance(scene,unknown);
    ASSERT_TRUE(Instance(scene,head).at("machineTypes").To<Array>().empty());
    auto emptyRef=Model(0);emptyRef.erase("assembly");emptyRef["geometryRef"]=Map{};emptyRef["previewRef"]=Map{};const auto empty=SeedInstance(scene,emptyRef);EXPECT_TRUE(Instance(scene,empty).at("machineTypes").To<Array>().empty());
    auto malformedRef=emptyRef;malformedRef["geometryRef"]=Map{{"unexpected",Text("not a resource reference")}};const auto malformed=SeedInstance(scene,malformedRef);EXPECT_TRUE(Instance(scene,malformed).at("machineTypes").To<Array>().empty());
    for(const auto& type:MachineTypes){const auto operation=Create(scene,fixture.Workpiece(),type);const auto setup=operation.at("setupId").To<Text>();
        EXPECT_TRUE(head_scope_tests::Setup(scene,setup).at("headModel").To<Map>().at("machineTypes").To<Array>().empty());for(const auto& id:{head,empty,malformed})Reject(scene,[&]{Select(scene,setup,id);});
    }
}

TEST(FiveAxisHeadMachineCompatibility, RotarySideBranchesDoNotChangeTheActualToolChainClassification){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    const auto head=SeedInstance(scene,Model(1,true));ExpectTypes(Instance(scene,head),"HeadTable");
    const auto operation=Create(scene,fixture.Workpiece(),"HeadTable");Select(scene,operation.at("setupId").To<Text>(),head);
    ExpectTypes(head_scope_tests::Setup(scene,operation.at("setupId").To<Text>()).at("headModel").To<Map>(),"HeadTable");
    const auto wrong=Create(scene,fixture.Workpiece(),"HeadHead");Reject(scene,[&]{Select(scene,wrong.at("setupId").To<Text>(),head);});
}

TEST(FiveAxisHeadMachineCompatibility, PhysicalAxisRetargetRemovalAndVirtualHeadAxisCannotBypassTheOperationType){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    const auto head=SeedInstance(scene,Model(2));const auto operation=Create(scene,fixture.Workpiece(),"HeadHead");const auto setup=operation.at("setupId").To<Text>();Select(scene,setup,head);
    const auto original=Instance(scene,head).at("parameters").To<Map>();
    auto retarget=original;auto axes=retarget.at("axes").To<Array>();auto work=axes.front().To<Map>();work["target"]=Text("Workpiece");axes.front()=work;retarget["axes"]=axes;Reject(scene,[&]{Apply(scene,setup,retarget);});
    auto removed=original;axes=removed.at("axes").To<Array>();axes.pop_back();removed["axes"]=axes;Reject(scene,[&]{Apply(scene,setup,removed);});
    auto virtualHead=original;axes=virtualHead.at("axes").To<Array>();auto extra=axes.front().To<Map>();extra["index"]=UInt(5);extra["name"]=Text("Virtual head axis");axes.emplace_back(extra);virtualHead["axes"]=axes;Reject(scene,[&]{Apply(scene,setup,virtualHead);});
}

TEST(FiveAxisHeadMachineCompatibility, ScopedDefaultsSkipIncompatibleHeadsAndKeepTheMoreSpecificExplicitSelection){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    std::array<Text,3> heads,setups;for(int count=0;count<3;++count){heads[count]=SeedInstance(scene,Model(count));setups[count]=Create(scene,fixture.Workpiece(),MachineTypes[count]).at("setupId").To<Text>();}
    head_scope_tests::Set(scene,setups[2],"Project",heads[2]);head_scope_tests::Expect(scene,setups[2],"Project",heads[2]);ExpectUnassigned(scene,setups[0]);ExpectUnassigned(scene,setups[1]);
    head_scope_tests::Set(scene,setups[1],"Workpiece",heads[1]);head_scope_tests::Expect(scene,setups[1],"Workpiece",heads[1]);head_scope_tests::Expect(scene,setups[2],"Project",heads[2]);ExpectUnassigned(scene,setups[0]);
    head_scope_tests::Set(scene,setups[0],"Setup",heads[0]);
    head_scope_tests::Set(scene,setups[1],"Project",heads[1]);head_scope_tests::Expect(scene,setups[0],"Setup",heads[0]);head_scope_tests::Expect(scene,setups[1],"Workpiece",heads[1]);ExpectUnassigned(scene,setups[2]);
    head_scope_tests::Inherit(scene,setups[1],"Workpiece");head_scope_tests::Expect(scene,setups[1],"Project",heads[1]);head_scope_tests::Expect(scene,setups[0],"Setup",heads[0]);
    const auto future=Create(scene,fixture.Workpiece(),"HeadHead");ExpectUnassigned(scene,future.at("setupId").To<Text>());
}

TEST(FiveAxisHeadMachineCompatibility, CompatibleInstanceEditsAlsoValidateEveryOtherOperationUsingThatInstance){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    const auto head=SeedInstance(scene,Model(1));const auto first=Create(scene,fixture.Workpiece(),"HeadTable"),second=Create(scene,fixture.Workpiece(),"HeadTable");
    for(const auto& operation:{first,second})Select(scene,operation.at("setupId").To<Text>(),head);
    auto parameters=Instance(scene,head).at("parameters").To<Map>();parameters["tcp"]=Array{2.,3.,4.};Apply(scene,first.at("setupId").To<Text>(),parameters);
    for(const auto& operation:{first,second}){const auto snapshot=head_scope_tests::Setup(scene,operation.at("setupId").To<Text>()).at("headModel").To<Map>();ExpectTypes(snapshot,"HeadTable");EXPECT_EQ((Array{2.,3.,4.}),snapshot.at("tcpLocalPosition").To<Array>());}
    auto axes=parameters.at("axes").To<Array>();auto axis=axes.front().To<Map>();axis["target"]=Text("Workpiece");axes.front()=axis;parameters["axes"]=axes;Reject(scene,[&]{Apply(scene,first.at("setupId").To<Text>(),parameters);});
}

TEST(FiveAxisHeadMachineCompatibility, FixedCreationMachineAndRealImportedToolingPersistWithTheMatchingHead){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    auto missing=Request(scene);missing["action"]=Text("create");missing["workpieceId"]=fixture.Workpiece();missing["name"]=Text("Missing type");const auto initial=Read(scene);EXPECT_THROW(Invoke(&scene,"CamOperation","Manage",missing),std::exception);EXPECT_EQ(initial,Read(scene));
    const auto head=SeedInstance(scene,Model(1));const auto operation=Create(scene,fixture.Workpiece(),"HeadTable");const auto setup=operation.at("setupId").To<Text>();Select(scene,setup,head);
    auto change=Request(scene,setup);change["scope"]=Text("Setup");change["action"]=Text("set");change["machineType"]=Text("HeadHead");const auto fixed=Read(scene);EXPECT_THROW(Invoke(&scene,"CamOperation","AssignMachineType",change),std::exception);EXPECT_EQ(fixed,Read(scene));
    File tooling(BRepPrimAPI_MakeBox(8,12,16).Shape());auto import=Request(scene,setup);import["sourcePath"]=tooling.Utf8();const auto task=Wait(scene,Invoke(&scene,"CamFixture","Import",import));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto saved=Read(scene),cad=head_scope_tests::Cad(scene);ASSERT_FALSE(saved.at("fixtures").To<Array>().empty());for(const auto& value:saved.at("fixtures").To<Array>())EXPECT_EQ(setup,value.To<Map>().at("setupId").To<Text>());
    auto path=tooling.Path;path.replace_extension(".i5cam");head_scope_tests::Save(scene,path);std::filesystem::remove(tooling.Path);auto reopened=head_scope_tests::Open(path,fixture.Store);
    EXPECT_EQ(cad,head_scope_tests::Cad(*reopened));const auto restored=Read(*reopened);EXPECT_EQ(saved.at("fixtures"),restored.at("fixtures"));EXPECT_EQ(saved.at("placements"),restored.at("placements"));
    const auto machine=Invoke(reopened.get(),"CamOperation","MachineAssignments",{{"setupId",setup}}).at("effective").To<Map>();EXPECT_EQ("HeadTable",machine.at("machineType").To<Text>());ExpectTypes(head_scope_tests::Setup(*reopened,setup).at("headModel").To<Map>(),"HeadTable");
    for(const auto& value:restored.at("fixtures").To<Array>()){const auto ref=value.To<Map>().at("geometryRef").To<Map>();EXPECT_TRUE(reopened->Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>())));}
    head_scope_tests::CloseScene(reopened);std::filesystem::remove(path);
}

TEST(FiveAxisHeadMachineCompatibility, BuiltinAndSavedConfigurationCatalogsExposeTheSameCompatibilityAndRejectWrongUse){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;
    BindBuiltinHeadSources(scene,InstalledHeadSources());std::set<Text> available;
    for(const auto& value:Invoke(&scene,"CamHead","ListBuiltins").at("items").To<Array>()){
        const auto item=value.To<Map>();ASSERT_TRUE(item.contains("machineTypes"));for(const auto& type:item.at("machineTypes").To<Array>())available.insert(type.To<Text>());
    }
    for(const auto& type:MachineTypes)EXPECT_TRUE(available.contains(type));
    std::array<Text,3> configurations;
    for(int count=0;count<3;++count){const auto head=SeedInstance(scene,Model(count)),setup=Create(scene,fixture.Workpiece(),MachineTypes[count]).at("setupId").To<Text>();Select(scene,setup,head);
        auto save=Request(scene,setup);save["action"]=Text("save");save["name"]="Configuration "+MachineTypes[count];const auto record=Invoke(&scene,"CamHead","Configurations",save);ExpectTypes(record,MachineTypes[count]);configurations[count]=record.at("id").To<Text>();
    }
    const auto records=Invoke(&scene,"CamHead","Configurations",{{"action",Text("list")}}).at("items").To<Array>();ASSERT_EQ(3u,records.size());
    for(int count=0;count<3;++count){const auto record=std::find_if(records.begin(),records.end(),[&](const auto& value){return value.template To<Map>().at("id").template To<Text>()==configurations[count];});ASSERT_NE(records.end(),record);ExpectTypes(record->To<Map>(),MachineTypes[count]);}
    const auto target=Create(scene,fixture.Workpiece(),"HeadHead").at("setupId").To<Text>();
    auto use=Request(scene,target);use["action"]=Text("use");use["entryId"]=configurations[0];Reject(scene,[&]{Invoke(&scene,"CamHead","Configurations",use);});
    use=Request(scene,target);use["action"]=Text("use");use["entryId"]=configurations[2];EXPECT_NO_THROW(Invoke(&scene,"CamHead","Configurations",use));ExpectTypes(head_scope_tests::Setup(scene,target).at("headModel").To<Map>(),"HeadHead");
}

TEST(FiveAxisHeadMachineCompatibility, BuiltinAndConfigurationDefaultProbesIgnoreLockedOperationsOfAnotherType){
    using namespace head_machine_compatibility_tests;Fixture fixture;auto& scene=*fixture.Value;BindBuiltinHeadSources(scene,InstalledHeadSources());
    const auto matching=Create(scene,fixture.Workpiece(),"HeadHead").at("setupId").To<Text>(),different=Create(scene,fixture.Workpiece(),"TableTable").at("setupId").To<Text>();
    const auto component=[&](const Text& id){return std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(id))->GetComponent(CFiveAxisSetupComponent::S_ClassName));};
    ASSERT_TRUE(component(different)->SetProperty("Locked",true));const auto unchanged=head_scope_tests::Setup(scene,different);
    Text builtin;for(const auto& value:Invoke(&scene,"CamHead","ListBuiltins").at("items").To<Array>()){const auto item=value.To<Map>();for(const auto& type:item.at("machineTypes").To<Array>())if(type.To<Text>()=="HeadHead"){builtin=item.at("id").To<Text>();break;}if(!builtin.empty())break;}ASSERT_FALSE(builtin.empty());
    auto assign=head_scope_tests::Request(scene,matching,"Project");assign["kind"]=Text("Builtin");assign["builtinDefinitionId"]=builtin;EXPECT_NO_THROW(Invoke(&scene,"CamHead","Assign",assign));EXPECT_EQ(unchanged,head_scope_tests::Setup(scene,different));
    const auto bound=head_scope_tests::Setup(scene,matching).at("headModel").To<Map>();ExpectTypes(bound,"HeadHead");
    auto save=Request(scene,matching);save["action"]=Text("save");save["name"]=Text("Compatible locked-default fixture");const auto configuration=Invoke(&scene,"CamHead","Configurations",save);
    for(const auto& kind:{Text("Builtin"),Text("Configuration")}){
        auto request=head_scope_tests::Request(scene,matching,"Project");request["kind"]=kind;request[kind=="Builtin"?"builtinDefinitionId":"entryId"]=kind=="Builtin"?Variant(builtin):configuration.at("id");
        EXPECT_NO_THROW(Invoke(&scene,"CamHead","Assign",request));EXPECT_EQ(unchanged,head_scope_tests::Setup(scene,different));
        ASSERT_TRUE(component(matching)->SetProperty("Locked",true));
        request=head_scope_tests::Request(scene,matching,"Project");request["kind"]=kind;request[kind=="Builtin"?"builtinDefinitionId":"entryId"]=kind=="Builtin"?Variant(builtin):configuration.at("id");
        Reject(scene,[&]{Invoke(&scene,"CamHead","Assign",request);},"CAM_SETUP_LOCKED");ASSERT_TRUE(component(matching)->SetProperty("Locked",false));
    }
}
