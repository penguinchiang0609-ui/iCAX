// Product regression: authored axis directions and topology stay fixed while
// instance dimensions change. A frozen machine program must then be replanned.
namespace machine_offset_replan_tests {
    namespace S=split_chain_tests;
    void PrepareShortPath(Scene& scene){
        auto cut=ConfirmWithPolicies(scene,0.).at("createdIntentIds").To<Array>().front();
        ZeroGenerationPolicies(scene,{cut});
        const auto pieces=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,cut,0.,1.)})).at("intentIds").To<Array>();
        if(pieces.size()!=2)throw std::runtime_error("Offset regression requires a split source arc");
        double shortest=1e100;
        for(const auto& value:Read(scene).at("intentToolpaths").To<Array>()){
            const auto item=value.To<Map>();if(std::find(pieces.begin(),pieces.end(),item.at("id"))==pieces.end())continue;
            const auto curve=IntentCurveValue(scene,item);
            if(!curve)throw std::runtime_error("Offset regression curve resource missing");
            double range=0;for(const auto& span:curve->Curve3.Segments)range+=std::abs(span.Range.Last-span.Range.First);
            if(range<shortest){shortest=range;cut=item.at("id");}
        }
        EXPECT_NEAR(1,shortest,1e-10);
        Array disabled;for(const auto& piece:pieces)if(piece!=cut)disabled.push_back(piece);
        auto disable=GenerateRequest(scene);disable["action"]=Text("Disable");disable["intentIds"]=disabled;Invoke(&scene,"CamCut","ManageTree",disable);
        Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,cut,"Fixed",{0.,0.,1.}));GenerateAxis(scene);
    }
    Map Snapshot(Scene& scene){return ReadMachineSimulation(scene,{{"setupId",Read(scene).at("activeSetupId")}});}
    Map Plan(Scene& scene){auto request=GenerateRequest(scene);request.erase("commandId");return BuildMachineSimulation(scene,request);}
    Map ExportRequest(Scene& scene,const Map& machine){return {{"expected",Read(scene).at("expected")},{"setupId",Read(scene).at("activeSetupId")},{"modelKey",machine.at("modelKey")}};}
    Text RejectedExport(Scene& scene,const Map& machine){
        try{(void)Invoke(&scene,"CamSimulation","ExportMachineProgram",ExportRequest(scene,machine));}
        catch(const std::exception& error){return error.what();}
        ADD_FAILURE()<<"A stale machine program was exported";return {};
    }
    Map Offset(Scene& scene,AssetPlanHost& host,S::ProductTree& tree,const Text& axis,const S::M::Vector& position){
        const auto index=tree.Geometry.Axes.at(axis);const auto id=tree.IDs[index];const auto entity=scene.Database().GetEntity(id);
        const auto element=entity->GetComponent("CMachineElementComponent"),joint=entity->GetComponent("CMachineJointComponent"),transform=entity->GetComponent("CTransformComponent");
        const auto rest=element->GetProperty("RestPose").To<Array>();
        const auto jointAxis=joint->GetProperty("Axis"),type=joint->GetProperty("JointType"),parent=transform->GetProperty("ParentEntityID"),q=joint->GetProperty("Position");
        const auto history=scene.Database().GetUndoArray().size();
        const auto result=host.Call(scene,"Machine","SetElementPosition",{{"entityId",iCAX::Data::to_string(id)},{"position",Array{position[0],position[1],position[2]}}});
        EXPECT_TRUE(result.at("changed").To<bool>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
        const auto actual=element->GetProperty("RestPose").To<Array>();if(actual.size()!=6)throw std::runtime_error("Edited machine part lost its authored rest pose");
        for(int k=0;k<3;++k){EXPECT_DOUBLE_EQ(position[k],actual[k].To<double>());EXPECT_EQ(rest[k+3],actual[k+3]);tree.Geometry.Oracle[index].Origin[k*4+3]=position[k];tree.Geometry.Model.Nodes[index].Origin[k*4+3]=position[k];}
        EXPECT_EQ(jointAxis,joint->GetProperty("Axis"));EXPECT_EQ(type,joint->GetProperty("JointType"));EXPECT_EQ(parent,transform->GetProperty("ParentEntityID"));EXPECT_EQ(q,joint->GetProperty("Position"));
        const auto snapshot=Snapshot(scene);bool found=false;
        for(const auto& raw:snapshot.at("nodes").To<Array>())if(const auto node=raw.To<Map>();node.at("id").To<Text>()==iCAX::Data::to_string(id)){
            found=true;const auto origin=node.at("origin").To<Array>();for(int k=0;k<3;++k)EXPECT_DOUBLE_EQ(position[k],origin[k*4+3].To<double>());
        }
        EXPECT_TRUE(found);return snapshot;
    }
    void VerifyOutput(Scene& scene,const S::ProductTree& tree,const Map& plan,const Map& snapshot,const Map& program){
        const auto frames=plan.at("frames").To<Array>(),nodes=plan.at("nodes").To<Array>(),instructions=program.at("motion").To<Map>().at("instructions").To<Array>();
        ASSERT_GT(frames.size(),3u);ASSERT_EQ(frames.size(),instructions.size());
        std::map<Text,std::size_t> indices;for(std::size_t i=0;i<nodes.size();++i)indices.emplace(nodes[i].To<Map>().at("id").To<Text>(),i);
        EXPECT_EQ(snapshot.at("modelKey"),program.at("modelKey"));
        for(std::size_t f=0;f<frames.size();++f){
            const auto frame=frames[f].To<Map>(),instruction=instructions[f].To<Map>();const auto raw=frame.at("joints").To<Array>();std::vector<double> q;
            for(const auto& id:tree.IDs)q.push_back(raw[indices.at(iCAX::Data::to_string(id))].To<double>());
            const auto oracle=S::Independent(tree.Geometry,q);const auto actual=frame.at("actualToolPose").To<Array>(),direction=frame.at("actualBeamDirection").To<Array>();
            S::M::Matrix actualMatrix{};for(int k=0;k<16;++k){actualMatrix[k]=actual[k].To<double>();EXPECT_NEAR(oracle.RelativeTool[k],actualMatrix[k],1e-8);}
            const auto tcp=S::M::Transform(actualMatrix,tree.Geometry.Task.TCP);
            for(int k=0;k<3;++k){EXPECT_NEAR(oracle.PositionMm[k],tcp[k],1e-8);EXPECT_NEAR(oracle.Direction[k],direction[k].To<double>(),1e-9);}
            EXPECT_EQ(raw,instruction.at("joints").To<Array>());const auto axes=instruction.at("axes").To<Map>();EXPECT_EQ(5u,axes.size());
            for(const auto& [word,index]:tree.Geometry.Axes){double expected=q[index];if(tree.Geometry.Model.Nodes[index].Type!=S::K::JointType::Prismatic)expected*=180/std::numbers::pi;if(word=="C")expected=17-expected;EXPECT_NEAR(expected,axes.at(word).To<double>(),1e-10);}
        }
    }
    void Check(bool cradle){
        SCOPED_TRACE(cradle?"3+2 A/C cradle offsets":"4+1 B/C head/table offsets");
        const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);AssetPlanHost host(project);PrepareShortPath(scene);
        auto tree=S::Create(scene,cradle);const auto initial=S::Values(tree.Geometry);S::Set(scene,tree,initial);const auto pose=S::Independent(tree.Geometry,initial);
        auto placement=pose.RelativeTool;const auto first=Generated(scene).at("operations").To<Array>().front().To<Map>().at("setupStart").To<Array>();
        for(int r=0;r<3;++r){placement[r*4+3]=pose.PositionMm[r]+5*pose.Direction[r];for(int c=0;c<3;++c)placement[r*4+3]-=placement[r*4+c]*first[c].To<double>();}
        Array placed;for(auto v:placement)placed.emplace_back(v);tree.Configuration["placement"]=placed;auto configure=GenerateRequest(scene);configure["configuration"]=tree.Configuration;Invoke(&scene,"CamSimulation","ConfigureMachine",configure);
        const auto baselineReport=Plan(scene);ASSERT_EQ("Ready",baselineReport.at("status").To<Text>())<<baselineReport.at("reason").To<Text>();
        const auto baselinePlan=ProductMachinePlan(scene),baselineModel=Snapshot(scene);ASSERT_EQ("Current",baselinePlan.at("validity").To<Text>());
        const auto baselineProgram=MachineProgram(scene,ExportRequest(scene,baselineModel));const auto originalGeometry=tree.Geometry;
        const auto firstChange=Offset(scene,host,tree,"C",{4.,-3.,2.});
        EXPECT_NE(baselineModel.at("modelKey"),firstChange.at("modelKey"));EXPECT_EQ("Stale",ProductMachinePlan(scene).at("validity").To<Text>());
        // Undo is semantic: the exact original frozen plan becomes current
        // again, rather than remaining invalid because of an edit counter.
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(baselineModel.at("modelKey"),Snapshot(scene).at("modelKey"));EXPECT_EQ(baselinePlan,ProductMachinePlan(scene));
        ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(firstChange.at("modelKey"),Snapshot(scene).at("modelKey"));EXPECT_EQ("Stale",ProductMachinePlan(scene).at("validity").To<Text>());
        const auto changed=Offset(scene,host,tree,cradle?"A":"B",{2.5,1.5,-1.});
        EXPECT_NE(firstChange.at("modelKey"),changed.at("modelKey"));const auto stale=ProductMachinePlan(scene);EXPECT_EQ("Stale",stale.at("validity").To<Text>());EXPECT_FALSE(stale.at("playbackAllowed").To<bool>());EXPECT_FALSE(stale.contains("frames"));EXPECT_EQ(baselinePlan.at("resource"),stale.at("resource"));
        EXPECT_NE(Text::npos,RejectedExport(scene,baselineModel).find("机床或后置已变化"));
        EXPECT_NE(Text::npos,RejectedExport(scene,changed).find("机床运动计划已过期"));
        const auto replannedReport=Plan(scene);ASSERT_EQ("Ready",replannedReport.at("status").To<Text>())<<replannedReport.at("reason").To<Text>();EXPECT_EQ("Clear",replannedReport.at("collisionStatus").To<Text>());
        const auto replanned=ProductMachinePlan(scene),live=Snapshot(scene);ASSERT_EQ("Current",replanned.at("validity").To<Text>());EXPECT_NE(baselinePlan.at("resource"),replanned.at("resource"));
        const auto program=MachineProgram(scene,ExportRequest(scene,live));VerifyOutput(scene,tree,replanned,live,program);
        EXPECT_NE(baselineProgram.at("motion").To<Map>().at("instructions"),program.at("motion").To<Map>().at("instructions"));
        // Counterfactual FK on the unadjusted machine must disagree, proving
        // the new output did not simply relabel the previously planned axes.
        const auto nodes=replanned.at("nodes").To<Array>(),frames=replanned.at("frames").To<Array>();std::map<Text,std::size_t> indices;for(std::size_t i=0;i<nodes.size();++i)indices.emplace(nodes[i].To<Map>().at("id").To<Text>(),i);
        double discrepancy=0;for(const auto& raw:frames){const auto frame=raw.To<Map>();const auto joint=frame.at("joints").To<Array>();std::vector<double> q;for(const auto& id:tree.IDs)q.push_back(joint[indices.at(iCAX::Data::to_string(id))].To<double>());const auto oldFK=S::Independent(originalGeometry,q),newFK=S::Independent(tree.Geometry,q);discrepancy=std::max(discrepancy,std::hypot(oldFK.PositionMm[0]-newFK.PositionMm[0],oldFK.PositionMm[1]-newFK.PositionMm[1],oldFK.PositionMm[2]-newFK.PositionMm[2]));}EXPECT_GT(discrepancy,1.);
        const auto out=std::filesystem::path("C:/icax-machine-kinematics/machine-offset-replan-product/evidence");std::filesystem::create_directories(out);
        const Text label=cradle?"cradle-ac":"head-table-bc";
        std::ofstream(out/(label+"-program.json"))<<program.at("content").To<Text>();
        std::ofstream(out/(label+"-verification.json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{"layout",label},{"staleExportRejected",true},{"latestModelKey",live.at("modelKey")},{"frames",std::to_string(frames.size())},{"oldGeometryCounterfactualErrorMm",discrepancy}});
        const auto path=out/(label+".i5cam");SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(project,project,false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(replanned,ProductMachinePlan(reopened));
    }
}
TEST(FiveAxisMachineOffsetReplan, HeadTableOffsetsInvalidateOldProgramAndRecomputeAllFiveOutputAxes){machine_offset_replan_tests::Check(false);}
TEST(FiveAxisMachineOffsetReplan, CradleOffsetsInvalidateOldProgramAndRecomputeAllFiveOutputAxes){machine_offset_replan_tests::Check(true);}
