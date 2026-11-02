// Setup lifecycle and locating-point records belong to the Scene, not the
// neutral geometry/snap services. Each setup references shared model geometry.
namespace {
    UInt SetupInsertionSequence(Scene& scene,const ID& workpieceID){
        UInt last=0;for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetWorkpieceID()==workpieceID&&!p->GetSetupID().is_nil())last=std::max(last,Require<CFiveAxisSetupComponent>(scene.Database(),p->GetSetupID())->GetSequence());return last?Next(last):NextSetupSequence(scene);
    }
    void ShiftSetupSequences(Scene& scene,iCAX::Database::ITransaction& tx,UInt insertion){
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto s=Component<CFiveAxisSetupComponent>(e);s&&s->GetSequence()>=insertion)tx.ModifyComponent(e->GetID(),CFiveAxisSetupComponent::S_ClassName,{{"Sequence",Next(s->GetSequence())}});
    }
    void SetupIdle(Scene& scene) {
        for(const auto& [_,task]:Runtime(scene)->Tasks)
            if(task.Status=="Running"&&task.Kind!="RapidPreview")throw std::runtime_error("CAM_SETUP_BUSY");
    }
    Map RegistrationPlanState(Scene& scene,const ID& setupID) {
        auto plan=Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetRegistrationPlan();
        if(plan.empty())return {{"status",Text("Missing")}};
        const auto id=Identifier(plan,"placementId");
        const auto entity=scene.Database().GetEntity(id);
        const auto p=Component<CFiveAxisPlacementComponent>(entity);
        if(!p||p->GetSetupID()!=setupID){plan["status"]=Text("Stale");return plan;}
        const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
        if(plan.at("geometryRef").To<Map>()!=WireRef(w->GetGeometryRef())||plan.at("placementTransform").To<Array>()!=p->GetTransform())plan["status"]=Text("Stale");
        return plan;
    }
}
Map ManageSetups(Scene& scene,const Map& request) {
    CutFields(request,{"commandId","expected","placementId","action","name"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("ManageSetups:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto it=runtime->Receipts.find(commandID);it!=runtime->Receipts.end()){
        if(it->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return it->second.second;
    }
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    auto& db=scene.Database();const auto id=Identifier(request,"placementId");const auto action=String(request,"action");
    const auto p=Require<CFiveAxisPlacementComponent>(db,id);const auto setupID=p->GetSetupID();
    const auto s=Component<CFiveAxisSetupComponent>(db.GetEntity(setupID));
    const auto w=Require<CFiveAxisWorkpieceComponent>(db,p->GetWorkpieceID());
    const auto jobID=db.GetMetaEntity()->GetID();
    if(action!="create"&&!s)throw std::invalid_argument("CAM_SETUP_REQUIRED: 请先新增装夹。");
    ID nextPlacement=id,nextSetup=setupID;
    std::vector<ID> ownedCuts,ownedFixtures;
    if(action=="create"){
        if(!w->GetEnabled())throw std::invalid_argument("CAM_WORKPIECE_DISABLED");
        if(s)RequireSetupWorkpieceEnabled(scene,setupID);
        nextSetup=iCAX::Data::GenerateNewUUID();nextPlacement=s?iCAX::Data::GenerateNewUUID():id;
        auto name=String(request,"name");if(name.empty())throw std::invalid_argument("CAM_SETUP_NAME_REQUIRED");
        const auto defaultFixtures=PrepareDefaultFixtures(scene);
        const auto profile=s?s->GetPlanningProfileData():StoredRef(DefaultPlanningProfile(scene));
        const auto insertion=SetupInsertionSequence(scene,p->GetWorkpieceID());
        const auto nextPlan=iCAX::Data::GenerateNewUUID();
        const std::vector<Map> commands{{{"kind",Text("CreatePlan")},{"commandId",Identifier(request,"commandId")},{"planId",nextPlan},{"name",name}}};
        CommitIntentHostCommands(scene,commands,[&](auto& tx,const auto&){
            ShiftSetupSequences(scene,tx,insertion);
            tx.CreateEntity(nextSetup);tx.AttachComponent(nextSetup,CFiveAxisSetupComponent::S_ClassName,
                {{"JobID",jobID},{"Name",name},{"Sequence",insertion},{"IntentPlanID",nextPlan},{"PlanningProfileData",profile},{"RapidSettings",s?s->GetRapidSettings():Map()},{"HeadInstanceID",s?s->GetHeadInstanceID():ID()}});
            if(s){tx.CreateEntity(nextPlacement);tx.AttachComponent(nextPlacement,CFiveAxisPlacementComponent::S_ClassName,
                {{"SetupID",nextSetup},{"WorkpieceID",p->GetWorkpieceID()},{"Transform",p->GetTransform()}});
            tx.AttachComponent(nextPlacement,iCAX::Transform::CTransformComponent::S_ClassName,{});
            const auto preview=ReadWireRef(WireRef(w->GetPreviewRef()));
            tx.AttachComponent(nextPlacement,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                {{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
            }else tx.ModifyComponent(id,CFiveAxisPlacementComponent::S_ClassName,{{"SetupID",nextSetup},{"RegistrationData",Map()},{"RegistrationMethod",Text()},{"ApplicationMode",Text("Pending")},{"InputRevision",Next(p->GetInputRevision())}});
            tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"DefaultSetupID",nextSetup},{"ActiveSetupID",nextSetup},{"DefaultPlacementID",nextPlacement},{"ActivePlacementID",nextPlacement}});
            AttachLibraryFixtures(tx,nextSetup,defaultFixtures);
        },"新增装夹",{nextSetup});
        ProjectPlacement(scene,nextPlacement,p->GetTransform());
    }else if(action=="rename"){
        auto name=String(request,"name");if(name.empty())throw std::invalid_argument("CAM_SETUP_NAME_REQUIRED");
        Transaction(db,"重命名装夹",[&](auto& tx){tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"Name",name}});});
    }else if(action=="enable"||action=="disable"){
        Transaction(db,action=="enable"?"启用装夹":"禁用装夹",[&](auto& tx){tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"Enabled",action=="enable"}});});
    }else if(action=="remove"){
        RequireSetupUnlocked(scene,setupID);nextPlacement=ID();
        for(const auto& e:db.FilterEntities([](auto){return true;})){
            if(const auto other=Component<CFiveAxisPlacementComponent>(e);other&&other->GetWorkpieceID()==p->GetWorkpieceID()&&e->GetID()!=id){nextPlacement=e->GetID();nextSetup=other->GetSetupID();}
            if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==setupID)ownedFixtures.push_back(e->GetID());
            if(const auto u=Component<CFiveAxisFixtureUnitComponent>(e);u&&u->GetSetupID()==setupID)ownedFixtures.push_back(e->GetID());
        }
        const bool last=nextPlacement.is_nil();if(last){nextPlacement=id;nextSetup=ID();}
        const auto retiredPlan=IntentPlan(scene,setupID);const auto commands=RetireIntentPlanCommands(scene,setupID,Identifier(request,"commandId"));
        CommitIntentHostCommands(scene,commands,[&](auto& tx,const auto&){
            for(const auto& e:ownedFixtures)tx.DisposeEntity(e);
            if(last){
                // Keep the independent workpiece visible; there is no setup.
                tx.ModifyComponent(id,CFiveAxisPlacementComponent::S_ClassName,
                    {{"SetupID",ID()},{"RegistrationData",Map()},{"RegistrationMethod",Text()},{"ApplicationMode",Text("Pending")},{"InputRevision",Next(p->GetInputRevision())}});
            }else tx.DisposeEntity(id);
            tx.DisposeEntity(setupID);
            tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"DefaultSetupID",nextSetup},{"DefaultPlacementID",nextPlacement}});
        },"删除装夹");
        if(const auto generation=runtime->IntentGenerations.find(retiredPlan);generation!=runtime->IntentGenerations.end())
        {generation->second->Cancel();runtime->IntentGenerations.erase(generation);}
        if(last)ProjectPlacement(scene,nextPlacement,p->GetTransform());
    }else throw std::invalid_argument("CAM_SETUP_ACTION");
    Map result{{"placementId",iCAX::Data::to_string(nextPlacement)},{"setupId",OptionalID(nextSetup)},{"workpieceId",iCAX::Data::to_string(p->GetWorkpieceID())}};
    runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
}
Map SetSetupSequence(Scene& scene,const Map& request) {
    CutFields(request,{"commandId","expected","setupIds"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetSetupSequence:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto it=runtime->Receipts.find(commandID);it!=runtime->Receipts.end()){
        if(it->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return it->second.second;
    }
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    auto& db=scene.Database();const auto jobID=db.GetMetaEntity()->GetID();
    const auto job=Require<CFiveAxisJobComponent>(db,jobID);
    std::set<ID> remaining;
    for(const auto& e:db.FilterEntities([](auto){return true;}))
        if(const auto s=Component<CFiveAxisSetupComponent>(e);s&&s->GetJobID()==jobID)remaining.insert(e->GetID());
    const auto order=request.at("setupIds").To<Array>();std::vector<ID> ids;
    for(const auto& value:order){
        const auto parsed=ID::from_string(value.To<Text>());
        if(!parsed||!remaining.erase(*parsed))throw std::invalid_argument("CAM_SETUP_SEQUENCE_INVALID");
        ids.push_back(*parsed);
    }
    if(!remaining.empty())throw std::invalid_argument("CAM_SETUP_SEQUENCE_INCOMPLETE");
    UInt previousWorkpieceOrder=0;
    for(const auto& setupID:ids)for(const auto& e:db.FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==setupID){const auto order=Require<CFiveAxisWorkpieceComponent>(db,p->GetWorkpieceID())->GetListOrder();if(order<previousWorkpieceOrder)throw std::invalid_argument("CAM_SETUP_SEQUENCE_WORKPIECE_GROUP: 装夹姿态只能在所属工件内排序。");previousWorkpieceOrder=order;}
    Transaction(db,"调整装夹工序顺序",[&](auto& tx){
        for(size_t i=0;i<ids.size();++i)tx.ModifyComponent(ids[i],CFiveAxisSetupComponent::S_ClassName,{{"Sequence",static_cast<UInt>(i+1)}});
        tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"InputRevision",Next(job->GetInputRevision())}});
    });
    Map result{{"setupIds",order}};runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
}
Map SetRegistrationPlan(Scene& scene,const Map& request) {
    CutFields(request,{"commandId","expected","placementId","queries","measuredPoints","maximumResidualMm"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetRegistrationPlan:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto it=runtime->Receipts.find(commandID);it!=runtime->Receipts.end()){
        if(it->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return it->second.second;
    }
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    const auto id=Identifier(request,"placementId");const auto p=Require<CFiveAxisPlacementComponent>(scene.Database(),id);
    RequireSetupWorkpieceEnabled(scene,p->GetSetupID());
    const auto object=ReadPickObject(scene,id,p->GetSetupID());const auto queries=request.at("queries").To<Array>();
    if(queries.size()!=3)throw std::invalid_argument("CAM_LOCATING_THREE_POINTS_REQUIRED: 请选择三个特征点。");
    Array points;std::array<AlignVector,3> positions;
    for(size_t i=0;i<3;++i){
        const auto q=queries[i].To<Map>();const auto mode=String(q,"mode");
        if(String(q,"frame")!="Workpiece"||!(mode=="point"||mode=="endpoint"||mode=="midpoint"||mode=="center"||mode=="sphere-center"||mode=="datum-point"))throw std::invalid_argument("CAM_LOCATING_POINT_REQUIRED");
        const auto exact=PickReference(scene,object,q);positions[i]=AlignMap(object.Transform,RegistrationPoint(exact.at("point")));
        points.emplace_back(Map{{"label",Text("P")+std::to_string(i+1)},{"query",exact.at("query")},{"modelPoint",exact.at("point")},
            {"point",Array{positions[i][0],positions[i][1],positions[i][2]}}});
    }
    auto a=AlignAdd(positions[1],positions[0],-1),b=AlignAdd(positions[2],positions[0],-1),c=AlignAdd(positions[2],positions[1],-1);
    const double minimumDistance=std::sqrt(std::min({AlignDot(a,a),AlignDot(b,b),AlignDot(c,c)}));
    const auto cross=AlignCross(a,b);const double condition=minimumDistance>1e-6?std::sqrt(AlignDot(cross,cross)/(AlignDot(a,a)*AlignDot(b,b))):0.;
    if(minimumDistance<.001||condition<.001)throw std::invalid_argument("CAM_LOCATING_COLLINEAR: 三点不能重合或共线，请重新选择分散的特征点。");
    Map plan{{"status",Text("Prepared")},{"machineStatus",Text("Unmeasured")},{"placementId",iCAX::Data::to_string(id)},
        {"setupId",iCAX::Data::to_string(p->GetSetupID())},{"unit",Text("mm")},{"coordinateFrame",Text("Setup")},
        {"geometryRef",WireRef(object.GeometryRef)},{"placementTransform",p->GetTransform()},{"points",points},
        {"minimumDistanceMm",minimumDistance},{"conditionRatio",condition}};
    if(request.contains("measuredPoints")){
        using Request=iCAX::Geometry::SRigidRegistrationRequest;
        Request fit;fit.Kind=Request::Method::PairedPoints;
        fit.MaximumResidualMm=RegistrationNumber(request.at("maximumResidualMm"));
        const auto measured=request.at("measuredPoints").To<Array>();
        if(measured.size()!=3)throw std::invalid_argument("CAM_LOCATING_THREE_MEASUREMENTS_REQUIRED: 请填写 P1、P2、P3 对应的机床坐标。");
        for(size_t i=0;i<3;++i){fit.ModelPoints.push_back(positions[i]);fit.MeasuredPoints.push_back(RegistrationPoint(measured[i]));}
        const auto service=scene.Services().Resolve<iCAX::Geometry::IRigidRegistrationService>();
        if(!service)throw std::runtime_error("CAM_REGISTRATION_SERVICE_MISSING");
        const auto solution=service->Solve(fit);
        if(!solution.Qualified)throw std::invalid_argument("CAM_LOCATING_RESIDUAL_EXCEEDED: 最大配对误差 "+std::to_string(solution.MaximumResidualMm)+" mm，超过允许值 "+std::to_string(fit.MaximumResidualMm)+" mm；请核对对应点和实测坐标。");
        Array matrix,residuals;for(auto n:solution.Transform)matrix.emplace_back(n);for(auto n:solution.Residuals)residuals.emplace_back(n);
        (void)RigidMatrix(matrix);
        plan["machineStatus"]=Text("Qualified");
        plan["machineRegistration"]=Map{{"sourceFrame",Text("Setup")},{"targetFrame",Text("Machine")},{"unit",Text("mm")},
            {"measuredPoints",measured},{"transform",matrix},{"residualsMm",residuals},{"rmsMm",solution.RmsMm},
            {"maximumResidualMm",solution.MaximumResidualMm},{"residualLimitMm",fit.MaximumResidualMm}};
    }else if(request.contains("maximumResidualMm"))throw std::invalid_argument("CAM_LOCATING_THREE_MEASUREMENTS_REQUIRED");
    Transaction(scene.Database(),"保存三点定位资料",[&](auto& tx){tx.ModifyComponent(p->GetSetupID(),CFiveAxisSetupComponent::S_ClassName,{{"RegistrationPlan",plan}});});
    runtime->Receipts.emplace(commandID,std::make_pair(payload,plan));return plan;
}
Map ExportRegistrationPlan(Scene& scene,const Map& request) {
    CheckExpected(*Runtime(scene),request);const auto id=Identifier(request,"setupId");RequireSetupWorkpieceEnabled(scene,id);
    const auto plan=RegistrationPlanState(scene,id);
    if(String(plan,"status")!="Prepared")throw std::invalid_argument("CAM_LOCATING_NOT_PREPARED: 定位资料缺失或已过期，请重新保存三点。");
    // The exact same Setup frame is used by generated path coordinates.
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);
    auto document=plan;document["setupName"]=setup->GetName();document["schema"]=Text("icax.setup-locating-points.v1");
    const auto bytes=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(document);
    Map result{{"fileName",Text("三点定位-")+iCAX::Data::to_string(id)+".json"},{"content",bytes}};
    if(request.contains("destination")){
        const auto destination=String(request,"destination");
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(destination.data()),destination.size()));
        if(!path.is_absolute()||!std::filesystem::is_directory(path.parent_path()))throw std::invalid_argument("CAM_LOCATING_FILE_PATH_REQUIRED");
        auto temporary=path;temporary+=L".tmp-"+std::filesystem::path(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())).wstring();
        try{
            {std::ofstream file(temporary,std::ios::binary);file.write(bytes.data(),bytes.size());file.flush();if(!file)throw std::runtime_error("CAM_LOCATING_WRITE_FAILED");}
            {std::ifstream file(temporary,std::ios::binary);const Text actual((std::istreambuf_iterator<char>(file)),{});if(actual!=bytes)throw std::runtime_error("CAM_LOCATING_READBACK_FAILED");}
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("CAM_LOCATING_PUBLISH_FAILED");
        }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
        result["path"]=destination;
    }
    return result;
}
