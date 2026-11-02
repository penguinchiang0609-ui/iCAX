// Gesture-local, lossy state transport. Only Begin/End/Cancel are reliable SDOs.
// The scene frame boundary swaps input before this pump and output afterwards.
namespace {
    Map InteractivePoseExpected(const SceneRuntime& runtime){return {{"epoch",runtime.Epoch},{"mutationSequence",std::to_string(runtime.Sequence)}};}
    void CollectMachineInteractionSlots(Scene& scene,SceneRuntime& runtime){
        if(!scene.HasPDOHub()){runtime.MachineInteractionRetiredSlots.clear();return;}
        auto& hub=scene.PDOHub();
        std::erase_if(runtime.MachineInteractionRetiredSlots,[&](UInt id){try{return !hub.HasSlot(id)||hub.FreeSlot(id);}catch(const std::logic_error&){return false;}});
    }
    void RetireInteractivePosePDO(Scene& scene,SceneRuntime& runtime,InteractiveMachinePoseRun& run){
        run.PDOActive=false;run.PendingResult.clear();
        if(run.TargetPDO)runtime.MachineInteractionRetiredSlots.push_back(run.TargetPDO);
        if(run.ResultPDO)runtime.MachineInteractionRetiredSlots.push_back(run.ResultPDO);
        run.TargetPDO=run.ResultPDO=0;CollectMachineInteractionSlots(scene,runtime);
    }
    std::vector<double> InteractivePosePositions(const InteractiveMachinePoseRun& run){return MachineJointValues(run.Machine);}
    void InteractivePoseMatrices(const InteractiveMachinePoseRun& run,std::vector<double>& tip,std::vector<double>& matrices){
        const auto world=MachineWorldFrames(run.Machine,run.Followers);
        for(const auto& m:world)matrices.insert(matrices.end(),m.begin(),m.end());
        tip.assign(world.at(run.Tip).begin(),world.at(run.Tip).end());
    }
    double InteractivePoseMetric(const Map& result,const char* key){
        if(!result.contains(key))return 0.;
        // Unavailable residuals are not represented as infinity in transport.
        try{return FiniteNumber(result.at(key),"CAM_POSE_METRIC_INVALID");}catch(const std::exception&){return 0.;}
    }
    void PrepareInteractivePoseResult(const SceneRuntime& runtime,InteractiveMachinePoseRun& run,const Map& result){
        const auto text=[&](const char* key){return result.contains(key)?String(result,key):Text{};};
        const auto status=text("status"),collision=text("collisionStatus"),reason=text("reason");
        std::vector<double> positions,tip,world;
        // A stale frozen snapshot cannot truthfully describe the current scene.
        if(!run.Invalidated){positions=InteractivePosePositions(run);InteractivePoseMatrices(run,tip,world);}
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<MachineCollisionPair>> collisionPairs;
        const auto collider=[&](const Map& ref){return CreateMachineCollisionColliderDirect(builder,
            String(ref,"ownerElementId").c_str(),String(ref,"ownerElementName").c_str(),String(ref,"boxId").c_str(),String(ref,"boxName").c_str(),
            ref.at("boxIndex").To<int>(),String(ref,"representation").c_str(),ref.at("editable").To<bool>());};
        if(result.contains("collisionPairs"))for(const auto& value:result.at("collisionPairs").To<Array>()){
            const auto pair=value.To<Map>();const auto first=collider(pair.at("first").To<Map>()),second=collider(pair.at("second").To<Map>());
            collisionPairs.push_back(CreateMachineCollisionPair(builder,first,second));
        }
        const auto record=CreateMachineIKResultDirect(builder,1,run.Token.c_str(),runtime.Epoch.c_str(),
            ++run.ResultVersion,run.ProcessedSequence,run.AppliedSequence,runtime.Sequence,status.c_str(),collision.c_str(),reason.c_str(),
            &positions,&tip,result.contains("cacheReused")&&result.at("cacheReused").To<bool>(),
            InteractivePoseMetric(result,"captureMilliseconds"),InteractivePoseMetric(result,"prepareMilliseconds"),
            InteractivePoseMetric(result,"solveMilliseconds"),InteractivePoseMetric(result,"collisionMilliseconds"),
            InteractivePoseMetric(result,"commitMilliseconds"),InteractivePoseMetric(result,"totalMilliseconds"),
            InteractivePoseMetric(result,"positionErrorMm"),InteractivePoseMetric(result,"rotationErrorRadians"),&world,&collisionPairs,
            result.contains("collisionDiagnosticsComplete")&&result.at("collisionDiagnosticsComplete").To<bool>());
        FinishMachineIKResultBuffer(builder,record);
        if(builder.GetSize()>static_cast<size_t>(run.ResultCapacity))throw std::length_error("CAM_POSE_RESULT_CAPACITY");
        run.PendingResult.assign(builder.GetBufferPointer(),builder.GetBufferPointer()+builder.GetSize());run.LastResult=result;
    }
    void PublishInteractivePoseResult(Scene& scene,InteractiveMachinePoseRun& run){
        if(run.PendingResult.empty()||!run.PDOActive)return;
        auto lease=iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(scene.PDOHub().GetSlot(run.ResultPDO),run.ResultVersion);
        if(!lease)return; // Never hold up a frame for a renderer's read lease.
        std::memcpy(lease->Data(),run.PendingResult.data(),run.PendingResult.size());lease->Commit(run.PendingResult.size());run.PendingResult.clear();
    }
    Map InteractivePoseDescriptor(Scene& scene,const InteractiveMachinePoseRun& run){
        const auto& name=scene.PDOHub().GetSharedArenaName();const Text arena(name.begin(),name.end()); // Hub names contain ASCII UUIDs.
        const auto descriptor=[&](UInt id){const auto& h=scene.PDOHub().GetSlot(id).GetHeader();return Map{{"id",std::to_string(id)},{"version",static_cast<int>(h.nVersion)},{"payloadSize",h.nPayloadSize},{"arenaName",arena}};};
        Array ids,worldIDs;for(const auto& n:run.Machine.Nodes){ids.emplace_back(n.ID);worldIDs.emplace_back(n.ID);}for(const auto& f:run.Followers)worldIDs.emplace_back(f.ID);
        std::vector<double> tip,world;InteractivePoseMatrices(run,tip,world);
        return {{"editToken",run.Token},{"epoch",Runtime(scene)->Epoch},{"expected",InteractivePoseExpected(*Runtime(scene))},
            {"motionMode",Text(run.ForwardJoint?"FK":"IK")},{"targetPolicy",Text(run.ClosestTarget?"Closest":"Strict")},{"jointIds",ids},{"positions",MachineNumbers(InteractivePosePositions(run))},{"targetPDO",descriptor(run.TargetPDO)},{"resultPDO",descriptor(run.ResultPDO)},
            {"worldIds",worldIDs},{"worldTransformIds",worldIDs},{"worldMatrices",MachineNumbers(world)},{"tipWorld",MachineNumbers(tip)},
            {"processedSequence",std::to_string(run.ProcessedSequence)},{"appliedSequence",std::to_string(run.AppliedSequence)}};
    }
}
Map BeginMachinePoseEdit(Scene& scene,const Map& request){
    CutFields(request,{"expected","machineId","tipId","editToken","motionMode","targetPolicy"});const auto runtime=Runtime(scene);
    const auto token=String(request,"editToken");if(!ID::from_string(token))throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_INVALID");
    if(runtime->PoseEditTerminal.contains(token))throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_RETIRED");
    if(const auto active=std::static_pointer_cast<InteractiveMachinePoseRun>(runtime->PoseEditCache);active&&active->PDOActive){
        if(active->Token!=token||active->MachineID!=String(request,"machineId")||active->TipID!=String(request,"tipId"))throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_MISMATCH");
        if((request.contains("motionMode")?String(request,"motionMode"):Text("IK"))!=(active->ForwardJoint?"FK":"IK"))throw std::invalid_argument("CAM_POSE_MOTION_MODE_MISMATCH");
        if((request.contains("targetPolicy")?String(request,"targetPolicy"):Text("Strict"))!=(active->ClosestTarget?"Closest":"Strict"))throw std::invalid_argument("CAM_POSE_TARGET_POLICY_MISMATCH");
        if(active->Invalidated||active->DomainSequence!=runtime->Sequence||active->ConfigurationSequence!=runtime->MachineConfigurationSequence)throw std::invalid_argument("CAM_POSE_EDIT_INVALIDATED");
        return InteractivePoseDescriptor(scene,*active);
    }
    if(runtime->PoseEditUndo||runtime->PoseEditCache||runtime->JogTick)throw std::invalid_argument("请先结束当前机床调整。");
    CheckExpected(*runtime,request);if(!scene.HasPDOHub())throw std::invalid_argument("CAM_POSE_PDO_UNAVAILABLE");
    CollectMachineInteractionSlots(scene,*runtime);Map metrics;auto run=CaptureInteractivePose(scene,request,runtime,metrics);
    const auto instance=iCAX::Data::to_string(scene.GetSceneID())+"/"+token;
    run->TargetPDO=iCAX::PDO::MakePDOID("iCAX.FiveAxisCAM.MachineIKTarget",instance);run->ResultPDO=iCAX::PDO::MakePDOID("iCAX.FiveAxisCAM.MachineIKResult",instance);
    const size_t capacity=65536+run->Machine.Nodes.size()*sizeof(double)+(run->Machine.Nodes.size()+run->Followers.size())*16*sizeof(double);
    if(capacity>static_cast<size_t>(std::numeric_limits<int>::max()))throw std::length_error("CAM_POSE_RESULT_CAPACITY");run->ResultCapacity=static_cast<int>(capacity);
    try{
        scene.PDOHub().AllocateSlot({1,run->TargetPDO,iCAX::PDO::kDirection2Inner,1024});
        scene.PDOHub().AllocateSlot({1,run->ResultPDO,iCAX::PDO::kDirection2External,run->ResultCapacity});run->PDOActive=true;
        runtime->PoseEditToken=token;runtime->PoseEditUndo=scene.Database().BeginUndoCommand("部件拖动");runtime->PoseEditCache=run;
        metrics["status"]=Text("Idle");PrepareInteractivePoseResult(*runtime,*run,metrics);PublishInteractivePoseResult(scene,*run);
        return InteractivePoseDescriptor(scene,*run);
    }catch(...){if(runtime->PoseEditUndo){runtime->PoseEditUndo->Cancel();runtime->PoseEditUndo.reset();}runtime->PoseEditToken.clear();runtime->PoseEditCache.reset();RetireInteractivePosePDO(scene,*runtime,*run);throw;}
}
void PumpMachinePoseEdit(Scene& scene){
    const auto runtime=Runtime(scene);CollectMachineInteractionSlots(scene,*runtime);
    const auto run=std::static_pointer_cast<InteractiveMachinePoseRun>(runtime->PoseEditCache);if(!run||!run->PDOActive||!scene.HasPDOHub())return;
    if(run->Invalidated){PublishInteractivePoseResult(scene,*run);return;}
    if(scene.IsStopRequested()||run->DomainSequence!=runtime->Sequence||run->ConfigurationSequence!=runtime->MachineConfigurationSequence){
        run->Invalidated=true;PrepareInteractivePoseResult(*runtime,*run,{{"status",Text("Invalidated")},{"reason",Text("机床或场景配置已改变，请结束本次拖动后重试。")}});PublishInteractivePoseResult(scene,*run);return;
    }
    UInt version=0;std::vector<std::uint8_t> packet;
    { // A lease never spans IK, collision checking or a database transaction.
        iCAX::PDO::CPDOReadLease lease(scene.PDOHub().GetSlot(run->TargetPDO));version=lease.DataVersion();
        if(version>run->InputVersion&&lease.PayloadSize())packet.assign(static_cast<const std::uint8_t*>(lease.Data()),static_cast<const std::uint8_t*>(lease.Data())+lease.PayloadSize());
    }
    if(packet.empty()){PublishInteractivePoseResult(scene,*run);return;}
    run->InputVersion=version;
    try{
        flatbuffers::Verifier verifier(packet.data(),packet.size());if(!VerifyMachineIKTargetBuffer(verifier))throw std::invalid_argument("CAM_POSE_TARGET_BUFFER_INVALID");
        const auto target=GetMachineIKTarget(packet.data());
        if(target->schema_version()!=1||target->edit_token()->str()!=run->Token||target->epoch()->str()!=runtime->Epoch||!target->sequence()||target->sequence()!=version||target->sequence()<=run->ProcessedSequence)throw std::invalid_argument("CAM_POSE_TARGET_IDENTITY_INVALID");
        Map request{{"expected",InteractivePoseExpected(*runtime)},{"machineId",run->MachineID},{"tipId",run->TipID},{"editToken",run->Token}};
        if(run->ForwardJoint){
            if(!target->joint_position().has_value()||target->target()->size()!=0)throw std::invalid_argument("CAM_POSE_JOINT_TARGET_REQUIRED");
            request["jointPosition"]=target->joint_position().value();
        }else{
            if(target->joint_position().has_value()||target->target()->size()!=16)throw std::invalid_argument("CAM_POSE_TARGET_MATRIX_INVALID");
            const Array matrix=MachineNumbers(*target->target());(void)RigidMatrix(matrix);request["target"]=matrix;
        }
        run->ProcessedSequence=target->sequence();
        auto result=MoveMachineIKCore(scene,request,true);
        if((String(result,"status")=="Ready"||String(result,"status")=="Approximated")&&result.contains("collisionStatus")&&String(result,"collisionStatus")=="Clear")run->AppliedSequence=run->ProcessedSequence;
        PrepareInteractivePoseResult(*runtime,*run,result);
    }catch(const std::exception& error){
        run->ProcessedSequence=version;run->Invalidated=true;PrepareInteractivePoseResult(*runtime,*run,{{"status",Text("Error")},{"reason",Text(error.what())}});
    }
    PublishInteractivePoseResult(scene,*run);
}
void ReleaseMachinePoseEdit(Scene& scene,const std::shared_ptr<SceneRuntime>& runtime){
    if(!runtime)return;
    if(runtime->PoseEditUndo){runtime->PoseEditUndo->Cancel();runtime->PoseEditUndo.reset();}
    if(const auto run=std::static_pointer_cast<InteractiveMachinePoseRun>(runtime->PoseEditCache);run&&run->PDOActive)RetireInteractivePosePDO(scene,*runtime,*run);
    runtime->PoseEditToken.clear();runtime->PoseEditCache.reset();CollectMachineInteractionSlots(scene,*runtime);
}
