namespace {
    Map FixtureFields(const Map& source,std::initializer_list<const char*> fields){Map result;for(const auto* key:fields)if(source.contains(key))result.emplace(key,source.at(key));return result;}
    Map FixtureRecipeSummary(const Map& recipe){auto result=recipe;result.erase("supportFaces");result["supportFaceCount"]=static_cast<UInt>(recipe.contains("supportFaces")?recipe.at("supportFaces").To<Array>().size():0);return result;}
    Map FixtureAssemblySummary(const Map& assembly){
        auto result=FixtureFields(assembly,{"id","unitId","status","validity","geometryValidity","beamValidity","joint","joinery","layoutSummary"});
        if(assembly.contains("parts")){const auto& parts=std::get<Array>(assembly.at("parts").m_Value);std::size_t windows=0,enabled=0;for(const auto& value:parts){const auto& p=std::get<Map>(value.m_Value);if(!p.contains("enabled")||p.at("enabled").To<bool>())++enabled;if(p.contains("windowHoleIndices"))windows+=std::get<Array>(p.at("windowHoleIndices").m_Value).size();}result["partCount"]=static_cast<UInt>(parts.size());result["enabledPartCount"]=static_cast<UInt>(enabled);result["windowCount"]=static_cast<UInt>(windows);}
        if(assembly.contains("supportCheck"))result["supportCheck"]=FixtureFields(std::get<Map>(assembly.at("supportCheck").m_Value),{"basis","status","centerMethod","centerOfMass","marginMm","maximumSpanMm","maximumEndOverhangMm","projectionFrame"});
        if(assembly.contains("openingPreclearance"))result["openingPreclearance"]=FixtureFields(std::get<Map>(assembly.at("openingPreclearance").m_Value),{"basis","mode","validity","status","boundaryCount","unclassifiedFaceCount","nonExitBoundaryCount","marginMm","reliefDepthMm","productionQualified"});
        if(assembly.contains("cutClearance"))result["cutClearance"]=FixtureFields(std::get<Map>(assembly.at("cutClearance").m_Value),{"mode","status","validity","scope","rapidStatus","headStatus","productionQualified","basis","issueCount","reportRevision"});
        return result;
    }
    Map FixtureDesignSummary(const Map& design){
        auto result=FixtureFields(design,{"unitId","unitSnapshot"});
        if(design.contains("recipe"))result["recipe"]=FixtureRecipeSummary(design.at("recipe").To<Map>());
        for(const auto* key:{"candidate","assembly"})if(design.contains(key))result[key]=FixtureAssemblySummary(std::get<Map>(design.at(key).m_Value));
        if(design.contains("breakdown")){const auto& list=std::get<Map>(design.at("breakdown").m_Value);auto header=FixtureFields(list,{"id","assemblyId","unit","quantityPerPart"});header["partCount"]=static_cast<UInt>(std::get<Array>(list.at("parts").m_Value).size());result["breakdown"]=header;}
        if(design.contains("delivery"))result["delivery"]=FixtureFields(design.at("delivery").To<Map>(),{"id","directory","assemblyId"});
        return result;
    }
    struct FixtureQueryTarget {ID SetupID;Map Design;Text Snapshot;};
    FixtureQueryTarget FixtureQuery(Scene& scene,const Map& request){
        const auto runtime=Runtime(scene);if(request.contains("epoch")&&String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
        FixtureQueryTarget target;target.SetupID=Identifier(request,"setupId");RequireSetupWorkpieceEnabled(scene,target.SetupID);
        if(request.contains("taskId")){
            const auto it=runtime->Tasks.find(String(request,"taskId"));if(it==runtime->Tasks.end()||it->second.Kind!="FixtureGenerate"||it->second.Status!="Completed"||it->second.SetupID!=target.SetupID||!it->second.FixtureModel)throw std::invalid_argument("CAM_FIXTURE_QUERY_TASK_INVALID");
            target.Design=Map{{"recipe",it->second.FixtureRecipe},{"candidate",it->second.FixtureModel->Assembly}};target.Snapshot=runtime->Epoch+":"+it->second.TaskID;
        }else if(request.contains("unitId")){
            const auto unitID=Identifier(request,"unitId");const auto unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),unitID);if(unit->GetSetupID()!=target.SetupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            target.Design=unit->GetDesign();target.Snapshot=runtime->Epoch+":"+iCAX::Data::to_string(unitID)+":"+std::to_string(unit->GetInputRevision());
        }else {const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),target.SetupID);target.Design=setup->GetFixtureDesign();target.Snapshot=runtime->Epoch+":"+iCAX::Data::to_string(target.SetupID)+":"+std::to_string(setup->GetInputRevision());}
        if(request.contains("snapshot")&&String(request,"snapshot")!=target.Snapshot)throw std::invalid_argument("CAM_FIXTURE_QUERY_SOURCE_CHANGED");return target;
    }
    Map FixtureQueryAssembly(const FixtureQueryTarget& target,const Map& request){
        const auto list=request.contains("list")?String(request,"list"):target.Design.contains("assembly")?Text("assembly"):Text("candidate");
        if(list!="candidate"&&list!="assembly"&&list!="breakdown")throw std::invalid_argument("CAM_FIXTURE_QUERY_LIST_INVALID");
        if(!target.Design.contains(list))throw std::invalid_argument("CAM_FIXTURE_QUERY_LIST_MISSING");return target.Design.at(list).To<Map>();
    }
    Map FixturePartSummary(const Map& part){
        auto row=FixtureFields(part,{"partId","number","role","material","thickness","enabled","geometryRef","previewRef","frame","minimumDistance","topRetreat","contactLengthMm"});
        row["windowCount"]=static_cast<UInt>(part.contains("windowHoleIndices")?part.at("windowHoleIndices").To<Array>().size():0);return row;
    }
}
Map ReadFixtureDesignDetails(Scene& scene,const Map& request){
    const auto target=FixtureQuery(scene,request);auto design=ProjectFixtureDesignState(scene,target.SetupID,target.Design,true);design["snapshot"]=target.Snapshot;return design;
}
Map QueryFixtureSupportFaces(Scene& scene,const Map& request){
    const auto target=FixtureQuery(scene,request);const auto recipe=target.Design.contains("recipe")?target.Design.at("recipe").To<Map>():Map();const auto faces=recipe.contains("supportFaces")?recipe.at("supportFaces").To<Array>():Array();
    return {{"snapshot",target.Snapshot},{"supportFaces",faces}};
}
Map QueryFixtureParts(Scene& scene,const Map& request){
    const auto target=FixtureQuery(scene,request);const auto list=FixtureQueryAssembly(target,request);const auto& parts=std::get<Array>(list.at("parts").m_Value);
    Array rows;for(const auto& part:parts)rows.emplace_back(FixturePartSummary(part.To<Map>()));return {{"snapshot",target.Snapshot},{"items",rows},{"totalCount",static_cast<UInt>(rows.size())}};
}
Map ReadFixturePartDetails(Scene& scene,const Map& request){
    const auto target=FixtureQuery(scene,request);const auto assembly=FixtureQueryAssembly(target,request);const auto& parts=std::get<Array>(assembly.at("parts").m_Value);
    for(const auto& value:parts){const auto& part=std::get<Map>(value.m_Value);if(String(part,"partId")!=String(request,"partId"))continue;
        const auto ref=PublishQueryDocument(scene,target.SetupID,"fixture-part:"+String(part,"partId"),target.Snapshot+":"+String(assembly,"id"),part);
        return {{"partId",part.at("partId")},{"snapshot",target.Snapshot},{"detailsResourceRef",WireRef(StoredRef(ref))}};
    }throw std::invalid_argument("CAM_FIXTURE_PART_NOT_IN_ASSEMBLY");
}
Map ReadFixturePartProfileProjection(Scene& scene,const Map& request){
    auto result=ReadFixturePartProfile(scene,request);const auto key=iCAX::Data::VariantSerializer::Serialize(result.at("geometryRef"));
    const auto resource=PublishQueryDocument(scene,Identifier(request,"setupId"),"fixture-profile:"+String(request,"partId"),key,Map{{"profile",result.at("profile")}});
    result.erase("profile");result["profileResourceRef"]=WireRef(StoredRef(resource));return result;
}
Map ReadFixtureSupportSelection(Scene& scene,const Map& request){
    const auto runtime=Runtime(scene);if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto found=runtime->Tasks.find(String(request,"taskId"));if(found==runtime->Tasks.end()||found->second.Kind!="FixtureSupportFaces"||found->second.Status!="Completed")throw std::invalid_argument("CAM_FIXTURE_SELECTION_UNAVAILABLE");const auto& task=found->second;
    if(task.Sequence!=runtime->Sequence||task.ClearanceInputKey!=iCAX::Data::VariantSerializer::Serialize(FixtureDesignSource(scene,task.SetupID)))throw std::invalid_argument("CAM_FIXTURE_QUERY_SOURCE_CHANGED");return task.SupportSelection;
}
Map ReadFixtureCheckDetails(Scene& scene,const Map& request){
    const auto id=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);const auto raw=setup->GetFixtureCheck();if(raw.empty())throw std::invalid_argument("CAM_FIXTURE_CHECK_MISSING");const auto report=FixtureCheckState(scene,id,raw);
    const auto resource=PublishQueryDocument(scene,id,"fixture-check",String(raw,"inputKey")+":"+String(report,"validity"),report);
    return {{"setupId",request.at("setupId")},{"reportResourceRef",WireRef(StoredRef(resource))}};
}
