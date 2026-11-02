namespace {
    constexpr const char* HeadConfigurationFeature="head-configurations";
    Map PackHeadConfiguration(Scene& scene,Map model,const Map& parameters){
        Array assets;std::map<Text,UInt> packed;
        std::function<void(Map&)> visit=[&](Map& object){
            for(auto& [key,value]:object){
                if((key=="sourceRef"||key=="geometryRef"||key=="previewRef"||key=="machinePreviewRef")&&std::holds_alternative<Map>(value.m_Value)&&!value.To<Map>().empty()){
                    const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(value.To<Map>());if(!ref)throw std::invalid_argument("CAM_HEAD_RESOURCE_MISSING");const auto resourceKey=ref->URL+"@"+std::to_string(ref->nVersion);
                    if(!packed.contains(resourceKey)){
                        Map asset{{"name",key}};
                        if(key=="geometryRef"){const auto data=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);if(!data)throw std::runtime_error("CAM_HEAD_RESOURCE_MISSING");asset["type"]=Text("geometry");asset["bytes"]=PackFixtureBytes(iCAX::GeometryData::Persistence::Serialize(*data));}
                        else if(key=="sourceRef"){const auto data=scene.Resources().Get<iCAX::Resource::CBinaryResource>(ref->URL,ref->nVersion);if(!data)throw std::runtime_error("CAM_HEAD_RESOURCE_MISSING");asset["type"]=Text("source");asset["bytes"]=PackFixtureBytes(data->Content);}
                        else{const auto data=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref->URL,ref->nVersion);if(!data)throw std::runtime_error("CAM_HEAD_RESOURCE_MISSING");asset["type"]=Text("preview");asset["bytes"]=PackFixtureBytes(data->Bytes());}
                        packed[resourceKey]=static_cast<UInt>(assets.size());assets.emplace_back(asset);
                    }value=Map{{"headAsset",packed.at(resourceKey)}};continue;
                }
                if(auto map=std::get_if<Map>(&value.m_Value))visit(*map);
                else if(auto array=std::get_if<Array>(&value.m_Value))for(auto& item:*array)if(auto map=std::get_if<Map>(&item.m_Value))visit(*map);
            }
        };visit(model);return {{"model",model},{"parameters",parameters},{"assets",assets}};
    }
    Map UnpackHeadConfiguration(Scene& scene,const Map& payload){
        auto model=payload.at("model").To<Map>();const auto assets=payload.at("assets").To<Array>();std::vector<Ref> published;
        if(assets.size()>512)throw std::invalid_argument("CAM_HEAD_CONFIGURATION_ASSETS_INVALID");
        (void)NormalizeHeadParameters(model,payload.at("parameters").To<Map>());
        // Decode and validate the complete snapshot before publishing resources.
        std::vector<std::pair<Text,std::vector<uint8_t>>> decoded;
        for(const auto& value:assets){const auto asset=value.To<Map>();auto bytes=UnpackFixtureBytes(String(asset,"bytes"));const auto type=String(asset,"type");
            if(bytes.empty()||bytes.size()>256*1024*1024)throw std::invalid_argument("CAM_HEAD_CONFIGURATION_ASSETS_INVALID");
            if(type=="geometry")(void)iCAX::GeometryData::Persistence::Deserialize(bytes);
            else if(type!="source"&&type!="preview")throw std::invalid_argument("CAM_HEAD_CONFIGURATION_ASSETS_INVALID");decoded.emplace_back(type,std::move(bytes));
        }
        for(const auto& [type,bytes]:decoded){
            if(type=="geometry")published.push_back(Publish(scene,std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::GeometryData::Persistence::Deserialize(bytes)),"geometry.brep","刀头配置模型"));
            else if(type=="preview")published.push_back(Publish(scene,std::make_shared<iCAX::Resource::CFlatBufferResource>(bytes),"resource.flatbuffer","刀头配置显示",published));
            else{auto data=std::make_shared<iCAX::Resource::CBinaryResource>();data->Content=bytes;published.push_back(Publish(scene,data,"resource.binary","刀头配置源数据"));}
        }
        std::function<void(Map&)> visit=[&](Map& object){for(auto& [key,value]:object){if(auto map=std::get_if<Map>(&value.m_Value)){
            if(map->contains("headAsset")){const auto index=map->at("headAsset").To<UInt>();if(index>=published.size())throw std::invalid_argument("CAM_HEAD_CONFIGURATION_ASSETS_INVALID");value=StoredRef(published[index]);}
            else visit(*map);
        }else if(auto array=std::get_if<Array>(&value.m_Value))for(auto& item:*array)if(auto map=std::get_if<Map>(&item.m_Value))visit(*map);}};
        visit(model);return model;
    }
    Map HeadConfigurationSummary(const LibraryRecord& record){const auto data=record.Payload.To<Map>();return {{"id",record.RecordID},{"revision",std::to_string(record.Revision)},{"name",data.at("name")},{"modelName",data.at("modelName")}};}
}
Map HeadConfigurationLibrary(Scene& scene,const Map& request){
    CutFields(request,{"action","commandId","expected","setupId","entryId","revision","name"});const auto action=String(request,"action");auto& store=FixtureStore(scene);
    if(action=="list"){iCAX::Application::CProductUserDataQuery query;query.FeatureID=HeadConfigurationFeature;query.RecordType="configuration";Array items;for(const auto& record:store.List(query))items.emplace_back(HeadConfigurationSummary(record));return {{"items",items}};}
    return RapidMutation(scene,request,"HeadConfigurations:",[&](const ID& setupID,const auto& setup){
        if(action=="save"){
            auto instance=ProjectHeadInstance(scene,*setup);if(instance.empty())throw std::invalid_argument("CAM_HEAD_INSTANCE_REQUIRED");
            auto model=instance.at("model").To<Map>(),parameters=instance.at("parameters").To<Map>();
            if(!instance.at("binding").To<Map>().empty()){
                model=EffectiveHeadDefinition(scene,*setup);model.erase("collisionMachineId");model.erase("collisionToolId");model.erase("machineHeadName");
                if(model.contains("machinePreviewRef")){model["previewRef"]=model.at("machinePreviewRef");model.erase("machinePreviewRef");}model["collisionSource"]=Text("HeadDescription");
                parameters["axes"]=DefaultHeadParameters(model).at("axes");
            }
            auto payload=PackHeadConfiguration(scene,model,NormalizeHeadParameters(model,parameters));payload["name"]=HeadInstanceName(String(request,"name"));payload["modelName"]=model.at("name");payload["calibrationConfirmed"]=instance.at("calibrationConfirmed");
            LibraryRecord record;record.FeatureID=HeadConfigurationFeature;record.RecordType="configuration";record.SubjectType="product";record.SubjectID="icax.five-axis-cam";record.RecordID=iCAX::Data::to_string(Identifier(request,"commandId"));record.Payload=payload;
            return HeadConfigurationSummary(store.Put(record,0));
        }
        const auto entry=store.Get(HeadConfigurationFeature,"configuration",String(request,"entryId"));if(!entry)throw std::invalid_argument("CAM_HEAD_CONFIGURATION_MISSING");
        if(action=="use"){
            RequireSetupUnlocked(scene,setupID);if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND: 请在未绑定机床的位姿中载入独立刀头配置。");
            const auto data=entry->Payload.To<Map>();auto model=UnpackHeadConfiguration(scene,data);const auto parameters=NormalizeHeadParameters(model,data.at("parameters").To<Map>());ID id;
            Transaction(scene.Database(),"载入刀头配置",[&](auto& tx){id=AttachProjectHead(scene,tx,setupID,model,String(data,"name"),parameters,HeadConfigurationSummary(*entry),{},data.at("calibrationConfirmed").To<bool>());});return Map{{"instanceId",iCAX::Data::to_string(id)}};
        }
        if(action=="delete"){const auto revision=std::stoull(String(request,"revision"));store.Delete(HeadConfigurationFeature,"configuration",entry->RecordID,revision);return Map{{"deleted",true}};}
        throw std::invalid_argument("CAM_HEAD_ACTION_REQUIRED");
    });
}
