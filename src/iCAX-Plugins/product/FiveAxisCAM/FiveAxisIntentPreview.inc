namespace
{
    std::optional<iCAX::GeometryData::CompositeOffsetCurve3> IntentStoredNominalCurve(const iCAX::CAM::Intent::SResolvedPath& path)
    {
        using namespace iCAX::GeometryData;const auto& value=path.Value;
        if(path.ResultState!="Current"||value.Geometry.Meaning!="ToolCenter")return {};
        for(const auto* name:{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads"})if(!value.Channels.contains(name))return {};
        const auto& kerf=std::get<Map>(value.Channels.at("fiveaxis.kerf").m_Value);
        const auto& trace=std::get<Map>(value.Channels.at("fiveaxis.trace").m_Value);
        const auto& leads=std::get<Map>(value.Channels.at("fiveaxis.leads").m_Value);
        if(RegistrationNumber(kerf.at("trimmedJoints"))!=0||RegistrationNumber(kerf.at("addedConnectors"))!=0
            ||RegistrationNumber(trace.at("gapLengthMm"))!=0||RegistrationNumber(trace.at("overcutLengthMm"))!=0
            ||(trace.contains("startAdjusted")&&trace.at("startAdjusted").To<bool>())
            ||String(std::get<Map>(leads.at("leadIn").m_Value),"kind")!="None"||String(std::get<Map>(leads.at("leadOut").m_Value),"kind")!="None")return {};
        CompositeOffsetCurve3 nominal;nominal.Closed=value.Geometry.Closed;
        for(std::size_t i=0;i<value.Geometry.Segments.size();++i)
        {
            const auto& segment=value.Geometry.Segments[i];const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve);
            if(!offset||!segment.ConstructionRole.empty())return {};
            nominal.Segments.push_back({offset->Source,i});
        }
        return nominal;
    }
    std::optional<ID> IntentExistingBranchProcess(Scene&,iCAX::CAM::Intent::SPortRef);
    iCAX::CAM::Intent::SResolvedPath IntentBranchNominalInput(Scene&,const iCAX::CAM::Intent::SResolvedPath&);
    bool IntentBranchClosed(Scene&,const iCAX::CAM::Intent::SResolvedPath&,const iCAX::CAM::Intent::SResolvedPath&);
    struct IntentPreviewSnapshot final
    {
        std::shared_ptr<iCAX::CAM::Intent::CToolpathOperationRegistry> Registry;
        std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> Geometry;
        std::map<iCAX::CAM::Intent::SPortRef,iCAX::CAM::Intent::SFrozenPathInput> Sources;
        std::vector<iCAX::CAM::Intent::SFrozenToolpathPreviewNode> Nodes;
        iCAX::CAM::Intent::SResolvedPath Target;
    };
    IntentPreviewSnapshot CaptureIntentPreview(Scene& scene,iCAX::CAM::Intent::SResolvedPath target,const std::map<ID,Map>& parameters={},
        const std::vector<iCAX::CAM::Intent::SPortRef>& additionalPorts={},const ID& validatedSetup={})
    {
        using namespace iCAX::CAM::Intent;
        const auto runtime=Runtime(scene);IntentPreviewSnapshot snapshot;
        snapshot.Registry=runtime->IntentRegistry;snapshot.Geometry=runtime->IntentGeometry;snapshot.Target=std::move(target);
        // The process-preview caller already validated this very frozen path's
        // engineering channel; reuse only its owner ID during the same call.
        const auto plan=IntentPlan(scene,validatedSetup.is_nil()?ReadIntentInspectionMetadata(scene,snapshot.Target).SetupID:validatedSetup);
        std::set<ID> active,visited;std::map<ID,bool> changed;
        // Validate the complete dependency graph, but replay only operations
        // affected by draft parameters. Current upstream results are sealed
        // values owned by this snapshot, never references to mutable Scene data.
        const auto inspect=[&](auto&& self,ID operation)->bool
        {
            if(changed.contains(operation))return changed.at(operation);
            if(!active.insert(operation).second)throw std::invalid_argument("CAM_INTENT_PREVIEW_GRAPH_CYCLE");
            const auto& definition=IntentOperationDefinition(scene,operation);const auto typeId=String(definition,"typeId");
            const auto type=snapshot.Registry->Find(typeId);
            if(!type||definition.at("schemaVersion").To<unsigned int>()!=type->SchemaVersion||String(definition,"algorithmVersion")!=type->AlgorithmVersion)
                throw std::invalid_argument("CAM_INTENT_PREVIEW_OPERATION_VERSION_MISMATCH");
            if(!definition.at("enabled").To<bool>())throw std::invalid_argument("CAM_INTENT_PREVIEW_OPERATION_DISABLED");
            bool affected=parameters.contains(operation);
            for(const auto& value:definition.at("inputs").To<Array>())
                affected=self(self,PortFromData(value.To<Map>().at("source").To<Map>()).OperationId)||affected;
            active.erase(operation);changed[operation]=affected;return affected;
        };
        inspect(inspect,snapshot.Target.Port.OperationId);
        for(const auto& port:additionalPorts)inspect(inspect,port.OperationId);
        const auto walk=[&](auto&& self,SPortRef port)->void
        {
            const auto operation=port.OperationId;
            if(snapshot.Sources.contains(port)||visited.contains(operation))return;
            if(!changed.at(operation))try
            {
                const auto& path=port==snapshot.Target.Port?snapshot.Target:ReadIntentPort(scene,plan,port);
                if(path.ResultState=="Current")
                {snapshot.Sources[port]={{"source",port,false,{}},path.PathId,path.Value,path.Fingerprint};return;}
            }
            catch(const std::invalid_argument& error)
            {if(Text(error.what())!="TOOLPATH_PATH_NOT_CURRENT")throw;}
            const auto& definition=IntentOperationDefinition(scene,operation);const auto typeId=String(definition,"typeId");
            const auto type=snapshot.Registry->Find(typeId);
            if(typeId=="icax.source")
            {
                const auto source=SourceToolpathPort(operation);const auto& path=ReadIntentPort(scene,plan,source);
                snapshot.Sources[source]={{"source",source,false,{}},path.PathId,path.Value,path.Fingerprint};
            }
            else
            {
                SFrozenToolpathPreviewNode node;node.TypeId=typeId;auto& input=node.Evaluation;input.OperationId=operation;
                input.Parameters=parameters.contains(operation)?parameters.at(operation):definition.at("parameters").To<Map>();input.Context=definition.at("context").To<Map>();
                for(const auto& value:definition.at("inputs").To<Array>())
                {
                    const auto data=value.To<Map>();SOperationInput binding;binding.Slot=String(data,"slot");binding.Source=PortFromData(data.at("source").To<Map>());binding.Replace=data.at("replace").To<bool>();
                    if(data.contains("ranges"))for(const auto& range:data.at("ranges").To<Array>())binding.Ranges.push_back(RangeFromData(range.To<Map>()));
                    self(self,binding.Source);input.Inputs.push_back({binding,{},{},{}});
                }
                if(type->FreezeContext)input.RuntimeContext=type->FreezeContext(input.Context);
                snapshot.Nodes.push_back(std::move(node));
            }
            visited.insert(operation);
        };
        walk(walk,snapshot.Target.Port);for(const auto& port:additionalPorts)walk(walk,port);return snapshot;
    }
    IntentPreviewSnapshot CaptureIntentPreview(Scene& scene,const ID& pathId,const std::map<ID,Map>& parameters={},
        const std::vector<iCAX::CAM::Intent::SPortRef>& additionalPorts={})
    {return CaptureIntentPreview(scene,ReadIntentPath(scene,pathId),parameters,additionalPorts);}
    iCAX::CAM::Intent::SResolvedPath EvaluateIntentPreview(const IntentPreviewSnapshot& snapshot,const std::shared_ptr<std::atomic_bool>& cancelled={})
    {
        using namespace iCAX::CAM::Intent;
        auto values=EvaluateFrozenToolpathGraph(*snapshot.Registry,*snapshot.Geometry,snapshot.Sources,snapshot.Nodes,cancelled);
        const auto selected=values.find(snapshot.Target.Port);if(selected==values.end())throw std::invalid_argument("CAM_INTENT_PREVIEW_TARGET_NEEDS_REBIND");
        auto path=snapshot.Target;path.Value=std::move(selected->second.Value);path.Fingerprint=selected->second.Fingerprint;path.ResultState="Preview";return path;
    }
    IntentPreviewSnapshot CaptureIntentProcessPreview(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,
        const Map& patch,const std::set<ID>& selected)
    {
        using namespace iCAX::CAM::Intent;
        // Failed process results intentionally have no value. Read the authored
        // policy, then evaluate the same graph with draft parameters so the
        // editor can repair the operation which failed.
        auto metadata=ReadIntentInspectionMetadata(scene,path);auto assignment=std::move(metadata.Process);Map start;
        const auto& feature=metadata.Feature;
        if(String(feature,"machiningKind")!="Cut")for(const auto* key:{"leadIn","leadOut"})
            if(patch.contains(key)&&String(patch.at(key).To<Map>(),"kind")!="None")throw std::invalid_argument("CAM_LEADS_REQUIRE_CUTTING_PATH");
        if(path.ResultState!="Current")assignment=IntentAuthoredProcessForRecovery(scene,path,std::move(assignment));
        for(const auto& [key,value]:patch)
        {
            if(key=="start")
            {
                start=value.To<Map>();const auto anchor=AnchorFromData(start);const auto plan=IntentPlan(scene,metadata.SetupID);
                (void)IntentAnchor(IntentDocument(scene).ReadPath(plan,anchor.Port),start);
                const auto design=IntentDesignPath(scene,path);(void)IntentDocument(scene).Preview("icax.edit.start",{{"anchor",ToData(anchor)}},{design.Port});
            }
            else if(key=="kerf"){auto policy=assignment.at(key).To<Map>();for(const auto& [field,item]:value.To<Map>())policy[field]=item;assignment[key]=std::move(policy);}
            else assignment[key]=value;
        }
        ValidateAssignmentValues(assignment,EngineeringBody(assignment.at("libraryData").To<Map>(),"cam.process-library"));
        const auto branch=IntentExistingBranchProcess(scene,path.Port);
        if(!branch&&(path.ResultState!="Current"||IntentDesignPort(scene,path)!=path.Port))
        {
            std::map<ID,Map> overrides;
            for(const auto& type:std::vector<Text>{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads"})
            {
                const auto operation=IntentProcessingOperation(scene,path.Port,type);const auto previous=IntentOperationDefinition(scene,operation).at("parameters").To<Map>();auto parameters=previous;
                if(type=="fiveaxis.kerf")parameters["policy"]=assignment.at("kerf");
                else if(type=="fiveaxis.trace"){parameters["gapLengthMm"]=assignment.at("gapLengthMm");parameters["overcutLengthMm"]=assignment.at("overcutLengthMm");if(!start.empty())parameters["start"]=start;}
                else {parameters["leadIn"]=assignment.at("leadIn");parameters["leadOut"]=assignment.at("leadOut");}
                if(parameters!=previous){RequireIntentOperationEditScope(scene,operation,selected);overrides[operation]=std::move(parameters);}
            }
            return CaptureIntentPreview(scene,path,overrides,{},metadata.SetupID);
        }
        bool exclusive=false;
        if(branch)
        {
            try{RequireIntentOperationEditScope(scene,*branch,{path.PathId});exclusive=true;}
            catch(const std::invalid_argument& error){if(Text(error.what())!="CAM_INTENT_SHARED_OPERATION_SELECT_ALL_RESULTS")throw;}
        }
        if(exclusive)
        {
            auto parameters=IntentOperationDefinition(scene,*branch).at("parameters").To<Map>();
            for(const auto* key:{"kerf","gapLengthMm","overcutLengthMm","leadIn","leadOut"})parameters[key]=assignment.at(key);
            if(!start.empty())parameters["start"]=start;
            return CaptureIntentPreview(scene,path,{{*branch,std::move(parameters)}},{},metadata.SetupID);
        }
        const auto nominal=IntentBranchNominalInput(scene,path);auto snapshot=CaptureIntentPreview(scene,path,{}, {nominal.Port},metadata.SetupID);
        SFrozenToolpathPreviewNode node;node.TypeId="fiveaxis.branch.process";auto& input=node.Evaluation;input.OperationId=iCAX::Data::GenerateNewUUID();
        input.Parameters={{"kerf",assignment.at("kerf")},{"gapLengthMm",assignment.at("gapLengthMm")},{"overcutLengthMm",assignment.at("overcutLengthMm")},
            {"leadIn",assignment.at("leadIn")},{"leadOut",assignment.at("leadOut")},{"closed",IntentBranchClosed(scene,path,nominal)}};
        if(!start.empty())input.Parameters["start"]=start;
        input.Context={{"sourceOperationId",IntentSourceOperation(scene,path.Port)}};
        input.Inputs={{{"selection",path.Port,true,{}},{},{},{}},{{"nominal",nominal.Port,false,{}},{},{},{}}};
        const auto type=snapshot.Registry->Find(node.TypeId);if(!type||!type->FreezeContext)throw std::invalid_argument("CAM_INTENT_PREVIEW_BRANCH_ALGORITHM_MISSING");
        input.RuntimeContext=type->FreezeContext(input.Context);snapshot.Target.Port=DeclaredToolpathPort(input.OperationId,"main");snapshot.Nodes.push_back(std::move(node));return snapshot;
    }
    Map IntentLeadPreview(const iCAX::GeometryServices::ICurveOffset3DService& service,const iCAX::CAM::Intent::SResolvedPath& path,
        const std::shared_ptr<std::atomic_bool>& cancelled={})
    {
        namespace Walk=iCAX::GeometryServices::OffsetTraversal;
        const auto metadata=ReadFiveAxisIntentMetadata(path.Value);Walk::Evaluator evaluator(service,[cancelled]{return cancelled&&cancelled->load();});
        const auto sampled=evaluator.SamplePoints(IntentExactCurve(path.Value));Array cutPoints,leads;Map lead;Text previous;
        const auto flush=[&]{if(!lead.empty()){const auto points=lead.at("points").To<Array>();if(!points.empty())lead["anchor"]=lead.at("role").To<Text>()=="LeadIn"?points.back():points.front();leads.emplace_back(std::move(lead));lead=Map{};}};
        for(const auto& sample:sampled.Points)
        {
            const auto& role=path.Value.Geometry.Segments[sample.PieceIndex].ConstructionRole;const auto p=sample.Value.OffsetPoint;const Array point{p.X,p.Y,p.Z};
            if(role=="LeadIn"||role=="LeadOut")
            {if(previous!=role){flush();previous=role;const auto policy=metadata.Process.at(role=="LeadIn"?"leadIn":"leadOut").To<Map>();lead={{"role",role},{"kind",policy.at("kind")},{"lengthMm",policy.at("lengthMm")},{"points",Array{}}};}std::get<Array>(lead.at("points").m_Value).emplace_back(point);}
            else {flush();previous.clear();cutPoints.emplace_back(point);}
        }
        flush();const auto gap=RegistrationNumber(metadata.Process.at("gapLengthMm")),overcut=RegistrationNumber(metadata.Process.at("overcutLengthMm"));
        return {{"intentId",iCAX::Data::to_string(path.PathId)},{"placementId",iCAX::Data::to_string(metadata.PlacementID)},
            {"start",cutPoints.empty()?Array{}:cutPoints.front().To<Array>()},{"end",cutPoints.empty()?Array{}:cutPoints.back().To<Array>()},
            {"closure",Map{{"kind",Text(gap>0?"Gap":overcut>0?"Overcut":"Normal")},{"lengthMm",std::max(gap,overcut)},{"points",Array{}}}},
            {"cutPoints",cutPoints},{"leads",leads}};
    }
    Map IntentLeadPreview(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {return IntentLeadPreview(*scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),path);}
}
