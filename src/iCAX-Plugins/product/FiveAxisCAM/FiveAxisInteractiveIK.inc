namespace {
    struct InteractiveMachinePoseRun {
        Text Token,MachineID,TipID;
        UInt DomainSequence=0,ConfigurationSequence=0;
        MachineSnapshot Machine;
        SimulationCollisionSnapshot Obstacles;
        PreparedMachineCollision Collision;
        MachineCollisionQueryCache CollisionQueries;
        int Tip=-1;
        std::vector<MachineFollower> Followers;
        bool PDOActive=false,Invalidated=false,Committed=false,ForwardJoint=false,ClosestTarget=false;
        UInt TargetPDO=0,ResultPDO=0,ProcessedSequence=0,AppliedSequence=0,InputVersion=0,ResultVersion=0;
        int ResultCapacity=0;
        Map LastResult;
        std::vector<std::uint8_t> PendingResult;
    };
    double InteractivePoseMs(std::chrono::steady_clock::time_point start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    std::shared_ptr<InteractiveMachinePoseRun> CaptureInteractivePose(Scene& scene,const Map& request,const std::shared_ptr<SceneRuntime>& runtime,Map& result){
        auto run=std::make_shared<InteractiveMachinePoseRun>();run->Token=request.contains("editToken")?String(request,"editToken"):Text{};run->MachineID=String(request,"machineId");run->TipID=String(request,"tipId");
        const auto capture=std::chrono::steady_clock::now();auto [setupID,configuration]=MachineMoveConfiguration(scene,request);
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto setup=Component<CFiveAxisSetupComponent>(e);setup&&!setup->GetMachineSetup().empty()&&String(setup->GetMachineSetup(),"machineId")==run->MachineID){
            if(!setupID.is_nil())throw std::invalid_argument("该机床绑定了多个装夹，请先确定当前作业。");setupID=e->GetID();configuration=setup->GetMachineSetup();
        }
        run->Machine=CaptureMachine(scene,configuration);
        const auto found=std::find_if(run->Machine.Nodes.begin(),run->Machine.Nodes.end(),[&](const auto& n){return n.ID==run->TipID;});
        if(found==run->Machine.Nodes.end()||found->Parent<0)throw std::invalid_argument("请选择该机床的非根节点。");
        const auto mode=request.contains("motionMode")?String(request,"motionMode"):Text("IK");
        if(mode!="IK"&&mode!="FK")throw std::invalid_argument("CAM_POSE_MOTION_MODE_INVALID");
        run->ForwardJoint=mode=="FK";
        const auto policy=request.contains("targetPolicy")?String(request,"targetPolicy"):Text("Strict");
        if((policy!="Strict"&&policy!="Closest")||(run->ForwardJoint&&policy!="Strict"))throw std::invalid_argument("CAM_POSE_TARGET_POLICY_INVALID");
        run->ClosestTarget=policy=="Closest";
        run->Tip=static_cast<int>(found-run->Machine.Nodes.begin());
        bool movable=false;for(int i=run->Tip;i>=0;i=run->Machine.Nodes[i].Parent)movable=movable||run->Machine.Nodes[i].Type!="fixed";
        if(!movable)throw std::invalid_argument("该节点到根之间没有可运动的关节。");
        if(run->ForwardJoint&&found->Type!="revolute"&&found->Type!="continuous"&&found->Type!="prismatic")throw std::invalid_argument("该节点没有可直接调整的运动关节。");
        run->Obstacles=MachineMoveObstacles(scene,setupID);
        run->Followers=MachineFollowingFrames(scene,setupID,run->Machine.Placement);
        result["captureMilliseconds"]=InteractivePoseMs(capture);
        const auto prepare=std::chrono::steady_clock::now();run->Collision=PrepareMachineCollision(scene,run->Machine,run->Obstacles);result["prepareMilliseconds"]=InteractivePoseMs(prepare);
        run->DomainSequence=runtime->Sequence;run->ConfigurationSequence=runtime->MachineConfigurationSequence;return run;
    }
}
// Full world-space end-link pose. Interactive motion deliberately retains its
// existing complete 6D task; the planning service's 5D IJK task is not substituted.
static Map MoveMachineIKCore(Scene& scene,const Map& request,bool fromPDO=false){
    CutFields(request,{"expected","machineId","tipId","target","editToken","motionMode","jointPosition","targetPolicy"});
    const auto runtime=Runtime(scene);const auto editToken=request.contains("editToken")?String(request,"editToken"):Text{};
    if(!editToken.empty()&&!ID::from_string(editToken))throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_INVALID");
    if(runtime->PoseEditUndo&&runtime->PoseEditToken!=editToken)throw std::invalid_argument("请先结束当前部件拖动。");
    if(runtime->JogTick)throw std::invalid_argument("请先停止连续点动。");
    auto run=std::static_pointer_cast<InteractiveMachinePoseRun>(runtime->PoseEditCache);
    if(run&&run->PDOActive&&!fromPDO)throw std::invalid_argument("CAM_POSE_PDO_TARGET_REQUIRED: 当前拖动通过目标 PDO 更新。");
    if(run&&(run->DomainSequence!=runtime->Sequence||run->ConfigurationSequence!=runtime->MachineConfigurationSequence)){
        if(fromPDO)run->Invalidated=true;else runtime->PoseEditCache.reset();
        throw std::invalid_argument("机床、工件或夹具配置已改变，拖动已停止。请结束本次调整后重试。");
    }
    CheckExpected(*runtime,request);
    const auto begin=std::chrono::steady_clock::now();Map result;Motion::Matrix target=Motion::Identity();bool hasTarget=false;
    const auto finish=[&](){
        if(run&&!run->Invalidated){
            const auto world=Motion::Forward(run->Machine.Nodes,MachineJointValues(run->Machine));result["tipWorld"]=MachineNumbers(world.at(run->Tip));
            if(run->ClosestTarget&&hasTarget){const auto residual=Motion::MeasurePoseResidual(world.at(run->Tip),target);result["positionErrorMm"]=residual.PositionError;result["rotationErrorRadians"]=residual.RotationError;}
        }
        result["expected"]=Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}};result["totalMilliseconds"]=InteractivePoseMs(begin);return result;
    };
    const auto machineID=String(request,"machineId"),tip=String(request,"tipId");
    const bool reused=run&&!editToken.empty()&&run->Token==editToken&&run->MachineID==machineID&&run->TipID==tip;
    result["cacheReused"]=reused;result["captureMilliseconds"]=0.;result["prepareMilliseconds"]=0.;
    if(!reused){
        if(run&&!run->Token.empty())throw std::invalid_argument("请先结束当前部件拖动。");
        run=CaptureInteractivePose(scene,request,runtime,result);
        if(!editToken.empty())runtime->PoseEditCache=run;
    }
    auto& machine=run->Machine;std::vector<double> seed;for(const auto& n:machine.Nodes)seed.push_back(n.Position);
    if(request.contains("motionMode")&&String(request,"motionMode")!=(run->ForwardJoint?"FK":"IK"))throw std::invalid_argument("CAM_POSE_MOTION_MODE_MISMATCH");
    if(request.contains("targetPolicy")&&String(request,"targetPolicy")!=(run->ClosestTarget?"Closest":"Strict"))throw std::invalid_argument("CAM_POSE_TARGET_POLICY_MISMATCH");
    result["targetPolicy"]=Text(run->ClosestTarget?"Closest":"Strict");
    Motion::PoseSolution solution;
    if(run->ForwardJoint){
        if(!request.contains("jointPosition")||request.contains("target"))throw std::invalid_argument("CAM_POSE_JOINT_TARGET_REQUIRED");
        const auto value=FiniteNumber(request.at("jointPosition"),"CAM_POSE_JOINT_TARGET_INVALID");const auto& joint=machine.Nodes[run->Tip];
        solution.Positions=seed;solution.Positions[run->Tip]=value;solution.Reached=value>=joint.Lower&&value<=joint.Upper;
    }else{
        if(request.contains("jointPosition"))throw std::invalid_argument("CAM_POSE_IK_TARGET_REQUIRED");
        target=RigidMatrix(request.at("target").To<Array>());hasTarget=true;
        if(run->ClosestTarget){Motion::PoseSolveOptions options;options.IsCancelled=[&]{return scene.IsStopRequested();};solution=Motion::SolveClosestPoseKDL(machine.Nodes,run->Tip,target,seed,options);}
        else solution=Motion::SolvePoseKDL(machine.Nodes,run->Tip,target,seed);
    }
    result["status"]=Text(solution.Reached?"Ready":run->ClosestTarget&&solution.CandidateValid?"Approximated":"Unreachable");result["solver"]=Text(run->ForwardJoint?"Joint FK":run->ClosestTarget?"Orocos KDL bounded closest full pose":"Orocos KDL FullPose warm seed");result["solveMilliseconds"]=solution.SolveMilliseconds;result["positionErrorMm"]=solution.PositionError;result["rotationErrorRadians"]=solution.RotationError;
    if(run->ClosestTarget&&(solution.Cancelled||solution.BudgetExhausted||!solution.Converged)){
        result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["solverInterruption"]=Text(solution.Cancelled?"Cancelled":solution.BudgetExhausted?"Deadline":"Unresolved");result["reason"]=Text(solution.Cancelled?"本次目标求解已取消，机床保持原位置。":"本次目标求解未完成，机床保持原位置，请缩小调整量后继续。");return finish();
    }
    if(!solution.Reached&&!(run->ClosestTarget&&solution.CandidateValid)){result["reason"]=Text("目标位姿不可达或超出关节行程，机床保持上一个有效姿态。");return finish();}
    if(run->ClosestTarget&&!solution.Reached)result["reason"]=Text("目标超出当前机构可达范围，已尽量靠近目标。");
    // Closest IK follows one locally stepped seed branch, without distant
    // alternative starts. A large requested move is certified as a full sweep
    // below; the strict solver retains its existing branch-jump rejection.
    for(size_t i=0;i<machine.Nodes.size();++i)if(!run->ClosestTarget&&!run->ForwardJoint&&std::abs(solution.Positions[i]-seed[i])>(machine.Nodes[i].Type=="prismatic"?50.:.25)){
        result["status"]=Text("Discontinuous");result["reason"]=Text("目标需要关节突跳，已停止。请缩小拖动幅度或先点动离开奇异姿态。");return finish();
    }
    result["frames"]=Array{Variant(Map{{"joints",MachineNumbers(solution.Positions)}})};
    const auto collision=std::chrono::steady_clock::now();
    constexpr auto collisionBudget=std::chrono::milliseconds(250);
    CheckPreparedMachineCollision(machine,result,run->Collision,[&]{return scene.IsStopRequested();},static_cast<double>(collisionBudget.count()),&seed,&run->CollisionQueries);
    if(run->ClosestTarget){
        const auto interrupted=[&](){
            const bool cancelled=scene.IsStopRequested();if(!cancelled&&InteractivePoseMs(collision)<collisionBudget.count())return false;
            result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["collisionInterruption"]=Text(cancelled?"Cancelled":"Deadline");result["reason"]=Text(cancelled?"本次机床间隙检查已取消，机床保持原位置，可以继续调整。":"本次机床间隙检查超时，机床保持原位置，可缩小调整量后继续。");return true;
        };
        if(!interrupted()&&String(result,"collisionStatus")=="Blocked"){
            // A collision witness is an unsafe upper bound, never a certified
            // safe fraction. Independently certify each candidate prefix from
            // the authoritative seed under one shared collision deadline.
            const auto destination=solution.Positions;Map safe;double lower=0,upper=1;std::vector<double> accepted=seed;
            const auto check=[&](const std::vector<double>& positions){
                Map proof{{"status",Text("Ready")},{"frames",Array{Map{{"joints",MachineNumbers(positions)}}}}};
                CheckPreparedMachineCollision(machine,proof,run->Collision,[&]{return scene.IsStopRequested();},std::max(0.,static_cast<double>(collisionBudget.count())-InteractivePoseMs(collision)),&seed,&run->CollisionQueries);return proof;
            };
            auto proof=check(seed);bool complete=false;
            if(String(proof,"collisionStatus")=="Clear"){
                safe=proof;double span=0;for(size_t i=0;i<seed.size();++i)span=std::max(span,std::abs(destination[i]-seed[i])/(machine.Nodes[i].Type=="prismatic"?.01:1e-4));
                for(int iteration=0;iteration<24;++iteration){
                    if(interrupted())break;
                    if(span*(upper-lower)<=1){complete=true;break;}
                    const auto fraction=(lower+upper)*.5;auto trial=seed;for(size_t i=0;i<trial.size();++i)trial[i]+=(destination[i]-seed[i])*fraction;
                    proof=check(trial);const auto status=String(proof,"collisionStatus");
                    if(status=="Clear"){lower=fraction;accepted=std::move(trial);safe=proof;}
                    else if(status=="Blocked")upper=fraction;
                    else {for(const auto& [key,value]:proof)result[key]=value;break;}
                }
                complete=complete||span*(upper-lower)<=1;
                if(!interrupted()&&complete){
                    // Drop rejected-candidate collision alarms; the final
                    // receipt only carries the proof for the accepted prefix.
                    for(const auto* key:{"collisionFrame","collisionFraction","collisionBodies","collisionWitnessFraction","collisionJoints","alarmFrame","alarmJoints","collisionPairs","collisionDiagnosticsComplete"})result.erase(key);
                    for(const auto& [key,value]:safe)if(Text(key)!="frames")result[key]=value;
                    solution.Positions=std::move(accepted);result["status"]=Text("Approximated");result["reason"]=Text("已停在碰撞限制前可安全到达的位置。");result["acceptedFraction"]=lower;
                }else if(String(result,"collisionStatus")!="Unchecked"){
                    result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["collisionInterruption"]=Text("Unresolved");result["reason"]=Text("尚未完成可达位置的间隙检查，机床保持原位置。");
                }
            }else if(String(proof,"collisionStatus")=="Unchecked")for(const auto& [key,value]:proof)result[key]=value;
        }
        interrupted();
    }
    if(result.contains("collisionStatus")&&String(result,"collisionStatus")=="Blocked"){
        Text first="相关部件",second="相关部件";
        if(result.contains("collisionBodies")){const auto ids=result.at("collisionBodies").To<Array>();
            const auto name=[&](size_t index){if(index>=ids.size())return Text("相关部件");const auto id=ids[index].To<Text>();for(const auto& body:run->Collision.Bodies)if(body.ID==id)return body.Name;return id;};first=name(0);second=name(1);
        }
        const bool atStart=result.contains("collisionWitnessFraction")&&result.at("collisionWitnessFraction").To<double>()<=1e-12;
        result["reason"]=Text((atStart?"当前姿态中，":"本次调整会使")+Text("“")+first+"”与“"+second+"”的检测间隙不足 0.1 mm，本次调整未执行。");
    }
    result["collisionMilliseconds"]=InteractivePoseMs(collision);
    if(String(result,"collisionStatus")=="Clear"){
        Map edits;for(size_t i=0;i<machine.Nodes.size();++i)if(machine.Nodes[i].Type!="fixed"&&std::abs(solution.Positions[i]-seed[i])>1e-10)edits[machine.Nodes[i].ID]=solution.Positions[i];
        const auto commit=std::chrono::steady_clock::now();
        if(!edits.empty()){
            if(!editToken.empty()&&!runtime->PoseEditUndo){runtime->PoseEditToken=editToken;runtime->PoseEditUndo=scene.Database().BeginUndoCommand("部件拖动");}
            Transaction(scene.Database(),run->ForwardJoint?"关节 FK 调整":"节点 IK 调整",[&](auto& tx){for(const auto& [key,value]:edits)tx.ModifyComponent(ID::from_string(key).value(),iCAX::CAM::CMachineJointComponent::S_ClassName,{{"Position",value}});},editToken.empty());
            for(const auto& [key,value]:edits)iCAX::CAM::SynchronizeMachineElementPose(*scene.Database().GetEntity(ID::from_string(key).value()));
            for(size_t i=0;i<machine.Nodes.size();++i)machine.Nodes[i].Position=solution.Positions[i];
            run->Committed=true;
        }
        // Only this receipt's own accepted joint commits advance the frozen
        // cache fence. Any intervening external edit invalidates it above.
        run->ConfigurationSequence=runtime->MachineConfigurationSequence;run->DomainSequence=runtime->Sequence;
        result["commitMilliseconds"]=InteractivePoseMs(commit);result["positions"]=edits;
    }
    result.erase("frames");result.erase("allowedContacts");return finish();
}
Map MoveMachineIK(Scene& scene,const Map& request){return MoveMachineIKCore(scene,request);}
#include "FiveAxisInteractiveIKPDO.inc"
Map EndMachinePoseEdit(Scene& scene,const Map& request){
    CutFields(request,{"editToken","cancel","finalSequence"});const auto runtime=Runtime(scene);const auto token=String(request,"editToken");
    if(!ID::from_string(token))throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_INVALID");
    if(const auto ended=runtime->PoseEditTerminal.find(token);ended!=runtime->PoseEditTerminal.end())return ended->second;
    const auto run=std::static_pointer_cast<InteractiveMachinePoseRun>(runtime->PoseEditCache);
    const bool cancel=request.contains("cancel")&&request.at("cancel").To<bool>();
    if((run&&run->Token!=token)||(runtime->PoseEditUndo&&runtime->PoseEditToken!=token)){
        if(!cancel)throw std::invalid_argument("CAM_POSE_EDIT_TOKEN_MISMATCH");
        // Cancellation may beat a delayed Begin response, and must never end a
        // different active gesture. Its token is retired to reject late Begin.
        Map result{{"changed",false},{"cancelled",true},{"pending",false},{"processedSequence",Text("0")},{"expected",InteractivePoseExpected(*runtime)}};
        runtime->PoseEditTerminal[token]=result;if(runtime->PoseEditTerminal.size()>64)runtime->PoseEditTerminal.erase(runtime->PoseEditTerminal.begin());return result;
    }
    if(run&&run->PDOActive&&run->Invalidated&&!cancel)throw std::invalid_argument("CAM_POSE_EDIT_INVALIDATED: 请取消本次拖动。");
    if(run&&run->PDOActive&&!cancel){const auto finalSequence=Integer(request,"finalSequence");if(finalSequence<run->ProcessedSequence)throw std::invalid_argument("CAM_POSE_FINAL_SEQUENCE_INVALID");if(finalSequence>run->ProcessedSequence)return {{"pending",true},{"processedSequence",std::to_string(run->ProcessedSequence)},{"expected",InteractivePoseExpected(*runtime)}};}
    const bool changed=runtime->PoseEditUndo&&(!run||!run->PDOActive||run->Committed);
    if(runtime->PoseEditUndo){if(cancel)runtime->PoseEditUndo->Cancel();else runtime->PoseEditUndo->End();runtime->PoseEditUndo.reset();runtime->PoseEditToken.clear();}
    if(run&&run->PDOActive)RetireInteractivePosePDO(scene,*runtime,*run);
    runtime->PoseEditCache.reset();Map result{{"changed",changed},{"cancelled",cancel},{"pending",false},{"processedSequence",std::to_string(run?run->ProcessedSequence:0)},{"expected",InteractivePoseExpected(*runtime)}};
    runtime->PoseEditTerminal[token]=result;if(runtime->PoseEditTerminal.size()>64)runtime->PoseEditTerminal.erase(runtime->PoseEditTerminal.begin());return result;
}
