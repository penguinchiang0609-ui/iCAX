namespace {
    namespace FixturePartGeometry=iCAX::GeometryData;
    namespace FixturePartJson=iCAX::CAD::TubeCadJson;
    struct FixturePlateTarget {
        ID SetupID,UnitID,PartID;
        std::shared_ptr<CFiveAxisFixtureUnitComponent> Unit;
        std::shared_ptr<CFiveAxisFixtureComponent> Fixture;
        Map Design,Assembly,Part;
        size_t Index=0;
    };
    FixturePlateTarget FixturePlate(Scene& scene,const Map& request){
        FixturePlateTarget target;target.SetupID=Identifier(request,"setupId");target.UnitID=Identifier(request,"unitId");target.PartID=Identifier(request,"partId");
        target.Unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),target.UnitID);target.Fixture=Require<CFiveAxisFixtureComponent>(scene.Database(),target.PartID);
        if(target.Unit->GetSetupID()!=target.SetupID||target.Fixture->GetSetupID()!=target.SetupID||target.Fixture->GetUnitID()!=target.UnitID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
        if(target.Unit->GetKind()!="Generated")throw std::invalid_argument("CAM_FIXTURE_GENERATED_PART_REQUIRED");
        target.Design=target.Unit->GetDesign();if(!target.Design.contains("assembly"))throw std::invalid_argument("CAM_FIXTURE_ASSEMBLY_REQUIRED");target.Assembly=target.Design.at("assembly").To<Map>();
        const auto parts=target.Assembly.at("parts").To<Array>();for(size_t i=0;i<parts.size();++i)if(Identifier(parts[i].To<Map>(),"partId")==target.PartID){target.Index=i;target.Part=parts[i].To<Map>();break;}
        if(target.Part.empty()||target.Part.at("geometryRef").To<Map>()!=target.Fixture->GetGeometryRef())throw std::invalid_argument("CAM_FIXTURE_PART_NOT_IN_ASSEMBLY");
        return target;
    }
    std::set<size_t> FixtureHoleIndices(const Map& part,const char* key,size_t count){
        std::set<size_t> result;if(!part.contains(key))return result;
        for(const auto& value:part.at(key).To<Array>()){const double index=FiniteNumber(value,"CAM_FIXTURE_HOLE_INDEX_INVALID");if(index<0||index>=count||std::floor(index)!=index)throw std::invalid_argument("CAM_FIXTURE_HOLE_INDEX_INVALID");result.insert(static_cast<size_t>(index));}return result;
    }
    auto FixturePlateService(Scene& scene){const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();if(!service)throw std::runtime_error("CAM_FIXTURE_PART_EDIT_SERVICE_UNAVAILABLE");return service;}
    FixturePartGeometry::Tube::CPlanarRegion2 FixturePlateProfile(Scene& scene,const FixturePlateTarget& target){
        if(!target.Part.contains("windowHoleIndices"))throw std::invalid_argument("CAM_FIXTURE_PART_EDIT_SOURCE_REQUIRED: 请重新生成夹具后编辑板件。");
        if(target.Part.contains("exactProfile"))return FixturePartJson::Decode<FixturePartGeometry::Tube::CPlanarRegion2>(target.Part.at("exactProfile"));
        return FixturePlateService(scene)->ReadProfile(*FixtureGeometry(scene,target.Fixture->GetGeometryRef()),RigidMatrix(target.Part.at("frame").To<Array>()),FiniteNumber(target.Part.at("thickness"),"PLATE_DIMENSION_INVALID"));
    }
    std::set<size_t> EditableFixtureHoles(const Map& part,size_t count){return FixtureHoleIndices(part,part.contains("editableHoleIndices")?"editableHoleIndices":"windowHoleIndices",count);}
    Array ProtectedFixtureBoundaryIDs(const Map& part,const FixturePartGeometry::Tube::CPlanarRegion2& profile){
        Array result;if(profile.Boundaries.empty())throw std::runtime_error("CAM_FIXTURE_PART_PROFILE_EMPTY");result.emplace_back(profile.Boundaries.front().ID);const auto editable=EditableFixtureHoles(part,profile.Boundaries.size()-1);
        for(size_t i=1;i<profile.Boundaries.size();++i)if(!editable.contains(i-1))result.emplace_back(profile.Boundaries[i].ID);return result;
    }
    FixturePartGeometry::Loop2 FixtureExactLoop(const FixturePartGeometry::Tube::SRegionLoop2& loop){FixturePartGeometry::Loop2 result;for(const auto& curve:loop.Curves)result.Segments.push_back(curve.Segment);return result;}
    std::vector<std::array<double,2>> FixturePreviewLoop(const Variant& value){std::vector<std::array<double,2>> result;for(const auto& point:value.To<Array>()){const auto p=point.To<Array>();if(p.size()!=2)throw std::invalid_argument("CAM_FIXTURE_PART_PROFILE_INVALID");result.push_back({FiniteNumber(p[0],"CAM_FIXTURE_PART_PROFILE_INVALID"),FiniteNumber(p[1],"CAM_FIXTURE_PART_PROFILE_INVALID")});}return result;}
    iCAX::Geometry::SectionPlate FixtureProtectionPlate(Scene& scene,const Map& part){
        iCAX::Geometry::SectionPlate plate;plate.Geometry=*FixtureGeometry(scene,part.at("geometryRef").To<Map>());plate.Frame=RigidMatrix(part.at("frame").To<Array>());plate.Thickness=FiniteNumber(part.at("thickness"),"PLATE_DIMENSION_INVALID");plate.Base=String(part,"role")=="Base";plate.Connection=String(part,"role")=="Connection";plate.Outline=FixturePreviewLoop(part.at("outline"));
        const auto span=part.at("span").To<Array>();plate.Span={FiniteNumber(span[0],"PLATE_DIMENSION_INVALID"),FiniteNumber(span[1],"PLATE_DIMENSION_INVALID")};for(const auto& hole:part.at("holes").To<Array>())plate.Holes.push_back(FixturePreviewLoop(hole));
        for(const auto& value:part.at("joints").To<Array>()){const auto joint=value.To<Map>();iCAX::Geometry::SectionPlateJoint j;j.Type=String(joint,"type");j.Boundary=FixturePreviewLoop(joint.at("boundary"));plate.Joints.push_back(std::move(j));}return plate;
    }
}
Map ReadFixturePartProfile(Scene& scene,const Map& request){
    const auto target=FixturePlate(scene,request);const auto profile=FixturePlateProfile(scene,target);
    return {{"unitId",request.at("unitId")},{"partId",request.at("partId")},{"geometryRef",WireRef(target.Part.at("geometryRef").To<Map>())},{"frame",target.Part.at("frame")},{"thickness",target.Part.at("thickness")},
        {"profile",FixturePartJson::Encode(profile)},{"protectedBoundaryIds",ProtectedFixtureBoundaryIDs(target.Part,profile)}};
}
Map UpdateFixturePart(Scene& scene,const Map& request){
    return FixtureDesignCommand(scene,request,"FixturePartUpdate",[&](const ID& setupID,Map){
        if(request.contains("transform")||request.contains("frame"))throw std::invalid_argument("CAM_FIXTURE_GENERATED_TRANSFORM_FIXED");if(request.contains("thickness"))throw std::invalid_argument("CAM_FIXTURE_PART_THICKNESS_FIXED");
        const auto target=FixturePlate(scene,request);RequireSetupUnlocked(scene,setupID);if(target.Unit->GetLocked()||target.Fixture->GetLocked())throw std::invalid_argument("CAM_FIXTURE_LOCKED");
        const auto expectedGeometry=ReadWireRef(request.at("geometryRef").To<Map>()),currentGeometry=ReadWireRef(WireRef(target.Fixture->GetGeometryRef()));
        if(expectedGeometry!=currentGeometry)throw std::invalid_argument("CAM_FIXTURE_PART_GEOMETRY_STALE");
        const auto service=FixturePlateService(scene);const auto original=FixturePlateProfile(scene,target),submitted=FixturePartJson::Decode<FixturePartGeometry::Tube::CPlanarRegion2>(request.at("profile"));
        if(submitted.Boundaries.empty()||submitted.FillRule!=original.FillRule||FixturePartJson::Encode(submitted.Boundaries.front())!=FixturePartJson::Encode(original.Boundaries.front()))throw std::invalid_argument("CAM_FIXTURE_PART_OUTLINE_FIXED");
        std::map<Text,size_t> byID;for(size_t i=0;i<submitted.Boundaries.size();++i)if(submitted.Boundaries[i].ID.empty()||!byID.emplace(submitted.Boundaries[i].ID,i).second)throw std::invalid_argument("CAM_FIXTURE_PART_BOUNDARY_ID_INVALID");
        const auto editable=EditableFixtureHoles(target.Part,original.Boundaries.size()-1),windows=FixtureHoleIndices(target.Part,"windowHoleIndices",original.Boundaries.size()-1);std::set<Text> protectedIDs,windowIDs;
        iCAX::Geometry::SectionPlateHoleEditInput input;input.OriginalGeometry=*FixtureGeometry(scene,target.Fixture->GetGeometryRef());input.Frame=RigidMatrix(target.Part.at("frame").To<Array>());input.Thickness=FiniteNumber(target.Part.at("thickness"),"PLATE_DIMENSION_INVALID");
        // Automatic layout spacing does not constrain a manually authored
        // cutout. Preserve the actual assembly's connections separately.
        input.MinimumWeb=0;
        for(size_t i=1;i<original.Boundaries.size();++i){const auto& loop=original.Boundaries[i];if(windows.contains(i-1))windowIDs.insert(loop.ID);if(editable.contains(i-1))continue;
            protectedIDs.insert(loop.ID);const auto found=byID.find(loop.ID);if(found==byID.end()||FixturePartJson::Encode(submitted.Boundaries[found->second])!=FixturePartJson::Encode(loop))throw std::invalid_argument("CAM_FIXTURE_PART_PROTECTED_HOLE");input.ProtectedVoids.push_back(FixtureExactLoop(loop));}
        Array editableIndices,windowIndices;for(size_t i=1;i<submitted.Boundaries.size();++i){const auto& loop=submitted.Boundaries[i];input.Holes.push_back(FixtureExactLoop(loop));if(!protectedIDs.contains(loop.ID))editableIndices.emplace_back(static_cast<UInt>(i-1));if(windowIDs.contains(loop.ID))windowIndices.emplace_back(static_cast<UInt>(i-1));}
        std::vector<iCAX::Geometry::SectionPlate> assembly;for(const auto& value:target.Assembly.at("parts").To<Array>())assembly.push_back(FixtureProtectionPlate(scene,value.To<Map>()));
        input.ProtectedMaterial=service->ReadProtection(assembly.at(target.Index),assembly,0,false);
        auto edited=service->EditHoles(input);auto normalized=edited.Profile;
        if(edited.HoleInputIndices.size()+1!=normalized.Boundaries.size()||edited.EqualInput.size()!=edited.HoleInputIndices.size())throw std::runtime_error("CAM_FIXTURE_PART_PROFILE_ORDER_INVALID");
        if(!edited.OutlineChanged)normalized.Boundaries.front()=submitted.Boundaries.front();
        editableIndices.clear();windowIndices.clear();std::set<Text> retainedIDs{normalized.Boundaries.front().ID};
        for(size_t i=0;i<edited.HoleInputIndices.size();++i){const auto& contributors=edited.HoleInputIndices[i];if(contributors.empty())throw std::runtime_error("CAM_FIXTURE_PART_PROFILE_ORDER_INVALID");
            const auto equal=edited.EqualInput[i];const auto source=equal<input.Holes.size()?equal:contributors.front();auto& loop=normalized.Boundaries[i+1];
            if(equal<input.Holes.size())loop=submitted.Boundaries[source+1];
            else{loop.ID=submitted.Boundaries[source+1].ID;for(size_t c=0;c<loop.Curves.size();++c)loop.Curves[c].ID=loop.ID+"/C"+std::to_string(c);}
            if(!retainedIDs.insert(loop.ID).second)throw std::runtime_error("CAM_FIXTURE_PART_PROFILE_ORDER_INVALID");
            if(!protectedIDs.contains(loop.ID))editableIndices.emplace_back(static_cast<UInt>(i));
            if(std::any_of(contributors.begin(),contributors.end(),[&](size_t sourceIndex){return windowIDs.contains(submitted.Boundaries[sourceIndex+1].ID);}))windowIndices.emplace_back(static_cast<UInt>(i));
        }
        for(const auto& id:protectedIDs){const auto found=std::find_if(normalized.Boundaries.cbegin()+1,normalized.Boundaries.cend(),[&](const auto& loop){return loop.ID==id;}),before=std::find_if(original.Boundaries.cbegin()+1,original.Boundaries.cend(),[&](const auto& loop){return loop.ID==id;});if(found==normalized.Boundaries.cend()||FixturePartJson::Encode(*found)!=FixturePartJson::Encode(*before))throw std::invalid_argument("CAM_FIXTURE_PART_PROTECTED_HOLE");}
        const auto display=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();if(!display)throw std::runtime_error("CAM_DISPLAY_SERVICE_UNAVAILABLE");const auto edges=display->BuildEdges(edited.Geometry,{});
        const auto oldRef=*iCAX::ProjectFile::TryGetResourceReferenceValue(target.Fixture->GetGeometryRef());Ref geometry,preview;bool committed=false;
        try{
            geometry=Publish(scene,std::make_shared<FixturePartGeometry::BRepModel>(edited.Geometry),"geometry.brep","编辑夹具板件",{Ref{oldRef.URL,oldRef.nVersion}});
            auto info=*scene.Resources().GetInfo(geometry.URL,geometry.nVersion);info.Key={};info.ResourceID={};info.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;info.ResourceTypeID=iCAX::Resource::CFlatBufferResource::kResourceTypeName;info.Dependencies={geometry};
            preview={scene.Resources().AllocateResourceURL(),1};auto render=std::make_shared<iCAX::Resource::CFlatBufferResource>(iCAX::RenderInteraction::MakeFrontendBRepGeometry(edited.Geometry,edges,1));info.nSize=render->Size();scene.Resources().Set(preview.URL,render,info);
            auto part=target.Part;const auto loop=[](const auto& points){Array result;for(auto p:points)result.emplace_back(Array{p[0],p[1]});return result;};Array holes;for(const auto& hole:edited.PreviewHoles)holes.emplace_back(loop(hole));
            part["geometryRef"]=StoredRef(geometry);part["previewRef"]=StoredRef(preview);part["outline"]=loop(edited.PreviewOutline);part["holes"]=holes;part["exactProfile"]=FixturePartJson::Encode(normalized);part["editableHoleIndices"]=editableIndices;part["windowHoleIndices"]=windowIndices;
            auto design=target.Design,assemblyValue=target.Assembly;auto parts=assemblyValue.at("parts").To<Array>();parts[target.Index]=part;assemblyValue["parts"]=parts;design["assembly"]=assemblyValue;InvalidateFixtureAssemblyEdits(design);
            Transaction(scene.Database(),"编辑夹具板件孔",[&](auto& tx){
                tx.ModifyComponent(target.PartID,CFiveAxisFixtureComponent::S_ClassName,{{"GeometryRef",StoredRef(geometry)},{"PreviewRef",StoredRef(preview)},{"InputRevision",Next(target.Fixture->GetInputRevision())}});
                tx.ModifyComponent(target.PartID,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
                StoreFixtureUnitDesign(scene,tx,target.UnitID,design);Invalidate(tx,scene.Database(),setupID);
            });committed=true;
            return Map{{"unitId",request.at("unitId")},{"partId",request.at("partId")},{"geometryRef",WireRef(part.at("geometryRef").To<Map>())},{"previewRef",WireRef(part.at("previewRef").To<Map>())}};
        }catch(...){if(!committed){if(!preview.URL.empty())scene.Resources().Delete(preview.URL);if(!geometry.URL.empty())scene.Resources().Delete(geometry.URL);}throw;}
    });
}
