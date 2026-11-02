// Split tool/workpiece chains must solve and certify motion in the support
// frame. These independent trees deliberately have no asset names or labels.
namespace split_chain_tests {
    namespace K=iCAX::GeometryServices::MachineKinematics;
    namespace P=iCAX::GeometryServices::MachineMotion;
    namespace M=iCAX::CAM::Motion;
    struct Tree {
        K::Snapshot Model;K::Task Task;
        std::vector<M::Node> Oracle;
        std::map<Text,std::size_t> Axes;
        std::size_t Tool=0,Support=0;
    };
    Tree MakeTree(bool cradle){
        Tree t;
        const auto add=[&](Text id,int parent,K::JointType type,K::Vector axis){
            K::Node n;n.ID=id;n.Parent=parent;n.Type=type;n.Axis=axis;
            n.Lower=type==K::JointType::Prismatic?-500:-2.5;
            n.Upper=type==K::JointType::Prismatic?500:2.5;
            if(type==K::JointType::Fixed)n.Lower=n.Upper=0;
            M::Node o;o.ID=id;o.Parent=parent;o.Axis=axis;o.Lower=n.Lower;o.Upper=n.Upper;
            o.Type=type==K::JointType::Fixed?"fixed":type==K::JointType::Prismatic?"prismatic":"revolute";
            const auto index=t.Model.Nodes.size();t.Model.Nodes.push_back(n);t.Oracle.push_back(o);
            if(type!=K::JointType::Fixed)t.Axes.emplace(id,index);
            return static_cast<int>(index);
        };
        const auto root=add("base",-1,K::JointType::Fixed,{1,0,0});
        auto tip=add("X",root,K::JointType::Prismatic,{1,0,0});
        tip=add("Y",tip,K::JointType::Prismatic,{0,1,0});
        tip=add("Z",tip,K::JointType::Prismatic,{0,0,1});
        if(!cradle)tip=add("B",tip,K::JointType::Revolute,{0,1,0});
        t.Tool=add("nozzle",tip,K::JointType::Fixed,{1,0,0});
        auto table=root;if(cradle)table=add("A",table,K::JointType::Revolute,{1,0,0});
        table=add("C",table,K::JointType::Revolute,{0,0,1});
        t.Support=add("chuck",table,K::JointType::Fixed,{1,0,0});
        t.Task.ToolFrameID=t.Model.Nodes[t.Tool].ID;t.Task.ReferenceFrameID=t.Model.Nodes[t.Support].ID;
        t.Task.TCP={0,0,-50};t.Task.Beam={0,0,-1};return t;
    }
    Tree Interleave(const Tree& source){
        // Still parent-before-child, but a support joint splits the tool
        // subtree. Source slots are an owner contract and must remain intact.
        const auto order=source.Axes.contains("A")?std::vector<std::size_t>{0,1,5,2,6,3,7,4}:std::vector<std::size_t>{0,1,6,2,7,3,4,5};
        Tree t;t.Task=source.Task;std::vector<int> indices(order.size());for(std::size_t i=0;i<order.size();++i)indices[order[i]]=static_cast<int>(i);
        for(const auto old:order){auto n=source.Model.Nodes[old];auto o=source.Oracle[old];if(n.Parent>=0)n.Parent=o.Parent=indices[n.Parent];const auto i=t.Model.Nodes.size();t.Model.Nodes.push_back(n);t.Oracle.push_back(o);if(old==source.Tool)t.Tool=i;if(old==source.Support)t.Support=i;for(const auto& [name,index]:source.Axes)if(index==old)t.Axes.emplace(name,i);}
        return t;
    }
    std::vector<double> Values(const Tree& t,double phase=0){
        std::vector<double> q(t.Model.Nodes.size());q[t.Axes.at("X")]=35+12*phase;
        q[t.Axes.at("Y")]=-20+7*phase;q[t.Axes.at("Z")]=150+9*phase;
        q[t.Axes.at(t.Axes.contains("A")?"A":"B")]=.4+.08*phase;q[t.Axes.at("C")]=.55+.09*phase;
        return q;
    }
    K::ForwardResult Independent(const Tree& t,const std::vector<double>& q){
        const auto world=M::Forward(t.Oracle,q);K::ForwardResult f;
        f.RelativeTool=M::Multiply(M::Inverse(world[t.Support]),world[t.Tool]);
        f.PositionMm=M::Transform(f.RelativeTool,t.Task.TCP);f.Direction=M::Transform(f.RelativeTool,t.Task.Beam,true);
        return f;
    }
    void CheckIndependent(const Tree& t,const K::ForwardResult& f,const std::vector<double>& q){
        const auto expected=Independent(t,q);
        for(int k=0;k<3;++k){EXPECT_NEAR(expected.PositionMm[k],f.PositionMm[k],1e-8);EXPECT_NEAR(expected.Direction[k],f.Direction[k],1e-10);}
    }
    struct ProductTree {Tree Geometry;std::vector<ID> IDs;Map Configuration,Post;};
    ProductTree Create(Scene& scene,bool cradle){
        ProductTree p;p.Geometry=MakeTree(cradle);for(std::size_t i=0;i<p.Geometry.Model.Nodes.size();++i)p.IDs.push_back(iCAX::Data::GenerateNewUUID());
        const auto root=scene.Database().CreateEntity(p.IDs[0]);PlanAttach(root,"CMachineInstanceComponent",{{"Name",Text(cradle?"Independent cradle":"Independent head/table")}});PlanAttach(root,"CTransformComponent",{});
        for(std::size_t i=1;i<p.IDs.size();++i){
            const auto& n=p.Geometry.Model.Nodes[i];const auto entity=scene.Database().CreateEntity(p.IDs[i]);
            PlanAttach(entity,"CTransformComponent",{{"ParentEntityID",p.IDs[n.Parent]}});
            PlanAttach(entity,"CMachineElementComponent",{{"MachineID",p.IDs[0]},{"Name",n.ID},{"ElementKind",Text("part")},{"RestPose",Array{0.,0.,0.,0.,0.,0.}}});
            // Intermediate synthetic nodes are empty coordinate frames. Only
            // the authored tool and support have physical collision bodies.
            auto disabled=PlanBox(0,0,0,.001).front().To<Map>();disabled["enabled"]=false;Array box{disabled};if(i==p.Geometry.Tool)box=PlanBox(0,0,-.03,.002);if(i==p.Geometry.Support){auto support=PlanBox(0,0,-1.,.001).front().To<Map>();support["geometry"]=Map{{"type",Text("box")},{"size",Array{1.,1.,.001}}};box=Array{support};}
            PlanAttach(entity,"CMachineCollisionComponent",{{"Collisions",box}});
            if(n.Type!=K::JointType::Fixed)PlanAttach(entity,"CMachineJointComponent",{{"JointName",n.ID},{"JointType",Text(n.Type==K::JointType::Prismatic?"prismatic":"revolute")},{"Axis",Array{n.Axis[0],n.Axis[1],n.Axis[2]}},{"LowerLimit",n.Lower},{"UpperLimit",n.Upper},{"VelocityLimit",n.Type==K::JointType::Prismatic?100.:1.},{"Position",0.}});
        }
        const auto id=[&](std::size_t i){return iCAX::Data::to_string(p.IDs[i]);};
        p.Configuration={{"machineId",id(0)},{"toolId",id(p.Geometry.Tool)},{"supportId",id(p.Geometry.Support)},{"placement",Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}},{"tcp",Array{0.,0.,-50.}},{"beam",Array{0.,0.,-1.}},{"toolCollisionRepresentation",Text("Boxes")}};
        auto configure=GenerateRequest(scene);configure["configuration"]=p.Configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
        Array axes;for(const auto& [word,index]:p.Geometry.Axes)axes.emplace_back(Map{{"jointId",id(index)},{"word",word},{"sign",word=="C"?-1.:1.},{"offset",word=="C"?17.:0.}});
        p.Post={{"name",Text("Independent split chain post")},{"format",Text("JointJSON")},{"extension",Text("json")},{"axes",axes},{"header",Text("G90\nG93")},{"footer",Text("M30")},{"processOn",Text("M3")},{"processOff",Text("M5")},{"motion",Text("G1 {axes} F{feed}")},{"cutFeedMmMin",600.},{"travelFeedMmMin",1200.}};
        Invoke(&scene,"CamSimulation","ConfigureMachinePost",{{"expected",Read(scene).at("expected")},{"machineId",id(0)},{"postprocessor",p.Post}});return p;
    }
    void Set(Scene& scene,const ProductTree& p,const std::vector<double>& q){
        for(const auto& [name,i]:p.Geometry.Axes){Text error;const auto joint=scene.Database().GetEntity(p.IDs[i])->GetComponent("CMachineJointComponent");if(!joint->SetProperty("Position",q[i],error))throw std::runtime_error(error);}
    }
    Map Move(Scene& scene,const ProductTree& p,const Text& name,double q){
        return CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",Map{{iCAX::Data::to_string(p.IDs[p.Geometry.Axes.at(name)]),q}}}});
    }
}

TEST(FiveAxisSplitChainKinematics, BothLayoutsMatchIndependentFKAndEveryActiveJacobianColumn)
{
    namespace S=split_chain_tests;
    Scene scene;const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineKinematicsService>();ASSERT_TRUE(service);
    for(const auto cradle:{false,true})for(const auto interleaved:{false,true}){
        SCOPED_TRACE(cradle?"3+2 cradle":"4+1 head/table");
        SCOPED_TRACE(interleaved?"interleaved source slots":"depth first source slots");const auto source=S::MakeTree(cradle);const auto t=interleaved?S::Interleave(source):source;const auto q=S::Values(t);const auto model=service->BuildModel(t.Model);
        const auto actual=service->Forward(model,q,t.Task);S::CheckIndependent(t,actual,q);const auto j=service->Jacobian(model,q,t.Task);EXPECT_EQ(5u,j.Rank);
        for(const auto& [axis,i]:t.Axes){SCOPED_TRACE(axis);const auto h=t.Model.Nodes[i].Type==S::K::JointType::Prismatic?1e-3:1e-6;auto a=q,b=q;a[i]-=h;b[i]+=h;const auto fa=S::Independent(t,a),fb=S::Independent(t,b);
            for(int k=0;k<3;++k){EXPECT_NEAR((fb.PositionMm[k]-fa.PositionMm[k])/(2*h),j.Values[k*q.size()+i],3e-5);EXPECT_NEAR((fb.Direction[k]-fa.Direction[k])/(2*h),j.Values[(k+3)*q.size()+i],3e-7);}
        }
        S::K::Request r;r.TaskDefinition=t.Task;r.Goal.PositionMm=actual.PositionMm;r.Goal.Direction=actual.Direction;r.Seeds={S::Values(t,-1)};r.MaxSeeds=2;r.MaxCandidates=2;r.MaxTimeMs=2000;
        const auto solved=service->InverseCandidates(model,r);ASSERT_EQ(S::K::Status::Solved,solved.State)<<solved.Diagnostic;ASSERT_FALSE(solved.Candidates.empty());
        for(const auto& c:solved.Candidates){const auto independent=S::Independent(t,c.Positions);for(int k=0;k<3;++k){EXPECT_NEAR(r.Goal.PositionMm[k],independent.PositionMm[k],.0100001);EXPECT_NEAR(r.Goal.Direction[k],independent.Direction[k],.000011);}for(const auto& [axis,i]:t.Axes)EXPECT_GT(std::abs(c.Positions[i]-r.Seeds[0][i]),.001)<<axis;}
        r.Goal.PositionMm={10000,20000,30000};r.MaxSeeds=2;r.MaxIterations=30;const auto impossible=service->InverseCandidates(model,r);EXPECT_NE(S::K::Status::Solved,impossible.State);EXPECT_TRUE(impossible.Candidates.empty());
    }
}

TEST(FiveAxisSplitChainMotion, AllFivePhysicalAxesPlanRelativeToMovingSupportAndRejectionsHaveNoPlayback)
{
    namespace S=split_chain_tests;Scene scene;const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineMotionPlanningService>();ASSERT_TRUE(service);
    for(const auto cradle:{false,true}){
        SCOPED_TRACE(cradle?"3+2 cradle":"4+1 head/table");const auto t=S::MakeTree(cradle);S::P::Query q;q.Machine=t.Model;q.TaskDefinition=t.Task;q.InitialPositions=S::Values(t,-.3);q.MaximumSeconds=30;q.IKTimeMsPerTarget=1000;q.IKSeedsPerTarget=2;q.IKCandidatesPerTarget=2;
        for(const auto& n:t.Model.Nodes){const auto fixed=n.Type==S::K::JointType::Fixed;q.MaximumVelocities.push_back(fixed?0:n.Type==S::K::JointType::Prismatic?100.:1.);q.MaximumAccelerations.push_back(fixed?0:n.Type==S::K::JointType::Prismatic?1000.:10.);q.MaximumJerks.push_back(fixed?0:n.Type==S::K::JointType::Prismatic?10000.:100.);q.MaximumAxisSteps.push_back(fixed?0:n.Upper-n.Lower);}
        for(int i=0;i<3;++i){const auto pose=S::Independent(t,S::Values(t,.3*i));S::P::Station s;s.ID="station-"+std::to_string(i);s.Role="Cut";s.Goal.PositionMm=pose.PositionMm;s.Goal.Direction=pose.Direction;s.FeedMmPerSecond=10;q.Stations.push_back(s);}
        std::set<std::size_t> certified;q.CheckState=[](auto,const auto&){return S::P::Validation{S::P::Verdict::Accepted,{}};};q.CheckMotion=[&](auto edge,const auto&,const auto&){certified.insert(edge);return S::P::Validation{S::P::Verdict::Accepted,{}};};q.CheckTrajectory=[](const auto&){return S::P::Validation{S::P::Verdict::Accepted,{}};};
        q.ReferenceAt=[&](std::size_t edge,double u,const auto& first,const auto& second){S::P::Reference r;for(int k=0;k<3;++k)r.Goal.PositionMm[k]=std::lerp(q.Stations[edge-1].Goal.PositionMm[k],q.Stations[edge].Goal.PositionMm[k],u);S::M::Vector d{};for(int k=0;k<3;++k)d[k]=std::lerp(first[k],second[k],u);r.Goal.Direction=S::M::Unit(d);return r;};
        const auto result=service->Plan(q);ASSERT_EQ(S::P::Status::Solved,result.State)<<result.Diagnostic;ASSERT_GT(result.Samples.size(),3u);EXPECT_EQ((std::set<std::size_t>{0,1,2}),certified);
        for(const auto& s:result.Samples){const auto actual=S::Independent(t,s.Positions);for(int k=0;k<3;++k){EXPECT_NEAR(actual.PositionMm[k],s.TCP[k],1e-7);EXPECT_NEAR(actual.Direction[k],s.Direction[k],1e-9);}}
        for(const auto& [axis,i]:t.Axes)EXPECT_GT(std::abs(result.Samples.back().Positions[i]-result.Samples.front().Positions[i]),.001)<<axis;
        q.CheckMotion=[](auto edge,const auto&,const auto&){return S::P::Validation{edge==1?S::P::Verdict::Rejected:S::P::Verdict::Accepted,"Physical support sweep collision"};};const auto blocked=service->Plan(q);EXPECT_EQ(S::P::Status::Rejected,blocked.State);EXPECT_TRUE(blocked.Samples.empty());
        q.CheckMotion=[](auto,const auto&,const auto&){return S::P::Validation{S::P::Verdict::Unresolved,"Owner collision proof incomplete"};};const auto unresolved=service->Plan(q);EXPECT_EQ(S::P::Status::Unresolved,unresolved.State);EXPECT_TRUE(unresolved.Samples.empty());
    }
}

TEST(FiveAxisSplitChainProduct, RotatingSupportSweepsBothRealWorkpieceAndFixtureMeshesIntoFixedTool)
{
    namespace S=split_chain_tests;
    for(const auto cradle:{false,true})for(const auto fixture:{false,true}){
        SCOPED_TRACE(cradle?"3+2 cradle A rotation":"4+1 table C rotation");
        SCOPED_TRACE(fixture?"fixture":"workpiece");Scene scene;
        const S::M::Vector center=cradle?S::M::Vector{20,100,20}:S::M::Vector{100,20,20};File shape(BRepPrimAPI_MakeBox(gp_Pnt(center[0]-3,center[1]-3,center[2]-3),6,6,6).Shape());Text obstacle;
        if(fixture){auto r=GenerateRequest(scene);r["sourcePath"]=shape.Utf8();const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",r));ASSERT_EQ("Completed",imported.at("status").To<Text>());obstacle=imported.at("result").To<Map>().at("fixtureId").To<Text>();}
        else {const auto imported=Imported(scene,shape);obstacle=imported.at("placementId").To<Text>();auto r=PlacementRequest(scene,imported);Array identity;for(const auto x:S::M::Identity())identity.emplace_back(x);r["transform"]=identity;Invoke(&scene,"CamPlacement","SetTransform",r);}
        const auto p=S::Create(scene,cradle);
        auto target=std::vector<double>(p.Geometry.Model.Nodes.size(),0.);const auto rotating=cradle?"A":"C";target[p.Geometry.Axes.at(rotating)]=std::numbers::pi/2;
        const auto world=S::M::Forward(p.Geometry.Oracle,target);const auto contact=S::M::Transform(world[p.Geometry.Support],center);
        auto initial=target;initial[p.Geometry.Axes.at(rotating)]=0;initial[p.Geometry.Axes.at("X")]=contact[0];initial[p.Geometry.Axes.at("Y")]=contact[1];initial[p.Geometry.Axes.at("Z")]=contact[2]+30;
        S::Set(scene,p,initial);const auto clear=S::Move(scene,p,rotating,0);ASSERT_EQ("Clear",clear.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(clear);
        const auto blocked=S::Move(scene,p,rotating,std::numbers::pi/2);ASSERT_EQ("Blocked",blocked.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(blocked);
        const auto bodies=blocked.at("collisionBodies").To<Array>();EXPECT_NE(bodies.end(),std::find(bodies.begin(),bodies.end(),Variant(obstacle)));EXPECT_TRUE(blocked.contains("collisionWitnessFraction"));
        EXPECT_DOUBLE_EQ(0,scene.Database().GetEntity(p.IDs[p.Geometry.Axes.at(rotating)])->GetComponent("CMachineJointComponent")->GetProperty("Position").To<double>());
    }
}

TEST(FiveAxisSplitChainFixtures, ExportRepresentativeRotationCupOnSupportZeroPlane)
{
    const auto outer=BRepPrimAPI_MakeCylinder(100.,140.).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,10),gp::DZ()),90.,140.).Shape();
    const auto cup=BRepAlgoAPI_Cut(outer,bore).Shape();ASSERT_FALSE(cup.IsNull());ASSERT_TRUE(BRepCheck_Analyzer(cup).IsValid());
    GProp_GProps mass;BRepGProp::VolumeProperties(cup,mass);EXPECT_NEAR(std::numbers::pi*(100*100*140.-90*90*130.),mass.Mass(),.001);
    Bnd_Box bounds;BRepBndLib::Add(cup,bounds);double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);EXPECT_NEAR(0,zmin,1e-5);EXPECT_NEAR(140,zmax,1e-5);
    const auto output=std::filesystem::path(__FILE__).parent_path()/"../../../../../../output/tests/split-chain-machines-20261024/rotary-cup.step";
    std::filesystem::create_directories(output.parent_path());STEPControl_Writer writer;ASSERT_EQ(IFSelect_RetDone,writer.Transfer(cup,STEPControl_AsIs));std::ofstream stream(output.lexically_normal(),std::ios::binary);ASSERT_EQ(IFSelect_RetDone,writer.WriteStream(stream));stream.close();ASSERT_GT(std::filesystem::file_size(output),1000u);
}

TEST(FiveAxisSplitChainProduct, GeneratedShortPathUsesSupportFrameAndPersistsExactFiveAxisPostCoordinates)
{
    namespace S=split_chain_tests;
    for(const auto cradle:{false,true}){
        SCOPED_TRACE(cradle?"3+2 cradle":"4+1 head/table");const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project);
        auto cut=ConfirmWithPolicies(scene,0.).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{cut});
        auto splitAnchor=Invoke(&scene,"CamCut","ResolveSplitPoint",{{"expected",Read(scene).at("expected")},{"intentId",cut},{"nearPoint",Array{4*std::cos(1.),4*std::sin(1.),10.}},{"maximumDistanceMm",.1},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}}).at("anchor").To<Map>();splitAnchor["nativeParameter"]=1.;
        const auto split=WaitCut(scene,Invoke(&scene,"CamCut","Split",SplitRequest(scene,{Map{{"intentId",cut},{"points",Array{splitAnchor}}}})));ASSERT_EQ("Completed",split.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(split);const auto pieces=split.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2u,pieces.size());double shortest=1e100;
        for(const auto& value:Read(scene).at("intentToolpaths").To<Array>()){
            const auto item=value.To<Map>();if(std::find(pieces.begin(),pieces.end(),item.at("id"))==pieces.end())continue;
            const auto curve=IntentCurveValue(scene,item);ASSERT_TRUE(curve);
            double range=0;for(const auto& span:curve->Curve3.Segments)range+=std::abs(span.Range.Last-span.Range.First);if(range<shortest){shortest=range;cut=item.at("id");}
        }
        EXPECT_NEAR(1,shortest,1e-10);Array disabled;for(const auto& piece:pieces)if(piece!=cut)disabled.push_back(piece);auto disable=GenerateRequest(scene);disable["action"]=Text("Disable");disable["intentIds"]=disabled;Invoke(&scene,"CamCut","ManageTree",disable);
        Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,cut,"Fixed",{0.,0.,1.}));
        GenerateAxis(scene);
        auto p=S::Create(scene,cradle);const auto initial=S::Values(p.Geometry);S::Set(scene,p,initial);const auto pose=S::Independent(p.Geometry,initial);
        auto placement=pose.RelativeTool;const auto first=Generated(scene).at("operations").To<Array>().front().To<Map>().at("setupStart").To<Array>();
        for(int r=0;r<3;++r){placement[r*4+3]=pose.PositionMm[r]+5*pose.Direction[r];for(int c=0;c<3;++c)placement[r*4+3]-=placement[r*4+c]*first[c].To<double>();}
        Array placed;for(auto value:placement)placed.emplace_back(value);p.Configuration["placement"]=placed;auto configure=GenerateRequest(scene);configure["configuration"]=p.Configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
        const auto before=Read(scene);auto planRequest=GenerateRequest(scene);planRequest.erase("commandId");const auto report=BuildMachineSimulation(scene,planRequest);const auto output=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../output/tests/split-chain-machines-20261024/native-agent").lexically_normal();
        std::ofstream(output/(cradle?"synthetic-cradle-plan.json":"synthetic-head-table-plan.json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
        ASSERT_EQ("Ready",report.at("status").To<Text>())<<report.at("reason").To<Text>();EXPECT_EQ("Clear",report.at("collisionStatus").To<Text>());EXPECT_FALSE(report.at("machineReady").To<bool>());
        const auto plan=ProductMachinePlan(scene);ASSERT_EQ("Current",plan.at("validity").To<Text>());const auto frames=plan.at("frames").To<Array>(),nodes=plan.at("nodes").To<Array>();ASSERT_GT(frames.size(),3u);
        std::map<Text,std::size_t> indices;for(std::size_t i=0;i<nodes.size();++i)indices.emplace(nodes[i].To<Map>().at("id").To<Text>(),i);
        std::vector<double> low(p.IDs.size(),1e100),high(p.IDs.size(),-1e100);
        for(const auto& value:frames){const auto f=value.To<Map>();const auto joint=f.at("joints").To<Array>();std::vector<double> q;for(const auto& id:p.IDs)q.push_back(joint[indices.at(iCAX::Data::to_string(id))].To<double>());const auto oracle=S::Independent(p.Geometry,q);
            const auto tool=f.at("actualToolPose").To<Array>(),direction=f.at("actualBeamDirection").To<Array>();for(int k=0;k<16;++k)EXPECT_NEAR(oracle.RelativeTool[k],tool[k].To<double>(),1e-8);for(int k=0;k<3;++k)EXPECT_NEAR(oracle.Direction[k],direction[k].To<double>(),1e-9);
            for(std::size_t i=0;i<q.size();++i){low[i]=std::min(low[i],q[i]);high[i]=std::max(high[i],q[i]);}
        }
        for(const auto& [axis,i]:p.Geometry.Axes)if(p.Geometry.Model.Nodes[i].Type==S::K::JointType::Prismatic)EXPECT_GT(high[i]-low[i],.001)<<axis;
        const auto live=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});const auto program=MachineProgram(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",live.at("modelKey")}});const auto instructions=program.at("motion").To<Map>().at("instructions").To<Array>();ASSERT_EQ(frames.size(),instructions.size());
        for(std::size_t i=0;i<frames.size();++i){const auto f=frames[i].To<Map>(),instruction=instructions[i].To<Map>();EXPECT_EQ(f.at("joints"),instruction.at("joints"));const auto values=instruction.at("axes").To<Map>();EXPECT_EQ(5u,values.size());
            for(const auto& [axis,index]:p.Geometry.Axes){auto physical=f.at("joints").To<Array>()[indices.at(iCAX::Data::to_string(p.IDs[index]))].To<double>();if(p.Geometry.Model.Nodes[index].Type!=S::K::JointType::Prismatic)physical*=180/std::numbers::pi;if(axis=="C")physical=17-physical;EXPECT_NEAR(physical,values.at(axis).To<double>(),1e-10);}
        }
        EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
        const auto path=output/(cradle?"synthetic-cradle.i5cam":"synthetic-head-table.i5cam");SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(plan,ProductMachinePlan(reopened));
    }
}

namespace split_chain_tests {
void CheckRealAsset(bool cradle){
    namespace S=split_chain_tests;
    {
        SCOPED_TRACE(cradle?"TC-800":"RC-1000");const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);
        const auto path=(std::filesystem::path(__FILE__).parent_path()/"../../../../../.."/(cradle?"src/apps/five-axis-cam/resources/machines/cradle-three-plus-two/TC-800.urdf":"src/apps/five-axis-cam/resources/machines/rotary-four-plus-one/RC-1000.urdf")).lexically_normal();
        const auto imported=host.Call(scene,"MachineDefinition","Import",{{"sourcePath",path.string()}});const auto instantiated=host.Call(scene,"Machine","Instantiate",{{"machineDefinitionId",imported.at("machineDefinitionId")}});const auto machine=instantiated.at("machine").To<Map>();Text tool,support;Array tcp,beam;std::map<Text,Text> axisIDs;
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            const auto element=e->GetComponent("CMachineElementComponent");if(!element||iCAX::Data::to_string(element->GetProperty("MachineID").To<ID>())!=machine.at("id").To<Text>())continue;
            const auto id=iCAX::Data::to_string(e->GetID());if(element->GetProperty("WorkSupport").To<bool>()){ASSERT_TRUE(support.empty());support=id;}
            if(const auto component=e->GetComponent("CMachineToolComponent")){ASSERT_TRUE(tool.empty());tool=id;tcp=component->GetProperty("TcpLocalPosition").To<Array>();beam=component->GetProperty("BeamLocalDirection").To<Array>();}
            if(const auto joint=e->GetComponent("CMachineJointComponent");joint&&joint->GetProperty("JointType").To<Text>()!="fixed")axisIDs.emplace(joint->GetProperty("JointName").To<Text>(),id);
        }
        ASSERT_FALSE(tool.empty());ASSERT_FALSE(support.empty());ASSERT_EQ(5u,axisIDs.size());EXPECT_TRUE(axisIDs.contains(cradle?"A":"B"));EXPECT_TRUE(axisIDs.contains("C"));
        File stock(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),2.,2.,1.).Shape());const auto placed=Imported(scene,stock);auto stockPlacement=PlacementRequest(scene,placed);stockPlacement["transform"]=Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};Invoke(&scene,"CamPlacement","SetTransform",stockPlacement);
        // Retain a real workpiece one mm above the declared support surface;
        // it follows the moving support throughout the five-axis sweep.
        auto configure=GenerateRequest(scene);configure["configuration"]=Map{{"machineId",machine.at("id")},{"toolId",tool},{"supportId",support},{"placement",Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,cradle?81.:5.,0.,0.,0.,1.}},{"tcp",tcp},{"beam",beam},{"toolCollisionRepresentation",Text("OwnMesh")}};Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
        const auto snapshot=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});EXPECT_EQ("Boxes",snapshot.at("configuration").To<Map>().at("toolCollisionRepresentation").To<Text>());EXPECT_FALSE(snapshot.contains("toolMesh"));EXPECT_EQ("MachineDeclaredCollisionBoxes",snapshot.at("toolCollisionSource").To<Text>());
        const auto toolEntity=scene.Database().GetEntity(*ID::from_string(tool));ASSERT_TRUE(toolEntity);const auto collision=toolEntity->GetComponent("CMachineCollisionComponent");ASSERT_TRUE(collision);const auto declared=collision->GetProperty("Collisions").To<Array>();ASSERT_FALSE(declared.empty());bool foundToolBoxes=false;
        for(const auto& value:snapshot.at("collisionDefinitions").To<Array>()){const auto record=value.To<Map>();if(record.at("id").To<Text>()!=tool)continue;EXPECT_EQ(declared,record.at("geometry").To<Array>());foundToolBoxes=true;}EXPECT_TRUE(foundToolBoxes);
        S::Tree t;std::vector<double> initial;const auto rawNodes=snapshot.at("nodes").To<Array>(),rawPositions=snapshot.at("positions").To<Array>();
        for(std::size_t i=0;i<rawNodes.size();++i){const auto raw=rawNodes[i].To<Map>();S::K::Node n;n.ID=raw.at("id").To<Text>();n.Parent=raw.at("parent").To<int>();const auto type=raw.at("type").To<Text>();n.Type=type=="fixed"?S::K::JointType::Fixed:type=="prismatic"?S::K::JointType::Prismatic:S::K::JointType::Revolute;n.Lower=raw.at("lower").To<double>();n.Upper=raw.at("upper").To<double>();n.ReferenceValue=rawPositions[i].To<double>();
            S::M::Node oracle;oracle.ID=n.ID;oracle.Parent=n.Parent;oracle.Type=type;oracle.Lower=n.Lower;oracle.Upper=n.Upper;const auto origin=raw.at("origin").To<Array>(),axis=raw.at("axis").To<Array>();for(int k=0;k<16;++k)n.Origin[k]=oracle.Origin[k]=origin[k].To<double>();for(int k=0;k<3;++k)n.Axis[k]=oracle.Axis[k]=axis[k].To<double>();t.Model.Nodes.push_back(n);t.Oracle.push_back(oracle);initial.push_back(rawPositions[i].To<double>());
            for(const auto& [name,id]:axisIDs)if(id==n.ID)t.Axes.emplace(name,i);if(n.ID==tool)t.Tool=i;if(n.ID==support)t.Support=i;
        }
        t.Task.ToolFrameID=tool;t.Task.ReferenceFrameID=support;for(int k=0;k<3;++k){t.Task.TCP[k]=tcp[k].To<double>();t.Task.Beam[k]=beam[k].To<double>();}
        auto desired=initial;desired[t.Axes.at("X")]+=30;desired[t.Axes.at("Y")]+=20;desired[t.Axes.at("Z")]-=15;desired[t.Axes.at(cradle?"A":"B")]=.2;desired[t.Axes.at("C")]=.35;
        const auto pose=S::Independent(t,desired);const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineKinematicsService>();ASSERT_TRUE(service);const auto model=service->BuildModel(t.Model);S::CheckIndependent(t,service->Forward(model,desired,t.Task),desired);EXPECT_EQ(5u,service->Jacobian(model,desired,t.Task).Rank);
        S::K::Request r;r.TaskDefinition=t.Task;r.Goal.PositionMm=pose.PositionMm;r.Goal.Direction=pose.Direction;r.Seeds={initial};r.MaxSeeds=4;r.MaxCandidates=2;r.MaxTimeMs=2000;const auto result=service->InverseCandidates(model,r);ASSERT_EQ(S::K::Status::Solved,result.State)<<result.Diagnostic;ASSERT_FALSE(result.Candidates.empty());
        for(const auto& candidate:result.Candidates){const auto actual=S::Independent(t,candidate.Positions);for(int k=0;k<3;++k){EXPECT_NEAR(pose.PositionMm[k],actual.PositionMm[k],.010001);EXPECT_NEAR(pose.Direction[k],actual.Direction[k],.000011);}}
        Map edits;for(const auto& [name,i]:t.Axes)edits[t.Model.Nodes[i].ID]=desired[i];const auto checked=CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",edits}});
        const auto output=(std::filesystem::path(__FILE__).parent_path()/"../../../../../../output/tests/split-chain-machines-20261024/native-agent").lexically_normal();std::ofstream(output/(cradle?"tc800-axis-sweep.json":"rc1000-axis-sweep.json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        EXPECT_EQ("Clear",checked.at("collisionStatus").To<Text>())<<(checked.contains("reason")?checked.at("reason").To<Text>():Text{});EXPECT_EQ("MachineDeclaredBoxesV5",checked.at("collisionAlgorithm").To<Text>());EXPECT_EQ("MachineDeclaredBoxesAndWorkpieceFixtureMesh",checked.at("collisionScope").To<Text>());EXPECT_EQ("MachineBoxes; WorkpieceFixtureOwnMesh",checked.at("collisionRepresentation").To<Text>());
        // Tool visuals are independent of the authored collision declarations.
        // Opening a display mesh must retain the same box model and verdict.
        const auto visuals=toolEntity->GetComponent("CMachineVisualComponent");ASSERT_TRUE(visuals);const auto rawVisuals=visuals->GetProperty("Visuals").To<Array>();ASSERT_FALSE(rawVisuals.empty());const auto url=rawVisuals.front().To<Map>().at("geometryResourceId").To<Text>();
        const auto source=scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(url);ASSERT_TRUE(source);ASSERT_FALSE(source->Mesh.Triangles.empty());auto broken=std::make_shared<iCAX::GeometryData::CTriangleMeshResource>(*source);broken->Mesh.Triangles.pop_back();auto info=*scene.Resources().GetInfo(url);++info.nVersion;scene.Resources().Set(url,broken,info);
        const auto updated=ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});EXPECT_EQ(snapshot.at("modelKey"),updated.at("modelKey"));EXPECT_EQ(snapshot.at("collisionDefinitions"),updated.at("collisionDefinitions"));
        const auto afterDisplayEdit=CheckMachineMove(scene,{{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"positions",edits}});EXPECT_EQ(checked.at("collisionStatus"),afterDisplayEdit.at("collisionStatus"));
    }
}
}
TEST(FiveAxisSplitChainAssets, RC1000DeclaredSplitChainFiveAxisSweepIgnoresDisplayMeshChanges){split_chain_tests::CheckRealAsset(false);}
TEST(FiveAxisSplitChainAssets, TC800DeclaredCradleFiveAxisSweepIgnoresDisplayMeshChanges){split_chain_tests::CheckRealAsset(true);}
