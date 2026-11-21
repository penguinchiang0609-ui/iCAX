namespace head_instance_tests {
    Map Instance(Scene& scene){return Head(scene).at("instance").To<Map>();}
    Map Command(Scene& scene,const Text& action){auto r=GenerateRequest(scene);r["action"]=action;return r;}
    Map InstanceAction(Scene& scene,const Text& action){return Invoke(&scene,"CamHead","Instance",Command(scene,action));}
    void SelectAB(Scene& scene){const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);for(const auto& v:sources){const auto source=v.To<Map>();if(source.contains("kind")&&source.at("kind").To<Text>()=="AB"){SelectInstalledHead(scene,source);return;}}throw std::runtime_error("Missing AB fixture");}
    void Apply(Scene& scene,const Map& parameters){auto r=Command(scene,"configure");r["parameters"]=parameters;Invoke(&scene,"CamHead","Instance",r);}
    void SelectSource(Scene& scene,const Text& kind){const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);for(const auto& value:sources){const auto source=value.To<Map>();if(source.contains("kind")&&source.at("kind").To<Text>()==kind){SelectInstalledHead(scene,source);return;}}throw std::runtime_error("Missing declared head kind "+kind);}
    void DeclareWorkpieceAxes(Scene& scene,int count){
        auto parameters=Instance(scene).at("parameters").To<Map>();Array axes;const auto nodeCount=Head(scene).at("assembly").To<Map>().at("nodes").To<Array>().size();
        Array origin;for(const auto value:iCAX::CAM::Motion::Identity())origin.emplace_back(value);
        for(int index=0;index<count;++index)axes.emplace_back(Map{{"index",static_cast<UInt>(nodeCount+index)},{"name","Work "+std::to_string(index+1)},{"target",Text("Workpiece")},
            {"velocityDegS",60.},{"origin",origin},{"axis",index?Array{0.,0.,1.}:Array{1.,0.,0.}},{"zeroOffsetDeg",0.},{"sign",1.},{"lowerDeg",-180.},{"upperDeg",180.}});
        for(const auto& axis:parameters.at("axes").To<Array>())axes.emplace_back(axis);parameters["axes"]=axes;Apply(scene,parameters);
    }
}
TEST(FiveAxisHeadMotion, WorkpieceBUsesTheSameNativeRelativeFKAndIKWithoutAMachineInstance){
    using namespace head_instance_tests;namespace M=iCAX::CAM::Motion;namespace K=iCAX::GeometryServices::MachineKinematics;
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareOrderPart(scene,true,"HeadTable");SelectSource(scene,"4+1");DeclareWorkpieceAxes(scene,1);const auto cad=Read(scene).at("workpieces");
    auto p=Instance(scene).at("parameters").To<Map>();auto axes=p.at("axes").To<Array>();ASSERT_EQ(2u,axes.size());
    auto work=axes.front().To<Map>();work["name"]=Text("B");work["target"]=Text("Workpiece");Array identity;for(auto x:M::Identity())identity.emplace_back(x);work["origin"]=identity;work["axis"]=Array{1.,0.,0.};work["zeroOffsetDeg"]=7.;work["sign"]=-1.;axes[0]=work;
    auto head=axes.back().To<Map>();head["name"]=Text("A");axes[1]=head;p["axes"]=axes;Apply(scene,p);
    const auto state=Read(scene);EXPECT_TRUE(state.at("setups").To<Array>().front().To<Map>().at("machineSetup").To<Map>().empty());
    auto snapshot=ReadMachineSimulation(scene,{{"setupId",state.at("activeSetupId")}});ASSERT_TRUE(snapshot.at("independentHead").To<bool>());
    K::Snapshot model;std::vector<double> seed;std::vector<M::Node> oracle;
    for(const auto& v:snapshot.at("nodes").To<Array>()){
        const auto n=v.To<Map>();K::Node k;M::Node m;k.ID=m.ID=n.at("id").To<Text>();k.Parent=m.Parent=n.at("parent").To<int>();m.Type=n.at("type").To<Text>();
        k.Type=m.Type=="fixed"?K::JointType::Fixed:m.Type=="prismatic"?K::JointType::Prismatic:K::JointType::Revolute;
        for(int j=0;j<16;++j)k.Origin[j]=m.Origin[j]=n.at("origin").To<Array>()[j].To<double>();for(int j=0;j<3;++j)k.Axis[j]=m.Axis[j]=n.at("axis").To<Array>()[j].To<double>();
        k.Lower=m.Lower=n.at("lower").To<double>();k.Upper=m.Upper=n.at("upper").To<double>();model.Nodes.push_back(k);oracle.push_back(m);
    }
    for(const auto& q:snapshot.at("positions").To<Array>())seed.push_back(q.To<double>());
    const auto tool=snapshot.at("tool").To<int>(),support=snapshot.at("support").To<int>(),workIndex=oracle[support].Parent;
    ASSERT_EQ("revolute",oracle[workIndex].Type);EXPECT_EQ((M::Vector{-1.,0.,0.}),oracle[workIndex].Axis);
    auto desired=seed;desired[workIndex]=.4;desired[1]=40;desired[2]=20;
    K::Task task;task.ToolFrameID=oracle[tool].ID;task.ReferenceFrameID=oracle[support].ID;
    const auto motion=Head(scene).at("motion").To<Map>();for(int j=0;j<3;++j){task.TCP[j]=motion.at("tcp").To<Array>()[j].To<double>();task.Beam[j]=motion.at("beam").To<Array>()[j].To<double>();}
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineKinematicsService>();ASSERT_TRUE(service);const auto built=service->BuildModel(model);const auto pose=service->Forward(built,desired,task);
    const auto world=M::Forward(oracle,desired);const auto relative=M::Multiply(M::Inverse(world[support]),world[tool]);const auto point=M::Transform(relative,task.TCP),direction=M::Transform(relative,task.Beam,true);
    for(int j=0;j<3;++j){EXPECT_NEAR(point[j],pose.PositionMm[j],1e-8);EXPECT_NEAR(direction[j],pose.Direction[j],1e-8);}
    K::Request request;request.TaskDefinition=task;request.Goal.PositionMm=pose.PositionMm;request.Goal.Direction=pose.Direction;request.Seeds={seed};request.MaxSeeds=8;request.MaxCandidates=2;request.MaxTimeMs=10000;
    const auto solved=service->InverseCandidates(built,request);ASSERT_EQ(K::Status::Solved,solved.State)<<solved.Diagnostic;ASSERT_FALSE(solved.Candidates.empty());EXPECT_LT(solved.Candidates.front().PositionErrorMm,.01);EXPECT_LT(solved.Candidates.front().DirectionErrorRad,1e-4);
    work["name"]=Text("C");axes[0]=work;p["axes"]=axes;Apply(scene,p);const auto renamed=ReadMachineSimulation(scene,{{"setupId",state.at("activeSetupId")}});
    const auto physical=[](const Variant& value){auto nodes=value.To<Array>();for(auto& item:nodes){auto node=item.To<Map>();node.erase("name");item=node;}return nodes;};
    EXPECT_EQ(physical(snapshot.at("nodes")),physical(renamed.at("nodes")));EXPECT_EQ(snapshot.at("positions"),renamed.at("positions"));EXPECT_EQ(cad,Read(scene).at("workpieces"));
}
TEST(FiveAxisHeadMotion, WorkpieceCradleIsSerialAndTravelsWithConfigurationAndProject){
    using namespace head_instance_tests;namespace M=iCAX::CAM::Motion;Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);scene.FixtureLibraryStore=TestFixtureLibraryStore();PrepareOrderPart(scene,true,"TableTable");SelectSource(scene,"3+2");DeclareWorkpieceAxes(scene,2);
    auto p=Instance(scene).at("parameters").To<Map>();auto axes=p.at("axes").To<Array>();Array identity;for(auto x:M::Identity())identity.emplace_back(x);
    for(int i=0;i<2;++i){auto a=axes[i].To<Map>();a["target"]=Text("Workpiece");a["name"]=Text(i?"B":"A");a["origin"]=identity;a["axis"]=i?Array{0.,0.,1.}:Array{1.,0.,0.};axes[i]=a;}p["axes"]=axes;Apply(scene,p);
    const auto motion=Head(scene).at("motion").To<Map>();const auto nodes=motion.at("nodes").To<Array>();const auto inner=nodes.at(motion.at("support").To<int>()).To<Map>().at("parent").To<int>(),outer=nodes[inner].To<Map>().at("parent").To<int>();
    EXPECT_EQ(0,nodes[outer].To<Map>().at("parent").To<int>());EXPECT_EQ("revolute",nodes[outer].To<Map>().at("type").To<Text>());EXPECT_EQ("revolute",nodes[inner].To<Map>().at("type").To<Text>());
    const auto savedMotion=Head(scene).at("motion");auto save=Command(scene,"save");save["name"]=Text("摇篮配置");const auto record=Invoke(&scene,"CamHead","Configurations",save);
    auto load=Command(scene,"use");load["entryId"]=record.at("id");Invoke(&scene,"CamHead","Configurations",load);EXPECT_EQ(p,Instance(scene).at("parameters").To<Map>());
    auto expected=Head(scene);File temp(BRepPrimAPI_MakeBox(1,1,1).Shape());auto path=temp.Path;path.replace_extension(".i5cam");SaveProductPlanFixture(scene,path);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(scene.Project,scene.GetSceneID(),false,true);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(expected,Head(reopened));std::filesystem::remove(path);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(savedMotion,Head(scene).at("motion"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(expected,Head(scene));
}
TEST(FiveAxisHeadMotion, GeneratedGeometrySurvivesRejectedReplacementOfAPhysicalABHeadByWorkpieceOnlyAxes){
    using namespace head_instance_tests;Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareProductPlanPaths(scene);SelectAB(scene);
    auto parameters=Instance(scene).at("parameters").To<Map>();auto work=parameters.at("axes").To<Array>().front().To<Map>();
    Array identity;for(auto x:iCAX::CAM::Motion::Identity())identity.emplace_back(x);work["origin"]=identity;work["axis"]=Array{1.,0.,0.};work["name"]=Text("B");work["target"]=Text("Workpiece");work["zeroOffsetDeg"]=20.;parameters["axes"]=Array{work};
    const auto before=Read(scene);const auto cuts=Generated(scene);const auto undo=scene.Database().GetUndoArray();
    EXPECT_THROW(Apply(scene,parameters),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(cuts,Generated(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray());
}
TEST(FiveAxisHeadInstance, SharedSetupReferencesCloneIsolationAndUndo){
    using namespace head_instance_tests;const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);SelectAB(scene);
    const auto first=Read(scene);const auto id=Instance(scene).at("id");ASSERT_EQ(1u,first.at("headInstances").To<Array>().size());
    auto create=GenerateRequest(scene);create.erase("setupId");create["placementId"]=first.at("placements").To<Array>().front().To<Map>().at("id");create["action"]=Text("create");create["name"]=Text("第二位姿");create["machineType"]=Text("HeadHead");Invoke(&scene,"CamSetup","Manage",create);
    // New poses inherit defaults. Explicitly share this instance to retain the
    // shared calibration/clone coverage independently of creation semantics.
    auto share=Command(scene,"select");share["instanceId"]=id;Invoke(&scene,"CamHead","Instance",share);
    const auto shared=Read(scene);ASSERT_EQ(2u,shared.at("setups").To<Array>().size());for(const auto& s:shared.at("setups").To<Array>())EXPECT_EQ(id,s.To<Map>().at("headInstanceId"));
    auto parameters=Instance(scene).at("parameters").To<Map>();parameters["tcp"]=Array{2.,3.,-225.};Apply(scene,parameters);
    for(const auto& s:Read(scene).at("setups").To<Array>())EXPECT_EQ((Array{2.,3.,-225.}),s.To<Map>().at("headModel").To<Map>().at("tcpLocalPosition").To<Array>());
    const auto clone=InstanceAction(scene,"clone").at("instanceId");EXPECT_NE(id,clone);auto local=Instance(scene).at("parameters").To<Map>();local["tcp"]=Array{2.,3.,-230.};Apply(scene,local);
    const auto changed=Read(scene);EXPECT_EQ(2u,changed.at("headInstances").To<Array>().size());
    for(const auto& s:changed.at("setups").To<Array>()){const auto setup=s.To<Map>();const auto expected=setup.at("headInstanceId")==clone?-230.:-225.;EXPECT_DOUBLE_EQ(expected,setup.at("headModel").To<Map>().at("tcpLocalPosition").To<Array>()[2].To<double>());}
    ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(-225.,Head(scene).at("tcpLocalPosition").To<Array>()[2].To<double>());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(changed.at("headInstances"),Read(scene).at("headInstances"));
    EXPECT_EQ(first.at("workpieces"),changed.at("workpieces"));
}
TEST(FiveAxisHeadInstance, GaugeZeroSignAndLimitsCompileTheSameEffectiveChain){
    using namespace head_instance_tests;namespace M=iCAX::CAM::Motion;const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);SelectAB(scene);const auto cad=Read(scene).at("workpieces");
    const auto base=Head(scene);auto p=Instance(scene).at("parameters").To<Map>();p["tcpMode"]=Text("Gauge");p["gaugeOrigin"]=Array{1.,2.,3.};p["lengthMm"]=225.;p["beam"]=Array{0.,0.,-2.};
    auto axes=p.at("axes").To<Array>();auto axis=axes.front().To<Map>();axis["zeroOffsetDeg"]=10.;axis["sign"]=-1.;axis["lowerDeg"]=20.;axis["upperDeg"]=80.;
    const auto axisIndex=axis.at("index").To<UInt>();axis["index"]=static_cast<int>(axisIndex);axes[0]=axis;p["axes"]=axes;Apply(scene,p);
    const auto head=Head(scene),assembly=head.at("assembly").To<Map>();const auto nodes=assembly.at("nodes").To<Array>();const auto index=axisIndex;const auto node=nodes[index].To<Map>();
    EXPECT_EQ((Array{1.,2.,-222.}),head.at("tcpLocalPosition").To<Array>());EXPECT_EQ(head.at("tcpLocalPosition"),assembly.at("tcp"));EXPECT_EQ((Array{0.,0.,-1.}),assembly.at("beam").To<Array>());
    EXPECT_NEAR(20.*std::numbers::pi/180,node.at("position").To<double>(),1e-12);EXPECT_EQ(-1.,node.at("directionSign").To<double>());
    auto matrix=[](const Variant& v){M::Matrix r{};const auto a=v.To<Array>();for(int i=0;i<16;++i)r[i]=a[i].To<double>();return r;};
    const auto original=base.at("assembly").To<Map>().at("nodes").To<Array>()[index].To<Map>();M::Node declared;declared.Type="revolute";const auto a=original.at("axis").To<Array>();for(int k=0;k<3;++k)declared.Axis[k]=a[k].To<double>();
    M::Node effective;effective.Type="revolute";const auto effectiveAxis=node.at("axis").To<Array>();for(int k=0;k<3;++k)effective.Axis[k]=effectiveAxis[k].To<double>();
    const auto expected=M::Multiply(matrix(original.at("origin")),M::Joint(declared,-20.*std::numbers::pi/180+10.*std::numbers::pi/180));
    const auto actual=M::Multiply(matrix(node.at("origin")),M::Joint(effective,20.*std::numbers::pi/180));for(int k=0;k<16;++k)EXPECT_NEAR(expected[k],actual[k],1e-12);
    EXPECT_EQ(cad,Read(scene).at("workpieces"));auto invalid=Instance(scene).at("parameters").To<Map>();invalid["lengthMm"]=-1.;const auto before=Read(scene);EXPECT_THROW(Apply(scene,invalid),std::exception);EXPECT_EQ(before.at("headInstances"),Read(scene).at("headInstances"));
    for(const double bad:{-1.,1.5,99999.}){auto q=Instance(scene).at("parameters").To<Map>();auto aa=q.at("axes").To<Array>();auto a=aa.front().To<Map>();a["index"]=bad;aa[0]=a;q["axes"]=aa;EXPECT_THROW(Apply(scene,q),std::exception);EXPECT_EQ(before.at("headInstances"),Read(scene).at("headInstances"));}
}
TEST(FiveAxisHeadInstance, ProfileCopiesTravelWithoutSourceOrLibraryAndKeepIndependentEdits){
    using namespace head_instance_tests;auto store=TestFixtureLibraryStore();Scene source;source.FixtureLibraryStore=store;PrepareOrderPart(source,true,"TableTable");File body(BRepPrimAPI_MakeBox(10,20,30).Shape());ASSERT_EQ("Completed",Wait(source,Invoke(&source,"CamHead","Import",ImportTestHead(source,body))).at("status").To<Text>());
    auto p=Instance(source).at("parameters").To<Map>();p["tcp"]=Array{1.,2.,3.};Apply(source,p);
    auto save=Command(source,"save");save["name"]=Text("客户甲刀头");const auto saved=Invoke(&source,"CamHead","Configurations",save);EXPECT_EQ(saved,Invoke(&source,"CamHead","Configurations",save));
    std::filesystem::remove(body.Path);Scene target;target.FixtureLibraryStore=store;PrepareOrderPart(target,true,"TableTable");auto load=Command(target,"use");load["entryId"]=saved.at("id");Invoke(&target,"CamHead","Configurations",load);
    const auto original=Head(target);EXPECT_NE(Instance(source).at("id"),Instance(target).at("id"));EXPECT_EQ((Array{1.,2.,3.}),original.at("tcpLocalPosition").To<Array>());
    auto edit=Instance(target).at("parameters").To<Map>();edit["tcp"]=Array{4.,5.,6.};Apply(target,edit);EXPECT_EQ((Array{1.,2.,3.}),Head(source).at("tcpLocalPosition").To<Array>());
    auto remove=Command(source,"delete");remove["entryId"]=saved.at("id");remove["revision"]=saved.at("revision");Invoke(&source,"CamHead","Configurations",remove);EXPECT_EQ((Array{4.,5.,6.}),Head(target).at("tcpLocalPosition").To<Array>());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=target.Project;info.MainSceneID=target.GetSceneID();info.ProjectName="工程刀头实例";
    auto path=body.Path;path.replace_extension(".i5cam");file.Save(path,info,target.Database(),target.Resources());Scene reopened(target.Project,target.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(Head(target),Head(reopened));EXPECT_EQ(Read(target).at("headInstances"),Read(reopened).at("headInstances"));const auto ref=Head(reopened).at("previewRef").To<Map>();EXPECT_TRUE(reopened.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>())));std::filesystem::remove(path);
}
TEST(FiveAxisHeadInstance, ReferencedDeletionLockedUsersAndStaleEditsAreRejected){
    using namespace head_instance_tests;Scene scene;PrepareOrderPart(scene,true,"TableTable");File body(BRepPrimAPI_MakeBox(10,20,30).Shape());ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,body))).at("status").To<Text>());
    const auto before=Read(scene);auto remove=Command(scene,"delete");remove["instanceId"]=Instance(scene).at("id");EXPECT_THROW(Invoke(&scene,"CamHead","Instance",remove),std::exception);
    auto stale=Command(scene,"rename");stale["name"]=Text("旧请求");auto rename=Command(scene,"rename");rename["name"]=Text("短名称");Invoke(&scene,"CamHead","Instance",rename);EXPECT_EQ(Text("短名称"),Instance(scene).at("name").To<Text>());EXPECT_THROW(Invoke(&scene,"CamHead","Instance",stale),std::exception);
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup->SetProperty("Locked",true));EXPECT_THROW(InstanceAction(scene,"clone"),std::exception);ASSERT_TRUE(setup->SetProperty("Locked",false));
    auto deselect=Command(scene,"select");deselect["instanceId"]=Text{};Invoke(&scene,"CamHead","Instance",deselect);remove=Command(scene,"delete");remove["instanceId"]=before.at("headInstances").To<Array>().front().To<Map>().at("id");Invoke(&scene,"CamHead","Instance",remove);EXPECT_TRUE(Read(scene).at("headInstances").To<Array>().empty());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(1u,Read(scene).at("headInstances").To<Array>().size());
}
TEST(FiveAxisHeadInstance, BoundGaugeAndMachineEditsShareOneTCPAndRespectLockedReferences){
    using namespace head_instance_tests;Scene scene;PrepareOrderPart(scene,true,"TableTable");const auto machine=CreateProductPlanMachine(scene,true);const auto cad=Read(scene).at("workpieces");
    const auto id=Instance(scene).at("id");EXPECT_EQ(iCAX::Data::to_string(machine.Machine),Instance(scene).at("binding").To<Map>().at("machineId").To<Text>());
    auto p=Instance(scene).at("parameters").To<Map>();p["tcpMode"]=Text("Gauge");p["gaugeOrigin"]=Array{2.,3.,4.};p["beam"]=Array{0.,0.,-2.};p["lengthMm"]=5.;Apply(scene,p);
    EXPECT_EQ((Array{2.,3.,-1.}),Head(scene).at("tcpLocalPosition").To<Array>());
    auto configuration=Invoke(&scene,"CamSimulation","ReadMachine",{{"setupId",Read(scene).at("activeSetupId")}}).at("configuration").To<Map>();
    EXPECT_EQ(id,configuration.at("headInstanceId"));EXPECT_EQ(Head(scene).at("tcpLocalPosition"),configuration.at("tcp"));EXPECT_EQ((Array{0.,0.,-1.}),configuration.at("beam").To<Array>());
    EXPECT_THROW(InstanceAction(scene,"clone"),std::exception);
    auto create=GenerateRequest(scene);create.erase("setupId");create["placementId"]=Read(scene).at("placements").To<Array>().front().To<Map>().at("id");create["action"]=Text("create");create["name"]=Text("共用绑定刀头");create["machineType"]=Text("TableTable");Invoke(&scene,"CamSetup","Manage",create);
    auto share=Command(scene,"select");share["instanceId"]=id;Invoke(&scene,"CamHead","Instance",share);
    const auto active=Read(scene).at("activeSetupId").To<Text>();ID other;
    for(const auto& value:Read(scene).at("setups").To<Array>()){const auto s=value.To<Map>();EXPECT_EQ(id,s.at("headInstanceId"));if(s.at("id").To<Text>()!=active)other=*ID::from_string(s.at("id").To<Text>());}
    const auto locked=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(other)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(locked->SetProperty("Locked",true));
    configuration["tcp"]=Array{1.,2.,3.};auto configure=GenerateRequest(scene);configure["configuration"]=configuration;EXPECT_THROW(Invoke(&scene,"CamSimulation","ConfigureMachine",configure),std::exception);EXPECT_EQ((Array{2.,3.,-1.}),Head(scene).at("tcpLocalPosition").To<Array>());
    ASSERT_TRUE(locked->SetProperty("Locked",false));configure=GenerateRequest(scene);configure["configuration"]=configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
    for(const auto& value:Read(scene).at("setups").To<Array>())EXPECT_EQ((Array{1.,2.,3.}),value.To<Map>().at("headModel").To<Map>().at("tcpLocalPosition").To<Array>());
    EXPECT_EQ("Coordinates",Instance(scene).at("parameters").To<Map>().at("tcpMode").To<Text>());EXPECT_EQ(cad,Read(scene).at("workpieces"));
}
