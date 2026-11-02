namespace {
    Map EngineeringStorageAudit(Scene& scene){
        const auto state=Read(scene);const auto paths=state.at("intentToolpaths").To<Array>();
        std::size_t cuts=paths.size(),complete=0,legacy=0;Array schemas;
        for(const auto& value:paths){const auto path=value.To<Map>();
            complete+=path.at("sourceState").To<Text>()=="Current"&&!path.at("curveRef").To<Map>().empty()&&!path.at("processRef").To<Map>().empty()&&!path.at("poseRef").To<Map>().empty();}
        bool profile=false,collision=false,plan=false,beam=false,registration=false;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            for(const auto* name:{"CFiveAxisCutFeatureComponent","CIntentToolpathComponent","CIntentGeometryComponent","CCutIntentComponent"})legacy+=entity->HasComponent(name);
            if(const auto c=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(entity->GetComponent(CFiveAxisSetupComponent::S_ClassName))){profile|=!c->GetPlanningProfileData().empty();collision|=!c->GetSimulationCheckData().empty();plan|=!c->GetMachineMotionPlanData().empty();beam|=!c->GetFixtureBeamCheck().empty();}
            if(const auto c=std::dynamic_pointer_cast<CFiveAxisPlacementComponent>(entity->GetComponent(CFiveAxisPlacementComponent::S_ClassName))){registration|=!c->GetRegistrationData().empty();}
        }
        for(const auto& info:scene.Resources().GetManifest(false)){
            if(info.ResourceTypeID=="cam.intent.curve")schemas.emplace_back(info.ResourceTypeID);
            if(const auto body=scene.Resources().Get<iCAX::Resource::CBinaryResource>(info.Key.Source,info.nVersion))for(const auto* key:{"schema","kind"})if(body->Metadata.contains(key)&&body->Metadata.at(key).starts_with("cam."))schemas.emplace_back(body->Metadata.at(key));
        }
        return {{"cutCount",std::to_string(cuts)},{"completeCutDataCount",std::to_string(complete)},{"legacyOwnerCount",std::to_string(legacy)},{"persistentCamSchemas",schemas},{"profile",profile},{"collision",collision},{"plan",plan},{"beam",beam},{"registration",registration}};
    }
    std::vector<uint8_t> DataBytes(const Map& data){
        const auto text=data.at("value").To<Map>().at("bytes").To<Text>();
        if(text.size()%2)throw std::runtime_error("Invalid curve bytes");
        std::vector<uint8_t> bytes;bytes.reserve(text.size()/2);
        for(std::size_t i=0;i<text.size();i+=2)bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
        return bytes;
    }
    Map FindEngineeringValue(const Variant& value,const Variant& revision){
        if(value.Is<Map>()){const auto object=value.To<Map>();
            if(object.contains("revision")&&object.at("revision")==revision&&object.contains("value"))return object;
            for(const auto& [_,child]:object){const auto found=FindEngineeringValue(child,revision);if(!found.empty())return found;}
        }else if(value.Is<Array>())for(const auto& child:value.To<Array>()){const auto found=FindEngineeringValue(child,revision);if(!found.empty())return found;}
        return {};
    }
    Text IntentEngineeringDigest(const Variant& value){
        const auto bytes=iCAX::Data::VariantSerializer::Serialize(value);unsigned char hash[32]{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),hash,sizeof(hash))<0)throw std::runtime_error("Test engineering digest failed");
        constexpr char hex[]="0123456789abcdef";Text result;for(auto byte:hash){result+=hex[byte>>4];result+=hex[byte&15];}return result;
    }
    Map ResolveEngineeringValue(Scene& scene,const Map& identity){
        using namespace iCAX::CAM::Intent;
        if(identity.contains("value"))return identity;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            Array values;
            if(const auto c=std::dynamic_pointer_cast<Editable::CEditableDerivedComponent>(entity->GetComponent(Editable::CEditableDerivedComponent::S_ClassName))){
                const auto data=c->GetData();if(data.contains("outputs"))for(const auto& output:data.at("outputs").To<Array>()){
                    const auto entry=output.To<Map>();SResolvedPath path;path.PathId=Editable::ID(entry.at("pathId"));path.Port={c->GetOwner(),Editable::ID(entry.at("portId"))};path.Value=ToolpathFromData(entry.at("value").To<Map>());
                    path.Fingerprint=entry.at("fingerprint").To<Text>();const auto metadata=ProjectFiveAxisIntentPath(path).Metadata;
                    for(const auto& body:{metadata.Feature,metadata.Process,metadata.SourceData,metadata.Pose})values.emplace_back(Map{{"revision",Text("intent-view/")+IntentEngineeringDigest(body)},{"value",body}});
                    values.emplace_back(Map{{"revision",Text("intent-pose/")+ToolpathFingerprint(path.Value,{"pose","poseControls","poseField","contourPoseField"})},{"value",metadata.Pose}});
                }
            }
            if(const auto c=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(entity->GetComponent(CFiveAxisSetupComponent::S_ClassName))){values.emplace_back(c->GetPlanningProfileData());values.emplace_back(c->GetSimulationCheckData());values.emplace_back(c->GetMachineMotionPlanData());values.emplace_back(c->GetFixtureBeamCheck());}
            if(const auto c=std::dynamic_pointer_cast<CFiveAxisPlacementComponent>(entity->GetComponent(CFiveAxisPlacementComponent::S_ClassName)))values.emplace_back(c->GetRegistrationData());
            const auto found=FindEngineeringValue(values,identity.at("revision"));if(!found.empty())return found;
        }
        throw std::runtime_error("Engineering value missing from unified database");
    }
    std::shared_ptr<iCAX::CAM::Intent::CIntentCurveResource> IntentCurveValue(Scene& scene,const Map& intent){
        using namespace iCAX::CAM::Intent;
        const auto fingerprint=intent.at("curveRef").To<Map>().at("revision");
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
            if(const auto c=std::dynamic_pointer_cast<Editable::CEditableDerivedComponent>(entity->GetComponent(Editable::CEditableDerivedComponent::S_ClassName))){
                const auto data=c->GetData();if(!data.contains("outputs"))continue;
                for(const auto& output:data.at("outputs").To<Array>()){
                    const auto entry=output.To<Map>();if(entry.at("fingerprint")!=fingerprint)continue;
                    const auto value=ToolpathFromData(entry.at("value").To<Map>());auto curve=std::make_shared<CIntentCurveResource>();
                    curve->HasCurve3=true;curve->Parameterization="Native";curve->Curve3.Closed=value.Geometry.Closed;
                    for(const auto& segment:value.Geometry.Segments){const auto* exact=std::get_if<iCAX::GeometryData::CurveSegment3>(&segment.Curve);
                        if(!exact)throw std::runtime_error("Test requires a nominal curve, received a compensated expression");curve->Curve3.Segments.push_back(*exact);}
                    return curve;
                }
            }
        throw std::runtime_error("Exact intent curve missing from unified database");
    }
}
