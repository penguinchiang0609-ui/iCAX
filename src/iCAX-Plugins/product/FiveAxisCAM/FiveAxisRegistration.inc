// Included inside the product namespace; all state stays on the Scene thread.
namespace
{
    Map RegistrationReport(Scene& scene,const Map& stored)
    {
        if(stored.empty())return {};
        return EngineeringBody(stored);
    }
    Map RegistrationSummary(Scene& scene,const Map& stored)
    {
        if(stored.empty())return {};
        const auto report=RegistrationReport(scene,stored);
        Map result;
        for(const auto* key:{"rmsMm","maximumResidualMm","modelConditionRatio","measuredConditionRatio","qualified"})result[key]=report.at(key);
        return result;
    }
    double RegistrationNumber(const Variant& v)
    {
        return FiniteNumber(v,"CAM_REGISTRATION_NUMBER_REQUIRED");
    }
    iCAX::Geometry::RegistrationPoint RegistrationPoint(const Variant& value)
    {
        const auto a=value.To<Array>();
        if(a.size()!=3)throw std::invalid_argument("CAM_REGISTRATION_POINT_REQUIRED");
        return {RegistrationNumber(a[0]),RegistrationNumber(a[1]),RegistrationNumber(a[2])};
    }
    Map ResolveReference(Scene& scene,const iCAX::GeometryData::BRepModel& geometry,const Map& query)
    {
        using Query=iCAX::Geometry::SCadReferenceQuery;
        Query q;
        const auto mode=String(query,"mode");
        if(mode=="point")q.Mode=Query::Kind::SurfacePoint;
        else if(mode=="endpoint")q.Mode=Query::Kind::Vertex;
        else if(mode=="midpoint")q.Mode=Query::Kind::EdgeMidpoint;
        else if(mode=="center")q.Mode=Query::Kind::CircleCenter;
        else if(mode=="line")q.Mode=Query::Kind::Line;
        else if(mode=="plane")q.Mode=Query::Kind::Plane;
        else if(mode=="axis")q.Mode=Query::Kind::CylinderAxis;
        else throw std::invalid_argument("CAM_REFERENCE_MODE_UNSUPPORTED");
        if(String(query,"frame")!="Workpiece")throw std::invalid_argument("CAM_REFERENCE_FRAME_MISMATCH");
        q.NearPoint=RegistrationPoint(query.at("nearPoint"));
        q.MaximumDistanceMm=RegistrationNumber(query.at("maximumDistanceMm"));
        UInt start=0,triangulationID=0;
        if(query.contains("faceId"))q.FaceID=Integer(query,"faceId");
        else {
        const auto triangle=Integer(query,"triangleIndex");
        // Same ordered fixed-version triangulations as RenderResourceAdapter.
        for(const auto& record:geometry.Triangulations3)
        {
            if(record.Geometry.Vertices.empty()||record.Geometry.Triangles.empty())continue;
            const auto count=record.Geometry.Triangles.size();
            if(triangle>=start&&triangle-start<count){triangulationID=record.Id;break;}
            start+=count;
        }
        if(!triangulationID)throw std::invalid_argument("CAM_REFERENCE_TRIANGLE_OUT_OF_RANGE");
        for(const auto& face:geometry.Faces)if(face.Triangulation3Id==triangulationID)
        {
            if(q.FaceID)throw std::invalid_argument("CAM_REFERENCE_FACE_USE_AMBIGUOUS");
            q.FaceID=face.Id;
        }
        }
        if(!q.FaceID||std::none_of(geometry.Faces.begin(),geometry.Faces.end(),[&](const auto& face){return face.Id==q.FaceID;}))
            throw std::invalid_argument("CAM_REFERENCE_FACE_MISSING");
        auto result=scene.Services().Resolve<iCAX::Geometry::ICadReferenceService>()->Resolve(geometry,q);
        const double sign=query.contains("sign")?RegistrationNumber(query.at("sign")):1.;
        if(sign!=1.&&sign!=-1.)throw std::invalid_argument("CAM_REFERENCE_SIGN_INVALID");
        for(auto& v:result.Direction)v*=sign;
        auto stableQuery=query;stableQuery["faceId"]=std::to_string(q.FaceID);
        return {{"point",Array{result.Point[0],result.Point[1],result.Point[2]}},
            {"direction",Array{result.Direction[0],result.Direction[1],result.Direction[2]}},
            {"faceId",std::to_string(result.FaceID)},{"edgeId",std::to_string(result.EdgeID)},
            {"distanceMm",result.DistanceMm},{"query",stableQuery}};
    }
    Map ComputeRegistration(Scene& scene,const Map& specification)
    {
        using Request=iCAX::Geometry::SRigidRegistrationRequest;
        Request request;
        if(String(specification,"modelFrame")!="Workpiece"||String(specification,"targetFrame")!="Setup"||String(specification,"unit")!="mm")
            throw std::invalid_argument("CAM_REGISTRATION_FRAME_MISMATCH");
        const auto method=String(specification,"method");
        if(method=="Points")request.Kind=Request::Method::Points;
        else if(method=="Lines")request.Kind=Request::Method::Lines;
        else if(method=="Plane")request.Kind=Request::Method::Plane;
        else if(method=="PairedPoints")request.Kind=Request::Method::PairedPoints;
        else throw std::invalid_argument("CAM_REGISTRATION_METHOD_UNSUPPORTED");
        if(specification.contains("minimumSpreadMm"))request.MinimumSpreadMm=RegistrationNumber(specification.at("minimumSpreadMm"));
        if(specification.contains("minimumConditionRatio"))request.MinimumConditionRatio=RegistrationNumber(specification.at("minimumConditionRatio"));
        Text weightSource="Equal";
        if(request.Kind==Request::Method::PairedPoints)
        {
            request.MaximumResidualMm=RegistrationNumber(specification.at("maximumResidualMm"));
            for(const auto& p:specification.at("modelPoints").To<Array>())request.ModelPoints.push_back(RegistrationPoint(p));
            for(const auto& p:specification.at("measuredPoints").To<Array>())request.MeasuredPoints.push_back(RegistrationPoint(p));
            if(specification.contains("weights"))
            {
                for(const auto& w:specification.at("weights").To<Array>())request.Weights.push_back(RegistrationNumber(w));
                weightSource=String(specification,"weightSource");
                if(request.Weights.empty()||weightSource.empty())throw std::invalid_argument("CAM_REGISTRATION_WEIGHT_SOURCE_REQUIRED");
            }
        }
        else
        {
            request.Origin=RegistrationPoint(specification.at("origin"));
            request.XReference=RegistrationPoint(specification.at(method=="Points"?"xPoint":"xDirection"));
            request.PlaneReference=RegistrationPoint(specification.at(method=="Points"?"planePoint":method=="Lines"?"planeDirection":"normal"));
        }
        const auto service=scene.Services().Resolve<iCAX::Geometry::IRigidRegistrationService>();
        if(!service)throw std::runtime_error("CAM_REGISTRATION_SERVICE_MISSING");
        const auto solution=service->Solve(request);
        Array transform,residuals,vectors;
        for(auto v:solution.Transform)transform.emplace_back(v==0.?0.:v);
        (void)RigidMatrix(transform);
        for(auto v:solution.Residuals)residuals.emplace_back(v);
        for(auto p:solution.ResidualVectors)vectors.emplace_back(Array{p[0],p[1],p[2]});
        return {{"method",method},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},
            {"input",specification},{"transform",transform},{"qualified",solution.Qualified},{"diagnostic",solution.Diagnostic},
            {"residualsMm",residuals},{"residualVectorsMm",vectors},{"rmsMm",solution.RmsMm},{"maximumResidualMm",solution.MaximumResidualMm},
            {"modelConditionRatio",solution.ModelConditionRatio},{"measuredConditionRatio",solution.MeasuredConditionRatio},
            {"reflectionCorrected",solution.ReflectionCorrected},{"weightSource",weightSource},
            {"minimumSpreadMm",request.MinimumSpreadMm},{"minimumConditionRatio",request.MinimumConditionRatio},
            {"residualLimitMm",request.MaximumResidualMm},{"applicationMode",Text("Pending")}};
    }
    Map ComputeAlignment(Scene& scene,const Map& request);
    Map RegistrationCandidate(Scene& scene,const Map& request)
    {
        CheckExpected(*Runtime(scene),request);
        if(String(request.at("registration").To<Map>(),"method")=="Alignment")return ComputeAlignment(scene,request);
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
        if(placement->GetSetupID()!=OptionalIdentifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
        const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
        auto specification=request.at("registration").To<Map>();
        Map resolvedReferences;
        if(specification.contains("referenceQueries"))
        {
            const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
            if(!ref)throw std::runtime_error("CAM_GEOMETRY_MISSING");
            const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
            if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
            const auto method=String(specification,"method");
            for(const auto& [field,queryValue]:specification.at("referenceQueries").To<Map>())
            {
                const auto query=queryValue.To<Map>();const auto mode=String(query,"mode");
                const auto result=ResolveReference(scene,*geometry,query);
                const bool vector=(method=="Lines"&&(field=="axisX"||field=="plane"))||(method=="Plane"&&field=="plane");
                if(vector&&mode!=(method=="Plane"&&field=="plane"?"plane":"line"))throw std::invalid_argument("CAM_REFERENCE_KIND_MISMATCH");
                if(!vector&&(mode=="line"||mode=="plane"))throw std::invalid_argument("CAM_REFERENCE_KIND_MISMATCH");
                if(method=="PairedPoints")
                {
                    auto points=specification.at("modelPoints").To<Array>();bool matched=false;
                    for(size_t i=0;i<points.size();++i)if(field=="model"+std::to_string(i+1)){points[i]=result.at("point");matched=true;break;}
                    if(!matched)throw std::invalid_argument("CAM_REFERENCE_FIELD_INVALID");
                    specification["modelPoints"]=points;
                }
                else if(field=="origin")specification["origin"]=result.at("point");
                else if(field=="axisX")specification[method=="Lines"?"xDirection":"xPoint"]=result.at(vector?"direction":"point");
                else if(field=="plane")specification[method=="Points"?"planePoint":method=="Lines"?"planeDirection":"normal"]=result.at(vector?"direction":"point");
                else throw std::invalid_argument("CAM_REFERENCE_FIELD_INVALID");
                resolvedReferences[field]=result;
            }
        }
        if(String(specification,"method")=="Plane"&&specification.contains("xPoint"))
        {
            const auto x=RegistrationPoint(specification.at("xPoint")),origin=RegistrationPoint(specification.at("origin"));
            specification["xDirection"]=Array{x[0]-origin[0],x[1]-origin[1],x[2]-origin[2]};
        }
        auto result=ComputeRegistration(scene,specification);
        result["resolvedReferences"]=resolvedReferences;
        result["geometryRef"]=WireRef(workpiece->GetGeometryRef());
        result["workpieceId"]=iCAX::Data::to_string(placement->GetWorkpieceID());
        result["placementId"]=request.at("placementId");
        result["setupId"]=request.at("setupId");
        result["sourcePlacementRevision"]=std::to_string(placement->GetInputRevision());
        result["expected"]=request.at("expected");
        return result;
    }
}
namespace {
    Array DisplayFaceRanges(const CFiveAxisWorkpieceComponent& workpiece, const iCAX::GeometryData::BRepModel& geometry) {
        const auto metadata=workpiece.GetImportMetadata();
        if(metadata.contains("displayMesh")) {
            const auto display=metadata.at("displayMesh").To<Map>();
            if(display.at("geometryRef")==workpiece.GetGeometryRef()&&display.at("previewRef")==workpiece.GetPreviewRef())
                return display.at("faces").To<Array>();
        }
        std::map<UInt,UInt> meshFaces;
        for(const auto& face:geometry.Faces)meshFaces.emplace(face.Triangulation3Id,face.Id);
        Array faces;UInt first=0;
        for(const auto& mesh:geometry.Triangulations3) {
            if(mesh.Geometry.Vertices.empty()||mesh.Geometry.Triangles.empty())continue;
            const auto count=mesh.Geometry.Triangles.size();
            if(const auto face=meshFaces.find(mesh.Id);face!=meshFaces.end())
                faces.emplace_back(Map{{"faceId",std::to_string(face->second)},{"first",std::to_string(first)},{"count",std::to_string(count)}});
            first+=count;
        }
        return faces;
    }
    Map DisplayReferenceQuery(const CFiveAxisWorkpieceComponent& workpiece, const iCAX::GeometryData::BRepModel& geometry, Map query) {
        const auto triangle=Integer(query,"triangleIndex");
        for(const auto& value:DisplayFaceRanges(workpiece,geometry)){const auto face=value.To<Map>();
            const auto first=std::stoull(String(face,"first")),count=std::stoull(String(face,"count"));
            if(triangle>=first&&triangle-first<count){query["faceId"]=face.at("faceId");return query;}
        }
        throw std::invalid_argument("CAM_REFERENCE_TRIANGLE_OUT_OF_RANGE");
    }
}
#include "FiveAxisPicking.inc"
#include "FiveAxisAlignment.inc"
Map ReadRegistrationFaces(Scene& scene,const Map& request)
{
    CheckExpected(*Runtime(scene),request);
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    if(placement->GetSetupID()!=OptionalIdentifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
    if(!ref)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
    if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    // Fixed View triangle order -> exact BRep face identity. Display only;
    // selecting/applying a reference still resolves the exact CAD geometry.
    return {{"geometryRef",WireRef(workpiece->GetGeometryRef())},{"faces",DisplayFaceRanges(*workpiece,*geometry)}};
}
Map ResolveRegistrationReference(Scene& scene,const Map& request)
{
    CheckExpected(*Runtime(scene),request);
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    if(placement->GetSetupID()!=OptionalIdentifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
    if(!ref)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
    if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    auto result=ResolveReference(scene,*geometry,DisplayReferenceQuery(*workpiece,*geometry,request.at("query").To<Map>()));
    result["geometryRef"]=WireRef(workpiece->GetGeometryRef());
    // Display evidence uses the same fixed triangulation as the View resource.
    // It is transient and is never accepted as a registration input.
    Array ranges;
    const auto faceID=Integer(result,"faceId");
    for(const auto& value:DisplayFaceRanges(*workpiece,*geometry)) {const auto face=value.To<Map>();
        if(String(face,"faceId")==std::to_string(faceID))ranges.emplace_back(Map{{"first",face.at("first")},{"count",face.at("count")}});
    }
    result["triangleRanges"]=ranges;
    if(String(request.at("query").To<Map>(),"mode")=="line") {
        const auto edgeID=Integer(result,"edgeId");
        const auto edge=std::find_if(geometry->Edges.begin(),geometry->Edges.end(),[&](const auto& e){return e.Id==edgeID;});
        Array ends;
        if(edge!=geometry->Edges.end())for(const auto id:{edge->StartVertexId,edge->EndVertexId}) {
            const auto vertex=std::find_if(geometry->Vertices.begin(),geometry->Vertices.end(),[&](const auto& v){return v.Id==id;});
            if(vertex!=geometry->Vertices.end())ends.emplace_back(Array{vertex->Position.X,vertex->Position.Y,vertex->Position.Z});
        }
        result["lineEnds"]=ends;
    }
    return result;
}
Map SolveRegistration(Scene& scene,const Map& request)
{
    return RegistrationCandidate(scene,request);
}
Map ReadRegistration(Scene& scene,const Map& request)
{
    CheckExpected(*Runtime(scene),request);
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    if(placement->GetSetupID()!=OptionalIdentifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto report=RegistrationReport(scene,placement->GetRegistrationData());
    if(!report.empty())
    {
        const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
        if(report.at("geometryRef")!=WireRef(workpiece->GetGeometryRef())||report.at("transform")!=placement->GetTransform()
            ||report.at("placementId")!=request.at("placementId")||report.at("setupId")!=request.at("setupId"))
            throw std::runtime_error("CAM_REGISTRATION_SOURCE_CHANGED");
    }
    return {{"registrationRef",EngineeringIdentity(placement->GetRegistrationData())},{"report",report}};
}
Map ApplyRegistration(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("ApplyRegistration:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto found=runtime->Receipts.find(commandID);found!=runtime->Receipts.end())
    {
        if(found->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
        return found->second.second;
    }
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckCapacity(*runtime);
    // Recompute from inputs; a browser-supplied matrix or qualified flag is not evidence.
    CheckExpected(*runtime,request);
    if(request.contains("resetWorkpieceFrame")&&request.at("resetWorkpieceFrame").To<bool>())
        RequireWorkpieceUnlocked(scene,Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"))->GetWorkpieceID());
    else RequireSetupUnlocked(scene,OptionalIdentifier(request,"setupId"));
    auto report=RegistrationCandidate(scene,request);
    if(!report.at("qualified").To<bool>())throw std::invalid_argument("CAM_REGISTRATION_RESIDUAL_EXCEEDED");
    const auto placementID=Identifier(request,"placementId"),setupID=OptionalIdentifier(request,"setupId");
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),placementID);
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    auto geometry=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
    if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    Ref appliedGeometry{geometry->URL,geometry->nVersion};
    const bool resetFrame=request.contains("resetWorkpieceFrame")&&request.at("resetWorkpieceFrame").To<bool>();
    Ref rebasedPreview;
    auto workpieceID=placement->GetWorkpieceID();
    std::map<ID,Array> rebasedTransforms;
    if(resetFrame){
        if(String(report,"method")!="Alignment")throw std::invalid_argument("CAM_FRAME_RESET_REQUIRES_ALIGNMENT");
        const auto source=scene.Resources().Get<iCAX::GeometryData::BRepModel>(geometry->URL,geometry->nVersion);
        const auto service=scene.Services().Resolve<iCAX::Geometry::ICadBRepTransformService>();
        const auto display=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
        if(!source||!service||!display)throw std::runtime_error("CAM_FRAME_RESET_SERVICE_UNAVAILABLE");
        auto rebased=std::make_shared<iCAX::GeometryData::BRepModel>(service->Transform(*source,RigidMatrix(report.at("transform").To<Array>())));
        const auto edges=display->BuildEdges(*rebased,{});
        report["sourceGeometryRef"]=WireRef(workpiece->GetGeometryRef());
        report["alignedTransform"]=report.at("transform");
        report["resetWorkpieceFrame"]=true;
        appliedGeometry=Publish(scene,rebased,"geometry.brep",workpiece->GetName(),{appliedGeometry});
        const auto transient=iCAX::RenderInteraction::EnsureFrontendBRepGeometryResource(scene.Resources(),appliedGeometry.URL,edges);
        const auto mesh=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(transient.URL,transient.nVersion);
        if(!mesh)throw std::runtime_error("CAM_PREVIEW_MISSING");
        rebasedPreview=Publish(scene,std::make_shared<iCAX::Resource::CFlatBufferResource>(*mesh),iCAX::Resource::CFlatBufferResource::kResourceTypeName,workpiece->GetName(),{appliedGeometry});
        report["geometryRef"]=WireRef(StoredRef(appliedGeometry));report["transform"]=Identity();
        const auto aligned=RigidMatrix(report.at("alignedTransform").To<Array>());
        AlignMatrix inverse{};inverse[15]=1;
        for(int i=0;i<3;++i){for(int j=0;j<3;++j)inverse[i*4+j]=aligned[j*4+i];for(int j=0;j<3;++j)inverse[i*4+3]-=inverse[i*4+j]*aligned[j*4+3];}
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))
            if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetWorkpieceID()==workpieceID){
                auto old=RigidMatrix(p->GetTransform());Array next;
                for(int i=0;i<4;++i)for(int j=0;j<4;++j){double v=0;for(int k=0;k<4;++k)v+=old[i*4+k]*inverse[k*4+j];next.emplace_back(v);}
                rebasedTransforms[e->GetID()]=e->GetID()==placementID?Identity():next;
            }
    }
    report["applicationMode"]=Text("AppliedInCAM");
    const auto reference=EngineeringData(report);
    const auto transform=report.at("transform").To<Array>();
    std::set<ID> stalePlacements,staleSetups;
    if(resetFrame)for(const auto& [id,matrix]:rebasedTransforms)
    {stalePlacements.insert(id);const auto setup=Require<CFiveAxisPlacementComponent>(scene.Database(),id)->GetSetupID();if(!setup.is_nil())staleSetups.insert(setup);}
    const auto intentCommands=StaleIntentSourceCommands(scene,stalePlacements,Identifier(request,"commandId"));
    CommitIntentHostCommands(scene,intentCommands,[&](auto& tx,const auto&){
        if(resetFrame){
            iCAX::Data::PropertySet fields{{"GeometryRef",StoredRef(appliedGeometry)},{"PreviewRef",StoredRef(rebasedPreview)},{"InputRevision",Next(workpiece->GetInputRevision())}};
            const auto input=report.at("input").To<Map>();
            if(input.contains("frame"))fields["CoordinateFrame"]=input.at("frame");
            else{
                const auto previous=workpiece->GetCoordinateFrame();const auto aligned=RigidMatrix(report.at("alignedTransform").To<Array>());
                AlignMatrix inverse{},pose{},next{};inverse[15]=pose[15]=1;
                for(int i=0;i<3;++i){for(int j=0;j<3;++j)inverse[i*4+j]=aligned[j*4+i];for(int j=0;j<3;++j)inverse[i*4+3]-=inverse[i*4+j]*aligned[j*4+3];}
                const auto origin=RegistrationPoint(previous.at("origin"));
                for(int i=0;i<3;++i){pose[i*4+3]=origin[i];for(int j=0;j<3;++j)pose[i*4+j]=RegistrationPoint(previous.at(std::array<Text,3>{"x","y","z"}[j]))[i];}
                for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)next[i*4+j]+=pose[i*4+k]*inverse[k*4+j];
                fields["CoordinateFrame"]=Map{{"origin",Array{next[3],next[7],next[11]}},{"x",Array{next[0],next[4],next[8]}},{"y",Array{next[1],next[5],next[9]}},{"z",Array{next[2],next[6],next[10]}}};
            }
            tx.ModifyComponent(workpieceID,CFiveAxisWorkpieceComponent::S_ClassName,fields);
            for(const auto& [id,matrix]:rebasedTransforms){
            const auto other=Require<CFiveAxisPlacementComponent>(scene.Database(),id);
            tx.ModifyComponent(id,CFiveAxisPlacementComponent::S_ClassName,{{"Transform",matrix},{"RegistrationData",Map()},{"RegistrationMethod",Text()},{"InputRevision",Next(other->GetInputRevision())}});
            tx.ModifyComponent(id,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                {{"GeometryResourceID",rebasedPreview.URL},{"GeometryResourceVersion",static_cast<UInt>(rebasedPreview.nVersion)}});
            Invalidate(tx,scene.Database(),other->GetSetupID());
            }
        }
        tx.ModifyComponent(placementID,CFiveAxisPlacementComponent::S_ClassName,
            {{"WorkpieceID",workpieceID},{"Transform",transform},{"RegistrationData",StoredRef(reference)},{"RegistrationMethod",report.at("method")},
             {"ApplicationMode",Text("AppliedInCAM")},{"InputRevision",Next(placement->GetInputRevision())}});
        Invalidate(tx,scene.Database(),setupID);
    },resetFrame?"对齐并重设物坐标系":"应用工件定位",staleSetups);
    Map result{{"placementId",request.at("placementId")},{"registrationRef",WireRef(StoredRef(reference))},{"report",report}};
    runtime->Receipts.emplace(commandID,std::make_pair(payload,result));
    if(resetFrame){for(const auto& [id,matrix]:rebasedTransforms)ProjectPlacement(scene,id,matrix);}
    else ProjectPlacement(scene,placementID,transform);
    return result;
}
