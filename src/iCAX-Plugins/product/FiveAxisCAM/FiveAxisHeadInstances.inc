namespace {
    std::vector<ID> HeadInstanceUsers(Scene& scene,const ID& instance){
        std::vector<ID> result;for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto s=Component<CFiveAxisSetupComponent>(e);s&&s->GetHeadInstanceID()==instance)result.push_back(e->GetID());return result;
    }
    void WriteHeadInstance(Scene& scene,const ID& setupID,Map instance,bool physical,const Text& label){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());
        const auto id=Identifier(instance,"id");auto instances=job->GetHeadInstances();if(!instances.contains(iCAX::Data::to_string(id)))throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");
        const auto users=HeadInstanceUsers(scene,id);if(physical)for(const auto& user:users)RequireSetupUnlocked(scene,user);
        instance["revision"]=Next(instance.at("revision").To<UInt>());instances[iCAX::Data::to_string(id)]=instance;
        Transaction(scene.Database(),label,[&](auto& tx){
            tx.ModifyComponent(setup->GetJobID(),CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",instances},{"InputRevision",Next(job->GetInputRevision())}});
            if(physical)for(const auto& user:users){const auto s=Require<CFiveAxisSetupComponent>(scene.Database(),user);tx.ModifyComponent(user,CFiveAxisSetupComponent::S_ClassName,{{"PlanValidity",Text(s->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});}
        });
    }
    Map BoundHeadSnapshot(Scene& scene,const Map& configuration){
        const auto tool=scene.Database().GetEntity(Identifier(configuration,"toolId"));const auto element=Require<iCAX::CAM::CMachineElementComponent>(scene.Database(),tool->GetID());
        Map model{{"kind",Text("Imported")},{"name",element->GetName()},{"calibrated",true},{"tcpLocalPosition",configuration.at("tcp")},{"beamLocalDirection",configuration.at("beam")},{"collisionSource",Text("HeadDescription")}};
        if(const auto render=Component<iCAX::RenderInteraction::CRenderInstanceComponent>(tool);render&&!render->GetGeometryResourceID().empty())model["previewRef"]=StoredRef({render->GetGeometryResourceID(),scene.Resources().GetVersion(render->GetGeometryResourceID())});
        const auto collision=Component<iCAX::CAM::CMachineCollisionComponent>(tool);model["collisionBoxes"]=collision?collision->GetCollisions():Array{};
        const auto assembly=BoundBuiltinHeadAssembly(scene,configuration);UInt count=0;if(!assembly.empty()){model["assembly"]=assembly;model["requiresMachineSimulation"]=true;for(const auto& v:assembly.at("nodes").To<Array>()){const auto type=String(v.To<Map>(),"type");if(type=="revolute"||type=="continuous")++count;}}
        model["motionType"]=Text(count?"Articulated":"Rigid");model["rotaryAxisCount"]=count;
        return model;
    }
    void AttachBoundProjectHead(Scene& scene,iCAX::Database::ITransaction& tx,const ID& setupID,Map& configuration){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());
        auto instance=ProjectHeadInstance(scene,*setup);const Map binding{{"machineId",configuration.at("machineId")},{"toolId",configuration.at("toolId")}};
        if(instance.empty()||instance.at("binding").To<Map>()!=binding){const auto id=AttachProjectHead(scene,tx,setupID,BoundHeadSnapshot(scene,configuration),{}, {}, {},binding);configuration["headInstanceId"]=iCAX::Data::to_string(id);return;}
        configuration["headInstanceId"]=instance.at("id");const auto effective=CalibratedHeadModel(instance);
        if(effective.at("tcpLocalPosition")==configuration.at("tcp")&&effective.at("beamLocalDirection")==configuration.at("beam"))return;
        const auto users=HeadInstanceUsers(scene,Identifier(instance,"id"));for(const auto& user:users)RequireSetupUnlocked(scene,user);
        auto parameters=instance.at("parameters").To<Map>();parameters["tcpMode"]=Text("Coordinates");parameters["tcp"]=configuration.at("tcp");parameters["beam"]=configuration.at("beam");
        instance["parameters"]=NormalizeHeadParameters(instance.at("model").To<Map>(),parameters);instance["calibrationConfirmed"]=true;instance["revision"]=Next(instance.at("revision").To<UInt>());
        auto instances=job->GetHeadInstances();instances[String(instance,"id")]=instance;tx.ModifyComponent(setup->GetJobID(),CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",instances},{"InputRevision",Next(job->GetInputRevision())}});
        for(const auto& user:users){const auto s=Require<CFiveAxisSetupComponent>(scene.Database(),user);tx.ModifyComponent(user,CFiveAxisSetupComponent::S_ClassName,{{"PlanValidity",Text(s->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});}
    }
}
Map ManageHeadInstance(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","action","instanceId","name","parameters","collisionOverrides"});
    return RapidMutation(scene,request,"HeadInstance:",[&](const ID& setupID,const auto& setup){
        const auto action=String(request,"action");RequireSetupUnlocked(scene,setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());
        if(action=="delete"){
            const auto id=Identifier(request,"instanceId");auto instances=job->GetHeadInstances();if(!instances.contains(iCAX::Data::to_string(id)))throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");
            if(!HeadInstanceUsers(scene,id).empty())throw std::invalid_argument("CAM_HEAD_INSTANCE_IN_USE: 请先解除所有位姿对该实例的引用。");
            instances.erase(iCAX::Data::to_string(id));Transaction(scene.Database(),"删除未使用刀头实例",[&](auto& tx){tx.ModifyComponent(setup->GetJobID(),CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",instances},{"InputRevision",Next(job->GetInputRevision())}});});return Map{{"deleted",true}};
        }
        if(action=="select"){
            if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND: 绑定机床后请在机床设置中选择工具端。");
            const auto text=String(request,"instanceId");const auto id=text.empty()?ID():Identifier(request,"instanceId");
            if(!text.empty()&&!job->GetHeadInstances().contains(text))throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");
            if(id!=setup->GetHeadInstanceID())RapidChange(scene,setupID,{{"HeadInstanceID",id}},"选择工程刀头");return Map{{"instanceId",text}};
        }
        auto instance=ProjectHeadInstance(scene,*setup);if(instance.empty())throw std::invalid_argument("CAM_HEAD_INSTANCE_REQUIRED: 请先选择刀头模型。");
        if(action=="rename"){const auto name=HeadInstanceName(String(request,"name"));if(name!=String(instance,"name")){instance["name"]=name;WriteHeadInstance(scene,setupID,instance,false,"重命名刀头实例");}}
        else if(action=="clone"){
            if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND: 请在未绑定机床的位姿中复制刀头配置。");
            ID id;Transaction(scene.Database(),"复制刀头实例",[&](auto& tx){id=AttachProjectHead(scene,tx,setupID,instance.at("model").To<Map>(),String(instance,"name")+" 副本",instance.at("parameters").To<Map>(),instance.at("profile").To<Map>(),{},instance.at("calibrationConfirmed").To<bool>());});return Map{{"instanceId",iCAX::Data::to_string(id)}};
        }
        else if(action=="colliders"){
            if(String(request,"instanceId")!=String(instance,"id"))throw std::invalid_argument("CAM_HEAD_INSTANCE_MISMATCH");
            if(!instance.at("binding").To<Map>().empty())throw std::invalid_argument("CAM_HEAD_COLLISION_MACHINE_OWNED");
            auto parameters=instance.at("parameters").To<Map>();auto groups=parameters.contains("collisionOverrides")?parameters.at("collisionOverrides").To<Array>():Array{};
            const auto incoming=NormalizeHeadCollisionOverrides(instance.at("model").To<Map>(),request.at("collisionOverrides").To<Array>());
            for(const auto& group:incoming){const auto part=group.To<Map>().at("partIndex");std::erase_if(groups,[&](const auto& v){return v.template To<Map>().at("partIndex")==part;});groups.push_back(group);}
            parameters["collisionOverrides"]=groups;parameters=NormalizeHeadParameters(instance.at("model").To<Map>(),parameters);
            if(instance.at("parameters")!=parameters){instance["parameters"]=parameters;WriteHeadInstance(scene,setupID,instance,true,"修改切割头碰撞盒");}
        }
        else if(action=="configure"||action=="restore"){
            auto parameters=action=="restore"?DefaultHeadParameters(instance.at("model").To<Map>()):NormalizeHeadParameters(instance.at("model").To<Map>(),request.at("parameters").To<Map>());
            if(!instance.at("binding").To<Map>().empty()&&parameters.at("axes")!=instance.at("parameters").To<Map>().at("axes"))throw std::invalid_argument("CAM_HEAD_AXES_MACHINE_OWNED: 绑定刀头的轴心、轴向与限位由机床实例管理，请在机床实例参数中修改。");
            const bool confirmed=action!="restore";
            if(instance.at("parameters")!=parameters||instance.at("calibrationConfirmed").To<bool>()!=confirmed){instance["parameters"]=parameters;instance["calibrationConfirmed"]=confirmed;WriteHeadInstance(scene,setupID,instance,true,"修改刀头标定");}
        }
        else throw std::invalid_argument("CAM_HEAD_ACTION_REQUIRED");
        return Map{{"instanceId",instance.at("id")}};
    });
}
