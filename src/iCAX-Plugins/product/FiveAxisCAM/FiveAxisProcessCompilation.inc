// Technology preview compiles the same resolved exact result as execution.
// Geometry processes have already run in the document graph.
struct CompilationSnapshot
{
    GenerationSnapshot Geometry;
    iCAX::CAM::Intent::SResolvedPath Path;
};
namespace
{
    bool CurrentCompilation(const SceneRuntime& runtime,const ImportTask& task)
    {const auto latest=runtime.LatestCompilation.find(task.IntentID);return runtime.Sequence==task.Sequence&&latest!=runtime.LatestCompilation.end()&&latest->second==task.TaskID;}
    void PumpIntentCompilation(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try
        {
            if(task.Cancelled->load()){task.Status="Cancelled";return;}
            if(!CurrentCompilation(runtime,task)){task.Status="Superseded";return;}
            if(runtime.IntentGenerations.contains(IntentPlan(scene,task.SetupID)))return;
            const auto cut=ReadIntentCut(scene,task.IntentID);const auto assignment=cut->Projection.Metadata.Process;
            const auto library=EngineeringBody(assignment.at("libraryData").To<Map>(),"cam.process-library");
            if(String(ProcessReadiness(library),"status")=="Incomplete")throw std::invalid_argument("PROCESS_PROFILE_INCOMPLETE");
            const std::set<ID> selected{task.IntentID};CompilationSnapshot snapshot;
            snapshot.Geometry=CaptureGeneration(scene,task.SetupID,&selected);snapshot.Path=cut->Path;
            task.CompilationInput=std::make_shared<const CompilationSnapshot>(std::move(snapshot));
            const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
            task.CompilationWorker=iCAX::Tasks::Run([input=task.CompilationInput,offset,cancelled=task.Cancelled]
            {
                const auto generated=CalculateGeneration(input->Geometry,offset,cancelled);
                if(generated->Operations.size()!=1||generated->Curves.size()!=1)throw std::invalid_argument("CAM_PROCESS_RESULT_MISSING");
                const auto operation=generated->Operations.front().To<Map>();Array spans;
                namespace Walk=iCAX::GeometryServices::OffsetTraversal;
                Walk::Evaluator evaluator(*offset,[cancelled]{return cancelled->load();});
                for(std::size_t i=0;i<generated->Curves.front().Segments.size();++i)
                {
                    const auto& segment=input->Path.Value.Geometry.Segments.at(i);const auto& piece=generated->Curves.front().Segments[i];
                    const auto range=Walk::Range(piece);Walk::Evaluator::Piece measure(evaluator,piece);
                    spans.emplace_back(Map{{"id",segment.Id},{"role",segment.ConstructionRole.empty()?Text("Cut"):segment.ConstructionRole},{"segmentIndex",std::to_string(i)},
                        {"sourceRange",Map{{"first",range.First},{"last",range.Last},{"reversed",range.Reversed}}},{"lengthMm",measure.Length(Walk::Start(piece),Walk::End(piece))}});
                }
                iCAX::GeometryData::Persistence::Writer writer;writer.Write(std::uint32_t(2));writer.Write(generated->Curves.front());writer.Write(generated->RemovedLoops.front());
                Map result{{"schema",Text("cam.compiled-process-preview")},{"schemaVersion",Text("1")},{"intentId",iCAX::Data::to_string(input->Path.PathId)},
                    {"compiler",Text("icax.intent-execution-projection/1")},{"scope",Text("ResolvedEditableResult")},{"frame",Text("Workpiece")},
                    {"exactCurveData",EngineeringData({{"schema",Text("cam.exact-curve-data")},{"schemaVersion",Text("1")},{"bytes",EngineeringBytes(writer.Bytes)}})},
                    {"spans",spans},{"eventSequence",operation.at("events")},{"lengthMm",operation.at("lengthMm")},{"parts",operation.at("parts")},
                    {"remainingChecks",Array{Text("MachineMotion"),Text("ToolEnvelope"),Text("Technology")}},
                    {"motionState",Text("NotPlanned")},{"productionQualified",false}};
                return std::make_shared<Map>(std::move(result));
            });
        }
        catch(const std::exception& error){task.Status="Failed";task.Error=error.what();task.CompilationInput.reset();}
    }
    void FinishCompilation(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try
        {
            const auto result=task.CompilationWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(!CurrentCompilation(runtime,task))task.Status="Superseded";
            else {if(!result)throw std::runtime_error("CAM_PROCESS_RESULT_MISSING");task.CompiledModel=result;task.Result={{"intentId",iCAX::Data::to_string(task.IntentID)},{"processState",Text("LocalGeometryCompiled")},{"motionState",Text("NotPlanned")}};task.Status="Completed";}
        }
        catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
        catch(const std::exception& error){task.Status="Failed";task.Error=error.what();}
        task.CompilationWorker={};task.CompilationInput.reset();
    }
}
Map CompileCutProcess(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","intentId"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("CompileProcess:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    ImportTask task;task.Kind="ProcessCompile";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.IntentID=Identifier(request,"intentId");
    const auto cut=ReadIntentCut(scene,task.IntentID);task.SetupID=cut->GetSetupID();task.PlacementID=cut->GetPlacementID();
    const auto id=task.TaskID;runtime->LatestCompilation[task.IntentID]=id;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
Map QueryCompiledProcess(Scene& scene,const Map& request)
{
    CutFields(request,{"epoch","taskId"});const auto runtime=Runtime(scene);if(String(request,"epoch")!=runtime->Epoch)throw std::invalid_argument("CAM_EPOCH_CHANGED");
    const auto found=runtime->Tasks.find(String(request,"taskId"));if(found==runtime->Tasks.end()||found->second.Kind!="ProcessCompile")throw std::invalid_argument("CAM_PROCESS_TASK_NOT_FOUND");
    const auto& task=found->second;if(task.Status!="Completed"||!CurrentCompilation(*runtime,task)||!task.CompiledModel)throw std::invalid_argument("CAM_COMPILED_PROCESS_SUPERSEDED");return *task.CompiledModel;
}
