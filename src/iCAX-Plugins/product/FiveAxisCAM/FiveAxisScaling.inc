// Scene adapter: immutable worker inputs; atomic geometry/preview publication.
namespace
{
    Map PublishScale(Scene& scene,const ImportTask& task,ImportResult&& prepared)
    {
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),task.PlacementID);
        const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
        const auto oldRef=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
        if(!oldRef)throw std::runtime_error("CAM_GEOMETRY_MISSING");
        const auto geometry=Publish(scene,std::make_shared<iCAX::GeometryData::BRepModel>(std::move(prepared.Import.Geometry)),
            "geometry.brep",workpiece->GetName(),{{oldRef->URL,oldRef->nVersion}});
        const auto transient=iCAX::RenderInteraction::EnsureFrontendBRepGeometryResource(scene.Resources(),geometry.URL,prepared.DisplayEdges);
        const auto display=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(transient.URL,transient.nVersion);
        if(!display)throw std::runtime_error("CAM_PREVIEW_MISSING");
        const auto preview=Publish(scene,std::make_shared<iCAX::Resource::CFlatBufferResource>(*display),
            iCAX::Resource::CFlatBufferResource::kResourceTypeName,workpiece->GetName(),{geometry});
        std::set<ID> changedPlacements,changedSetups;
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto owner=Component<CFiveAxisPlacementComponent>(entity);owner&&owner->GetWorkpieceID()==placement->GetWorkpieceID())
            {changedPlacements.insert(entity->GetID());if(!owner->GetSetupID().is_nil())changedSetups.insert(owner->GetSetupID());}
        const auto commands=StaleIntentSourceCommands(scene,changedPlacements,ID::from_string(task.CommandID).value());
        CommitIntentHostCommands(scene,commands,[&](auto& tx,const auto&){
            tx.ModifyComponent(placement->GetWorkpieceID(),CFiveAxisWorkpieceComponent::S_ClassName,
                {{"GeometryRef",StoredRef(geometry)},{"PreviewRef",StoredRef(preview)},{"InputRevision",Next(workpiece->GetInputRevision())}});
            for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})) {
            const auto owner=Component<CFiveAxisPlacementComponent>(entity);if(!owner||owner->GetWorkpieceID()!=placement->GetWorkpieceID())continue;
            tx.ModifyComponent(entity->GetID(),iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                {{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
            tx.ModifyComponent(entity->GetID(),CFiveAxisPlacementComponent::S_ClassName,
                {{"RegistrationData",Map()},{"RegistrationMethod",Text()},{"ApplicationMode",Text("Pending")},{"InputRevision",Next(owner->GetInputRevision())}});
            Invalidate(tx,scene.Database(),owner->GetSetupID());
            }
        },"缩放工件",changedSetups);
        return {{"workpieceId",iCAX::Data::to_string(placement->GetWorkpieceID())},{"placementId",iCAX::Data::to_string(task.PlacementID)},
            {"geometryRef",WireRef(StoredRef(geometry))},{"previewRef",WireRef(StoredRef(preview))}};
    }
}
Map ScaleWorkpiece(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("Scale:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID){
        if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
        return TaskState(scene,*runtime,task);
    }
    if(runtime->Receipts.contains(commandID))throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto placementID=Identifier(request,"placementId"),setupID=OptionalIdentifier(request,"setupId");
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),placementID);
    if(placement->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    RequireWorkpieceUnlocked(scene,placement->GetWorkpieceID());
    if(request.at("sourceGeometry").To<Map>()!=WireRef(workpiece->GetGeometryRef()))throw std::invalid_argument("CAM_GEOMETRY_CHANGED");
    const auto f=request.at("factors").To<Array>(),c=request.at("center").To<Array>();
    if(f.size()!=3||c.size()!=3)throw std::invalid_argument("CAM_SCALE_VECTOR_REQUIRED");
    std::array<double,3> factors{},center{};
    for(int i=0;i<3;++i){
        factors[i]=FiniteNumber(f[i],"CAM_SCALE_INVALID");center[i]=FiniteNumber(c[i],"CAM_SCALE_INVALID");
        if(factors[i]<=0)throw std::invalid_argument("CAM_SCALE_INVALID: positive factors required");
    }
    if(factors[0]!=factors[1]||factors[1]!=factors[2])
        throw std::invalid_argument("CAM_SCALE_UNIFORM_REQUIRED: 仅支持 XYZ 等比缩放。");
    const auto ref=iCAX::ProjectFile::TryGetResourceReferenceValue(workpiece->GetGeometryRef());
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref->URL,ref->nVersion);
    if(!geometry)throw std::runtime_error("CAM_GEOMETRY_MISSING");
    const auto service=scene.Services().Resolve<iCAX::CAD::ICadBRepScaleService>();
    const auto display=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
    if(!service||!display)throw std::runtime_error("CAM_SCALE_SERVICE_UNAVAILABLE");
    ImportTask task;task.Kind="Scale";task.CommandID=commandID;task.Payload=payload;
    task.SetupID=setupID;task.PlacementID=placementID;task.Sequence=runtime->Sequence;
    const auto cancelled=[flag=task.Cancelled]{return flag->load();};
    task.Worker=iCAX::Tasks::Run([service,display,geometry,factors,center,cancelled]{
        iCAX::CAD::SCadBRepImportResult value;value.Geometry=service->Scale(*geometry,factors,center,cancelled);
        auto result=std::make_shared<ImportResult>(std::move(value));
        iCAX::GeometryServices::BRepDisplayOptions options;options.IsCancelled=cancelled;
        result->DisplayEdges=display->BuildEdges(result->Import.Geometry,options);return result;
    });
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
