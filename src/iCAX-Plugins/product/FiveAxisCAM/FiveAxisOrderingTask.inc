// Capture on Scene, calculate on a worker, then commit on Scene. Neither the
// geometry sampler nor the ordering search may hold the reliable SDO channel.
namespace
{
    void FinishOrdering(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try{
            const auto candidate=task.OrderWorker.Result();
            if(task.Sequence!=runtime.Sequence)task.Status="Superseded";
            else if(task.Cancelled->load())task.Status="Cancelled";
            else{
                if(!candidate)throw std::runtime_error("CAM_ORDER_RESULT_MISSING");
                if(task.OrderRequest.contains("preview")&&task.OrderRequest.at("preview").To<bool>())task.Result=*candidate;
                else task.Result=CommitToolpathOrder(scene,task.OrderRequest,candidate->at("definitionOrder").To<Array>(),candidate->at("optimization").To<Map>());
                task.Progress->store(100);task.Status="Completed";
            }
        }catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
        catch(const std::exception& error){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=error.what();}
        task.OrderWorker={};
    }
}
Map BeginToolpathOrder(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","mode","strategy","options","intentIds","fromIntentId","preview"});
    if(String(request,"mode")!="Automatic")throw std::invalid_argument("CAM_ORDER_MODE_INVALID");
    const auto strategy=OrderStrategy(request);const auto options=OrderOptions(request);
    if(request.contains("preview")&&!request.at("preview").Is<bool>())throw std::invalid_argument("CAM_ORDER_OPTION_INVALID");
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("BeginToolpathOrder:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){
        if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto setupID=Identifier(request,"setupId");RequireSetupUnlocked(scene,setupID);RequireSetupWorkpieceEnabled(scene,setupID);
    for(const auto& [_,task]:runtime->Tasks)if(task.Status=="Running"&&task.SetupID==setupID&&(task.Kind=="Order"||task.Kind=="Generate"))throw std::invalid_argument("CAM_GENERATE_ALREADY_RUNNING");
    const auto current=CutTreeOrder(scene,setupID);auto solver=CaptureOrderingSolver(scene,setupID,current);
    ConfigureOrderSolver(solver,request,options);
    if(std::none_of(solver.Cuts.begin(),solver.Cuts.end(),[](const auto& cut){return cut.Enabled;}))throw std::invalid_argument("CAM_ORDER_NO_CUTS");
    auto paths=CaptureOrderEstimate(scene,solver);
    ImportTask task;task.Kind="Order";task.CommandID=command;task.Payload=payload;task.SetupID=setupID;task.Sequence=runtime->Sequence;task.OrderRequest=request;
    const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
    task.OrderWorker=iCAX::Tasks::Run([solver=std::move(solver),paths=std::move(paths),request,offset,cancelled=task.Cancelled,progress=task.Progress]() mutable {
        progress->store(5);
        auto result=CalculateOrderEstimate(std::move(solver),paths,request,offset,cancelled);progress->store(95);
        return std::make_shared<Map>(std::move(result));
    });
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
