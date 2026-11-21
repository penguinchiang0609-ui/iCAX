// Search on frozen CAM inputs, then commit only the selected keypoint controls.
// No candidate or failed search changes the repository or a neighboring path.
std::shared_ptr<CollisionRepairResult> CalculateKeyposeRepair(const CollisionRepairSnapshot& input,
    const std::shared_ptr<iCAX::GeometryServices::ICurveOffset3DService>& offset,
    const std::shared_ptr<std::atomic_bool>& cancelled,const std::shared_ptr<CollisionRepairProgress>& progress){
    auto result=std::make_shared<CollisionRepairResult>();result->RepairKind="Keyposes";
    const auto& originalCut=input.Generation.Cuts.at(0);const auto& originalValue=originalCut.IntentPath.Value;
    const auto original=std::find_if(input.Operations.begin(),input.Operations.end(),[&](const auto& value){return String(value.To<Map>(),"intentId")==originalCut.IDText;})->To<Map>();
    struct Point {Map Control;RepairMath::Vector Direction,Offset;};std::vector<Point> points;
    const auto groups=ProjectFiveAxisPoseControls(originalValue);const auto controls=groups.contains("active")?groups.at("active").To<Array>():Array{};
    CFiveAxisIntentPoseField field(*originalCut.Context,originalValue);
    for(const auto& value:input.RepairLocations){const auto location=value.To<Map>();if(!location.contains("anchor"))continue;const auto anchor=location.at("anchor").To<Map>();
        const auto a=iCAX::CAM::Intent::AnchorFromData(anchor);const auto segment=std::find_if(originalValue.Geometry.Segments.begin(),originalValue.Geometry.Segments.end(),[&](const auto& s){return s.Id==a.SegmentId;});if(segment==originalValue.Geometry.Segments.end())continue;
        Map control;for(const auto& existing:controls){const auto p=existing.To<Map>();if(String(p,"kind")=="Control"&&IntentSameNativeAnchor(p.at("anchor").To<Map>(),anchor)){control=p;break;}}
        const auto direction=GenVec(field.At(segment-originalValue.Geometry.Segments.begin(),a.Parameter));
        if(control.empty())control={{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"kind",Text("Control")},{"set",Text("active")},{"boundaryPolicy",Text("Duplicate")},{"anchor",anchor},{"direction",GenPoint(direction)},{"transition",Text("ArcLength")}};
        points.push_back({control,direction,control.contains("positionOffset")?RegistrationPoint(control.at("positionOffset")):RepairMath::Vector{}});
        for(const auto& origin:input.RepairOrigins){const auto p=origin.To<Map>();if(String(p,"intentId")==originalCut.IDText&&p.contains("anchor")&&IntentSameNativeAnchor(p.at("anchor").To<Map>(),anchor)){
            points.back().Direction=RepairMath::Unit(RegistrationPoint(p.at("direction")));points.back().Offset=RegistrationPoint(p.at("positionOffset"));break;}}
        result->RepairOrigins.emplace_back(Map{{"intentId",originalCut.IDText},{"anchor",anchor},{"direction",GenPoint(points.back().Direction)},{"positionOffset",GenPoint(points.back().Offset)}});
    }
    if(points.empty())throw std::invalid_argument("CAM_REPAIR_EDITABLE_KEYPOSE_REQUIRED");
    const auto unresolved=[&](const Text& reason){result->Unresolved=Array{Map{{"reason",reason},{"intentId",originalCut.IDText},{"index",std::to_string(input.Selected)}}};return result;};
    for(const auto& point:points)if(point.Control.contains("driver"))return unresolved("CAM_REPAIR_LINKED_KEYPOINT_CONSTRAINT");
    std::set<std::size_t> targetFrames;for(const auto& value:input.RepairLocations)targetFrames.insert(static_cast<std::size_t>(Integer(value.To<Map>(),"frameIndex")));
    const auto seam=[&](const Map& anchor){if(!originalValue.Geometry.Closed)return false;
        // Projected authored controls retain their source port; segment identity
        // and native parameter locate the same point in this frozen path.
        auto a=iCAX::CAM::Intent::AnchorFromData(anchor);a.Port=originalCut.IntentPath.Port;
        const auto x=RepairAnchorPosition(originalCut,iCAX::CAM::Intent::ToData(a));return std::abs(x)<1e-10||std::abs(x-originalValue.Geometry.Segments.size())<1e-10;};
    const bool editsSeam=std::any_of(points.begin(),points.end(),[&](const auto& point){return seam(point.Control.at("anchor").To<Map>());});
    // A closed path's start and end are two occurrences of the same authored
    // seam control. Verify both local neighborhoods, without widening the
    // editable window across the unrelated middle of the path.
    if(editsSeam)for(const auto& [frame,location]:input.Collision.KeyposeLocations)
        if(String(location,"intentId")==originalCut.IDText&&String(location,"role")!="Rapid"&&location.contains("anchor")&&seam(location.at("anchor").To<Map>()))targetFrames.insert(frame);
    const auto& originalKeys=input.Collision.KinematicKeyframes;
    if(input.RepairPhase!="Limits")for(const auto frame:targetFrames){const auto found=std::find(originalKeys.begin(),originalKeys.end(),frame);if(found==originalKeys.end())throw std::runtime_error("CAM_REPAIR_KEYPOSE_SAMPLE_MISSING");
        for(const auto neighbor:{found==originalKeys.begin()?frame:*std::prev(found),std::next(found)==originalKeys.end()?frame:*std::next(found)})if(!targetFrames.contains(neighbor))
            for(const auto& value:input.Report.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>(),location=row.at("location").To<Map>();
                if(String(location,"intentId")==originalCut.IDText&&String(location,"role")!="Rapid"&&Integer(row,"frameIndex")==neighbor&&String(row,"status")!="Clear"&&(input.RepairPhase=="All"||String(row,"reason")=="CAM_HEAD_LIMIT_NO_SOLUTION"))return unresolved("CAM_REPAIR_NEIGHBOR_KEYPOINT_NOT_CLEAR");}
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);bool incomplete=false;
    std::vector<Array> orientations;for(const auto& point:points){auto values=RepairDirections(point.Direction,input.MaximumDegrees*std::numbers::pi/180);values.insert(values.begin(),GenPoint(point.Direction));orientations.push_back(std::move(values));}
    const auto attempt=[&](unsigned mask,std::size_t directionIndex,double distance){
        if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();if(std::chrono::steady_clock::now()>=deadline){incomplete=true;return false;}
        Array edits;for(std::size_t i=0;i<points.size();++i)if(mask&(1u<<i)){
            auto control=points[i].Control;control["direction"]=orientations[i].at(std::min(directionIndex,orientations[i].size()-1));
            if(control.contains("driver")){incomplete=true;return false;}
            control["positionOffset"]=GenPoint(RepairMath::Add(points[i].Offset,RepairMath::Scale(points[i].Direction,distance)));edits.emplace_back(std::move(control));
        }
        ++result->Candidates;RepairProgress(progress,distance==0?"SearchingAngles":"SearchingPosition",result->Candidates,input.MaximumDegrees);
        auto generation=input.Generation;auto& cut=generation.Cuts.at(0);auto& v=cut.IntentPath.Value;
        v.Channels["poseField"]=BuildFiveAxisIntentPoseDelta(*cut.Context,originalValue,cut.IntentPath.Port,edits,"active");
        auto channel=v.Channels.contains("poseControls")?v.Channels.at("poseControls").To<Map>():Map{{"schema",Text("fiveaxis.pose-controls/1")},{"schemaVersion",1u},{"attachment",Text("Points")},{"entries",Array{}}};auto entries=channel.at("entries").To<Array>();
        std::set<Text> replacing;for(const auto& e:edits)replacing.insert(String(e.To<Map>(),"id"));std::erase_if(entries,[&](const auto& e){return replacing.contains(String(e.To<Map>(),"id"));});entries.insert(entries.end(),edits.begin(),edits.end());channel["entries"]=std::move(entries);v.Channels["poseControls"]=channel;
        const auto generated=CalculateGeneration(generation,offset,cancelled);if(generated->Operations.size()!=1)throw std::runtime_error("CAM_REPAIR_LOCAL_GENERATION_REQUIRED");auto operation=generated->Operations.front().To<Map>();operation["order"]=original.at("order");if(original.contains("simulationStartFrame"))operation["simulationStartFrame"]=original.at("simulationStartFrame");
        auto local=input;local.Generation=generation;const auto motion=RepairTrajectory(local,Array{operation},{});
        auto candidate=input.Collision;candidate.Frames=motion.Frames;candidate.Query=motion.Query;candidate.FullSegmentCount=motion.Query.Segments.size();candidate.KeypointFrames.clear();candidate.KeypointSegments.clear();candidate.SegmentIndices.clear();candidate.KinematicKeyframes.clear();candidate.KeyposeLocations.clear();
        const auto anchors=RepairFieldStations(cut);std::set<std::size_t> selected;
        for(std::size_t i=0;i<motion.Samples.size();++i){const auto& sample=motion.Samples[i];bool key=i==0||i+1==motion.Samples.size()||i&&motion.Samples[i-1].Part!=sample.Part||i+1<motion.Samples.size()&&motion.Samples[i+1].Part!=sample.Part;
            if(!sample.Anchor.empty())for(const auto& a:anchors)key|=IntentSameNativeAnchor(sample.Anchor,a.To<Map>());
            if(!key)continue;candidate.KinematicKeyframes.push_back(i);Map location{{"frameIndex",std::to_string(i)},{"intentId",sample.Intent},{"order",operation.at("order")},{"role",sample.Role},{"label",Text(i==0?"起点":i+1==motion.Samples.size()?"终点":"关键点")}};
            if(!sample.Anchor.empty()){location["anchor"]=sample.Anchor;if(editsSeam&&seam(sample.Anchor))selected.insert(i);for(const auto& target:input.RepairLocations)if(target.To<Map>().contains("anchor")&&IntentSameNativeAnchor(sample.Anchor,target.To<Map>().at("anchor").To<Map>()))selected.insert(i);}
            candidate.KeyposeLocations[i]=std::move(location);
        }
        if(selected.empty())throw std::runtime_error("CAM_REPAIR_KEYPOSE_SAMPLE_MISSING");
        const auto& keys=candidate.KinematicKeyframes;std::set<std::size_t> segmentsToCheck,keysToCheck;std::vector<std::pair<double,double>> windows;
        for(const auto frame:selected){const auto at=std::lower_bound(keys.begin(),keys.end(),frame);const auto first=at==keys.begin()?frame:*std::prev(at),last=std::next(at)==keys.end()?frame:*std::next(at);
            for(auto i=first;i<last;++i)segmentsToCheck.insert(i);for(const auto i:keys)if(i>=first&&i<=last)keysToCheck.insert(i);
            if(motion.Samples[first].Anchor.empty()||motion.Samples[last].Anchor.empty()){incomplete=true;return false;}
            windows.emplace_back(RepairAnchorPosition(cut,motion.Samples[first].Anchor),RepairAnchorPosition(cut,motion.Samples[last].Anchor));}
        const auto all=candidate.Query.Segments;candidate.Query.Segments.clear();
        for(const auto i:segmentsToCheck){candidate.SegmentIndices.push_back(i);candidate.Query.Segments.push_back(all.at(i));}
        for(const auto i:keysToCheck){const auto pose=i<all.size()?all[i].Start:all.back().End;candidate.KeypointFrames.push_back(i);candidate.KeypointSegments.push_back({pose,pose,all.at(std::min(i,all.size()-1)).Clearance,true});}
        // Compare all original exterior stations by source anchor. A local
        // search must not move an unrelated portion of the same cut.
        const auto oldMotion=RepairTrajectory(input,Array{original},{});
        const auto exact=IntentExactCurve(v);iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*offset);PositionOffsets offsets(v,exact,evaluator);CFiveAxisIntentPoseField candidateField(*cut.Context,v);
        for(std::size_t i=0;i<oldMotion.Samples.size();++i){const auto& old=oldMotion.Samples[i];if(old.Anchor.empty()){incomplete=true;return false;}
            const auto coordinate=RepairAnchorPosition(cut,old.Anchor);if(std::any_of(windows.begin(),windows.end(),[&](const auto& window){return coordinate>=window.first-1e-10&&coordinate<=window.second+1e-10;}))continue;
            const auto anchor=iCAX::CAM::Intent::AnchorFromData(old.Anchor);const auto segment=std::find_if(v.Geometry.Segments.begin(),v.Geometry.Segments.end(),[&](const auto& item){return item.Id==anchor.SegmentId;});if(segment==v.Geometry.Segments.end()){incomplete=true;return false;}
            const auto index=static_cast<std::size_t>(segment-v.Geometry.Segments.begin());const auto axis=RepairMath::Unit(RapidTransform(cut.Transform,GenPoint(GenVec(candidateField.At(index,anchor.Parameter))),true));
            const auto position=RepairMath::Add(RapidTransform(cut.Transform,GenPoint(offsets.At(index,anchor.Parameter)),false),RepairMath::Scale(axis,RegistrationNumber(input.Settings.at("followHeightMm"))));
            if(RepairMath::Length(RepairMath::Sub(RegistrationPoint(oldMotion.Frames[i].To<Map>().at("position")),position))>1e-8||RepairMath::Length(RepairMath::Sub(old.Axis,axis))>1e-9){incomplete=true;return false;}
        }
        if(input.RepairPhase=="Limits"){
            candidate.Query.Segments.clear();candidate.SegmentIndices.clear();candidate.KeypointFrames.clear();candidate.KeypointSegments.clear();
            for(const auto i:selected){const auto pose=i<all.size()?all[i].Start:all.back().End;candidate.KeypointFrames.push_back(i);candidate.KeypointSegments.push_back({pose,pose,0,true});}
        }
        candidate.ConfigureFullHead(candidate);const auto stop=[&]{return cancelled->load()||std::chrono::steady_clock::now()>=deadline;};
        RepairProgress(progress,"CheckingLimits",result->Candidates,input.MaximumDegrees);
        if(!candidate.CheckHeadLimits(stop)){incomplete|=candidate.HeadLimitsUnverified;return false;}
        if(stop()){incomplete=true;return false;}
        if(input.RepairPhase!="Limits"){
        RepairProgress(progress,"CheckingCollision",result->Candidates,input.MaximumDegrees);
        const auto checkedKeys=candidate.CheckFullHead(candidate.KeypointSegments,true,{},stop,{},{});
        for(std::size_t i=0;i<checkedKeys.Segments.size();++i){const auto& row=checkedKeys.Segments[i];if(input.RepairPhase=="Collision"&&!selected.contains(candidate.KeypointFrames[i]))continue;
        if(row.Status!="Clear"){if(row.Reason.find("BUDGET")!=Text::npos||row.Reason=="CAM_HEAD_SWEEP_UNRESOLVED")incomplete=true;return false;}}
        RepairProgress(progress,"Verifying",result->Candidates,input.MaximumDegrees);
        const auto checked=candidate.CheckFullHead(candidate.Query.Segments,false,{},stop,{},{});
        for(const auto& value:candidate.InterpolationChecks){const auto row=value.To<Map>();if(input.RepairPhase=="Collision"&&String(row,"status")=="Skipped")continue;if(String(row,"status")!="Clear"){if(String(row,"status")=="Unchecked")incomplete=true;return false;}}
        }
        result->Solved=true;result->Verified=true;result->PoseEdits[originalCut.IDText]=Map{{"controls",edits}};
        result->Adjustments=Array{Map{{"intentId",originalCut.IDText},{"changedPointCount",std::to_string(edits.size())},{"translationMm",distance}}};return true;
    };
    const unsigned masks=(1u<<points.size());
    try{
        for(std::size_t direction=1;direction<orientations.front().size();++direction)for(unsigned mask=1;mask<masks;++mask)if(attempt(mask,direction,0))return result;
        if(input.MaximumTranslationMm>0)for(const double fraction:{.25,.5,.75,1.})for(std::size_t direction=0;direction<orientations.front().size();++direction)for(unsigned mask=1;mask<masks;++mask)if(attempt(mask,direction,input.MaximumTranslationMm*fraction))return result;
    }catch(const iCAX::Tasks::TaskCanceledException&){throw;}catch(const std::exception& error){if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();if(std::chrono::steady_clock::now()<deadline)throw;incomplete=true;}
    result->Unresolved=Array{Map{{"reason",Text(incomplete?"CAM_REPAIR_SEARCH_UNVERIFIED":"CAM_REPAIR_NO_SOLUTION_IN_TOLERANCE")},{"intentId",originalCut.IDText},{"index",std::to_string(input.Selected)}}};return result;
}
#include "FiveAxisRapidKeyposeRepair.inc"
Map StartKeyposeRepair(Scene& scene,SceneRuntime& runtime,const Map& request,ImportTask task){
    auto input=std::make_shared<CollisionRepairSnapshot>();input->KeyposeRepair=true;
    if(request.contains("toleranceOrigins"))input->RepairOrigins=request.at("toleranceOrigins").To<Array>();
    input->RepairPhase=request.contains("repairPhase")?String(request,"repairPhase"):Text("All");
    if(input->RepairPhase!="All"&&input->RepairPhase!="Limits"&&input->RepairPhase!="Collision")throw std::invalid_argument("CAM_REPAIR_PHASE_INVALID");
    input->MaximumDegrees=request.contains("maxDeviationDeg")?RegistrationNumber(request.at("maxDeviationDeg")):15.;
    input->MaximumTranslationMm=request.contains("maxTranslationMm")?RegistrationNumber(request.at("maxTranslationMm")):0.;
    if(!std::isfinite(input->MaximumDegrees)||input->MaximumDegrees<0||input->MaximumDegrees>180||!std::isfinite(input->MaximumTranslationMm)||input->MaximumTranslationMm<0)throw std::invalid_argument("CAM_REPAIR_TOLERANCE_INVALID");
    input->Report=SimulationCollisionState(scene,task.SetupID);if(String(input->Report,"validity")!="Current")throw std::invalid_argument("CAM_COLLISION_CURRENT_REPORT_REQUIRED");
    input->Collision=*CaptureSimulationCollision(scene,task.SetupID,true,true,true);PrepareFullHeadCollision(scene,task.SetupID,input->Collision);
    Map chosen;
    if(request.contains("keyposeFrameIndex")){for(const auto& row:input->Report.at("keyposeChecks").To<Array>())if(Integer(row.To<Map>(),"frameIndex")==Integer(request,"keyposeFrameIndex")){chosen=row.To<Map>();input->RepairLocations={chosen.at("location")};break;}}
    else if(request.contains("intervalFirstFrameIndex")){for(const auto& row:input->Report.at("interpolationChecks").To<Array>())if(Integer(row.To<Map>(),"firstFrameIndex")==Integer(request,"intervalFirstFrameIndex")){chosen=row.To<Map>();input->RepairLocations={chosen.at("first"),chosen.at("last")};break;}}
    if(chosen.empty()){
        const auto index=static_cast<std::size_t>(Integer(request,"segmentIndex"));const auto segments=input->Report.at("segments").To<Array>();if(index>=segments.size())throw std::invalid_argument("CAM_REPAIR_SELECT_ISSUE");chosen=segments[index].To<Map>();
        const auto& keys=input->Collision.KinematicKeyframes;const auto b=std::upper_bound(keys.begin(),keys.end(),index);if(b!=keys.end()&&b!=keys.begin())input->RepairLocations={input->Collision.KeyposeLocations.at(*std::prev(b)),input->Collision.KeyposeLocations.at(*b)};
    }
    if(String(chosen,"status")!="Collision"&&String(chosen,"reason")!="CAM_HEAD_LIMIT_NO_SOLUTION")throw std::invalid_argument("CAM_REPAIR_SELECT_ISSUE");input->Selected=static_cast<std::size_t>(Integer(chosen,"index"));
    if(input->RepairPhase=="Limits"&&String(chosen,"reason")!="CAM_HEAD_LIMIT_NO_SOLUTION"||input->RepairPhase=="Collision"&&String(chosen,"status")!="Collision")throw std::invalid_argument("CAM_REPAIR_PHASE_ISSUE_MISMATCH");
    if(input->RepairLocations.empty()||input->RepairLocations.size()>2)throw std::invalid_argument("CAM_REPAIR_EDITABLE_KEYPOSE_REQUIRED");
    const auto first=input->RepairLocations.front().To<Map>();const auto id=ID::from_string(String(first,"intentId")).value();RequireCutUnlocked(scene,id);
    const auto geometry=GeneratedGeometryState(scene,task.SetupID);input->Operations=geometry.at("operations").To<Array>();input->Transitions=geometry.at("transitions").To<Array>();
    const bool rapid=String(first,"role")=="Rapid";
    for(const auto& value:input->RepairLocations){const auto location=value.To<Map>();
        if(String(location,"intentId")!=String(first,"intentId")||(String(location,"role")=="Rapid")!=rapid||rapid&&(String(location,"from")!=String(first,"from")||String(location,"to")!=String(first,"to")))throw std::invalid_argument("CAM_REPAIR_SINGLE_PATH_REQUIRED");}
    if(rapid){
        RequireCutUnlocked(scene,Identifier(first,"from"));
        for(const auto& value:input->Transitions){const auto transition=value.To<Map>();if(String(transition,"from")!=String(first,"from")||String(transition,"to")!=String(first,"to"))continue;
            Map read{{"geometryRevision",geometry.at("revision")},{"from",transition.at("from")},{"to",transition.at("to")},{"policy",transition.at("policy")}};
            if(transition.contains("manualPath"))read["manualPath"]=transition.at("manualPath");input->Rapids=CaptureRapids(scene,task.SetupID,read,true,true);break;}
        if(!input->Rapids)throw std::invalid_argument("CAM_RAPID_CONNECTION_MISSING");
    }else{
        const std::set<ID> selected{id};input->Generation=CaptureGeneration(scene,task.SetupID,&selected);
        if(input->Generation.Cuts.size()!=1)throw std::invalid_argument("CAM_REPAIR_LOCAL_GENERATION_REQUIRED");
    }
    input->Settings=RapidSettings(*Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID));
    for(const auto& value:input->Operations){const auto operation=value.To<Map>();input->Placements[String(operation,"placementId")]=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(operation,"placementId"))->GetTransform();}
    task.RepairInput=input;task.RepairProgress=std::make_shared<CollisionRepairProgress>();RepairProgress(task.RepairProgress,"Preparing",0,input->MaximumDegrees);
    if(rapid){const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();task.RepairWorker=iCAX::Tasks::Run([input,service,cancelled=task.Cancelled,progress=task.RepairProgress]{return CalculateRapidKeyposeRepair(*input,service,cancelled,progress);});}
    else{const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();task.RepairWorker=iCAX::Tasks::Run([input,offset,cancelled=task.Cancelled,progress=task.RepairProgress]{return CalculateKeyposeRepair(*input,offset,cancelled,progress);});}
    const auto taskID=task.TaskID;runtime.Tasks.emplace(taskID,std::move(task));return TaskState(scene,runtime,runtime.Tasks.at(taskID));
}
void FinishKeyposeRepair(Scene& scene,SceneRuntime& runtime,ImportTask& task){
    try{const auto result=task.RepairWorker.Result();
        if(task.Cancelled->load())task.Status="Cancelled";
        else if(task.Sequence!=runtime.Sequence||task.RepairInput->Collision.InputKey!=SimulationCollisionKey(scene,task.SetupID))task.Status="Superseded";
        else if(!result->Solved){task.Status="Completed";task.Result={{"status",Text("Unresolved")},{"changed",false},{"unresolved",result->Unresolved},{"candidateCount",std::to_string(result->Candidates)}};}
        else if(result->RepairKind=="RapidKeyposes"){
            if(result->Overrides.size()!=1||!result->Verified)throw std::runtime_error("CAM_REPAIR_LOCAL_VERIFICATION_REQUIRED");
            const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID);auto overrides=setup->GetRapidOverrides();
            const auto& [pair,value]=*result->Overrides.begin();auto record=value.To<Map>();const auto policy=record.at("policy").To<Map>(),manual=record.at("manualPath").To<Map>();
            record["inputKey"]=RapidInputKey(scene,task.SetupID,record,policy,manual);overrides[pair]=std::move(record);
            RapidChange(scene,task.SetupID,{{"RapidOverrides",overrides}},"自动修正空移关键点");task.Sequence=runtime.Sequence;
            task.Status="Completed";task.Result={{"status",Text("Repaired")},{"changed",true},{"verification",Text("VerifiedCandidate")},{"geometryDeferred",false},{"changedIntentIds",Array{}},{"changedRapidCount",Text("1")},{"adjustments",result->Adjustments},{"candidateCount",std::to_string(result->Candidates)}};
        }else{
            if(result->PoseEdits.size()!=1||!result->Verified)throw std::runtime_error("CAM_REPAIR_LOCAL_VERIFICATION_REQUIRED");
            const auto& [id,edit]=*result->PoseEdits.begin();const auto pathID=ID::from_string(id).value();const auto terminal=ReadAxisTerminalPath(scene,pathID),path=ReadAxisAuthoringPath(scene,pathID);
            const auto commands=DeferredAxisCommands(scene,terminal,path,edit.To<Map>().at("controls").To<Array>(),{},ID::from_string(task.CommandID).value());
            CommitDeferredAxis(scene,commands,terminal,path,"自动修正局部关键点");task.Sequence=runtime.Sequence;
            task.Status="Completed";task.Result={{"status",Text("Repaired")},{"changed",true},{"verification",Text("VerifiedCandidate")},{"geometryDeferred",true},{"changedIntentIds",Array{id}},{"changedRapidCount",Text("0")},{"adjustments",result->Adjustments},{"candidateCount",std::to_string(result->Candidates)}};
        }
    }catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}catch(const std::exception& error){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=error.what();}
    if(task.Status=="Completed"&&!task.Result.empty()){task.Result["repairPhase"]=task.RepairInput->RepairPhase;if(task.RepairInput->RepairPhase!="All"&&task.Result.at("changed").To<bool>())task.Result["verification"]=task.RepairInput->RepairPhase=="Limits"?Text("KeyposeLimitsVerified"):Text("LocalTargetsVerified");task.Result["toleranceOrigins"]=task.RepairWorker.Result()->RepairOrigins;}
    task.RepairWorker={};task.RepairInput.reset();
}
