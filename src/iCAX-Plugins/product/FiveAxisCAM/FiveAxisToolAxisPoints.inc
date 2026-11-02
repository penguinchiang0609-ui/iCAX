namespace
{
Map ToolAxisSamples(Scene&,const AxisPath&);
std::pair<Map,Map> AxisEndpoints(Scene&,const AxisPath& path)
{namespace Walk=iCAX::GeometryServices::OffsetTraversal;const auto exact=IntentExactCurve(path.Value);if(exact.Segments.empty())throw std::invalid_argument("CAM_INTENT_EMPTY_PATH");std::size_t first=0,last=exact.Segments.size()-1;while(first<last&&path.Value.Geometry.Segments[first].ConstructionRole=="LeadIn")++first;while(last>first&&path.Value.Geometry.Segments[last].ConstructionRole=="LeadOut")--last;return {AxisAnchor(path,first,Walk::Start(exact.Segments[first])),AxisAnchor(path,last,Walk::End(exact.Segments[last]))};}
Array AxisActiveControls(const AxisPath& path)
{const auto projected=ProjectFiveAxisPoseControls(path.Value);return projected.contains("active")?projected.at("active").To<Array>():Array{};}
Map AxisPointPosition(Scene& scene,const AxisPath& path,const Map& anchor)
{
 const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),ReadFiveAxisIntentMetadata(path.Value).PlacementID);const auto runtime=Runtime(scene);const auto key=iCAX::CAM::Intent::ToolpathPortKey(path.Port)+"/"+path.Fingerprint+"/"+iCAX::Data::VariantSerializer::Serialize(Map{{"anchor",anchor},{"poseControls",path.Value.Channels.contains("poseControls")?path.Value.Channels.at("poseControls"):Variant(Map{})},{"placement",placement->GetTransform()}});
 if(const auto saved=runtime->PosePointPositions.find(key);saved!=runtime->PosePointPositions.end())return saved->second;
 const auto at=AxisLocation(path,anchor);const auto exact=IntentExactCurve(path.Value);
 const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*service);
 PositionOffsets offsets(path.Value,exact,evaluator);const auto point=offsets.At(at.first,at.second);
 const auto m=RigidMatrix(placement->GetTransform());Array setup;
 for(int i=0;i<3;++i){double x=m[i*4+3];for(int j=0;j<3;++j)x+=m[i*4+j]*point[j];setup.emplace_back(x);}Map result{{"point",GenPoint(point)},{"setupPoint",setup}};
 if(runtime->PosePointPositions.size()>=8192)runtime->PosePointPositions.erase(runtime->PosePointPositions.begin());runtime->PosePointPositions[key]=result;return result;
}
Array AxisPointList(Scene& scene,const AxisPath& path)
{
 const auto [start,end]=AxisEndpoints(scene,path);const auto controls=AxisActiveControls(path);Array output;std::size_t index=0;
 const auto row=[&](const Text& id,const Text& label,const Map& anchor,bool system){const auto current=AxisCurrentAnchor(path,anchor);const auto state=AxisPointPosition(scene,path,current);Map item{{"id",id},{"label",label},{"anchor",current},{"point",state.at("point")},{"setupPoint",state.at("setupPoint")},{"system",system},{"overridden",false},{"transition",AxisPolicy(path).contains("transitionMetric")?AxisPolicy(path).at("transitionMetric"):Variant(Text("ArcLength"))}};
  for(const auto& value:controls){const auto point=value.To<Map>();if(String(point,"kind")!="Control"||!SameAxisAnchor(path,current,point.at("anchor").To<Map>()))continue;item["overridden"]=true;item["transition"]=point.at("transition");if(point.contains("driver")){const auto driver=point.at("driver").To<Map>();Array targets;for(const auto& candidate:controls){const auto target=candidate.To<Map>();if(String(target,"kind")!="InfluenceTarget"||!target.contains("driver")||String(target.at("driver").To<Map>(),"id")!=String(driver,"id"))continue;targets.emplace_back(Map{{"anchor",AxisCurrentAnchor(path,target.at("anchor").To<Map>())},{"weight",target.at("weight")},{"referenceDirection",target.at("referenceDirection")}});}item["influence"]=Map{{"referenceDirection",driver.at("referenceDirection")},{"interpolation",driver.at("interpolation")},{"targets",targets}};}}output.emplace_back(std::move(item));};
 row("start","起点",start,true);row("end","终点",end,true);for(const auto& value:controls){const auto point=value.To<Map>();if(String(point,"kind")!="Control")continue;const auto anchor=point.at("anchor").To<Map>();if(SameAxisAnchor(path,anchor,start)||SameAxisAnchor(path,anchor,end))continue;row(String(point,"id"),"关键点 "+std::to_string(++index),anchor,false);}return output;
}
Map AxisPointPatch(Scene& scene,const AxisPath& path,bool changed){
 const auto runtime=Runtime(scene);const auto setupID=ReadFiveAxisIntentMetadata(path.Value).SetupID;
 const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());
 return {{"deferredPose",true},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}},
  {"inputRevision",std::to_string(job->GetInputRevision())},{"invalidatedIntentIds",changed?Array{iCAX::Data::to_string(path.PathId)}:Array{}},
  {"setup",Map{{"id",iCAX::Data::to_string(setupID)},{"inputRevision",std::to_string(setup->GetInputRevision())},{"planValidity",setup->GetPlanValidity()}}}};
}
std::vector<Map> DeferredAxisCommands(Scene& scene,const AxisPath& terminal,const AxisPath& authoring,const Array& edits,const Text& removed,const ID& command)
{
 using namespace iCAX::CAM::Intent;const auto definition=IntentOperationDefinition(scene,terminal.Port.OperationId);Map parameters{{"controls",edits}};
 if(terminal.ResultState!="Current"&&String(definition,"typeId")=="fiveaxis.pose.delta"){
  parameters=definition.at("parameters").To<Map>();auto controls=parameters.at("controls").To<Array>();std::set<Text> replacing;if(!removed.empty())replacing.insert(removed);
  for(const auto& item:edits){const auto p=item.To<Map>();if(String(p,"kind")=="Control")replacing.insert(String(p,"id"));}
  std::erase_if(controls,[&](const auto& item){const auto p=item.To<Map>();return replacing.contains(String(p,"id"))||p.contains("driver")&&replacing.contains(String(p.at("driver").To<Map>(),"id"));});
  controls.insert(controls.end(),edits.begin(),edits.end());parameters["controls"]=std::move(controls);
  auto ids=parameters.contains("removedIds")?parameters.at("removedIds").To<Array>():Array{};
  if(!removed.empty()&&std::none_of(ids.begin(),ids.end(),[&](const auto& item){return item.To<Text>()==removed;}))ids.emplace_back(removed);
  if(!ids.empty())parameters["removedIds"]=std::move(ids);
  return {IntentOperationUpdate(scene,terminal.Port.OperationId,IntentPlan(scene,ReadFiveAxisIntentMetadata(authoring.Value).SetupID),command,std::move(parameters))};
 }
 if(!removed.empty())parameters["removedIds"]=Array{removed};
 if(!terminal.OwnParticipating)return AppendIntentPathOperation(scene,terminal,"fiveaxis.pose.delta",std::move(parameters),command,{{"sourceOperationId",IntentSourceOperation(scene,terminal.Port)}});
 auto input=ToData(SOperationInput{"input/0",terminal.Port,true,{}});input["deferred"]=true;input["producerFingerprint"]=definition.at("definitionFingerprint");
 return {Map{{"kind",Text("AddOperation")},{"commandId",command},{"planId",IntentPlan(scene,ReadFiveAxisIntentMetadata(authoring.Value).SetupID)},
  {"typeId",Text("fiveaxis.pose.delta")},{"parameters",std::move(parameters)},{"context",Map{{"sourceOperationId",IntentSourceOperation(scene,terminal.Port)}}},{"inputs",Array{input}}}};
}
AxisPath CommitDeferredAxis(Scene& scene,const std::vector<Map>& commands,const AxisPath& terminal,const AxisPath& path,const Text& title)
{
 const auto setup=ReadFiveAxisIntentMetadata(path.Value).SetupID;const auto receipts=CommitIntentCommands(scene,commands,{setup},title,false);
 const auto runtime=Runtime(scene);const auto plan=IntentPlan(scene,setup);
 if(const auto task=runtime->IntentGenerations.find(plan);task!=runtime->IntentGenerations.end()){task->second->Cancel();runtime->IntentGenerations.erase(task);}
 if(const auto task=runtime->IntentGenerationTasks.find(plan);task!=runtime->IntentGenerationTasks.end()){
  if(runtime->Tasks.contains(task->second)&&runtime->Tasks.at(task->second).Status=="Running")runtime->Tasks.at(task->second).Status="Superseded";
  runtime->IntentGenerationTasks.erase(task);
 }
 auto saved=path;auto channel=saved.Value.Channels.contains("poseControls")?saved.Value.Channels.at("poseControls").To<Map>():Map{{"schema",Text("fiveaxis.pose-controls/1")},{"schemaVersion",1u},{"attachment",Text("Points")},{"entries",Array{}}};auto entries=channel.at("entries").To<Array>();
 const auto edit=std::find_if(commands.begin(),commands.end(),[](const auto& command){return command.contains("parameters");});const auto parameters=edit->at("parameters").To<Map>();std::set<Text> replacing;
 if(parameters.contains("removedIds"))for(const auto& item:parameters.at("removedIds").To<Array>())replacing.insert(item.To<Text>());
 for(const auto& item:parameters.at("controls").To<Array>()){const auto p=item.To<Map>();if(String(p,"kind")=="Control")replacing.insert(String(p,"id"));}
 std::erase_if(entries,[&](const auto& item){const auto p=item.To<Map>();return replacing.contains(String(p,"id"))||p.contains("driver")&&replacing.contains(String(p.at("driver").To<Map>(),"id"));});
 for(const auto& item:parameters.at("controls").To<Array>())entries.push_back(item);channel["entries"]=std::move(entries);saved.Value.Channels["poseControls"]=std::move(channel);saved.ResultState="Pending";if(parameters.contains("removedIds"))saved.Value.Channels["fiveaxis.pendingPoseRemoval"]=parameters.at("removedIds");
 auto nextTerminal=terminal;nextTerminal.ResultState="Pending";nextTerminal.Value={};nextTerminal.Fingerprint.clear();
 for(const auto& receipt:receipts)if(receipt.contains("operationId"))nextTerminal.Port=iCAX::CAM::Intent::DeclaredToolpathPort(receipt.at("operationId").To<ID>(),"main");
 runtime->PoseAuthoringSnapshots[path.PathId]={runtime->Sequence,std::move(nextTerminal),saved};return saved;
}

}
Map ListToolAxisPoints(Scene& scene,const Map& request)
{CutFields(request,{"expected","intentId"});CheckExpected(*Runtime(scene),request);return {{"points",AxisPointList(scene,ReadAxisAuthoringPath(scene,Identifier(request,"intentId")))}};}
Map ResolveToolAxisPoint(Scene& scene,const Map& request)
{
 CutFields(request,{"expected","intentId","nearPoint","maximumDistanceMm","frame"});CheckExpected(*Runtime(scene),request);if(String(request,"frame")!="Workpiece")throw std::invalid_argument("CAM_POSE_FRAME_INVALID");const auto path=ReadAxisAuthoringPath(scene,Identifier(request,"intentId"));auto query=request;query["mode"]=Text("ClosestPoint");const auto found=ResolveIntentPoint(scene,path,query,false);const auto anchor=found.at("anchor").To<Map>();Text existing;for(const auto& value:AxisPointList(scene,path)){const auto row=value.To<Map>();if(SameAxisAnchor(path,anchor,row.at("anchor").To<Map>())){existing=String(row,"id");break;}}return {{"anchor",anchor},{"point",found.at("point")},{"pointId",existing},{"axis",ToolAxisState(scene,path,&anchor)}};
}
Map EditToolAxisPoint(Scene& scene,const Map& request)
{
 using namespace iCAX::CAM::Intent;CutFields(request,{"commandId","expected","intentId","action","pointId","anchor","direction","position","frame","transition","influence"});const auto runtime=Runtime(scene);const auto command=Identifier(request,"commandId");const auto key=iCAX::Data::to_string(command),payload=Text("EditToolAxisPoint:")+iCAX::Data::VariantSerializer::Serialize(request);if(const auto prior=runtime->Receipts.find(key);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto poseStart=std::chrono::steady_clock::now();auto poseMark=poseStart;Map timings;const auto mark=[&](const Text& name){const auto now=std::chrono::steady_clock::now();timings[name]=std::chrono::duration<double,std::milli>(now-poseMark).count();poseMark=now;};const auto id=Identifier(request,"intentId");const auto terminal=ReadAxisTerminalPath(scene,id);if(terminal.Locked)throw std::invalid_argument("CAM_CUT_LOCKED");const auto path=ReadAxisAuthoringPath(scene,id);const auto action=String(request,"action"),pointId=String(request,"pointId");if(action!="Upsert"&&action!="Reset"&&action!="Delete")throw std::invalid_argument("CAM_POSE_POINT_ACTION_INVALID");if((pointId=="start"||pointId=="end")&&action=="Delete")throw std::invalid_argument("CAM_POSE_ENDPOINT_NOT_DELETABLE");const auto controls=AxisActiveControls(path);Map anchor,previous;Text storedId=pointId;
 if(pointId=="start"||pointId=="end"){const auto endpoints=AxisEndpoints(scene,path);anchor=pointId=="start"?endpoints.first:endpoints.second;storedId=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());}for(const auto& value:controls){const auto point=value.To<Map>();if(String(point,"kind")!="Control")continue;if(String(point,"id")==pointId||!anchor.empty()&&SameAxisAnchor(path,anchor,point.at("anchor").To<Map>())){anchor=AxisCurrentAnchor(path,point.at("anchor").To<Map>());storedId=String(point,"id");previous=point;break;}}if(anchor.empty()&&action=="Upsert"){(void)Identifier(request,"pointId");anchor=AxisCurrentAnchor(path,request.at("anchor").To<Map>());}if(anchor.empty())throw std::invalid_argument("CAM_POSE_KEYPOINT_NOT_FOUND");
 mark("readAuthoring");const auto selectedBefore=ToolAxisState(scene,path,&anchor);mark("selectedAxis");const auto metric=request.contains("transition")?String(request,"transition"):AxisPolicy(path).contains("transitionMetric")?String(AxisPolicy(path),"transitionMetric"):Text("ArcLength");if(metric!="ArcLength"&&metric!="DirectionChange")throw std::invalid_argument("CAM_POSE_TRANSITION_INVALID");Array edits;if(path.Value.Geometry.Meaning=="ToolCenter"&&(action=="Upsert"||!previous.empty()))throw std::invalid_argument("CAM_POSE_MANUAL_KERF_UNSUPPORTED");
 if(action=="Delete"||action=="Reset"){
  if(previous.empty()){Map result{{"changed",false},{"axis",ToolAxisState(scene,path)},{"selectedAxis",selectedBefore},{"points",AxisPointList(scene,path)},{"pointId",pointId},{"statePatch",AxisPointPatch(scene,path,false)}};runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;}
  const auto commands=DeferredAxisCommands(scene,terminal,path,{},storedId,command);const auto saved=CommitDeferredAxis(scene,commands,terminal,path,"删除刀轴关键点");
  auto selected=action=="Delete"?ToolAxisState(scene,saved):ToolAxisState(scene,saved,&anchor);selected["deferred"]=true;
  Map result{{"changed",true},{"deferred",true},{"selectedAxis",selected},{"points",AxisPointList(scene,saved)},{"pointId",pointId},{"statePatch",AxisPointPatch(scene,path,true)}};runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;
 }
 const auto direction=action=="Reset"?AxisUnit(selectedBefore.at("baseDirection")):AxisToWorkpiece(scene,path,request.at("direction"),String(request,"frame"));Map point{{"id",storedId},{"kind",Text("Control")},{"set",Text("active")},{"boundaryPolicy",Text("Duplicate")},{"anchor",anchor},{"direction",GenPoint(direction)},{"transition",metric}};
 if(previous.contains("positionOffset"))point["positionOffset"]=previous.at("positionOffset");
 if(request.contains("position")){
  auto position=RegistrationPoint(request.at("position"));if(String(request,"frame")=="Setup"){
   const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),ReadFiveAxisIntentMetadata(path.Value).PlacementID);const auto m=RigidMatrix(placement->GetTransform());const auto p=position;
   for(int i=0;i<3;++i){position[i]=0;for(int j=0;j<3;++j)position[i]+=m[j*4+i]*(p[j]-m[j*4+3]);}
  }
  const auto exact=IntentExactCurve(path.Value);const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*service);const auto at=AxisLocation(path,anchor);const auto base=GenVec(evaluator.At(exact.Segments.at(at.first),at.second).OffsetPoint);
  for(int i=0;i<3;++i)position[i]-=base[i];point["positionOffset"]=GenPoint(position);
 }
 Map oldDriver=previous.contains("driver")?previous.at("driver").To<Map>():Map{};Array targets;Text interpolation=oldDriver.empty()?Text("Smooth"):String(oldDriver,"interpolation");if(request.contains("influence")){const auto supplied=request.at("influence").To<Map>();CutFields(supplied,{"targets","interpolation"});targets=supplied.at("targets").To<Array>();interpolation=String(supplied,"interpolation");}else if(!oldDriver.empty())for(const auto& value:controls){const auto target=value.To<Map>();if(String(target,"kind")=="InfluenceTarget"&&target.contains("driver")&&String(target.at("driver").To<Map>(),"id")==storedId)targets.emplace_back(Map{{"anchor",AxisCurrentAnchor(path,target.at("anchor").To<Map>())},{"weight",target.at("weight")}});}
 if(targets.size()>256)throw std::invalid_argument("CAM_POSE_INFLUENCE_BUDGET");if(interpolation!="Linear"&&interpolation!="Smooth")throw std::invalid_argument("CAM_POSE_INFLUENCE_CURVE_INVALID");Map driver{{"id",storedId},{"referenceDirection",oldDriver.empty()?selectedBefore.at("direction"):oldDriver.at("referenceDirection")},{"direction",GenPoint(direction)},{"transition",metric},{"interpolation",interpolation}};if(!targets.empty())point["driver"]=driver;edits.emplace_back(point);
 for(std::size_t i=0;i<targets.size();++i){const auto supplied=targets[i].To<Map>();CutFields(supplied,{"anchor","weight"});const auto targetAnchor=AxisCurrentAnchor(path,supplied.at("anchor").To<Map>());const auto weight=RegistrationNumber(supplied.at("weight"));if(weight<0||weight>1)throw std::invalid_argument("CAM_POSE_INFLUENCE_WEIGHT_INVALID");if(SameAxisAnchor(path,anchor,targetAnchor))throw std::invalid_argument("CAM_POSE_INFLUENCE_OWNER_SELECTED");for(const auto& prior:edits)if(SameAxisAnchor(path,prior.To<Map>().at("anchor").To<Map>(),targetAnchor))throw std::invalid_argument("CAM_POSE_INFLUENCE_OVERLAP");Variant reference=ToolAxisState(scene,path,&targetAnchor).at("direction");Text targetId=storedId+"/target/"+std::to_string(i);for(const auto& existing:controls){const auto old=existing.To<Map>();if(String(old,"kind")!="InfluenceTarget"||!old.contains("driver")||String(old.at("driver").To<Map>(),"id")!=storedId||!SameAxisAnchor(path,targetAnchor,old.at("anchor").To<Map>()))continue;reference=old.at("referenceDirection");targetId=String(old,"id");}edits.emplace_back(Map{{"id",targetId},{"kind",Text("InfluenceTarget")},{"set",Text("active")},{"boundaryPolicy",Text("Duplicate")},{"anchor",targetAnchor},{"referenceDirection",reference},{"weight",weight},{"driver",driver}});}
 for(const auto& item:controls){const auto old=item.To<Map>();if(!old.contains("anchor")||String(old,"id")==storedId||old.contains("driver")&&String(old.at("driver").To<Map>(),"id")==storedId)continue;
  if(SameAxisAnchor(path,anchor,old.at("anchor").To<Map>())){
   if(String(old,"kind")=="InfluenceTarget")throw std::invalid_argument("CAM_POSE_INFLUENCED_POINT");
   if(String(old,"kind")=="Control")throw std::invalid_argument("CAM_POSE_DUPLICATE_KEYPOINT");
  }
  for(const auto& edit:edits){const auto target=edit.To<Map>();if(String(target,"kind")!="InfluenceTarget"||!SameAxisAnchor(path,target.at("anchor").To<Map>(),old.at("anchor").To<Map>()))continue;
   if(String(old,"kind")=="InfluenceTarget")throw std::invalid_argument("CAM_POSE_INFLUENCE_OVERLAP");
   if(String(old,"kind")=="Control"&&old.contains("driver"))throw std::invalid_argument("CAM_POSE_INFLUENCE_OWNER_SELECTED");
  }
 }
 if(!previous.empty()&&!previous.contains("driver")&&targets.empty()&&String(previous,"transition")==metric){const auto before=AxisUnit(previous.at("direction"));double delta=0;for(int i=0;i<3;++i)delta=std::max(delta,std::abs(before[i]-direction[i]));if(delta<1e-12&&(!point.contains("positionOffset")&&!previous.contains("positionOffset")||point.contains("positionOffset")&&previous.contains("positionOffset")&&point.at("positionOffset")==previous.at("positionOffset"))){Map result{{"changed",false},{"deferred",terminal.ResultState!="Current"},{"selectedAxis",selectedBefore},{"points",AxisPointList(scene,path)},{"pointId",pointId},{"statePatch",AxisPointPatch(scene,path,false)}};runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;}}
 mark("edits");const auto commands=DeferredAxisCommands(scene,terminal,path,edits,{},command);mark("commands");const auto saved=CommitDeferredAxis(scene,commands,terminal,path,"编辑刀轴关键点");mark("commit");
 auto selectedAxis=selectedBefore;selectedAxis["direction"]=GenPoint(direction);const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),ReadFiveAxisIntentMetadata(path.Value).PlacementID);const auto matrix=RigidMatrix(placement->GetTransform());Array setupDirection;
 for(int i=0;i<3;++i){double value=0;for(int j=0;j<3;++j)value+=matrix[i*4+j]*direction[j];setupDirection.emplace_back(value);}selectedAxis["setupDirection"]=setupDirection;selectedAxis["deferred"]=true;
 const auto selectedPosition=AxisPointPosition(scene,saved,anchor);selectedAxis["point"]=selectedPosition.at("point");selectedAxis["setupPoint"]=selectedPosition.at("setupPoint");
 mark("readback");Map result{{"changed",true},{"deferred",true},{"selectedAxis",selectedAxis},{"points",AxisPointList(scene,saved)},{"pointId",pointId},{"statePatch",AxisPointPatch(scene,path,true)}};mark("response");result["timingsMs"]=timings;runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;
}
