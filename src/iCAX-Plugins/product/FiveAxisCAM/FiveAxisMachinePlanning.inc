// Device planning adapter. Neutral solvers own kinematics and candidate graphs;
// the scene owns CAM meaning, frozen geometry, receipts and embedded resources.
struct MachinePlanSnapshot {
    MachineSnapshot Machine;
    Map State;
    Text InputKey;
    bool PlacementPreview=false;
    double PreviewMaximumSeconds=60;
    std::shared_ptr<const SimulationCollisionSnapshot> Trajectory;
    PreparedMachineCollision Collision;
    std::vector<Ref> Dependencies;
    std::map<Text,GenerationCut> Sources;
};
namespace {
    namespace MK=iCAX::GeometryServices::MachineKinematics;
    namespace MM=iCAX::GeometryServices::MachineMotion;
    Ref PublishJsonQueryResource(Scene& scene,const ID& setupID,const Text& kind,const Text& key,
        const Map& body,const std::vector<Ref>& dependencies,const char* identifier,const Text& title){
        auto& cache=Runtime(scene)->MachineWireResources;const auto slot=iCAX::Data::to_string(setupID)+":"+kind;
        if(const auto found=cache.find(slot);found!=cache.end()&&found->second.first==key&&
            scene.Resources().GetInfo(found->second.second.URL,found->second.second.nVersion))return found->second.second;
        const auto bytes=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(WireJsonValue(body));
        flatbuffers::FlatBufferBuilder builder;const auto json=builder.CreateString(bytes);
        const auto table=builder.StartTable();builder.AddElement<std::uint32_t>(4,1,0);builder.AddOffset(6,json);
        builder.Finish(flatbuffers::Offset<void>(builder.EndTable(table)),identifier);
        auto value=std::make_shared<iCAX::Resource::CFlatBufferResource>(std::vector<std::uint8_t>(builder.GetBufferPointer(),builder.GetBufferPointer()+builder.GetSize()));
        iCAX::Resource::CResourceInfo info;info.Name=title;
        info.ResourceTypeID=iCAX::Resource::CFlatBufferResource::kResourceTypeName;info.nVersion=1;
        info.nSchemaVersion=1;info.nMinimumReaderVersion=1;info.Persistence=iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;info.Dependencies=dependencies;
        const Ref ref{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(ref.URL,std::move(value),info);
        if(const auto previous=cache.find(slot);previous!=cache.end())scene.Resources().DiscardRuntimeResource(previous->second.second.URL);
        cache[slot]={key,ref};return ref;
    }
    Ref PublishMachineWireResource(Scene& scene,const ID& setupID,const Text& kind,const Text& key,const Map& body,const std::vector<Ref>& dependencies){
        return PublishJsonQueryResource(scene,setupID,kind,key,body,dependencies,"ICMJ",kind=="collision"?"轨迹碰撞检查":kind=="program"?"机床运动指令":"机床轴运动计划");
    }
    Ref PublishQueryDocument(Scene& scene,const ID& setupID,const Text& scope,const Text& revision,const Map& body){
        return PublishJsonQueryResource(scene,setupID,"query:"+scope,revision,body,{},"ICJD","查询详情");
    }
    void DiscardQueryDocument(Scene& scene,const ID& setupID,const Text& scope){
        auto& cache=Runtime(scene)->MachineWireResources;const auto key=iCAX::Data::to_string(setupID)+":query:"+scope;
        if(const auto found=cache.find(key);found!=cache.end()){scene.Resources().DiscardRuntimeResource(found->second.second.URL);cache.erase(found);}
    }
    Map ProjectMachinePlanReport(Scene& scene,const ID& setupID,const Map& report,const Text& attempt){
        Map metadata;for(const auto& [key,value]:report)if(key!="frames"&&key!="inputKey")metadata.emplace(key,value);
        if(!report.contains("frames"))return metadata;
        std::vector<Ref> dependencies;Text key=attempt;
        if(report.contains("revision")){key=iCAX::Data::VariantSerializer::Serialize(report.at("revision"));}
        if(key.empty())throw std::logic_error("CAM_MACHINE_PLAN_PROJECTION_ID_REQUIRED");
        // The business report is already stripped of its private input key.
        // Keep its complete, immutable payload outside the bounded SDO queue.
        if(report.contains("inputKey"))throw std::logic_error("CAM_MACHINE_PLAN_PRIVATE_INPUT_KEY");
        const auto ref=PublishMachineWireResource(scene,setupID,dependencies.empty()?"attempt":"plan",key,report,dependencies);
        metadata["planResourceRef"]=WireRef(StoredRef(ref));return metadata;
    }
    Text MachinePlanKey(Scene& scene,const ID& id,const MachineSnapshot& machine){
        return iCAX::Data::VariantSerializer::Serialize(Map{{"algorithm",Text("COPPQuinticMotionV6NormalSideNeutralZRapids")},
            {"trajectory",SimulationCollisionKey(scene,id)},{"machine",MachineSnapshotState(machine)},
            {"positionToleranceMm",.01},{"directionToleranceRad",1e-4},{"clearanceMm",.1}});
    }
    Map MachinePlanState(Scene& scene,const ID& id,bool full=false){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);
        if(setup->GetMachineMotionPlanData().empty())return {{"validity",Text("Missing")},{"playbackAllowed",false}};
        const auto ref=setup->GetMachineMotionPlanData();
        auto body=EngineeringBody(ref);
        if(String(body,"schema")!="cam.machine-motion-plan")throw std::invalid_argument("CAM_ENGINEERING_DATA_SCHEMA_MISMATCH");
        const auto version=String(body,"schemaVersion");
        if(version=="1")return {{"validity",Text("Stale")},{"revision",EngineeringIdentity(ref)},{"playbackAllowed",false}};
        if(version!="2")throw std::invalid_argument("CAM_ENGINEERING_DATA_SCHEMA_MISMATCH");
        bool current=false;
        if(!setup->GetMachineSetup().empty()||!setup->GetHeadInstanceID().is_nil())current=String(body,"inputKey")==MachinePlanKey(scene,id,CaptureSetupMachine(scene,*setup));
        if(!current)return {{"validity",Text("Stale")},{"revision",EngineeringIdentity(setup->GetMachineMotionPlanData())},{"playbackAllowed",false}};
        body.erase("inputKey");body["validity"]=Text("Current");body["revision"]=EngineeringIdentity(setup->GetMachineMotionPlanData());
        if(full)return body;
        return {{"validity",Text("Current")},{"revision",EngineeringIdentity(setup->GetMachineMotionPlanData())},
            {"status",body.at("status")},{"playbackAllowed",body.at("playbackAllowed")},
            {"sampleCount",body.at("sampleCount")},{"durationSeconds",body.at("durationSeconds")},
            {"provider",body.at("provider")},{"interpolationCoverage",body.at("interpolationCoverage")},
            {"timingQualification",body.at("timingQualification")},{"machineReady",false}};
    }
    std::shared_ptr<MachinePlanSnapshot> CaptureMachinePlan(Scene& scene,const ID& id){
        RequireMachiningSetup(scene,id);
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);
        if(setup->GetMachineSetup().empty()&&setup->GetHeadInstanceID().is_nil())throw std::invalid_argument("CAM_HEAD_INSTANCE_REQUIRED: 请先设置切割头。");
        auto input=std::make_shared<MachinePlanSnapshot>();input->Machine=CaptureSetupMachine(scene,*setup);
        input->State=MachineSnapshotState(input->Machine);input->InputKey=MachinePlanKey(scene,id,input->Machine);
        input->State["nozzleStandOffMm"]=RapidSettings(*setup).at("followHeightMm");
        input->Trajectory=CaptureSimulationCollision(scene,id,true);
        for(auto& cut:CaptureGeneration(scene,id).Cuts)input->Sources.emplace(cut.IDText,std::move(cut));
        for(const auto& edge:input->Trajectory->Query.Segments)if(!edge.Connected)throw std::invalid_argument("CAM_MACHINE_PATH_DISCONNECTED: 请先补齐空移路径。");
        if(input->Trajectory->Frames.empty()||input->Trajectory->Frames.size()>100000)throw std::invalid_argument("CAM_MACHINE_SAMPLE_LIMIT");
        input->Collision=PrepareMachineCollision(scene,input->Machine,*input->Trajectory);
        input->Dependencies=input->Trajectory->Dependencies;
        input->Dependencies.insert(input->Dependencies.end(),input->Machine.ToolGeometryDependencies.begin(),input->Machine.ToolGeometryDependencies.end());
        AppendGeneratedDataDependencies(scene,id,input->Dependencies);
        return input;
    }
    std::vector<std::size_t> MachinePlanKeyposes(const SimulationCollisionSnapshot& trajectory){
        namespace R=iCAX::GeometryServices::RigidMotion;std::vector<Motion::KeyposeReference> samples;const auto& frames=trajectory.Frames;samples.reserve(frames.size());
        for(const auto& value:frames){const auto f=value.To<Map>();const auto rotation=f.at("rotation").To<Array>();R::Quaternion q{};for(int k=0;k<4;++k)q[k]=rotation[k].To<double>();
            samples.push_back({RegistrationPoint(f.at("position")),R::Rotate(q,{0,0,-1}),String(f,"role")=="Rapid"||!f.contains("sourceSample")});}
        for(const auto index:trajectory.KeypointFrames)if(index<samples.size())samples[index].Mandatory=true;
        for(std::size_t i=1;i<frames.size();++i){const auto a=frames[i-1].To<Map>(),b=frames[i].To<Map>();bool boundary=String(a,"role")!=String(b,"role")||String(a,"intentId")!=String(b,"intentId");
            if(a.contains("sourceSample")&&b.contains("sourceSample")){const auto sa=a.at("sourceSample").To<Map>(),sb=b.at("sourceSample").To<Map>();boundary|=sa.at("segmentIndex")!=sb.at("segmentIndex");}
            else boundary=true;
            if(boundary)samples[i-1].Mandatory=samples[i].Mandatory=true;
        }
        return Motion::SelectKeyposes(samples);
    }
    void MachinePlanningProgress(const std::shared_ptr<CollisionRepairProgress>& progress,const Text& stage,std::size_t at,std::size_t count){
        if(!progress)return;std::lock_guard lock(progress->Mutex);
        progress->State={{"stage",stage},{"currentStation",std::to_string(at)},{"totalStations",std::to_string(count)}};
    }
    std::shared_ptr<Map> ComputeMachinePlan(const MachinePlanSnapshot& input,
        const iCAX::GeometryServices::IMachineMotionPlanningService& service,
        const std::shared_ptr<std::atomic_bool>& cancelled,const std::shared_ptr<CollisionRepairProgress>& progress){
        const auto& machine=input.Machine;MM::Query query;query.Machine.SourceDigest=String(input.State,"modelKey");
        query.TaskDefinition.ToolFrameID=machine.Nodes.at(machine.Target.Tool).ID;
        query.TaskDefinition.ReferenceFrameID=machine.Nodes.at(machine.Target.Support).ID;
        query.TaskDefinition.TCP=machine.Target.TCP;query.TaskDefinition.Beam=machine.Target.Beam;
        const auto budget=MachinePlanPolicy::ForWorkload(input.Trajectory->Frames.size(),input.Collision.Pairs.size());
        query.MaximumSeconds=budget.MaximumSeconds;query.MaximumVerificationSamples=budget.MaximumVerificationSamples;
        if(input.PlacementPreview)query.MaximumSeconds=std::min(query.MaximumSeconds,input.PreviewMaximumSeconds);
        // The trajectory solver reserves 90% of the contour tolerance for
        // interpolation. Give numerical arm IK time to reach that tighter
        // internal accuracy; the unchanged overall workload deadline still wins.
        query.IKTimeMsPerTarget=500;query.IKSeedsPerTarget=2;query.IKCandidatesPerTarget=2;
        query.PositionToleranceMm=.01;query.DirectionToleranceRad=1e-4;
        for(const auto& n:machine.Nodes){
            MK::Node node;node.ID=n.ID;node.Parent=n.Parent;node.Origin=n.Origin;node.Axis=n.Axis;
            node.Type=n.Type=="fixed"?MK::JointType::Fixed:n.Type=="prismatic"?MK::JointType::Prismatic:n.Type=="continuous"?MK::JointType::Continuous:MK::JointType::Revolute;
            node.Lower=n.Lower;node.Upper=n.Upper;node.ReferenceValue=n.Type=="fixed"?0:n.Position;
            query.Machine.Nodes.push_back(node);query.InitialPositions.push_back(n.Type=="fixed"?0:n.Position);
            if(n.Type!="fixed"&&(!std::isfinite(n.Velocity)||n.Velocity<=0))throw std::invalid_argument("CAM_MACHINE_VELOCITY_REQUIRED: 运动轴缺少有效速度上限。");
            query.MaximumVelocities.push_back(n.Type=="fixed"?0:n.Velocity);query.MaximumAxisSteps.push_back(n.Type=="fixed"?0:n.Upper-n.Lower);
            double acceleration=0,jerk=0;if(n.Type!="fixed"){
                acceleration=jerk=std::numeric_limits<double>::infinity();
                const auto axes=machine.Postprocessor.contains("axes")?machine.Postprocessor.at("axes").To<Array>():Array{};
                for(const auto& value:axes){const auto axis=value.To<Map>();if(String(axis,"jointId")!=n.ID)continue;
                    if(axis.contains("maxAcceleration"))acceleration=FiniteNumber(axis.at("maxAcceleration"),"轴加速度无效。");
                    if(axis.contains("maxJerk"))jerk=FiniteNumber(axis.at("maxJerk"),"轴加加速度无效。");break;}
                if(acceleration<=0||jerk<=0)throw std::invalid_argument("CAM_MACHINE_DYNAMICS_INVALID: 已填写的轴加速度与加加速度上限必须大于零，留空表示不限。");
                if(n.Type!="prismatic"){acceleration*=std::numbers::pi/180.;jerk*=std::numbers::pi/180.;}
            }query.MaximumAccelerations.push_back(acceleration);query.MaximumJerks.push_back(jerk);
        }
        namespace R=iCAX::GeometryServices::RigidMotion;
        const auto keyposes=MachinePlanKeyposes(*input.Trajectory);
        for(const auto sourceIndex:keyposes){const auto frame=input.Trajectory->Frames.at(sourceIndex).To<Map>();MM::Station station;
            station.ID=String(frame,"intentId");station.Role=String(frame,"role");
            station.Goal.PositionMm=Motion::Transform(machine.Placement,RegistrationPoint(frame.at("position")));
            const auto raw=frame.at("rotation").To<Array>();R::Quaternion rotation{};for(int k=0;k<4;++k)rotation[k]=raw[k].To<double>();
            station.Goal.Direction=Motion::Transform(machine.Placement,R::Rotate(rotation,{0,0,-1}),true);
            const auto feedKey=station.Role=="Rapid"?"travelFeedMmMin":"cutFeedMmMin";
            station.FeedMmPerSecond=machine.Postprocessor.contains(feedKey)?FiniteNumber(machine.Postprocessor.at(feedKey),"CAM_MACHINE_FEED_INVALID")/60.:std::numeric_limits<double>::infinity();
            query.Stations.push_back(std::move(station));
        }
        const auto fixedRapid=[&](std::size_t i){const auto frame=input.Trajectory->Frames.at(keyposes.at(i)).To<Map>();return frame.contains("rapidDirectionLocked")&&frame.at("rapidDirectionLocked").To<bool>();};
        for(std::size_t i=0;i+1<query.Stations.size();++i)query.Stations[i].StopAfter=fixedRapid(i)||fixedRapid(i+1)||query.Stations[i].ID!=query.Stations[i+1].ID||((query.Stations[i].Role=="Rapid")!=(query.Stations[i+1].Role=="Rapid"));
        // Only interior rapid stations may choose new signed directions. The
        // authored TCP route and both cut-connection endpoint poses stay fixed.
        for(std::size_t i=1;i+1<query.Stations.size();++i)if(!fixedRapid(i)&&query.Stations[i-1].Role=="Rapid"&&query.Stations[i].Role=="Rapid"&&query.Stations[i+1].Role=="Rapid"){
            query.Stations[i].AlternativeDirections.push_back(query.Stations[i].Goal.Direction);
            for(const auto& axis:std::array<MK::Vector,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}})
                query.Stations[i].AlternativeDirections.push_back(Motion::Transform(machine.Placement,axis,true));
        }
        const auto stop=[&]{return cancelled->load();};Map lastRejected;std::size_t checkedEdges=0;
        // Frozen geometry belongs to this plan. Reuse exact relative-pose
        // distance proofs across graph candidates, not motion certificates.
        MachineCollisionQueryCache collisionCache;
        const auto validate=[&](std::size_t edge,const std::vector<double>& a,const std::vector<double>& b){
            if(stop())return MM::Validation{MM::Verdict::Unresolved,"Cancelled"};
            MachinePlanningProgress(progress,"CheckingMotion",edge==std::numeric_limits<std::size_t>::max()?0:edge,query.Stations.size());
            Map result{{"status",Text("Ready")},{"frames",Array{Map{{"joints",MachineNumbers(b)}}}}};
            CheckPreparedMachineCollision(machine,result,input.Collision,stop,1000,&a,&collisionCache);++checkedEdges;
            if(String(result,"collisionStatus")=="Clear")return MM::Validation{MM::Verdict::Accepted,{}};
            const bool initial=edge==std::numeric_limits<std::size_t>::max(),state=a==b;
            const Text label=initial?"当前初始轴姿态":edge==0&&!state?"初始接近运动":
                "关键姿态 "+std::to_string(edge+1)+(state?" 的轴姿态":" 的接近运动");
            auto reason=String(result,"reason");const auto separator=reason.find("：");
            result["reason"]=label+(separator==Text::npos?"："+reason:reason.substr(separator));
            result["sourceStation"]=initial?Text("Initial"):std::to_string(edge);result["checkKind"]=Text(state?"State":"Motion");
            if(!initial&&edge<query.Stations.size()){
                result["referenceSample"]=std::to_string(keyposes.at(edge));
                result["sourceIntentId"]=query.Stations[edge].ID;result["sourceRole"]=query.Stations[edge].Role;
                result["sourcePositionMm"]=MachineNumbers(query.Stations[edge].Goal.PositionMm);
            }
            lastRejected=result;
            return MM::Validation{String(result,"collisionStatus")=="Blocked"?MM::Verdict::Rejected:MM::Verdict::Unresolved,String(result,"reason")};
        };
        query.CheckState=[&](auto station,const auto& q){return validate(station,q,q);};
        query.CheckMotion=validate;
        query.CheckTrajectory=[&](const MM::AxisSegment& segment){return CheckPreparedPolynomialCollision(machine,segment,input.Collision,stop);};
        std::map<Text,std::unique_ptr<CFiveAxisIntentPoseField>> poseFields;
        for(const auto& [id,cut]:input.Sources)poseFields.emplace(id,std::make_unique<CFiveAxisIntentPoseField>(*cut.Context,cut.IntentPath.Value));
        query.ReferenceAt=[&](std::size_t edge,double fraction,const MK::Vector& first,const MK::Vector& second){
            MM::Reference reference;const auto& a=query.Stations.at(edge-1);const auto& b=query.Stations.at(edge);
            const auto fa=input.Trajectory->Frames.at(keyposes.at(edge-1)).To<Map>(),fb=input.Trajectory->Frames.at(keyposes.at(edge)).To<Map>();
            if(a.Role!="Rapid"&&b.Role!="Rapid"&&a.ID==b.ID&&fa.contains("sourceSample")&&fb.contains("sourceSample")&&input.Sources.contains(a.ID)){
                const auto sa=fa.at("sourceSample").To<Map>(),sb=fb.at("sourceSample").To<Map>();
                if(sa.at("segmentIndex")==sb.at("segmentIndex")){
                    const auto& cut=input.Sources.at(a.ID);const auto index=std::stoull(String(sa,"segmentIndex"));const auto& segment=cut.IntentPath.Value.Geometry.Segments.at(index);
                    const double u=FiniteNumber(sa.at("nativeParameter"),"CAM_MACHINE_REFERENCE_INVALID")+(FiniteNumber(sb.at("nativeParameter"),"CAM_MACHINE_REFERENCE_INVALID")-FiniteNumber(sa.at("nativeParameter"),"CAM_MACHINE_REFERENCE_INVALID"))*fraction;
                    iCAX::GeometryData::OffsetCurvePiece3 piece;if(const auto* curve=std::get_if<iCAX::GeometryData::CurveSegment3>(&segment.Curve))piece.Geometry=*curve;else if(const auto* offset=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve))piece.Geometry=*offset;else throw std::invalid_argument("CAM_MACHINE_EXACT_REFERENCE_UNSUPPORTED");
                    iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(cut.Context->Offset(),stop);const auto evaluated=evaluator.At(piece,u);const auto rawAxis=poseFields.at(a.ID)->At(index,u);
                    const auto placement=Motion::Multiply(machine.Placement,RigidMatrix(cut.Transform));const auto axis=Motion::Unit(Motion::Transform(placement,{rawAxis.X,rawAxis.Y,rawAxis.Z},true));
                    reference.Goal.PositionMm=Motion::Transform(placement,{evaluated.OffsetPoint.X,evaluated.OffsetPoint.Y,evaluated.OffsetPoint.Z});
                    const double height=FiniteNumber(input.State.at("nozzleStandOffMm"),"CAM_MACHINE_REFERENCE_HEIGHT_INVALID");for(int k=0;k<3;++k){reference.Goal.PositionMm[k]+=axis[k]*height;reference.Goal.Direction[k]=-axis[k];}return reference;
                }
            }
            for(int k=0;k<3;++k)reference.Goal.PositionMm[k]=a.Goal.PositionMm[k]+(b.Goal.PositionMm[k]-a.Goal.PositionMm[k])*fraction;
            reference.Goal.Direction=R::Rotate(R::Interpolate({0,0,0,1},R::Between(first,second),fraction),first);
            reference.CheckDirection=a.Role!="Rapid"&&b.Role!="Rapid";return reference;
        };
        query.Progress=[&](const auto& stage,auto at,auto count){MachinePlanningProgress(progress,stage,at,count);};
        MachinePlanningProgress(progress,"Solving",0,query.Stations.size());
        const auto solved=service.Plan(query,stop);auto output=std::make_shared<Map>(input.State);auto& report=*output;
        Array axisLimits;for(std::size_t i=0;i<machine.Nodes.size();++i)if(machine.Nodes[i].Type!="fixed")axisLimits.emplace_back(Map{{"jointId",machine.Nodes[i].ID},{"velocity",query.MaximumVelocities[i]},
            {"acceleration",std::isfinite(query.MaximumAccelerations[i])?Variant(query.MaximumAccelerations[i]):Variant(Text("Unbounded"))},{"jerk",std::isfinite(query.MaximumJerks[i])?Variant(query.MaximumJerks[i]):Variant(Text("Unbounded"))}});
        report["axisLimits"]=axisLimits;report["limitUnits"]=Text("mm/radian and seconds; Unbounded means the corresponding constraint is omitted");
        const bool ready=solved.State==MM::Status::Solved;
        const bool collisionRejected=!lastRejected.empty()&&MachinePlanPolicy::IsAuthoritativeCollision(solved,
            String(lastRejected,"collisionStatus"),String(lastRejected,"reason"),String(lastRejected,"sourceStation"));
        report["schema"]=Text("cam.machine-motion-plan");report["schemaVersion"]=Text("2");report["inputKey"]=input.InputKey;
        report["interpolation"]=Text("QuinticNormalized");report["dynamics"]=Map{{"maximumVelocityRatio",solved.MaximumVelocityRatio},{"maximumAccelerationRatio",solved.MaximumAccelerationRatio},{"maximumJerkRatio",solved.MaximumJerkRatio},{"approximateSolver",solved.ApproximateSolver},{"stopKnots",std::to_string(solved.StopKnots)},{"blendedKnots",std::to_string(solved.BlendedKnots)},{"verification",Text("ConservativePolynomialBounds")}};
        report["status"]=Text(ready?"Ready":solved.State==MM::Status::NoCandidate?"Unresolved":solved.State==MM::Status::Rejected?"Blocked":"Unchecked");
        report["kinematicsStatus"]=Text(ready?"Ready":"Unresolved");report["collisionStatus"]=Text(ready?"Clear":collisionRejected?"Blocked":"Unchecked");
        report["reason"]=Text(ready?"已完成轴轨迹规划及连续碰撞检查。":solved.Diagnostic);
        report["planningBudget"]=Map{{"policy",Text("SourceStationsAndCollisionPairsV1")},{"maximumSeconds",query.MaximumSeconds},
            {"maximumVerificationSamples",std::to_string(query.MaximumVerificationSamples)},{"sourceStationCount",std::to_string(input.Trajectory->Frames.size())},{"keyposeCount",std::to_string(query.Stations.size())},
            {"collisionPairCount",std::to_string(input.Collision.Pairs.size())},{"startupSeconds",60.},{"secondsPerSourceStation",.025},
            {"secondsPerSourceStationPair",.0005},{"hardMaximumSeconds",600.},{"verificationSamplesPerSourceStation",Text("64")},{"hardMaximumVerificationSamples",Text("2000000")}};
        report["referenceSampleCount"]=std::to_string(input.Trajectory->Frames.size());report["keyposeCount"]=std::to_string(keyposes.size());
        report["keyposePolicy"]=Map{{"selection",Text("NativeSegmentBoundaries;UserPoseAnchors;RapidCorners;AdaptiveFKRefinement")},{"seedPositionMm",5.},{"seedDirectionRad",.08726646259971647},{"maximumSeedSpanMm",50.},{"acceptancePositionMm",query.PositionToleranceMm},{"acceptanceDirectionRad",query.DirectionToleranceRad}};
        report["diagnostic"]=solved.Diagnostic;report["provider"]=solved.Provider;report["candidateCoverage"]=solved.Coverage;
        report["interpolationCoverage"]=solved.InterpolationCoverage;report["timingQualification"]=solved.TimingQualification;
        report["machineReady"]=false;report["productionQualified"]=false;report["playbackAllowed"]=ready;
        if(input.PlacementPreview){report["placementPreview"]=true;report["validity"]=Text("Preview");}
        report["positionErrorMm"]=solved.MaximumPositionErrorMm;report["directionError"]=solved.MaximumDirectionErrorRad;
        report["planningMilliseconds"]=solved.ElapsedMs;report["ikCalls"]=std::to_string(solved.IKCalls);report["candidateCount"]=std::to_string(solved.CandidateCount);
        report["failedStation"]=solved.FailedStation==std::numeric_limits<std::size_t>::max()?Text{}:std::to_string(solved.FailedStation);
        report["failedEdge"]=solved.FailedEdge==std::numeric_limits<std::size_t>::max()?Text{}:std::to_string(solved.FailedEdge);
        if(!ready&&solved.FailedStation<query.Stations.size()){
            const auto& failed=query.Stations[solved.FailedStation];
            report["failedTarget"]=Map{{"station",std::to_string(solved.FailedStation)},{"referenceSample",std::to_string(keyposes.at(solved.FailedStation))},{"intentId",failed.ID},{"role",failed.Role},
                {"positionMm",MachineNumbers(failed.Goal.PositionMm)},{"beamDirection",MachineNumbers(failed.Goal.Direction)}};
            if(solved.Diagnostic=="No FK-accepted candidate at the station in the finite IK seed/time budget; unreachability is not established.")
                report["reason"]=Text("第 "+std::to_string(solved.FailedStation+1)+" 个关键姿态（原刀路点 "+std::to_string(keyposes.at(solved.FailedStation)+1)+(failed.Role=="Rapid"?"，空移":"，切割")+"）在本次逆解搜索限额内未找到满足精度的轴姿态；尚不能判定为不可达。未生成可播放的运动计划。");
        }
        report["checkedMotionCount"]=std::to_string(checkedEdges);report["verificationSamples"]=std::to_string(solved.VerificationSamples);
        report["candidateSearchBudgetExhausted"]=solved.CandidateSearchBudgetExhausted;
        report["rapidOrientationCoverage"]=Text("FiniteFreeDirectionCandidates; connection endpoints fixed");
        Array frames;double elapsed=0,previousTime=0;
        const auto outgoingProcess=[&](std::size_t index){if(index+1>=input.Trajectory->Frames.size())return false;
            const auto a=input.Trajectory->Frames[index].To<Map>(),b=input.Trajectory->Frames[index+1].To<Map>();
            return String(a,"role")!="Rapid"&&String(b,"role")!="Rapid"&&String(a,"intentId")==String(b,"intentId");};
        for(const auto& sample:solved.Samples){const bool initial=sample.SourceStation==std::numeric_limits<std::size_t>::max();
            const bool approaching=initial||(sample.SourceStation==0&&sample.SourceFraction<1);
            const auto sourceIndex=initial?std::size_t(0):keyposes.at(sample.SourceStation);
            Map frame=initial?Map{{"role",Text("Approach")},{"laser",false}}:input.Trajectory->Frames.at(sourceIndex).To<Map>();
            // Frozen native anchors belong to reference evaluation, not the
            // standard-JSON motion resource (which cannot carry typed UUIDs).
            frame.erase("sourceSample");
            if(approaching)frame["role"]=Text("Approach");
            frame["laser"]=!approaching&&outgoingProcess(sample.SourceFraction<1?keyposes.at(sample.SourceStation-1):sourceIndex);
            if(frame.contains("rotation")){frame["camRotation"]=frame.at("rotation");frame.erase("rotation");}
            const auto world=Motion::Forward(machine.Nodes,sample.Positions);
            frame["actualToolPose"]=MachineNumbers(Motion::Multiply(Motion::Inverse(world.at(machine.Target.Support)),world.at(machine.Target.Tool)));
            frame["actualBeamDirection"]=MachineNumbers(sample.Direction);
            const double duration=frames.empty()?0:sample.TimeSeconds-previousTime;elapsed+=duration;previousTime=sample.TimeSeconds;
            frame["joints"]=MachineNumbers(sample.Positions);frame["position"]=MachineNumbers(Motion::Transform(Motion::Inverse(machine.Placement),sample.TCP));
            frame["durationSeconds"]=duration;frame["timeSeconds"]=elapsed;frame["sourceStation"]=initial?Text("Initial"):std::to_string(sourceIndex);frame["keyposeStation"]=initial?Text("Initial"):std::to_string(sample.SourceStation);
            frame["velocities"]=MachineNumbers(sample.Velocities);frame["accelerations"]=MachineNumbers(sample.Accelerations);
            if(sample.SegmentIndex!=std::numeric_limits<std::size_t>::max()){
                const auto& segment=solved.Segments.at(sample.SegmentIndex);Array coefficients;for(const auto& axis:segment.Coefficients)coefficients.emplace_back(MachineNumbers(axis));
                frame["motion"]=Map{{"interpolation",Text("QuinticNormalized")},{"coefficients",coefficients},{"durationSeconds",segment.DurationSeconds}};
            }
            frame["sourceFraction"]=sample.SourceFraction;frame["positionErrorMm"]=sample.PositionErrorMm;frame["directionErrorRad"]=sample.DirectionErrorRad;
            frame["seedSource"]=sample.SeedSource;frame["branchKey"]=sample.BranchKey;frames.emplace_back(std::move(frame));
        }
        report["frames"]=frames;report["sampleCount"]=std::to_string(frames.size());report["sourceStationCount"]=std::to_string(input.Trajectory->Frames.size());report["durationSeconds"]=elapsed;
        if(collisionRejected)report["rejectedMotion"]=lastRejected;
        else if(!ready&&!lastRejected.empty())report["lastRejectedCandidate"]=lastRejected;
        return output;
    }
    void PublishMachinePlan(Scene& scene,const ID& setupID,const MachinePlanSnapshot& input,Map& report){
        if(String(report,"status")!="Ready"||!report.at("playbackAllowed").To<bool>())return;
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        if(input.InputKey!=MachinePlanKey(scene,setupID,CaptureSetupMachine(scene,*setup)))throw std::runtime_error("CAM_MACHINE_PLAN_SUPERSEDED");
        const auto data=EngineeringData(report);
        Transaction(scene.Database(),"更新机床运动计划",[&](auto& tx){tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"MachineMotionPlanData",data}});},false);
        report.erase("inputKey");report["revision"]=EngineeringIdentity(data);report["validity"]=Text("Current");
    }
    void FinishMachinePlan(Scene& scene,SceneRuntime& runtime,ImportTask& task){
        try{
            const auto result=task.MachinePlanWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence)task.Status="Superseded";
            else{if(task.MachinePlanInput->PlacementPreview&&task.MachinePlanInput->InputKey!=MachinePlanKey(scene,task.SetupID,CaptureMachine(scene,Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID)->GetMachineSetup())))throw std::runtime_error("CAM_MACHINE_PLAN_SUPERSEDED");auto report=*result;MachinePlanningProgress(task.MachinePlanProgress,"Saving",task.MachinePlanInput->Trajectory->Frames.size(),task.MachinePlanInput->Trajectory->Frames.size());
                if(!task.MachinePlanInput->PlacementPreview)PublishMachinePlan(scene,task.SetupID,*task.MachinePlanInput,report);report.erase("inputKey");task.MachinePlanModel=std::make_shared<const Map>(std::move(report));
                task.Result={{"setupId",iCAX::Data::to_string(task.SetupID)},{"status",task.MachinePlanModel->at("status")},{"playbackAllowed",task.MachinePlanModel->at("playbackAllowed")}};task.Status="Completed";}
        }catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
        catch(const std::exception& error){task.Status=task.Cancelled->load()?"Cancelled":task.Sequence!=runtime.Sequence?"Superseded":"Failed";if(task.Status=="Failed")task.Error=error.what();}
        task.MachinePlanWorker={};task.MachinePlanInput.reset();
    }
}
Map ReadMachineMotionPlan(Scene& scene,const Map& request){CutFields(request,{"setupId"});return MachinePlanState(scene,Identifier(request,"setupId"),true);}
Map ReadMachineMotionPlanProjection(Scene& scene,const Map& request){return ProjectMachinePlanReport(scene,Identifier(request,"setupId"),ReadMachineMotionPlan(scene,request));}
Map ReadMachineMotionAttempt(Scene& scene,const Map& request){
    CutFields(request,{"epoch","taskId"});const auto runtime=Runtime(scene);if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto found=runtime->Tasks.find(String(request,"taskId"));if(found==runtime->Tasks.end()||found->second.Kind!="MachinePlan")throw std::invalid_argument("CAM_MACHINE_PLAN_TASK_NOT_FOUND");
    const auto& task=found->second;if(task.Status!="Completed"||!task.MachinePlanModel)throw std::invalid_argument("CAM_MACHINE_PLAN_SUPERSEDED");
    return ProjectMachinePlanReport(scene,task.SetupID,*task.MachinePlanModel,task.TaskID);
}
Map PlanMachineMotion(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","previewConfiguration","previewMaximumSeconds"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("MachinePlan:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineMotionPlanningService>();if(!service)throw std::runtime_error("CAM_MACHINE_MOTION_SERVICE_UNAVAILABLE");
    ImportTask task;task.Kind="MachinePlan";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");
    task.MachinePlanInput=CaptureMachinePlan(scene,task.SetupID);task.MachinePlanProgress=std::make_shared<CollisionRepairProgress>();
    if(request.contains("previewConfiguration")){
        auto configuration=MachineBoxConfiguration(request.at("previewConfiguration").To<Map>());const auto current=task.MachinePlanInput->Machine.Configuration;
        auto unchanged=configuration;unchanged["placement"]=current.at("placement");
        if(unchanged!=current)throw std::invalid_argument("CAM_MACHINE_PREVIEW_PLACEMENT_ONLY: 摆放检查只能整体移动工件与工装，不能修改刀具、标定或检测范围。");
        auto candidate=CaptureMachine(scene,configuration);
        ValidateMachinePlacement(scene,task.SetupID,candidate,task.MachinePlanInput->Machine.Placement);
        auto preview=std::make_shared<MachinePlanSnapshot>(*task.MachinePlanInput);preview->PlacementPreview=true;
        if(request.contains("previewMaximumSeconds")){const auto seconds=FiniteNumber(request.at("previewMaximumSeconds"),"CAM_MACHINE_PREVIEW_BUDGET_INVALID");if(seconds<=0||seconds>600)throw std::invalid_argument("CAM_MACHINE_PREVIEW_BUDGET_INVALID");preview->PreviewMaximumSeconds=seconds;}
        preview->Machine=std::move(candidate);preview->State=MachineSnapshotState(preview->Machine);
        preview->State["nozzleStandOffMm"]=RapidSettings(*Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID)).at("followHeightMm");
        preview->Collision=PrepareMachineCollision(scene,preview->Machine,*preview->Trajectory);task.MachinePlanInput=std::move(preview);
    }else if(request.contains("previewMaximumSeconds"))throw std::invalid_argument("CAM_MACHINE_PREVIEW_CONFIGURATION_REQUIRED");
    task.MachinePlanWorker=iCAX::Tasks::Run([service,input=task.MachinePlanInput,cancelled=task.Cancelled,progress=task.MachinePlanProgress]{return ComputeMachinePlan(*input,*service,cancelled,progress);});
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
Map BuildMachineSimulation(Scene& scene,const Map& request){
    CutFields(request,{"expected","setupId"});CheckExpected(*Runtime(scene),request);SetupIdle(scene);
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::IMachineMotionPlanningService>();if(!service)throw std::runtime_error("CAM_MACHINE_MOTION_SERVICE_UNAVAILABLE");
    const auto id=Identifier(request,"setupId");const auto input=CaptureMachinePlan(scene,id);
    auto report=*ComputeMachinePlan(*input,*service,std::make_shared<std::atomic_bool>(false),{});PublishMachinePlan(scene,id,*input,report);report.erase("inputKey");return report;
}
Map BuildMachineSimulationProjection(Scene& scene,const Map& request){return ProjectMachinePlanReport(scene,Identifier(request,"setupId"),BuildMachineSimulation(scene,request),iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));}
