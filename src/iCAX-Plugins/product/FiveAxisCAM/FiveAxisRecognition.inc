// Included in the product namespace. Workers receive immutable values; only
// this Scene-thread adapter resolves resources, manages tickets and publishes.
namespace
{
    Ref ReadWireRef(const Map& value)
    {
        Ref ref{String(value,"url"),Integer(value,"version")};
        if(!ref.IsValid())throw std::invalid_argument("CAM_FIXED_REFERENCE_REQUIRED");return ref;
    }
    void CutFields(const Map& value,std::initializer_list<const char*> allowed)
    {
        for(const auto& [key,_]:value)if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())throw std::invalid_argument("CAM_CUT_UNKNOWN_FIELD: "+key);
    }
    Array CutIDs(const std::vector<std::uint64_t>& ids){Array out;for(auto id:ids)out.emplace_back(std::to_string(id));return out;}
    Array CutStrings(const std::vector<Text>& values){Array out;for(const auto& v:values)out.emplace_back(v);return out;}
    Array CutPoint(const std::array<double,3>& p){return {p[0],p[1],p[2]};}
    Ref PublishCutJSON(Scene& scene,const Map& value,const Text& name,const std::vector<Ref>& dependencies,bool runtimeOnly)
    {
        auto body=std::make_shared<iCAX::Resource::CBinaryResource>();
        const auto text=iCAX::Data::VariantSerializer::Serialize(value);body->Content.assign(text.begin(),text.end());
        body->DisplayName=name;body->FileExtension=".json";body->Metadata={{"schema",String(value,"schema")}};
        if(!runtimeOnly)throw std::logic_error("CAM_BUSINESS_DATA_REQUIRES_DATABASE_STORAGE");
        iCAX::Resource::CResourceInfo info;info.Name=name;info.ResourceTypeID="resource.binary";info.nVersion=1;
        info.nSchemaVersion=1;info.nMinimumReaderVersion=1;info.Dependencies=dependencies;
        info.Persistence=iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        Ref ref{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(ref.URL,body,info);return ref;
    }
    Map DefaultPlanningProfile(Scene& scene)
    {
        if(iCAX::CAM::Recognition::GetRecognitionContractVersion()!=1)throw std::runtime_error("CAM_RECOGNITION_CONTRACT_MISMATCH");
        // Independent IGES trims can differ by a few millionths of a millimetre
        // at shared endpoints. Keep join tolerance explicit and far below the
        // separate 0.01 mm feature-classification tolerance.
        return EngineeringData({{"schema",Text("cam.planning-profile")},{"schemaVersion",Text("1")},
            {"recognition",Map{{"algorithm",Text("boundary-termination-v2")},{"joinToleranceMm",1e-5},{"angleToleranceRad",1e-7},
            {"shapeToleranceMm",.01},{"radiusRangeMm",Array{.001,100000.}},{"widthRangeMm",Array{.001,100000.}},
            {"lengthRangeMm",Array{.001,100000.}},{"maximumEdgeUses",Text("200000")},{"maximumFaces",Text("50000")},{"maximumCandidates",Text("4096")}}}});
    }
    void LoadRecognitionProfile(Scene& scene,RecognitionInput& input)
    {
        const auto value=EngineeringBody(input.PlanningData,"cam.planning-profile");
        if(String(value,"schema")!="cam.planning-profile"||String(value,"schemaVersion")!="1")throw std::invalid_argument("CAM_PLANNING_PROFILE_SCHEMA_UNSUPPORTED");
        const auto r=value.at("recognition").To<Map>();
        CutFields(r,{"algorithm","joinToleranceMm","angleToleranceRad","shapeToleranceMm","radiusRangeMm","widthRangeMm","lengthRangeMm","maximumEdgeUses","maximumFaces","maximumCandidates"});
        if(String(r,"algorithm")!="boundary-termination-v2")throw std::invalid_argument("CAM_RECOGNITION_ALGORITHM_UNSUPPORTED");
        auto& p=input.Profile;p.Geometry.DistanceToleranceMm=RegistrationNumber(r.at("joinToleranceMm"));
        p.Geometry.AngleToleranceRad=RegistrationNumber(r.at("angleToleranceRad"));p.ShapeToleranceMm=RegistrationNumber(r.at("shapeToleranceMm"));
        const auto range=[&](const char* key){const auto a=r.at(key).To<Array>();if(a.size()!=2)throw std::invalid_argument("CAM_RECOGNITION_RANGE_INVALID");
            return iCAX::CAM::Recognition::SSizeRange{RegistrationNumber(a[0]),RegistrationNumber(a[1])};};
        p.Radius=range("radiusRangeMm");p.Width=range("widthRangeMm");p.Length=range("lengthRangeMm");
        p.Geometry.MaximumEdgeUses=Integer(r,"maximumEdgeUses");p.MaximumFaces=Integer(r,"maximumFaces");p.MaximumCandidates=Integer(r,"maximumCandidates");
    }
    RecognitionInput CutSource(Scene& scene,const Map& request)
    {
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
        if(placement->GetSetupID()!=Identifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
        RequireMachiningSetup(scene,placement->GetSetupID());
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),placement->GetSetupID());
        const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
        RecognitionInput input;input.GeometryRef=ReadWireRef(WireRef(workpiece->GetGeometryRef()));input.TopologyMapRef=input.GeometryRef;
        input.PlanningData=setup->GetPlanningProfileData();input.PlanningProfileRevision=iCAX::Data::VariantSerializer::Serialize(EngineeringIdentity(input.PlanningData));
        input.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(input.GeometryRef.URL,input.GeometryRef.nVersion);
        if(!input.Geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
        return input;
    }
    Map CutFaceUse(const iCAX::CAM::Recognition::SFaceUse& use,const Ref& ref)
    {
        return {{"kind",Text("Face")},{"localId",std::to_string(use.FaceID)},{"usePath",CutIDs(use.UsePath)},
            {"geometryRef",WireRef(StoredRef(ref))},{"occurrencePath",Array{}}};
    }
    iCAX::Geometry::SBoundaryCurveSpan CutEdgeSpan(const iCAX::GeometryData::BRepModel& model,UInt faceID,UInt edgeID)
    {
        const auto face=std::find_if(model.Faces.begin(),model.Faces.end(),[&](const auto& f){return f.Id==faceID;});
        const auto edge=std::find_if(model.Edges.begin(),model.Edges.end(),[&](const auto& e){return e.Id==edgeID;});
        if(face==model.Faces.end()||edge==model.Edges.end()||edge->Degenerated)throw std::invalid_argument("CAM_EDGE_USE_INVALID");
        std::optional<iCAX::Geometry::SBoundaryCurveSpan> result;
        for(const auto wireID:face->WireIds)for(const auto& wire:model.Wires)if(wire.Id==wireID)
            for(size_t i=0;i<wire.Coedges.size();++i)if(wire.Coedges[i].EdgeId==edgeID){
                if(result)throw std::invalid_argument("CAM_EDGE_USE_AMBIGUOUS");
                result=iCAX::Geometry::SBoundaryCurveSpan{edgeID,edge->Curve3Id,edge->Range.First,edge->Range.Last,false,faceID,wireID,i};
            }
        if(!result)throw std::invalid_argument("CAM_EDGE_USE_INVALID");return *result;
    }
    Map CutBoundary(const iCAX::Geometry::SAnalyticBoundary& b)
    {
        Array preview;for(const auto& p:b.PreviewPoints)preview.emplace_back(CutPoint(p));
        Array spans;for(const auto& s:b.Spans)spans.emplace_back(Map{{"edgeId",std::to_string(s.EdgeID)},{"curveId",std::to_string(s.CurveID)},
            {"faceId",std::to_string(s.FaceID)},{"wireId",std::to_string(s.WireID)},{"coedgeIndex",std::to_string(s.CoedgeIndex)},
            {"first",s.First},{"last",s.Last},{"reversed",s.Reversed}});
        Map parameters;
        if(b.GeometryKind!="GeneralContour")parameters={{"center",CutPoint(b.Center)},{"normal",CutPoint(b.Normal)},{"xDirection",CutPoint(b.XDirection)}};
        if(b.GeometryKind=="Circle")parameters["radiusMm"]=b.RadiusMm;
        if(b.GeometryKind=="Rectangle"||b.GeometryKind=="Slot"||b.GeometryKind=="Ellipse"){parameters["widthMm"]=b.WidthMm;parameters["lengthMm"]=b.LengthMm;}
        return {{"previewPoints",preview},{"topologyClosed",b.TopologyClosed},{"sourceSpans",spans},{"closed",b.Closed},{"valid",b.Valid},{"geometryKind",b.GeometryKind},{"regionRelation",b.RegionRelation},
            {"parameters",parameters},{"perimeterMm",b.PerimeterMm},{"standardBoundProven",b.StandardBoundProven},{"standardDeviationUpperMm",b.StandardDeviationUpperMm}};
    }
    Map CutCandidate(const iCAX::CAM::Recognition::SCutCandidate& c)
    {
        Array predicates;for(const auto& p:c.Predicates)predicates.emplace_back(Map{{"id",p.ID},{"state",p.State},{"method",p.Method},{"faceIds",CutIDs(p.Faces)}});
        const auto& s=c.Sweep;
        return {{"candidateId",c.LocalID},{"sourceKey",c.SourceKey},{"boundaryRole",c.BoundaryRole},{"geometryKind",c.GeometryKind},
            {"machiningKind",c.MachiningKind},
            {"termination",Map{{"entranceRelation",c.Termination.EntranceRelation},{"oppositeRelation",c.Termination.OppositeRelation},{"singleFaceCap",c.Termination.SingleFaceCap},
                {"entranceEdges",CutIDs(c.Termination.EntranceEdges)},{"sideFaces",CutIDs(c.Termination.SideFaces)},
                {"oppositeEdges",CutIDs(c.Termination.OppositeEdges)},{"terminalFaces",CutIDs(c.Termination.TerminalFaces)}}},
            {"classificationStatus",c.ClassificationStatus},{"confirmationEligibility",c.ConfirmationEligibility},{"processEligibility",c.ProcessEligibility},
            {"boundary",CutBoundary(c.Boundary)},{"sourceErrorCoverage",c.SourceErrorCoverage},{"predicates",predicates},{"issues",CutStrings(c.Issues)},
            {"passage",Map{{"kind",c.PassageKind},{"proof",c.Proof},{"sideFaces",CutIDs(s.SideFaces)},{"terminalFaces",CutIDs(s.TerminalFaces)},
                {"terminalRelation",s.TerminalRelation},{"terminal",CutBoundary(s.Terminal)},{"wallGeometry",s.WallGeometry},
                {"direction",CutPoint(s.Direction)},{"lengthMm",s.LengthMm},{"coneHalfAngleRad",s.ConeHalfAngleRad},
                {"volumeTestComplete",s.VolumeTestComplete},{"sweptInteriorEmpty",s.SweptInteriorEmpty}}}};
    }
    bool CurrentExtraction(const SceneRuntime& runtime,const ImportTask& task)
    {
        const auto latest=runtime.LatestExtraction.find(task.PlacementID);
        return task.Sequence==runtime.Sequence&&latest!=runtime.LatestExtraction.end()&&latest->second==task.TaskID;
    }
    void FinishExtraction(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try
        {
            auto result=task.RecognitionWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(!CurrentExtraction(runtime,task))task.Status="Superseded";
            else
            {
                if(!result||!result->Complete)throw std::runtime_error("CAM_RECOGNITION_COVERAGE_INCOMPLETE");
                Array candidates;for(auto& c:result->Candidates){c.LocalID=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());candidates.emplace_back(CutCandidate(c));}
                const auto& input=*task.RecognitionInput;
                const Map manifest{{"schema",Text("cam.cut-candidate-set")},{"schemaVersion",Text("1")},{"algorithmIdentity",result->AlgorithmIdentity},
                    {"inputDigest",result->InputDigest},{"draftId",task.DraftID},{"draftRevision",std::to_string(task.DraftRevision)},{"draftDigest",task.DraftDigest},
                    {"sourceGeometry",WireRef(StoredRef(input.GeometryRef))},{"planningProfileData",EngineeringIdentity(input.PlanningData)},
                    {"coverage",Map{{"complete",true},{"requestedFaces",CutIDs(result->RequestedFaces)},{"checkedFaces",CutIDs(result->CheckedFaces)}}},
                    {"issues",CutStrings(result->Issues)},{"candidates",candidates}};
                task.CandidateSet=PublishCutJSON(scene,manifest,"切割候选",{input.GeometryRef},true);
                task.RecognitionResult=std::move(result);
                task.Result={{"candidateSet",WireRef(StoredRef(task.CandidateSet))},{"candidateCount",std::to_string(candidates.size())},
                    {"draftId",task.DraftID},{"draftRevision",std::to_string(task.DraftRevision)},{"draftDigest",task.DraftDigest}};
                task.Status="Completed";
            }
        }
        catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
        catch(const std::exception& error)
        {
            task.Status="Failed";task.Error=error.what();
            task.TerminationReason=task.Error.find("BUDGET_EXHAUSTED")!=Text::npos?"BudgetExhausted":"ComputationFailed";
        }
        task.RecognitionWorker={};
    }
    void FinishCutRetainedFaces(Scene&,SceneRuntime& runtime,ImportTask& task)
    {
        try {
            const auto faces=task.RetainedFaceWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence)task.Status="Superseded";
            else {task.RetainedFaceSelection={{"faceIds",CutIDs(*faces)}};
                task.Result={{"selectionId",task.TaskID},{"selectedFaceCount",static_cast<UInt>(faces->size())}};task.Status="Completed";}
        }catch(const std::exception& e){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}
        task.RetainedFaceWorker={};
    }
}

Map FindCutRetainedFaces(Scene& scene,const Map& request)
{
    auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("CutRetainedFaces")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto source=CutSource(scene,request);
    const auto raw=request.at("observationDirection").To<Array>();if(raw.size()!=3)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_INVALID");
    iCAX::CAM::Recognition::SRetainedFaceSelectionInput input;input.Geometry=source.Geometry;
    for(size_t i=0;i<3;++i)input.ObservationDirection[i]=RegistrationNumber(raw[i]);
    const auto& d=input.ObservationDirection;const auto length=std::hypot(d[0],d[1],d[2]);
    if(!std::isfinite(length)||std::abs(length-1.)>1e-10)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_INVALID");
    ImportTask task;task.Kind="CutRetainedFaces";task.CommandID=command;task.Payload=payload;task.SetupID=Identifier(request,"setupId");task.PlacementID=Identifier(request,"placementId");task.Sequence=runtime->Sequence;
    input.Options.IsCancelled=[cancelled=task.Cancelled]{return cancelled->load();};
    const auto service=scene.Services().Resolve<iCAX::CAM::Recognition::ICamCutRecognitionService>();
    if(!service)throw std::runtime_error("CAM_RECOGNITION_SERVICE_MISSING");
    task.RetainedFaceWorker=iCAX::Tasks::Run([service,input]{return std::make_shared<std::vector<std::uint64_t>>(service->SelectRetainedFaces(input));});
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
Map ReadCutRetainedSelection(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto found=runtime->Tasks.find(String(request,"taskId"));
    if(found==runtime->Tasks.end()||found->second.Kind!="CutRetainedFaces"||found->second.Status!="Completed")throw std::invalid_argument("CAM_RETAINED_SELECTION_UNAVAILABLE");
    if(found->second.Sequence!=runtime->Sequence)throw std::invalid_argument("CAM_RETAINED_SELECTION_SOURCE_CHANGED");
    return found->second.RetainedFaceSelection;
}
Map QueryCutSources(Scene& scene,const Map& request)
{
    CheckExpected(*Runtime(scene),request);const auto input=CutSource(scene,request);
    Array faces;for(const auto& use:iCAX::CAM::Recognition::EnumerateFaceUses(*input.Geometry))faces.emplace_back(CutFaceUse(use,input.GeometryRef));
    return {{"sourceGeometry",WireRef(StoredRef(input.GeometryRef))},{"topologyMapRef",WireRef(StoredRef(input.TopologyMapRef))},
        {"extractionRuleRef",EngineeringIdentity(input.PlanningData)},{"faceUses",faces},{"modelFrame",Text("Workpiece")},{"unit",Text("mm")}};
}
Map ResolveCutFace(Scene& scene,const Map& request)
{
    CheckExpected(*Runtime(scene),request);const auto input=CutSource(scene,request);
    if(ReadWireRef(request.at("sourceGeometry").To<Map>())!=input.GeometryRef)throw std::invalid_argument("CAM_GEOMETRY_VERSION_CONFLICT");
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    const auto query=DisplayReferenceQuery(*workpiece,*input.Geometry,request.at("query").To<Map>());if(String(query,"mode")!="point")throw std::invalid_argument("CAM_FACE_PICK_REQUIRES_POINT");
    const auto result=ResolveReference(scene,*input.Geometry,query);const auto id=Integer(result,"faceId");
    const auto uses=iCAX::CAM::Recognition::EnumerateFaceUses(*input.Geometry);
    if(std::count_if(uses.begin(),uses.end(),[&](const auto& u){return u.FaceID==id;})!=1)throw std::invalid_argument("CAM_FACE_USE_AMBIGUOUS");
    const auto use=std::find_if(uses.begin(),uses.end(),[&](const auto& u){return u.FaceID==id;});
    Array triangleRanges;
    for(const auto& value:DisplayFaceRanges(*workpiece,*input.Geometry)){const auto face=value.To<Map>();if(String(face,"faceId")==std::to_string(id))
        triangleRanges.emplace_back(Map{{"first",face.at("first")},{"count",face.at("count")}});}
    return {{"faceUse",CutFaceUse(*use,input.GeometryRef)},{"point",result.at("point")},{"triangleRanges",triangleRanges}};
}
Map ResolveCutEdge(Scene& scene,const Map& request)
{
    const auto face=ResolveCutFace(scene,request);const auto input=CutSource(scene,request);
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    auto query=DisplayReferenceQuery(*workpiece,*input.Geometry,request.at("query").To<Map>());query["mode"]=Text("midpoint");
    const auto resolved=ResolveReference(scene,*input.Geometry,query);
    const auto span=CutEdgeSpan(*input.Geometry,Integer(resolved,"faceId"),Integer(resolved,"edgeId"));
    const auto curve=std::find_if(input.Geometry->Curves3.begin(),input.Geometry->Curves3.end(),[&](const auto& c){return c.Id==span.CurveID;});
    if(curve==input.Geometry->Curves3.end())throw std::invalid_argument("CAM_EDGE_CURVE_MISSING");
    iCAX::GeometryData::CompositeCurve3 composite;composite.Segments.push_back({curve->Geometry,{span.First,span.Last,false}});
    const auto service=scene.Services().Resolve<iCAX::Geometry::ICadReferenceService>();Array preview;
    for(int i=0;i<=64;++i)preview.emplace_back(CutPoint(service->EvaluateCurve(composite,0,span.First+(span.Last-span.First)*i/64.)));
    return {{"edgeUse",Map{{"kind",Text("Edge")},{"localId",std::to_string(span.EdgeID)},{"faceUse",face.at("faceUse")},
        {"geometryRef",WireRef(StoredRef(input.GeometryRef))}}},{"previewPoints",preview},{"point",resolved.at("point")}};
}
Map ExtractCutCandidates(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","placementId","sourceGeometry","selection","extractionRuleRef","representation","workpieceFamily",
        "side","declaredThicknessMm","requestedKinds","draftId","draftRevision","draftDigest","geometryOnly"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("Extract:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)
    {if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(commandID))throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    auto input=CutSource(scene,request);
    if(ReadWireRef(request.at("sourceGeometry").To<Map>())!=input.GeometryRef)throw std::invalid_argument("CAM_GEOMETRY_VERSION_CONFLICT");
    if(request.at("extractionRuleRef").To<Map>()!=EngineeringIdentity(input.PlanningData))throw std::invalid_argument("CAM_PROFILE_VERSION_CONFLICT");
    LoadRecognitionProfile(scene,input);
    input.GeometryOnly=request.contains("geometryOnly")&&request.at("geometryOnly").To<bool>();
    const auto selection=request.at("selection").To<Map>();CutFields(selection,{"mode","faceUses","edgeUses"});
    const auto mode=String(selection,"mode");
    if(mode!="Faces"&&mode!="Automatic"&&mode!="Edges"&&mode!="Region")throw std::invalid_argument("CAM_RECOGNITION_SELECTION_UNSUPPORTED");
    input.SelectionMode=mode=="Automatic"?"Faces":mode;
    const auto available=iCAX::CAM::Recognition::EnumerateFaceUses(*input.Geometry);
    if(mode=="Automatic")
    {
        if(!input.GeometryOnly)throw std::invalid_argument("CAM_AUTOMATIC_REQUIRES_GEOMETRY_EXTRACTION");
        if(!input.Geometry->Solids.empty())throw std::invalid_argument("CAM_AUTOMATIC_SOLID_REQUIRES_FACE_SELECTION");
        if(selection.contains("faceUses")&&!selection.at("faceUses").To<Array>().empty())throw std::invalid_argument("CAM_AUTOMATIC_SELECTION_CONFLICT");
        for(const auto& use:available)input.SelectedFaces.push_back(use.FaceID);
    }
    else if(mode=="Edges"){
        if(selection.contains("faceUses"))throw std::invalid_argument("CAM_RECOGNITION_SELECTION_CONFLICT");
        for(const auto& v:selection.at("edgeUses").To<Array>()){
            const auto use=v.To<Map>();CutFields(use,{"kind","localId","faceUse","geometryRef"});
            if(String(use,"kind")!="Edge"||ReadWireRef(use.at("geometryRef").To<Map>())!=input.GeometryRef)throw std::invalid_argument("CAM_TOPOLOGY_USE_MISMATCH");
            const auto face=use.at("faceUse").To<Map>();CutFields(face,{"kind","localId","usePath","geometryRef","occurrencePath"});
            const auto faceID=Integer(face,"localId");
            if(String(face,"kind")!="Face"||ReadWireRef(face.at("geometryRef").To<Map>())!=input.GeometryRef||!face.at("occurrencePath").To<Array>().empty()
                ||std::count_if(available.begin(),available.end(),[&](const auto& u){return u.FaceID==faceID&&CutIDs(u.UsePath)==face.at("usePath").To<Array>();})!=1)
                throw std::invalid_argument("CAM_TOPOLOGY_USE_MISMATCH");
            input.SelectedEdges.push_back(CutEdgeSpan(*input.Geometry,faceID,Integer(use,"localId")));
            if(std::find(input.SelectedFaces.begin(),input.SelectedFaces.end(),faceID)==input.SelectedFaces.end())input.SelectedFaces.push_back(faceID);
        }
    }
    else for(const auto& v:selection.at("faceUses").To<Array>())
    {
        const auto use=v.To<Map>();CutFields(use,{"kind","localId","usePath","geometryRef","occurrencePath"});
        if(String(use,"kind")!="Face"||ReadWireRef(use.at("geometryRef").To<Map>())!=input.GeometryRef||!use.at("occurrencePath").To<Array>().empty())
            throw std::invalid_argument("CAM_TOPOLOGY_USE_MISMATCH");
        const auto id=Integer(use,"localId");
        const auto count=std::count_if(available.begin(),available.end(),[&](const auto& u){return u.FaceID==id&&CutIDs(u.UsePath)==use.at("usePath").To<Array>();});
        if(count!=1)throw std::invalid_argument("CAM_TOPOLOGY_USE_MISMATCH");
        input.SelectedFaces.push_back(id);
    }
    input.Representation=String(request,"representation");input.WorkpieceFamily=String(request,"workpieceFamily");
    if(input.GeometryOnly){input.Representation=input.Geometry->Solids.empty()?"SkinSurface":"Solid";input.WorkpieceFamily=input.Geometry->Solids.empty()?"SheetSurface":"GeneralSolid";}
    const auto side=request.at("side").To<Map>();CutFields(side,{"outwardSign","observationDirection"});
    const auto sign=RegistrationNumber(side.at("outwardSign"));if(sign!=1.&&sign!=-1.)throw std::invalid_argument("CAM_RECOGNITION_SIDE_INVALID");input.OutwardSign=static_cast<int>(sign);
    if(side.contains("observationDirection")){
        const auto values=side.at("observationDirection").To<Array>();if(values.size()!=3||mode!="Faces"||!input.GeometryOnly)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_INVALID");
        std::array<double,3> direction;for(int i=0;i<3;++i)direction[i]=RegistrationNumber(values[i]);
        const auto scale=std::max({std::abs(direction[0]),std::abs(direction[1]),std::abs(direction[2])});
        if(!std::isfinite(scale)||scale==0)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_INVALID");
        for(auto& v:direction)v/=scale;const auto length=std::hypot(direction[0],direction[1],direction[2]);for(auto& v:direction)v/=length;input.ObservationDirection=direction;
    }
    if(request.contains("declaredThicknessMm"))input.DeclaredThicknessMm=RegistrationNumber(request.at("declaredThicknessMm"));
    input.RequestedKinds.clear();for(const auto& v:request.at("requestedKinds").To<Array>())input.RequestedKinds.push_back(v.To<Text>());
    ImportTask task;task.Kind="Extract";task.CommandID=commandID;task.Payload=payload;task.SetupID=Identifier(request,"setupId");
    task.PlacementID=Identifier(request,"placementId");task.Sequence=runtime->Sequence;
    task.DraftID=iCAX::Data::to_string(Identifier(request,"draftId"));task.DraftRevision=Integer(request,"draftRevision");task.DraftDigest=String(request,"draftDigest");
    if(task.DraftRevision==0||task.DraftDigest.empty()||task.DraftDigest.size()>256)throw std::invalid_argument("CAM_EXTRACTION_DRAFT_INVALID");
    if(const auto old=runtime->LatestExtraction.find(task.PlacementID);old!=runtime->LatestExtraction.end())
    {
        const auto& prior=runtime->Tasks.at(old->second);
        if(prior.DraftID==task.DraftID&&prior.DraftRevision>=task.DraftRevision)throw std::invalid_argument("CAM_EXTRACTION_DRAFT_REVISION_CONFLICT");
    }
    auto service=scene.Services().Resolve<iCAX::CAM::Recognition::ICamCutRecognitionService>();
    if(!service)throw std::runtime_error("CAM_RECOGNITION_SERVICE_MISSING");
    input.Profile.Geometry.IsCancelled=[cancelled=task.Cancelled]{return cancelled->load();};
    task.RecognitionInput=std::make_shared<const RecognitionInput>(std::move(input));
    task.RecognitionWorker=iCAX::Tasks::Run([service,input=task.RecognitionInput]{return std::make_shared<iCAX::CAM::Recognition::SRecognitionResult>(service->Recognize(*input));});
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));runtime->LatestExtraction[Identifier(request,"placementId")]=id;
    return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
Map QueryCutCandidates(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto ref=ReadWireRef(request.at("candidateSet").To<Map>());
    for(const auto& [_,task]:runtime->Tasks)if(task.Kind=="Extract"&&task.CandidateSet==ref)
    {
        if(task.Status!="Completed"||!CurrentExtraction(*runtime,task)||!task.RecognitionResult)throw std::invalid_argument("CAM_CANDIDATES_SUPERSEDED");
        const auto start=request.contains("pageToken")?Integer(request,"pageToken"):0;
        const auto size=request.contains("pageSize")?Integer(request,"pageSize"):100;
        const auto& result=*task.RecognitionResult;
        if(!size||size>200||start>result.Candidates.size())throw std::invalid_argument("CAM_CANDIDATE_PAGE_INVALID");
        const auto end=std::min<UInt>(result.Candidates.size(),start+size);Array candidates;
        for(auto i=start;i<end;++i)candidates.emplace_back(CutCandidate(result.Candidates[i]));
        return {{"candidateSet",request.at("candidateSet")},{"candidates",candidates},{"totalCount",std::to_string(result.Candidates.size())},
            {"nextPageToken",end<result.Candidates.size()?std::to_string(end):Text()},{"coverageComplete",result.Complete},{"inputDigest",result.InputDigest},
            {"draftId",task.DraftID},{"draftRevision",std::to_string(task.DraftRevision)},{"draftDigest",task.DraftDigest},{"issues",CutStrings(result.Issues)}};
    }
    throw std::invalid_argument("CAM_CANDIDATE_SET_UNKNOWN");
}
