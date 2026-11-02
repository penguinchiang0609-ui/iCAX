Map FixtureQueryDocument(Scene& scene,const Map& reference){
    iCAX::Resource::CResourceAccessService access(scene.Resources());iCAX::Resource::CResourceRequest request;request.URL=reference.at("url").To<Text>();request.Headers["ICAX-Resource-Version"]=reference.at("version").To<Text>();const auto response=access.Request(request);
    if(response.nStatus!=200||!flatbuffers::BufferHasIdentifier(response.Body.Data(),"ICJD"))throw std::runtime_error("Fixture query resource unavailable");
    const auto table=flatbuffers::GetRoot<flatbuffers::Table>(response.Body.Data());const auto json=table->GetPointer<const flatbuffers::String*>(6);if(!json)throw std::runtime_error("Fixture query document missing");return iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json->str()).To<Map>();
}
// Geometry acceptance explicitly queries the needed details after receiving
// a command receipt, exactly as a consumer of the separated API must do.
Map FixtureQueryDesign(Scene& scene,Map request={}){
    request["setupId"]=Read(scene).at("activeSetupId");auto design=Invoke(&scene,"CamFixture","ReadDesign",request);request["snapshot"]=design.at("snapshot");
    if(design.contains("recipe")){auto recipe=design.at("recipe").To<Map>();recipe["supportFaces"]=Invoke(&scene,"CamFixture","QuerySupportFaces",request).at("supportFaces");design["recipe"]=recipe;}
    for(const auto* name:{"candidate","assembly","breakdown"})if(design.contains(name)){
        request["list"]=Text(name);const auto page=Invoke(&scene,"CamFixture","QueryParts",request);Array parts;
        for(const auto& row:page.at("items").To<Array>()){auto detail=request;detail["partId"]=row.To<Map>().at("partId");const auto descriptor=Invoke(&scene,"CamFixture","PartDetails",detail);parts.emplace_back(FixtureQueryDocument(scene,descriptor.at("detailsResourceRef").To<Map>()));}
        auto assembly=design.at(name).To<Map>();assembly["parts"]=parts;design[name]=assembly;
    }return design;
}
Map FixtureBreakdownDetails(Scene& scene,const Map& request){Invoke(&scene,"CamFixture","Breakdown",request);return FixtureQueryDesign(scene,request).at("breakdown").To<Map>();}
Map FixtureProfileDetails(Scene& scene,const Map& request){auto result=Invoke(&scene,"CamFixture","PartProfile",request);result["profile"]=FixtureQueryDocument(scene,result.at("profileResourceRef").To<Map>()).at("profile");return result;}
