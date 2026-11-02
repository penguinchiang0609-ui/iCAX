namespace
{
    void InvalidateFixtureAssemblyEdits(Map& design){
        design.erase("candidate");design.erase("breakdown");design.erase("delivery");
        if(!design.contains("assembly"))return;auto assembly=design.at("assembly").To<Map>();assembly["status"]=Text("ManuallyModified");assembly["manualReviewRequired"]=true;
        if(assembly.contains("supportCheck")){auto support=assembly.at("supportCheck").To<Map>();support["status"]=Text("Unchecked");assembly["supportCheck"]=support;}
        for(const auto* key:{"cutClearance","openingPreclearance"})if(assembly.contains(key)){auto check=assembly.at(key).To<Map>();check["status"]=Text("Unchecked");check["validity"]=Text("Stale");assembly[key]=check;}
        design["assembly"]=assembly;
    }
    template<class Tx>void StoreFixtureUnitDesign(Scene& scene,Tx& tx,const ID& unitID,Map design){
        const auto unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),unitID);tx.ModifyComponent(unitID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"Design",design},{"InputRevision",Next(unit->GetInputRevision())}});
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),unit->GetSetupID());auto editor=setup->GetFixtureDesign();
        if(editor.contains("unitId")&&Identifier(editor,"unitId")==unitID){editor["assembly"]=design.at("assembly");InvalidateFixtureAssemblyEdits(editor);tx.ModifyComponent(unit->GetSetupID(),CFiveAxisSetupComponent::S_ClassName,{{"FixtureDesign",editor}});}
    }
    Map WithFixturePartEnabled(Map design,const ID& partID,bool enabled){
        if(!design.contains("assembly"))throw std::invalid_argument("CAM_FIXTURE_ASSEMBLY_REQUIRED");auto assembly=design.at("assembly").To<Map>();auto parts=assembly.at("parts").To<Array>();bool found=false;
        for(auto& value:parts){auto part=value.To<Map>();if(Identifier(part,"partId")!=partID)continue;part["enabled"]=enabled;value=part;found=true;break;}
        if(!found)throw std::invalid_argument("CAM_FIXTURE_PART_NOT_IN_ASSEMBLY");assembly["parts"]=parts;design["assembly"]=assembly;InvalidateFixtureAssemblyEdits(design);return design;
    }
    Text FixtureInputKey(Scene& scene,const ID& setupID)
    {
        std::map<Text,Variant> inputs;
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})) {
            const auto id=iCAX::Data::to_string(e->GetID());
            if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==setupID) {
                const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
                inputs[id]=Map{{"geometry",WireRef(w->GetGeometryRef())},{"transform",p->GetTransform()}};
            }
            if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==setupID)
                inputs[id]=Map{{"geometry",WireRef(f->GetGeometryRef())},{"transform",f->GetTransform()},{"enabled",(f->GetEnabled()&&f->GetCollisionEnabled())}};
        }
        Map canonical;for(const auto& [id,value]:inputs)canonical[id]=value;
        return iCAX::Data::VariantSerializer::Serialize(canonical);
    }
    Map FixtureCheckState(Scene& scene,const ID& setupID,Map report)
    {
        if(!report.empty())report["validity"]=Text(String(report,"inputKey")==FixtureInputKey(scene,setupID)?"Current":"Stale");
        return report;
    }
    template<class Action> Map FixtureMutation(Scene& scene,const Map& request,const Text& operation,Action action)
    {
        const auto runtime=Runtime(scene);
        const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
        const auto payload=operation+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto found=runtime->Receipts.find(commandID);found!=runtime->Receipts.end()) {
            if(found->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
            return found->second.second;
        }
        for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime,request);CheckCapacity(*runtime);
        const auto id=Identifier(request,"fixtureId"),setupID=Identifier(request,"setupId");
        const auto fixture=Require<CFiveAxisFixtureComponent>(scene.Database(),id);
        if(fixture->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
        action(id,setupID,fixture);
        Map result{{"fixtureId",iCAX::Data::to_string(id)}};
        runtime->Receipts.emplace(commandID,std::make_pair(payload,result));
        PumpTasks(scene);
        return result;
    }
    std::shared_ptr<const iCAX::GeometryData::BRepModel> FixtureGeometry(Scene& scene,const Map& stored)
    {
        const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(stored);
        if(!ref)throw std::runtime_error("CAM_GEOMETRY_MISSING");
        const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
        if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
        return geometry;
    }
    void FinishFixtureCheck(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try {
            auto measured=task.ClearanceWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence)task.Status="Superseded";
            else {
                Array pairs;UInt contacts=0,nearCount=0;
                for(const auto& pair:*measured) {
                    const double available=std::max(0.,pair.DistanceMm-pair.MeshAllowanceMm);
                    const bool contact=pair.DistanceMm<=1e-6,close=!contact&&available+1e-6<task.ClearanceMm;
                    contacts+=contact;nearCount+=close;
                    pairs.emplace_back(Map{{"first",pair.First},{"second",pair.Second},{"distanceMm",pair.DistanceMm},{"availableClearanceMm",available},{"meshAllowanceMm",pair.MeshAllowanceMm},
                        {"status",Text(contact?"Contact":close?"InsufficientClearance":"Separated")},
                        {"firstPoint",Array{pair.FirstPoint[0],pair.FirstPoint[1],pair.FirstPoint[2]}},
                        {"secondPoint",Array{pair.SecondPoint[0],pair.SecondPoint[1],pair.SecondPoint[2]}}});
                }
                // Contact includes touching, intersection and containment. It is
                // not a claim that intended support contact is a machining error.
                const Map report{{"inputRevision",std::to_string(task.SetupRevision)},{"inputKey",task.ClearanceInputKey},{"scope",Text("StaticAssembly")},
                    {"clearanceMm",task.ClearanceMm},{"pairs",pairs},{"contactCount",contacts},{"clearanceCount",nearCount}};
                Transaction(scene.Database(),"检查装夹间隙",[&](auto& tx){
                    tx.ModifyComponent(task.SetupID,CFiveAxisSetupComponent::S_ClassName,{{"FixtureCheck",report}});
                });
                task.Result={{"setupId",iCAX::Data::to_string(task.SetupID)},{"contactCount",contacts},{"clearanceCount",nearCount}};
                task.Status="Completed";
            }
        } catch(const std::exception& error) {
            task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=error.what();
        }
        task.ClearanceWorker={};
    }
}
Map UpdateFixture(Scene& scene,const Map& request)
{
    return FixtureMutation(scene,request,"FixtureUpdate:",[&](const ID& id,const ID& setupID,const auto& fixture){
        if(request.contains("transform")&&!fixture->GetUnitID().is_nil())if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(scene.Database().GetEntity(fixture->GetUnitID()));unit&&unit->GetKind()=="Generated")throw std::invalid_argument("CAM_FIXTURE_GENERATED_TRANSFORM_FIXED: 生成支撑的拼装位置固定，不能平移或旋转。");
        iCAX::Data::PropertySet changes;bool geometryChanged=false;
        if(fixture->GetLocked()&&(request.contains("transform")||request.contains("name")))throw std::invalid_argument("CAM_FIXTURE_LOCKED: 请先解锁工装。");
        if(request.contains("locked"))changes["Locked"]=request.at("locked").To<bool>();
        if(request.contains("enabled")){bool enabled=request.at("enabled").To<bool>();if(!fixture->GetUnitID().is_nil())if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(scene.Database().GetEntity(fixture->GetUnitID()));unit&&unit->GetKind()=="Generated")enabled&=unit->GetEnabled();changes["Enabled"]=enabled;geometryChanged|=enabled!=fixture->GetEnabled();}
        if(request.contains("name")) {
            const auto name=String(request,"name");
            if(name.empty()||name.size()>512)throw std::invalid_argument("CAM_FIXTURE_NAME_INVALID");
            changes["Name"]=name;
        }
        if(request.contains("transform")) {
            const auto matrix=RigidMatrix(request.at("transform").To<Array>());Array values;
            for(auto n:matrix)values.emplace_back(n==0.?0.:n);
            geometryChanged=values!=fixture->GetTransform();changes["Transform"]=values;
        }
        if(request.contains("visible"))changes["Visible"]=request.at("visible").To<bool>();
        if(request.contains("collisionEnabled")) {
            const bool enabled=request.at("collisionEnabled").To<bool>();
            geometryChanged|=enabled!=fixture->GetCollisionEnabled();changes["CollisionEnabled"]=enabled;
        }
        if(changes.empty())throw std::invalid_argument("CAM_FIXTURE_UPDATE_EMPTY");
        changes["InputRevision"]=Next(fixture->GetInputRevision());
        Transaction(scene.Database(),"修改工装",[&](auto& tx){
            tx.ModifyComponent(id,CFiveAxisFixtureComponent::S_ClassName,changes);
            if(request.contains("enabled")&&!fixture->GetUnitID().is_nil())if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(scene.Database().GetEntity(fixture->GetUnitID()));unit&&unit->GetKind()=="Generated"){
                StoreFixtureUnitDesign(scene,tx,fixture->GetUnitID(),WithFixturePartEnabled(unit->GetDesign(),id,request.at("enabled").To<bool>()));
            }
            if(geometryChanged)Invalidate(tx,scene.Database(),setupID);
        });
    });
}
Map RemoveFixture(Scene& scene,const Map& request)
{
    return FixtureMutation(scene,request,"FixtureRemove:",[&](const ID& id,const ID& setupID,const auto& fixture){
        if(!fixture->GetUnitID().is_nil())if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(scene.Database().GetEntity(fixture->GetUnitID()));unit&&unit->GetKind()=="Generated")throw std::invalid_argument("CAM_FIXTURE_GENERATED_PART_DISABLE_ONLY: 生成板件请使用禁用，保留原拼装连接。");
        if(fixture->GetLocked())throw std::invalid_argument("CAM_FIXTURE_LOCKED: 请先解锁工装。");
        Transaction(scene.Database(),"移除工装",[&](auto& tx){
            tx.DisposeEntity(id);Invalidate(tx,scene.Database(),setupID);
        });
    });
}
Map CheckFixtures(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("FixtureCheck:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID) {
        if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
        return TaskState(scene,*runtime,task);
    }
    if(runtime->Receipts.contains(commandID))throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    for(const auto& [_,task]:runtime->Tasks)if(task.Status=="Running"&&task.Kind!="RapidPreview")throw std::runtime_error("CAM_IMPORT_BUSY");
    const auto setupID=Identifier(request,"setupId");
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const double gap=FiniteNumber(request.at("clearanceMm"),"CAM_FIXTURE_CLEARANCE_INVALID");
    if(gap<0||gap>1000)throw std::invalid_argument("CAM_FIXTURE_CLEARANCE_INVALID");
    std::vector<iCAX::Geometry::SClearanceObject> objects;
    size_t workpieces=0,fixtures=0;
    for(const auto& e:scene.Database().FilterEntities([](auto){return true;})) {
        if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==setupID) {
            const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
            objects.push_back({iCAX::Data::to_string(e->GetID()),FixtureGeometry(scene,w->GetGeometryRef()),RigidMatrix(p->GetTransform())});++workpieces;
        }
        if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==setupID&&(f->GetEnabled()&&f->GetCollisionEnabled())) {
            objects.push_back({iCAX::Data::to_string(e->GetID()),FixtureGeometry(scene,f->GetGeometryRef()),RigidMatrix(f->GetTransform())});++fixtures;
        }
    }
    if(workpieces!=1)throw std::invalid_argument("CAM_FIXTURE_WORKPIECE_REQUIRED");
    if(!fixtures)throw std::invalid_argument("CAM_FIXTURE_ENABLED_REQUIRED");
    auto service=scene.Services().Resolve<iCAX::Geometry::ICadClearanceService>();
    if(!service)throw std::runtime_error("CAM_CLEARANCE_SERVICE_UNAVAILABLE");
    ImportTask task;task.Kind="FixtureCheck";task.CommandID=commandID;task.Payload=payload;task.SetupID=setupID;
    task.Sequence=runtime->Sequence;task.SetupRevision=setup->GetInputRevision();task.ClearanceMm=gap;
    task.ClearanceInputKey=FixtureInputKey(scene,setupID);
    task.ClearanceWorker=iCAX::Tasks::Run([service,objects=std::move(objects),cancel=task.Cancelled]{
        return std::make_shared<std::vector<iCAX::Geometry::SClearancePair>>(service->Measure(objects,[cancel]{return cancel->load();}));
    });
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));
    return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
