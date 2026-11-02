namespace gantry_head_tests {
    namespace M=iCAX::CAM::Motion;
    namespace K=iCAX::GeometryServices::MachineKinematics;
    const auto Root=(std::filesystem::path(__FILE__).parent_path()/"../../../../../..").lexically_normal();
    struct Spec {Text Model;std::set<Text> Head,Work;};
    const std::vector<Spec> Specs{{"GAC-3015",{"A","C"},{}},{"GAB-3015",{"A","B"},{}},{"GBC-3015",{"B","C"},{}},{"GA-3015",{"A"},{"C"}},{"GF-3015",{},{"A","C"}}};
    void Check(const Spec& spec){
        SCOPED_TRACE(spec.Model);const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
        const auto path=Root/"src/apps/five-axis-cam/resources/machines/gantry-heads"/spec.Model/(spec.Model+".urdf");
        const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);Text definitionID;for(const auto& value:sources){const auto source=value.To<Map>();if(std::filesystem::path(source.at("sourcePath").To<Text>()).stem()==spec.Model)definitionID=source.at("id").To<Text>();}ASSERT_FALSE(definitionID.empty());
        const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",path.string()},{"machineDefinitionId",definitionID}});
        const auto instance=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}}).at("machine").To<Map>();
        Text tool,support;Array tcp,beam;std::map<Text,Text> axes;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            const auto element=entity->GetComponent("CMachineElementComponent");if(!element||iCAX::Data::to_string(element->GetProperty("MachineID").To<ID>())!=instance.at("id").To<Text>())continue;
            const auto id=iCAX::Data::to_string(entity->GetID());
            if(element->GetProperty("WorkSupport").To<bool>()){ASSERT_TRUE(support.empty());support=id;}
            if(const auto t=entity->GetComponent("CMachineToolComponent")){ASSERT_TRUE(tool.empty());tool=id;tcp=t->GetProperty("TcpLocalPosition").To<Array>();beam=t->GetProperty("BeamLocalDirection").To<Array>();}
            if(const auto j=entity->GetComponent("CMachineJointComponent");j&&j->GetProperty("JointType").To<Text>()!="fixed")axes.emplace(j->GetProperty("JointName").To<Text>(),id);
        }
        ASSERT_FALSE(tool.empty());ASSERT_FALSE(support.empty());ASSERT_EQ(5u,axes.size());
        double setupOffsetZ=-1e100;for(const auto& value:scene.Database().GetEntity(*ID::from_string(support))->GetComponent("CMachineCollisionComponent")->GetProperty("Collisions").To<Array>()){
            const auto box=value.To<Map>();const auto p=box.at("pose").To<Array>(),s=box.at("geometry").To<Map>().at("size").To<Array>();const auto matrix=iCAX::Transform::MakeLocalMatrix(0,0,p[2].To<double>()*1000,p[5].To<double>(),p[4].To<double>(),p[3].To<double>(),1,1,1);double top=matrix(2,3);for(int k=0;k<3;++k)top+=std::abs(matrix(2,k))*s[k].To<double>()*500;setupOffsetZ=std::max(setupOffsetZ,top+2.);
        }
        File stock(BRepPrimAPI_MakeBox(2.,2.,2.).Shape());Imported(scene,stock);
        auto request=GenerateRequest(scene);request["configuration"]=Map{{"machineId",instance.at("id")},{"toolId",tool},{"supportId",support},{"tcp",tcp},{"beam",beam},{"toolCollisionRepresentation",Text("Boxes")},{"placement",Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,setupOffsetZ,0.,0.,0.,1.}}};
        Invoke(&scene,"CamSimulation","ConfigureMachine",request);
        const auto boundHead=Head(scene);ASSERT_TRUE(boundHead.contains("assembly"));const auto boundAssembly=boundHead.at("assembly").To<Map>();EXPECT_EQ("MachineInstance",boundAssembly.at("source").To<Text>());EXPECT_EQ(tcp,boundAssembly.at("tcp").To<Array>());EXPECT_EQ(beam,boundAssembly.at("beam").To<Array>());EXPECT_EQ(spec.Model=="GAB-3015"?3u:spec.Head.empty()?1u:2u,boundAssembly.at("nodes").To<Array>().size());
        const auto snapshot=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});
        if(spec.Model=="GAB-3015"){
            auto legacySources=sources;for(auto& value:legacySources){auto source=value.To<Map>();if(source.at("id").To<Text>()!=definitionID)continue;source["headRoot"]=Text("future boundary absent from legacy instance");value=source;}
            BindBuiltinHeadSources(scene,legacySources);
            const auto legacy=Head(scene).at("assembly").To<Map>();EXPECT_EQ(2u,legacy.at("nodes").To<Array>().size());EXPECT_EQ("B 回转叉架",legacy.at("nodes").To<Array>().front().To<Map>().at("name").To<Text>());
            BindBuiltinHeadSources(scene,sources);EXPECT_EQ(3u,Head(scene).at("assembly").To<Map>().at("nodes").To<Array>().size());
        }
        split_chain_tests::Tree tree;std::vector<double> initial;
        const auto rawNodes=snapshot.at("nodes").To<Array>(),positions=snapshot.at("positions").To<Array>();
        for(std::size_t i=0;i<rawNodes.size();++i){const auto n=rawNodes[i].To<Map>();M::Node oracle;K::Node node;
            node.ID=oracle.ID=n.at("id").To<Text>();node.Parent=oracle.Parent=n.at("parent").To<int>();oracle.Type=n.at("type").To<Text>();node.Type=oracle.Type=="fixed"?K::JointType::Fixed:oracle.Type=="prismatic"?K::JointType::Prismatic:K::JointType::Revolute;
            node.Lower=oracle.Lower=n.at("lower").To<double>();node.Upper=oracle.Upper=n.at("upper").To<double>();node.ReferenceValue=positions[i].To<double>();
            const auto o=n.at("origin").To<Array>(),a=n.at("axis").To<Array>();for(int k=0;k<16;++k)node.Origin[k]=oracle.Origin[k]=o[k].To<double>();for(int k=0;k<3;++k)node.Axis[k]=oracle.Axis[k]=a[k].To<double>();
            tree.Model.Nodes.push_back(node);tree.Oracle.push_back(oracle);initial.push_back(positions[i].To<double>());
            if(node.ID==tool)tree.Tool=i;if(node.ID==support)tree.Support=i;
            for(const auto& [name,id]:axes)if(id==node.ID)tree.Axes[name]=i;
        }
        tree.Task.ToolFrameID=tool;tree.Task.ReferenceFrameID=support;for(int k=0;k<3;++k){tree.Task.TCP[k]=tcp[k].To<double>();tree.Task.Beam[k]=beam[k].To<double>();}
        const auto branch=[&](std::size_t endpoint){std::set<Text> result;for(int i=static_cast<int>(endpoint);i>=0;i=tree.Model.Nodes[i].Parent)for(const auto& [axis,index]:tree.Axes)if(index==i&&axis!="X"&&axis!="Y"&&axis!="Z")result.insert(axis);return result;};
        EXPECT_EQ(spec.Head,branch(tree.Tool));EXPECT_EQ(spec.Work,branch(tree.Support));
        for(const auto& [name,i]:tree.Axes){const auto expected=name=="X"||name=="A"?M::Vector{1,0,0}:name=="Y"||name=="B"?M::Vector{0,1,0}:M::Vector{0,0,1};EXPECT_EQ(expected,tree.Model.Nodes[i].Axis)<<name;}
        auto desired=initial;desired[tree.Axes.at("X")]+=10;desired[tree.Axes.at("Y")]+=7;desired[tree.Axes.at("Z")]-=15;
        for(const auto& [name,i]:tree.Axes)if(name=="A"||name=="B"||name=="C")desired[i]=name=="C"?.25:.15;
        const auto expected=split_chain_tests::Independent(tree,desired);const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineKinematicsService>();ASSERT_TRUE(service);const auto model=service->BuildModel(tree.Model);
        split_chain_tests::CheckIndependent(tree,service->Forward(model,desired,tree.Task),desired);EXPECT_EQ(5u,service->Jacobian(model,desired,tree.Task).Rank);
        K::Request ik;ik.TaskDefinition=tree.Task;ik.Goal.PositionMm=expected.PositionMm;ik.Goal.Direction=expected.Direction;ik.Seeds={initial};ik.MaxSeeds=4;ik.MaxCandidates=2;ik.MaxTimeMs=3000;
        const auto result=service->InverseCandidates(model,ik);ASSERT_EQ(K::Status::Solved,result.State)<<result.Diagnostic;ASSERT_FALSE(result.Candidates.empty());
        for(const auto& candidate:result.Candidates){const auto actual=split_chain_tests::Independent(tree,candidate.Positions);for(int k=0;k<3;++k){EXPECT_NEAR(expected.PositionMm[k],actual.PositionMm[k],.010001);EXPECT_NEAR(expected.Direction[k],actual.Direction[k],.000011);}}
        ik.Goal.PositionMm={1e6,1e6,1e6};ik.MaxTimeMs=500;EXPECT_NE(K::Status::Solved,service->InverseCandidates(model,ik).State);
        Map edits;for(const auto& [name,i]:tree.Axes)edits[tree.Model.Nodes[i].ID]=desired[i];
        const auto checked=CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",edits}});
        const auto output=Root/"output/tests/gantry-heads-20261101/native";std::filesystem::create_directories(output);std::ofstream(output/(spec.Model+"-sweep.json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        EXPECT_EQ("Clear",checked.at("collisionStatus").To<Text>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        EXPECT_EQ("MachineDeclaredBoxesAndWorkpieceFixtureMesh",checked.at("collisionScope").To<Text>());
        // Place a real fixture inside an upstream head box. The complete
        // machine checker must reject it even though it misses the nozzle.
        if(!spec.Head.empty()){
            auto node=tree.Tool;while(tree.Model.Nodes[node].Parent>=0&&tree.Model.Nodes[tree.Model.Nodes[node].Parent].Type!=K::JointType::Prismatic)node=tree.Model.Nodes[node].Parent;
            const auto e=scene.Database().GetEntity(*ID::from_string(tree.Model.Nodes[node].ID));const auto boxes=e->GetComponent("CMachineCollisionComponent")->GetProperty("Collisions").To<Array>();ASSERT_FALSE(boxes.empty());
            const auto p=boxes.front().To<Map>().at("pose").To<Array>();const auto world=M::Forward(tree.Oracle,initial);M::Vector center{p[0].To<double>()*1000,p[1].To<double>()*1000,p[2].To<double>()*1000};center=M::Transform(M::Multiply(M::Inverse(world[tree.Support]),world[node]),center);center[2]-=setupOffsetZ;
            File fixture(BRepPrimAPI_MakeBox(gp_Pnt(center[0]-1,center[1]-1,center[2]-1),2.,2.,2.).Shape());auto add=GenerateRequest(scene);add["sourcePath"]=fixture.Utf8();const auto added=Wait(scene,Invoke(&scene,"CamFixture","Import",add));ASSERT_EQ("Completed",added.at("status").To<Text>());
            const auto collision=CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",Map{{tree.Model.Nodes[tree.Axes.at("X")].ID,initial[tree.Axes.at("X")]+.01}}}});
            EXPECT_EQ("Blocked",collision.at("collisionStatus").To<Text>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(collision);
            std::ofstream(output/(spec.Model+"-housing-collision.json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(collision);
        }
        SaveProductPlanFixture(scene,output/(spec.Model+".i5cam"));
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false,true);file.Open(output/(spec.Model+".i5cam"),reopened.Database(),reopened.Resources());InitializeScene(reopened);
        const auto restored=ReadMachineSimulation(reopened,{{"setupId",Read(reopened).at("activeSetupId")}});EXPECT_EQ(snapshot.at("modelKey"),restored.at("modelKey"));EXPECT_EQ(snapshot.at("collisionDefinitions"),restored.at("collisionDefinitions"));
    }
}
TEST(FiveAxisGantryHeads, ACJointPoseSweepAndHousingCollision){gantry_head_tests::Check(gantry_head_tests::Specs[0]);}
TEST(FiveAxisGantryHeads, ABJointPoseSweepAndHousingCollision){gantry_head_tests::Check(gantry_head_tests::Specs[1]);}
TEST(FiveAxisGantryHeads, BCJointPoseSweepAndHousingCollision){gantry_head_tests::Check(gantry_head_tests::Specs[2]);}
TEST(FiveAxisGantryHeads, FourPlusOneUsesPhysicalAAndWorkpieceC){gantry_head_tests::Check(gantry_head_tests::Specs[3]);}
TEST(FiveAxisGantryHeads, ThreePlusTwoHasNoHeadRotaryJoint){gantry_head_tests::Check(gantry_head_tests::Specs[4]);}
namespace gantry_head_tests {
    void Plan(const Text& model){
        const auto relative="src/apps/five-axis-cam/resources/machines/gantry-heads/"+model+"/"+model+".urdf";
        iCAX::CAM::CMachineDescriptionResource description;Text error;ASSERT_TRUE(iCAX::CAM::LoadMachineDescription((Root/relative).string(),description,error))<<error;
        const auto tool=std::find_if(description.Elements.begin(),description.Elements.end(),[](const auto& e){return e.Tool.bValid;});const auto support=std::find_if(description.Elements.begin(),description.Elements.end(),[](const auto& e){return e.bWorkSupport;});ASSERT_NE(description.Elements.end(),tool);ASSERT_NE(description.Elements.end(),support);
        const auto tcp=tool->Tool.TcpLocalPosition,beam=tool->Tool.BeamLocalDirection;
        CheckImportedMachinePlan(relative,tool->Name,support->Name,tcp,beam,model+"-gantry-head-plan","Ready",true);
    }
}
TEST(FiveAxisGantryHeadPlans, ACFullPlanCollisionExportAndReopen){gantry_head_tests::Plan("GAC-3015");}
TEST(FiveAxisGantryHeadPlans, ABFullPlanCollisionExportAndReopen){gantry_head_tests::Plan("GAB-3015");}
TEST(FiveAxisGantryHeadPlans, BCFullPlanCollisionExportAndReopen){gantry_head_tests::Plan("GBC-3015");}
TEST(FiveAxisGantryHeadPlans, FourPlusOneFullPlanCollisionExportAndReopen){gantry_head_tests::Plan("GA-3015");}
TEST(FiveAxisGantryHeadPlans, ThreePlusTwoFullPlanCollisionExportAndReopen){gantry_head_tests::Plan("GF-3015");}
