namespace {
    struct ProductPlanMachine {ID Machine,X,Y,Tool,Support;Map Configuration,Post;};
    auto PlanAttach(const auto& entity,const Text& name,const Map& fields){const auto component=entity->AddComponent(name);for(const auto& [key,value]:fields){Text error;if(!component->SetProperty(key,value,error))throw std::runtime_error(error);}return component;}
    Array PlanBox(double x,double y,double z,double size=.001){return {Map{{"enabled",true},{"name",Text("Authored test collision box")},{"pose",Array{x,y,z,0.,0.,0.}},{"geometry",Map{{"type",Text("box")},{"size",Array{size,size,size}}}}}};}
    ProductPlanMachine CreateProductPlanMachine(Scene& scene,bool declaredFixedHead=false){
        ProductPlanMachine m;m.Machine=iCAX::Data::GenerateNewUUID();m.X=iCAX::Data::GenerateNewUUID();m.Y=iCAX::Data::GenerateNewUUID();m.Tool=iCAX::Data::GenerateNewUUID();m.Support=iCAX::Data::GenerateNewUUID();
        const auto root=scene.Database().CreateEntity(m.Machine);PlanAttach(root,"CMachineInstanceComponent",{{"Name",Text("Native axis-plan acceptance gantry")}});PlanAttach(root,"CTransformComponent",{});
        const auto part=[&](ID id,ID parent,const Text& name,Array colliders,Array axis={},double position=0){const auto entity=scene.Database().CreateEntity(id);PlanAttach(entity,"CTransformComponent",{{"ParentEntityID",parent}});PlanAttach(entity,"CMachineElementComponent",{{"MachineID",m.Machine},{"Name",name},{"ElementKind",Text("part")},{"RestPose",Array{0.,0.,0.,0.,0.,0.}}});PlanAttach(entity,"CMachineCollisionComponent",{{"Collisions",colliders}});
            if(!axis.empty()){const auto joint=PlanAttach(entity,"CMachineJointComponent",{{"JointName",name},{"JointType",Text("prismatic")},{"Axis",axis},{"LowerLimit",-200.},{"UpperLimit",200.},{"VelocityLimit",100.}});Text error;if(!joint->SetProperty("Position",position,error))throw std::runtime_error(error);}return entity;};
        part(m.X,m.Machine,"X",PlanBox(0,1,0),{1.,0.,0.});part(m.Y,m.X,"Y",PlanBox(0,2,0),{0.,1.,0.});part(m.Tool,m.Y,"Z tool",PlanBox(0,0,.01),{0.,0.,1.},30);part(m.Support,m.Machine,"Work support",PlanBox(0,0,-5.095,10));
        const auto zAxis=m.Tool;
        if(declaredFixedHead){
            // A tool boundary must declare a real fixed nozzle, rather than
            // treating the external Z slide as an undeclared cutting head.
            File nozzle(BRepPrimAPI_MakeBox(.1,.1,.1).Shape());const auto imported=Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,nozzle)));
            if(imported.at("status").To<Text>()!="Completed")throw std::runtime_error("Declared fixture nozzle import failed");
            const auto visual=Head(scene).at("previewRef").To<Map>();const auto definition=Text("native-declared-fixed-head");
            const auto instance=root->GetComponent("CMachineInstanceComponent");Text declarationError;if(!instance->SetProperty("MachineDefinitionID",definition,declarationError))throw std::runtime_error("Fixture machine definition missing: "+declarationError);
            m.Tool=iCAX::Data::GenerateNewUUID();const auto nozzlePart=part(m.Tool,zAxis,"Declared fixed nozzle",PlanBox(0,0,.01));
            PlanAttach(nozzlePart,"CMachineLinkComponent",{{"LinkName",Text("Declared fixed nozzle")}});
            PlanAttach(nozzlePart,"CRenderInstanceComponent",{{"GeometryResourceID",visual.at("url")},{"GeometryResourceVersion",static_cast<UInt>(std::stoull(visual.at("version").To<Text>()))},{"GeometryKind",UInt(1)}});
            BindBuiltinHeadSources(scene,Array{Map{{"id",definition},{"kind",Text("3+2")},{"headRoot",Text("Declared fixed nozzle")},{"headAxes",Array{}},{"workpieceAxes",Array{}}}});
        }
        const auto text=[](ID id){return iCAX::Data::to_string(id);};const Array identity{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};
        m.Configuration={{"machineId",text(m.Machine)},{"toolId",text(m.Tool)},{"supportId",text(m.Support)},{"placement",identity},{"tcp",Array{0.,0.,0.}},{"beam",Array{0.,0.,-1.}},{"toolCollisionRepresentation",Text("Boxes")}};
        auto configure=GenerateRequest(scene);configure["configuration"]=m.Configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
        Array axes;for(const auto& [id,word]:std::vector<std::pair<ID,Text>>{{m.X,"X"},{m.Y,"Y"},{zAxis,"Z"}})axes.emplace_back(Map{{"jointId",text(id)},{"word",word},{"sign",1.},{"offset",0.},{"maxAcceleration",200.},{"maxJerk",1000.}});
        m.Post={{"name",Text("Uncalibrated native plan JSON")},{"format",Text("JointJSON")},{"extension",Text("json")},{"axes",axes},{"header",Text("G90\nG93")},{"footer",Text("M30")},{"processOn",Text("M3")},{"processOff",Text("M5")},{"motion",Text("G1 {axes} F{feed}")},{"cutFeedMmMin",600.},{"travelFeedMmMin",1200.}};
        Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",text(m.Machine)},{"postprocessor",m.Post}});return m;
    }
    Map ProductMachinePlan(Scene& scene){return ReadMachineMotionPlan(scene,{{"setupId",Read(scene).at("activeSetupId")}});}
    Map MachineProjectionBody(Scene& scene,const Map& metadata,const Text& key){
        EXPECT_FALSE(metadata.contains("frames"));EXPECT_FALSE(metadata.contains("inputKey"));EXPECT_FALSE(metadata.contains("content"));EXPECT_FALSE(metadata.contains("motion"));
        // Includes the complete fixed machine model key, but no trajectory.
        EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(metadata).size(),1024u*1024u);
        const auto reference=metadata.at(key).To<Map>();iCAX::Resource::CResourceAccessService access(scene.Resources());
        iCAX::Resource::CResourceRequest request;request.URL=reference.at("url").To<Text>();request.Headers["ICAX-Resource-Version"]=reference.at("version").To<Text>();
        const auto response=access.Request(request);if(response.nStatus!=200)throw std::runtime_error("Machine projection Resource GET failed: "+std::to_string(response.nStatus));
        EXPECT_EQ(request.Headers.at("ICAX-Resource-Version"),iCAX::Resource::GetResourceHeader(response.Headers,"ICAX-Resource-Version").value_or(Text{}));
        if(response.Body.Size()<12||!flatbuffers::BufferHasIdentifier(response.Body.Data(),"ICMJ"))throw std::runtime_error("Invalid machine projection envelope");
        const auto table=flatbuffers::GetRoot<flatbuffers::Table>(response.Body.Data());
        if(table->GetField<std::uint32_t>(4,0)!=1)throw std::runtime_error("Invalid machine projection version");
        const auto json=table->GetPointer<const flatbuffers::String*>(6);if(!json)throw std::runtime_error("Machine projection JSON absent");
        const auto body=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json->str()).To<Map>();EXPECT_FALSE(body.contains("inputKey"));
        const auto info=scene.Resources().GetInfo(request.URL,std::stoull(request.Headers.at("ICAX-Resource-Version")));EXPECT_TRUE(info&&info->IsRuntimeOnly());
        return body;
    }
    Map MachineProgram(Scene& scene,const Map& request){const auto metadata=Invoke(&scene,"CamSimulation","ExportMachineProgram",request);const auto body=MachineProjectionBody(scene,metadata,"programResourceRef");for(const auto key:{"modelKey","format","extension","instructionCount","machineReady"})EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{key,metadata.at(key)}}),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{key,body.at(key)}}));EXPECT_EQ(metadata.at("modelKey"),body.at("motion").To<Map>().at("machine").To<Map>().at("modelKey"));return body;}
    Map WaitMachinePlan(Scene& scene,const Map& task){const auto started=std::chrono::steady_clock::now(),until=started+std::chrono::seconds(600);auto logged=started;for(;;){auto result=Invoke(&scene,"CamSimulation","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});if(result.at("status").To<Text>()!="Running"){if(result.at("status").To<Text>()=="Completed"){const auto receipt=result.at("result").To<Map>();EXPECT_EQ(3u,receipt.size());EXPECT_FALSE(receipt.contains("frames"));const auto metadata=Invoke(&scene,"CamSimulation","ReadMachineAttempt",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});result["result"]=MachineProjectionBody(scene,metadata,"planResourceRef");};return result;}const auto now=std::chrono::steady_clock::now();if(now-logged>=std::chrono::seconds(5)){logged=now;std::cerr<<"Machine plan "<<std::chrono::duration<double>(now-started).count()<<"s "<<(result.contains("machineProgress")?iCAX::Data::VariantSerializer::Serialize(result.at("machineProgress")):"capturing")<<std::endl;}if(now>until)throw std::runtime_error("Product machine plan timeout");std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
    void PrepareProductPlanPaths(Scene& scene){const auto created=ConfirmWithPolicies(scene);const auto ids=created.at("createdIntentIds").To<Array>();ZeroGenerationPolicies(scene,ids);const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));if(generated.at("status").To<Text>()!="Completed")throw std::runtime_error("Native generated contour failed");}
    void SaveProductPlanFixture(Scene& scene,const std::filesystem::path& path){std::filesystem::create_directories(path.parent_path());iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Machine motion native acceptance";file.Save(path,info,scene.Database(),scene.Resources());}
}
TEST(FiveAxisMachinePlanPolicy, WorkloadBudgetCoversSourceAndCollisionWorkWithoutChangingVerificationQuality)
{
    namespace P=iCAX::FiveAxisCAM::MachinePlanPolicy;
    const auto budgetSmall=P::ForWorkload(10,4),budgetActual=P::ForWorkload(5444,75),fewerPairs=P::ForWorkload(5444,10),budgetLarge=P::ForWorkload(std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max());
    EXPECT_DOUBLE_EQ(180,budgetSmall.MaximumSeconds);EXPECT_EQ(100000u,budgetSmall.MaximumVerificationSamples);
    EXPECT_DOUBLE_EQ(400.25,budgetActual.MaximumSeconds);EXPECT_EQ(348416u,budgetActual.MaximumVerificationSamples);EXPECT_GT(budgetActual.MaximumSeconds,fewerPairs.MaximumSeconds);
    EXPECT_DOUBLE_EQ(600,budgetLarge.MaximumSeconds);EXPECT_EQ(2000000u,budgetLarge.MaximumVerificationSamples);
}
TEST(FiveAxisMachinePlanPolicy, BudgetCancellationAndUnresolvedNeverBorrowARejectedCandidateCollision)
{
    namespace MM=iCAX::GeometryServices::MachineMotion;namespace P=iCAX::FiveAxisCAM::MachinePlanPolicy;
    MM::Result outcome;outcome.FailedStation=3609;outcome.FailedEdge=3609;
    for(const auto status:{MM::Status::BudgetExhausted,MM::Status::Cancelled,MM::Status::Unresolved,MM::Status::NoCandidate,MM::Status::Solved}){
        outcome.State=status;outcome.Diagnostic="Earlier candidate collision";EXPECT_FALSE(P::IsAuthoritativeCollision(outcome,"Blocked",outcome.Diagnostic,"3609"));
    }
    outcome.State=MM::Status::Rejected;outcome.Diagnostic="Final state collision";
    EXPECT_TRUE(P::IsAuthoritativeCollision(outcome,"Blocked",outcome.Diagnostic,"3609"));
    EXPECT_FALSE(P::IsAuthoritativeCollision(outcome,"Blocked","Earlier candidate collision","3609"));
    EXPECT_FALSE(P::IsAuthoritativeCollision(outcome,"Blocked",outcome.Diagnostic,"3608"));
    EXPECT_FALSE(P::IsAuthoritativeCollision(outcome,"Unchecked",outcome.Diagnostic,"3609"));
    outcome.FailedStation=outcome.FailedEdge=std::numeric_limits<std::size_t>::max();EXPECT_TRUE(P::IsAuthoritativeCollision(outcome,"Blocked",outcome.Diagnostic,"Initial"));
}
TEST(FiveAxisMachinePlan, DeclaredBoxGantryPlanIsPersistentCurrentAndExportConsumesExactStoredAxes)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    const auto missing=Invoke(&scene,"CamSimulation","ReadMachinePlan",{{"setupId",Read(scene).at("activeSetupId")}});EXPECT_EQ("Missing",missing.at("validity").To<Text>());EXPECT_FALSE(missing.contains("planResourceRef"));
    const auto output=std::filesystem::path(L"C:/icax-copp-motion-20261026/product-fixtures");SaveProductPlanFixture(scene,output/L"machine-plan-before.i5cam");
    EXPECT_EQ(iCAX::Database::EPropertyChangePolicy::Observable,scene.Database().GetMetaRegistry()->GetPropertyChangePolicyByName(CFiveAxisSetupComponent::S_ClassName,"MachineMotionPlanData"));const auto request=GenerateRequest(scene);const auto history=scene.Database().GetUndoArray().size();const auto beforePaths=Generated(scene);
    const auto task=Invoke(&scene,"CamSimulation","PlanMachine",request);EXPECT_EQ(history,scene.Database().GetUndoArray().size())<<"Plan capture";EXPECT_EQ(task.at("taskId"),Invoke(&scene,"CamSimulation","PlanMachine",request).at("taskId"));const auto terminal=WaitMachinePlan(scene,task);
    ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);const auto result=terminal.at("result").To<Map>();ASSERT_EQ("Ready",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);EXPECT_EQ("Clear",result.at("collisionStatus").To<Text>());EXPECT_EQ("MachineDeclaredBoxesAndWorkpieceFixtureMesh",result.at("collisionScope").To<Text>());EXPECT_EQ("MachineDeclaredBoxesV5",result.at("collisionAlgorithm").To<Text>());
    const auto plan=ProductMachinePlan(scene);ASSERT_EQ("Current",plan.at("validity").To<Text>());EXPECT_TRUE(plan.at("playbackAllowed").To<bool>());EXPECT_FALSE(plan.at("machineReady").To<bool>());EXPECT_EQ("SampledFK;CertifiedPolynomialDynamics",plan.at("interpolationCoverage").To<Text>());EXPECT_EQ("VelocityAccelerationJerk",plan.at("timingQualification").To<Text>());EXPECT_EQ("QuinticNormalized",plan.at("interpolation").To<Text>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(beforePaths,Generated(scene));
    const auto budget=plan.at("planningBudget").To<Map>();EXPECT_EQ("SourceStationsAndCollisionPairsV1",budget.at("policy").To<Text>());EXPECT_GE(budget.at("maximumSeconds").To<double>(),180);EXPECT_LE(budget.at("maximumSeconds").To<double>(),600);EXPECT_GE(std::stoull(budget.at("maximumVerificationSamples").To<Text>()),100000u);EXPECT_EQ(plan.at("sourceStationCount"),budget.at("sourceStationCount"));
    const auto frames=plan.at("frames").To<Array>();ASSERT_GT(frames.size(),3u);EXPECT_FALSE(frames.front().To<Map>().at("laser").To<bool>());EXPECT_EQ("Approach",frames.front().To<Map>().at("role").To<Text>());
    const auto nodes=plan.at("nodes").To<Array>();std::map<Text,std::size_t> nodeIndices;for(std::size_t i=0;i<nodes.size();++i)nodeIndices.emplace(nodes[i].To<Map>().at("id").To<Text>(),i);
    for(const auto& v:frames){const auto f=v.To<Map>();const auto joints=f.at("joints").To<Array>();EXPECT_EQ(nodes.size(),joints.size());EXPECT_TRUE(f.contains("sourceStation"));EXPECT_TRUE(f.contains("sourceFraction"));EXPECT_LE(f.at("positionErrorMm").To<double>(),.0100001);ASSERT_TRUE(f.contains("actualToolPose"));const auto pose=f.at("actualToolPose").To<Array>();ASSERT_EQ(16u,pose.size());EXPECT_NEAR(joints[nodeIndices.at(iCAX::Data::to_string(machine.X))].To<double>(),pose[3].To<double>(),1e-8);EXPECT_NEAR(joints[nodeIndices.at(iCAX::Data::to_string(machine.Y))].To<double>(),pose[7].To<double>(),1e-8);EXPECT_NEAR(joints[nodeIndices.at(iCAX::Data::to_string(machine.Tool))].To<double>(),pose[11].To<double>(),1e-8);const auto beam=f.at("actualBeamDirection").To<Array>();EXPECT_NEAR(0,beam[0].To<double>(),1e-9);EXPECT_NEAR(0,beam[1].To<double>(),1e-9);EXPECT_NEAR(-1,beam[2].To<double>(),1e-9);EXPECT_FALSE(f.contains("rotation"));if(f.at("role").To<Text>()=="Approach")EXPECT_FALSE(f.at("laser").To<bool>());}
    const auto projection=Invoke(&scene,"CamScene","ReadState");const auto setup=projection.at("setups").To<Array>().front().To<Map>();EXPECT_FALSE(setup.at("machineMotionPlanRef").To<Map>().empty());const auto wirePlan=Invoke(&scene,"CamSimulation","ReadMachinePlan",{{"setupId",projection.at("activeSetupId")}});const auto hydrated=MachineProjectionBody(scene,wirePlan,"planResourceRef");EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(hydrated));
    EXPECT_EQ(wirePlan.at("planResourceRef"),Invoke(&scene,"CamSimulation","ReadMachinePlan",{{"setupId",projection.at("activeSetupId")}}).at("planResourceRef"));
    const auto snapshot=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}});const Map exportRequest{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",snapshot.at("modelKey")}};const auto exported=MachineProgram(scene,exportRequest);const auto instructions=exported.at("motion").To<Map>().at("instructions").To<Array>();ASSERT_EQ(frames.size(),instructions.size());for(std::size_t i=0;i<frames.size();++i){EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(frames[i].To<Map>().at("joints").To<Array>()),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(instructions[i].To<Map>().at("joints").To<Array>()));EXPECT_EQ(i>0&&frames[i-1].To<Map>().at("laser").To<bool>(),instructions[i].To<Map>().at("process").To<bool>());}EXPECT_EQ(plan,ProductMachinePlan(scene));
    auto destinationRequest=exportRequest;const auto destination=(output/L"machine-program.json").string();destinationRequest["destination"]=destination;const auto written=Invoke(&scene,"CamSimulation","ExportMachineProgram",destinationRequest);EXPECT_FALSE(written.contains("content"));EXPECT_FALSE(written.contains("motion"));EXPECT_FALSE(written.contains("programResourceRef"));EXPECT_EQ(destination,written.at("path").To<Text>());std::ifstream savedProgram(destination,std::ios::binary);EXPECT_EQ(exported.at("content").To<Text>(),Text((std::istreambuf_iterator<char>(savedProgram)),{}));
    auto changedPost=machine.Post;changedPost["cutFeedMmMin"]=601.;Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"postprocessor",changedPost}});EXPECT_EQ("Stale",ProductMachinePlan(scene).at("validity").To<Text>());const auto stale=Invoke(&scene,"CamSimulation","ReadMachinePlan",{{"setupId",Read(scene).at("activeSetupId")}});EXPECT_EQ("Stale",stale.at("validity").To<Text>());EXPECT_FALSE(stale.contains("planResourceRef"));EXPECT_FALSE(stale.contains("frames"));EXPECT_THROW(Invoke(&scene,"CamSimulation","ExportMachineProgram",{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",snapshot.at("modelKey")}}),std::exception);ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(plan,ProductMachinePlan(scene));
    SaveProductPlanFixture(scene,output/L"machine-plan-current.i5cam");iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false);file.Open(output/L"machine-plan-current.i5cam",reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(plan,ProductMachinePlan(reopened));
}
TEST(FiveAxisMachinePlan, BlankAxisAndFeedLimitsRemainUnboundedAndExplicitLimitsStillApply)
{
    Scene scene;PrepareProductPlanPaths(scene);auto machine=CreateProductPlanMachine(scene);auto post=machine.Post;auto axes=post.at("axes").To<Array>();
    for(auto& value:axes){auto axis=value.To<Map>();axis["maxAcceleration"]=Text(" ");axis.erase("maxJerk");value=axis;}
    auto bounded=axes.front().To<Map>();bounded["maxJerk"]=50.;axes.front()=bounded;post["axes"]=axes;post["cutFeedMmMin"]=Text("");post.erase("travelFeedMmMin");
    const auto configured=Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"postprocessor",post}}).at("postprocessor").To<Map>();
    EXPECT_FALSE(configured.contains("cutFeedMmMin"));EXPECT_FALSE(configured.contains("travelFeedMmMin"));for(const auto& value:configured.at("axes").To<Array>())EXPECT_FALSE(value.To<Map>().contains("maxAcceleration"));
    const auto task=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",task.at("status").To<Text>());const auto result=task.at("result").To<Map>();ASSERT_EQ("Ready",result.at("status").To<Text>())<<result.at("reason").To<Text>();
    EXPECT_EQ("ConfiguredAxisLimits;BlankIsUnbounded",result.at("timingQualification").To<Text>());EXPECT_FALSE(result.at("machineReady").To<bool>());std::size_t finiteJerks=0;
    for(const auto& value:result.at("axisLimits").To<Array>()){const auto limit=value.To<Map>();EXPECT_EQ("Unbounded",limit.at("acceleration").To<Text>());if(!limit.at("jerk").Is<Text>())++finiteJerks;}EXPECT_EQ(1u,finiteJerks);
    const auto snapshot=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});const auto exported=MachineProgram(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",snapshot.at("modelKey")}});
    EXPECT_EQ(result.at("axisLimits"),exported.at("motion").To<Map>().at("axisLimits"));
    auto invalid=configured;auto invalidAxes=invalid.at("axes").To<Array>();auto axis=invalidAxes.front().To<Map>();axis["maxAcceleration"]=0.;invalidAxes.front()=axis;invalid["axes"]=invalidAxes;
    EXPECT_THROW(Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"postprocessor",invalid}}),std::exception);
}
TEST(FiveAxisMachinePlan, PlacementPreviewChecksCompleteMotionWithoutChangingTheJobAndCannotChangeCalibration)
{
    Scene scene;PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    const auto first=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",first.at("status").To<Text>());ASSERT_EQ("Ready",first.at("result").To<Map>().at("status").To<Text>());
    const auto before=Read(scene),stored=ProductMachinePlan(scene);const auto history=scene.Database().GetUndoArray().size();
    auto candidate=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",before.at("activeSetupId")}}).at("configuration").To<Map>();auto placement=candidate.at("placement").To<Array>();placement[3]=5.;candidate["placement"]=placement;
    auto request=GenerateRequest(scene);request["previewConfiguration"]=candidate;request["previewMaximumSeconds"]=60.;
    const auto receipt=Invoke(&scene,"CamSimulation","PlanMachine",request);EXPECT_EQ(receipt.at("taskId"),Invoke(&scene,"CamSimulation","PlanMachine",request).at("taskId"));
    const auto terminal=WaitMachinePlan(scene,receipt);ASSERT_EQ("Completed",terminal.at("status").To<Text>());const auto preview=terminal.at("result").To<Map>();ASSERT_EQ("Ready",preview.at("status").To<Text>())<<preview.at("reason").To<Text>();
    EXPECT_TRUE(preview.at("placementPreview").To<bool>());EXPECT_EQ("Preview",preview.at("validity").To<Text>());EXPECT_EQ("Clear",preview.at("collisionStatus").To<Text>());EXPECT_EQ(candidate,preview.at("configuration").To<Map>());EXPECT_FALSE(preview.at("frames").To<Array>().empty());EXPECT_GT(std::stoull(preview.at("checkedMotionCount").To<Text>()),1u);
    EXPECT_EQ(before.at("expected"),Read(scene).at("expected"));EXPECT_EQ(before.at("setups"),Read(scene).at("setups"));EXPECT_EQ(stored,ProductMachinePlan(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    auto invalid=GenerateRequest(scene);auto altered=candidate;altered["tcp"]=Array{1.,0.,0.};invalid["previewConfiguration"]=altered;EXPECT_THROW(Invoke(&scene,"CamSimulation","PlanMachine",invalid),std::exception);
    invalid=GenerateRequest(scene);placement[3]=100000.;altered=candidate;altered["placement"]=placement;invalid["previewConfiguration"]=altered;EXPECT_THROW(Invoke(&scene,"CamSimulation","PlanMachine",invalid),std::exception);
    request=GenerateRequest(scene);request["previewConfiguration"]=candidate;const auto running=Invoke(&scene,"CamSimulation","PlanMachine",request);Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",running.at("epoch")},{"taskId",running.at("taskId")}});const auto cancelled=WaitMachinePlan(scene,running);EXPECT_EQ("Cancelled",cancelled.at("status").To<Text>());EXPECT_EQ(stored,ProductMachinePlan(scene));EXPECT_EQ(before.at("expected"),Read(scene).at("expected"));
}
TEST(FiveAxisMachineMount, DeclaredEndpointsMountTheWholeAssemblyOnceAndRejectUnsafeOrOversizeLoading)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(10,20,10).Shape());Imported(scene,source);
    const auto machine=CreateProductPlanMachine(scene);
    const auto support=scene.Database().GetEntity(machine.Support),tool=scene.Database().GetEntity(machine.Tool);
    Text error;
    ASSERT_TRUE(support->GetComponent("CMachineElementComponent")->SetProperty("WorkSupport",true,error));
    ASSERT_TRUE(support->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",PlanBox(0,0,-.495,1),error));
    PlanAttach(tool,"CMachineToolComponent",{{"ToolName",Text("Physical nozzle")},{"ToolType",Text("laser_cutting_head")},{"TcpLocalPosition",Array{0.,0.,0.}},{"BeamLocalDirection",Array{0.,0.,-1.}}});
    ASSERT_TRUE(tool->GetComponent("CMachineJointComponent")->SetProperty("Position",100.,error));
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-20,-30,-30),40,60,20).Shape());
    auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();
    const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",import));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    const auto fixtureID=imported.at("result").To<Map>().at("fixtureId").To<Text>();
    const auto setupEntity=scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()));
    ASSERT_TRUE(setupEntity->GetComponent("CFiveAxisSetupComponent")->SetProperty("MachineSetup",Map{},error));
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();auto request=GenerateRequest(scene);request["machineId"]=iCAX::Data::to_string(machine.Machine);
    const auto mounted=Invoke(&scene,"CamSimulation","MountMachine",request),configuration=mounted.at("configuration").To<Map>(),proof=mounted.at("mounting").To<Map>();
    EXPECT_EQ(mounted,Invoke(&scene,"CamSimulation","MountMachine",request));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ("Clear",proof.at("collisionStatus").To<Text>());EXPECT_FALSE(proof.at("calibrated").To<bool>());EXPECT_EQ(2u,proof.at("includedEntityIds").To<Array>().size());
    EXPECT_EQ("AuthoredMachineEndpointsAndOwnMeshBounds",proof.at("source").To<Text>());EXPECT_EQ("AuthoredPhysicalNozzle",proof.at("toolTCPSource").To<Text>());
    const auto placement=configuration.at("placement").To<Array>();EXPECT_NEAR(0,placement[3].To<double>(),1e-7);EXPECT_NEAR(0,placement[7].To<double>(),1e-7);EXPECT_NEAR(35,placement[11].To<double>(),1e-7);EXPECT_NEAR(5,proof.at("surfaceZMm").To<double>(),1e-7);
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_TRUE(Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>().empty());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(configuration,Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
    // A currently penetrating initial pose cannot be "repaired" by mounting.
    ASSERT_TRUE(tool->GetComponent("CMachineJointComponent")->SetProperty("Position",10.,error));request=GenerateRequest(scene);request["machineId"]=iCAX::Data::to_string(machine.Machine);
    EXPECT_THROW(Invoke(&scene,"CamSimulation","MountMachine",request),std::exception);EXPECT_EQ(configuration,Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
    ASSERT_TRUE(tool->GetComponent("CMachineJointComponent")->SetProperty("Position",100.,error));
    const auto fixtureEntity=scene.Database().GetEntity(*ID::from_string(fixtureID));ASSERT_TRUE(fixtureEntity->GetComponent("CFiveAxisFixtureComponent")->SetProperty("Transform",Array{1.,0.,0.,2000.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.},error));
    request=GenerateRequest(scene);request["machineId"]=iCAX::Data::to_string(machine.Machine);EXPECT_THROW(Invoke(&scene,"CamSimulation","MountMachine",request),std::exception);
    EXPECT_EQ(configuration,Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
}
TEST(FiveAxisMachineMount, ExplicitConfiguredEndpointsAreUsedWithoutTemplateMetadata)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(10,20,10).Shape());Imported(scene,source);
    const auto machine=CreateProductPlanMachine(scene);
    const auto support=scene.Database().GetEntity(machine.Support),tool=scene.Database().GetEntity(machine.Tool);
    Text error;
    // The selected endpoints are explicitly configured, with no tool component
    // or work-support marker on either node. Preserve their TCP and beam.
    auto explicitConfig=machine.Configuration;
    explicitConfig["tcp"]=Array{1.,2.,-3.};explicitConfig["beam"]=Array{0.,-1.,-1.};
    auto configure=GenerateRequest(scene);configure["configuration"]=explicitConfig;
    const auto saved=Invoke(&scene,"CamSimulation","ConfigureMachine",configure).at("configuration").To<Map>();
    ASSERT_TRUE(support->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",PlanBox(0,0,-.495,1),error));
    ASSERT_TRUE(tool->GetComponent("CMachineJointComponent")->SetProperty("Position",100.,error));
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    auto request=GenerateRequest(scene);request["machineId"]=iCAX::Data::to_string(machine.Machine);
    const auto mounted=Invoke(&scene,"CamSimulation","MountMachine",request),configuration=mounted.at("configuration").To<Map>(),proof=mounted.at("mounting").To<Map>();
    EXPECT_EQ(saved.at("toolId"),configuration.at("toolId"));EXPECT_EQ(saved.at("supportId"),configuration.at("supportId"));
    EXPECT_EQ(saved.at("tcp"),configuration.at("tcp"));EXPECT_EQ(saved.at("beam"),configuration.at("beam"));
    EXPECT_EQ("ConfiguredMachineEndpointsAndOwnMeshBounds",proof.at("source").To<Text>());EXPECT_EQ("ConfiguredToolTCP",proof.at("toolTCPSource").To<Text>());
    EXPECT_EQ("Clear",proof.at("collisionStatus").To<Text>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(saved,ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
    // A draft may be below the table and cannot be saved with ConfigureMachine;
    // the mount operation must use its explicit TCP while finding a safe pose.
    auto draft=saved;draft["tcp"]=Array{4.,5.,-6.};
    auto explicitRequest=GenerateRequest(scene);explicitRequest["machineId"]=iCAX::Data::to_string(machine.Machine);explicitRequest["configuration"]=draft;
    const auto explicitMount=Invoke(&scene,"CamSimulation","MountMachine",explicitRequest);
    EXPECT_EQ(draft.at("tcp"),explicitMount.at("configuration").To<Map>().at("tcp"));
    EXPECT_EQ("ExplicitToolTCP",explicitMount.at("mounting").To<Map>().at("toolTCPSource").To<Text>());
    const auto mountedConfig=explicitMount.at("configuration").To<Map>();const auto mountedHistory=scene.Database().GetUndoArray().size();
    auto foreign=draft;foreign["toolId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    explicitRequest=GenerateRequest(scene);explicitRequest["machineId"]=iCAX::Data::to_string(machine.Machine);explicitRequest["configuration"]=foreign;
    EXPECT_THROW(Invoke(&scene,"CamSimulation","MountMachine",explicitRequest),std::exception);
    EXPECT_EQ(mountedHistory,scene.Database().GetUndoArray().size());EXPECT_EQ(mountedConfig,ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
}
TEST(FiveAxisMachineMount, WholeAssemblyPlacementRejectsTablePenetrationAndSweptBoxCollisionsAtomically)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(10,20,10).Shape());Imported(scene,source);const auto machine=CreateProductPlanMachine(scene);Text error;
    const auto support=scene.Database().GetEntity(machine.Support),tool=scene.Database().GetEntity(machine.Tool);
    ASSERT_TRUE(support->GetComponent("CMachineElementComponent")->SetProperty("WorkSupport",true,error));
    ASSERT_TRUE(support->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",PlanBox(0,0,-.495,1),error));
    PlanAttach(tool,"CMachineToolComponent",{{"TcpLocalPosition",Array{0.,0.,0.}},{"BeamLocalDirection",Array{0.,0.,-1.}}});
    ASSERT_TRUE(tool->GetComponent("CMachineJointComponent")->SetProperty("Position",150.,error));
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-20,-80,-30),40,160,20).Shape());auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();
    const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",import));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    const auto fixtureID=imported.at("result").To<Map>().at("fixtureId").To<Text>();
    ASSERT_TRUE(scene.Database().GetEntity(*ID::from_string(fixtureID))->GetComponent("CFiveAxisFixtureComponent")->SetProperty("CollisionEnabled",false,error));
    auto request=GenerateRequest(scene);request["machineId"]=iCAX::Data::to_string(machine.Machine);const auto mounted=Invoke(&scene,"CamSimulation","MountMachine",request).at("configuration").To<Map>();
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto check=[&](const Map& c){return Invoke(&scene,"CamSimulation","CheckMachinePlacement",{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"configuration",c}});};
    const auto apply=[&](const Map& c){auto r=GenerateRequest(scene);r["configuration"]=c;return Invoke(&scene,"CamSimulation","ConfigureMachine",r);};
    auto p=mounted.at("placement").To<Array>();EXPECT_NEAR(35,p[11].To<double>(),1e-7);
    auto moved=mounted;p[3]=100.;moved["placement"]=p;EXPECT_EQ("Clear",check(moved).at("collisionStatus").To<Text>());apply(moved);
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(mounted,ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
    const auto unchanged=scene.Database().GetUndoArray().size();
    auto below=mounted;p=mounted.at("placement").To<Array>();p[11]=34.9;below["placement"]=p;EXPECT_THROW(check(below),std::exception);
    EXPECT_THROW(apply(below),std::exception);
    auto outside=mounted;p=mounted.at("placement").To<Array>();p[3]=490.;outside["placement"]=p;EXPECT_THROW(apply(outside),std::exception);
    EXPECT_EQ(unchanged,scene.Database().GetUndoArray().size());
    // Thin wall: both translation endpoints are clear; the intervening path
    // is blocked by tooling whose machining collision flag is disabled.
    auto wall=PlanBox(.08,0,.055).front().To<Map>();wall["geometry"]=Map{{"type",Text("box")},{"size",Array{.002,.3,.1}}};
    const auto x=scene.Database().GetEntity(machine.X);ASSERT_TRUE(x->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",Array{wall},error));
    const auto wallHistory=scene.Database().GetUndoArray().size();
    auto through=mounted;p=mounted.at("placement").To<Array>();p[3]=160.;through["placement"]=p;EXPECT_THROW(check(through),std::exception);
    EXPECT_THROW(apply(through),std::exception);
    // A 180 degree rotation has clear endpoints but its 90 degree midpoint
    // sweeps the fixture through the same wall.
    auto rotated=mounted;p=mounted.at("placement").To<Array>();p[0]=-1.;p[5]=-1.;rotated["placement"]=p;EXPECT_THROW(check(rotated),std::exception);
    EXPECT_THROW(apply(rotated),std::exception);
    // A full turn ends at the starting matrix. Its actual travelled angle
    // must still be checked rather than collapsing to a zero-length edit.
    Map fullTurn{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"configuration",mounted},{"placementPivot",Array{0.,0.,0.}},
        {"placementRotation",Map{{"axis",Array{0.,0.,1.}},{"angle",2*std::numbers::pi}}}};
    EXPECT_THROW(Invoke(&scene,"CamSimulation","CheckMachinePlacement",fullTurn),std::exception);
    auto fullTurnCommit=fullTurn;fullTurnCommit["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamSimulation","ConfigureMachine",fullTurnCommit),std::exception);
    EXPECT_EQ(wallHistory,scene.Database().GetUndoArray().size());EXPECT_EQ(mounted,ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>());
    ASSERT_TRUE(x->GetComponent("CMachineCollisionComponent")->SetProperty("Collisions",PlanBox(0,1,0),error));
    fullTurn["expected"]=Read(scene).at("expected");EXPECT_EQ("Clear",Invoke(&scene,"CamSimulation","CheckMachinePlacement",fullTurn).at("collisionStatus").To<Text>());
    EXPECT_EQ("Clear",check(rotated).at("collisionStatus").To<Text>());apply(rotated);EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
}
TEST(FiveAxisMachinePlan, CancelAndRealWholeMachineCollisionNeverReplaceAnExistingPlanWithPartialFrames)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto first=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",first.at("status").To<Text>());const auto original=ProductMachinePlan(scene);ASSERT_EQ("Current",original.at("validity").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(original);const auto ref=original.at("revision");
    const auto task=Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene));Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});const auto cancelled=WaitMachinePlan(scene,task);EXPECT_EQ("Cancelled",cancelled.at("status").To<Text>())<<(cancelled.contains("error")?cancelled.at("error").To<Text>():Text{});EXPECT_EQ(original,ProductMachinePlan(scene));
    const auto undo=scene.Database().BeginUndoCommand("Change actual collision box");auto& tx=scene.Database().BeginTransaction("Block real target mesh");tx.ModifyComponent(machine.Tool,"CMachineCollisionComponent",{{"Collisions",PlanBox(0,0,0,10)}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));undo->End();EXPECT_EQ("Stale",ProductMachinePlan(scene).at("validity").To<Text>());const auto rejected=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",rejected.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(rejected);const auto bad=rejected.at("result").To<Map>();EXPECT_EQ("Blocked",bad.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(bad);EXPECT_FALSE(bad.at("playbackAllowed").To<bool>());EXPECT_TRUE(bad.at("frames").To<Array>().empty());EXPECT_EQ(ref,ProductMachinePlan(scene).at("revision"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,ProductMachinePlan(scene));
}
TEST(FiveAxisMachinePlan, NoCandidateWithoutCollisionWitnessIsAnExplicitUnresolvedPlanAndKeepsPriorResource)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto first=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",first.at("status").To<Text>());const auto original=ProductMachinePlan(scene);ASSERT_EQ("Current",original.at("validity").To<Text>());const auto reference=original.at("revision");
    const auto scope=scene.Database().BeginUndoCommand("Constrain X travel");auto& tx=scene.Database().BeginTransaction("Make target physically unavailable");tx.ModifyComponent(machine.X,"CMachineJointComponent",{{"LowerLimit",-.01},{"UpperLimit",.01}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));scope->End();EXPECT_EQ("Stale",ProductMachinePlan(scene).at("validity").To<Text>());
    const auto task=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",task.at("status").To<Text>())<<(task.contains("error")?task.at("error").To<Text>():Text{});const auto result=task.at("result").To<Map>();EXPECT_NE("Ready",result.at("status").To<Text>());EXPECT_FALSE(result.at("playbackAllowed").To<bool>());EXPECT_TRUE(result.at("frames").To<Array>().empty());EXPECT_EQ(Text::npos,result.at("reason").To<Text>().find("CAM_FIELD_REQUIRED"));EXPECT_EQ(reference,ProductMachinePlan(scene).at("revision"));ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,ProductMachinePlan(scene));
}
TEST(FiveAxisMachineToolBoxes, LegacyOwnMeshTagUsesDeclaredBoxesAndIgnoresVisualMeshVersions)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,29),2,2,2).Shape());const auto imported=Imported(scene,source);
    auto placement=PlacementRequest(scene,imported);placement["transform"]=Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};Invoke(&scene,"CamPlacement","SetTransform",placement);
    auto machine=CreateProductPlanMachine(scene);const auto tool=scene.Database().GetEntity(machine.Tool);
    auto mesh=std::make_shared<iCAX::GeometryData::CTriangleMeshResource>();
    for(const auto& p:std::vector<std::array<double,3>>{{-5,-5,-5},{5,-5,-5},{5,5,-5},{-5,5,-5},{-5,-5,5},{5,-5,5},{5,5,5},{-5,5,5}})mesh->Mesh.Vertices.push_back({p[0],p[1],p[2]});
    mesh->Mesh.Triangles={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
    const auto url=scene.Resources().AllocateResourceURL();iCAX::Resource::CResourceInfo info;info.Name="Closed test tool";info.ResourceTypeID=iCAX::GeometryData::CTriangleMeshResource::kResourceTypeName;info.nVersion=1;info.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;scene.Resources().Set(url,mesh,info);
    PlanAttach(tool,"CMachineVisualComponent",{{"Visuals",Array{Map{{"pose",Array{0.,0.,.002,0.,0.,0.}},{"geometry",Map{{"type",Text("box")}}},{"geometryResourceId",url}}}}});
    const auto move=[&]{return CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",Map{{iCAX::Data::to_string(machine.Tool),30.}}}});};
    // The authored box is tiny and ten mm above the tool origin; it is clear.
    EXPECT_EQ("Clear",move().at("collisionStatus").To<Text>());
    machine.Configuration["toolCollisionRepresentation"]=Text("OwnMesh");auto request=GenerateRequest(scene);request["configuration"]=machine.Configuration;ConfigureMachineSimulation(scene,request);
    const auto snapshot=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});
    EXPECT_EQ("Boxes",snapshot.at("configuration").To<Map>().at("toolCollisionRepresentation").To<Text>());EXPECT_FALSE(snapshot.contains("toolMesh"));
    const auto stillClear=move();EXPECT_EQ("Clear",stillClear.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(stillClear);EXPECT_EQ("MachineBoxes; WorkpieceFixtureOwnMesh",stillClear.at("collisionRepresentation").To<Text>());
    // Display-only triangle changes cannot alter a collision model or verdict.
    info.nVersion=2;scene.Resources().Set(url,std::make_shared<iCAX::GeometryData::CTriangleMeshResource>(*mesh),info);
    const auto updated=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});EXPECT_EQ(snapshot.at("modelKey"),updated.at("modelKey"));EXPECT_EQ("Clear",move().at("collisionStatus").To<Text>());
    auto open=std::make_shared<iCAX::GeometryData::CTriangleMeshResource>(*mesh);open->Mesh.Triangles.pop_back();info.nVersion=3;scene.Resources().Set(url,open,info);EXPECT_EQ(snapshot.at("modelKey"),ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("modelKey"));
    const auto collider=tool->GetComponent("CMachineCollisionComponent");ASSERT_TRUE(collider);Text error;const auto smallColliders=collider->GetProperty("Collisions").To<Array>();auto large=PlanBox(0,0,0,.01).front().To<Map>();large["name"]=Text("Declared box enclosing target material");
    auto enabled=smallColliders;enabled.emplace_back(large);ASSERT_TRUE(collider->SetProperty("Collisions",enabled,error));const auto blocked=move();EXPECT_EQ("Blocked",blocked.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(blocked);EXPECT_NE(snapshot.at("modelKey"),ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}}).at("modelKey"));
    large["enabled"]=false;auto disabled=smallColliders;disabled.emplace_back(large);ASSERT_TRUE(collider->SetProperty("Collisions",disabled,error));EXPECT_EQ("Clear",move().at("collisionStatus").To<Text>());
}
TEST(FiveAxisMachineToolBoxes, CachedDisabledOrdinaryPartCannotBecomeAnUnprotectedTool)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),2.,2.,1.).Shape());const auto imported=Imported(scene,source);auto placement=PlacementRequest(scene,imported);placement["transform"]=Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};Invoke(&scene,"CamPlacement","SetTransform",placement);auto machine=CreateProductPlanMachine(scene);const auto ordinaryPart=scene.Database().GetEntity(machine.X);ASSERT_TRUE(ordinaryPart);
    const auto collision=ordinaryPart->GetComponent("CMachineCollisionComponent");ASSERT_TRUE(collision);auto disabledBox=collision->GetProperty("Collisions").To<Array>().front().To<Map>();disabledBox["enabled"]=false;const Array disabledBoxes{disabledBox};Text error;ASSERT_TRUE(collision->SetProperty("Collisions",disabledBoxes,error));
    const auto check=[&]{const auto state=Read(scene);return CheckMachineMove(scene,{{"expected",state.at("expected")},{"setupId",state.at("activeSetupId")},{"positions",Map{{iCAX::Data::to_string(machine.Tool),30.}}}});};
    // A normal non-tool part may contribute no enabled boxes. Both calls run
    // the real collision preparation path and cache that same empty body.
    EXPECT_EQ("Clear",check().at("collisionStatus").To<Text>());EXPECT_EQ("Clear",check().at("collisionStatus").To<Text>());
    const auto switchTool=[&](ID tool){auto configuration=machine.Configuration;configuration["toolId"]=iCAX::Data::to_string(tool);auto request=GenerateRequest(scene);request["configuration"]=configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",request);};
    switchTool(machine.X);EXPECT_EQ(disabledBoxes,collision->GetProperty("Collisions").To<Array>());EXPECT_EQ("CollisionBoxesRequired",Head(scene).at("collisionModel").To<Map>().at("status").To<Text>());
    const auto rejected=check();EXPECT_EQ("Unchecked",rejected.at("status").To<Text>());EXPECT_EQ("Unchecked",rejected.at("collisionStatus").To<Text>());EXPECT_NE(Text::npos,rejected.at("reason").To<Text>().find("CAM_HEAD_COLLISION_BOXES_REQUIRED"))<<iCAX::Data::VariantSerializer::Serialize(rejected);
    // Returning to a protected tool reuses exactly the same disabled-part
    // declarations without turning the earlier rejection into a global error.
    switchTool(machine.Tool);EXPECT_EQ(disabledBoxes,collision->GetProperty("Collisions").To<Array>());EXPECT_EQ("Clear",check().at("collisionStatus").To<Text>());
}

namespace {
    // These contexts retain the real product catalog between the two SDO
    // calls. Only the host's storage is in memory; import, machine creation,
    // mesh loading, axis planning and collision work all execute production code.
    class AssetPlanApplication final:public iCAX::Application::IApplicationContext {
        iCAX::Application::CApplicationDescriptor descriptor;
        iCAX::Application::CApplicationPaths paths;
        iCAX::Services::CServiceProvider services;
    public:
        AssetPlanApplication(){paths.UserConfigDirectory="C:/icax-copp-motion-20261026/product-fixtures/catalog";std::filesystem::create_directories(paths.UserConfigDirectory);}
        const iCAX::Application::CApplicationDescriptor& GetDescriptor()const override{return descriptor;}
        const iCAX::Application::CApplicationPaths& GetPaths()const override{return paths;}
        iCAX::Data::PropertyBag GetSettings()const override{return {};}
        const iCAX::Services::CServiceProvider& Services()const override{return services;}
    };
    class AssetPlanProduct final:public iCAX::Product::IProductContext {
        iCAX::Product::CProductDefinition definition;
        iCAX::Product::CProductData data;
    public:
        AssetPlanProduct(){definition.ProductID="icax.five-axis-cam";definition.Capabilities["machineDefinition"]=Map{{"supportedFormats",Array{Map{{"formatId",Text("machine.urdf")},{"name",Text("URDF")},{"extensions",Array{Text(".urdf")}}},Map{{"formatId",Text("machine.sdf")},{"name",Text("SDF")},{"extensions",Array{Text(".sdf")}}}}}};}
        const iCAX::Product::CProductDefinition& GetDefinition()const override{return definition;}
        const Text& GetProductID()const override{return definition.ProductID;}
        std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore()const override{return {};}
        iCAX::Product::CProductData GetProductData()const override{return data;}
        void ReplaceSettings(const iCAX::Data::PropertyBag& fields)override{data.Settings=fields;}
        void UpdateSettingsWithHistory(const Text&,const iCAX::Data::PropertyBag& fields,const Text&)override{for(const auto& [key,value]:fields.GetProperties())data.Settings.Set(key,value);}
        iCAX::Services::CServiceProvider& GetServiceProvider()const override{throw std::logic_error("unused");}
        iCAX::Database::IMetaRegistry& GetMetaRegistry()const override{throw std::logic_error("unused");}
        iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry()const override{throw std::logic_error("unused");}
        iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry()const override{throw std::logic_error("unused");}
        iCAX::Interaction::CSDORegistry& GetSDORegistry()const override{throw std::logic_error("unused");}
    };
    class AssetPlanProject final:public iCAX::Project::IProjectContext {
        ID id;Text name="Imported machine plan regression",path;iCAX::Data::PropertyBag settings;
    public:
        explicit AssetPlanProject(ID project):id(project){}
        const ID& GetProjectID()const override{return id;}
        const Text& GetProjectName()const override{return name;}
        const Text& GetProjectPath()const override{return path;}
        iCAX::Data::PropertyBag& Settings()override{return settings;}
        const iCAX::Data::PropertyBag& Settings()const override{return settings;}
    };
    struct AssetPlanHost {
        AssetPlanApplication Application;AssetPlanProduct Product;AssetPlanProject Project;
        explicit AssetPlanHost(ID project):Project(project){}
        Map Call(Scene& scene,const Text& group,const Text& method,const Map& payload){
            auto registry=std::make_shared<iCAX::Interaction::CSDORegistry>();iCAX::Interaction::CSDORegistrationCatalog::ReplayByModulePaths(*registry,{Module(L"CamRuntime.dll")});
            iCAX::Interaction::CSDOChannel channel;iCAX::Interaction::CSDOInvoker invoker(registry);auto frontend=channel.GetEndAEndpoint(),backend=channel.GetEndBEndpoint();const auto json=iCAX::Data::VariantSerializer::Serialize(payload);
            frontend.Send(iCAX::Interaction::CreateTextSDOFrame(1,iCAX::Interaction::MakeSDOMethod(group,method).GetCode(),iCAX::Interaction::ESDOFrameKind::Request,json));invoker.DispatchAvailableFrames(backend,Application,&Product,&Project,&scene);auto frames=frontend.Receive();
            if(frames.size()!=1)throw std::runtime_error("Asset SDO response count");const Text body(frames[0].Payload.begin(),frames[0].Payload.end());if(frames[0].nStatus!=iCAX::Interaction::EInvocationStatus::Ok)throw std::runtime_error(body);return iCAX::Data::VariantSerializer::Deserialize(body).To<Map>();
        }
    };
    void CheckImportedMachinePlan(const Text& relativePath,const Text& toolName,const Text& supportName,Array tcp,Array beam,const Text& label,const Text& expectedStatus="Ready",bool mountFirst=false){
        namespace M=iCAX::CAM::Motion;
        const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
        const auto source=std::filesystem::path(__FILE__).parent_path()/"../../../../../.."/relativePath;
        const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",source.lexically_normal().string()}});const auto result=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}});const auto machine=result.at("machine").To<Map>();ASSERT_FALSE(machine.at("visuals").To<Array>().empty());ASSERT_FALSE(machine.at("collisions").To<Array>().empty());
        Text tool,support;for(const auto& item:machine.at("elements").To<Array>()){const auto element=item.To<Map>();if(element.at("name").To<Text>()==toolName)tool=element.at("entityId").To<Text>();if(element.at("name").To<Text>()==supportName)support=element.at("entityId").To<Text>();}ASSERT_FALSE(tool.empty());ASSERT_FALSE(support.empty());
        if(mountFirst){Map poses;for(const auto& value:machine.at("joints").To<Array>()){const auto j=value.To<Map>();if(j.at("type").To<Text>()=="revolute")poses[j.at("entityId").To<Text>()]=j.at("jointName").To<Text>()=="C"?.25:.15;}
            const auto moved=MoveMachine(scene,{{"expected",Read(scene).at("expected")},{"machineId",machine.at("id")},{"positions",poses}});ASSERT_EQ("Clear",moved.at("collisionStatus").To<Text>());}
        PrepareProductPlanPaths(scene);Array identity;for(auto x:M::Identity())identity.emplace_back(x);Map configuration{{"machineId",machine.at("id")},{"toolId",tool},{"supportId",support},{"placement",identity},{"tcp",tcp},{"beam",beam},{"toolCollisionRepresentation",Text(label.starts_with("lc")?"OwnMesh":"Boxes")}};
        if(mountFirst){auto mount=GenerateRequest(scene);mount["machineId"]=machine.at("id");configuration=Invoke(&scene,"CamSimulation","MountMachine",mount).at("configuration").To<Map>();}
        auto request=GenerateRequest(scene);request["configuration"]=configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",request);const auto snapshot=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}});
        EXPECT_EQ("Boxes",snapshot.at("configuration").To<Map>().at("toolCollisionRepresentation").To<Text>());EXPECT_FALSE(snapshot.contains("toolMesh"));EXPECT_EQ("MachineDeclaredCollisionBoxes",snapshot.at("toolCollisionSource").To<Text>());
        const auto declared=scene.Database().GetEntity(*ID::from_string(tool))->GetComponent("CMachineCollisionComponent")->GetProperty("Collisions").To<Array>();ASSERT_FALSE(declared.empty());bool foundToolBoxes=false;
        for(const auto& value:snapshot.at("collisionDefinitions").To<Array>()){const auto record=value.To<Map>();if(record.at("id").To<Text>()!=tool)continue;EXPECT_EQ(declared,record.at("geometry").To<Array>());foundToolBoxes=true;}EXPECT_TRUE(foundToolBoxes);
        std::vector<M::Node> nodes;std::vector<double> positions;for(const auto& value:snapshot.at("nodes").To<Array>()){const auto n=value.To<Map>();M::Node node;node.ID=n.at("id").To<Text>();node.Parent=n.at("parent").To<int>();node.Type=n.at("type").To<Text>();const auto origin=n.at("origin").To<Array>(),axis=n.at("axis").To<Array>();for(int k=0;k<16;++k)node.Origin[k]=origin[k].To<double>();for(int k=0;k<3;++k)node.Axis[k]=axis[k].To<double>();nodes.push_back(node);}for(const auto& q:snapshot.at("positions").To<Array>())positions.push_back(q.To<double>());
        const auto world=M::Forward(nodes,positions);const auto relative=M::Multiply(M::Inverse(world.at(snapshot.at("support").To<int>())),world.at(snapshot.at("tool").To<int>()));M::Vector t{},d{};for(int k=0;k<3;++k){t[k]=tcp[k].To<double>();d[k]=beam[k].To<double>();}auto target=M::Transform(relative,t);const auto direction=M::Unit(M::Transform(relative,d,true));
        // Leave travel for the separate CAM follow height below a raised
        // loading pose; a first cut must not require Z above its upper stop.
        if(expectedStatus=="Ready")for(int k=0;k<3;++k)target[k]+=(mountFirst?35.:5.)*direction[k];
        const auto rotation=gp_Quaternion(gp_Vec(0,0,-1),gp_Vec(direction[0],direction[1],direction[2])).GetMatrix();auto placement=M::Identity();for(int r=0;r<3;++r)for(int c=0;c<3;++c)placement[r*4+c]=rotation.Value(r+1,c+1);
        const auto first=Generated(scene).at("operations").To<Array>().front().To<Map>().at("setupStart").To<Array>();for(int r=0;r<3;++r){placement[r*4+3]=target[r];for(int c=0;c<3;++c)placement[r*4+3]-=placement[r*4+c]*first[c].To<double>();}Array placed;for(auto x:placement)placed.emplace_back(x);configuration["placement"]=placed;request=GenerateRequest(scene);request["configuration"]=configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",request);
        Array axes;const Text words="XYZABCUVW";std::size_t at=0;for(const auto& node:nodes)if(node.Type!="fixed"){Map axis{{"jointId",node.ID},{"word",words.substr(at++,1)},{"sign",1.},{"offset",0.}};axis["maxAcceleration"]=200.;axis["maxJerk"]=1000.;axes.emplace_back(std::move(axis));}const Map post{{"name",Text("Uncalibrated real asset test")},{"format",Text("JointJSON")},{"extension",Text("json")},{"axes",axes},{"header",Text("G90\nG93")},{"footer",Text("M30")},{"processOn",Text("M3")},{"processOff",Text("M5")},{"motion",Text("G1 {axes} F{feed}")},{"cutFeedMmMin",600.},{"travelFeedMmMin",1200.}};Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",machine.at("id")},{"postprocessor",post}});
        const auto output=std::filesystem::path("C:/icax-copp-motion-20261026/product-fixtures");SaveProductPlanFixture(scene,output/(label+"-before.i5cam"));const auto terminal=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);const auto report=terminal.at("result").To<Map>();std::ofstream(output/(label+"-report.json"))<<iCAX::Data::VariantSerializer::Serialize(report);
        EXPECT_EQ(expectedStatus,report.at("status").To<Text>())<<report.at("reason").To<Text>();EXPECT_EQ("MachineDeclaredBoxesAndWorkpieceFixtureMesh",report.at("collisionScope").To<Text>());EXPECT_EQ("MachineDeclaredBoxesV5",report.at("collisionAlgorithm").To<Text>());
        if(report.at("status").To<Text>()=="Ready"){
            EXPECT_EQ("Clear",report.at("collisionStatus").To<Text>());EXPECT_TRUE(report.at("playbackAllowed").To<bool>());const auto persisted=ProductMachinePlan(scene);EXPECT_EQ("Current",persisted.at("validity").To<Text>());
            EXPECT_FALSE(persisted.at("revision").To<Map>().at("revision").To<Text>().empty());EXPECT_FALSE(persisted.contains("toolMesh"));
            const auto live=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}});const auto exported=MachineProgram(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",live.at("modelKey")}});const auto instructions=exported.at("motion").To<Map>().at("instructions").To<Array>(),frames=persisted.at("frames").To<Array>();ASSERT_EQ(frames.size(),instructions.size());for(std::size_t i=0;i<frames.size();++i)EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(frames[i].To<Map>().at("joints").To<Array>()),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(instructions[i].To<Map>().at("joints").To<Array>()))<<"Stored axis frame "<<i;
            const auto path=output/(label+"-current.i5cam");SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false,true);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_TRUE(persisted==ProductMachinePlan(reopened))<<"Embedded real-asset plan changed across reopen";const auto reopenedMachine=Invoke(&reopened,"CamSimulation","ReadMachine",{{"setupId",Read(reopened).at("activeSetupId")}});EXPECT_EQ(live.at("modelKey"),reopenedMachine.at("modelKey"));
        }else{EXPECT_FALSE(report.at("playbackAllowed").To<bool>());EXPECT_TRUE(report.at("frames").To<Array>().empty());EXPECT_EQ("Missing",ProductMachinePlan(scene).at("validity").To<Text>());}
    }
}
TEST(FiveAxisMachinePlanAssets, LC3015PhysicalNozzleTipContactCannotBePublishedAsAClearPlan){CheckImportedMachinePlan("src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf","A 激光切割头","工作台",{-3.262460231781006,0.,-220.},{0.,0.,-1.},"lc3015-tip-contact","Blocked");}
TEST(FiveAxisMachinePlanAssets, LC3015DeclaresPhysicalEndpointsAndLoadsAndHomesAtItsRaisedZPose)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
    const auto source=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf").lexically_normal();
    const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",source.string()}});
    const auto response=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}});const auto machine=response.at("machine").To<Map>();
    ID zID;std::size_t tools=0,supports=0;
    for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
        if(const auto element=entity->GetComponent("CMachineElementComponent")){
            if(element->GetProperty("WorkSupport").To<bool>())++supports;
            if(const auto tool=entity->GetComponent("CMachineToolComponent")){
                ++tools;EXPECT_EQ((Array{-3.262460231781006,0.,-220.}),tool->GetProperty("TcpLocalPosition").To<Array>());
                EXPECT_EQ((Array{0.,0.,-1.}),tool->GetProperty("BeamLocalDirection").To<Array>());
            }
        }
        if(const auto joint=entity->GetComponent("CMachineJointComponent");joint&&joint->GetProperty("JointName").To<Text>()=="Z"){
            zID=entity->GetID();EXPECT_NEAR(150,joint->GetProperty("Position").To<double>(),1e-9);EXPECT_NEAR(150,joint->GetProperty("HomePosition").To<double>(),1e-9);
        }
    }
    EXPECT_EQ(1u,tools);EXPECT_EQ(1u,supports);ASSERT_FALSE(zID.is_nil());
    bool publicHome=false;
    for(const auto& value:machine.at("joints").To<Array>()){
        const auto joint=value.To<Map>();if(joint.at("jointName").To<Text>()!="Z")continue;
        ASSERT_TRUE(joint.contains("homePosition"));EXPECT_NEAR(150,joint.at("homePosition").To<double>(),1e-9);publicHome=true;
    }
    EXPECT_TRUE(publicHome);
    host.Call(scene,"Machine","SetJointPosition",{{"entityId",iCAX::Data::to_string(zID)},{"position",100.}});
    host.Call(scene,"Machine","Home",{{"machineEntityId",machine.at("id")}});
    EXPECT_NEAR(150,scene.Database().GetEntity(zID)->GetComponent("CMachineJointComponent")->GetProperty("Position").To<double>(),1e-9);
}
TEST(FiveAxisMachinePlanAssets, IR1600DeclaresSupportAndMountsWithToolingAndLegacyRoleRecovery)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
    File part(BRepPrimAPI_MakeBox(20,30,5).Shape());Imported(scene,part);
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-30,-40,-30),60,80,30).Shape());auto fixtureRequest=GenerateRequest(scene);fixtureRequest["sourcePath"]=fixture.Utf8();
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",fixtureRequest)).at("status").To<Text>());
    const auto source=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../src/apps/five-axis-cam/resources/machines/industrial-arm/IR-1600.sdf").lexically_normal();
    const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",source.string()}});
    const auto machine=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}}).at("machine").To<Map>();
    ID supportID,toolID;std::size_t supports=0,tools=0;
    for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
        const auto element=e->GetComponent("CMachineElementComponent");if(!element||element->GetProperty("MachineID").To<ID>()!=*ID::from_string(machine.at("id").To<Text>()))continue;
        if(element->GetProperty("WorkSupport").To<bool>()){supportID=e->GetID();++supports;}
        if(e->GetComponent("CMachineToolComponent")){toolID=e->GetID();++tools;}
    }
    ASSERT_EQ(1u,supports);ASSERT_EQ(1u,tools);const auto before=Read(scene);
    const auto mount=[&]{auto request=GenerateRequest(scene);request["machineId"]=machine.at("id");return Invoke(&scene,"CamSimulation","MountMachine",request);};
    const auto mounted=mount();EXPECT_EQ("Clear",mounted.at("mounting").To<Map>().at("collisionStatus").To<Text>());
    EXPECT_EQ(iCAX::Data::to_string(supportID),mounted.at("configuration").To<Map>().at("supportId").To<Text>());
    EXPECT_EQ(iCAX::Data::to_string(toolID),mounted.at("configuration").To<Map>().at("toolId").To<Text>());
    EXPECT_EQ(2u,mounted.at("mounting").To<Map>().at("includedEntityIds").To<Array>().size());
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
    ASSERT_TRUE(scene.Database().Undo());Text error;const auto support=scene.Database().GetEntity(supportID)->GetComponent("CMachineElementComponent");
    ASSERT_TRUE(support->SetProperty("WorkSupport",false,error));const auto recovered=mount();EXPECT_EQ(mounted.at("configuration"),recovered.at("configuration"));
    ASSERT_TRUE(scene.Database().Undo());auto pose=support->GetProperty("RestPose").To<Array>();pose[0]=pose[0].To<double>()+10;ASSERT_TRUE(support->SetProperty("RestPose",pose,error));
    EXPECT_THROW(mount(),std::exception);
}
TEST(FiveAxisMachinePlanAssets, LegacyLC3015RecoversBothEndpointsFromMatchingInstalledDefinitionOnly)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
    File part(BRepPrimAPI_MakeBox(20,30,5).Shape());Imported(scene,part);
    const auto source=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf").lexically_normal();
    const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",source.string()}});
    const auto machine=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}}).at("machine").To<Map>();
    const auto machineID=*ID::from_string(machine.at("id").To<Text>());ID supportID,toolID;Text error;
    for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
        const auto element=e->GetComponent("CMachineElementComponent");if(!element||element->GetProperty("MachineID").To<ID>()!=machineID)continue;
        if(element->GetProperty("WorkSupport").To<bool>()){supportID=e->GetID();ASSERT_TRUE(element->SetProperty("WorkSupport",false,error));}
        if(e->GetComponent("CMachineToolComponent")){toolID=e->GetID();e->RemoveComponent("CMachineToolComponent");}
    }
    const auto stale=std::filesystem::temp_directory_path()/("icax-legacy-endpoints-"+iCAX::Data::to_string(project)+".urdf");
    struct RemoveFile {std::filesystem::path Path;~RemoveFile(){std::error_code ignored;std::filesystem::remove(Path,ignored);}} cleanup{stale};
    std::ifstream input(source);Text xml((std::istreambuf_iterator<char>(input)),{});
    for(const auto* marker:{"<icax_work_support","<icax_tool"})for(auto pos=xml.find(marker);pos!=Text::npos;pos=xml.find(marker)){const auto end=xml.find("/>",pos);ASSERT_NE(end,Text::npos);xml.erase(pos,end+2-pos);}
    std::ofstream(stale)<<xml;ASSERT_TRUE(scene.Database().GetEntity(machineID)->GetComponent("CMachineInstanceComponent")->SetProperty("SourcePath",stale.string(),error));
    const auto request=[&]{auto value=GenerateRequest(scene);value["machineId"]=machine.at("id");return value;};
    EXPECT_THROW(MountMachine(scene,request()),std::exception);
    EXPECT_THROW(MountMachineWithTemplates(scene,request(),{{iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()),source.string()}}),std::exception);
    const Map sources{{imported.at("machineDefinitionId").To<Text>(),source.string()}};const auto before=Read(scene);
    const auto recovered=MountMachineWithTemplates(scene,request(),sources),config=recovered.at("configuration").To<Map>();
    EXPECT_EQ(iCAX::Data::to_string(supportID),config.at("supportId").To<Text>());EXPECT_EQ(iCAX::Data::to_string(toolID),config.at("toolId").To<Text>());
    EXPECT_EQ((Array{-3.262460231781006,0.,-220.}),config.at("tcp").To<Array>());EXPECT_EQ((Array{0.,0.,-1.}),config.at("beam").To<Array>());
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));
    ASSERT_TRUE(scene.Database().Undo());const auto tool=scene.Database().GetEntity(toolID)->GetComponent("CMachineElementComponent");auto pose=tool->GetProperty("RestPose").To<Array>();pose[0]=pose[0].To<double>()+10;ASSERT_TRUE(tool->SetProperty("RestPose",pose,error));
    EXPECT_THROW(MountMachineWithTemplates(scene,request(),sources),std::exception);
}
// The declared focus is ten millimetres beyond the authored physical nozzle.
// Neither collision boxes nor the workpiece mesh are removed or reduced.
TEST(FiveAxisMachinePlanAssets, LC3015ProductionImportAndTenMillimetreFocusPassRealWholeMachineChecks){CheckImportedMachinePlan("src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf","A 激光切割头","工作台",{-3.262460231781006,0.,-230.},{0.,0.,-1.},"lc3015-focus-plan");}
TEST(FiveAxisMachinePlanAssets, IR1600ProductionImportAndAuthoredFocusPassRealWholeMachineChecks){CheckImportedMachinePlan("src/apps/five-axis-cam/resources/machines/industrial-arm/IR-1600.sdf","J6 激光加工头","工件装夹台",{280.,0.,0.},{1.,0.,0.},"ir1600-plan");}
TEST(FiveAxisMachinePlanFixtureAudit, ExistingRealAssetFilesContainRenderableMovablePartsAndEmbeddedGeometry)
{
    const auto folder=std::filesystem::path("C:/icax-copp-motion-20261026/product-fixtures");
    for(const auto& label:std::vector<Text>{"lc3015-focus-plan-before","lc3015-focus-plan-current","ir1600-plan-before","ir1600-plan-current"}){
        SCOPED_TRACE(label);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});const auto prepared=file.PrepareOpen(folder/(label+".i5cam"));const auto info=prepared.Result().Info;ASSERT_EQ(info.ProjectID,info.MainSceneID);Scene scene(info.ProjectID,info.MainSceneID,false,true);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);const auto snapshot=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}});const auto config=snapshot.at("configuration").To<Map>();const auto machine=*ID::from_string(config.at("machineId").To<Text>());
        Array parts;std::size_t renderedRotations=0,renderedTranslations=0;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){const auto part=entity->GetComponent("CMachineElementComponent");if(!part||part->GetProperty("MachineID").To<ID>()!=machine)continue;const auto render=entity->GetComponent("CRenderInstanceComponent");ASSERT_TRUE(render)<<part->GetProperty("Name").To<Text>();EXPECT_TRUE(render->GetProperty("Visible").To<bool>());const auto geometry=render->GetProperty("GeometryResourceID").To<Text>(),material=render->GetProperty("MaterialResourceID").To<Text>();const auto mesh=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(geometry);ASSERT_TRUE(mesh)<<geometry;EXPECT_FALSE(mesh->Empty());const auto paint=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(material);ASSERT_TRUE(paint)<<material;EXPECT_FALSE(paint->Empty());EXPECT_TRUE(scene.Resources().GetInfo(geometry)->IsEmbedded());
            Text type="fixed";if(const auto joint=entity->GetComponent("CMachineJointComponent")){type=joint->GetProperty("JointType").To<Text>();if(type=="prismatic")++renderedTranslations;else if(type=="revolute"||type=="continuous")++renderedRotations;}parts.emplace_back(Map{{"id",iCAX::Data::to_string(entity->GetID())},{"name",part->GetProperty("Name")},{"jointType",type},{"geometryBytes",mesh->Size()},{"materialBytes",paint->Size()}});
        }
        EXPECT_GE(parts.size(),9u);EXPECT_GE(renderedRotations,2u);if(label.starts_with("lc"))EXPECT_EQ(3u,renderedTranslations);else EXPECT_EQ(6u,renderedRotations);const Map report{{"file",label+".i5cam"},{"projectId",iCAX::Data::to_string(info.ProjectID)},{"sceneId",iCAX::Data::to_string(info.MainSceneID)},{"configuration",config},{"renderedPartCount",std::to_string(parts.size())},{"renderedRotations",std::to_string(renderedRotations)},{"renderedTranslations",std::to_string(renderedTranslations)},{"parts",parts}};std::ofstream(folder/(label+"-render-audit.json"))<<iCAX::Data::VariantSerializer::Serialize(report);
    }
}

