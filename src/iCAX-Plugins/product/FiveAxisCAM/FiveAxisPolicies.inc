namespace
{
    Map IntentAuthoredProcessForRecovery(Scene&,const iCAX::CAM::Intent::SResolvedPath&,Map);
    Map CutResourceBody(Scene& scene,const Ref& ref,const Text& schema)
    {
        const auto body=scene.Resources().Get<iCAX::Resource::CBinaryResource>(ref.URL,ref.nVersion);
        if(!body)throw std::invalid_argument("CAM_LEGACY_RESOURCE_MISSING");
        const auto value=iCAX::Data::VariantSerializer::Deserialize(Text(body->Content.begin(),body->Content.end())).To<Map>();
        if(String(value,"schema")!=schema||String(value,"schemaVersion")!="1")throw std::invalid_argument("CAM_LEGACY_SCHEMA_MISMATCH");return value;
    }
    void ValidatePolicyClosure(Scene&,const Map& data){(void)EngineeringBody(data);}
    void ExactPolicyDependencies(Scene&,const Map& data,const std::vector<Map>& expected){
        const auto body=EngineeringBody(data);if(String(body,"schema")=="cam.process-assignment"&&(expected.size()!=1||body.at("libraryData").To<Map>()!=expected.front()))throw std::invalid_argument("CAM_POLICY_DEPENDENCY_MISMATCH");
    }
    // Numeric process descriptors have fixed units. No laser/gas/time defaults
    // are invented for a newly authored, unqualified process.
    struct ProcessParameter {const char* ID;const char* Unit;double Minimum;bool Strict;};
    constexpr ProcessParameter ProcessParameters[]={
        {"feedMmPerSecond","mm/s",0,true},{"laserPowerW","W",0,true},{"gasPressureBar","bar",0,false},
        {"standOffMm","mm",0,false},{"focusOffsetMm","mm",std::numeric_limits<double>::lowest(),false},{"pierceTimeSeconds","s",0,false},
        {"pierceFootprintRadiusMm","mm",0,false}};
    const ProcessParameter& ProcessDescriptor(const Text& id)
    {
        for(const auto& p:ProcessParameters)if(id==p.ID)return p;
        throw std::invalid_argument("CAM_PROCESS_PARAMETER_UNKNOWN: "+id);
    }
    void ValidateProcessValue(const Text& id,const iCAX::Data::Variant& value)
    {
        const auto& p=ProcessDescriptor(id);const auto number=RegistrationNumber(value);
        if(number<p.Minimum||(p.Strict&&number==p.Minimum))throw std::invalid_argument("CAM_PROCESS_PARAMETER_RANGE: "+id);
    }
    void ValidateProcessLibrary(const Map& library)
    {
        CutFields(library,{"schema","schemaVersion","name","productionQualified","material","thicknessMm","gas","parameters","defaults"});
        if(String(library,"name").empty()||String(library,"name").size()>256)throw std::invalid_argument("CAM_PROCESS_NAME_REQUIRED");
        if(!library.at("productionQualified").Is<bool>()||library.at("productionQualified").To<bool>())
            throw std::invalid_argument("CAM_PROCESS_QUALIFICATION_EVIDENCE_REQUIRED");
        for(const auto* key:{"material","gas"})if(library.contains(key)&&library.at(key).To<Text>().size()>256)throw std::invalid_argument("CAM_PROCESS_LABEL_INVALID");
        if(library.contains("thicknessMm")&&RegistrationNumber(library.at("thicknessMm"))<=0)throw std::invalid_argument("CAM_PROCESS_THICKNESS_INVALID");
        if(library.contains("parameters"))for(const auto& [id,value]:library.at("parameters").To<Map>())ValidateProcessValue(id,value);
    }
    void ValidateAssignmentValues(const Map& assignment,const Map& library,bool hasLibrary=true)
    {
        CutFields(assignment,{"schema","schemaVersion","libraryData","overrides","kerf","leadIn","leadOut","overcutLengthMm","gapLengthMm","layerKey"});
        if(hasLibrary)(void)assignment.at("libraryData").To<Map>();
        std::set<Text> parameters;
        for(const auto& v:assignment.at("overrides").To<Array>())
        {
            const auto o=v.To<Map>();CutFields(o,{"parameterId","value"});const auto id=String(o,"parameterId");
            if(!parameters.insert(id).second)throw std::invalid_argument("CAM_PROCESS_OVERRIDE_DUPLICATE");
            if(!library.contains("parameters")||!library.at("parameters").To<Map>().contains(id))throw std::invalid_argument("CAM_PROCESS_OVERRIDE_NOT_DECLARED");
            ValidateProcessValue(id,o.at("value"));
        }
        const auto kerf=assignment.at("kerf").To<Map>();
        CutFields(kerf,{"owner","widthMm","miterLimit","measurementSection","cornerMode"});
        if(String(kerf,"owner")!="Cam"&&String(kerf,"owner")!="Executor")throw std::invalid_argument("CAM_KERF_OWNER_INVALID");
        if(RegistrationNumber(kerf.at("widthMm"))<0||RegistrationNumber(kerf.at("miterLimit"))<1)throw std::invalid_argument("CAM_KERF_PARAMETERS_INVALID");
        if(String(kerf,"measurementSection")!="Surface"&&String(kerf,"measurementSection")!="BeamNormalSection"&&String(kerf,"measurementSection")!="ThicknessSection")throw std::invalid_argument("CAM_MEASUREMENT_SECTION_INVALID");
        if(String(kerf,"cornerMode")!="Miter"&&String(kerf,"cornerMode")!="Round"&&String(kerf,"cornerMode")!="Bevel")throw std::invalid_argument("CAM_KERF_CORNER_INVALID");
        for(const auto* key:{"leadIn","leadOut"})
        {
            const auto lead=assignment.at(key).To<Map>();const auto kind=String(lead,"kind");
            CutFields(lead,{"kind","placement","lengthMm","angleRad","radiusMm"});
            const auto length=RegistrationNumber(lead.at("lengthMm"));(void)RegistrationNumber(lead.at("angleRad"));
            if((kind!="None"&&kind!="Line"&&kind!="Arc")||length<0||(kind=="None"&&length!=0)||(kind!="None"&&length==0))throw std::invalid_argument("CAM_LEAD_INVALID");
            if(std::abs(RegistrationNumber(lead.at("angleRad")))>3.141592653589793)throw std::invalid_argument("CAM_LEAD_ANGLE_INVALID");
            if(String(lead,"placement")!="ScrapSide"&&String(lead,"placement")!="OutsideStock")throw std::invalid_argument("CAM_LEAD_PLACEMENT_INVALID");
            if(kind=="Arc"&&RegistrationNumber(lead.at("radiusMm"))<=0)throw std::invalid_argument("CAM_LEAD_RADIUS_INVALID");
            if(kind!="Arc"&&lead.contains("radiusMm"))throw std::invalid_argument("CAM_LEAD_RADIUS_NOT_APPLICABLE");
        }
        const auto overcut=RegistrationNumber(assignment.at("overcutLengthMm")),gap=RegistrationNumber(assignment.at("gapLengthMm"));
        if(overcut<0||gap<0||overcut>100000||gap>100000)throw std::invalid_argument("CAM_LEAD_CLOSURE_LENGTH_INVALID");
        if(overcut>0&&gap>0)throw std::invalid_argument("CAM_LEAD_CLOSURE_CONFLICT");
        if(String(assignment,"layerKey").empty())throw std::invalid_argument("CAM_PROCESS_LAYER_REQUIRED");
    }
    bool FixedToolAxis(const Map& policy)
    {return String(policy,"mode")=="FixedDirection"||(String(policy,"mode")=="Continuous"&&String(policy,"initialization")=="FixedDirection");}
    void ValidatePoseValues(const Map& policy)
    {
        if(String(policy,"mode")=="FixedDirection") {
            CutFields(policy,{"schema","schemaVersion","mode","frame","direction"});
            if(String(policy,"frame")!="Workpiece")throw std::invalid_argument("CAM_POSE_FRAME_INVALID");
            const auto direction=RegistrationPoint(policy.at("direction"));double norm=0;
            for(const auto v:direction)norm+=v*v;
            if(std::abs(norm-1)>1e-10)throw std::invalid_argument("CAM_POSE_DIRECTION_INVALID");
            return;
        }
        CutFields(policy,{"schema","schemaVersion","mode","initialization","transitionMetric","leadAngleRad","sideAngleRad", "leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight","keypoints","direction","axisReversed","controlsSet"});
        if(policy.contains("axisReversed"))(void)policy.at("axisReversed").To<bool>();
        if(String(policy,"mode")!="Continuous")throw std::invalid_argument("CAM_INITIAL_FIXED_ROTARY_NOT_IMPLEMENTED");
        const auto initialization=String(policy,"initialization"),metric=String(policy,"transitionMetric");
        if(initialization!="SurfaceNormal"&&initialization!="TangentInSurface"&&initialization!="CrossTangentInSurface"&&initialization!="FixedDirection")throw std::invalid_argument("CAM_POSE_INITIALIZATION_INVALID");
        if(initialization=="FixedDirection"){
            const auto direction=RegistrationPoint(policy.at("direction"));double norm=0;for(auto v:direction)norm+=v*v;
            if(std::abs(norm-1)>1e-10||RegistrationNumber(policy.at("leadAngleRad"))!=0||RegistrationNumber(policy.at("sideAngleRad"))!=0)throw std::invalid_argument("CAM_POSE_DIRECTION_INVALID");
        }else if(policy.contains("direction"))throw std::invalid_argument("CAM_POSE_DIRECTION_NOT_APPLICABLE");
        if(metric!="ArcLength"&&metric!="DirectionChange")throw std::invalid_argument("CAM_POSE_TRANSITION_INVALID");
        for(const auto* key:{"leadAngleRad","sideAngleRad"})if(std::abs(RegistrationNumber(policy.at(key)))>3.141592653589793)throw std::invalid_argument("CAM_POSE_ANGLE_INVALID");
        for(const auto* key:{"leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad"})
        {const auto number=RegistrationNumber(policy.at(key));if(number<0||number>3.141592653589793)throw std::invalid_argument("CAM_POSE_LIMIT_INVALID");}
        const auto weight=RegistrationNumber(policy.at("surfaceNormalPreferenceWeight"));
        if(weight<0||weight>1)throw std::invalid_argument("CAM_INITIAL_POSE_POLICY_INVALID");
        const auto points=policy.contains("keypoints")?policy.at("keypoints").To<Array>():Array{};if(points.size()>256)throw std::invalid_argument("CAM_POSE_KEYPOINT_BUDGET");std::set<Text> ids;
        for(const auto& value:points){const auto p=value.To<Map>();CutFields(p,{"id","anchor","direction","transition","influence"});
            const auto id=String(p,"id");if(id.empty()||id.size()>128||!ids.insert(id).second)throw std::invalid_argument("CAM_POSE_KEYPOINT_ID_INVALID");
            const auto anchor=p.at("anchor").To<Map>();CutFields(anchor,{"sourceSpanId","nativeParameter"});if(String(anchor,"sourceSpanId").empty())throw std::invalid_argument("CAM_POSE_KEYPOINT_ANCHOR_INVALID");(void)RegistrationNumber(anchor.at("nativeParameter"));
            const auto direction=RegistrationPoint(p.at("direction"));double norm=0;for(auto x:direction)norm+=x*x;if(std::abs(norm-1)>1e-10)throw std::invalid_argument("CAM_POSE_DIRECTION_INVALID");
            const auto transition=String(p,"transition");if(transition!="ArcLength"&&transition!="DirectionChange")throw std::invalid_argument("CAM_POSE_TRANSITION_INVALID");
            if(p.contains("influence")){
                const auto influence=p.at("influence").To<Map>();CutFields(influence,{"referenceDirection","targets","interpolation"});
                const auto interpolation=String(influence,"interpolation");if(interpolation!="Linear"&&interpolation!="Smooth")throw std::invalid_argument("CAM_POSE_INFLUENCE_CURVE_INVALID");
                const auto validDirection=[](const Variant& v){const auto d=RegistrationPoint(v);if(std::abs(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]-1)>1e-10)throw std::invalid_argument("CAM_POSE_DIRECTION_INVALID");};
                validDirection(influence.at("referenceDirection"));const auto targets=influence.at("targets").To<Array>();if(targets.size()>256)throw std::invalid_argument("CAM_POSE_INFLUENCE_BUDGET");
                for(const auto& target:targets){const auto t=target.To<Map>();CutFields(t,{"anchor","referenceDirection","weight"});
                    const auto a=t.at("anchor").To<Map>();CutFields(a,{"sourceSpanId","nativeParameter"});if(String(a,"sourceSpanId").empty())throw std::invalid_argument("CAM_POSE_KEYPOINT_ANCHOR_INVALID");(void)RegistrationNumber(a.at("nativeParameter"));
                    validDirection(t.at("referenceDirection"));const auto w=RegistrationNumber(t.at("weight"));if(w<0||w>1)throw std::invalid_argument("CAM_POSE_INFLUENCE_WEIGHT_INVALID");
                }
            }
        }
    }
    void ValidateLibraryDefaults(const Map& library)
    {
        if(!library.contains("defaults"))return; // Missing categories cannot be restored or guessed.
        const auto defaults=library.at("defaults").To<Map>();
        CutFields(defaults,{"kerf","leadIn","leadOut","overcutLengthMm","gapLengthMm","layerKey"});
        auto assignment=defaults;assignment["overrides"]=Array{};ValidateAssignmentValues(assignment,library,false);
    }
    void ValidateInitialCutPolicies(Scene& scene,const Map& process,const Map& pose)
    {
        ValidatePolicyClosure(scene,process);ValidatePolicyClosure(scene,pose);
        const auto assignment=EngineeringBody(process,"cam.process-assignment");
        const auto libraryRef=assignment.at("libraryData").To<Map>();
        const auto library=EngineeringBody(libraryRef,"cam.process-library");
        ExactPolicyDependencies(scene,process,{libraryRef});ExactPolicyDependencies(scene,libraryRef,{});ExactPolicyDependencies(scene,pose,{});
        ValidateProcessLibrary(library);ValidateLibraryDefaults(library);ValidateAssignmentValues(assignment,library);
        ValidatePoseValues(EngineeringBody(pose,"cam.pose-policy"));
    }
    Map ProcessReadiness(const Map& library)
    {
        Array missing;
        for(const auto* key:{"material","gas"})if(!library.contains(key)||library.at(key).To<Text>().empty())missing.emplace_back(Text(key));
        if(!library.contains("thicknessMm"))missing.emplace_back(Text("thicknessMm"));
        const auto values=library.contains("parameters")?library.at("parameters").To<Map>():Map{};
        for(const auto& p:ProcessParameters)if(!values.contains(p.ID))missing.emplace_back(Text(p.ID));
        return {{"status",Text(missing.empty()?"Unqualified":"Incomplete")},{"missingParameters",missing},{"productionQualified",false}};
    }
}

// Publishes candidate resources without changing the engineering model. They
// become part of its saved dependency closure only when CamCut.Create commits.
Map PrepareCutPolicies(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","placementId","library","assignment","pose"});
    const auto runtime=Runtime(scene);const auto id=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("PrepareCutPolicies:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(id);prior!=runtime->Receipts.end())
    {if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==id)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);(void)CutSource(scene,request);
    auto library=request.at("library").To<Map>(),assignment=request.at("assignment").To<Map>(),pose=request.at("pose").To<Map>();
    CutFields(library,{"name","material","thicknessMm","gas","parameters"});
    CutFields(assignment,{"overrides","kerf","leadIn","leadOut","overcutLengthMm","gapLengthMm","layerKey"});
    CutFields(pose,{"mode","initialization","transitionMetric","leadAngleRad","sideAngleRad","leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight","keypoints"});
    library["productionQualified"]=false;ValidateProcessLibrary(library);ValidateAssignmentValues(assignment,library,false);ValidatePoseValues(pose);
    if(String(pose,"mode")!="Continuous")throw std::invalid_argument("CAM_STANDARD_HOLE_REQUIRES_GEOMETRY");
    auto defaults=assignment;defaults.erase("overrides");library["defaults"]=defaults;
    library["schema"]=Text("cam.process-library");library["schemaVersion"]=Text("1");
    assignment["schema"]=Text("cam.process-assignment");assignment["schemaVersion"]=Text("1");
    pose["schema"]=Text("cam.pose-policy");pose["schemaVersion"]=Text("1");
    const auto libraryRef=EngineeringData(library);
    assignment["libraryData"]=WireRef(StoredRef(libraryRef));
    const auto process=EngineeringData(assignment);
    const auto poseRef=EngineeringData(pose);
    Map result{{"process",WireRef(StoredRef(process))},{"pose",WireRef(StoredRef(poseRef))},{"readiness",ProcessReadiness(library)}};
    runtime->Receipts.emplace(id,std::make_pair(payload,result));return result;
}
Map ReadCutPolicies(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentIds"});const auto runtime=Runtime(scene);CheckExpected(*runtime,request);
    const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    Array items;std::set<ID> unique;
    for(const auto& id:ids)
    {
        const auto entityID=Identifier(Map{{"id",id}},"id");if(!unique.insert(entityID).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
        const auto cut=ReadIntentCut(scene,entityID);
        auto process=cut->GetProcessData();const auto pose=cut->GetPoseData();
        if(cut->GetResultState()!="Current")process=EngineeringView(IntentAuthoredProcessForRecovery(scene,cut->Path,EngineeringBody(process,"cam.process-assignment")));
        ValidateInitialCutPolicies(scene,process,pose);
        const auto assignment=EngineeringBody(process,"cam.process-assignment");
        const auto library=EngineeringBody(assignment.at("libraryData").To<Map>(),"cam.process-library");
        const auto& controls=cut->Projection.PoseControls;
        items.emplace_back(Map{{"intentId",id},{"process",WireRef(process)},{"pose",WireRef(pose)},
            {"assignment",assignment},{"library",library},{"posePolicy",EngineeringBody(pose,"cam.pose-policy")},
            {"poseControls",controls.contains("active")?controls.at("active"):Variant(Array{})},{"readiness",ProcessReadiness(library)}});
    }
    return {{"items",items}};
}
Map QueryCommand(Scene& scene,const Map& request)
{
    CutFields(request,{"epoch","commandId"});const auto runtime=Runtime(scene);
    if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_MISMATCH");
    const auto id=iCAX::Data::to_string(Identifier(request,"commandId"));
    if(const auto found=runtime->Receipts.find(id);found!=runtime->Receipts.end())return {{"status",Text("Completed")},{"result",found->second.second}};
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==id)return TaskState(scene,*runtime,task);
    return {{"status",Text("NotFound")}};
}

