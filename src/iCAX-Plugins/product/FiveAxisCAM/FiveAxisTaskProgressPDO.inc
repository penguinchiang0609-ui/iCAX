// Included in the task helpers' anonymous namespace. Worker-owned progress
// maps are copied under their existing locks; only the Scene touches slots.
struct TaskProgressPDOChannel final {
    UInt Slot=0;
    std::uint64_t Sequence=0;
    Text LastTelemetry;
    std::chrono::steady_clock::time_point LastFrame{};
    std::vector<std::uint8_t> Pending;
};
void CollectTaskProgressSlots(Scene& scene,SceneRuntime& runtime) {
    if(!scene.HasPDOHub()){runtime.TaskProgressRetiredSlots.clear();return;}
    auto& hub=scene.PDOHub();
    std::erase_if(runtime.TaskProgressRetiredSlots,[&](UInt id){
        try{return !hub.HasSlot(id)||hub.FreeSlot(id);}catch(const std::logic_error&){return false;}
    });
}
void AttachTaskProgressPDO(Scene& scene,const SceneRuntime& runtime,const ImportTask& task,Map& receipt) {
    // Native headless callers can still use the reliable result API. Browser
    // consumers require this descriptor and never substitute mailbox polling.
    if(!scene.HasPDOHub())return;
    auto channel=std::static_pointer_cast<TaskProgressPDOChannel>(task.ProgressPDO);
    if(!channel&&task.Status=="Running"){
        channel=std::make_shared<TaskProgressPDOChannel>();
        const auto instance=iCAX::Data::to_string(scene.GetSceneID())+"/"+runtime.Epoch+"/"+task.TaskID;
        channel->Slot=iCAX::PDO::MakePDOID("iCAX.FiveAxisCAM.TaskProgress",instance);
        scene.PDOHub().AllocateSlot({1,channel->Slot,iCAX::PDO::kDirection2External,65536});
        task.ProgressPDO=channel;
    }
    if(!channel||!channel->Slot)return;
    auto& hub=scene.PDOHub();const auto& name=hub.GetSharedArenaName();
    const auto& header=hub.GetSlot(channel->Slot).GetHeader();
    receipt["progressPDO"]=Map{{"id",std::to_string(channel->Slot)},{"version",static_cast<int>(header.nVersion)},
        {"payloadSize",header.nPayloadSize},{"arenaName",Text(name.begin(),name.end())}};
}
void RetireTaskProgressPDO(Scene& scene,SceneRuntime& runtime,const ImportTask& task) {
    if(const auto channel=std::static_pointer_cast<TaskProgressPDOChannel>(task.ProgressPDO)){
        if(channel->Slot)runtime.TaskProgressRetiredSlots.push_back(channel->Slot);
        task.ProgressPDO.reset();
    }
    CollectTaskProgressSlots(scene,runtime);
}
void AbandonTaskProgressPDO(Scene& scene,SceneRuntime& runtime,const ImportTask& task) {
    if(const auto channel=std::static_pointer_cast<TaskProgressPDOChannel>(task.ProgressPDO)){
        if(channel->Slot)runtime.TaskProgressRetiredSlots.push_back(channel->Slot);
        // Retain an empty channel marker until terminal QueryTask: a still
        // winding-down task must not recreate the abandoned slot in a receipt.
        channel->Slot=0;channel->Pending.clear();
    }
    CollectTaskProgressSlots(scene,runtime);
}
void PumpTaskProgressPDOImpl(Scene& scene,SceneRuntime& runtime) {
    CollectTaskProgressSlots(scene,runtime);if(!scene.HasPDOHub())return;
    for(const auto& [_,task]:runtime.Tasks){
        const auto channel=std::static_pointer_cast<TaskProgressPDOChannel>(task.ProgressPDO);if(!channel||!channel->Slot)continue;
        const auto telemetry=WireJsonValue(TaskTelemetry(runtime,task)).To<Map>();
        const auto signature=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(telemetry);
        const auto now=std::chrono::steady_clock::now();
        // A healthy long-running task may have no new percentage or stage yet.
        // Publish producer liveness through the overwritable PDO snapshot;
        // unchanged terminal receipts remain stable until their final query.
        const bool heartbeat=task.Status=="Running"&&now-channel->LastFrame>=std::chrono::seconds(1);
        if(signature!=channel->LastTelemetry||heartbeat){
            Map details;for(const auto* key:{"collisionProgress","repairProgress","machineProgress"})
                if(telemetry.contains(key))details[key]=telemetry.at(key);
            const auto json=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(details);
            flatbuffers::FlatBufferBuilder builder;
            const auto root=iCAX::FiveAxisCAM::CreateTaskProgressDirect(builder,1,task.TaskID.c_str(),runtime.Epoch.c_str(),
                ++channel->Sequence,task.Status.c_str(),task.Kind.c_str(),task.Progress->load(),task.Error.c_str(),task.TerminationReason.c_str(),json.c_str());
            iCAX::FiveAxisCAM::FinishTaskProgressBuffer(builder,root);
            if(builder.GetSize()>65536)throw std::length_error("CAM_TASK_PROGRESS_CAPACITY");
            channel->Pending.assign(builder.GetBufferPointer(),builder.GetBufferPointer()+builder.GetSize());
            channel->LastTelemetry=signature;
            channel->LastFrame=now;
        }
        if(channel->Pending.empty())continue;
        auto lease=iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(scene.PDOHub().GetSlot(channel->Slot),channel->Sequence);
        if(!lease)continue; // A renderer holding a read lease never blocks Tick.
        std::memcpy(lease->Data(),channel->Pending.data(),channel->Pending.size());
        lease->Commit(channel->Pending.size());channel->Pending.clear();
    }
}
void ReleaseTaskProgressPDOImpl(Scene& scene,SceneRuntime& runtime) {
    for(auto& [_,task]:runtime.Tasks){task.Cancelled->store(true);RetireTaskProgressPDO(scene,runtime,task);}
    CollectTaskProgressSlots(scene,runtime);
}
