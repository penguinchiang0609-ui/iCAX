// Scene-owned orchestration. Geometry services receive immutable neutral values;
// no worker accesses the repository or publishes resources.
struct GenerationCut
{
    iCAX::CAM::Intent::SResolvedPath IntentPath;
    SFiveAxisIntentMetadata Metadata;
    std::shared_ptr<const CFiveAxisIntentOperationContext> Context;
    Map Assignment;
    std::vector<Ref> Dependencies;
    Text IDText,PlacementID,Role,BeforeCommands,AfterCommands;
    Array Transform;
    UInt Number=0;
};
struct GenerationSnapshot
{
    std::vector<GenerationCut> Cuts;
    Array ReusedOperations;
    Map SimulationStartFrames;
    std::vector<Ref> Dependencies;
    Text InputKey;
};
struct GeneratedGeometry
{
    Array Operations,Transitions;
    std::vector<iCAX::GeometryData::CompositeOffsetCurve3> Curves;
    std::vector<std::vector<iCAX::GeometryData::CompositeOffsetCurve3>> RemovedLoops;
};
namespace
{
    Map CaptureGeneratedSimulationStartFrames(Scene&,const ID&);
    Map GeneratedDataRevision(const CFiveAxisSetupComponent& setup){return setup.GetGeneratedToolpaths().empty()?Map{}:setup.GetGeneratedToolpaths().at("revision").To<Map>();}
    Map RapidDataRevision(const CFiveAxisSetupComponent& setup){return setup.GetGeneratedRapidPaths().empty()?Map{}:setup.GetGeneratedRapidPaths().at("revision").To<Map>();}
    Map NewMachiningDataRevision(){return {{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())}};}
    Map ReadGeneratedToolpathData(Scene& scene,const ID& setupID)
    {
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        return setup->GetGeneratedToolpaths();
    }
    void StoreGeneratedToolpathData(auto& tx,Scene&,const ID& setupID,Map data,const Map& revision)
    {
        data.erase("transitions");data["revision"]=revision;
        tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"GeneratedToolpaths",std::move(data)}});
    }
    void StoreGeneratedRapidData(auto& tx,const ID& setupID,const Map& entries,const Map& revision){
        tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"GeneratedRapidPaths",Map{{"entries",entries},{"revision",revision}}}});
    }
    void AppendGeneratedDataDependencies(Scene& scene,const ID& setupID,std::vector<Ref>& dependencies)
    {
        const auto data=ReadGeneratedToolpathData(scene,setupID);if(!data.contains("operations"))return;
        for(const auto& value:data.at("operations").To<Array>())
        {const auto path=ReadIntentPath(scene,Identifier(value.To<Map>(),"intentId"));dependencies.push_back(ReadWireRef(WireRef(ReadFiveAxisIntentMetadata(path.Value).SourceGeometryRef)));}
    }
    Map OperationInSetup(Map op,const Array& transform)
    {
        const auto m=RigidMatrix(transform);
        for(const auto* key:{"start","end"}){const auto p=RegistrationPoint(op.at(key));Array q;
            for(int i=0;i<3;++i){double v=m[i*4+3];for(int j=0;j<3;++j)v+=m[i*4+j]*p[j];q.emplace_back(v);}
            op[Text(key)=="start"?"setupStart":"setupEnd"]=q;}
        if(op.contains("parts")){
            const auto& parts=std::get<Array>(op.at("parts").m_Value);
            std::array<double,3> lo{},hi{};bool first=true;
            for(const auto& partValue:parts){const auto& part=std::get<Map>(partValue.m_Value);if(String(part,"role")!="Cut")continue;
                for(const auto& point:std::get<Array>(part.at("points").m_Value)){const auto p=RegistrationPoint(point);std::array<double,3> q{};for(int i=0;i<3;++i){q[i]=m[i*4+3];for(int j=0;j<3;++j)q[i]+=m[i*4+j]*p[j];}
                    if(first){lo=hi=q;first=false;}else for(int i=0;i<3;++i){lo[i]=std::min(lo[i],q[i]);hi[i]=std::max(hi[i],q[i]);}}}
            if(!first)op["setupCenter"]=Array{(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,(lo[2]+hi[2])/2};
            for(bool start:{true,false}){const auto& part=std::get<Map>((start?parts.front():parts.back()).m_Value);const auto& directions=std::get<Array>(part.at("directions").m_Value);
                const auto p=RegistrationPoint(start?directions.front():directions.back());Array q;
                for(int i=0;i<3;++i){double v=0;for(int j=0;j<3;++j)v+=m[i*4+j]*p[j];q.emplace_back(v);}
                op[start?"setupStartDirection":"setupEndDirection"]=q;
            }
        }
        return op;
    }
    Map RapidSettings(const CFiveAxisSetupComponent&);
    Map DefaultRapidPolicy(const Map&);
    Text OrderStrategy(const Map& request)
    {
        const auto strategy=request.contains("strategy")?String(request,"strategy"):Text("MarkHoleTrim");
        if(strategy!="MarkHoleTrim"&&strategy!="MarkAndHoleTrim")throw std::invalid_argument("CAM_ORDER_STRATEGY_INVALID");
        return strategy;
    }
    Ordering::Solver CaptureOrderingSolver(Scene& scene,const ID& setupID,const Array& current)
    {
        Ordering::Solver solver;solver.Nodes.push_back({});std::map<Text,std::size_t> groupNodes;
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto settings=RapidSettings(*setup);
        solver.Settings={RegistrationNumber(settings.at("linearSpeedMmS")),RegistrationNumber(settings.at("linearAccelerationMmS2")),
            RegistrationNumber(settings.at("angularSpeedRadS")),RegistrationNumber(settings.at("angularAccelerationRadS2")),RegistrationNumber(settings.at("liftHeightMm")),true};
        for(const auto& value:current){const auto id=value.To<Text>();const auto& path=FindIntentPath(scene,CutID(value));
            const auto metadata=ReadIntentInspectionMetadata(scene,path);
            Ordering::Cut cut;cut.ID=id;cut.Number=IntentPathDisplayNumber(path);cut.Enabled=IntentPathEnabledForMetadata(scene,path,metadata);
            const auto leaf=solver.Cuts.size();solver.Cuts.push_back(cut);std::vector<std::size_t> ancestors{0};
            auto chain=CutGroupAncestors(scene,setupID,path.GroupId);std::reverse(chain.begin(),chain.end());std::size_t parent=0;
            for(const auto& group:chain){const auto key=String(group,"id");
                if(!groupNodes.contains(key)){const auto node=solver.Nodes.size();solver.Nodes.push_back({});groupNodes[key]=node;solver.Nodes[parent].Children.push_back(node);solver.Nodes[node].Locked=group.at("locked").To<bool>();}
                parent=groupNodes.at(key);ancestors.push_back(parent);}
            const auto node=solver.Nodes.size();solver.Nodes.push_back({{}, {leaf},path.Locked,path.Locked});solver.Nodes[parent].Children.push_back(node);
            for(auto ancestor:ancestors){solver.Nodes[ancestor].Leaves.push_back(leaf);if(solver.Nodes[node].Locked)solver.Nodes[ancestor].Anchored=true;}
        }
        return solver;
    }
    Array SolveMachiningOrder(Ordering::Solver& solver,const Array& operations,Map* report=nullptr)
    {
        std::map<Text,const Map*> byID;for(const auto& value:operations){const auto& op=std::get<Map>(value.m_Value);byID[String(op,"intentId")]=&op;}
        for(auto& cut:solver.Cuts){
            if(cut.Enabled){const auto& id=cut.ID;if(!byID.contains(id))throw std::invalid_argument("CAM_ORDER_GEOMETRY_INCOMPLETE");const auto& op=*byID.at(id);
                cut.Placement=String(op,"placementId");cut.Phase=String(op,"machiningKind")=="Mark"?0:String(op,"boundaryRole")=="OuterTrim"?2:1;
                cut.Start=RegistrationPoint(op.at("setupStart"));cut.End=RegistrationPoint(op.at("setupEnd"));
                if(op.contains("setupStartDirection"))cut.StartDirection=RegistrationPoint(op.at("setupStartDirection"));
                if(op.contains("setupEndDirection"))cut.EndDirection=RegistrationPoint(op.at("setupEndDirection"));}
            cut.Center=cut.Start;
            if(cut.Enabled){const auto& op=*byID.at(cut.ID);
                if(op.contains("setupCenter"))cut.Center=RegistrationPoint(op.at("setupCenter"));
                else {cut.Center={(cut.Start[0]+cut.End[0])/2,(cut.Start[1]+cut.End[1])/2,(cut.Start[2]+cut.End[2])/2};}}
        }
        solver.Prepare();const auto entry=solver.Cuts.size()+(solver.UseOrigin?1:0);const auto result=solver.Solve(0,entry,solver.Cuts.size());Array ids;for(auto i:result)ids.emplace_back(solver.Cuts[i].ID);
        if(report){std::vector<std::size_t> original;for(std::size_t i=0;i<solver.Cuts.size();++i)original.push_back(i);
            *report={{"algorithm",Text("HierarchicalMachiningDAGBeamRelocateV1")},{"endpointSource",Text("ExactProcessGeometry")},
                {"strategy",Text(solver.CombineMarkAndHole?"MarkAndHoleTrim":"MarkHoleTrim")},
                {"beforeCost",solver.Score(original,entry,solver.Cuts.size())},{"afterCost",solver.Score(result,entry,solver.Cuts.size())},
                {"costUnit",Text(solver.DistanceObjective?"mm":"s")},
                {"beforeEstimatedSeconds",solver.DistanceObjective?0.:solver.Score(original,entry,solver.Cuts.size())},{"afterEstimatedSeconds",solver.DistanceObjective?0.:solver.Score(result,entry,solver.Cuts.size())},
                {"expandedStates",std::to_string(solver.Expanded)},{"costScope",Text("TcpRetractAndToolDirectionEstimate")},{"clearanceValidation",Text("Unchecked")},{"machineValidation",Text("Unchecked")}};}
        return ids;
    }
    Array OptimizeMachiningOrder(Scene& scene,const ID& setupID,const Array& current,const Array& operations,Map* report=nullptr,const Text& strategy="MarkHoleTrim")
    {auto solver=CaptureOrderingSolver(scene,setupID,current);solver.CombineMarkAndHole=strategy=="MarkAndHoleTrim";return SolveMachiningOrder(solver,operations,report);}
    Array OrderOperations(Array operations,const std::vector<std::size_t>& order)
    {
        Array result;for(auto index:order){auto op=std::move(operations.at(index)).To<Map>();op["order"]=std::to_string(result.size()+1);result.emplace_back(std::move(op));}return result;
    }
    void QualifyOperationEndpoints(Scene& scene,Map& operation)
    {
        operation["startStatus"]=operation["endStatus"]=operation.at("status");
        if(String(operation,"status")=="Current")return;
        // A cut revision includes interior geometry and engineering metadata.
        // A connector depends only on the two actual endpoint poses.
        try{
            const auto path=ReadIntentPath(scene,Identifier(operation,"intentId"));
            if(path.ResultState!="Current"||path.Value.Geometry.Segments.empty())return;
            const auto context=FreezeIntentContext(scene,IntentSourceOperation(scene,path.Port));
            const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
            iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*service);
            CFiveAxisIntentPoseField field(*context,path.Value);
            const auto parts=operation.at("parts").To<Array>();
            const auto same=[](const Array& saved,const auto& current){return saved.size()==3&&std::abs(RegistrationNumber(saved[0])-current.X)<=1e-8&&std::abs(RegistrationNumber(saved[1])-current.Y)<=1e-8&&std::abs(RegistrationNumber(saved[2])-current.Z)<=1e-8;};
            for(const bool start:{true,false}){
                const auto index=start?std::size_t(0):path.Value.Geometry.Segments.size()-1;
                const auto& segment=path.Value.Geometry.Segments[index];iCAX::GeometryData::OffsetCurvePiece3 piece;
                if(const auto* curve=std::get_if<iCAX::GeometryData::CurveSegment3>(&segment.Curve))piece.Geometry=*curve;
                else if(const auto* offset=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve))piece.Geometry=*offset;
                else continue;
                const auto range=iCAX::GeometryServices::OffsetTraversal::Range(piece);
                const auto parameter=(start!=range.Reversed)?range.First:range.Last;
                const auto point=evaluator.At(piece,parameter).OffsetPoint;const auto axis=field.At(index,parameter);
                const auto directions=(start?parts.front():parts.back()).To<Map>().at("directions").To<Array>();
                if(same(operation.at(start?"start":"end").To<Array>(),point)&&same((start?directions.front():directions.back()).To<Array>(),axis))
                    operation[start?"startStatus":"endStatus"]=Text("Current");
            }
        }catch(const std::exception&){/* Unresolved endpoint geometry remains unqualified. */}
    }
    Array OperationTransitions(const Array& operations)
    {
        Array result;for(std::size_t i=1;i<operations.size();++i){const auto& from=std::get<Map>(operations[i-1].m_Value);const auto& to=std::get<Map>(operations[i].m_Value);
            result.emplace_back(Map{{"from",from.at("intentId")},{"to",to.at("intentId")},{"points",Array{from.at("setupEnd"),to.at("setupStart")}},{"frame",Text("Setup")},{"fromPlacementId",from.at("placementId")},{"toPlacementId",to.at("placementId")},
                {"endpointStatus",Text((from.contains("status")&&String(from,from.contains("endStatus")?"endStatus":"status")!="Current")||(to.contains("status")&&String(to,to.contains("startStatus")?"startStatus":"status")!="Current")?"Stale":"Current")},
                {"laser",false},{"strategy",Text("Direct")},{"validation",Text("Unchecked")}});}return result;
    }
    Text GenerationInputKey(Scene& scene,const ID& setupID,const Map* poseReferences=nullptr,std::optional<UInt> revision={})
    {
        if(poseReferences)throw std::invalid_argument("CAM_INTENT_PREVIEW_REQUIRES_GRAPH_PREVIEW");
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);std::map<Text,Variant> ordered;
        const auto& resolved=ResolveIntentPlan(scene,setupID);
        for(const auto& path:IntentPaths(scene,setupID))
        {const auto metadata=ReadIntentInspectionMetadata(scene,path);ordered[iCAX::Data::to_string(path.PathId)]=Map{{"fingerprint",path.Fingerprint},{"enabled",path.Participating},{"state",path.ResultState},{"beforeCommands",metadata.BeforeCommands},{"afterCommands",metadata.AfterCommands}};}
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
        {
            const auto id=iCAX::Data::to_string(entity->GetID());
            if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setupID)
            {const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());ordered[id]=Map{{"transform",p->GetTransform()},{"registration",EngineeringSignature(p->GetRegistrationData())},{"geometry",w->GetGeometryRef()}};}
            if(const auto f=Component<CFiveAxisFixtureComponent>(entity);f&&f->GetSetupID()==setupID)ordered[id]=Map{{"transform",f->GetTransform()},{"geometry",f->GetGeometryRef()},{"collision",(f->GetEnabled()&&f->GetCollisionEnabled())}};
        }
        Map inputs;for(const auto& [k,v]:ordered)inputs[k]=v;
        return iCAX::Data::VariantSerializer::Serialize(Map{{"setupId",iCAX::Data::to_string(setupID)},{"algorithm",Text("intent-execution-projection/1")},{"poseField",Text("intent-pose-field/1")},
            {"revision",std::to_string(revision.value_or(setup->GetInputRevision()))},{"profile",EngineeringSignature(setup->GetPlanningProfileData())},{"planFingerprint",resolved.Fingerprint},{"inputs",inputs}});
    }
    // A setup revision fences asynchronous work; it is not a dependency of
    // every exact cut. Recover per-cut inputs from existing manifests too.
    Map GenerationOperationInputs(const Map& key,const Map& operation)
    {
        const auto& inputs=std::get<Map>(key.at("inputs").m_Value);const auto id=String(operation,"intentId"),placement=String(operation,"placementId");
        if(!inputs.contains(id)||!inputs.contains(placement))return {};
        return {{"algorithm",key.at("algorithm")},{"poseField",key.at("poseField")},{"profile",key.at("profile")},
            {"cut",inputs.at(id)},{"placement",inputs.at(placement)}};
    }
    Map GeneratedRapidEntries(Scene& scene,const ID& setupID);
    Map GeneratedGeometryBaseState(Scene& scene,const ID& setupID)
    {
        if(!SetupWorkpieceEnabled(scene,setupID))return {{"status",Text("Disabled")},{"operations",Array()},{"transitions",Array()}};
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);auto result=ReadGeneratedToolpathData(scene,setupID);
        if(result.empty()){
            const auto ordering=IntentCutOrderPolicy(scene,setupID);
            return {{"status",Text("Missing")},{"ordering",ordering.empty()?Map{{"mode",Text("Automatic")}}:ordering}};
        }
        const auto currentKey=GenerationInputKey(scene,setupID);
        const auto currentInputs=iCAX::Data::VariantSerializer::Deserialize(currentKey).To<Map>();
        const auto previousInputs=iCAX::Data::VariantSerializer::Deserialize(String(result,"inputKey")).To<Map>();
        // Execution constraints order unchanged exact paths. Compare geometry
        // inputs independently of the document's aggregate execution digest.
        auto currentGeometry=currentInputs,previousGeometry=previousInputs;
        currentGeometry.erase("planFingerprint");previousGeometry.erase("planFingerprint");
        result["status"]=Text(currentGeometry==previousGeometry?"Current":"Stale");
        const auto policy=IntentCutOrderPolicy(scene,setupID);
        auto operations=std::move(result.at("operations")).To<Array>();
        for(auto& value:operations){auto op=std::move(value).To<Map>();
            const auto previous=op.contains("executionInputs")?op.at("executionInputs").To<Map>():GenerationOperationInputs(previousInputs,op),current=GenerationOperationInputs(currentInputs,op);
            op["status"]=Text(!current.empty()&&current==previous?"Current":"Stale");
            if(String(op,"status")=="Stale")result["status"]=Text("Stale");
            QualifyOperationEndpoints(scene,op);
            const auto transform=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"))->GetTransform();value=OperationInSetup(std::move(op),transform);}
        // Index once: To<Map>() copies all sampled geometry. Avoid quadratic copies.
        std::map<Text,std::size_t> operationIndex;
        for(std::size_t i=0;i<operations.size();++i)
            operationIndex.emplace(String(std::get<Map>(operations[i].m_Value),"intentId"),i);
        std::vector<std::size_t> order;
        // Reading a scene must preserve its stored execution order. Cost
        // optimization belongs exclusively to the explicit SetOrder command.
        for(std::size_t i=0;i<operations.size();++i)order.push_back(i);
        if(!policy.empty()){
            std::vector<std::size_t> manual;std::set<std::size_t> used;
            // Existing paths retain their explicit order after regeneration.
            // Newly created paths append in the immutable generation order.
            for(const auto& id:policy.at("intentIds").To<Array>()){const auto found=operationIndex.find(id.To<Text>()); if(found!=operationIndex.end()&&used.insert(found->second).second)manual.push_back(found->second);}
            for(auto i:order)if(used.insert(i).second)manual.push_back(i);order=std::move(manual);
        }
        // Apply the common expanded constraints after cost optimization. This
        // preserves atomic derived blocks and precedence for execution too.
        const auto& effective=ResolveIntentPlan(scene,setupID);
        std::map<ID,iCAX::CAM::Intent::SResolvedPath> byPath;for(const auto& path:effective.Paths)byPath.emplace(path.PathId,path);
        std::vector<iCAX::CAM::Intent::SResolvedPath> ranked;std::set<std::size_t> constrained;
        for(auto index:order){const auto id=Identifier(operations[index].To<Map>(),"intentId");if(byPath.contains(id)){ranked.push_back(byPath.at(id));constrained.insert(index);}}
        // Pending local edits may temporarily omit their path from the resolved
        // projection. Do not move that path to the end while merely reading.
        if(ranked.size()==operations.size()&&!ranked.empty())
        {
            ranked=OrderFiveAxisIntentPaths(std::move(ranked),effective.ExecutionConstraints);std::vector<std::size_t> next;
            for(const auto& path:ranked)next.push_back(operationIndex.at(iCAX::Data::to_string(path.PathId)));
            for(auto index:order)if(!constrained.contains(index))next.push_back(index);order=std::move(next);
        }
        result["operations"]=OrderOperations(std::move(operations),order);
        result["transitions"]=OperationTransitions(std::get<Array>(result.at("operations").m_Value));
        result["ordering"]=policy.empty()?Map{{"mode",Text("Automatic")}}:policy;
        result.erase("inputKey");result["revision"]=GeneratedDataRevision(*setup);return result;
    }
    Map GeneratedGeometryState(Scene& scene,const ID& setupID){
        auto result=GeneratedGeometryBaseState(scene,setupID);
        if(result.contains("revision")&&result.contains("transitions"))ApplyRapidState(scene,setupID,result);
        return result;
    }
    Map GeneratedDependencyPatch(Scene& scene,const ID& setupID)
    {
        const auto generated=GeneratedGeometryState(scene,setupID);Map geometry{{"status",generated.at("status")}},operations,transitions;Array order;
        for(const auto* key:{"rapidClearCount","rapidGeneratedCount"})if(generated.contains(key))geometry[key]=generated.at(key);
        if(generated.contains("operations"))for(const auto& value:generated.at("operations").To<Array>()){const auto op=value.To<Map>();
            const auto id=String(op,"intentId");order.emplace_back(id);operations[id]=op.at("status");}
        if(generated.contains("transitions"))for(const auto& value:generated.at("transitions").To<Array>()){const auto t=value.To<Map>();Map state;
            for(const auto* key:{"inputKey","endpointStatus","geometryStatus","validation","reason","editingOnly","planningMethod","productionQualified","machineValidation"})if(t.contains(key))state[key]=t.at(key);
            transitions[String(t,"from")+"/"+String(t,"to")]=state;}
        return {{"geometry",geometry},{"operationStates",operations},{"transitionStates",transitions},{"operationOrder",order}};
    }
    GenerationSnapshot CaptureGeneration(Scene& scene,const ID& setupID,const std::set<ID>* selected=nullptr,bool incremental=false)
    {
        using namespace iCAX::CAM::Intent;
        RequireMachiningSetup(scene,setupID);const auto& resolved=ResolveIntentPlan(scene,setupID);
        if(resolved.Status!=EOutputCompleteness::Complete)throw std::invalid_argument("CAM_INTENT_PLAN_NOT_CURRENT");
        GenerationSnapshot out;out.InputKey=GenerationInputKey(scene,setupID);Map reusable,retained;const auto previous=ReadGeneratedToolpathData(scene,setupID);
        if(!previous.empty())out.SimulationStartFrames=CaptureGeneratedSimulationStartFrames(scene,setupID);
        if(incremental&&!previous.empty())
        {
            const auto oldInputs=iCAX::Data::VariantSerializer::Deserialize(String(previous,"inputKey")).To<Map>(),inputs=iCAX::Data::VariantSerializer::Deserialize(out.InputKey).To<Map>();
            for(const auto& value:previous.at("operations").To<Array>())
            {auto op=value.To<Map>();const auto current=GenerationOperationInputs(inputs,op),prior=op.contains("executionInputs")?op.at("executionInputs").To<Map>():GenerationOperationInputs(oldInputs,op);
                op["executionInputs"]=prior;op.erase("status");retained[String(op,"intentId")]=op;
                if(!current.empty()&&current==prior)reusable[String(op,"intentId")]=op;}
        }
        for(const auto& path:resolved.Paths)
        {
            if(selected&&!selected->contains(path.PathId)){
                if(incremental&&retained.contains(iCAX::Data::to_string(path.PathId)))out.ReusedOperations.push_back(retained.at(iCAX::Data::to_string(path.PathId)));
                continue;
            }auto metadata=ReadIntentInspectionMetadata(scene,path);if(!IntentPathEnabledForMetadata(scene,path,metadata))continue;
            const auto id=iCAX::Data::to_string(path.PathId);const auto number=IntentPathDisplayNumber(path);
            if(reusable.contains(id)){auto op=reusable.at(id).To<Map>();op["cutNumber"]=std::to_string(number);out.ReusedOperations.emplace_back(std::move(op));continue;}
            GenerationCut input;input.IntentPath=path;input.Metadata=std::move(metadata);input.Context=FreezeIntentContext(scene,IntentSourceOperation(scene,path.Port));
            input.IDText=id;input.PlacementID=iCAX::Data::to_string(input.Metadata.PlacementID);input.Transform=Require<CFiveAxisPlacementComponent>(scene.Database(),input.Metadata.PlacementID)->GetTransform();
            input.Number=number;input.BeforeCommands=input.Metadata.BeforeCommands;input.AfterCommands=input.Metadata.AfterCommands;input.Assignment=input.Metadata.Process;input.Role=String(input.Metadata.Feature,"boundaryRole");
            input.Dependencies={ReadWireRef(WireRef(input.Metadata.SourceGeometryRef))};out.Dependencies.insert(out.Dependencies.end(),input.Dependencies.begin(),input.Dependencies.end());out.Cuts.push_back(std::move(input));
        }
        if(out.Cuts.empty()&&out.ReusedOperations.empty()&&!resolved.IntentionalEmpty)throw std::invalid_argument("CAM_GENERATE_NO_CUTS");
        std::sort(out.Cuts.begin(),out.Cuts.end(),[](const auto& a,const auto& b){return a.Number!=b.Number?a.Number<b.Number:a.IDText<b.IDText;});return out;
    }
    Array GenPoint(const auto& p){return {p[0],p[1],p[2]};}
    std::array<double,3> GenVec(const auto& p){return {p.X,p.Y,p.Z};}
    #include "FiveAxisPositionOffsets.inc"
    std::shared_ptr<GeneratedGeometry> CalculateGeneration(const GenerationSnapshot& snapshot,
        const std::shared_ptr<iCAX::GeometryServices::ICurveOffset3DService>& offsetService,const std::shared_ptr<std::atomic_bool>& cancelled)
    {
        using namespace iCAX::GeometryData;namespace Walk=iCAX::GeometryServices::OffsetTraversal;
        auto out=std::make_shared<GeneratedGeometry>();const auto check=[&]{if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();};
        for(const auto& input:snapshot.Cuts)
        {
            check();const auto& value=input.IntentPath.Value;CompositeOffsetCurve3 exact;exact.Closed=value.Geometry.Closed;
            for(std::size_t i=0;i<value.Geometry.Segments.size();++i)
            {
                const auto& segment=value.Geometry.Segments[i];OffsetCurvePiece3 piece;
                if(const auto* curve=std::get_if<CurveSegment3>(&segment.Curve))piece.Geometry=*curve;
                else if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve))piece.Geometry=*offset;
                else throw std::invalid_argument("CAM_INTENT_EXECUTION_REQUIRES_3D_CURVE");
                if(segment.ConstructionRole.empty())piece.SourceSegment=i;exact.Segments.push_back(std::move(piece));
            }
            // Sampling feeds display/motion. Exact process expressions are
            // copied unchanged; this projection performs no geometry process.
            Walk::Evaluator evaluator(*offsetService,[cancelled]{return cancelled->load();});auto samples=evaluator.Sample(exact);
            if(samples.Points.size()<2)throw std::invalid_argument("CAM_GENERATE_EMPTY_INTENT");
            CFiveAxisIntentPoseField field(*input.Context,value);
            PositionOffsets positionOffsets(value,exact,evaluator);
            // Refinement revisits shared endpoints and quarter stations. Keep
            // exact directions within this immutable cut calculation only.
            std::vector<std::map<double,Direction3>> directions(exact.Segments.size());
            const auto directionAt=[&](std::size_t index,double parameter)
            {
                auto& cache=directions.at(index);const auto found=cache.find(parameter);if(found!=cache.end())return found->second;
                return cache.emplace(parameter,field.At(index,parameter)).first->second;
            };
            std::map<std::string,std::size_t> segmentIndices;std::vector<std::set<double>> stations(exact.Segments.size());
            for(std::size_t i=0;i<value.Geometry.Segments.size();++i)segmentIndices[value.Geometry.Segments[i].Id]=i;
            for(const auto& sample:samples.Points)stations[sample.PieceIndex].insert(sample.Value.SourceParameter);
            const auto includeAnchor=[&](const Map& data)
            {
                const auto anchor=iCAX::CAM::Intent::AnchorFromData(data);const auto found=segmentIndices.find(anchor.SegmentId);
                if(found==segmentIndices.end())throw std::invalid_argument("CAM_INTENT_POSE_ANCHOR_NEEDS_REBIND");
                const auto range=Walk::Range(exact.Segments[found->second]);
                if(anchor.Parameter<range.First-1e-12||anchor.Parameter>range.Last+1e-12)throw std::invalid_argument("CAM_INTENT_POSE_ANCHOR_NEEDS_REBIND");
                stations[found->second].insert(std::clamp(anchor.Parameter,range.First,range.Last));
            };
            for(const auto& [name,channelValue]:value.Channels)
            {
                if(name!="poseField"&&name!="poseControls")continue;const auto channel=channelValue.To<Map>();
                if(!channel.contains("entries"))continue;for(const auto& item:channel.at("entries").To<Array>())
                {const auto entry=item.To<Map>();if(entry.contains("anchor"))includeAnchor(entry.at("anchor").To<Map>());
                 if(entry.contains("range")){const auto range=entry.at("range").To<Map>();includeAnchor(range.at("start").To<Map>());includeAnchor(range.at("end").To<Map>());}}
            }
            // Pose angularToleranceRad limits permitted axis adjustment. Its
            // valid default of zero is independent of sample discretization.
            const double angularTolerance=.01;
            const auto angle=[](const auto& a,const auto& b){return std::atan2(std::hypot(a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X),a.X*b.X+a.Y*b.Y+a.Z*b.Z);};
            samples.Points.clear();std::size_t poseSamples=0;
            for(std::size_t i=0;i<stations.size();++i)
            {
                std::vector<double> ordered(stations[i].begin(),stations[i].end());if(Walk::Range(exact.Segments[i]).Reversed)std::reverse(ordered.begin(),ordered.end());
                const auto append=[&](double u){if(++poseSamples>1000000)throw std::invalid_argument("CAM_POSE_SAMPLING_BUDGET");samples.Points.push_back({i,evaluator.At(exact.Segments[i],u)});};
                append(ordered.front());
                const auto refine=[&](auto&& self,double a,double b,int depth)->void
                {
                    check();const auto m=(a+b)/2,q1=(3*a+b)/4,q3=(a+3*b)/4;
                    const auto da=directionAt(i,a),d1=directionAt(i,q1),dm=directionAt(i,m),d3=directionAt(i,q3),db=directionAt(i,b);
                    if(std::max({angle(da,db),angle(da,d1),angle(d1,dm),angle(dm,d3),angle(d3,db)})>angularTolerance)
                    {if(depth>=20)throw std::invalid_argument("CAM_POSE_SAMPLING_UNRESOLVED");self(self,a,m,depth+1);self(self,m,b,depth+1);}
                    else append(b);
                };
                for(std::size_t j=1;j<ordered.size();++j)refine(refine,ordered[j-1],ordered[j],0);
            }
            Array parts;Map part;Text currentRole;double length=0;std::optional<std::array<double,3>> previousPosition;
            const auto flush=[&]{if(!part.empty()){parts.emplace_back(std::move(part));part=Map{};}};
            for(const auto& sample:samples.Points)
            {
                check();const auto& segment=value.Geometry.Segments.at(sample.PieceIndex);const auto& station=sample.Value;
                const auto role=segment.ConstructionRole=="LeadIn"?Text("LeadIn"):segment.ConstructionRole=="LeadOut"?Text("LeadOut"):String(input.Metadata.Feature,"machiningKind");
                if(role!=currentRole){flush();currentRole=role;part={{"role",role},{"points",Array{}},{"directions",Array{}},{"sourceSamples",Array{}}};}
                const auto axis=directionAt(sample.PieceIndex,station.SourceParameter);
                const auto position=positionOffsets.Apply(GenVec(station.OffsetPoint),sample.PieceIndex,station.SourceParameter);
                if(previousPosition)length+=std::hypot(position[0]-(*previousPosition)[0],position[1]-(*previousPosition)[1],position[2]-(*previousPosition)[2]);
                previousPosition=position;std::get<Array>(part.at("points").m_Value).emplace_back(GenPoint(position));
                std::get<Array>(part.at("directions").m_Value).emplace_back(Array{axis.X,axis.Y,axis.Z});
                const iCAX::CAM::Intent::SPathAnchor anchor{input.IntentPath.Port,segment.Id,station.SourceParameter};
                std::get<Array>(part.at("sourceSamples").m_Value).emplace_back(Map{{"segmentIndex",std::to_string(sample.PieceIndex)},{"segmentId",segment.Id},{"nativeParameter",station.SourceParameter},
                    {"anchor",iCAX::CAM::Intent::ToData(anchor)},{"kind",segment.ConstructionRole.empty()?Text("IntentSource"):segment.ConstructionRole}});
            }
            flush();if(!positionOffsets.HasOffsets()){length=0;for(double n:samples.Lengths)length+=n;}
            Map op{{"intentId",input.IDText},{"intentFingerprint",input.IntentPath.Fingerprint},{"placementId",input.PlacementID},{"cutNumber",std::to_string(input.Number)},
                {"beforeCommands",input.BeforeCommands},{"afterCommands",input.AfterCommands},{"machiningKind",input.Metadata.Feature.at("machiningKind")},{"boundaryRole",input.Role},{"lengthMm",length},{"parts",parts},{"closed",value.Geometry.Closed},
                {"gapLengthMm",input.Assignment.at("gapLengthMm")},{"overcutLengthMm",input.Assignment.at("overcutLengthMm")},
                {"start",parts.front().To<Map>().at("points").To<Array>().front()},{"end",parts.back().To<Map>().at("points").To<Array>().back()},
                {"events",Array{Map{{"at",Text("Start")},{"laser",true}},Map{{"at",Text("End")},{"laser",false}}}}};
            if(const auto it=value.Channels.find("fiveaxis.kerf");it!=value.Channels.end())op["compensation"]=it->second;
            out->Operations.emplace_back(OperationInSetup(std::move(op),input.Transform));out->Curves.push_back(std::move(exact));out->RemovedLoops.emplace_back();
        }
        check();std::vector<std::size_t> order;for(std::size_t i=0;i<out->Operations.size();++i)order.push_back(i);
        out->Operations=OrderOperations(std::move(out->Operations),order);out->Transitions=OperationTransitions(out->Operations);return out;
    }
    void PumpIntentExecution(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try
        {
            if(task.Cancelled->load()){task.Status="Cancelled";return;}
            if(task.Sequence!=runtime.Sequence||runtime.LatestGeneration[task.SetupID]!=task.TaskID){task.Status="Superseded";return;}
            if(runtime.IntentGenerations.contains(IntentPlan(scene,task.SetupID)))return;
            std::set<ID> selected;if(!task.IntentID.is_nil())selected.insert(task.IntentID);
            task.GenerationInput=std::make_shared<const GenerationSnapshot>(CaptureGeneration(scene,task.SetupID,selected.empty()?nullptr:&selected,true));
            const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
            task.GenerationWorker=iCAX::Tasks::Run([input=task.GenerationInput,offset,cancelled=task.Cancelled]
                {return CalculateGeneration(*input,offset,cancelled);});
        }
        catch(const std::exception& error){task.Status="Failed";task.Error=error.what();task.GenerationInput.reset();}
    }
    void FinishGeneration(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        try{
            const auto result=task.GenerationWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence||runtime.LatestGeneration[task.SetupID]!=task.TaskID)task.Status="Superseded";
            else{
                if(!result)throw std::runtime_error("CAM_GENERATE_RESULT_MISSING");
                auto dependencies=task.GenerationInput->Dependencies;Array operations=task.GenerationInput->ReusedOperations;
                for(std::size_t i=0;i<result->Curves.size();++i){
                    iCAX::GeometryData::Persistence::Writer writer;writer.Write(std::uint32_t(2));writer.Write(result->Curves[i]);writer.Write(result->RemovedLoops[i]);
                    const auto curve=EngineeringData({{"schema",Text("cam.exact-curve-data")},{"schemaVersion",Text("1")},{"bytes",EngineeringBytes(writer.Bytes)}});
                    auto op=result->Operations[i].To<Map>();op["curveData"]=curve;
                    op["executionInputs"]=GenerationOperationInputs(iCAX::Data::VariantSerializer::Deserialize(task.GenerationInput->InputKey).To<Map>(),op);operations.emplace_back(op);
                }
                std::sort(operations.begin(),operations.end(),[](const auto& a,const auto& b){const auto x=a.template To<Map>(),y=b.template To<Map>();
                    const auto xn=Integer(x,"cutNumber"),yn=Integer(y,"cutNumber");return xn!=yn?xn<yn:String(x,"intentId")<String(y,"intentId");});
                std::vector<std::size_t> order;for(std::size_t i=0;i<operations.size();++i)order.push_back(i);operations=OrderOperations(std::move(operations),order);
                for(auto& value:operations){auto& op=std::get<Map>(value.m_Value);const auto id=String(op,"intentId");
                    if(task.GenerationInput->SimulationStartFrames.contains(id))op["simulationStartFrame"]=task.GenerationInput->SimulationStartFrames.at(id);}
                Map manifest{{"schema",Text("cam.generated-toolpath-geometry")},{"schemaVersion",Text("1")},{"exactCurveVersion",Text("2")},{"inputKey",task.GenerationInput->InputKey},
                    {"frame",Text("Workpiece")},{"operations",operations},{"transitions",OperationTransitions(operations)},
                    {"sampling",Map{{"chordToleranceMm",.02},{"maximumStepMm",2.}}},{"motionState",Text("NotPlanned")},{"collisionState",Text("Unchecked")},
                    {"productionQualified",false},{"remainingChecks",Array{Text("MachineMotion"),Text("TransitionClearance"),Text("ToolEnvelope"),Text("Technology"),Text("ProgramEntryExit")}}};
                const auto revision=NewMachiningDataRevision();const auto rapids=GeneratedRapidEntries(scene,task.SetupID);
                Transaction(scene.Database(),"生成工艺刀路",[&](auto& tx){StoreGeneratedToolpathData(tx,scene,task.SetupID,manifest,revision);
                    if(!rapids.empty())StoreGeneratedRapidData(tx,task.SetupID,rapids,RapidDataRevision(*Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID)));},false);
                task.Result={{"geometryRevision",revision},{"operationCount",std::to_string(operations.size())},{"calculatedCount",std::to_string(result->Operations.size())},{"reusedCount",std::to_string(task.GenerationInput->ReusedOperations.size())},{"motionState",Text("NotPlanned")}};task.Status="Completed";
            }
        }catch(const iCAX::Tasks::TaskCanceledException&){task.Status="Cancelled";}
        catch(const std::exception& error){task.Status="Failed";task.Error=error.what();}
        task.GenerationWorker={};task.GenerationInput.reset();
    }
}
Map PreviewCutCompensation(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentIds"});CheckExpected(*Runtime(scene),request);
    const auto ids=request.at("intentIds").To<Array>();
    if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    std::set<ID> unique;
    for(const auto& value:ids){const auto id=CutID(value);if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");ReadIntentCut(scene,id);}
    Array items;
    for(const auto& value:ids){
        const auto id=CutID(value);const auto feature=ReadIntentCut(scene,id);
        const auto assignment=EngineeringBody(feature->GetProcessData(),"cam.process-assignment");
        const double width=RegistrationNumber(assignment.at("kerf").To<Map>().at("widthMm"));
        Map item{{"intentId",value},{"placementId",iCAX::Data::to_string(feature->GetPlacementID())},{"widthMm",width},{"parts",Array{}}};
        if(width>0&&CutEnabled(scene,id))try{
            // Display the editable result itself. Visualization must not depend
            // on unrelated production feed/pierce or machine readiness checks.
            auto path=feature->Path;if(path.ResultState!="Current")path=EvaluateIntentPreview(CaptureIntentPreview(scene,id));
            const auto display=IntentLeadPreview(scene,path);Array parts;
            parts.emplace_back(Map{{"role",feature->Projection.Metadata.Feature.at("machiningKind")},{"points",display.at("cutPoints")}});
            for(const auto& lead:display.at("leads").To<Array>()){const auto data=lead.To<Map>();parts.emplace_back(Map{{"role",data.at("role")},{"points",data.at("points")}});}
            const Map none{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};
            const auto nominal=EvaluateIntentPreview(CaptureIntentProcessPreview(scene,path,{{"kerf",Map{{"widthMm",0.}}},{"leadIn",none},{"leadOut",none}},unique));
            item["nominalPoints"]=IntentPreviewPoints(scene,nominal);item["parts"]=std::move(parts);
            if(path.Value.Channels.contains("fiveaxis.kerf"))item["compensation"]=path.Value.Channels.at("fiveaxis.kerf");
        }catch(const std::exception& error){item["error"]=Text(error.what());}
        items.emplace_back(std::move(item));
    }
    // A read-only view of the same exact process geometry used by generation.
    // No resources, plan state, command receipts or undo entries are published.
    return {{"frame",Text("Workpiece")},{"items",items}};
}
Map GenerateToolpaths(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","intentId"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("Generate:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    ImportTask task;task.Kind="Generate";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");
    if(request.contains("intentId")){task.IntentID=Identifier(request,"intentId");const auto& path=FindIntentPath(scene,task.IntentID);if(ReadIntentInspectionMetadata(scene,path).SetupID!=task.SetupID)throw std::invalid_argument("CAM_COLLISION_PATH_SETUP_MISMATCH");}
    const auto latest=runtime->LatestGeneration.find(task.SetupID);
    if(latest!=runtime->LatestGeneration.end()&&runtime->Tasks.at(latest->second).Status=="Running")throw std::invalid_argument("CAM_GENERATE_ALREADY_RUNNING");
    // Revalidate current definitions and external contexts, then project the
    // current exact paths. A completed CRC must not be regenerated just because
    // the user asks for execution samples.
    const auto plan=IntentPlan(scene,task.SetupID);auto current=IntentDocument(scene).Resolve(plan);
    if(current.Status==iCAX::CAM::Intent::EOutputCompleteness::Complete)runtime->IntentResolutions[plan]=std::move(current);
    else if(!runtime->IntentGenerations.contains(plan))QueueIntentGeneration(scene,task.SetupID);
    const auto id=task.TaskID;runtime->LatestGeneration[task.SetupID]=id;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
#include "FiveAxisOrderingEstimate.inc"
namespace {
Map CommitToolpathOrder(Scene& scene,const Map& request,const Array& full,const Map& optimization)
{
    const auto mode=String(request,"mode");const auto setupID=Identifier(request,"setupId");
    const auto previousPolicy=IntentCutOrderPolicy(scene,setupID);
    const auto strategy=OrderStrategy(mode=="Automatic"?request:previousPolicy);
    const auto options=mode=="Automatic"?OrderOptions(request):OrderOptions(Map{{"options",previousPolicy.contains("options")?previousPolicy.at("options"):Variant(Map{})}});
    const Map policy{{"mode",mode},{"intentIds",full},{"strategy",strategy},{"options",options}};
    const bool changed=policy!=previousPolicy;
    if(changed)
    {
        using namespace iCAX::CAM::Intent;Array members;
        auto& structure=ReadOrderStructure(scene,setupID);
        for(const auto& id:full)members.emplace_back(ToData(structure.Paths.at(CutID(id)).Port));
        const auto plan=IntentPlan(scene,setupID);
        auto constraints=IntentDocument(scene).ReadPlanDefinition(plan).at("executionConstraints").To<Map>();
        auto blocks=constraints.contains("blocks")?constraints.at("blocks").To<Array>():Array{};
        std::erase_if(blocks,[](const auto& item){const auto id=String(item.To<Map>(),"id");return id=="scene.manual-order"||id=="scene.automatic-order";});
        blocks.emplace_back(Map{{"id",Text(mode=="Manual"?"scene.manual-order":"scene.automatic-order")},{"members",members},{"atomic",false},{"strategy",strategy},{"options",options}});
        constraints["blocks"]=std::move(blocks);
        Map orderCommand{{"kind",Text("SetExecutionConstraints")},{"commandId",Identifier(request,"commandId")},{"planId",plan},{"constraints",constraints}};
        IntentDocument(scene).Execute(orderCommand);
        // This transaction changes order only. Other mutations (including
        // history and group edits) fail the sequence fence and rebuild it.
        structure.Order=full;structure.Sequence=Runtime(scene)->Sequence;structure.Valid=true;
        // Pure ordering is already covered by the document's native undo; exact
        // geometry and its cached results remain reusable.
    }
    Map result{{"changed",changed},{"setupId",iCAX::Data::to_string(setupID)},{"ordering",policy.empty()?Map{{"mode",Text("Automatic")}}:policy}};
    if(mode=="Automatic")result["optimization"]=optimization;
    const auto runtime=Runtime(scene);const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    result["expected"]=Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}};
    result["definitionOrder"]=full;result["rapidDefaultPolicy"]=DefaultRapidPolicy(RapidSettings(*setup));
    Map policies;for(const auto& [pair,value]:setup->GetRapidOverrides()){
        const auto& record=std::get<Map>(value.m_Value);if(record.contains("policy"))policies[pair]=record.at("policy");}
    result["rapidPolicies"]=policies;
    return result;
}
}
Map QueryToolpathOrder(Scene& scene,const Map& request)
{
    const auto runtime=Runtime(scene);CheckExpected(*runtime,request);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto policy=IntentCutOrderPolicy(scene,setupID);
    Map policies;for(const auto& [pair,value]:setup->GetRapidOverrides()){
        const auto record=value.To<Map>();if(record.contains("policy"))policies[pair]=record.at("policy");}
    return {{"setupId",iCAX::Data::to_string(setupID)},{"expected",request.at("expected")},
        {"definitionOrder",CutTreeOrder(scene,setupID)},{"ordering",policy},{"rapidDefaultPolicy",DefaultRapidPolicy(RapidSettings(*setup))},{"rapidPolicies",policies}};
}
Map SetToolpathOrder(Scene& scene,const Map& request)
{
    const auto mode=String(request,"mode");
    if(mode=="Manual")CutFields(request,{"commandId","expected","setupId","mode","intentIds"});
    else if(mode=="Automatic"){CutFields(request,{"commandId","expected","setupId","mode","strategy","options","intentIds","fromIntentId","previewTaskId"});OrderStrategy(request);OrderOptions(request);}
    else throw std::invalid_argument("CAM_ORDER_MODE_INVALID");
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetToolpathOrder:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    RequireSetupUnlocked(scene,setupID);RequireSetupWorkpieceEnabled(scene,setupID);
    if(mode=="Automatic"&&request.contains("previewTaskId")){
        const auto found=runtime->Tasks.find(String(request,"previewTaskId"));
        if(found==runtime->Tasks.end()||found->second.Kind!="Order"||found->second.SetupID!=setupID||found->second.Status!="Completed"||found->second.Sequence!=runtime->Sequence
            ||!found->second.OrderRequest.contains("preview")||!found->second.OrderRequest.at("preview").To<bool>())throw std::invalid_argument("CAM_ORDER_PREVIEW_STALE");
        const auto& preview=found->second;
        if(OrderOptions(request)!=OrderOptions(preview.OrderRequest)||OrderStrategy(request)!=OrderStrategy(preview.OrderRequest))throw std::invalid_argument("CAM_ORDER_PREVIEW_STALE");
        for(const auto* key:{"intentIds","fromIntentId"})if(request.contains(key)!=preview.OrderRequest.contains(key)||(request.contains(key)&&request.at(key)!=preview.OrderRequest.at(key)))throw std::invalid_argument("CAM_ORDER_PREVIEW_STALE");
        auto result=CommitToolpathOrder(scene,request,preview.Result.at("definitionOrder").To<Array>(),preview.Result.at("optimization").To<Map>());
        runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
    }
    const auto latest=runtime->LatestGeneration.find(setupID);
    if(latest!=runtime->LatestGeneration.end()&&runtime->Tasks.at(latest->second).Status=="Running")throw std::invalid_argument("CAM_GENERATE_ALREADY_RUNNING");
    const auto current=CutTreeOrder(scene,setupID);Array enabled;
    for(const auto& id:current)if(CutEnabled(scene,CutID(id)))enabled.push_back(id);
    if(enabled.empty())throw std::invalid_argument("CAM_ORDER_NO_CUTS");
    Array desired;Map optimization;
    if(mode=="Manual"){
        const auto ids=request.at("intentIds").To<Array>();std::set<Text> available;
        for(const auto& id:enabled)available.insert(id.To<Text>());
        if(ids.size()!=available.size())throw std::invalid_argument("CAM_ORDER_PERMUTATION_REQUIRED");
        for(const auto& id:ids)if(!available.erase(id.To<Text>()))throw std::invalid_argument("CAM_ORDER_PERMUTATION_REQUIRED");
        desired=ids;
    }
    else {
        const auto options=OrderOptions(request);
        auto solver=CaptureOrderingSolver(scene,setupID,current);ConfigureOrderSolver(solver,request,options);
        const auto paths=CaptureOrderEstimate(scene,solver);
        const auto candidate=CalculateOrderEstimate(std::move(solver),paths,request,
            scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),std::make_shared<std::atomic_bool>(false));
        desired=candidate.at("definitionOrder").To<Array>();optimization=candidate.at("optimization").To<Map>();
    }
    const auto full=mode=="Manual"?ConstrainedCutOrder(scene,current,desired):desired;Array orderedEnabled;
    for(const auto& id:full)if(CutEnabled(scene,CutID(id)))orderedEnabled.push_back(id);
    if(mode=="Manual"&&orderedEnabled!=desired)throw std::invalid_argument("CAM_ORDER_GROUP_OR_LOCK_CONSTRAINT");
    auto result=CommitToolpathOrder(scene,request,full,optimization);
    runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
