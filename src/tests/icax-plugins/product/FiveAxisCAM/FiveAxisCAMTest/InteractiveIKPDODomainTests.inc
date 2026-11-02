namespace {
    Array PDONumbers(const auto& values){Array result;for(const auto x:values)result.emplace_back(x);return result;}
    UInt PDOIDOf(const Map& begin,const char* key){return std::stoull(begin.at(key).To<Map>().at("id").To<Text>());}
    Text PDOEditToken(){return iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());}
    Map PDOBegin(Scene& scene,const ProductPlanMachine& machine,const Text& token){return Invoke(&scene,"CamSimulation","BeginMachinePoseEdit",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(machine.Tool)},{"editToken",token}});}
    Map PDOEnd(Scene& scene,const Text& token,UInt final=0,bool cancel=false){return Invoke(&scene,"CamSimulation","EndMachinePoseEdit",{{"editToken",token},{"finalSequence",std::to_string(final)},{"cancel",cancel}});}
    void PDOWrite(Scene& scene,const Map& begin,UInt sequence,double x,double y=0,double z=30,const Text& tokenOverride={},const Text& epochOverride={}){
        auto target=iCAX::CAM::Motion::Identity();target[3]=x;target[7]=y;target[11]=z;std::vector<double> values(target.begin(),target.end());
        flatbuffers::FlatBufferBuilder b;const auto token=tokenOverride.empty()?begin.at("editToken").To<Text>():tokenOverride,epoch=epochOverride.empty()?begin.at("epoch").To<Text>():epochOverride;
        const auto record=CreateMachineIKTargetDirect(b,1,token.c_str(),epoch.c_str(),sequence,&values);FinishMachineIKTargetBuffer(b,record);
        auto lease=iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(scene.PDOHub().GetSlot(PDOIDOf(begin,"targetPDO")),sequence);if(!lease)throw std::runtime_error("Target PDO write unexpectedly busy");std::memcpy(lease->Data(),b.GetBufferPointer(),b.GetSize());lease->Commit(b.GetSize());
    }
    Map PDORead(Scene& scene,const Map& begin){
        iCAX::PDO::CPDOReadLease lease(scene.PDOHub().GetSlot(PDOIDOf(begin,"resultPDO")));flatbuffers::Verifier verifier(static_cast<const std::uint8_t*>(lease.Data()),lease.PayloadSize());if(!VerifyMachineIKResultBuffer(verifier))throw std::runtime_error("Result PDO invalid");const auto r=GetMachineIKResult(lease.Data());
        const auto ref=[](const MachineCollisionCollider* value){return Map{{"ownerElementId",value->owner_element_id()->str()},{"ownerElementName",value->owner_element_name()->str()},{"boxId",value->box_id()?value->box_id()->str():Text{}},{"boxName",value->box_name()->str()},{"boxIndex",value->box_index()},{"representation",value->representation()->str()},{"editable",value->editable()}};};
        Array pairs;if(r->collision_pairs())for(const auto* pair:*r->collision_pairs())pairs.emplace_back(Map{{"first",ref(pair->first())},{"second",ref(pair->second())}});
        return {{"status",r->status()->str()},{"collisionStatus",r->collision_status()?r->collision_status()->str():Text{}},{"reason",r->reason()?r->reason()->str():Text{}},{"sequence",std::to_string(r->result_sequence())},{"processed",std::to_string(r->processed_sequence())},{"applied",std::to_string(r->applied_sequence())},{"mutationSequence",std::to_string(r->mutation_sequence())},{"positions",PDONumbers(*r->positions())},{"tip",PDONumbers(*r->tip_world())},{"world",PDONumbers(*r->world_matrices())},{"collisionPairs",pairs},{"collisionDiagnosticsComplete",r->collision_diagnostics_complete()},{"cacheReused",r->cache_reused()},{"capture",r->capture_ms()},{"prepare",r->prepare_ms()},{"solve",r->solve_ms()},{"total",r->total_ms()}};
    }
    Map PDOTick(Scene& scene,const Map& begin){scene.PDOHub().SwapInSlot();PumpMachinePoseEdit(scene);scene.PDOHub().SwapOutSlot();return PDORead(scene,begin);}
    double PDOJoint(Scene& scene,ID id){return scene.Database().GetEntity(id)->GetComponent("CMachineJointComponent")->GetProperty("Position").To<double>();}
    void PDOReady(const Map& result,UInt sequence){ASSERT_EQ("Ready",result.at("status").To<Text>())<<result.at("reason").To<Text>();ASSERT_EQ("Clear",result.at("collisionStatus").To<Text>());EXPECT_EQ(std::to_string(sequence),result.at("processed").To<Text>());EXPECT_EQ(std::to_string(sequence),result.at("applied").To<Text>());}
}
TEST(FiveAxisInteractiveIKPDO, LatestTargetPerTickFinalAckSingleHistoryAndArchivePersistence)
{
    const auto id=iCAX::Data::GenerateNewUUID();Scene scene(id,id,true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();const auto token=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();
    EXPECT_EQ("Idle",PDORead(scene,begin).at("status").To<Text>());EXPECT_EQ(begin,PDOBegin(scene,machine,token));
    PDOWrite(scene,begin,1,1);PDOWrite(scene,begin,2,2);PDOWrite(scene,begin,3,3);
    const auto pending=PDOEnd(scene,token,3);EXPECT_TRUE(pending.at("pending").To<bool>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto latest=PDOTick(scene,begin);PDOReady(latest,3);EXPECT_NEAR(3,PDOJoint(scene,machine.X),.001);const auto seq=latest.at("sequence");EXPECT_EQ(seq,PDOTick(scene,begin).at("sequence"));
    PDOWrite(scene,begin,4,4);const auto next=PDOTick(scene,begin);PDOReady(next,4);EXPECT_TRUE(next.at("cacheReused").To<bool>());EXPECT_DOUBLE_EQ(0,next.at("capture").To<double>());EXPECT_DOUBLE_EQ(0,next.at("prepare").To<double>());
    const auto ended=PDOEnd(scene,token,4);EXPECT_FALSE(ended.at("pending").To<bool>());EXPECT_TRUE(ended.at("changed").To<bool>());EXPECT_EQ(ended,PDOEnd(scene,token,4));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(begin,"targetPDO")));EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(begin,"resultPDO")));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_NEAR(0,PDOJoint(scene,machine.X),1e-7);ASSERT_TRUE(scene.Database().Redo());EXPECT_NEAR(4,PDOJoint(scene,machine.X),.001);
    const auto path=std::filesystem::path("C:/icax-machine-kinematics/ik-pdo-native/committed.i5cam");SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(id,id,false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_DOUBLE_EQ(PDOJoint(scene,machine.X),PDOJoint(reopened,machine.X));
}
TEST(FiveAxisInteractiveIKPDO, RejectedTargetCanRecoverAndCancelRestoresExactJointStateWithoutHistory)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();const auto token=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();
    PDOWrite(scene,begin,1,10,0,-10);const auto blocked=PDOTick(scene,begin);EXPECT_EQ("Blocked",blocked.at("status").To<Text>())<<blocked.at("reason").To<Text>();EXPECT_EQ("1",blocked.at("processed").To<Text>());EXPECT_EQ("0",blocked.at("applied").To<Text>());EXPECT_NEAR(30,PDOJoint(scene,machine.Tool),1e-7);EXPECT_EQ(16u,blocked.at("tip").To<Array>().size());
    PDOWrite(scene,begin,2,2);PDOReady(PDOTick(scene,begin),2);EXPECT_NEAR(2,PDOJoint(scene,machine.X),.001);const auto cancelled=PDOEnd(scene,token,2,true);EXPECT_TRUE(cancelled.at("cancelled").To<bool>());EXPECT_NEAR(0,PDOJoint(scene,machine.X),1e-7);EXPECT_NEAR(30,PDOJoint(scene,machine.Tool),1e-7);EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto next=PDOBegin(scene,machine,PDOEditToken());EXPECT_NE(PDOIDOf(begin,"targetPDO"),PDOIDOf(next,"targetPDO"));PDOWrite(scene,next,1,1);PDOReady(PDOTick(scene,next),1);PDOEnd(scene,next.at("editToken").To<Text>(),1,true);
}
TEST(FiveAxisInteractiveIKPDO, ForeignCancelCannotStopActiveSessionAndLateBeginCannotResurrectRetiredToken)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto token=PDOEditToken(),foreign=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();const auto cancel=PDOEnd(scene,foreign,0,true);EXPECT_TRUE(cancel.at("cancelled").To<bool>());EXPECT_TRUE(scene.PDOHub().HasSlot(PDOIDOf(begin,"targetPDO")));EXPECT_THROW(PDOBegin(scene,machine,foreign),std::exception);
    PDOWrite(scene,begin,1,1);PDOReady(PDOTick(scene,begin),1);EXPECT_TRUE(PDOEnd(scene,token,1,true).at("cancelled").To<bool>());EXPECT_EQ(cancel,PDOEnd(scene,foreign,0,true));const auto early=PDOEditToken();PDOEnd(scene,early,0,true);EXPECT_THROW(PDOBegin(scene,machine,early),std::exception);
}
TEST(FiveAxisInteractiveIKPDO, StaleIdentityInvalidatesWithoutMutationAndLeasedSlotsRetireOnLaterTick)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();const auto token=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();PDOWrite(scene,begin,1,1,0,30,{},PDOEditToken());const auto invalid=PDOTick(scene,begin);EXPECT_EQ("Error",invalid.at("status").To<Text>());EXPECT_TRUE(invalid.at("positions").To<Array>().empty());EXPECT_TRUE(invalid.at("world").To<Array>().empty());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));
    iCAX::PDO::CPDOReadLease held(scene.PDOHub().GetSlot(PDOIDOf(begin,"resultPDO")));PDOEnd(scene,token,1,true);EXPECT_TRUE(scene.PDOHub().HasSlot(PDOIDOf(begin,"resultPDO")));EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(begin,"targetPDO")));held.Release();PumpMachinePoseEdit(scene);EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(begin,"resultPDO")));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisInteractiveIKPDO, ExternalConfigurationInvalidatesAndSceneReleaseCancelsAndFreesAllSlots)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();const auto token=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();PDOWrite(scene,begin,1,1);PDOReady(PDOTick(scene,begin),1);
    auto& tx=scene.Database().BeginTransaction("External configuration invalidation");tx.ModifyComponent(machine.X,"CMachineJointComponent",{{"VelocityLimit",90.}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));const auto invalid=PDOTick(scene,begin);EXPECT_EQ("Invalidated",invalid.at("status").To<Text>());EXPECT_TRUE(invalid.at("tip").To<Array>().empty());EXPECT_THROW(PDOEnd(scene,token,1),std::exception);PDOEnd(scene,token,1,true);EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(begin,"resultPDO")));EXPECT_NEAR(0,PDOJoint(scene,machine.X),1e-7);EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto next=PDOBegin(scene,machine,PDOEditToken());scene.PDOHub().SwapOutSlot();PDOWrite(scene,next,1,1);PDOReady(PDOTick(scene,next),1);const auto component=std::dynamic_pointer_cast<CFiveAxisSceneComponent>(scene.Database().GetEntity(scene.GetSceneID())->GetComponent(CFiveAxisSceneComponent::S_ClassName));ASSERT_TRUE(component);ReleaseMachinePoseEdit(scene,component->Runtime);EXPECT_NEAR(0,PDOJoint(scene,machine.X),1e-7);EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(next,"targetPDO")));EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(next,"resultPDO")));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisInteractiveIKPDO, NativeResultMatricesIncludeActualSupportBranchAndFollowingWorkpiece)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);auto& tx=scene.Database().BeginTransaction("Moving support branch");tx.ModifyComponent(machine.Support,"CTransformComponent",{{"ParentEntityID",machine.X}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));const auto token=PDOEditToken();const auto begin=PDOBegin(scene,machine,token);scene.PDOHub().SwapOutSlot();
    const auto ids=begin.at("worldIds").To<Array>(),initial=begin.at("worldMatrices").To<Array>();ASSERT_EQ(ids,begin.at("worldTransformIds").To<Array>());const auto joints=begin.at("jointIds").To<Array>();ASSERT_GT(ids.size(),joints.size());PDOWrite(scene,begin,1,2);const auto result=PDOTick(scene,begin);PDOReady(result,1);const auto matrices=result.at("world").To<Array>(),tip=result.at("tip").To<Array>();ASSERT_EQ(ids.size()*16,matrices.size());EXPECT_NEAR(2,tip[3].To<double>(),.001);EXPECT_DOUBLE_EQ(PDOJoint(scene,machine.X),tip[3].To<double>());EXPECT_NEAR(30,tip[11].To<double>(),1e-7);
    for(std::size_t i=0;i<ids.size();++i){if(ids[i].To<Text>()==iCAX::Data::to_string(machine.Support)||i>=joints.size())EXPECT_NEAR(initial[i*16+3].To<double>()+PDOJoint(scene,machine.X),matrices[i*16+3].To<double>(),1e-12);for(int k=0;k<16;++k)EXPECT_TRUE(std::isfinite(matrices[i*16+k].To<double>()));}PDOEnd(scene,token,1,true);
}
TEST(FiveAxisInteractiveIKPDO, EmptyAndUnchangedGesturesHaveNoHistoryAndMalformedMatrixCannotMutate)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto empty=PDOBegin(scene,machine,PDOEditToken());const auto ended=PDOEnd(scene,empty.at("editToken").To<Text>());EXPECT_FALSE(ended.at("changed").To<bool>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(empty,"resultPDO")));
    const auto unchanged=PDOBegin(scene,machine,PDOEditToken());scene.PDOHub().SwapOutSlot();PDOWrite(scene,unchanged,1,0);PDOReady(PDOTick(scene,unchanged),1);EXPECT_FALSE(PDOEnd(scene,unchanged.at("editToken").To<Text>(),1).at("changed").To<bool>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto invalid=PDOBegin(scene,machine,PDOEditToken());scene.PDOHub().SwapOutSlot();PDOWrite(scene,invalid,1,std::numeric_limits<double>::quiet_NaN());const auto error=PDOTick(scene,invalid);EXPECT_EQ("Error",error.at("status").To<Text>());EXPECT_TRUE(error.at("world").To<Array>().empty());PDOEnd(scene,invalid.at("editToken").To<Text>(),1,true);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
namespace {
    Map NodeBegin(Scene& scene,const ProductPlanMachine& machine,ID node,const Text& mode){return Invoke(&scene,"CamSimulation","BeginMachinePoseEdit",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(node)},{"editToken",PDOEditToken()},{"motionMode",mode}});}
    void PDOWriteJoint(Scene& scene,const Map& begin,UInt sequence,double position){
        flatbuffers::FlatBufferBuilder b;const std::vector<double> empty;const auto token=begin.at("editToken").To<Text>(),epoch=begin.at("epoch").To<Text>();
        const auto record=CreateMachineIKTargetDirect(b,1,token.c_str(),epoch.c_str(),sequence,&empty,position);FinishMachineIKTargetBuffer(b,record);
        auto lease=iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(scene.PDOHub().GetSlot(PDOIDOf(begin,"targetPDO")),sequence);ASSERT_TRUE(lease);std::memcpy(lease->Data(),b.GetBufferPointer(),b.GetSize());lease->Commit(b.GetSize());
    }
}
TEST(FiveAxisInteractiveNode, FKChangesOnlySelectedJointAndMovesDescendantsWithOneUndo)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto begin=NodeBegin(scene,machine,machine.Y,"FK");scene.PDOHub().SwapOutSlot();
    PDOWriteJoint(scene,begin,1,3);PDOReady(PDOTick(scene,begin),1);PDOWriteJoint(scene,begin,2,5);const auto r=PDOTick(scene,begin);PDOReady(r,2);
    EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(5,PDOJoint(scene,machine.Y));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));
    const auto ids=begin.at("worldIds").To<Array>(),world=r.at("world").To<Array>();for(size_t i=0;i<ids.size();++i)if(ids[i].To<Text>()==iCAX::Data::to_string(machine.Tool))EXPECT_DOUBLE_EQ(5,world[i*16+7].To<double>());
    PDOWriteJoint(scene,begin,3,201);EXPECT_EQ("Unreachable",PDOTick(scene,begin).at("status").To<Text>());EXPECT_DOUBLE_EQ(5,PDOJoint(scene,machine.Y));
    EXPECT_TRUE(PDOEnd(scene,begin.at("editToken").To<Text>(),3).at("changed").To<bool>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.Y));ASSERT_TRUE(scene.Database().Redo());EXPECT_DOUBLE_EQ(5,PDOJoint(scene,machine.Y));
}
TEST(FiveAxisInteractiveNode, IntermediateNodeIKUsesItsAncestorsAndPreservesDownstreamJointCoordinates)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    const auto begin=NodeBegin(scene,machine,machine.Y,"IK");scene.PDOHub().SwapOutSlot();PDOWrite(scene,begin,1,3,4,0);PDOReady(PDOTick(scene,begin),1);
    EXPECT_NEAR(3,PDOJoint(scene,machine.X),.001);EXPECT_NEAR(4,PDOJoint(scene,machine.Y),.001);EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));
    PDOEnd(scene,begin.at("editToken").To<Text>(),1,true);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.Y));
    EXPECT_THROW(NodeBegin(scene,machine,machine.Machine,"IK"),std::exception);
    EXPECT_THROW(NodeBegin(scene,machine,machine.Support,"FK"),std::exception);
}
TEST(FiveAxisInteractiveNode, FKCollisionAndCancellationHoldAuthoritativePoseAndRejectModeMismatch)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    Invoke(&scene,"CamSimulation","MoveMachine",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"positions",Map{{iCAX::Data::to_string(machine.X),10.}}}});
    const auto history=scene.Database().GetUndoArray().size();
    const auto begin=NodeBegin(scene,machine,machine.Tool,"FK");scene.PDOHub().SwapOutSlot();PDOWriteJoint(scene,begin,1,-10);EXPECT_EQ("Blocked",PDOTick(scene,begin).at("status").To<Text>());EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));
    PDOWriteJoint(scene,begin,2,31);PDOReady(PDOTick(scene,begin),2);PDOEnd(scene,begin.at("editToken").To<Text>(),2,true);EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto wrong=NodeBegin(scene,machine,machine.Y,"FK");scene.PDOHub().SwapOutSlot();PDOWrite(scene,wrong,1,1);EXPECT_EQ("Error",PDOTick(scene,wrong).at("status").To<Text>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.Y));PDOEnd(scene,wrong.at("editToken").To<Text>(),1,true);
}
namespace {
    Map NumericMachineIK(Scene& scene,const ProductPlanMachine& machine,double x,double z=30,double yaw=0){
        auto m=iCAX::CAM::Motion::Identity();m[0]=m[5]=std::cos(yaw);m[1]=-std::sin(yaw);m[4]=std::sin(yaw);m[3]=x;m[11]=z;
        return Invoke(&scene,"CamSimulation","MoveMachineIK",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(machine.Tool)},{"target",PDONumbers(m)}});
    }
}
TEST(FiveAxisInteractiveIKNumeric, ReliableNumericEditsAreIndependentTransactionsAndRejectFullPoseAndCollision)
{
    // Numeric commands deliberately remain a reliable SDO; pointer drags have
    // a separate PDO transport and never fall back to this route.
    Scene scene;PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto first=NumericMachineIK(scene,machine,1);ASSERT_EQ("Ready",first.at("status").To<Text>())<<first.at("reason").To<Text>();ASSERT_EQ("Clear",first.at("collisionStatus").To<Text>());const auto q1=PDOJoint(scene,machine.X);EXPECT_NEAR(1,q1,.001);EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto second=NumericMachineIK(scene,machine,2);ASSERT_EQ("Ready",second.at("status").To<Text>());const auto q2=PDOJoint(scene,machine.X);EXPECT_NEAR(2,q2,.001);EXPECT_EQ(history+2,scene.Database().GetUndoArray().size());
    const auto unreachable=NumericMachineIK(scene,machine,2,30,.1);EXPECT_EQ("Unreachable",unreachable.at("status").To<Text>());EXPECT_DOUBLE_EQ(q2,PDOJoint(scene,machine.X));EXPECT_EQ(history+2,scene.Database().GetUndoArray().size());
    const auto blocked=NumericMachineIK(scene,machine,10,-10);EXPECT_EQ("Blocked",blocked.at("status").To<Text>())<<blocked.at("reason").To<Text>();EXPECT_DOUBLE_EQ(q2,PDOJoint(scene,machine.X));EXPECT_EQ(history+2,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(q1,PDOJoint(scene,machine.X));ASSERT_TRUE(scene.Database().Redo());EXPECT_DOUBLE_EQ(q2,PDOJoint(scene,machine.X));
}
namespace interactive_closest_tests {
    Map Target(Scene& scene,const ProductPlanMachine& machine,double x,double y=0,double z=30,double yaw=0,ID tip={}){
        auto pose=iCAX::CAM::Motion::Identity();pose[0]=pose[5]=std::cos(yaw);pose[1]=-std::sin(yaw);pose[4]=std::sin(yaw);pose[3]=x;pose[7]=y;pose[11]=z;
        return {{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(tip.is_nil()?machine.Tool:tip)},{"target",PDONumbers(pose)},{"targetPolicy",Text("Closest")}};
    }
    Map Move(Scene& scene,const ProductPlanMachine& machine,double x,double y=0,double z=30,double yaw=0,ID tip={}){return Invoke(&scene,"CamSimulation","MoveMachineIK",Target(scene,machine,x,y,z,yaw,tip));}
    void Approximate(const Map& result){ASSERT_EQ("Approximated",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);ASSERT_EQ("Clear",result.at("collisionStatus").To<Text>());ASSERT_EQ(16u,result.at("tipWorld").To<Array>().size());}
    Map Begin(Scene& scene,const ProductPlanMachine& machine,ID tip={}){return Invoke(&scene,"CamSimulation","BeginMachinePoseEdit",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(tip.is_nil()?machine.Tool:tip)},{"editToken",PDOEditToken()},{"targetPolicy",Text("Closest")}});}
}
TEST(FiveAxisInteractiveClosest, NumericLimitAndUnavailableOrientationUseActualPoseAndNativeHistory)
{
    namespace T=interactive_closest_tests;const auto id=iCAX::Data::GenerateNewUUID();Scene scene(id,id,true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto strict=NumericMachineIK(scene,machine,600);EXPECT_EQ("Unreachable",strict.at("status").To<Text>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto limited=T::Move(scene,machine,600);T::Approximate(limited);EXPECT_NEAR(200,PDOJoint(scene,machine.X),.001);EXPECT_NEAR(200,limited.at("tipWorld").To<Array>()[3].To<double>(),.001);EXPECT_NEAR(400,limited.at("positionErrorMm").To<double>(),.001);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.Y));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto repeated=T::Move(scene,machine,600);T::Approximate(repeated);EXPECT_TRUE(repeated.at("positions").To<Map>().empty());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));ASSERT_TRUE(scene.Database().Redo());EXPECT_NEAR(200,PDOJoint(scene,machine.X),.001);
    const auto orient=T::Move(scene,machine,25,0,30,.1);T::Approximate(orient);EXPECT_NEAR(25,PDOJoint(scene,machine.X),.001);EXPECT_NEAR(0,orient.at("positionErrorMm").To<double>(),.001);EXPECT_NEAR(.1,orient.at("rotationErrorRadians").To<double>(),1e-7);EXPECT_EQ(history+2,scene.Database().GetUndoArray().size());
    const auto path=std::filesystem::path("C:/icax-machine-kinematics/closest-ik-native/committed.i5cam");SaveProductPlanFixture(scene,path);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});Scene reopened(id,id,false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_DOUBLE_EQ(PDOJoint(scene,machine.X),PDOJoint(reopened,machine.X));EXPECT_DOUBLE_EQ(30,PDOJoint(reopened,machine.Tool));
}
TEST(FiveAxisInteractiveClosest, PDOLimitAcknowledgesApproximationAndCancelRestoresWholeGesture)
{
    namespace T=interactive_closest_tests;Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();const auto begin=T::Begin(scene,machine);scene.PDOHub().SwapOutSlot();
    EXPECT_EQ("Closest",begin.at("targetPolicy").To<Text>());PDOWrite(scene,begin,1,600);const auto limit=PDOTick(scene,begin);EXPECT_EQ("Approximated",limit.at("status").To<Text>())<<limit.at("reason").To<Text>();EXPECT_EQ("Clear",limit.at("collisionStatus").To<Text>());EXPECT_EQ("1",limit.at("processed").To<Text>());EXPECT_EQ("1",limit.at("applied").To<Text>());EXPECT_NEAR(200,limit.at("tip").To<Array>()[3].To<double>(),.001);EXPECT_NEAR(200,PDOJoint(scene,machine.X),.001);
    PDOWrite(scene,begin,2,30);PDOReady(PDOTick(scene,begin),2);EXPECT_NEAR(30,PDOJoint(scene,machine.X),.001);EXPECT_TRUE(PDOEnd(scene,begin.at("editToken").To<Text>(),2,true).at("cancelled").To<bool>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto committed=T::Begin(scene,machine);scene.PDOHub().SwapOutSlot();PDOWrite(scene,committed,1,600);EXPECT_EQ("Approximated",PDOTick(scene,committed).at("status").To<Text>());EXPECT_TRUE(PDOEnd(scene,committed.at("editToken").To<Text>(),1).at("changed").To<bool>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));ASSERT_TRUE(scene.Database().Redo());EXPECT_NEAR(200,PDOJoint(scene,machine.X),.001);
    const auto strict=PDOBegin(scene,machine,PDOEditToken());scene.PDOHub().SwapOutSlot();PDOWrite(scene,strict,1,600);EXPECT_EQ("Unreachable",PDOTick(scene,strict).at("status").To<Text>());PDOEnd(scene,strict.at("editToken").To<Text>(),1,true);
}
TEST(FiveAxisInteractiveClosest, SparseAndDenseTargetsReachSameLimitWithoutMovingDownstreamAxes)
{
    namespace T=interactive_closest_tests;
    const auto final=[&](int samples){Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto begin=T::Begin(scene,machine,machine.Y);scene.PDOHub().SwapOutSlot();Map report;
        for(int i=1;i<=samples;++i){PDOWrite(scene,begin,i,600.*i/samples,0,0);report=PDOTick(scene,begin);EXPECT_TRUE(report.at("status").To<Text>()=="Ready"||report.at("status").To<Text>()=="Approximated")<<report.at("reason").To<Text>();EXPECT_EQ("Clear",report.at("collisionStatus").To<Text>());}
        EXPECT_EQ("Approximated",report.at("status").To<Text>());EXPECT_NEAR(200,PDOJoint(scene,machine.X),.001);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.Y));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));const auto x=PDOJoint(scene,machine.X);PDOEnd(scene,begin.at("editToken").To<Text>(),samples,true);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));return x;};
    EXPECT_NEAR(final(1),final(20),.001);
}
TEST(FiveAxisInteractiveClosest, CollisionUsesCertifiedPrefixAndCancelledOrExpiredProofNeverCommits)
{
    namespace T=interactive_closest_tests;Scene scene;PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto blocked=T::Move(scene,machine,10,0,-10);T::Approximate(blocked);ASSERT_TRUE(blocked.contains("acceptedFraction"));EXPECT_GT(blocked.at("acceptedFraction").To<double>(),0);EXPECT_LT(blocked.at("acceptedFraction").To<double>(),1);const auto x=PDOJoint(scene,machine.X),z=PDOJoint(scene,machine.Tool);EXPECT_GT(x,0);EXPECT_LT(x,10);EXPECT_GT(z,-10);EXPECT_LT(z,30);const auto tip=blocked.at("tipWorld").To<Array>();EXPECT_DOUBLE_EQ(x,tip[3].To<double>());EXPECT_DOUBLE_EQ(z,tip[11].To<double>());EXPECT_NEAR(std::hypot(10-x,-10-z),blocked.at("positionErrorMm").To<double>(),1e-7);EXPECT_FALSE(blocked.contains("collisionWitnessFraction"));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));
    // Freeze the gesture before injecting a stop, so scene snapshot/entry
    // checks cannot consume a solver/collision cancellation probe.
    const auto token=PDOEditToken();auto warmRequest=T::Target(scene,machine,0);warmRequest["editToken"]=token;ASSERT_EQ("Ready",Invoke(&scene,"CamSimulation","MoveMachineIK",warmRequest).at("status").To<Text>());
    auto cancelRequest=T::Target(scene,machine,10,0,-10);cancelRequest["editToken"]=token;int checks=0;scene.StopProbe=[&] {return ++checks>=6;};const auto cancelled=Invoke(&scene,"CamSimulation","MoveMachineIK",cancelRequest);scene.StopProbe={};EXPECT_EQ("Unchecked",cancelled.at("status").To<Text>());EXPECT_EQ("Unchecked",cancelled.at("collisionStatus").To<Text>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    auto deadlineRequest=T::Target(scene,machine,10,0,-10);deadlineRequest["editToken"]=token;checks=0;scene.StopProbe=[&]{if(++checks==6)std::this_thread::sleep_for(std::chrono::milliseconds(270));return false;};const auto expired=Invoke(&scene,"CamSimulation","MoveMachineIK",deadlineRequest);scene.StopProbe={};EXPECT_EQ("Unchecked",expired.at("status").To<Text>());EXPECT_EQ("Unchecked",expired.at("collisionStatus").To<Text>());EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));EXPECT_EQ(history,scene.Database().GetUndoArray().size());PDOEnd(scene,token,0,true);
    const auto recovered=T::Move(scene,machine,2);EXPECT_EQ("Ready",recovered.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(recovered);EXPECT_EQ("Clear",recovered.at("collisionStatus").To<Text>());EXPECT_NEAR(2,PDOJoint(scene,machine.X),.001);
}

TEST(FiveAxisInteractiveCollisionDiagnostics, ExactBoxPairsKeepRawAttachmentIndicesAcrossPDOAndClearAfterGeometryEdit)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    auto disabled=PlanBox(0,0,.04,.01).front().To<Map>();disabled["enabled"]=false;disabled["id"]=Text("disabled");disabled["name"]=Text("Disabled contact");
    auto distant=PlanBox(1,0,.04,.01).front().To<Map>();distant["id"]=Text("distant");distant["name"]=Text("Distant box");
    auto contact=PlanBox(0,0,.04,.01).front().To<Map>();contact["name"]=Text("X contact box");
    auto tool=PlanBox(0,0,.01).front().To<Map>();tool["id"]=Text("z-contact");tool["name"]=Text("Z contact box");
    auto& configure=scene.Database().BeginTransaction("Author exact collision-box diagnostic fixture");configure.ModifyComponent(machine.X,"CMachineCollisionComponent",{{"Collisions",Array{disabled,distant,contact}}});configure.ModifyComponent(machine.Tool,"CMachineCollisionComponent",{{"Collisions",Array{tool}}});ASSERT_TRUE(scene.Database().CommitTransaction(configure));
    const auto history=scene.Database().GetUndoArray().size();
    const auto verify=[&](const Map& result){
        EXPECT_EQ("Blocked",result.at("status").To<Text>());ASSERT_TRUE(result.at("collisionDiagnosticsComplete").To<bool>());const auto pairs=result.at("collisionPairs").To<Array>();ASSERT_EQ(1u,pairs.size());const auto pair=pairs.front().To<Map>(),first=pair.at("first").To<Map>(),second=pair.at("second").To<Map>();
        EXPECT_EQ(iCAX::Data::to_string(machine.X),first.at("ownerElementId").To<Text>());EXPECT_EQ("X",first.at("ownerElementName").To<Text>());EXPECT_EQ(2,first.at("boxIndex").To<int>());EXPECT_TRUE(first.at("boxId").To<Text>().empty());EXPECT_EQ("X contact box",first.at("boxName").To<Text>());EXPECT_EQ("Box",first.at("representation").To<Text>());EXPECT_TRUE(first.at("editable").To<bool>());
        EXPECT_EQ(iCAX::Data::to_string(machine.Tool),second.at("ownerElementId").To<Text>());EXPECT_EQ(0,second.at("boxIndex").To<int>());EXPECT_EQ("z-contact",second.at("boxId").To<Text>());EXPECT_EQ("Z contact box",second.at("boxName").To<Text>());
    };
    const auto blocked=NumericMachineIK(scene,machine,0);verify(blocked);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto begin=PDOBegin(scene,machine,PDOEditToken());scene.PDOHub().SwapOutSlot();PDOWrite(scene,begin,1,0);const auto streamed=PDOTick(scene,begin);verify(streamed);EXPECT_EQ("0",streamed.at("applied").To<Text>());PDOEnd(scene,begin.at("editToken").To<Text>(),1,true);
    contact["enabled"]=false;auto& remove=scene.Database().BeginTransaction("Disable only the colliding box");remove.ModifyComponent(machine.X,"CMachineCollisionComponent",{{"Collisions",Array{disabled,distant,contact}}});ASSERT_TRUE(scene.Database().CommitTransaction(remove));
    const auto clear=NumericMachineIK(scene,machine,0);EXPECT_EQ("Ready",clear.at("status").To<Text>());EXPECT_EQ("Clear",clear.at("collisionStatus").To<Text>());EXPECT_TRUE(!clear.contains("collisionPairs")||clear.at("collisionPairs").To<Array>().empty());EXPECT_TRUE(clear.at("collisionDiagnosticsComplete").To<bool>());
    contact["enabled"]=true;auto& restore=scene.Database().BeginTransaction("Restore colliding geometry");restore.ModifyComponent(machine.X,"CMachineCollisionComponent",{{"Collisions",Array{disabled,distant,contact}}});ASSERT_TRUE(scene.Database().CommitTransaction(restore));verify(NumericMachineIK(scene,machine,0));
}
