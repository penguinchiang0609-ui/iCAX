namespace
{
    Map PathPreviewState(SceneRuntime& runtime,SceneRuntime::PathPreview& entry)
    {
        auto& task=entry.Task;
        if(entry.Sequence!=runtime.Sequence){task.Cancelled->store(true);task.Status="Cancelled";task.Result.clear();}
        if(task.Status=="Running"&&entry.DeferredRequest.empty()&&task.Worker.IsCompleted())
        {
            try{task.Result=std::move(*task.Worker.Result());task.Status="Completed";}
            catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
            catch(const std::exception& error){task.Status="Failed";task.Error=error.what();}
            task.Worker={};
        }
        return {{"epoch",runtime.Epoch},{"taskId",task.TaskID},{"status",task.Status},{"error",task.Error},{"result",task.Result}};
    }
}

namespace
{
void StartPathPreview(Scene& scene,const Map& request,SceneRuntime::PathPreview& entry)
{
    const auto runtime=Runtime(scene);const auto kind=String(request,"kind");const auto ids=request.at("intentIds").To<Array>();
    const auto patch=request.contains("patch")?request.at("patch").To<Map>():Map{};
    struct Input final
    {
        ID IDValue,PlacementID;double Width=0;bool Enabled=false;Text Error;
        iCAX::CAM::Intent::SResolvedPath Path;
        std::optional<IntentPreviewSnapshot> Resolved,Nominal;
        std::optional<iCAX::GeometryData::CompositeOffsetCurve3> NominalCurve;
    };
    std::set<ID> selected;
    for(const auto& value:ids){const auto id=CutID(value);if(!selected.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");}
    std::vector<Input> inputs;
    for(const auto& value:ids)
    {
        const auto id=CutID(value);Input input;input.IDValue=id;input.Path=ReadIntentPath(scene,id);
        if(kind=="Leads")
        {
            if(!patch.empty())input.Resolved=CaptureIntentProcessPreview(scene,input.Path,patch,selected);
            else if(input.Path.ResultState!="Current")input.Resolved=CaptureIntentPreview(scene,input.Path);
        }
        else
        {
            const auto metadata=ReadIntentInspectionMetadata(scene,input.Path);input.PlacementID=metadata.PlacementID;
            const auto& assignment=metadata.Process;
            if(!assignment.contains("schema")||String(assignment,"schema")!="cam.process-assignment"||assignment.at("schemaVersion").To<Text>()!="1")
                throw std::invalid_argument("CAM_ENGINEERING_DATA_SCHEMA_MISMATCH");
            input.Width=RegistrationNumber(std::get<Map>(assignment.at("kerf").m_Value).at("widthMm"));input.Enabled=IntentPathEnabledForMetadata(scene,input.Path,metadata);
            if(input.Width>0&&input.Enabled)try
            {
                if(input.Path.ResultState!="Current")input.Resolved=CaptureIntentPreview(scene,input.Path,{}, {},metadata.SetupID);
                const Map none{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};
                input.NominalCurve=IntentStoredNominalCurve(input.Path);
                if(!input.NominalCurve)input.Nominal=CaptureIntentProcessPreview(scene,input.Path,{{"kerf",Map{{"widthMm",0.}}},{"leadIn",none},{"leadOut",none}},selected);
            }
            catch(const std::exception& error){input.Error=error.what();}
        }
        inputs.push_back(std::move(input));
    }
    const auto cancelled=entry.Task.Cancelled;
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
    entry.Task.Worker=iCAX::Tasks::Run([inputs=std::move(inputs),service,cancelled,kind]() mutable
    {
        Array items;
        for(auto& input:inputs)
        {
            if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();
            if(kind=="Leads")
            {
                if(input.Resolved)input.Path=EvaluateIntentPreview(*input.Resolved,cancelled);
                items.emplace_back(IntentLeadPreview(*service,input.Path,cancelled));continue;
            }
            Map item{{"intentId",iCAX::Data::to_string(input.IDValue)},{"placementId",iCAX::Data::to_string(input.PlacementID)},
                {"widthMm",input.Width},{"parts",Array{}}};
            if(!input.Error.empty())item["error"]=input.Error;
            else if(input.Width>0&&input.Enabled)try
            {
                if(input.Resolved)input.Path=EvaluateIntentPreview(*input.Resolved,cancelled);
                const auto display=IntentLeadPreview(*service,input.Path,cancelled);Array parts;
                const auto processMetadata=ReadFiveAxisIntentMetadata(input.Path.Value);
                parts.emplace_back(Map{{"role",processMetadata.Feature.at("machiningKind")},{"points",display.at("cutPoints")}});
                for(const auto& lead:display.at("leads").To<Array>()){const auto data=lead.To<Map>();parts.emplace_back(Map{{"role",data.at("role")},{"points",data.at("points")}});}
                if(input.NominalCurve)item["nominalPoints"]=IntentPreviewPoints(*service,*input.NominalCurve,cancelled);
                else {const auto nominal=EvaluateIntentPreview(*input.Nominal,cancelled);item["nominalPoints"]=IntentPreviewPoints(*service,nominal,cancelled);}
                item["parts"]=std::move(parts);
                if(input.Path.Value.Channels.contains("fiveaxis.kerf"))item["compensation"]=input.Path.Value.Channels.at("fiveaxis.kerf");
            }
            catch(const iCAX::Tasks::TaskCanceledException&){throw;}
            catch(const std::exception& error){item["error"]=Text(error.what());}
            items.emplace_back(std::move(item));
        }
        auto result=std::make_shared<Map>(Map{{"frame",Text("Workpiece")},{"items",std::move(items)}});
        if(kind=="Leads")(*result)["scope"]=Text("ResolvedEditableResult");return result;
    });
    entry.DeferredRequest.clear();entry.WaitingPlans.clear();
}
}

Map BeginPathPreview(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentIds","kind","patch"});const auto runtime=Runtime(scene);CheckExpected(*runtime,request);
    const auto kind=String(request,"kind");if(kind!="Compensation"&&kind!="Leads")throw std::invalid_argument("CAM_PREVIEW_KIND_INVALID");
    const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    const auto patch=request.contains("patch")?request.at("patch").To<Map>():Map{};
    CutFields(patch,{"leadIn","leadOut","overcutLengthMm","gapLengthMm","start"});
    if((kind=="Compensation"&&!patch.empty())||(patch.contains("start")&&ids.size()!=1))throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    const auto key=iCAX::Data::VariantSerializer::Serialize(request);
    for(auto& [_,entry]:runtime->PathPreviews)
        if(entry.Sequence==runtime->Sequence&&entry.Key==key&&entry.Task.Status=="Completed"&&!entry.Task.Cancelled->load())return PathPreviewState(*runtime,entry);
    while(runtime->PathPreviews.size()>=8)
    {
        const auto old=std::find_if(runtime->PathPreviews.begin(),runtime->PathPreviews.end(),[&](const auto& item)
            {return item.second.Sequence!=runtime->Sequence||item.second.Task.Status!="Running"||item.second.Task.Cancelled->load();});
        if(old==runtime->PathPreviews.end())throw std::invalid_argument("CAM_PREVIEW_CAPACITY");
        old->second.Task.Cancelled->store(true);runtime->PathPreviews.erase(old);
    }
    SceneRuntime::PathPreview entry;entry.Sequence=runtime->Sequence;entry.Key=key;std::set<ID> selected;
    for(const auto& value:ids)
    {
        const auto id=CutID(value);if(!selected.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
        const auto& path=FindIntentPath(scene,id);
        if(kind=="Compensation"&&path.ResultState!="Current")
        {
            const auto plan=IntentOperationDefinition(scene,path.Port.OperationId).at("planId").To<ID>();
            if(runtime->IntentGenerations.contains(plan))entry.WaitingPlans.insert(plan);
        }
    }
    // A committed edit already owns a canonical background calculation.
    // Let status queries publish that result, then sample its sealed value;
    // starting another CRC calculation here duplicates the same exact work.
    if(entry.WaitingPlans.empty())StartPathPreview(scene,request,entry);else entry.DeferredRequest=request;
    const auto id=entry.Task.TaskID;auto inserted=runtime->PathPreviews.emplace(id,std::move(entry));
    return PathPreviewState(*runtime,inserted.first->second);
}

Map QueryPathPreview(Scene& scene,const Map& request)
{
    CutFields(request,{"epoch","taskId","release"});const auto runtime=Runtime(scene);
    if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_STALE_EPOCH");
    const auto id=String(request,"taskId");const auto found=runtime->PathPreviews.find(id);
    if(found==runtime->PathPreviews.end())throw std::invalid_argument("CAM_PREVIEW_NOT_FOUND");
    if(request.contains("release")&&request.at("release").To<bool>())
    {found->second.Task.Cancelled->store(true);runtime->PathPreviews.erase(found);return {{"status",Text("Cancelled")}};}
    auto& entry=found->second;
    if(!entry.DeferredRequest.empty()&&entry.Sequence==runtime->Sequence&&!entry.Task.Cancelled->load())
    {
        try
        {
            PumpIntentGenerations(scene);
            if(std::none_of(entry.WaitingPlans.begin(),entry.WaitingPlans.end(),[&](const ID& plan){return runtime->IntentGenerations.contains(plan);}))
            {const auto pending=entry.DeferredRequest;StartPathPreview(scene,pending,entry);}
        }
        catch(const std::exception& error){entry.Task.Status="Failed";entry.Task.Error=error.what();entry.DeferredRequest.clear();entry.WaitingPlans.clear();}
    }
    return PathPreviewState(*runtime,found->second);
}
