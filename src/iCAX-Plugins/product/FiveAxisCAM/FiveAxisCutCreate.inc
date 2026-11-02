namespace
{
    UInt NextCutNumber(Scene& scene,const ID& setupID)
    {
        UInt number=0;
        for(const auto& cut:IntentCuts(scene,setupID))number=std::max(number,cut->GetDisplayNumber());
        return Next(number);
    }
    struct CutPublicationBatch
    {
        std::map<std::uint64_t,const iCAX::GeometryData::Curve3Record*> Curves;
        Array SelectedUses;
        std::map<std::uint64_t,std::optional<std::array<double,3>>> PlaneNormals;
        explicit CutPublicationBatch(const ImportTask& task)
        {
            const auto& input=*task.RecognitionInput;
            for(const auto& curve:input.Geometry->Curves3)Curves.emplace(curve.Id,&curve);
            const std::set<std::uint64_t> selected(input.SelectedFaces.begin(),input.SelectedFaces.end());
            for(const auto& use:iCAX::CAM::Recognition::EnumerateFaceUses(*input.Geometry))
                if(selected.contains(use.FaceID))SelectedUses.emplace_back(CutFaceUse(use,input.GeometryRef));
        }
    };
    int ObservedCutSide(Scene& scene,const ImportTask& task,const iCAX::CAM::Recognition::SCutCandidate& c,CutPublicationBatch& batch)
    {
        const auto& input=*task.RecognitionInput;if(!input.ObservationDirection)return input.OutwardSign;
        // Resolve oriented surface normals on the exact selected CAD spans.
        // The viewing ray points into the material; tool axes point towards
        // the observer. Curved faces use their differential, never a fitted plane.
        double dot=0;const auto service=scene.Services().Resolve<iCAX::Geometry::ICadReferenceService>();
        for(const auto& span:c.Boundary.Spans){
            iCAX::GeometryData::CompositeCurve3 curve;const auto record=batch.Curves.find(span.CurveID);
            if(record==batch.Curves.end())throw std::runtime_error("CAM_INTENT_SOURCE_CURVE_MISSING");
            curve.Segments.push_back({record->second->Geometry,{span.First,span.Last,false}});
            auto plane=batch.PlaneNormals.find(span.FaceID);
            if(plane==batch.PlaneNormals.end()){
                iCAX::Geometry::SCadReferenceQuery query;query.Mode=decltype(query.Mode)::Plane;query.FaceID=span.FaceID;
                query.NearPoint=service->EvaluateCurve(curve,0,(span.First+span.Last)*.5);query.MaximumDistanceMm=input.Profile.Geometry.DistanceToleranceMm;
                std::optional<std::array<double,3>> normal;
                try{normal=service->Resolve(*input.Geometry,query).Direction;}
                catch(const std::invalid_argument& error){if(Text(error.what())!="CAD_REFERENCE_REQUIRES_PLANE")throw;}
                plane=batch.PlaneNormals.emplace(span.FaceID,normal).first;
            }
            if(plane->second){for(int i=0;i<3;++i)dot+=3*(*plane->second)[i]*(*input.ObservationDirection)[i];continue;}
            for(const double t:{.25,.5,.75}){
                iCAX::Geometry::SCadReferenceQuery query;query.Mode=decltype(query.Mode)::SurfaceNormal;query.FaceID=span.FaceID;
                query.NearPoint=service->EvaluateCurve(curve,0,span.First+(span.Last-span.First)*t);query.MaximumDistanceMm=input.Profile.Geometry.DistanceToleranceMm;
                const auto normal=service->Resolve(*input.Geometry,query).Direction;
                for(int i=0;i<3;++i)dot+=normal[i]*(*input.ObservationDirection)[i];
            }
        }
        if(std::abs(dot)<1e-10)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_TANGENT");
        return dot>0?-1:1;
    }
    Map StandardHoleDescriptor(Scene& scene,const ImportTask& task,const iCAX::CAM::Recognition::SCutCandidate& c,CutPublicationBatch& batch,int outwardSign)
    {
        const auto& b=c.Boundary;
        if(c.MachiningKind!="Cut"||b.RegionRelation!="InnerLoop"||!b.Valid||!b.Closed||!b.StandardBoundProven||
            (c.GeometryKind!="Circle"&&c.GeometryKind!="Ellipse"&&c.GeometryKind!="Rectangle"&&c.GeometryKind!="Slot"))return {};
        // A bounded analytic hole alone does not orient a beam. Resolve its
        // machining-side normal through the neutral CAD service.
        iCAX::Geometry::SCadReferenceQuery query;query.Mode=decltype(query.Mode)::Plane;
        query.FaceID=b.Spans.front().FaceID;query.NearPoint=b.PreviewPoints.front();
        query.MaximumDistanceMm=task.RecognitionInput->Profile.Geometry.DistanceToleranceMm;
        // Candidates belong to the same immutable, validated extraction ticket.
        // Their source spans are already on this face; its oriented plane normal
        // is constant. Reuse only within this batch, never across model versions.
        auto found=batch.PlaneNormals.find(query.FaceID);
        if(found==batch.PlaneNormals.end()) {
            std::optional<std::array<double,3>> normal;
            try {normal=scene.Services().Resolve<iCAX::Geometry::ICadReferenceService>()->Resolve(*task.RecognitionInput->Geometry,query).Direction;}
            catch(const std::invalid_argument& error) {
                if(Text(error.what())!="CAD_REFERENCE_REQUIRES_PLANE")throw;
            }
            found=batch.PlaneNormals.emplace(query.FaceID,normal).first;
        }
        if(!found->second)return {};
        auto normal=*found->second;
        double dot=0;for(int i=0;i<3;++i)dot+=normal[i]*b.Normal[i];
        if(std::abs(std::abs(dot)-1)>1e-8)return {};
        for(auto& v:normal)v*=outwardSign;
        Map descriptor{{"kind",c.GeometryKind},{"frame",Text("Workpiece")},{"direction",CutPoint(normal)},{"center",CutPoint(b.Center)},{"xDirection",CutPoint(b.XDirection)}};
        if(c.GeometryKind=="Circle")descriptor["diameterMm"]=2*b.RadiusMm;
        else if(c.GeometryKind=="Ellipse"){descriptor["majorDiameterMm"]=b.LengthMm;descriptor["minorDiameterMm"]=b.WidthMm;}
        else {descriptor["widthMm"]=b.WidthMm;descriptor["lengthMm"]=b.LengthMm;}
        return descriptor;
    }
    Map StandardHolePose(const Map& descriptor)
    {return {{"schema",Text("cam.pose-policy")},{"schemaVersion",Text("1")},{"mode",Text("FixedDirection")},
        {"frame",descriptor.at("frame")},{"direction",descriptor.at("direction")}};}
    struct CutPublication {ID IntentID;Map Curve,Recipe,Feature,Pose;Text SourceKey,Name;};
    CutPublication PublishConfirmedCut(Scene& scene,const ImportTask& task,const iCAX::CAM::Recognition::SCutCandidate& c,const Map& contourPose,CutPublicationBatch& batch)
    {
        const auto& input=*task.RecognitionInput;
        const auto outwardSign=ObservedCutSide(scene,task,c,batch);
        auto curve=std::make_shared<iCAX::CAM::Intent::CIntentCurveResource>();
        curve->Version=1;curve->HasCurve3=true;curve->Parameterization="Native";curve->Curve3.Closed=c.Boundary.Closed;
        for(const auto& span:c.Boundary.Spans)
        {
            const auto found=batch.Curves.find(span.CurveID);
            if(found==batch.Curves.end())throw std::runtime_error("CAM_INTENT_SOURCE_CURVE_MISSING");
            curve->Curve3.Segments.push_back({found->second->Geometry,{span.First,span.Last,span.Reversed}});
        }
        if(curve->Curve3.Segments.empty())throw std::runtime_error("CAM_INTENT_EMPTY_CURVE");
        CutPublication out;out.IntentID=iCAX::Data::GenerateNewUUID();out.SourceKey=c.SourceKey;
        out.Name=c.MachiningKind=="Mark"?"打标":c.BoundaryRole=="OuterTrim"?"修边":c.GeometryKind=="Circle"?"圆":c.GeometryKind=="Rectangle"?"矩形":c.GeometryKind=="Slot"?"腰型":"刀路";
        out.Curve=IntentCurveData(*curve);
        auto evidence=CutCandidate(c);evidence["schema"]=Text("cam.recognition-evidence");evidence["schemaVersion"]=Text("1");
        if(input.ObservationDirection)evidence["observationDirection"]=CutPoint(*input.ObservationDirection);
        evidence["inputDigest"]=task.RecognitionResult->InputDigest;evidence["algorithmIdentity"]=task.RecognitionResult->AlgorithmIdentity;
        const auto evidenceRef=EngineeringData(evidence);
        auto boundary=CutBoundary(c.Boundary);auto spans=boundary.at("sourceSpans").To<Array>();
        for(auto& span:spans){auto record=span.To<Map>();record["sourceSpanId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());span=record;}
        const auto sourceMap=EngineeringData({{"schema",Text("cam.intent-source-map")},{"schemaVersion",Text("1")},
            {"sourceGeometry",WireRef(StoredRef(input.GeometryRef))},{"curveData",WireRef(StoredRef(out.Curve))},{"spans",spans}});
        Array selectedEdges;for(const auto& edge:input.SelectedEdges)selectedEdges.emplace_back(Map{{"edgeId",std::to_string(edge.EdgeID)},
            {"faceId",std::to_string(edge.FaceID)},{"wireId",std::to_string(edge.WireID)},{"coedgeIndex",std::to_string(edge.CoedgeIndex)}});
        out.Recipe=EngineeringData({{"schema",Text("cam.source-recipe")},{"schemaVersion",Text("1")},{"kind",Text("BoundaryRecognition")},
            {"selectionMode",input.SelectionMode},{"selectedEdges",selectedEdges},
            {"sourceGeometry",WireRef(StoredRef(input.GeometryRef))},{"planningProfileData",input.PlanningData},
            {"selectedUses",batch.SelectedUses},{"representation",input.Representation},{"workpieceFamily",input.WorkpieceFamily},
            {"outwardSign",static_cast<long long>(outwardSign)},{"declaredThicknessMm",input.DeclaredThicknessMm},
            {"sourceMapData",WireRef(StoredRef(sourceMap))},{"evidenceData",WireRef(StoredRef(evidenceRef))},{"sourceKey",c.SourceKey}});
        auto feature=CutCandidate(c);feature["schema"]=Text("cam.feature-definition");feature["schemaVersion"]=Text("1");
        feature["recognitionEvidenceData"]=WireRef(StoredRef(evidenceRef));feature["sourceMapData"]=WireRef(StoredRef(sourceMap));
        feature["curveFrame"]=Text("Workpiece");feature["retainedSide"]=Text(c.BoundaryRole=="Opening"?"Exterior":c.BoundaryRole=="OuterTrim"?"Interior":"Unspecified");
        feature["start"]=Map{{"sourceSpanId",spans.front().To<Map>().at("sourceSpanId")},
            {"nativeParameter",c.Boundary.Spans.front().Reversed?c.Boundary.Spans.front().Last:c.Boundary.Spans.front().First}};
        out.Pose=contourPose;
        const auto standard=StandardHoleDescriptor(scene,task,c,batch,outwardSign);
        if(!standard.empty()) {
            feature["standardHole"]=standard;feature["contourPoseData"]=WireRef(StoredRef(contourPose));

            out.Pose=EngineeringData(StandardHolePose(standard));
        }
        out.Feature=EngineeringData(feature);return out;
    }
}
Map CreateCuts(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","placementId","candidateSet","candidateIds","draftDigest","decisions","process","pose"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("CreateCuts:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end())
    {if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
    const auto ref=ReadWireRef(request.at("candidateSet").To<Map>());ImportTask* ticket=nullptr;
    for(auto& [_,task]:runtime->Tasks)if(task.Kind=="Extract"&&task.CandidateSet==ref){ticket=&task;break;}
    if(!ticket)throw std::invalid_argument("CAM_CANDIDATE_SET_UNKNOWN");
    auto& task=*ticket;
    if(!task.ConsumedByCommandID.empty())throw std::invalid_argument("CAM_CANDIDATE_ALREADY_CONSUMED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    if(task.Status!="Completed"||!CurrentExtraction(*runtime,task)||!task.RecognitionResult)throw std::invalid_argument("CAM_CANDIDATES_SUPERSEDED");
    if(task.SetupID!=Identifier(request,"setupId")||task.PlacementID!=Identifier(request,"placementId")||task.DraftDigest!=String(request,"draftDigest"))
        throw std::invalid_argument("CAM_CANDIDATE_CONTEXT_MISMATCH");
    if(!request.at("decisions").To<Array>().empty())throw std::invalid_argument("CAM_CANDIDATE_DECISION_REQUIRES_REEXTRACTION");
    const auto source=CutSource(scene,request);
    if(source.GeometryRef!=task.RecognitionInput->GeometryRef||EngineeringIdentity(source.PlanningData)!=EngineeringIdentity(task.RecognitionInput->PlanningData))
        throw std::invalid_argument("CAM_CANDIDATE_INPUT_CHANGED");
    std::set<Text> ids;std::vector<const iCAX::CAM::Recognition::SCutCandidate*> selected;
    for(const auto& v:request.at("candidateIds").To<Array>())
    {
        const auto id=v.To<Text>();if(!ids.insert(id).second)throw std::invalid_argument("CAM_CANDIDATE_DUPLICATE_SELECTION");
        const auto found=std::find_if(task.RecognitionResult->Candidates.begin(),task.RecognitionResult->Candidates.end(),[&](const auto& c){return c.LocalID==id;});
        if(found==task.RecognitionResult->Candidates.end())throw std::invalid_argument("CAM_CANDIDATE_NOT_FOUND");
        if(found->ConfirmationEligibility!="Ready")throw std::invalid_argument("CAM_CANDIDATE_NOT_CONFIRMABLE");selected.push_back(&*found);
    }
    if(selected.empty())throw std::invalid_argument("CAM_CANDIDATE_SELECTION_REQUIRED");
    for(const auto& cut:IntentCuts(scene))if(cut->GetPlacementID()==task.PlacementID)
        for(const auto* candidate:selected)if(candidate->SourceKey==cut->GetSourceKey())throw std::invalid_argument("CAM_EXISTING_INTENT_SOURCE");
    const auto process=request.at("process").To<Map>(),pose=request.at("pose").To<Map>();
    ValidateInitialCutPolicies(scene,process,pose);
    if(String(EngineeringBody(pose,"cam.pose-policy"),"mode")!="Continuous")throw std::invalid_argument("CAM_STANDARD_HOLE_REQUIRES_GEOMETRY");
    const auto assignment=EngineeringBody(process,"cam.process-assignment");
    if(RegistrationNumber(assignment.at("overcutLengthMm"))>0||RegistrationNumber(assignment.at("gapLengthMm"))>0)for(const auto* c:selected)
        if(!c->Boundary.Closed)throw std::invalid_argument("CAM_OVERCUT_REQUIRES_CLOSED_CURVE");
    const auto priority=[](const auto* c){return c->MachiningKind=="Mark"?0:c->BoundaryRole=="OuterTrim"?2:1;};
    std::stable_sort(selected.begin(),selected.end(),[&](const auto* a,const auto* b){return priority(a)<priority(b);});
    CutPublicationBatch batch(task);
    std::vector<CutPublication> publications;publications.reserve(selected.size());
    for(const auto* c:selected)
    {try{publications.push_back(PublishConfirmedCut(scene,task,*c,pose,batch));}
     catch(const std::bad_variant_access&){throw std::runtime_error("CAM_CREATE_PUBLICATION_VALUE_TYPE: "+c->LocalID);}}
    using namespace iCAX::CAM::Intent;
    Array created;auto number=NextCutNumber(scene,task.SetupID);std::vector<Map> commands;
    SEditableToolpathHostEdit host;host.UndoName="创建切割定义";
    const auto plan=IntentPlan(scene,task.SetupID);iCAX::Data::uuid_name_generator sourceNames(Identifier(request,"commandId"));
    for(const auto& publication:publications)
    {
        try
        {
        SFiveAxisIntentSource input;input.PathId=publication.IntentID;input.SourceOperationId=iCAX::Data::GenerateNewUUID();input.SetupID=task.SetupID;input.PlacementID=task.PlacementID;
        input.Name=publication.Name;input.UserNumber=std::to_string(number++);
        const auto sourceCommand=sourceNames("source/"+iCAX::Data::to_string(input.PathId));const auto port=SourceToolpathPort(input.SourceOperationId);
        const auto curve=ReadIntentCurveData(publication.Curve);const auto feature=EngineeringBody(publication.Feature,"cam.feature-definition");
        const auto sourceMap=EngineeringBody(feature.at("sourceMapData").To<Map>(),"cam.intent-source-map");input.SourceSpans=sourceMap.at("spans").To<Array>();
        const auto frame=Text("placement/")+iCAX::Data::to_string(task.PlacementID);std::vector<Text> segmentIds;
        for(const auto& span:input.SourceSpans)segmentIds.push_back(frame+"/span/"+String(span.To<Map>(),"sourceSpanId"));
        input.Value=MakeFiveAxisIntentValue(curve->Curve3,segmentIds,frame);
        const Map metadata{{"setupId",task.SetupID},{"placementId",task.PlacementID},{"beforeCommands",Text()},{"afterCommands",Text()},
            {"sourceKey",publication.SourceKey},{"sourceGeometryRef",StoredRef(source.GeometryRef)},
            {"sourceData",EngineeringBody(publication.Recipe,"cam.source-recipe")},{"feature",feature},
            {"pose",EngineeringBody(publication.Pose,"cam.pose-policy")},{"process",assignment},{"inputRevision",static_cast<unsigned long long>(1)}};
        input.Value.Channels=BuildFiveAxisIntentChannels(metadata,input.Value.Geometry,input.SourceSpans,port);
        commands.push_back({{"kind",Text("CreateSource")},{"commandId",sourceCommand},{"planId",plan},{"name",input.Name},
            {"userNumber",input.UserNumber},{"labelScope",iCAX::Data::to_string(task.SetupID)},{"value",ToData(input.Value)}});
        host.SourceIdentities[sourceCommand]={ESourcePathIdentityMode::CreateWithId,input.PathId,input.SourceOperationId};
        auto processing=BuildIntentProcessing(input,plan,sourceCommand);commands.insert(commands.end(),processing.begin(),processing.end());
        created.emplace_back(iCAX::Data::to_string(input.PathId));
        }
        catch(const std::bad_variant_access&){throw std::runtime_error("CAM_CREATE_SOURCE_VALUE_TYPE: "+publication.Name);}
    }
    host.Stage=[&](auto& tx,const auto&){Invalidate(tx,scene.Database(),task.SetupID);};
    try{IntentDocument(scene).ExecuteBatch(commands,host);}
    catch(const std::bad_variant_access&){throw std::runtime_error("CAM_CREATE_DOCUMENT_VALUE_TYPE");}
    Runtime(scene)->IntentInputsChanged=false;
    Map result{{"createdIntentIds",created},{"setupId",request.at("setupId")},{"placementId",request.at("placementId")}};
    result["definitionProjection"]=CutDefinitionsProjection(scene,created);
    // The projection already contains the same authoritative metadata patch.
    // Reuse it instead of rebuilding every pending row before the projection.
    result["statePatch"]=result.at("definitionProjection").To<Map>().at("statePatch");
    // Finish the owner-thread pending projection before starting the workers.
    // Both perform substantial value work; overlapping them here delays the
    // synchronous reply while the UI cannot consume any generated result yet.
    // Initial chains materialize large engineering values. The Debug CRT's
    // allocation tracking contends heavily between these workers; one worker
    // is faster for this batch and still keeps computation off the scene owner.
#ifdef _DEBUG
    QueueIntentGeneration(scene,task.SetupID,1);
#else
    QueueIntentGeneration(scene,task.SetupID);
#endif
    const auto generation=runtime->IntentGenerationTasks.at(plan);
    result["definitionTask"]=TaskState(scene,*runtime,runtime->Tasks.at(generation));
    task.ConsumedByCommandID=commandID;runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
}

