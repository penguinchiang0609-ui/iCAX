namespace {
    Array InstalledHeadSources(){
        const auto root=(std::filesystem::path(__FILE__).parent_path()/"../../../../../..").lexically_normal();
        std::ifstream file(root/"src/apps/five-axis-cam/product.manifest.json",std::ios::binary);
        const auto manifest=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(Text((std::istreambuf_iterator<char>(file)),{})).To<Map>();
        Array result;for(const auto& value:manifest.at("capabilities").To<Map>().at("machineDefinition").To<Map>().at("defaults").To<Array>()){
            auto item=value.To<Map>();item["sourcePath"]=(root/"src"/item.at("sourcePath").To<Text>()).lexically_normal().string();result.emplace_back(item);
        }return result;
    }
    Map SelectInstalledHead(Scene& scene,const Map& item){auto request=GenerateRequest(scene);request["kind"]=Text("Builtin");request["builtinDefinitionId"]=item.at("id");return Invoke(&scene,"CamHead","Configure",request);}
}
TEST(FiveAxisBuiltinHead, AllDefinitionsKeepVisualsDeclaredBoxesAndToolCalibration)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);const auto sources=InstalledHeadSources();ASSERT_EQ(9u,sources.size());BindBuiltinHeadSources(scene,sources);
    const auto list=Invoke(&scene,"CamHead","ListBuiltins").at("items").To<Array>();ASSERT_EQ(sources.size(),list.size());
    for(std::size_t n=0;n<sources.size();++n){const auto source=sources[n].To<Map>();SCOPED_TRACE(source.at("name").To<Text>());
        EXPECT_EQ(source.at("name"),list[n].To<Map>().at("name"));EXPECT_FALSE(list[n].To<Map>().contains("sourcePath"));
        SelectInstalledHead(scene,source);const auto head=Head(scene);EXPECT_EQ(source.at("name"),head.at("name"));EXPECT_EQ(source.at("id"),head.at("builtinDefinitionId"));EXPECT_TRUE(head.at("calibrated").To<bool>());EXPECT_EQ("MachineDefinition",head.at("collisionSource").To<Text>());
        iCAX::CAM::CMachineDescriptionResource definition;Text error;ASSERT_TRUE(iCAX::CAM::LoadMachineDescription(source.at("sourcePath").To<Text>(),definition,error))<<error;
        const auto tool=std::find_if(definition.Elements.begin(),definition.Elements.end(),[](const auto& e){return e.Tool.bValid;});ASSERT_NE(definition.Elements.end(),tool);
        EXPECT_EQ(tool->Tool.TcpLocalPosition,head.at("tcpLocalPosition").To<Array>());EXPECT_EQ(tool->Tool.BeamLocalDirection,head.at("beamLocalDirection").To<Array>());
        ASSERT_TRUE(head.contains("collisionBoxes"));const auto declared=head.at("collisionBoxes").To<Array>();ASSERT_FALSE(tool->Collisions.empty());ASSERT_EQ(tool->Collisions.size(),declared.size());
        for(std::size_t i=0;i<tool->Collisions.size();++i){const auto& expected=tool->Collisions[i];const auto actual=declared[i].To<Map>();SCOPED_TRACE(expected.Name);
            EXPECT_EQ(expected.Name,actual.at("name").To<Text>());ASSERT_EQ("box",expected.Geometry.at("type").To<Text>());EXPECT_EQ(expected.Geometry,actual.at("geometry").To<Map>());
            const auto pose=actual.at("pose").To<Array>();ASSERT_EQ(6u,pose.size());for(int axis=0;axis<6;++axis)EXPECT_DOUBLE_EQ(expected.Pose[axis],pose[axis].To<double>());
            ASSERT_TRUE(actual.contains("enabled"));EXPECT_TRUE(actual.at("enabled").To<bool>());
        }
        const auto ref=head.at("geometryRef").To<Map>();const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(geometry);ASSERT_EQ(tool->Visuals.size(),geometry->Triangulations3.size());
        for(std::size_t i=0;i<tool->Visuals.size();++i){const auto& visual=tool->Visuals[i];const auto uri=visual.Geometry.at("uri").To<Text>();iCAX::Resource::CResourceImportRequest request;request.SourcePath=(std::filesystem::path(source.at("sourcePath").To<Text>()).parent_path()/uri).string();request.FormatID="geometry.stl";
            const auto raw=scene.Resources().Import<iCAX::GeometryData::CTriangleMeshResource>(request);ASSERT_TRUE(raw);const auto& output=geometry->Triangulations3[i].Geometry;EXPECT_EQ(raw->Mesh.Triangles,output.Triangles);ASSERT_EQ(raw->Mesh.Vertices.size(),output.Vertices.size());
            std::array<double,3> scale{1000,1000,1000};if(visual.Geometry.contains("scale")){const auto v=visual.Geometry.at("scale").To<Array>();for(int k=0;k<3;++k)scale[k]*=v[k].To<double>();}
            const auto& p=visual.Pose;const auto matrix=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],scale[0],scale[1],scale[2]);
            for(std::size_t k=0;k<output.Vertices.size();++k){const auto& a=raw->Mesh.Vertices[k];const auto& b=output.Vertices[k];EXPECT_DOUBLE_EQ(matrix(0,0)*a.X+matrix(0,1)*a.Y+matrix(0,2)*a.Z+matrix(0,3),b.X);EXPECT_DOUBLE_EQ(matrix(1,0)*a.X+matrix(1,1)*a.Y+matrix(1,2)*a.Z+matrix(1,3),b.Y);EXPECT_DOUBLE_EQ(matrix(2,0)*a.X+matrix(2,1)*a.Y+matrix(2,2)*a.Z+matrix(2,3),b.Z);}
        }
        const auto collision=head.at("collisionModel").To<Map>();EXPECT_EQ("MachineDeclaredBoxes",collision.at("basis").To<Text>());EXPECT_EQ(std::to_string(tool->Collisions.size()),collision.at("boxCount").To<Text>());EXPECT_EQ("CalibratedTCP",collision.at("frame").To<Text>());
        const auto display=head.at("previewRef").To<Map>();ASSERT_TRUE(scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(display.at("url").To<Text>(),std::stoull(display.at("version").To<Text>())));
    }
}
TEST(FiveAxisBuiltinHead, GantryAssembliesEmbedAllPartsAndRequireTheFullMachineForCollision)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);
    const auto output=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../output/tests/gantry-heads-20261101").lexically_normal();std::filesystem::create_directories(output);Array snapshots;
    for(const auto& value:sources){const auto source=value.To<Map>();if(!source.contains("headRoot"))continue;SCOPED_TRACE(source.at("name").To<Text>());
        SelectInstalledHead(scene,source);const auto head=Head(scene);ASSERT_TRUE(head.contains("assembly"));EXPECT_TRUE(head.at("requiresMachineSimulation").To<bool>());
        EXPECT_EQ("MachineBindingRequired",head.at("collisionModel").To<Map>().at("status").To<Text>());const auto assembly=head.at("assembly").To<Map>();EXPECT_EQ(source.at("headAxes"),assembly.at("headAxes"));EXPECT_EQ(source.at("workpieceAxes"),assembly.at("workpieceAxes"));
        const auto nodes=assembly.at("nodes").To<Array>();EXPECT_EQ(source.at("kind").To<Text>()=="AB"?3u:source.at("headAxes").To<Array>().empty()?1u:2u,nodes.size());
        for(const auto& node:nodes){const auto preview=node.To<Map>().at("previewRef").To<Map>();ASSERT_TRUE(scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(preview.at("url").To<Text>(),std::stoull(preview.at("version").To<Text>())));}
        Text failure;try{RunCollision(scene);ADD_FAILURE()<<"Head-only rigid collision must not certify the articulated assembly";}catch(const std::exception& e){failure=e.what();}EXPECT_NE(Text::npos,failure.find("CAM_HEAD_MACHINE_BINDING_REQUIRED"))<<failure;
        snapshots.emplace_back(head);const auto path=output/(source.at("kind").To<Text>()+"-head.i5cam");SaveProductPlanFixture(scene,path);
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false,true);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);BindBuiltinHeadSources(reopened,{});
        EXPECT_EQ(head,Head(reopened));for(const auto& node:Head(reopened).at("assembly").To<Map>().at("nodes").To<Array>()){const auto preview=node.To<Map>().at("previewRef").To<Map>();EXPECT_TRUE(reopened.Resources().Get<iCAX::Resource::CFlatBufferResource>(preview.at("url").To<Text>(),std::stoull(preview.at("version").To<Text>())));}
    }
    ASSERT_EQ(5u,snapshots.size());std::ofstream(output/"head-snapshots.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(snapshots);
}
TEST(FiveAxisBuiltinHead, BoundInstanceColliderEditsAndDisablementReachTheCalibratedCAMBoxes)
{
    namespace R=iCAX::GeometryServices::RigidMotion;
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);SelectInstalledHead(scene,sources.front().To<Map>());
    const auto selectedSnapshot=Head(scene);ASSERT_GT(std::stoull(selectedSnapshot.at("collisionModel").To<Map>().at("boxCount").To<Text>()),1u);
    const auto machine=CreateProductPlanMachine(scene);auto configuration=machine.Configuration;
    configuration["tcp"]=Array{2.,3.,4.};configuration["beam"]=Array{1.,0.,0.};auto configure=GenerateRequest(scene);configure["configuration"]=configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
    const auto collision=scene.Database().GetEntity(machine.Tool)->GetComponent("CMachineCollisionComponent");ASSERT_TRUE(collision);
    const Map first{{"id",Text("edited-tool-box")},{"name",Text("Offset and rotated tool box")},{"enabled",true},{"pose",Array{.003,.005,.007,.1,.2,.3}},{"geometry",Map{{"type",Text("box")},{"size",Array{.002,.004,.006}}}}};
    Map second{{"id",Text("disabled-tool-box")},{"name",Text("Disabled box must never enlarge the head")},{"enabled",false},{"pose",Array{.020,.030,.040,0.,0.,0.}},{"geometry",Map{{"type",Text("box")},{"size",Array{.010,.012,.014}}}}};
    const auto write=[&](const Array& boxes){const auto undo=scene.Database().BeginUndoCommand("Edit tool collision boxes");auto& tx=scene.Database().BeginTransaction("Edit tool collision boxes");tx.ModifyComponent(machine.Tool,"CMachineCollisionComponent",{{"Collisions",boxes}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));undo->End();};
    const auto expectCAMBox=[&](const Map& declared){
        const auto head=Head(scene);EXPECT_EQ(configuration.at("tcp"),head.at("tcpLocalPosition"));EXPECT_EQ(configuration.at("beam"),head.at("beamLocalDirection"));EXPECT_EQ("MachineDeclaredBoxes",head.at("collisionModel").To<Map>().at("basis").To<Text>());EXPECT_EQ("1",head.at("collisionModel").To<Map>().at("boxCount").To<Text>());EXPECT_EQ("MachineInstance",head.at("collisionSource").To<Text>());EXPECT_EQ(iCAX::Data::to_string(machine.Machine),head.at("collisionMachineId").To<Text>());EXPECT_EQ(iCAX::Data::to_string(machine.Tool),head.at("collisionToolId").To<Text>());
        const auto history=scene.Database().GetUndoArray().size();RunCollision(scene);EXPECT_EQ(history,scene.Database().GetUndoArray().size());const auto report=SimulationReport(scene);const auto boxes=report.at("headBoxes").To<Array>();ASSERT_EQ(1u,boxes.size());const auto box=boxes.front().To<Map>();
        const auto pose=declared.at("pose").To<Array>(),size=declared.at("geometry").To<Map>().at("size").To<Array>();const auto matrix=iCAX::Transform::MakeLocalMatrix(pose[0].To<double>()*1000,pose[1].To<double>()*1000,pose[2].To<double>()*1000,pose[5].To<double>(),pose[4].To<double>(),pose[3].To<double>(),1,1,1);
        const auto calibration=R::Between({1,0,0},{0,0,-1});const auto center=R::Rotate(calibration,{matrix(0,3)-2,matrix(1,3)-3,matrix(2,3)-4});
        const auto actualCenter=box.at("center").To<Array>(),half=box.at("halfExtents").To<Array>(),axes=box.at("axes").To<Array>();
        for(int axis=0;axis<3;++axis){EXPECT_NEAR(center[axis],actualCenter[axis].To<double>(),1e-9);EXPECT_NEAR(size[axis].To<double>()*500,half[axis].To<double>(),1e-9);
            const auto expectedAxis=R::Rotate(calibration,{matrix(0,axis),matrix(1,axis),matrix(2,axis)});const auto actualAxis=axes[axis].To<Array>();for(int component=0;component<3;++component)EXPECT_NEAR(expectedAxis[component],actualAxis[component].To<double>(),1e-9);}
        EXPECT_EQ("CalibratedTCP",box.at("frame").To<Text>());
    };
    write(Array{first,second});EXPECT_EQ((Array{first,second}),Head(scene).at("collisionBoxes").To<Array>());expectCAMBox(first);
    auto edited=first;auto pose=edited.at("pose").To<Array>();pose[0]=.013;edited["pose"]=pose;write(Array{edited,second});EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());expectCAMBox(edited);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ((Array{first,second}),Head(scene).at("collisionBoxes").To<Array>());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ((Array{edited,second}),Head(scene).at("collisionBoxes").To<Array>());
    edited["enabled"]=false;second["enabled"]=true;write(Array{edited,second});EXPECT_EQ((Array{edited,second}),Head(scene).at("collisionBoxes").To<Array>());expectCAMBox(second);
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);const auto previous=setup->GetSimulationCheckData();second["enabled"]=false;write(Array{edited,second});const auto disabledHead=Head(scene),disabledModel=disabledHead.at("collisionModel").To<Map>(),paths=setup->GetGeneratedToolpaths(),overrides=setup->GetRapidOverrides();
    EXPECT_EQ("CollisionBoxesRequired",disabledModel.at("status").To<Text>());EXPECT_EQ("0",disabledModel.at("boxCount").To<Text>());EXPECT_TRUE(disabledModel.at("boxes").To<Array>().empty());EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());
    const auto rejectMissingBoxes=[&](const Text& domain,const Text& action,const Map& request){Text failure;try{Invoke(&scene,domain,action,request);ADD_FAILURE()<<domain<<"."<<action<<" must not certify a head without enabled collision boxes";}catch(const std::exception& error){failure=error.what();}EXPECT_NE(Text::npos,failure.find("CAM_HEAD_COLLISION_BOXES_REQUIRED"))<<failure;};
    rejectMissingBoxes("CamSimulation","CheckCollision",GenerateRequest(scene));rejectMissingBoxes("CamRapid","Preview",RapidRequest(scene));rejectMissingBoxes("CamSimulation","PlanMachine",GenerateRequest(scene));
    const auto geometry=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",geometry.at("status").To<Text>());const auto connections=geometry.at("result").To<Map>().at("connections").To<Map>();ASSERT_FALSE(connections.empty());for(const auto& [_,value]:connections){const auto result=value.To<Map>().at("result").To<Map>();EXPECT_EQ("Generated",result.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",result.at("validation").To<Text>());EXPECT_EQ("GeometryOnly",result.at("generation").To<Text>());}
    EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());EXPECT_EQ(previous,setup->GetSimulationCheckData());EXPECT_EQ(paths,setup->GetGeneratedToolpaths());EXPECT_EQ(overrides,setup->GetRapidOverrides());EXPECT_FALSE(ProductMachinePlan(scene).at("playbackAllowed").To<bool>());
}
TEST(FiveAxisBuiltinHead, SelectionUsesNativeHistoryAndEmbeddedGeometryWithoutChangingAuthoredPaths)
{
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);
    const auto setupId=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupId)->GetComponent(CFiveAxisSetupComponent::S_ClassName));
    const auto paths=setup->GetGeneratedToolpaths(),rapids=setup->GetGeneratedRapidPaths(),overrides=setup->GetRapidOverrides(),original=Head(scene);const auto work=Read(scene).at("workpieces"),placements=Read(scene).at("placements");
    auto request=GenerateRequest(scene);request["kind"]=Text("Builtin");request["builtinDefinitionId"]=sources.front().To<Map>().at("id");const auto result=Invoke(&scene,"CamHead","Configure",request),selected=Head(scene);EXPECT_EQ(result,Invoke(&scene,"CamHead","Configure",request));
    EXPECT_EQ(paths,setup->GetGeneratedToolpaths());EXPECT_EQ(rapids,setup->GetGeneratedRapidPaths());EXPECT_EQ(overrides,setup->GetRapidOverrides());EXPECT_EQ(work,Read(scene).at("workpieces"));EXPECT_EQ(placements,Read(scene).at("placements"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,Head(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(selected,Head(scene));
    const auto path=std::filesystem::temp_directory_path()/"icax-builtin-head-persistence.i5cam";SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false,true);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(selected,Head(reopened));std::filesystem::remove(path);
    auto invalid=GenerateRequest(scene);invalid["kind"]=Text("Builtin");invalid["builtinDefinitionId"]=Text("unknown");EXPECT_THROW(Invoke(&scene,"CamHead","Configure",invalid),std::exception);EXPECT_EQ(selected,Head(scene));
}
TEST(FiveAxisBuiltinHead, ColliderEditsSupersedeInFlightRapidSnapshotsWithoutReplacingSavedRoutes)
{
    Scene scene;PrepareOrderPart(scene);const auto machine=CreateProductPlanMachine(scene);
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const auto initial=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",initial.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(initial);
    const auto saved=setup->GetGeneratedRapidPaths(),paths=setup->GetGeneratedToolpaths(),overrides=setup->GetRapidOverrides();ASSERT_FALSE(saved.empty());
    const auto edit=[&](double x){auto boxes=PlanBox(x,0,.01);const auto undo=scene.Database().BeginUndoCommand("Move live tool collider");auto& tx=scene.Database().BeginTransaction("Move live tool collider");tx.ModifyComponent(machine.Tool,"CMachineCollisionComponent",{{"Collisions",boxes}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));undo->End();};
    edit(.001);const auto request=RapidRequest(scene,false);const auto sequence=std::stoull(request.at("expected").To<Map>().at("mutationSequence").To<Text>());const auto running=Invoke(&scene,"CamRapid","Generate",request);ASSERT_EQ("Running",running.at("status").To<Text>());
    // Do not pump/read the scene between capture and the live collider edit.
    // This isolates the collision component's own mutation notification.
    edit(.002);const auto changed=Read(scene);EXPECT_GT(std::stoull(changed.at("expected").To<Map>().at("mutationSequence").To<Text>()),sequence);
    const auto superseded=Wait(scene,running);EXPECT_EQ("Superseded",superseded.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(superseded);
    EXPECT_EQ(saved,setup->GetGeneratedRapidPaths());EXPECT_EQ(paths,setup->GetGeneratedToolpaths());EXPECT_EQ(overrides,setup->GetRapidOverrides());
}
