#include "FiveAxisCollisionRotation.inc"
struct SimulationCollisionSnapshot
{
    iCAX::GeometryServices::RigidMotionCollisionQuery Query;
    Text InputKey,Scope;
    Text SelectedIntent,SelectedFrom,SelectedTo,PathKind;
    Array Frames;
    std::size_t BaseHeadBoxCount=0;
    std::vector<Ref> Dependencies;
    std::vector<std::size_t> SegmentIndices;
    std::vector<std::size_t> KeypointFrames;
    std::vector<std::size_t> KinematicKeyframes;
    std::vector<iCAX::GeometryServices::RigidMotionSegment> KeypointSegments;
    Array PreviousSegments;
    Map PreviousReportRef;
    unsigned PreviousMaxSegmentTimeMs=0;
    std::size_t FullSegmentCount=0;
    double MaximumRotationCorrectionRad=0;
    unsigned ParallelWorkers=1;
    bool PartialTrajectory=false;
    using FullCheck=std::function<iCAX::GeometryServices::RigidMotionCollisionResult(const std::vector<iCAX::GeometryServices::RigidMotionSegment>&,bool,std::optional<std::size_t>,const std::function<bool()>&,const std::function<void(std::size_t)>&,const std::function<void(std::size_t,const iCAX::GeometryServices::RigidMotionCollisionSegment&)>&)>;
    FullCheck CheckFullHead;
    std::function<bool(const std::function<bool()>&)> CheckHeadLimits;
    bool HeadLimitsUnverified=false;
    std::function<void(SimulationCollisionSnapshot&)> ConfigureFullHead;
    Map HeadCoverage;
    Map CutRevisions;
    std::vector<Map> HeadDiagnostics;
    std::map<std::size_t,Map> KeyposeLocations;
    Array KeyposeChecks,InterpolationChecks,PreviousKeyposeChecks,PreviousInterpolationChecks;
    std::vector<std::size_t> InterpolationStarts;
};
namespace
{
    void PrepareFullHeadCollision(Scene&,const ID&,SimulationCollisionSnapshot&);
    // Each stored cut retains its existing head frame. Editing one cut must
    // not transport a new roll through every later, unchanged cut.
    Map CaptureGeneratedSimulationStartFrames(Scene& scene,const ID& id){
        namespace M=iCAX::GeometryServices::RigidMotion;
        const auto geometry=GeneratedGeometryState(scene,id);Map starts;
        if(!geometry.contains("operations"))return starts;
        M::Vector previous{0,0,1};M::Quaternion rotation{0,0,0,1};
        const auto operations=geometry.at("operations").To<Array>(),transitions=geometry.at("transitions").To<Array>();
        for(std::size_t index=0;index<operations.size();++index){const auto op=operations[index].To<Map>();
            const auto transform=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"))->GetTransform();bool seeded=false,first=true;
            if(op.contains("simulationStartFrame")){const auto seed=op.at("simulationStartFrame").To<Map>();if(seed.at("placementTransform")==Variant(transform)){
                previous=RegistrationPoint(seed.at("direction"));const auto q=seed.at("rotation").To<Array>();for(int i=0;i<4;++i)rotation[i]=RegistrationNumber(q[i]);seeded=true;}}
            for(const auto& partValue:op.at("parts").To<Array>())for(const auto& value:partValue.To<Map>().at("directions").To<Array>()){
                const auto axis=M::Unit(RapidTransform(transform,value,true));if(!(first&&seeded&&axis==previous))rotation=CollisionRotation::Step(previous,axis,rotation);previous=axis;
                if(first){starts[String(op,"intentId")]=Map{{"direction",GenPoint(axis)},{"rotation",Array{rotation[0],rotation[1],rotation[2],rotation[3]}},{"placementTransform",transform}};first=false;}}
            if(index+1==operations.size())continue;
            for(const auto& value:transitions){const auto t=value.To<Map>();if(t.at("from")!=op.at("intentId")||t.at("to")!=operations[index+1].To<Map>().at("intentId")||!HasRapidEditingGeometry(t))continue;
                for(const auto& direction:t.at("directions").To<Array>()){const auto axis=M::Unit(RegistrationPoint(direction));rotation=CollisionRotation::Step(previous,axis,rotation);previous=axis;}break;}
        }return starts;
    }
    Map RebaseSimulationCollision(Scene&,const ID&,const Map&,const SimulationCollisionSnapshot&);
    Map RebaseSimulationCollisionState(Scene&,const ID&,const Map&);
    // A formal product budget, shared by full checks and incremental playback.
    // Fast segments return immediately; difficult exact solves get one bounded
    // attempt without rebuilding obstacle indices or losing clearance caches.
    constexpr unsigned SimulationCollisionSegmentBudgetMs=5000;
    constexpr unsigned SimulationCollisionRetryBudgetMs=20000;
    Text SimulationCollisionKey(Scene& scene,const ID& id,const Map* replacement=nullptr){
        const auto s=Require<CFiveAxisSetupComponent>(scene.Database(),id);
        Map fields{{"algorithm",Text("HeadLimitsThenCollisionV13")},{"maxSegmentTimeMs",std::to_string(SimulationCollisionSegmentBudgetMs)},{"generation",GenerationInputKey(scene,id)},
            {"geometry",GeneratedDataRevision(*s)},{"order",IntentCutOrderPolicy(scene,id)},{"rapids",RapidDataRevision(*s)},
            {"overrides",s->GetRapidOverrides()},{"settings",RapidSettings(*s)},{"head",HeadState(scene,*s)},{"obstacles",FixtureInputKey(scene,id)}};
        const auto machineType=EffectiveMachineTypeValue(scene,id);if(!machineType.empty())fields["machineType"]=machineType;
        const auto assembly=OperationAssemblyTransform(scene,id);if(assembly!=Identity())fields["assemblyTransform"]=assembly;
        if(replacement)for(const auto& [key,value]:*replacement)fields[key]=value;
        return iCAX::Data::VariantSerializer::Serialize(fields);
    }
    Array CollisionHeadBoxes(const iCAX::GeometryServices::RigidMotionCollisionQuery& query,std::size_t baseCount=std::numeric_limits<std::size_t>::max()){
        Array boxes;for(std::size_t i=0;i<query.Boxes.size();++i){const auto& b=query.Boxes[i];Array axes;for(const auto& axis:b.Axes)axes.emplace_back(GenPoint(axis));
            boxes.emplace_back(Map{{"index",std::to_string(i)},{"center",GenPoint(b.Center)},{"halfExtents",GenPoint(b.HalfExtents)},{"axes",axes},{"frame",Text("CalibratedTCP")},{"source",Text(i<baseCount?"Head":"AdditionalEnvelope")}});
        }return boxes;
    }
    Map CollisionSegmentState(std::size_t index,const iCAX::GeometryServices::RigidMotionCollisionSegment& result){
        Map state{{"index",std::to_string(index)},{"status",result.Status},{"reason",result.Reason},{"obstacleId",result.Obstacle},{"fraction",result.Fraction},{"certifiedClearanceMm",result.CertifiedClearance}};
        if(result.Witness){const auto& w=*result.Witness;state["witness"]=Map{{"kind",Text(w.Containment?"Contained":w.Separation<=1e-9?"Overlap":"NearestPoints")},{"frame",Text("Setup")},
            {"headBoxIndex",std::to_string(w.BoxIndex)},{"headPoint",GenPoint(w.BodyPoint)},{"obstaclePoint",GenPoint(w.ObstaclePoint)},{"separationMm",w.Separation},{"surfaceDistanceMm",w.SurfaceDistance},{"clearanceMm",w.RequiredClearance},{"containment",w.Containment}};}
        return state;
    }
    Map SimulationCollisionState(Scene& scene,const ID& id){
        const auto s=Require<CFiveAxisSetupComponent>(scene.Database(),id);if(s->GetSimulationCheckData().empty())return {{"validity",Text("Missing")},{"status",Text("Unchecked")}};
        auto report=EngineeringBody(s->GetSimulationCheckData(),"cam.simulation-collision");
        const bool current=String(report,"inputKey")==SimulationCollisionKey(scene,id);
        if(!current)return RebaseSimulationCollisionState(scene,id,report);
        report.erase("inputKey");
        report["validity"]=Text("Current");return report;
    }
    std::shared_ptr<SimulationCollisionSnapshot> CaptureSimulationCollision(Scene& scene,const ID& id,bool collectKeypoints=false,bool allowPartial=false,bool wholeHead=false){
        namespace M=iCAX::GeometryServices::RigidMotion;
        using Pose=iCAX::GeometryServices::RigidMotionPose;
        RequireMachiningSetup(scene,id);
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);const auto generated=GeneratedGeometryState(scene,id);
        if(String(generated,"status")!="Current"&&!(allowPartial&&String(generated,"status")=="Stale"))throw std::invalid_argument("CAM_COLLISION_GEOMETRY_REQUIRED: 当前装夹的切割路径尚未生成或已过期，请先更新切割与空移路径。");
        const auto head=EffectiveHeadDefinition(scene,*setup),settings=RapidSettings(*setup);auto input=std::make_shared<SimulationCollisionSnapshot>();
        input->PartialTrajectory=allowPartial;
        input->InputKey=SimulationCollisionKey(scene,id);auto& q=input->Query;
        q.MaxSegmentTimeMs=SimulationCollisionSegmentBudgetMs;
        q.Boxes=wholeHead&&IndependentArticulatedHead(head)?AssemblyHeadCollisionBoxes(head):HeadCollisionBoxes(scene,head);input->BaseHeadBoxCount=q.Boxes.size();AppendAdditionalHeadEnvelope(q.Boxes,settings);
        if(head.contains("geometryRef"))input->Dependencies.push_back(ReadWireRef(WireRef(head.at("geometryRef").To<Map>())));
        input->Scope=head.contains("collisionBoxes")?"MachineDeclaredBoxesWorkpieceAndFixtures":"BuiltinSchematicWorkpieceAndFixtures";
        if(wholeHead&&IndependentArticulatedHead(head))input->Scope="SelectedHeadAssemblyWorkpieceAndFixtures";
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==id){const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
                q.Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(e->GetID()),w->GetGeometryRef(),RigidMatrix(p->GetTransform())));input->Dependencies.push_back(ReadWireRef(WireRef(w->GetGeometryRef())));}
            if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==id&&(f->GetEnabled()&&f->GetCollisionEnabled())){
                q.Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(e->GetID()),f->GetGeometryRef(),RigidMatrix(f->GetTransform())));input->Dependencies.push_back(ReadWireRef(WireRef(f->GetGeometryRef())));}
        }
        if(!allowPartial)AppendGeneratedDataDependencies(scene,id,input->Dependencies);
        const auto height=RegistrationNumber(settings.at("followHeightMm")),threshold=RegistrationNumber(settings.at("collisionThresholdMm"));
        std::vector<Pose> poses;std::vector<bool> breaks;std::vector<double> clearances;
        M::Vector previousDirection{0,0,1};M::Quaternion rotation{0,0,0,1};bool missingRapid=false,seededFirst=false;
        const auto append=[&](const Map& part,const Array& transform,bool tcp,const Text& intent,const Variant& order,double clearance,bool forceBreak,const Array* controls=nullptr,const Array* editablePoints=nullptr){
            const auto points=part.at("points").To<Array>(),directions=part.at("directions").To<Array>();if(points.empty()||points.size()!=directions.size())throw std::invalid_argument("CAM_COLLISION_POSES_REQUIRED");
            const auto samples=part.contains("sourceSamples")?part.at("sourceSamples").To<Array>():Array{};
            for(std::size_t i=0;i<points.size();++i){const auto axis=M::Unit(RapidTransform(transform,directions[i],true));auto position=RapidTransform(transform,points[i],false);if(!tcp)position=M::Add(position,M::Scale(axis,height));
                RequireRigidHeadDirection(head,axis);
                if(!(seededFirst&&axis==previousDirection))rotation=CollisionRotation::Step(previousDirection,axis,rotation,&input->MaximumRotationCorrectionRad);seededFirst=false;const bool disconnected=!poses.empty()&&i==0&&(forceBreak||M::Length(M::Sub(position,poses.back().Position))>1e-4);
                poses.push_back({position,rotation});breaks.push_back(disconnected);clearances.push_back(clearance);previousDirection=axis;
                input->Frames.emplace_back(Map{{"position",GenPoint(position)},{"rotation",Array{rotation[0],rotation[1],rotation[2],rotation[3]}},{"role",part.at("role")},{"intentId",intent},{"order",order}});
                if(tcp&&part.contains("planningMethod")&&String(part,"planningMethod")=="QuickRetract")std::get<Map>(input->Frames.back().m_Value)["rapidDirectionLocked"]=true;
                if(!tcp&&i<samples.size())std::get<Map>(input->Frames.back().m_Value)["sourceSample"]=samples[i];
                bool keypoint=collectKeypoints&&(i==0||i+1==points.size());
                if(collectKeypoints&&tcp)keypoint|=part.contains("quickMotion")||part.contains("manualPath")&&part.at("manualPath").To<Map>().contains("points")||part.contains("construction")&&String(part.at("construction").To<Map>(),"kind")=="Retract";
                if(controls&&i<samples.size()){const auto sample=samples[i].To<Map>();if(sample.contains("anchor"))
                    keypoint|=std::any_of(controls->begin(),controls->end(),[&](const auto& anchor){return IntentSameNativeAnchor(sample.at("anchor").To<Map>(),anchor.To<Map>());});}
                if(keypoint){
                    const auto frame=poses.size()-1;input->KeypointFrames.push_back(frame);
                    const auto role=String(part,"role");
                    Map location{{"frameIndex",std::to_string(frame)},{"intentId",intent},{"order",order},{"role",role},
                        {"label",Text((role=="LeadIn"?"引入":role=="LeadOut"?"引出":tcp?"空移":"")+(i==0?Text("起点"):i+1==points.size()?Text("终点"):Text("姿态边界")))}};
                    if(tcp){location["rapidPointIndex"]=std::to_string(i);location["rapidPointCount"]=std::to_string(points.size());location["from"]=part.at("from");location["to"]=part.at("to");if(i&&i+1<points.size())location["label"]=Text("关键点 ")+std::to_string(i);}
                    if(i<samples.size()){const auto sample=samples[i].To<Map>();if(sample.contains("anchor")){
                        location["anchor"]=sample.at("anchor");
                        if(editablePoints)for(const auto& value:*editablePoints){const auto point=value.To<Map>();if(IntentSameNativeAnchor(sample.at("anchor").To<Map>(),point.at("anchor").To<Map>())){
                            location["pointId"]=point.at("id");location["label"]=point.at("label");break;}}
                    }}
                    input->KeyposeLocations[frame]=std::move(location);
                }
            }
        };
        const auto operations=generated.at("operations").To<Array>(),transitions=generated.at("transitions").To<Array>();
        for(std::size_t i=0;i<operations.size();++i){const auto op=operations[i].To<Map>();const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"));
            if(op.contains("curveData"))input->CutRevisions[String(op,"intentId")]=EngineeringIdentity(op.at("curveData").To<Map>());
            if(op.contains("simulationStartFrame")){const auto seed=op.at("simulationStartFrame").To<Map>();if(seed.at("placementTransform")==Variant(placement->GetTransform())){
                previousDirection=RegistrationPoint(seed.at("direction"));const auto q=seed.at("rotation").To<Array>();for(int j=0;j<4;++j)rotation[j]=RegistrationNumber(q[j]);seededFirst=true;}}
            // Only a full collision check needs all source control anchors.
            // Playback/machine snapshots use the existing sampled poses;
            // local repair checks its selected cut's neighbors separately.
            const auto controls=collectKeypoints&&(!allowPartial||String(op,"status")=="Current")?IntentFieldControlAnchors(ReadIntentPath(scene,Identifier(op,"intentId"))):Array{};
            const auto editablePoints=collectKeypoints&&(!allowPartial||String(op,"status")=="Current")?AxisPointList(scene,ReadAxisAuthoringPath(scene,Identifier(op,"intentId"))):Array{};
            for(const auto& value:op.at("parts").To<Array>()){append(value.To<Map>(),placement->GetTransform(),false,String(op,"intentId"),op.at("order"),threshold,missingRapid,collectKeypoints?&controls:nullptr,collectKeypoints?&editablePoints:nullptr);missingRapid=false;}
            if(i+1==operations.size())continue;const auto next=operations[i+1].To<Map>();bool found=false;
            for(const auto& v:transitions){auto t=v.To<Map>();if(t.at("from")!=op.at("intentId")||t.at("to")!=next.at("intentId"))continue;
                // Draft marks an authored route that has not been certified;
                // its real TCP poses still require the same continuous check.
                if((String(t,"geometryStatus")=="Generated"||String(t,"geometryStatus")=="Draft"||allowPartial&&String(t,"geometryStatus")=="Stale"&&HasRapidEditingGeometry(t))&&t.contains("frame")&&String(t,"frame")=="Setup"&&t.contains("referencePoint")&&String(t,"referencePoint")=="ToolTip"&&t.at("points").To<Array>().size()>1){
                    t["role"]=Text("Rapid");append(t,Identity(),true,String(next,"intentId"),Text(String(op,"order")+" → "+String(next,"order")),threshold+RegistrationNumber(t.at("policy").To<Map>().at("marginMm")),false);found=true;}break;}
            missingRapid=!found;
        }
        if(poses.empty())throw std::invalid_argument("CAM_COLLISION_POSES_REQUIRED");
        for(std::size_t i=0;i+1<poses.size();++i)q.Segments.push_back({poses[i],poses[i+1],std::max(clearances[i],clearances[i+1]),!breaks[i+1]});
        if(q.Segments.empty())q.Segments.push_back({poses[0],poses[0],clearances[0],true});
        for(const auto i:input->KeypointFrames)input->KeypointSegments.push_back({poses[i],poses[i],std::max(clearances[i],i?clearances[i-1]:clearances[i]),true});
        input->KinematicKeyframes=input->KeypointFrames;
        input->FullSegmentCount=q.Segments.size();for(std::size_t i=0;i<q.Segments.size();++i)input->SegmentIndices.push_back(i);return input;
    }
    Map RebaseSimulationCollision(Scene& scene,const ID& id,const Map& prior,const SimulationCollisionSnapshot& input){
        // Reuse only the same physical sweep under the same head, obstacles
        // and clearance settings. Document/repository revisions fence tasks;
        // they do not invalidate an unrelated, geometrically identical sweep.
        const auto parse=[](const Text& key){auto fields=iCAX::Data::VariantSerializer::Deserialize(key).To<Map>();
            for(const auto* name:{"generation","geometry","rapids","overrides"})fields.erase(name);return fields;};
        if(String(prior,"schemaVersion")!="1"||String(prior,"scope")!=input.Scope||parse(String(prior,"inputKey"))!=parse(input.InputKey)||
            prior.at("headBoxes")!=Variant(CollisionHeadBoxes(input.Query,input.BaseHeadBoxCount)))return {};
        const auto geometry=GeneratedGeometryState(scene,id);std::set<Text> stale;
        // A keypose edit can change interpolated part poses even where a
        // nominal sampled TCP/direction happens to remain identical. Reuse
        // certificates only within the same independently generated cut.
        if(prior.contains("cutRevisions")){const auto oldRevisions=prior.at("cutRevisions").To<Map>();for(const auto& [intent,revision]:input.CutRevisions)
            if(!oldRevisions.contains(intent)||oldRevisions.at(intent)!=revision)stale.insert(intent);}
        else if(input.Scope=="SelectedHeadAssemblyWorkpieceAndFixtures")return {};
        for(const auto& value:geometry.at("operations").To<Array>()){const auto op=value.To<Map>();if(String(op,"status")!="Current")stale.insert(String(op,"intentId"));}
        std::map<Text,Text> previous;const auto operations=geometry.at("operations").To<Array>();
        for(std::size_t i=1;i<operations.size();++i)previous[String(operations[i].To<Map>(),"intentId")]=String(operations[i-1].To<Map>(),"intentId");
        std::set<Text> pendingRapids;for(const auto& value:geometry.at("transitions").To<Array>()){const auto t=value.To<Map>();if(String(t,"geometryStatus")=="Stale")pendingRapids.insert(String(t,"to"));}
        const auto pending=[&](const Variant& value){const auto frame=value.To<Map>();const auto intent=String(frame,"intentId");return stale.contains(intent)||
            String(frame,"role")=="Rapid"&&(stale.contains(previous[intent])||pendingRapids.contains(intent));};
        const auto frameKey=[](const Variant& value){const auto frame=value.To<Map>();Map key;
            for(const auto* name:{"position","rotation","role","intentId","order"})key[name]=frame.at(name);
            return iCAX::Data::VariantSerializer::Serialize(key);};
        const auto oldFrames=prior.at("frames").To<Array>(),oldSegments=prior.at("segments").To<Array>();
        std::map<std::pair<Text,Text>,Map> old;
        for(std::size_t i=0;i<oldSegments.size()&&i+1<oldFrames.size();++i){const auto segment=oldSegments[i].To<Map>();
            if(String(segment,"index")==std::to_string(i))old[{frameKey(oldFrames[i]),frameKey(oldFrames[i+1])}]=segment;}
        Array segments;UInt reused=0,collisions=0,unchecked=0;
        for(std::size_t i=0;i<input.Query.Segments.size();++i){
            Map segment{{"index",std::to_string(i)},{"status",Text("Unchecked")},{"reason",Text("CAM_COLLISION_LOCAL_EDIT")},{"fraction",0.},{"certifiedClearanceMm",0.}};
            const auto& query=input.Query.Segments[i];const auto next=std::min(i+1,input.Frames.size()-1);
            if(query.Connected&&!pending(input.Frames[i])&&!pending(input.Frames[next])){
                const auto found=old.find({frameKey(input.Frames[i]),frameKey(input.Frames[next])});
                if(found!=old.end()){const auto& saved=found->second;const auto status=String(saved,"status");
                    if(status=="Unchecked"||status=="Collision"||(status=="Clear"&&RegistrationNumber(saved.at("certifiedClearanceMm"))>=query.Clearance)){
                        segment=saved;segment["index"]=std::to_string(i);++reused;
                    }
                }
            }
            collisions+=String(segment,"status")=="Collision";unchecked+=String(segment,"status")=="Unchecked";segments.emplace_back(std::move(segment));
        }
        auto report=prior;report["inputKey"]=input.InputKey;report["frames"]=input.Frames;report["segments"]=std::move(segments);report["cutRevisions"]=input.CutRevisions;
        report["partialTrajectory"]=input.PartialTrajectory;
        report["validity"]=Text("Current");report["status"]=Text(collisions?"Collision":unchecked?"Unchecked":"Clear");
        report["collisionCount"]=std::to_string(collisions);report["uncheckedCount"]=std::to_string(unchecked);
        report["checkMode"]=Text("LocalEdit");report["reusedSegmentCount"]=std::to_string(reused);report["checkedSegmentCount"]=Text("0");
        report["frameCount"]=std::to_string(input.Frames.size());report["segmentCount"]=std::to_string(input.Query.Segments.size());
        for(const auto* name:{"keypoints","keypointCollisionCount","keypointUncheckedCount","keypointCheckCount"})report.erase(name);report["keypointResultsCurrent"]=false;
        std::map<Text,std::size_t> currentFrames;for(std::size_t i=0;i<input.Frames.size();++i)if(!pending(input.Frames[i]))currentFrames[frameKey(input.Frames[i])]=i;
        for(const auto* field:{"keyposeChecks","interpolationChecks"}){
            Array kept;
            if(prior.contains(field))for(const auto& value:prior.at(field).To<Array>()){
                auto row=value.To<Map>();bool valid=true;
                const auto remap=[&](Map& record,const Text& name){if(!record.contains(name))return;const auto oldIndex=static_cast<std::size_t>(Integer(record,name));
                    if(oldIndex>=oldFrames.size()){valid=false;return;}const auto found=currentFrames.find(frameKey(oldFrames[oldIndex]));if(found==currentFrames.end()){valid=false;return;}record[name]=std::to_string(found->second);};
                for(const auto* name:{"index","frameIndex","firstFrameIndex","lastFrameIndex"})remap(row,name);
                for(const auto* name:{"location","first","last"})if(row.contains(name)){auto location=row.at(name).To<Map>();remap(location,"frameIndex");row[name]=location;}
                if(valid)kept.emplace_back(std::move(row));
            }
            report[field]=std::move(kept);
        }
        return report;
    }
    Map RebaseSimulationCollisionState(Scene& scene,const ID& id,const Map& report){
        try{auto input=CaptureSimulationCollision(scene,id,false,true,true);auto rebased=RebaseSimulationCollision(scene,id,report,*input);
            if(!rebased.empty()){rebased.erase("inputKey");return rebased;}}
        catch(const std::exception& error){return {{"validity",Text("Stale")},{"status",Text("Unchecked")},{"reason",Text(error.what())}};}
        return {{"validity",Text("Stale")},{"status",Text("Unchecked")}};
    }
    void ResumeUncheckedSimulationCollision(Scene& scene,const ID& id,SimulationCollisionSnapshot& input){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);if(setup->GetSimulationCheckData().empty())return;
        // Only the current safe algorithm's exact input signature is reusable.
        // It contains head/calibration, all workpiece/fixture resource versions
        // and transforms, cutting geometry/poses, order, rapids and clearances.
        const auto ref=setup->GetSimulationCheckData();
        auto prior=EngineeringBody(ref,"cam.simulation-collision");
        if(String(prior,"inputKey")!=input.InputKey){prior=RebaseSimulationCollision(scene,id,prior,input);if(prior.empty())return;}
        if(String(prior,"schemaVersion")!="1"||String(prior,"inputKey")!=input.InputKey||String(prior,"scope")!=input.Scope||prior.at("frames")!=Variant(input.Frames))return;
        const auto segments=prior.at("segments").To<Array>();if(segments.size()!=input.FullSegmentCount)return;
        std::vector<std::size_t> pending;std::vector<iCAX::GeometryServices::RigidMotionSegment> queries;UInt collisions=0;
        for(std::size_t i=0;i<segments.size();++i){const auto segment=segments[i].To<Map>();if(String(segment,"index")!=std::to_string(i))return;
            const auto status=String(segment,"status");const auto& query=input.Query.Segments[i];
            if(status=="Unchecked"){pending.push_back(i);queries.push_back(query);}
            else if(status=="Clear"){
                const auto certified=RegistrationNumber(segment.at("certifiedClearanceMm"));
                if(!query.Connected||!std::isfinite(certified)||certified<query.Clearance)return;
            }else if(status=="Collision"){
                if(!query.Connected)return;const auto fraction=RegistrationNumber(segment.at("fraction"));if(!std::isfinite(fraction)||fraction<0||fraction>1)return;++collisions;
            }else return;
        }
        if(pending.empty()||Integer(prior,"uncheckedCount")!=pending.size()||Integer(prior,"collisionCount")!=collisions)return;
        const auto priorBudget=Integer(prior,"maxSegmentTimeMs");if(priorBudget<1||priorBudget>60000)return;
        if(input.Scope=="SelectedHeadAssemblyWorkpieceAndFixtures"){
            const auto jobs=KeyposeCollision::Intervals(input.KinematicKeyframes,pending);pending.clear();queries.clear();
            for(const auto& job:jobs)for(auto i=job.First;i<std::max(job.First+1,job.Last);++i){pending.push_back(i);queries.push_back(input.Query.Segments.at(i));}
        }
        input.PreviousSegments=segments;input.PreviousReportRef=EngineeringIdentity(setup->GetSimulationCheckData());input.PreviousMaxSegmentTimeMs=static_cast<unsigned>(priorBudget);
        if(prior.contains("keyposeChecks"))input.PreviousKeyposeChecks=prior.at("keyposeChecks").To<Array>();
        if(prior.contains("interpolationChecks"))input.PreviousInterpolationChecks=prior.at("interpolationChecks").To<Array>();
        input.SegmentIndices=std::move(pending);input.Query.Segments=std::move(queries);input.Query.MaxSegmentTimeMs=SimulationCollisionRetryBudgetMs;

    }
    void FinishSimulationCollision(Scene& scene,SceneRuntime& runtime,ImportTask& task){
        try{const auto calculated=task.SimulationWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence||task.SimulationInput->InputKey!=SimulationCollisionKey(scene,task.SetupID))task.Status="Superseded";
            else{UInt collisions=0,unchecked=0;const auto& input=*task.SimulationInput;const bool resumed=!input.PreviousSegments.empty();
                if(calculated->Segments.size()!=input.SegmentIndices.size())throw std::runtime_error("CAM_COLLISION_RESULT_INCOMPLETE");
                Array segments=resumed?input.PreviousSegments:Array(input.FullSegmentCount);
                for(std::size_t i=0;i<calculated->Segments.size();++i){const auto& r=calculated->Segments[i];const auto index=input.SegmentIndices[i];
                    auto row=CollisionSegmentState(index,r);if(index<input.HeadDiagnostics.size()&&!input.HeadDiagnostics[index].empty())row["headPose"]=input.HeadDiagnostics[index];segments.at(index)=std::move(row);}
                for(const auto& value:segments){const auto segment=value.To<Map>();collisions+=String(segment,"status")=="Collision";unchecked+=String(segment,"status")=="Unchecked";}
                const auto reused=resumed?input.FullSegmentCount-input.SegmentIndices.size():0;
                Map report{{"schema",Text("cam.simulation-collision")},{"schemaVersion",Text("1")},{"inputKey",input.InputKey},{"scope",input.Scope},
                    {"representation",Text("BoxCompoundMesh")},{"headBoxCount",std::to_string(input.Query.Boxes.size())},{"headBoxes",CollisionHeadBoxes(input.Query,input.BaseHeadBoxCount)},{"meshDeflectionMm",CollisionMeshDeflectionMm},
                    {"rotationNumerics",Map{{"convention",Text("AxisAndScalarRollV1")},{"rollQuantumRad",CollisionRotation::RollQuantumRad},{"maximumStepCorrectionRad",CollisionRotation::MaximumStepCorrectionRad},{"maximumObservedStepCorrectionRad",input.MaximumRotationCorrectionRad}}},
                    {"status",Text(collisions?"Collision":unchecked?"Unchecked":"Clear")},{"collisionCount",std::to_string(collisions)},{"uncheckedCount",std::to_string(unchecked)},
                    {"segments",segments},{"frames",input.Frames},{"distanceQueries",std::to_string(calculated->DistanceQueries)},
                    {"checkMode",Text(!input.PathKind.empty()?input.PathKind:!input.SelectedIntent.empty()||!input.SelectedTo.empty()?"SelectedPath":resumed?"UncheckedOnly":"Full")},{"initialSegmentTimeMs",std::to_string(SimulationCollisionSegmentBudgetMs)},
                    {"checkOrder",Text("LimitsThenCollision")},{"collisionOrder",Text("KeypointsThenIntervals")},{"keypointCheckCount",std::to_string(input.KeypointFrames.size())},
                    {"parallelWorkers",std::to_string(input.ParallelWorkers)},
                    {"retrySegmentTimeMs",std::to_string(resumed?SimulationCollisionRetryBudgetMs:0)},{"maxSegmentTimeMs",std::to_string(std::max(input.Query.MaxSegmentTimeMs,input.PreviousMaxSegmentTimeMs))},
                    {"reusedSegmentCount",std::to_string(reused)},{"checkedSegmentCount",std::to_string(calculated->Segments.size())},{"retrySegmentCount",std::to_string(resumed?calculated->Segments.size():0)},
                    {"machineValidation",Text("Unchecked")},{"productionQualified",false}};
                if(resumed)report["sourceReportRevision"]=input.PreviousReportRef;
                report["partialTrajectory"]=input.PartialTrajectory;
                report["cutRevisions"]=input.CutRevisions;
                if(!input.HeadCoverage.empty())report["headCoverage"]=input.HeadCoverage;
                if(input.CheckFullHead){
                    const auto merge=[](const Array& previous,const Array& updated,const Text& key){Array rows=previous;std::set<Text> replacing;for(const auto& value:updated)replacing.insert(String(value.To<Map>(),key));
                        std::erase_if(rows,[&](const auto& value){return replacing.contains(String(value.To<Map>(),key));});rows.insert(rows.end(),updated.begin(),updated.end());return rows;};
                    report["keyposeChecks"]=merge(input.PreviousKeyposeChecks,input.KeyposeChecks,"frameIndex");
                    report["interpolationChecks"]=merge(input.PreviousInterpolationChecks,input.InterpolationChecks,"firstFrameIndex");
                    report["interpolationTaskCount"]=std::to_string(input.InterpolationChecks.size());
                }
                if(!input.PathKind.empty())report["checkedPathKind"]=input.PathKind;
                if(!input.SelectedIntent.empty())report["checkedIntentId"]=input.SelectedIntent;
                if(!input.SelectedTo.empty()){report["checkedFrom"]=input.SelectedFrom;report["checkedTo"]=input.SelectedTo;}
                Array keypoints;UInt keypointCollisions=0,keypointUnchecked=0;
                {std::lock_guard lock(task.CollisionProgress->Mutex);for(const auto& [index,value]:task.CollisionProgress->KeypointResults){auto row=CollisionSegmentState(index,value);row["frameIndex"]=std::to_string(input.KeypointFrames.at(index));keypoints.emplace_back(std::move(row));keypointCollisions+=value.Status=="Collision";keypointUnchecked+=value.Status=="Unchecked";}}
                if(keypoints.size()!=input.KeypointFrames.size())throw std::runtime_error("CAM_COLLISION_KEYPOINT_RESULTS_INCOMPLETE");
                report["keypoints"]=keypoints;report["keypointCollisionCount"]=std::to_string(keypointCollisions);report["keypointUncheckedCount"]=std::to_string(keypointUnchecked);report["keypointResultsCurrent"]=input.SelectedIntent.empty()&&input.SelectedTo.empty()&&input.PathKind.empty();
                const auto resource=EngineeringData(report);
                Transaction(scene.Database(),"更新轨迹碰撞检查",[&](auto& tx){tx.ModifyComponent(task.SetupID,CFiveAxisSetupComponent::S_ClassName,{{"SimulationCheckData",StoredRef(resource)}});},false);
                task.Result={{"status",report.at("status")},{"collisionCount",report.at("collisionCount")},{"uncheckedCount",report.at("uncheckedCount")},
                    {"checkMode",report.at("checkMode")},{"reusedSegmentCount",report.at("reusedSegmentCount")},{"checkedSegmentCount",report.at("checkedSegmentCount")},{"retrySegmentCount",report.at("retrySegmentCount")},{"maxSegmentTimeMs",report.at("maxSegmentTimeMs")}};task.Status="Completed";
            }
        }catch(const std::exception& e){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}
        if(task.CollisionProgress){std::lock_guard lock(task.CollisionProgress->Mutex);task.CollisionProgress->Active.clear();task.CollisionProgress->Current.reset();}
        task.SimulationWorker={};task.SimulationInput.reset();
    }
    void FinishPlaybackCollision(Scene&,SceneRuntime& runtime,ImportTask& task){
        try{task.SimulationWorker.Result();task.Status=task.Sequence!=runtime.Sequence?"Superseded":task.Cancelled->load()?"Cancelled":"Completed";}
        catch(const std::exception& e){task.Status=task.Sequence!=runtime.Sequence?"Superseded":task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}
        task.SimulationWorker={};task.SimulationInput.reset();
    }
}
Map StartPlaybackCollision(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","firstSegment"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("PlaybackCollision:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,prior]:runtime->Tasks)if(prior.CommandID==command){if(prior.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,prior);}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    ImportTask task;task.Kind="PlaybackCollision";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");
    for(auto& [_,other]:runtime->Tasks)if(other.Kind==task.Kind&&other.SetupID==task.SetupID&&other.Status=="Running")other.Cancelled->store(true);
    auto captured=CaptureSimulationCollision(scene,task.SetupID,true,false,true);
    if(captured->Scope=="SelectedHeadAssemblyWorkpieceAndFixtures")PrepareFullHeadCollision(scene,task.SetupID,*captured);
    task.SimulationInput=std::move(captured);
    const auto first=static_cast<std::size_t>(Integer(request,"firstSegment"));
    if(first>=task.SimulationInput->Query.Segments.size())throw std::invalid_argument("CAM_PLAYBACK_SEGMENT_OUTSIDE_PATH");
    task.Result={{"segmentCount",std::to_string(task.SimulationInput->Query.Segments.size())},{"frameCount",std::to_string(task.SimulationInput->Frames.size())},{"maxSegmentTimeMs",std::to_string(task.SimulationInput->Query.MaxSegmentTimeMs)},
        {"representation",Text("BoxCompoundMesh")},{"headBoxCount",std::to_string(task.SimulationInput->Query.Boxes.size())},{"meshDeflectionMm",CollisionMeshDeflectionMm}};
    task.PlaybackProgress=std::make_shared<PlaybackCollisionProgress>();task.PlaybackProgress->Through=first;
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>();
    task.SimulationWorker=iCAX::Tasks::Run([input=task.SimulationInput,service,progress=task.PlaybackProgress,cancelled=task.Cancelled,first]{
        auto index=first;
        const auto next=[&]() -> std::optional<std::size_t>{
            while(index>progress->Through.load()){
                if(cancelled->load())return {};std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if(cancelled->load()||index>=input->Query.Segments.size())return {};return index++;
        };
        if(input->CheckFullHead){auto result=std::make_shared<iCAX::GeometryServices::RigidMotionCollisionResult>();
            const auto keys=input->CheckFullHead(input->KeypointSegments,true,{},[&]{return cancelled->load();},{},{});
            {std::lock_guard lock(progress->Mutex);progress->HeadCoverage=input->HeadCoverage;}
            while(const auto i=next()){const auto checked=input->CheckFullHead({input->Query.Segments.at(*i)},false,*i,[&]{return cancelled->load();},{},{});
                {std::lock_guard lock(progress->Mutex);progress->Results[*i]=checked.Segments.front();progress->HeadPoses[*i]=input->HeadDiagnostics.at(*i);}result->Segments.push_back(checked.Segments.front());result->DistanceQueries+=checked.DistanceQueries;}
            return result;}
        return std::make_shared<iCAX::GeometryServices::RigidMotionCollisionResult>(service->CheckIncremental(input->Query,next,
            [&](auto i,const auto& result){std::lock_guard lock(progress->Mutex);progress->Results[i]=result;},[&]{return cancelled->load();}));
    });
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
Map QueryPlaybackCollision(Scene& scene,const Map& request){
    CutFields(request,{"epoch","taskId","throughSegment","afterSegment"});const auto runtime=Runtime(scene);
    if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto found=runtime->Tasks.find(String(request,"taskId"));
    if(found==runtime->Tasks.end()||found->second.Kind!="PlaybackCollision")throw std::invalid_argument("CAM_TASK_NOT_FOUND");
    auto& task=found->second;const auto count=static_cast<std::size_t>(Integer(task.Result,"segmentCount"));const auto through=static_cast<std::size_t>(Integer(request,"throughSegment"));
    if(through>=count)throw std::invalid_argument("CAM_PLAYBACK_SEGMENT_OUTSIDE_PATH");
    const bool superseded=task.Sequence!=runtime->Sequence;if(superseded)task.Cancelled->store(true);
    if(task.Status=="Running"&&!superseded)task.PlaybackProgress->Through=std::max(task.PlaybackProgress->Through.load(),through);
    auto response=TaskState(scene,*runtime,task);Array segments;
    if(superseded)response["status"]=Text("Superseded");
    const auto after=request.contains("afterSegment")?std::optional<UInt>(Integer(request,"afterSegment")):std::nullopt;
    {std::lock_guard lock(task.PlaybackProgress->Mutex);
            response["headCoverage"]=task.PlaybackProgress->HeadCoverage;
            for(const auto& [i,r]:task.PlaybackProgress->Results)if(!superseded&&(task.Status=="Running"||task.Status=="Completed")&&(!after||i>*after)){auto row=CollisionSegmentState(i,r);if(task.PlaybackProgress->HeadPoses.contains(i))row["headPose"]=task.PlaybackProgress->HeadPoses.at(i);segments.emplace_back(row);}
    }
    response["segments"]=segments;response["segmentCount"]=std::to_string(count);response["maxSegmentTimeMs"]=task.Result.at("maxSegmentTimeMs");return response;
}
Map CheckSimulationCollision(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","maxParallelTasks","intentId","from","to","pathKind"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("SimulationCollision:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    ImportTask task;task.Kind="SimulationCollision";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");
    for(const auto& [_,other]:runtime->Tasks)if(other.Kind==task.Kind&&other.SetupID==task.SetupID&&other.Status=="Running")throw std::invalid_argument("CAM_COLLISION_ALREADY_RUNNING");
    const auto hardware=std::max(1u,std::thread::hardware_concurrency());
    const auto requestedWorkers=request.contains("maxParallelTasks")?Integer(request,"maxParallelTasks"):UInt(std::min(4u,hardware>1?hardware-1:1u));
    if(!requestedWorkers||requestedWorkers>32)throw std::invalid_argument("CAM_COLLISION_PARALLEL_BUDGET_INVALID");
    const bool pathScope=request.contains("intentId")||request.contains("from")||request.contains("to"),scoped=pathScope||request.contains("pathKind");
    if(request.contains("pathKind")&&(pathScope||(String(request,"pathKind")!="Cutting"&&String(request,"pathKind")!="Rapid")))throw std::invalid_argument("CAM_COLLISION_PATH_SCOPE_INVALID");
    auto captured=CaptureSimulationCollision(scene,task.SetupID,true,scoped,true);
    if(scoped){
        if(request.contains("intentId")&&(request.contains("from")||request.contains("to")))throw std::invalid_argument("CAM_COLLISION_PATH_SCOPE_INVALID");
        if(request.contains("pathKind"))captured->PathKind=String(request,"pathKind");
        else if(request.contains("intentId"))captured->SelectedIntent=iCAX::Data::to_string(Identifier(request,"intentId"));
        else{captured->SelectedFrom=iCAX::Data::to_string(Identifier(request,"from"));captured->SelectedTo=iCAX::Data::to_string(Identifier(request,"to"));}
        const auto geometry=GeneratedGeometryState(scene,task.SetupID);const auto operations=geometry.at("operations").To<Array>();
        std::map<Text,Text> previous;bool ready=false;
        for(std::size_t i=0;i<operations.size();++i){const auto op=operations[i].To<Map>();const auto id=String(op,"intentId");
            if(i)previous[id]=String(operations[i-1].To<Map>(),"intentId");
            if(id==captured->SelectedIntent){if(String(op,"status")!="Current")throw std::invalid_argument("CAM_COLLISION_SELECTED_PATH_PENDING");ready=true;}}
        if(!captured->SelectedTo.empty())for(const auto& value:geometry.at("transitions").To<Array>()){const auto t=value.To<Map>();
            if(String(t,"from")==captured->SelectedFrom&&String(t,"to")==captured->SelectedTo){ready=String(t,"geometryStatus")=="Generated"||String(t,"geometryStatus")=="Draft";}}
        if(pathScope&&!ready)throw std::invalid_argument("CAM_COLLISION_SELECTED_PATH_PENDING");
        const auto belongs=[&](const Variant& value){const auto frame=value.To<Map>();
            if(!captured->PathKind.empty())return (String(frame,"role")=="Rapid")==(captured->PathKind=="Rapid");
            return String(frame,"role")=="Rapid"?
            !captured->SelectedTo.empty()&&String(frame,"intentId")==captured->SelectedTo&&previous[captured->SelectedTo]==captured->SelectedFrom:
            !captured->SelectedIntent.empty()&&String(frame,"intentId")==captured->SelectedIntent;};
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID);Map prior;
        if(!setup->GetSimulationCheckData().empty()){
            const auto stored=EngineeringBody(setup->GetSimulationCheckData(),"cam.simulation-collision");
            prior=String(stored,"inputKey")==captured->InputKey?stored:RebaseSimulationCollision(scene,task.SetupID,stored,*captured);
            captured->PreviousReportRef=EngineeringIdentity(setup->GetSimulationCheckData());
        }
        if(!prior.empty()&&prior.at("frames")==Variant(captured->Frames)){
            captured->PreviousSegments=prior.at("segments").To<Array>();
            if(prior.contains("keyposeChecks"))captured->PreviousKeyposeChecks=prior.at("keyposeChecks").To<Array>();
            if(prior.contains("interpolationChecks"))captured->PreviousInterpolationChecks=prior.at("interpolationChecks").To<Array>();
        }
        else for(std::size_t i=0;i<captured->FullSegmentCount;++i)captured->PreviousSegments.emplace_back(Map{{"index",std::to_string(i)},{"status",Text("Unchecked")},{"reason",Text("CAM_COLLISION_NOT_CHECKED")},{"fraction",0.},{"certifiedClearanceMm",0.}});
        const auto all=captured->Query.Segments;captured->Query.Segments.clear();captured->SegmentIndices.clear();
        for(std::size_t i=0;i<all.size();++i){const auto a=captured->Frames[i].To<Map>(),b=captured->Frames[std::min(i+1,captured->Frames.size()-1)].To<Map>();
            const bool samePath=String(a,"intentId")==String(b,"intentId")&&(String(a,"role")=="Rapid")== (String(b,"role")=="Rapid");
            if(belongs(captured->Frames[i])&&samePath){captured->Query.Segments.push_back(all[i]);captured->SegmentIndices.push_back(i);}}
        if(captured->Query.Segments.empty())throw std::invalid_argument("CAM_COLLISION_PATH_NOT_FOUND");
        const auto frames=captured->KeypointFrames;const auto segments=captured->KeypointSegments;captured->KeypointFrames.clear();captured->KeypointSegments.clear();
        for(std::size_t i=0;i<frames.size();++i)if(belongs(captured->Frames[frames[i]])){captured->KeypointFrames.push_back(frames[i]);captured->KeypointSegments.push_back(segments[i]);}
    }else ResumeUncheckedSimulationCollision(scene,task.SetupID,*captured);
    captured->ParallelWorkers=static_cast<unsigned>(std::min<UInt>(requestedWorkers,hardware));
    if(captured->Scope=="SelectedHeadAssemblyWorkpieceAndFixtures")PrepareFullHeadCollision(scene,task.SetupID,*captured);
    task.SimulationInput=std::move(captured);const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>();
    const auto& input=*task.SimulationInput;const bool resumed=!input.PreviousSegments.empty();
    task.Result={{"checkMode",Text(resumed?"UncheckedOnly":"Full")},{"checkedSegmentCount",std::to_string(input.Query.Segments.size())},
        {"reusedSegmentCount",std::to_string(resumed?input.FullSegmentCount-input.Query.Segments.size():0)},{"retrySegmentCount",std::to_string(resumed?input.Query.Segments.size():0)},{"maxSegmentTimeMs",std::to_string(input.Query.MaxSegmentTimeMs)}};
    task.CollisionProgress=std::make_shared<SimulationCollisionProgress>();task.CollisionProgress->SegmentIndices=input.CheckFullHead?input.InterpolationStarts:input.SegmentIndices;task.CollisionProgress->FullSegmentCount=input.FullSegmentCount;task.CollisionProgress->WorkerCount=input.ParallelWorkers;task.CollisionProgress->KeypointCount=input.KeypointFrames.size();task.CollisionProgress->CheckingKeypoints=true;
    task.SimulationWorker=iCAX::Tasks::Run([input=task.SimulationInput,service,cancelled=task.Cancelled,progress=task.Progress,collision=task.CollisionProgress]{
        // Static keypoint checks precede interpolation checks. They never
        // replace continuous certification of the intervals between them.
        auto keypoints=input->Query;keypoints.Segments=input->KeypointSegments;
        {std::lock_guard lock(collision->Mutex);collision->CheckingKeypoints=true;collision->KeypointCount=keypoints.Segments.size();}
        const auto total=keypoints.Segments.size()+(input->CheckFullHead?input->InterpolationStarts.size():input->Query.Segments.size());
        const auto keyStarted=[&](std::size_t index){std::lock_guard lock(collision->Mutex);collision->Active.insert(index);collision->Current=input->KeypointFrames.at(index);collision->Location=input->KeyposeLocations.at(*collision->Current);};
        const auto keyCompleted=[&](std::size_t index,const auto& value){std::lock_guard lock(collision->Mutex);collision->Active.erase(index);collision->KeypointResults.emplace(index,value);++collision->KeypointsCompleted;progress->store(static_cast<unsigned>(collision->KeypointsCompleted*100/total));};
        const auto keyResult=input->CheckFullHead?input->CheckFullHead(keypoints.Segments,true,{},[&]{return cancelled->load();},keyStarted,keyCompleted):service->CheckParallel(keypoints,input->ParallelWorkers,[&]{return cancelled->load();},keyStarted,keyCompleted);
        // CheckParallel joins every worker, including on error/cancellation.
        // No interval can start while a keypoint query is still running.
        {std::lock_guard lock(collision->Mutex);collision->CheckingKeypoints=false;collision->Active.clear();}
        const auto started=[&](std::size_t index){std::lock_guard lock(collision->Mutex);collision->Active.insert(input->SegmentIndices.at(index));collision->Current=*collision->Active.begin();
            const auto upper=std::upper_bound(input->KinematicKeyframes.begin(),input->KinematicKeyframes.end(),*collision->Current);
            if(upper!=input->KinematicKeyframes.begin()&&upper!=input->KinematicKeyframes.end())collision->Location=Map{{"first",input->KeyposeLocations.at(*std::prev(upper))},{"last",input->KeyposeLocations.at(*upper)}};
        };
        const auto completed=[&](std::size_t index,const auto&){
            std::lock_guard lock(collision->Mutex);collision->LastCompleted=input->SegmentIndices.at(index);collision->Active.erase(input->SegmentIndices.at(index));collision->Current=collision->Active.empty()?std::nullopt:std::optional(*collision->Active.begin());++collision->Completed;
            progress->store(static_cast<unsigned>((collision->KeypointsCompleted+collision->Completed)*100/total));
        };
        auto result=std::make_shared<iCAX::GeometryServices::RigidMotionCollisionResult>(input->CheckFullHead?input->CheckFullHead(input->Query.Segments,false,{},[&]{return cancelled->load();},started,completed):service->CheckParallel(input->Query,input->ParallelWorkers,[&]{return cancelled->load();},started,completed));result->DistanceQueries+=keyResult.DistanceQueries;return result;
    });
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
