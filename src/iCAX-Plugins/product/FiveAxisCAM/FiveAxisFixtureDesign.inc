namespace {
    Map FixtureFields(const Map&,std::initializer_list<const char*>);
    Map FixtureAssemblySummary(const Map&);
    Map FixtureDesignSummary(const Map&);
    Text FixtureCutClearanceKey(Scene&,const ID&,const Map&);
    void ValidateFixtureCutClearanceRecipe(const Map&);
    bool SameFixtureGeometryInput(Scene&,const ID&,const Map&,const Map&);
    iCAX::Tasks::Task<std::shared_ptr<PlateGenerationResult>> StartFixturePlateWorker(Scene&,const ID&,const Map&,const iCAX::Geometry::SectionPlateInput&,const std::shared_ptr<std::atomic_bool>&);
    constexpr double FixtureOpeningBoundaryMargin=1.;
    Text NewFixtureID(){return iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());}
    std::array<double,3> FixtureSupportDirection(const Map& r){
        if(!r.contains("supportDirection"))return {0,0,1};
        if(!r.at("supportDirection").Is<Array>())throw std::invalid_argument("CAM_FIXTURE_SUPPORT_DIRECTION_INVALID");
        const auto values=r.at("supportDirection").To<Array>();if(values.size()!=3)throw std::invalid_argument("CAM_FIXTURE_SUPPORT_DIRECTION_INVALID");
        std::array<double,3> direction;double scale=0;for(size_t i=0;i<3;++i){direction[i]=FiniteNumber(values[i],"CAM_FIXTURE_SUPPORT_DIRECTION_INVALID");scale=std::max(scale,std::abs(direction[i]));}
        if(scale==0)throw std::invalid_argument("CAM_FIXTURE_SUPPORT_DIRECTION_INVALID");for(auto& n:direction)n/=scale;
        const double length=std::hypot(direction[0],direction[1],direction[2]);for(auto& n:direction)n/=length;return direction;
    }
    Map FixtureDesignSource(Scene& scene,const ID& setupID) {
        for(const auto& e:scene.Database().FilterEntities([](auto e){return e->HasComponent(CFiveAxisPlacementComponent::S_ClassName);})) {
            const auto p=Component<CFiveAxisPlacementComponent>(e);if(p->GetSetupID()!=setupID)continue;
            const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
            return {{"placementId",iCAX::Data::to_string(e->GetID())},{"geometry",w->GetGeometryRef()},{"transform",p->GetTransform()}};
        }
        throw std::invalid_argument("CAM_FIXTURE_WORKPIECE_REQUIRED");
    }
    Text FixtureDesignKey(Scene& scene,const ID& setupID,const Map& recipe) {
        return iCAX::Data::VariantSerializer::Serialize(Map{{"generator",Text("PlateSurface.7")},{"source",FixtureDesignSource(scene,setupID)},{"recipe",recipe},{"cutClearance",FixtureCutClearanceKey(scene,setupID,recipe)}});
    }
    bool SameFixtureGeometryInput(Scene& scene,const ID& id,const Map& design,const Map& assembly){
        try{auto old=iCAX::Data::VariantSerializer::Deserialize(String(assembly,"inputKey")).To<Map>();
            auto now=iCAX::Data::VariantSerializer::Deserialize(FixtureDesignKey(scene,id,design.at("recipe").To<Map>())).To<Map>();old.erase("cutClearance");now.erase("cutClearance");return old==now;
        }catch(...){return false;}
    }
    bool CurrentFixtureGeometry(Scene& scene,const ID& setupID,const Map& design,const Map& assembly,bool checkEntities,bool forceLive=false) {
        const bool snapshot=!forceLive&&design.contains("unitSnapshot")&&design.at("unitSnapshot").To<bool>();
        if(assembly.empty()||!design.contains("recipe")||(!snapshot&&String(assembly,"inputKey")!=FixtureDesignKey(scene,setupID,design.at("recipe").To<Map>())&&!SameFixtureGeometryInput(scene,setupID,design,assembly)))return false;
        const auto machiningKey=FixtureCutClearanceKey(scene,setupID,design.at("recipe").To<Map>());
        if(!machiningKey.empty()&&!SameFixtureGeometryInput(scene,setupID,design,assembly))return false;
        const bool adopted=design.contains("assembly")&&String(std::get<Map>(design.at("assembly").m_Value),"id")==String(assembly,"id");
        if(checkEntities||(!machiningKey.empty()&&adopted))for(const auto& value:std::get<Array>(assembly.at("parts").m_Value)) {
            const auto& p=std::get<Map>(value.m_Value);auto f=Component<CFiveAxisFixtureComponent>(scene.Database().GetEntity(Identifier(p,"partId")));
            if(!f||f->GetSetupID()!=setupID||f->GetGeometryRef()!=p.at("geometryRef").To<Map>()||((!snapshot||!machiningKey.empty())&&(f->GetTransform()!=Identity()||!f->GetCollisionEnabled())))return false;
            if(!machiningKey.empty()&&design.contains("unitId")&&f->GetUnitID()!=Identifier(design,"unitId"))return false;
        }
        return true;
    }
    bool CurrentFixtureAssembly(Scene& scene,const ID& setupID,const Map& design,const Map& assembly,bool checkEntities) {
        return CurrentFixtureGeometry(scene,setupID,design,assembly,checkEntities);
    }
    Map ProjectFixtureAssembly(Scene& scene,const ID& setupID,const Map& design,const Map& source,bool adopted,bool summary=false){
        const bool geometry=CurrentFixtureGeometry(scene,setupID,design,source,adopted),current=CurrentFixtureAssembly(scene,setupID,design,source,adopted);
        Map a=summary?FixtureAssemblySummary(source):source;a["geometryValidity"]=Text(geometry?"Current":"Stale");
        if(adopted){
            const auto& parts=std::get<Array>(source.at("parts").m_Value);Array projected;UInt enabled=0;
            for(const auto& value:parts){const auto& part=std::get<Map>(value.m_Value);const auto fixture=Component<CFiveAxisFixtureComponent>(scene.Database().GetEntity(Identifier(part,"partId")));const bool active=fixture&&fixture->GetEnabled();enabled+=active;
                if(!summary){auto row=part;row["enabled"]=active;projected.emplace_back(std::move(row));}}
            if(summary)a["enabledPartCount"]=enabled;else a["parts"]=std::move(projected);
        }
        if(a.contains("openingPreclearance")){
            // Frozen unit geometry remains reusable after moving it; the
            // opening reservation only belongs to its original live input.
            const bool openingCurrent=CurrentFixtureGeometry(scene,setupID,design,source,adopted,true);
            auto opening=a.at("openingPreclearance").To<Map>();opening["validity"]=Text(openingCurrent?"Current":"Stale");if(!openingCurrent)opening["status"]=Text("Unchecked");a["openingPreclearance"]=opening;
        }
        a["validity"]=Text(current?"Current":"Stale");
        return a;
    }
    Map ProjectFixtureDesignState(Scene& scene,const ID& setupID,const Map& source,bool summary=false) {
        Map d=summary?FixtureDesignSummary(source):source;
        for(const auto* name:{"candidate","assembly"})if(source.contains(name)) {
            d[name]=ProjectFixtureAssembly(scene,setupID,source,std::get<Map>(source.at(name).m_Value),Text(name)=="assembly",summary);
        }
        return d;
    }
    Map FixtureDesignState(Scene& scene,const ID& setupID) {return ProjectFixtureDesignState(scene,setupID,Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetFixtureDesign());}
    Map FixtureValidityState(Scene& scene,const ID& setupID,const Map& design){
        Map result;
        for(const auto* name:{"candidate","assembly"})if(design.contains(name)){
            const auto assembly=ProjectFixtureAssembly(scene,setupID,design,design.at(name).To<Map>(),Text(name)=="assembly");
            Map state{{"id",assembly.at("id")},{"validity",assembly.at("validity")},{"geometryValidity",assembly.at("geometryValidity")}};
            if(assembly.contains("cutClearance"))state["cutClearance"]=Map{{"validity",assembly.at("cutClearance").To<Map>().at("validity")},{"status",assembly.at("cutClearance").To<Map>().at("status")}};
            if(assembly.contains("openingPreclearance"))state["openingPreclearance"]=Map{{"validity",assembly.at("openingPreclearance").To<Map>().at("validity")},{"status",assembly.at("openingPreclearance").To<Map>().at("status")}};
            result[name]=state;
        }
        return result;
    }
    Map FixtureValidityPatch(Scene& scene,const ID& setupID){
        Map units;for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
            if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(entity);unit&&unit->GetSetupID()==setupID)
                units[iCAX::Data::to_string(entity->GetID())]=FixtureValidityState(scene,setupID,unit->GetDesign());
        return {{"setup",FixtureValidityState(scene,setupID,Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetFixtureDesign())},{"units",units}};
    }
    template<class F>Map FixtureDesignCommand(Scene& scene,const Map& request,const Text& method,F fn) {
        auto runtime=Runtime(scene);const auto command=String(request,"commandId"),payload=method+iCAX::Data::VariantSerializer::Serialize(request);
        (void)Identifier(request,"commandId");
        if(auto i=runtime->Receipts.find(command);i!=runtime->Receipts.end()){if(i->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return i->second.second;}
        for(const auto&[_,t]:runtime->Tasks)if(t.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto id=Identifier(request,"setupId");
        auto d=Require<CFiveAxisSetupComponent>(scene.Database(),id)->GetFixtureDesign();
        if(request.contains("unitId")){const auto unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),Identifier(request,"unitId"));if(unit->GetSetupID()!=id)throw std::invalid_argument("CAM_SETUP_MISMATCH");d=unit->GetDesign();}
        auto result=fn(id,d);
        runtime->Receipts.emplace(command,std::make_pair(payload,result));PumpTasks(scene);return result;
    }
    void SaveFixtureDesign(Scene& scene,const ID& id,const Map& d,const Text& title,bool invalidate=false,const ID& unitID=ID()) {
        Transaction(scene.Database(),title,[&](auto& tx){if(unitID.is_nil())tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"FixtureDesign",d}});else tx.ModifyComponent(unitID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"Design",d}});if(invalidate)Invalidate(tx,scene.Database(),id);});
    }
    iCAX::Geometry::SectionPlateInput PlateInput(Scene& scene,const ID& id,const Map& r) {
        ValidateFixtureCutClearanceRecipe(r);
        iCAX::Geometry::SectionPlateInput input;input.SolidBase=true;const auto source=FixtureDesignSource(scene,id);
        input.Geometry=FixtureGeometry(scene,source.at("geometry").To<Map>());input.Transform=RigidMatrix(source.at("transform").To<Array>());
        input.SupportDirection=FixtureSupportDirection(r);
        // Existing opening boundaries can be reserved before authoring a path.
        // This geometric preparation never substitutes for oriented beam checks.
        input.AvoidOpeningBoundaries=true;input.OpeningBoundaryMargin=FixtureOpeningBoundaryMargin;
        if(r.contains("openingReliefDepth")){input.OpeningReliefDepth=FiniteNumber(r.at("openingReliefDepth"),"PLATE_OPENING_DEPTH_INVALID");if(input.OpeningReliefDepth<.1||input.OpeningReliefDepth>1000)throw std::invalid_argument("PLATE_OPENING_DEPTH_INVALID");}
        const auto family=String(r,"family");if(family!="ParallelX"&&family!="ParallelY")throw std::invalid_argument("CAM_FIXTURE_FAMILY_UNSUPPORTED");input.AlongX=family=="ParallelX";
        const auto layout=String(r,"layout");if(layout!="Automatic"&&layout!="Manual")throw std::invalid_argument("CAM_FIXTURE_LAYOUT_REQUIRED");input.Automatic=layout=="Automatic";
        input.Pattern=String(r,"pattern");if(input.Pattern!="Skeleton"&&input.Pattern!="Grid"&&input.Pattern!="Parallel"&&input.Pattern!="Staggered")throw std::invalid_argument("PLATE_PATTERN_INVALID");
        if(input.Automatic&&input.Pattern=="Skeleton"){
            input.CountLayout=r.contains("supportCount")||r.contains("connectionCount");
            if(input.CountLayout){
                const auto count=[&](const char* key){if(!r.contains(key))throw std::invalid_argument("PLATE_COUNT_INVALID");const double n=FiniteNumber(r.at(key),"PLATE_COUNT_INVALID");if(n<0||n==1||n>32||n!=std::floor(n))throw std::invalid_argument("PLATE_COUNT_INVALID");return static_cast<size_t>(n);};
                input.SupportCount=count("supportCount");input.ConnectionCount=count("connectionCount");
                const auto positions=[&](const char* key,std::vector<double>& values){if(r.contains(key))for(const auto& value:r.at(key).To<Array>())values.push_back(FiniteNumber(value,"PLATE_POSITION_INVALID"));};
                positions("supportPositions",input.SupportPositions);positions("connectionPositions",input.ConnectionPositions);
            }else input.MaximumSupportSpan=FiniteNumber(r.at("spacing"),"PLATE_SPACING_INVALID");
        }
        input.BaseShape=String(r,"baseShape");if(input.BaseShape!="Rectangle"&&input.BaseShape!="Circle")throw std::invalid_argument("PLATE_BASE_SHAPE_INVALID");
        const auto sizing=String(r,"sizing");if(sizing!="Automatic"&&sizing!="Manual")throw std::invalid_argument("PLATE_AREA_INVALID");
        if(sizing=="Manual"){input.AreaWidth=FiniteNumber(r.at("areaWidth"),"PLATE_AREA_INVALID");input.AreaDepth=input.BaseShape=="Circle"?input.AreaWidth:FiniteNumber(r.at("areaDepth"),"PLATE_AREA_INVALID");if(input.AreaWidth<10||input.AreaDepth<10||input.AreaWidth>10000||input.AreaDepth>10000)throw std::invalid_argument("PLATE_AREA_INVALID");input.AreaX=FiniteNumber(r.at("areaX"),"PLATE_AREA_INVALID");input.AreaY=FiniteNumber(r.at("areaY"),"PLATE_AREA_INVALID");}
        input.PostWidth=FiniteNumber(r.at("postWidth"),"PLATE_POST_WIDTH_INVALID");
        const auto material=String(r,"material");if(material!="CarbonSteel"&&material!="StainlessSteel"&&material!="Aluminum")throw std::invalid_argument("CAM_FIXTURE_MATERIAL_REQUIRED");
        for(const auto& f:r.at("supportFaces").To<Array>()){const auto s=f.To<Text>();std::uint64_t n=0;auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);if(parsed.ec!=std::errc()||parsed.ptr!=s.data()+s.size()||!n)throw std::invalid_argument("PLATE_FACE_MISSING");input.FaceIDs.push_back(n);}
        if(input.FaceIDs.empty())throw std::invalid_argument("PLATE_INPUT_REQUIRED");
        std::set<Text> ids;double previous=-1e100;
        for(const auto& s:r.at("stations").To<Array>()){const auto v=s.To<Map>();(void)Identifier(v,"id");if(!ids.insert(String(v,"id")).second)throw std::invalid_argument("CAM_FIXTURE_STATION_ID_DUPLICATE");double n=FiniteNumber(v.at("coordinate"),"PLATE_DIMENSION_INVALID");
            iCAX::Geometry::SupportStation station;station.Coordinate=n;station.AlongX=input.AlongX;
            if(input.Automatic){station.AlongX=String(v,"axis")=="X";station.From=FiniteNumber(v.at("from"),"PLATE_DIMENSION_INVALID");station.To=FiniteNumber(v.at("to"),"PLATE_DIMENSION_INVALID");if(input.Pattern=="Skeleton")station.Connection=v.at("connection").To<bool>();if(input.CountLayout){const double index=FiniteNumber(v.at("layoutIndex"),"PLATE_POSITION_INVALID");if(index<0||index>31||index!=std::floor(index))throw std::invalid_argument("PLATE_POSITION_INVALID");station.LayoutIndex=static_cast<size_t>(index);station.NominalCoordinate=FiniteNumber(v.at("nominalCoordinate"),"PLATE_POSITION_INVALID");}}
            else if(n<=previous||(!input.Stations.empty()&&n-previous<FiniteNumber(r.at("thickness"),"PLATE_DIMENSION_INVALID")+.001))throw std::invalid_argument("PLATE_STATION_OVERLAP");previous=n;input.Stations.push_back(station);}
        if((input.Stations.empty()&&!input.Automatic)||input.Stations.size()>128)throw std::invalid_argument("PLATE_INPUT_REQUIRED");
        auto dimension=[&](const char* key,double min,double max){double n=FiniteNumber(r.at(key),"PLATE_DIMENSION_INVALID");if(n<min||n>max)throw std::invalid_argument("PLATE_DIMENSION_INVALID");return n;};
        input.Thickness=dimension("thickness",.5,50);input.BaseThickness=dimension("baseThickness",.5,100);input.BaseDrop=dimension("baseDrop",5,1000);
        input.Margin=dimension("margin",1,500);input.Gap=dimension("gap",.01,10);input.MinimumWeb=dimension("minimumWeb",1,100);
        input.JointMode=String(r,"jointMode");if(input.JointMode!="Weld"&&input.JointMode!="TabSlot")throw std::invalid_argument("PLATE_JOINT_MODE_INVALID");
        if(input.JointMode=="TabSlot"){
            const auto jointDimension=[&](const char* key,double first,double last){double n=FiniteNumber(r.at(key),"PLATE_JOINT_DIMENSION_INVALID");if(n<first||n>last)throw std::invalid_argument("PLATE_JOINT_DIMENSION_INVALID");return n;};
            input.JointClearance=jointDimension("jointClearance",0,std::min(input.Thickness,input.BaseThickness)/2);input.JointWeb=jointDimension("jointWeb",.5,50);input.TenonWidth=jointDimension("tenonWidth",input.Thickness,500);input.TenonPitch=jointDimension("tenonPitch",input.TenonWidth+2*input.JointWeb+input.JointClearance,1000);
            input.TenonRadius=jointDimension("tenonRadius",0,std::min((input.TenonWidth-1e-5)/2,input.BaseThickness/2));
        }
        if(r.contains("windowMode"))input.WindowMode=String(r,"windowMode");
        if(input.WindowMode!="None"&&input.WindowMode!="Automatic")throw std::invalid_argument("PLATE_WINDOW_MODE_INVALID");
        if(input.WindowMode=="Automatic"){
            if(!r.contains("windowWeb"))throw std::invalid_argument("PLATE_WINDOW_WEB_INVALID");
            input.WindowWeb=FiniteNumber(r.at("windowWeb"),"PLATE_WINDOW_WEB_INVALID");
            if(input.WindowWeb<input.Thickness||input.WindowWeb>500)throw std::invalid_argument("PLATE_WINDOW_WEB_INVALID");
        }
        return input;
    }
    void FinishFixtureSupportFaces(Scene& scene,SceneRuntime& runtime,ImportTask& task) {
        try {
            const auto faces=task.SupportFaceWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence||task.ClearanceInputKey!=iCAX::Data::VariantSerializer::Serialize(FixtureDesignSource(scene,task.SetupID)))task.Status="Superseded";
            else {Array selected;for(auto id:*faces)selected.emplace_back(std::to_string(id));task.SupportSelection={{"supportFaces",selected},{"basis",Text("SampledTrimmedMeshLowerEnvelope")},{"meshDeflectionMm",.025}};task.Result={{"selectionId",task.TaskID},{"selectedFaceCount",static_cast<UInt>(selected.size())}};task.Status="Completed";}
        }catch(const std::exception& e){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}
        task.SupportFaceWorker={};
    }
    void FinishFixtureGeneration(Scene& scene,SceneRuntime& runtime,ImportTask& task) {
        try {
            auto prepared=task.PlateWorker.Result();auto* plates=&prepared->Plates;
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence||task.ClearanceInputKey!=FixtureDesignKey(scene,task.SetupID,task.FixtureRecipe))task.Status="Superseded";
            else {
                auto design=Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID)->GetFixtureDesign();
                const auto& r=task.FixtureRecipe;const auto source=FixtureDesignSource(scene,task.SetupID);
                const auto sourceRef=*iCAX::ProjectFile::TryGetResourceReferenceValue(source.at("geometry").To<Map>());
                Array parts;const auto stations=r.at("stations").To<Array>();
                if(prepared->PlateDisplayEdges.size()!=plates->size())throw std::runtime_error("CAM_DISPLAY_SERVICE_UNAVAILABLE");
                for(size_t i=0;i<plates->size();++i){auto& p=plates->at(i);const auto& edges=prepared->PlateDisplayEdges[i];
                    Map exactProfile;if(!p.Base&&String(r,"jointMode")=="TabSlot"&&FiniteNumber(r.at("tenonRadius"),"PLATE_TENON_RADIUS_INVALID")>0){
                        if(p.ExactProfile.Boundaries.empty())throw std::runtime_error("PLATE_PROFILE_CURVE_INVALID");
                        exactProfile=iCAX::CAD::TubeCadJson::Encode(p.ExactProfile).To<Map>();
                    }
                    auto dependencies=prepared->Dependencies;dependencies.push_back({sourceRef.URL,sourceRef.nVersion});
                    const auto geometry=Publish(scene,std::make_shared<iCAX::GeometryData::BRepModel>(std::move(p.Geometry)),"geometry.brep","夹具板件",dependencies);
                    if(task.FixtureDraft){auto info=*scene.Resources().GetInfo(geometry.URL,geometry.nVersion);info.Persistence=iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;scene.Resources().UpdateInfo(geometry.URL,info);task.FixtureDraftResources.emplace_back(geometry,true);}
                    const auto transient=iCAX::RenderInteraction::EnsureFrontendBRepGeometryResource(scene.Resources(),geometry.URL,edges);
                    if(task.FixtureDraft)task.FixtureDraftResources.emplace_back(transient,false);
                    auto info=*scene.Resources().GetInfo(transient.URL,transient.nVersion);info.Persistence=task.FixtureDraft?iCAX::Resource::EResourcePersistenceMode::RuntimeOnly:iCAX::Resource::EResourcePersistenceMode::Embedded;info.Key={};info.ResourceID={};
                    info.ResourceTypeID=iCAX::Resource::CFlatBufferResource::kResourceTypeName;
                    const Ref preview{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(preview.URL,std::make_shared<iCAX::Resource::CFlatBufferResource>(*scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(transient.URL,transient.nVersion)),info);
                    if(task.FixtureDraft)task.FixtureDraftResources.emplace_back(preview,true);
                    Array outline,frame,contacts,holes,joints,windows;for(auto index:p.WindowHoleIndices)windows.emplace_back(static_cast<UInt>(index));const auto loop=[](const auto& points){Array a;for(auto q:points)a.emplace_back(Array{q[0],q[1]});return a;};outline=loop(p.Outline);for(const auto& h:p.Holes)holes.emplace_back(loop(h));for(auto n:p.Frame)frame.emplace_back(n);for(auto q:p.Contacts)contacts.emplace_back(Array{q[0],q[1],q[2]});
                    for(const auto& joint:p.Joints)joints.emplace_back(Map{{"id",Text("J")+std::to_string(joint.Index)},{"type",joint.Type},{"matePartId",joint.MateBase?r.at("baseId"):stations[joint.MateStationIndex].To<Map>().at("id")},{"boundary",loop(joint.Boundary)}});
                    const auto station=p.Base?Map():stations[p.StationIndex].To<Map>();
                    parts.emplace_back(Map{{"partId",p.Base?r.at("baseId"):station.at("id")},{"number",p.Base?Text("B01"):Text(p.Connection?"C":"S")+String(station,"number")},
                        {"role",Text(p.Base?"Base":p.Connection?"Connection":"Support")},{"material",r.at("material")},{"geometryRef",StoredRef(geometry)},{"previewRef",StoredRef(preview)},
                        {"outline",outline},{"holes",holes},{"windowHoleIndices",windows},{"enabled",true},{"joints",joints},{"span",Array{p.Span[0],p.Span[1]}},{"frame",frame},{"thickness",p.Thickness},{"minimumDistance",p.Distance},{"topRetreat",p.TopRetreat},
                        {"meshDeflectionMm",p.MeshDeflection},{"clearanceAllowanceMm",p.ClearanceAllowance},{"clearanceMethod",Text(p.Base&&p.MeshDeflection==0?"SupportDirectionBounds":p.Base||p.Connection?"CoalMeshWithAllowance":"UnderlyingSurfaceEnvelope")},{"reliefCount",static_cast<UInt>(p.ReliefCount)},
                        {"keepOutReliefCount",static_cast<UInt>(p.KeepOutReliefCount)},
                        {"contactPoints",contacts},{"contactLengthMm",p.ContactLength}});
                    if(!exactProfile.empty())std::get<Map>(parts.back().m_Value)["exactProfile"]=exactProfile;
                }
                const auto& base=plates->back();Array hull;for(auto q:base.SupportHull)hull.emplace_back(Array{q[0],q[1]});
                Array projectionFrame;for(auto n:base.ProjectionFrame)projectionFrame.emplace_back(n);
                Map supportCheck{{"status",Text(base.Supported?"Supported":"Insufficient")},{"hull",hull},{"projectionFrame",projectionFrame},{"centerOfMass",Array{base.CenterOfMass[0],base.CenterOfMass[1],base.CenterOfMass[2]}},
                    {"centerMethod",base.CenterMethod},{"marginMm",base.SupportMargin},{"basis",Text("Nominal mesh contact and projection along support direction; no clamping, gravity load or strength qualification")}};
                if(String(r,"layout")=="Automatic"&&String(r,"pattern")=="Skeleton"){
                    supportCheck["maximumSpanMm"]=base.MaximumUnsupportedSpan;supportCheck["maximumEndOverhangMm"]=base.MaximumEndOverhang;if(!r.contains("supportCount"))supportCheck["allowedSpanMm"]=r.at("spacing");
                }
                UInt tenons=static_cast<UInt>(base.Joints.size()),laps=0;for(const auto& p:*plates)for(const auto& j:p.Joints)if(j.Type=="LapTop")++laps;
                Map assembly{{"id",NewFixtureID()},{"inputKey",task.ClearanceInputKey},{"parts",parts},{"status",Text("GeometryChecked")},
                    {"joint",Text(String(r,"jointMode")=="TabSlot"?"TabSlot":"LocateAndWeld")},{"joinery",Map{{"baseTenons",tenons},{"crossLaps",laps}}},{"supportCheck",supportCheck},{"coverage",Text("Selected underlying surfaces limit the full-thickness slab envelope; bearing contacts remain on actual trimmed material. Support-direction contact footprint and non-contact clearance checked. Cut/lead beam and head avoidance are reported by cutClearance. Rapids, clamping, strength and machine motion require separate checks")}};
                if(r.contains("supportCount")){
                    Array supportPositions,connectionPositions;
                    for(const auto& p:*plates)if(!p.Base){const auto station=stations[p.StationIndex].To<Map>();(p.Connection?connectionPositions:supportPositions).push_back(station.at("coordinate"));}
                    assembly["layoutSummary"]=Map{{"axis",r.at("layoutAxis")},{"supportCount",r.at("supportCount")},{"connectionCount",r.at("connectionCount")},{"supportPositions",supportPositions},{"connectionPositions",connectionPositions},{"maximumSpacingMm",base.MaximumUnsupportedSpan}};
                }
                auto cutClearance=prepared->CutClearance;UInt reliefs=0;for(const auto& p:*plates)reliefs+=static_cast<UInt>(p.KeepOutReliefCount);
                cutClearance["reliefCount"]=reliefs;cutClearance["rejectedStationCount"]=static_cast<UInt>(base.RejectedStationCount);assembly["cutClearance"]=cutClearance;
                assembly["openingPreclearance"]=Map{{"mode",Text("ExistingOpeningBoundaries")},{"status",Text(base.UnclassifiedOpeningFaceCount?"Partial":"Prepared")},{"validity",Text("Current")},
                    {"basis",Text("ConservativeSupportDirectionBoundaryEnvelope")},{"scope",Text("VerticalPlatePlanarBoundarySubset")},{"boundaryCount",static_cast<UInt>(base.OpeningBoundaryCount)},
                    {"nonExitBoundaryCount",static_cast<UInt>(base.OpeningNonExitBoundaryCount)},
                    {"unclassifiedFaceCount",static_cast<UInt>(base.UnclassifiedOpeningFaceCount)},{"marginMm",FixtureOpeningBoundaryMargin},
                    {"reliefDepthMm",r.contains("openingReliefDepth")?r.at("openingReliefDepth"):Variant(0.)},{"planarityToleranceMm",1e-7},{"maximumPlaneDeviationUpperMm",base.OpeningPlaneDeviationUpper},
                    {"toolpathRequired",false},{"toolpathStatus",Text("NotChecked")},{"productionQualified",false}};
                // The close-fitting supports and the visible workpiece need the
                // same accuracy. Keep the machining BRep resource untouched.
                const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(source,"placementId"));
                const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
                const auto oldPreview=*iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetPreviewRef());
                auto info=*scene.Resources().GetInfo(oldPreview.URL,oldPreview.nVersion);
                info.Key={};info.ResourceID={};info.nVersion=1;
                info.Persistence=task.FixtureDraft?iCAX::Resource::EResourcePersistenceMode::RuntimeOnly:iCAX::Resource::EResourcePersistenceMode::Embedded;
                info.ResourceTypeID=iCAX::Resource::CFlatBufferResource::kResourceTypeName;
                info.Dependencies={Ref{sourceRef.URL,sourceRef.nVersion}};
                const Ref workPreview{scene.Resources().AllocateResourceURL(),1};
                auto resource=std::make_shared<iCAX::Resource::CFlatBufferResource>(iCAX::RenderInteraction::MakeFrontendBRepGeometry(prepared->DisplayGeometry,prepared->DisplayEdges,1));
                info.nSize=resource->Size();scene.Resources().Set(workPreview.URL,resource,info);
                if(task.FixtureDraft)task.FixtureDraftResources.emplace_back(workPreview,true);
                auto metadata=workpiece->GetImportMetadata();std::map<UInt,UInt> faceIDs;Array faces;UInt first=0;
                for(const auto& face:prepared->DisplayGeometry.Faces)faceIDs.emplace(face.Triangulation3Id,face.Id);
                for(const auto& mesh:prepared->DisplayGeometry.Triangulations3){if(mesh.Geometry.Vertices.empty()||mesh.Geometry.Triangles.empty())continue;
                    const auto count=mesh.Geometry.Triangles.size();faces.emplace_back(Map{{"faceId",std::to_string(faceIDs.at(mesh.Id))},{"first",std::to_string(first)},{"count",std::to_string(count)}});first+=count;}
                metadata["displayMesh"]=Map{{"geometryRef",workpiece->GetGeometryRef()},{"previewRef",StoredRef(workPreview)},{"faces",faces},{"deflectionMm",prepared->DisplayDeflection}};
                design["candidate"]=assembly;
                task.FixtureModel=std::make_shared<FixtureGenerationModel>();task.FixtureModel->Assembly=assembly;
                task.FixtureModel->SourcePreview={{"workpieceId",iCAX::Data::to_string(placement->GetWorkpieceID())},{"previewRef",StoredRef(workPreview)},{"metadata",metadata}};
                task.Result={{"setupId",iCAX::Data::to_string(task.SetupID)},{"candidateId",assembly.at("id")},{"partCount",static_cast<UInt>(assembly.at("parts").To<Array>().size())}};
                if(task.FixtureDraft){
                    task.Status="Completed";task.PlateWorker={};return;
                }
                Transaction(scene.Database(),"生成夹具候选",[&](auto& tx){
                    tx.ModifyComponent(placement->GetWorkpieceID(),CFiveAxisWorkpieceComponent::S_ClassName,{{"PreviewRef",StoredRef(workPreview)},{"ImportMetadata",metadata}});
                    for(const auto& entity:scene.Database().FilterEntities([](auto e){return e->HasComponent(CFiveAxisPlacementComponent::S_ClassName);})){const auto p=Component<CFiveAxisPlacementComponent>(entity);
                        if(p->GetWorkpieceID()==placement->GetWorkpieceID())tx.ModifyComponent(entity->GetID(),iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",workPreview.URL},{"GeometryResourceVersion",1ull}});}
                    tx.ModifyComponent(task.SetupID,CFiveAxisSetupComponent::S_ClassName,{{"FixtureDesign",design}});
                });task.Status="Completed";
            }
        }catch(const std::exception& e){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}task.PlateWorker={};
    }
}
namespace {
    Map FixtureCountLayoutInfo(const std::vector<iCAX::Geometry::SupportStation>& stations){
        std::map<size_t,double> bearings,connections;bool alongX=true;
        for(const auto& s:stations){(s.Connection?connections:bearings)[s.LayoutIndex]=s.NominalCoordinate;if(!s.Connection)alongX=s.AlongX;}
        Array supportPositions,connectionPositions;for(const auto& [_,p]:bearings)supportPositions.emplace_back(p);for(const auto& [_,p]:connections)connectionPositions.emplace_back(p);
        return {{"axis",Text(alongX?"X":"Y")},{"supportCount",static_cast<UInt>(bearings.size())},{"connectionCount",static_cast<UInt>(connections.size())},{"supportPositions",supportPositions},{"connectionPositions",connectionPositions}};
    }
    Map NormalizeFixtureRecipe(Scene& scene,const ID& id,Map r,const Map& old,bool fresh){
        if(!r.contains("tenonRadius"))r["tenonRadius"]=1.;
        if(r.contains("supportCount")&&!r.contains("openingReliefDepth"))r["openingReliefDepth"]=5.;
        const auto direction=FixtureSupportDirection(r);r["supportDirection"]=Array{direction[0],direction[1],direction[2]};if(fresh){Array stations;for(const auto& value:r.at("stations").To<Array>()){auto v=value.To<Map>();v["id"]=NewFixtureID();stations.emplace_back(v);}r["stations"]=stations;}
        auto proposal=r;if(String(r,"layout")=="Automatic")proposal["stations"]=Array{};
        auto input=PlateInput(scene,id,proposal);
        if(String(r,"layout")=="Automatic"){
            const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();if(!service)throw std::runtime_error("CAM_PLATE_SERVICE_UNAVAILABLE");
            const auto proposed=service->SuggestStations(input,input.CountLayout?220.:FiniteNumber(r.at("spacing"),"PLATE_SPACING_INVALID"));Array stations;const auto previous=old.contains("stations")?old.at("stations").To<Array>():Array();
            for(size_t i=0;i<proposed.size();++i){const auto& p=proposed[i];Map station{{"id",i<previous.size()?previous[i].To<Map>().at("id"):Variant(NewFixtureID())},{"coordinate",p.Coordinate},{"axis",Text(p.AlongX?"X":"Y")},{"from",p.From},{"to",p.To}};if(input.Pattern=="Skeleton")station["connection"]=p.Connection;if(input.CountLayout){station["layoutIndex"]=static_cast<UInt>(p.LayoutIndex);station["nominalCoordinate"]=p.NominalCoordinate;}stations.emplace_back(station);}r["stations"]=stations;
            if(input.CountLayout){const auto info=FixtureCountLayoutInfo(proposed);r["supportCount"]=info.at("supportCount");r["connectionCount"]=info.at("connectionCount");r["layoutAxis"]=info.at("axis");r.erase("spacing");}
        }
        r["baseId"]=old.contains("baseId")?old.at("baseId"):Variant(NewFixtureID());r["id"]=old.contains("id")?old.at("id"):Variant(NewFixtureID());
        std::map<Text,Text> numbers;UInt next=0;
        if(old.contains("stations"))for(const auto& s:old.at("stations").To<Array>()){auto v=s.To<Map>();numbers[String(v,"id")]=String(v,"number");next=std::max(next,static_cast<UInt>(std::stoull(String(v,"number"))));}
        if(old.contains("nextNumber"))next=std::max(next,Integer(old,"nextNumber"));
        Array stations;for(const auto& value:r.at("stations").To<Array>()){auto s=value.To<Map>();auto key=String(s,"id");if(!numbers.contains(key)){if(scene.Database().GetEntity(Identifier(s,"id")))throw std::invalid_argument("CAM_FIXTURE_STATION_ID_DUPLICATE");numbers[key]=std::to_string(++next);}s["number"]=numbers[key];stations.emplace_back(s);}r["stations"]=stations;r["nextNumber"]=std::to_string(next);
        (void)PlateInput(scene,id,r);return r;
    }
}
Map SuggestFixtureLayout(Scene& scene,const Map& request){
    auto runtime=Runtime(scene);CheckExpected(*runtime,request);const auto id=Identifier(request,"setupId");RequireSetupWorkpieceEnabled(scene,id);
    auto r=request.at("recipe").To<Map>();r["stations"]=Array{};if(!r.contains("tenonRadius"))r["tenonRadius"]=1.;auto input=PlateInput(scene,id,r);
    if(!input.CountLayout)throw std::invalid_argument("PLATE_COUNT_INVALID");
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();if(!service)throw std::runtime_error("CAM_PLATE_SERVICE_UNAVAILABLE");
    return FixtureCountLayoutInfo(service->SuggestStations(input,220.));
}
Map SetFixtureRecipe(Scene& scene,const Map& request) {
    return FixtureDesignCommand(scene,request,"FixtureRecipe",[&](const ID& id,Map d){
        const bool fresh=request.contains("newUnit")&&request.at("newUnit").To<bool>();if(fresh)d.clear();
        // Editing a unit recipe starts a live setup design; it must not retain
        // the frozen unit's bypass for candidate/recipe identity checks.
        d.erase("unitSnapshot");
        auto r=NormalizeFixtureRecipe(scene,id,request.at("recipe").To<Map>(),d.contains("recipe")?d.at("recipe").To<Map>():Map(),fresh);d["recipe"]=r;SaveFixtureDesign(scene,id,d,"设置夹具参数",true);return r;
    });
}
Map FindFixtureSupportFaces(Scene& scene,const Map& request) {
    auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("FixtureSupportFaces")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto&[_,t]:runtime->Tasks)if(t.CommandID==command){if(t.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,t);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto id=Identifier(request,"setupId"),placementID=Identifier(request,"placementId");
    const auto source=FixtureDesignSource(scene,id);
    if(Identifier(source,"placementId")!=placementID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();if(!service)throw std::runtime_error("CAM_PLATE_SERVICE_UNAVAILABLE");
    iCAX::Geometry::SectionPlateInput input;input.Geometry=FixtureGeometry(scene,source.at("geometry").To<Map>());input.Transform=RigidMatrix(source.at("transform").To<Array>());
    input.SupportDirection=FixtureSupportDirection(request);
    ImportTask t;t.Kind="FixtureSupportFaces";t.CommandID=command;t.Payload=payload;t.SetupID=id;t.PlacementID=placementID;t.Sequence=runtime->Sequence;t.ClearanceInputKey=iCAX::Data::VariantSerializer::Serialize(source);
    const auto cancelled=t.Cancelled;
    t.SupportFaceWorker=iCAX::Tasks::Run([service,input,cancelled]{return std::make_shared<std::vector<std::uint64_t>>(service->FindLowerEnvelopeFaces(input,[cancelled]{return cancelled->load();}));});
    const auto taskID=t.TaskID;runtime->Tasks.emplace(taskID,std::move(t));return TaskState(scene,*runtime,runtime->Tasks.at(taskID));
}
Map GenerateFixture(Scene& scene,const Map& request) {
    auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("FixtureGenerate")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto&[_,t]:runtime->Tasks)if(t.CommandID==command){if(t.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,t);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    for(const auto&[_,t]:runtime->Tasks)if(t.Status=="Running"&&t.Kind!="RapidPreview")throw std::runtime_error("CAM_IMPORT_BUSY");
    const auto id=Identifier(request,"setupId");RequireSetupWorkpieceEnabled(scene,id);RequireSetupUnlocked(scene,id);
    auto d=Require<CFiveAxisSetupComponent>(scene.Database(),id)->GetFixtureDesign();
    const auto unitID=request.contains("unitId")?Identifier(request,"unitId"):ID();
    if(!unitID.is_nil()){const auto unit=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),unitID);if(unit->GetSetupID()!=id)throw std::invalid_argument("CAM_SETUP_MISMATCH");if(unit->GetLocked())throw std::invalid_argument("CAM_FIXTURE_LOCKED");d=unit->GetDesign();}
    const bool draft=request.contains("recipe"),fresh=request.contains("newUnit")&&request.at("newUnit").To<bool>();
    if(fresh)d.clear();
    if(!draft&&!d.contains("recipe"))throw std::invalid_argument("CAM_FIXTURE_RECIPE_REQUIRED");
    const auto r=draft?NormalizeFixtureRecipe(scene,id,request.at("recipe").To<Map>(),d.contains("recipe")?d.at("recipe").To<Map>():Map(),fresh):d.at("recipe").To<Map>();auto input=PlateInput(scene,id,r);
    ImportTask t;t.Kind="FixtureGenerate";t.CommandID=command;t.Payload=payload;t.SetupID=id;t.Sequence=runtime->Sequence;t.FixtureRecipe=r;t.ClearanceInputKey=FixtureDesignKey(scene,id,r);
    t.FixtureDraft=draft;t.FixtureNewUnit=fresh;t.FixtureUnitID=unitID;
    t.PlateWorker=StartFixturePlateWorker(scene,id,r,input,t.Cancelled);
    const auto taskID=t.TaskID;runtime->Tasks.emplace(taskID,std::move(t));return TaskState(scene,*runtime,runtime->Tasks.at(taskID));
}
Map ConfirmFixture(Scene& scene,const Map& request) {
    return FixtureDesignCommand(scene,request,"FixtureConfirm",[&](const ID& id,Map d){
        RequireSetupWorkpieceEnabled(scene,id);RequireSetupUnlocked(scene,id);
        ImportTask* draft=nullptr;
        if(request.contains("taskId")){
            const auto runtime=Runtime(scene);const auto it=runtime->Tasks.find(String(request,"taskId"));
            if(it==runtime->Tasks.end()||!it->second.FixtureDraft||it->second.SetupID!=id||it->second.Status!="Completed"||!it->second.ConsumedByCommandID.empty()||!it->second.FixtureModel)throw std::invalid_argument("CAM_FIXTURE_DESIGN_STALE");
            draft=&it->second;
            if(draft->Sequence!=runtime->Sequence)throw std::invalid_argument("CAM_FIXTURE_DESIGN_STALE");
            if(draft->FixtureNewUnit)d.clear();
            else if(!draft->FixtureUnitID.is_nil())d=Require<CFiveAxisFixtureUnitComponent>(scene.Database(),draft->FixtureUnitID)->GetDesign();
            d.erase("unitSnapshot");d["recipe"]=draft->FixtureRecipe;d["candidate"]=draft->FixtureModel->Assembly;
        }
        if(!d.contains("candidate"))throw std::invalid_argument("CAM_FIXTURE_CANDIDATE_REQUIRED");auto a=d.at("candidate").To<Map>();
        if(String(request,"candidateId")!=String(a,"id")||!CurrentFixtureAssembly(scene,id,d,a,false))throw std::invalid_argument("CAM_FIXTURE_DESIGN_STALE");
        if(String(a.at("supportCheck").To<Map>(),"status")!="Supported")throw std::invalid_argument("CAM_FIXTURE_SUPPORT_INSUFFICIENT");
        if(!request.at("supportReviewed").To<bool>())throw std::invalid_argument("CAM_FIXTURE_REVIEW_REQUIRED");
        const auto requireUnlocked=[&](const Array& parts){
            for(const auto& part:parts){
                const auto f=Component<CFiveAxisFixtureComponent>(scene.Database().GetEntity(Identifier(part.To<Map>(),"partId")));
                if(f&&f->GetLocked())throw std::invalid_argument("CAM_FIXTURE_LOCKED: 请先解锁将被替换的工装。");
            }
        };
        requireUnlocked(a.at("parts").To<Array>());
        if(d.contains("assembly"))requireUnlocked(d.at("assembly").To<Map>().at("parts").To<Array>());
        const auto unitID=d.contains("unitId")?Identifier(d,"unitId"):iCAX::Data::GenerateNewUUID();
        const auto previousParts=d.contains("assembly")?d.at("assembly").To<Map>().at("parts").To<Array>():Array();
        d["unitId"]=iCAX::Data::to_string(unitID);a["unitId"]=d.at("unitId");a["status"]=Text("SupportReviewed");d["assembly"]=a;d.erase("breakdown");d.erase("delivery");
        const auto persistence=[&](iCAX::Resource::EResourcePersistenceMode mode){if(draft)for(const auto& [ref,persist]:draft->FixtureDraftResources){if(!persist)continue;auto info=*scene.Resources().GetInfo(ref.URL,ref.nVersion);info.Persistence=mode;scene.Resources().UpdateInfo(ref.URL,info);}};
        persistence(iCAX::Resource::EResourcePersistenceMode::Embedded);
        try{
        Transaction(scene.Database(),"采用夹具装配",[&](auto& tx){
            if(draft){
                const auto& display=draft->FixtureModel->SourcePreview;const auto workpieceID=Identifier(display,"workpieceId");
                const auto preview=*iCAX::ProjectFile::TryGetResourceReferenceValue(display.at("previewRef").To<Map>());
                tx.ModifyComponent(workpieceID,CFiveAxisWorkpieceComponent::S_ClassName,{{"PreviewRef",display.at("previewRef")},{"ImportMetadata",display.at("metadata")}});
                for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetWorkpieceID()==workpieceID)
                    tx.ModifyComponent(e->GetID(),iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
            }
            auto snapshot=d;snapshot["unitSnapshot"]=true;snapshot.erase("candidate");
            if(scene.Database().GetEntity(unitID))tx.ModifyComponent(unitID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"Design",snapshot},{"Transform",Identity()}});
            else{tx.CreateEntity(unitID);tx.AttachComponent(unitID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"SetupID",id},{"Name",Text("生成夹具单元")},{"Kind",Text("Generated")},{"Transform",Identity()},{"Design",snapshot}});}
            std::set<ID> alive;for(const auto& p:a.at("parts").To<Array>())alive.insert(Identifier(p.To<Map>(),"partId"));
            for(const auto& p:previousParts){
                auto pid=Identifier(p.To<Map>(),"partId");if(!alive.contains(pid)&&Component<CFiveAxisFixtureComponent>(scene.Database().GetEntity(pid)))tx.DisposeEntity(pid);
            }
            for(const auto& value:a.at("parts").To<Array>()){auto p=value.To<Map>();const auto pid=Identifier(p,"partId");const bool exists=bool(scene.Database().GetEntity(pid));
                iCAX::Data::PropertySet properties{{"SetupID",id},{"UnitID",unitID},{"Name",String(p,"number")+(String(p,"role")=="Base"?" 底板":String(p,"role")=="Connection"?" 连接板":" 承托板")},
                    {"GeometryRef",p.at("geometryRef")},{"PreviewRef",p.at("previewRef")},{"Transform",Identity()},
                    {"ImportMetadata",Map{{"generated",true},{"assemblyId",a.at("id")},{"partNumber",p.at("number")}}},{"CollisionEnabled",true}};
                if(exists)tx.ModifyComponent(pid,CFiveAxisFixtureComponent::S_ClassName,properties);
                else {tx.CreateEntity(pid);tx.AttachComponent(pid,CFiveAxisFixtureComponent::S_ClassName,properties);tx.AttachComponent(pid,iCAX::Transform::CTransformComponent::S_ClassName);}
                const auto preview=*iCAX::ProjectFile::TryGetResourceReferenceValue(p.at("previewRef").To<Map>());
                if(exists)tx.ModifyComponent(pid,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
                else tx.AttachComponent(pid,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
            }
            tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"FixtureDesign",d}});Invalidate(tx,scene.Database(),id);
        });}catch(...){persistence(iCAX::Resource::EResourcePersistenceMode::RuntimeOnly);throw;}
        if(draft){draft->ConsumedByCommandID=String(request,"commandId");draft->FixtureModel.reset();}return Map{{"unitId",d.at("unitId")},{"assemblyId",a.at("id")},{"partCount",static_cast<UInt>(a.at("parts").To<Array>().size())}};
    });
}
Map BreakdownFixture(Scene& scene,const Map& request) {
    return FixtureDesignCommand(scene,request,"FixtureBreakdown",[&](const ID& id,Map d){
        if(!d.contains("assembly"))throw std::invalid_argument("CAM_FIXTURE_ASSEMBLY_REQUIRED");const auto a=d.at("assembly").To<Map>();
        if(!CurrentFixtureAssembly(scene,id,d,a,true))throw std::invalid_argument("CAM_FIXTURE_DESIGN_STALE");
        // Blanks are construction provenance: each generated solid is an exact
        // constant-thickness prism of this loop. Arbitrary imported solids are
        // deliberately excluded from this operation.
        Array enabled;for(const auto& value:a.at("parts").To<Array>()){const auto p=value.To<Map>();const auto fixture=Require<CFiveAxisFixtureComponent>(scene.Database(),Identifier(p,"partId"));if((!p.contains("enabled")||p.at("enabled").To<bool>())&&fixture->GetEnabled())enabled.push_back(value);}if(enabled.empty())throw std::invalid_argument("CAM_FIXTURE_NO_ENABLED_PARTS");
        Map list{{"id",NewFixtureID()},{"assemblyId",a.at("id")},{"parts",enabled},{"unit",Text("mm")},{"quantityPerPart",1ull}};
        d["breakdown"]=list;d.erase("delivery");SaveFixtureDesign(scene,id,d,"拆分夹具板件",false,request.contains("unitId")?Identifier(request,"unitId"):ID());return Map{{"breakdownId",list.at("id")},{"assemblyId",a.at("id")},{"partCount",static_cast<UInt>(enabled.size())}};
    });
}
namespace {
    Text FixtureSHA(const Text& bytes){
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD size=0,used=0;std::vector<unsigned char> object;unsigned char digest[32]{};
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("CAM_HASH_FAILED");
        auto close=[&]{if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);};
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&used,0)<0){close();throw std::runtime_error("CAM_HASH_FAILED");}
        object.resize(size);
        if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0||BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)<0||BCryptFinishHash(hash,digest,32,0)<0){close();throw std::runtime_error("CAM_HASH_FAILED");}close();
        std::ostringstream out;for(auto b:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(b);return out.str();
    }
    Text PlateDXF(const Map& p,const Array& parts,const Text& baseShape) {
        std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
        out<<"0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n4\n";
        for(const auto* layer:{"CUT_OUTER","CUT_INNER","MARK_ID","REFERENCE"})out<<"0\nLAYER\n2\n"<<layer<<"\n70\n0\n62\n7\n6\nCONTINUOUS\n";
        out<<"0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
        const auto loop=p.at("outline").To<Array>();
        const auto polyline=[&](const Array& points,const char* layer){out<<"0\nLWPOLYLINE\n100\nAcDbEntity\n8\n"<<layer<<"\n100\nAcDbPolyline\n90\n"<<points.size()<<"\n70\n1\n";for(const auto& v:points){auto q=v.To<Array>();out<<"10\n"<<q[0].To<double>()<<"\n20\n"<<q[1].To<double>()<<"\n";if(q.size()>2&&q[2].To<double>()!=0)out<<"42\n"<<q[2].To<double>()<<"\n";}};
        const auto boundary=[&](const FixtureDXFBoundary& value,const char* layer){if(value.Circle){const auto c=*value.Circle;out<<"0\nCIRCLE\n100\nAcDbEntity\n8\n"<<layer<<"\n100\nAcDbCircle\n10\n"<<c[0]<<"\n20\n"<<c[1]<<"\n30\n0\n40\n"<<c[2]<<"\n";}else polyline(value.Vertices,layer);};
        boundary(FixtureDXFOuter(p),"CUT_OUTER");for(const auto& hole:FixtureDXFHoles(p))boundary(hole,"CUT_INNER");
        double minx=1e100,miny=1e100,maxx=-1e100;for(const auto& v:loop){auto point=v.To<Array>();double x=point[0].To<double>(),y=point[1].To<double>();minx=std::min(x,minx);maxx=std::max(x,maxx);miny=std::min(y,miny);}
        const auto text=[&](const Text& number,double x,double y,double height=2.){out<<"0\nTEXT\n100\nAcDbEntity\n8\nMARK_ID\n100\nAcDbText\n10\n"<<x<<"\n20\n"<<y<<"\n30\n0\n40\n"<<height<<"\n1\n"<<number<<"\n";};
        // Labels are in the reserved margin/foot, outside contact edges.
        const auto number=String(p,"number");const double markHeight=std::min(2.,(maxx-minx-1)/(number.size()*.6));
        text(number,String(p,"role")=="Base"&&baseShape=="Circle"?(minx+maxx)/2-number.size()*.6:minx+.5,String(p,"role")!="Base"?1:miny+1,markHeight);
        if(String(p,"role")=="Base")for(const auto& v:parts){auto support=v.To<Map>();if(String(support,"role")=="Base")continue;auto f=support.at("frame").To<Array>(),span=support.at("span").To<Array>();const double t=support.at("thickness").To<double>();
            out<<"0\nLWPOLYLINE\n100\nAcDbEntity\n8\nREFERENCE\n100\nAcDbPolyline\n90\n4\n70\n1\n";
            for(auto local:std::array<std::array<double,2>,4>{{{span[0].To<double>(),0},{span[1].To<double>(),0},{span[1].To<double>(),t},{span[0].To<double>(),t}}}){
                out<<"10\n"<<f[0].To<double>()*local[0]+f[2].To<double>()*local[1]+f[3].To<double>()<<"\n20\n"<<f[4].To<double>()*local[0]+f[6].To<double>()*local[1]+f[7].To<double>()<<"\n";
            }
            const auto number=String(support,"number");const double mid=(span[0].To<double>()+span[1].To<double>())/2;
            const double cx=f[0].To<double>()*mid+f[2].To<double>()*t/2+f[3].To<double>(),cy=f[4].To<double>()*mid+f[6].To<double>()*t/2+f[7].To<double>();
            text(number,cx+(f[2].To<double>()>.5?t/2+1:-.6*number.size()),cy+(f[2].To<double>()>.5?0:t/2+1));
        }
        out<<"0\nENDSEC\n0\nEOF\n";return out.str();
    }
}
Map ExportFixture(Scene& scene,const Map& request) {
    return FixtureDesignCommand(scene,request,"FixtureExport",[&](const ID& id,Map d){
        if(!d.contains("assembly")||!d.contains("breakdown"))throw std::invalid_argument("CAM_FIXTURE_BREAKDOWN_REQUIRED");
        const auto assembly=d.at("assembly").To<Map>(),list=d.at("breakdown").To<Map>();
        if(!CurrentFixtureAssembly(scene,id,d,assembly,true)||list.at("assemblyId")!=assembly.at("id"))throw std::invalid_argument("CAM_FIXTURE_DESIGN_STALE");
        const auto destination=String(request,"destination");const auto root=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(destination.data()),destination.size()));
        if(!root.is_absolute()||!std::filesystem::is_directory(root))throw std::invalid_argument("CAM_FIXTURE_DIRECTORY_REQUIRED");
        const auto suffix=NewFixtureID();const auto folder=root/("fixture-"+suffix),temporary=root/(".fixture-"+suffix+".tmp");
        if(!std::filesystem::create_directory(temporary))throw std::runtime_error("CAM_FIXTURE_EXPORT_CREATE_FAILED");
        Array files;const auto write=[&](const Text& name,const Text& bytes){auto path=temporary/name;{std::ofstream file(path,std::ios::binary);file.write(bytes.data(),bytes.size());file.flush();if(!file)throw std::runtime_error("CAM_FIXTURE_EXPORT_WRITE_FAILED");}std::ifstream read(path,std::ios::binary);Text actual((std::istreambuf_iterator<char>(read)),{});if(actual!=bytes)throw std::runtime_error("CAM_FIXTURE_EXPORT_READBACK_FAILED");files.emplace_back(Map{{"name",name},{"sha256",FixtureSHA(actual)},{"bytes",static_cast<UInt>(actual.size())}});};
        // On interruption a .tmp directory is intentionally not a published pack.
        const auto parts=list.at("parts").To<Array>();std::ostringstream csv;csv.imbue(std::locale::classic());csv<<"number,role,material,thickness_mm,quantity,part_id,nominal_mesh_gap_mm,contact_length_mm\n";
        const bool interlocked=String(assembly,"joint")=="TabSlot";
        std::ostringstream svg;svg.imbue(std::locale::classic());svg<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1000\" height=\""<<120+parts.size()*240<<"\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/><g font-family=\"sans-serif\" fill=\"#203040\"><text x=\"30\" y=\"35\" font-size=\"22\">夹具板件与拼装示意 · 待装夹及焊接工艺确认</text><text x=\"30\" y=\"65\" font-size=\"14\">"<<(interlocked?(assembly.at("joinery").To<Map>().at("crossLaps").To<UInt>()>0?"B01 为底座。先装上开槽 Y 板，再向下插入 X 板；底榫齐平。CUT_INNER 为贯穿内孔。":"B01 为底座。支撑板向下插入对应榫孔；底榫齐平。CUT_INNER 为贯穿内孔。"):"B01 为底板；支撑板按底板 REFERENCE 轮廓定位，沿 +Z 放置。")<<"</text><text x=\"30\" y=\"85\" font-size=\"14\">尺寸为成品尺寸；割缝补偿在切割工艺中设置。</text>";
        size_t row=0;
        for(const auto& v:parts){auto p=v.To<Map>();write(String(p,"number")+".dxf",PlateDXF(p,parts,String(d.at("recipe").To<Map>(),"baseShape")));csv<<String(p,"number")<<','<<String(p,"role")<<','<<String(p,"material")<<','<<p.at("thickness").To<double>()<<",1,"<<String(p,"partId")<<','<<p.at("minimumDistance").To<double>()<<','<<p.at("contactLengthMm").To<double>()<<'\n';
            double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;for(auto point:p.at("outline").To<Array>()){auto q=point.To<Array>();x0=std::min(x0,q[0].To<double>());x1=std::max(x1,q[0].To<double>());y0=std::min(y0,q[1].To<double>());y1=std::max(y1,q[1].To<double>());}double scale=std::min(800/(x1-x0),150/(y1-y0)),y=110+static_cast<double>(row++)*240;
            svg<<"<text x=\"30\" y=\""<<y<<"\">"<<String(p,"number")<<" · 厚度 "<<p.at("thickness").To<double>()<<" mm · 数量 1 · 非比例示意</text><path fill-rule=\"evenodd\" fill=\"#dbe8ee\" stroke=\"#284860\" d=\"";
            const auto contour=[&](const Array& points){bool first=true;for(auto point:points){auto q=point.To<Array>();svg<<(first?'M':'L')<<30+(q[0].To<double>()-x0)*scale<<','<<y+180-(q[1].To<double>()-y0)*scale;first=false;}svg<<'Z';};contour(p.at("outline").To<Array>());for(auto h:p.at("holes").To<Array>())contour(h.To<Array>());svg<<"\"/>";
        }
        svg<<"</g></svg>";write("parts.csv",csv.str());write("assembly.svg",svg.str());write("assembly.pdf",FixtureAssemblyPDF(parts,interlocked));
        const auto dxfReader=scene.Services().Resolve<iCAX::CAD::ICadDocumentImportService>();if(!dxfReader)throw std::runtime_error("CAM_FIXTURE_DXF_READER_UNAVAILABLE");
        for(const auto& v:parts){auto p=v.To<Map>();VerifyFixtureDXF(*dxfReader,temporary/(String(p,"number")+".dxf"),p);}
        Map manifest{{"schema",Text("icax.fixture.delivery.1")},{"assemblyId",assembly.at("id")},{"breakdownId",list.at("id")},{"unit",Text("mm")},{"scale",1.},{"joint",assembly.at("joint")},{"joinery",assembly.at("joinery")},{"recipe",d.at("recipe")},{"parts",parts},{"files",files},{"coverage",assembly.at("coverage")}};
        if(assembly.contains("cutClearance"))manifest["cutClearance"]=assembly.at("cutClearance");
        if(assembly.contains("openingPreclearance"))manifest["openingPreclearance"]=ProjectFixtureAssembly(scene,id,d,assembly,true).at("openingPreclearance");
        write("manifest.json",iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(manifest));
        std::filesystem::rename(temporary,folder);const auto utf8=folder.u8string();Map result{{"directory",Text(reinterpret_cast<const char*>(utf8.data()),utf8.size())},{"assemblyId",assembly.at("id")},{"files",files}};
        d["delivery"]=result;SaveFixtureDesign(scene,id,d,"导出夹具下料包",false,request.contains("unitId")?Identifier(request,"unitId"):ID());return result;
    });
}
