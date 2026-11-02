namespace {
    using JogClock=std::chrono::steady_clock;
    constexpr auto JogLeaseDuration=std::chrono::milliseconds(300);
    struct MachineJogRun {
        bool PreciseTimer=timeBeginPeriod(1)==TIMERR_NOERROR;
        ~MachineJogRun(){if(PreciseTimer)timeEndPeriod(1);}
        MachineSnapshot Machine;
        ID SetupID;
        SimulationCollisionSnapshot Obstacles;
        PreparedMachineCollision Collision;
        std::vector<MachineFollower> Followers;
        size_t Joint=0,Ticks=0;
        double Speed=0,CollisionMs=0,TickMs=0,IntervalMs=0;
        UInt Sequence=0,ConfigurationSequence=0,ControlPDO=0,ResultPDO=0,InputVersion=0,ProcessedSequence=0,ResultVersion=0;
        int ResultCapacity=0;
        bool Held=false,Committed=false,Invalidated=false,OutputDirty=true;
        std::vector<std::uint8_t> PendingResult;
    };
    void CopyMachineJogCollision(SceneRuntime& runtime,const Map& result){
        for(const auto* key:{"collisionStatus","collisionPairs","collisionDiagnosticsComplete","collisionBodies"}){
            if(result.contains(key))runtime.JogStatus[key]=result.at(key);else runtime.JogStatus.erase(key);
        }
    }
    void FinishMachineJog(SceneRuntime& runtime,const Text& status,const Text& reason={}){
        runtime.JogTick={};runtime.JogStatus["status"]=status;
        if(!reason.empty())runtime.JogStatus["reason"]=reason;else runtime.JogStatus.erase("reason");
        if(runtime.JogUndo){runtime.JogUndo->End();runtime.JogUndo.reset();}
        if(const auto run=std::static_pointer_cast<MachineJogRun>(runtime.JogCache)){run->Held=false;run->OutputDirty=true;}
    }
    void RetireMachineJogPDO(Scene& scene,SceneRuntime& runtime,MachineJogRun& run){
        if(run.ControlPDO)runtime.MachineInteractionRetiredSlots.push_back(run.ControlPDO);
        if(run.ResultPDO)runtime.MachineInteractionRetiredSlots.push_back(run.ResultPDO);
        run.ControlPDO=run.ResultPDO=0;run.PendingResult.clear();CollectMachineInteractionSlots(scene,runtime);
    }
    std::vector<double> MachineJogMatrices(const MachineJogRun& run){
        std::vector<double> values;for(const auto& m:MachineWorldFrames(run.Machine,run.Followers))values.insert(values.end(),m.begin(),m.end());return values;
    }
    Map MachineJogState(Scene& scene,const SceneRuntime& runtime,const MachineJogRun& run,bool descriptors=false){
        Array ids,worldIDs;for(const auto& n:run.Machine.Nodes){ids.emplace_back(n.ID);worldIDs.emplace_back(n.ID);}for(const auto& f:run.Followers)worldIDs.emplace_back(f.ID);
        auto result=runtime.JogStatus;result["token"]=runtime.JogToken;result["epoch"]=runtime.Epoch;result["expected"]=InteractivePoseExpected(runtime);
        result["jointIds"]=ids;result["worldIds"]=worldIDs;result["worldTransformIds"]=worldIDs;result["positions"]=run.Invalidated?Array{}:MachineNumbers(MachineJointValues(run.Machine));
        result["worldMatrices"]=run.Invalidated?Array{}:MachineNumbers(MachineJogMatrices(run));result["changed"]=run.Committed;result["active"]=static_cast<bool>(runtime.JogTick);result["ticks"]=std::to_string(run.Ticks);result["processedSequence"]=std::to_string(run.ProcessedSequence);result["leaseMilliseconds"]=300;result["timings"]=runtime.JogTimings;
        if(descriptors&&run.ControlPDO&&run.ResultPDO){
            const auto& name=scene.PDOHub().GetSharedArenaName();Text arena;for(const auto c:name)arena.push_back(static_cast<char>(c));
            const auto descriptor=[&](UInt id){const auto& h=scene.PDOHub().GetSlot(id).GetHeader();return Map{{"id",std::to_string(id)},{"version",static_cast<int>(h.nVersion)},{"payloadSize",h.nPayloadSize},{"arenaName",arena}};};
            result["controlPDO"]=descriptor(run.ControlPDO);result["resultPDO"]=descriptor(run.ResultPDO);
        }return result;
    }
    void PublishMachineJogPDO(Scene& scene,SceneRuntime& runtime,MachineJogRun& run){
        if(!run.ResultPDO)return;
        if(run.OutputDirty){
            const auto status=String(runtime.JogStatus,"status"),reason=runtime.JogStatus.contains("reason")?String(runtime.JogStatus,"reason"):Text{};
            const auto positions=run.Invalidated?std::vector<double>{}:MachineJointValues(run.Machine),world=run.Invalidated?std::vector<double>{}:MachineJogMatrices(run);
            flatbuffers::FlatBufferBuilder b;const auto record=CreateMachineJogResultDirect(b,1,runtime.JogToken.c_str(),runtime.Epoch.c_str(),++run.ResultVersion,run.ProcessedSequence,runtime.Sequence,status.c_str(),reason.c_str(),&positions,&world,run.Ticks,run.CollisionMs,run.TickMs,run.IntervalMs);FinishMachineJogResultBuffer(b,record);
            if(b.GetSize()>static_cast<size_t>(run.ResultCapacity))throw std::length_error("CAM_JOG_RESULT_CAPACITY");run.PendingResult.assign(b.GetBufferPointer(),b.GetBufferPointer()+b.GetSize());run.OutputDirty=false;
        }
        if(run.PendingResult.empty())return;auto lease=iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(scene.PDOHub().GetSlot(run.ResultPDO),run.ResultVersion);if(!lease)return;
        std::memcpy(lease->Data(),run.PendingResult.data(),run.PendingResult.size());lease->Commit(run.PendingResult.size());run.PendingResult.clear();
    }
    void RememberStoppedJog(SceneRuntime& runtime,const Text& token,const Map& receipt){
        runtime.JogTerminal[token]=receipt;
        if(runtime.JogTerminal.size()>128)runtime.JogTerminal.erase(runtime.JogTerminal.begin());
    }
}
Map StartMachineJog(Scene& scene,const Map& request){
    CutFields(request,{"token","expected","setupId","machineId","jointId","direction","speed"});const auto runtime=Runtime(scene);const auto token=String(request,"token");
    if(!ID::from_string(token))throw std::invalid_argument("CAM_JOG_TOKEN_REQUIRED");
    if(const auto ended=runtime->JogTerminal.find(token);ended!=runtime->JogTerminal.end())return ended->second;
    if(const auto prior=std::static_pointer_cast<MachineJogRun>(runtime->JogCache);prior&&runtime->JogToken==token)return MachineJogState(scene,*runtime,*prior,true);
    if(runtime->JogTick||runtime->PoseEditUndo||runtime->PoseEditCache)throw std::invalid_argument("请先结束当前机床调整。");
    CheckExpected(*runtime,request);if(!scene.HasPDOHub())throw std::invalid_argument("CAM_JOG_PDO_UNAVAILABLE");
    if(const auto old=std::static_pointer_cast<MachineJogRun>(runtime->JogCache)){
        RememberStoppedJog(*runtime,runtime->JogToken,MachineJogState(scene,*runtime,*old));RetireMachineJogPDO(scene,*runtime,*old);runtime->JogCache.reset();
    }
    const auto [id,configuration]=MachineMoveConfiguration(scene,request);auto run=std::make_shared<MachineJogRun>();run->SetupID=id;run->Machine=CaptureMachine(scene,configuration);run->Obstacles=MachineMoveObstacles(scene,id);run->Followers=MachineFollowingFrames(scene,id,run->Machine.Placement);
    const auto joint=String(request,"jointId");const auto found=std::find_if(run->Machine.Nodes.begin(),run->Machine.Nodes.end(),[&](const auto& n){return n.ID==joint&&n.Type!="fixed";});if(found==run->Machine.Nodes.end())throw std::invalid_argument("CAM_MACHINE_JOINT_REQUIRED");
    const auto direction=FiniteNumber(request.at("direction"),"CAM_JOG_DIRECTION_INVALID"),speed=FiniteNumber(request.at("speed"),"CAM_JOG_SPEED_INVALID");
    if((direction!=1&&direction!=-1)||speed<=0||!std::isfinite(found->Velocity)||found->Velocity<=0||speed>found->Velocity)throw std::invalid_argument("点动速度必须大于零，且不能超过该轴的速度上限。");
    run->Joint=std::distance(run->Machine.Nodes.begin(),found);run->Speed=speed*direction;run->Sequence=runtime->Sequence;run->ConfigurationSequence=runtime->MachineConfigurationSequence;
    Map checked{{"status",Text("Ready")},{"frames",Array{Variant(Map{{"joints",MachineNumbers(MachineJointValues(run->Machine))}})}}};CheckMachineCollision(scene,run->Machine,run->Obstacles,checked,&run->Collision);
    runtime->JogToken=token;runtime->JogTimings.clear();runtime->JogStatus={{"token",token},{"status",Text("Running")}};runtime->JogCache=run;
    CopyMachineJogCollision(*runtime,checked);
    if(String(checked,"collisionStatus")!="Clear"){
        runtime->JogStatus["status"]=checked.at("collisionStatus");runtime->JogStatus["reason"]=checked.at("reason");return MachineJogState(scene,*runtime,*run);
    }
    const auto instance=iCAX::Data::to_string(scene.GetSceneID())+"/"+token;run->ControlPDO=iCAX::PDO::MakePDOID("iCAX.FiveAxisCAM.MachineJogControl",instance);run->ResultPDO=iCAX::PDO::MakePDOID("iCAX.FiveAxisCAM.MachineJogResult",instance);
    const auto capacity=2048+run->Machine.Nodes.size()*sizeof(double)+(run->Machine.Nodes.size()+run->Followers.size())*16*sizeof(double);if(capacity>static_cast<size_t>(std::numeric_limits<int>::max()))throw std::length_error("CAM_JOG_RESULT_CAPACITY");run->ResultCapacity=static_cast<int>(capacity);
    try{
        scene.PDOHub().AllocateSlot({1,run->ControlPDO,iCAX::PDO::kDirection2Inner,512});scene.PDOHub().AllocateSlot({1,run->ResultPDO,iCAX::PDO::kDirection2External,run->ResultCapacity});
        runtime->JogLease=JogClock::now()+JogLeaseDuration;runtime->JogUndo=scene.Database().BeginUndoCommand("连续点动");
        runtime->JogTick=[run](Scene& current,double dt){
            const auto state=Runtime(current);const auto begin=JogClock::now();
            if(state->Sequence!=run->Sequence||state->MachineConfigurationSequence!=run->ConfigurationSequence){run->Invalidated=true;FinishMachineJog(*state,"Invalidated","机床或作业配置已改变，连续点动已停止。");return;}
            auto& node=run->Machine.Nodes[run->Joint];const auto target=std::clamp(node.Position+run->Speed*std::clamp(dt,0.,.02),node.Lower,node.Upper);const bool limit=run->Speed>0?target>=node.Upper:target<=node.Lower;
            auto next=MachineJointValues(run->Machine);next[run->Joint]=target;Map result{{"status",Text("Ready")},{"frames",Array{Variant(Map{{"joints",MachineNumbers(next)}})}}};const auto collisionStart=JogClock::now();CheckMachineCollision(current,run->Machine,run->Obstacles,result,&run->Collision,8.);run->CollisionMs=InteractivePoseMs(collisionStart);run->IntervalMs=dt*1000;
            CopyMachineJogCollision(*state,result);
            if(String(result,"collisionStatus")!="Clear"){run->TickMs=InteractivePoseMs(begin);FinishMachineJog(*state,String(result,"collisionStatus"),String(result,"reason"));return;}
            if(target!=node.Position){const auto entity=ID::from_string(node.ID).value();state->JogCommitting=true;
                try{Transaction(current.Database(),"连续点动",[&](auto& tx){tx.ModifyComponent(entity,iCAX::CAM::CMachineJointComponent::S_ClassName,{{"Position",target}});},false);iCAX::CAM::SynchronizeMachineElementPose(*current.Database().GetEntity(entity));}catch(...){state->JogCommitting=false;throw;}
                state->JogCommitting=false;node.Position=target;run->Committed=true;
            }
            run->TickMs=InteractivePoseMs(begin);++run->Ticks;run->OutputDirty=true;
            if(state->JogTimings.size()<600)state->JogTimings.emplace_back(Map{{"collisionMs",run->CollisionMs},{"tickMs",run->TickMs},{"intervalMs",run->IntervalMs}});
            if(limit)FinishMachineJog(*state,"Limit","轴已到达行程限位，连续点动已停止。");
        };
        PublishMachineJogPDO(scene,*runtime,*run);return MachineJogState(scene,*runtime,*run,true);
    }catch(...){if(runtime->JogUndo){runtime->JogUndo->Cancel();runtime->JogUndo.reset();}runtime->JogTick={};RetireMachineJogPDO(scene,*runtime,*run);runtime->JogCache.reset();throw;}
}
Map StopMachineJog(Scene& scene,const Map& request){
    CutFields(request,{"token"});const auto runtime=Runtime(scene);const auto token=String(request,"token");if(!ID::from_string(token))throw std::invalid_argument("CAM_JOG_TOKEN_REQUIRED");
    if(const auto ended=runtime->JogTerminal.find(token);ended!=runtime->JogTerminal.end())return ended->second;
    const auto run=std::static_pointer_cast<MachineJogRun>(runtime->JogCache);
    if(token!=runtime->JogToken||!run){Map result{{"token",token},{"epoch",runtime->Epoch},{"expected",InteractivePoseExpected(*runtime)},{"status",Text("Stopped")},{"changed",false},{"active",false},{"jointIds",Array{}},{"positions",Array{}},{"worldIds",Array{}},{"worldTransformIds",Array{}},{"worldMatrices",Array{}},{"ticks",Text("0")}};RememberStoppedJog(*runtime,token,result);return result;}
    if(runtime->Sequence!=run->Sequence||runtime->MachineConfigurationSequence!=run->ConfigurationSequence)run->Invalidated=true;
    if(run->Invalidated){runtime->JogStatus.erase("collisionPairs");runtime->JogStatus.erase("collisionBodies");runtime->JogStatus["collisionDiagnosticsComplete"]=false;runtime->JogStatus["collisionStatus"]=Text("Unchecked");}
    if(runtime->JogTick)FinishMachineJog(*runtime,run->Invalidated?"Invalidated":"Stopped",run->Invalidated?"机床或作业配置已改变，连续点动已停止。":Text{});
    // Terminal reliable receipts describe the actual scene, even when an
    // external edit invalidated the frozen collision/visual snapshot.
    if(run->Invalidated)try{run->Machine=CaptureMachine(scene,run->Machine.Configuration);run->Followers=MachineFollowingFrames(scene,run->SetupID,run->Machine.Placement);run->Invalidated=false;}catch(const std::exception&){}
    auto result=MachineJogState(scene,*runtime,*run);result["timings"]=runtime->JogTimings;RetireMachineJogPDO(scene,*runtime,*run);runtime->JogCache.reset();RememberStoppedJog(*runtime,token,result);return result;
}
void PumpMachineJog(Scene& scene,double dt){
    const auto runtime=Runtime(scene);CollectMachineInteractionSlots(scene,*runtime);const auto run=std::static_pointer_cast<MachineJogRun>(runtime->JogCache);if(!run)return;
    if(!runtime->JogTick){PublishMachineJogPDO(scene,*runtime,*run);return;}
    if(runtime->Sequence!=run->Sequence||runtime->MachineConfigurationSequence!=run->ConfigurationSequence){run->Invalidated=true;FinishMachineJog(*runtime,"Invalidated","机床或作业配置已改变，连续点动已停止。");PublishMachineJogPDO(scene,*runtime,*run);return;}
    if(JogClock::now()>=runtime->JogLease){FinishMachineJog(*runtime,"Expired","点动连接中断，已停止。");PublishMachineJogPDO(scene,*runtime,*run);return;}
    try{
        std::vector<std::uint8_t> packet;UInt version=0;
        {iCAX::PDO::CPDOReadLease lease(scene.PDOHub().GetSlot(run->ControlPDO));version=lease.DataVersion();if(version>run->InputVersion&&lease.PayloadSize())packet.assign(static_cast<const std::uint8_t*>(lease.Data()),static_cast<const std::uint8_t*>(lease.Data())+lease.PayloadSize());}
        if(!packet.empty()){
            run->InputVersion=version;flatbuffers::Verifier v(packet.data(),packet.size());if(!VerifyMachineJogControlBuffer(v))throw std::invalid_argument("CAM_JOG_CONTROL_BUFFER_INVALID");const auto control=GetMachineJogControl(packet.data());
            if(control->schema_version()!=1||control->token()->str()!=runtime->JogToken||control->epoch()->str()!=runtime->Epoch||!control->sequence()||control->sequence()!=version||control->sequence()<=run->ProcessedSequence)throw std::invalid_argument("CAM_JOG_CONTROL_IDENTITY_INVALID");
            run->ProcessedSequence=control->sequence();run->Held=control->held();run->OutputDirty=true;runtime->JogLease=JogClock::now()+JogLeaseDuration;
            if(!run->Held)FinishMachineJog(*runtime,"Stopped");
        }
        if(runtime->JogTick&&run->Held){const auto tick=runtime->JogTick;tick(scene,dt);}
    }catch(const std::exception& error){FinishMachineJog(*runtime,"Error",Text(error.what()));}
    PublishMachineJogPDO(scene,*runtime,*run);
}
void ReleaseMachineJog(Scene& scene,const std::shared_ptr<SceneRuntime>& runtime){
    if(!runtime)return;if(runtime->JogTick||runtime->JogUndo)FinishMachineJog(*runtime,"Stopped");
    if(const auto run=std::static_pointer_cast<MachineJogRun>(runtime->JogCache)){RememberStoppedJog(*runtime,runtime->JogToken,MachineJogState(scene,*runtime,*run));RetireMachineJogPDO(scene,*runtime,*run);runtime->JogCache.reset();}
    CollectMachineInteractionSlots(scene,*runtime);
}
