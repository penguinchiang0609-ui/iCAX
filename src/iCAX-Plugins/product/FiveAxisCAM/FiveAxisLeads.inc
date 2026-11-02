Map PreviewCutLeads(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentIds","patch"});CheckExpected(*Runtime(scene),request);
    const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    const auto patch=request.contains("patch")?request.at("patch").To<Map>():Map{};CutFields(patch,{"leadIn","leadOut","overcutLengthMm","gapLengthMm","start"});
    if(patch.contains("start")&&ids.size()!=1)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    Array items;std::set<ID> unique;std::vector<ID> paths;
    for(const auto& value:ids){const auto id=CutID(value);if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");paths.push_back(id);}
    for(const auto& id:paths)
    {
        auto path=ReadIntentPath(scene,id);
        if(!patch.empty())path=EvaluateIntentPreview(CaptureIntentProcessPreview(scene,path,patch,unique));
        else if(path.ResultState!="Current")path=EvaluateIntentPreview(CaptureIntentPreview(scene,id));
        items.emplace_back(IntentLeadPreview(scene,path));
    }
    return {{"frame",Text("Workpiece")},{"scope",Text("ResolvedEditableResult")},{"items",items}};
}
