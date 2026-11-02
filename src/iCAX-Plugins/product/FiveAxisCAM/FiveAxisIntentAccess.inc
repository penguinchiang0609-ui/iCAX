// Product query projections over the unified document. These objects
// are command-local values and never attach a second authority to the database.
namespace
{
    SFiveAxisIntentMetadata ReadIntentInspectionMetadata(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        using namespace iCAX::CAM::Intent;
        // Policy/setup reads need the validated engineering channel, not the
        // UI's clipped source spans, pose controls or another copy of geometry.
        // ProjectFiveAxisIntentPath changes only Feature.start in this channel;
        // that transient display anchor is deliberately absent here.
        if(path.ResultState=="Current"||!path.Value.Geometry.Segments.empty()||path.Value.Channels.contains("fiveaxis")){
            auto metadata=ReadFiveAxisIntentMetadata(path.Value);metadata.Feature.erase("start");return metadata;
        }
        // A pending edit can originate at a merged or dimension-edited path.
        // Find the nearest retained engineering metadata without assuming that
        // every context boundary is an icax.source with parameters.value.
        std::set<SPortRef> visited;
        const auto metadata=[&](auto&& self,const SPortRef& port)->std::optional<SFiveAxisIntentMetadata>
        {
            if(!visited.insert(port).second)return {};
            const auto definition=IntentDocument(scene).ReadOperation(port.OperationId);
            const auto read=[](const Map& value)->std::optional<SFiveAxisIntentMetadata>{if(!value.contains("channels"))return {};const auto& channels=std::get<Map>(value.at("channels").m_Value);if(!channels.contains("fiveaxis"))return {};return FiveAxisIntentMetadataFromChannel(std::get<Map>(channels.at("fiveaxis").m_Value));};
            if(definition.contains("result")){const auto& result=std::get<Map>(definition.at("result").m_Value);if(result.contains("outputs"))for(const auto& item:std::get<Array>(result.at("outputs").m_Value)){const auto& output=std::get<Map>(item.m_Value);if(output.at("portId").To<ID>()==port.PortId&&output.contains("value"))if(auto found=read(std::get<Map>(output.at("value").m_Value)))return found;}}
            const auto& parameters=std::get<Map>(definition.at("parameters").m_Value);if(parameters.contains("value"))if(auto found=read(std::get<Map>(parameters.at("value").m_Value)))return found;
            for(const auto& item:std::get<Array>(definition.at("inputs").m_Value)){const auto& input=std::get<Map>(item.m_Value);if(input.contains("replace")&&!input.at("replace").To<bool>())continue;if(auto found=self(self,PortFromData(std::get<Map>(input.at("source").m_Value))))return found;}
            return {};
        };
        auto saved=metadata(metadata,path.Port);if(!saved)throw std::invalid_argument("CAM_INTENT_PENDING_METADATA_MISSING");
        saved->Feature.erase("start");return std::move(*saved);
    }
    SFiveAxisIntentProjection ProjectIntentInspectionPath(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        if(path.ResultState=="Current"||!path.Value.Geometry.Segments.empty()||path.Value.Channels.contains("fiveaxis"))return ProjectFiveAxisIntentPath(path);
        SFiveAxisIntentProjection projection;projection.PathId=path.PathId;projection.Port=path.Port;projection.Name=path.Name;projection.UserNumber=path.UserNumber;
        projection.GroupId=path.GroupId;projection.Fingerprint=path.Fingerprint;projection.Value=path.Value;
        projection.Metadata=ReadIntentInspectionMetadata(scene,path);
        // This is definition metadata for a pending row. Its geometry, local
        // spans and pose controls remain absent until the real result exists.
        return projection;
    }
    bool IntentPathEnabledForMetadata(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,const SFiveAxisIntentMetadata& metadata)
    {
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),metadata.PlacementID);
        return Require<CFiveAxisSetupComponent>(scene.Database(),metadata.SetupID)->GetEnabled()
            &&Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID())->GetEnabled()&&path.Participating;
    }
    Map EngineeringView(const Map& value)
    {
        if(value.empty())return {};
        const auto body=EngineeringAssets(value,true);
        return {{"revision",Text("intent-view/")+EngineeringDigest(body)},{"value",body}};
    }
    UInt IntentPathDisplayNumber(const iCAX::CAM::Intent::SResolvedPath& path)
    {
        if(path.UserNumber.empty())return 0;
        std::size_t end=0;const auto value=std::stoull(path.UserNumber,&end);
        if(end!=path.UserNumber.size()||value>std::numeric_limits<UInt>::max())throw std::invalid_argument("CAM_INTENT_NUMBER_NOT_NUMERIC");
        return static_cast<UInt>(value);
    }
    struct IntentCutView final
    {
        iCAX::CAM::Intent::SResolvedPath Path;
        SFiveAxisIntentProjection Projection;
        Text PoseDefinitionRevision;
        explicit IntentCutView(Scene& scene,iCAX::CAM::Intent::SResolvedPath path)
            :Path(std::move(path)),Projection(ProjectIntentInspectionPath(scene,Path))
        {if(Path.ResultState!="Current")PoseDefinitionRevision=String(IntentOperationDefinition(scene,Path.Port.OperationId),"definitionFingerprint");}
        const ID& GetID()const{return Path.PathId;}
        const ID& GetSetupID()const{return Projection.Metadata.SetupID;}
        const ID& GetPlacementID()const{return Projection.Metadata.PlacementID;}
        const Text& GetName()const{return Path.Name;}
        const Text& GetUserNumber()const{return Path.UserNumber;}
        UInt GetDisplayNumber()const{return IntentPathDisplayNumber(Path);}
        const Text& GetGroupID()const{return Path.GroupId;}
        bool GetEnabled()const{return Path.OwnParticipating;}
        bool GetLocked()const{return Path.OwnLocked;}
        bool GetEffectiveEnabled()const{return Path.Participating;}
        bool GetEffectiveLocked()const{return Path.Locked;}
        const Text& GetResultState()const{return Path.ResultState;}
        Map GetDefinitionData()const{return EngineeringView(Projection.Metadata.Feature);}
        Map GetPoseData()const
        {
            auto data=EngineeringView(Projection.Metadata.Pose);
            data["revision"]=Text("intent-pose/")+(Path.ResultState=="Current"?
                iCAX::CAM::Intent::ToolpathFingerprint(Path.Value,{"pose","poseControls","poseField","contourPoseField"}):PoseDefinitionRevision);
            return data;
        }
        Map GetProcessData()const{return EngineeringView(Projection.Metadata.Process);}
        Map GetSourceData()const{return EngineeringView(Projection.Metadata.SourceData);}
        const Map& GetSourceGeometryRef()const{return Projection.Metadata.SourceGeometryRef;}
        const Text& GetBeforeCommands()const{return Projection.Metadata.BeforeCommands;}
        const Text& GetAfterCommands()const{return Projection.Metadata.AfterCommands;}
        const Text& GetSourceKey()const{return Projection.Metadata.SourceKey;}
        UInt GetInputRevision()const{return static_cast<UInt>(Projection.Metadata.InputRevision);}
    };
    using IntentCut=std::shared_ptr<const IntentCutView>;
    ID IntentAccessId(const Variant& value)
    {
        if(value.Is<ID>())return value.To<ID>();
        const auto parsed=ID::from_string(value.To<Text>());if(!parsed)throw std::invalid_argument("CAM_INTENT_ID_INVALID");return *parsed;
    }
    IntentCut ReadIntentCut(Scene& scene,const ID& path)
    {return std::make_shared<const IntentCutView>(scene,ReadIntentPath(scene,path));}
    std::vector<IntentCut> IntentCuts(Scene& scene,const ID& setup={})
    {
        std::vector<IntentCut> result;
        const auto append=[&](const ID& id)
        {
            auto inspection=InspectIntentPlan(scene,id);
            std::erase_if(inspection.Paths,[](const auto& path){return path.ParticipationReason=="Deleted";});
            for(auto& path:OrderFiveAxisIntentPaths(std::move(inspection.Paths),inspection.DisplayConstraints))result.push_back(std::make_shared<const IntentCutView>(scene,std::move(path)));
        };
        if(!setup.is_nil())append(setup);
        else for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(Component<CFiveAxisSetupComponent>(entity))append(entity->GetID());
        return result;
    }
    Array IntentCutGroups(Scene& scene,const ID& setup)
    {
        Array result;
        for(const auto& [id,value]:InspectIntentPlan(scene,setup).Organization)
        {
            const auto group=value.To<Map>();
            result.emplace_back(Map{{"id",id},{"name",group.at("name")},{"parentId",group.at("parentGroupId")},
                {"enabled",!group.contains("enabled")||group.at("enabled").To<bool>()},
                {"locked",group.contains("locked")&&group.at("locked").To<bool>()}});
        }
        return result;
    }
    SceneRuntime::OrderStructure& ReadOrderStructure(Scene& scene,const ID& setup)
    {
        const auto runtime=Runtime(scene);auto& cache=runtime->OrderStructures;
        if(const auto found=cache.find(setup);found!=cache.end()&&found->second.Valid&&found->second.Sequence==runtime->Sequence)return found->second;
        SceneRuntime::OrderStructure snapshot;snapshot.Sequence=runtime->Sequence;snapshot.Valid=true;
        const auto& inspection=InspectIntentPlan(scene,setup);std::vector<iCAX::CAM::Intent::SResolvedPath> identities;
        for(const auto& path:inspection.Paths)if(path.ParticipationReason!="Deleted"){
            iCAX::CAM::Intent::SResolvedPath id;id.PathId=path.PathId;id.Port=path.Port;id.GroupId=path.GroupId;
            id.OwnParticipating=path.OwnParticipating;id.OwnLocked=path.OwnLocked;
            snapshot.Paths.emplace(id.PathId,id);identities.push_back(std::move(id));
        }
        for(const auto& id:OrderFiveAxisIntentPaths(std::move(identities),inspection.DisplayConstraints))snapshot.Order.emplace_back(iCAX::Data::to_string(id.PathId));
        snapshot.Groups=IntentCutGroups(scene,setup);
        return cache.insert_or_assign(setup,std::move(snapshot)).first->second;
    }
    Array CutTreeOrder(Scene&,const ID&,const Map*);
    Map IntentCutOrderPolicy(Scene& scene,const ID& setup)
    {
        const auto plan=IntentDocument(scene).ReadPlanDefinition(IntentPlan(scene,setup));
        const auto constraints=plan.at("executionConstraints").To<Map>();
        if(!constraints.contains("blocks"))return {};Text mode,strategy="MarkHoleTrim";Map options;
        for(const auto& item:constraints.at("blocks").To<Array>())
        {
            const auto id=String(item.To<Map>(),"id");
            if(id=="scene.manual-order"||id=="scene.automatic-order")
            {
                if(!mode.empty())throw std::invalid_argument("CAM_INTENT_ORDER_POLICY_CONFLICT");
                mode=id=="scene.manual-order"?Text("Manual"):Text("Automatic");
                if(item.To<Map>().contains("strategy"))strategy=String(item.To<Map>(),"strategy");
                if(item.To<Map>().contains("options"))options=item.To<Map>().at("options").To<Map>();
            }
        }
        if(mode.empty())return {};
        auto order=CutTreeOrder(scene,setup,nullptr);
        Map result{{"mode",mode},{"intentIds",std::move(order)},{"strategy",strategy}};if(!options.empty())result["options"]=options;return result;
    }
    Array IntentFieldControlAnchors(const iCAX::CAM::Intent::SResolvedPath& path)
    {
        Array anchors;const auto& channels=path.Value.Channels;
        if(channels.contains("poseControls"))for(const auto& item:channels.at("poseControls").To<Map>().at("entries").To<Array>())
        {const auto point=item.To<Map>();if(!point.contains("set")||String(point,"set")=="active")anchors.push_back(point.at("anchor"));}
        if(channels.contains("poseField"))for(const auto& item:channels.at("poseField").To<Map>().at("entries").To<Array>())
        {const auto range=iCAX::CAM::Intent::RangeFromData(item.To<Map>().at("range").To<Map>());anchors.emplace_back(iCAX::CAM::Intent::ToData(range.Start));anchors.emplace_back(iCAX::CAM::Intent::ToData(range.End));}
        return anchors;
    }
    bool IntentSameNativeAnchor(const Map& a,const Map& b)
    {
        const auto first=iCAX::CAM::Intent::AnchorFromData(a),second=iCAX::CAM::Intent::AnchorFromData(b);
        return first.SegmentId==second.SegmentId&&std::abs(first.Parameter-second.Parameter)<1e-10;
    }
    using IntentHostStage=std::function<void(iCAX::Database::ITransaction&,const std::vector<Map>&)>;
    std::vector<Map> CommitIntentHostCommands(Scene& scene,const std::vector<Map>& commands,const IntentHostStage& stage,
        const Text& undoName,const std::set<ID>& regenerate={})
    {
        std::vector<Map> receipts;
        if(commands.empty())Transaction(scene.Database(),undoName,[&](auto& tx){if(stage)stage(tx,receipts);});
        else
        {
            auto& document=IntentDocument(scene);
            iCAX::CAM::Intent::SEditableToolpathHostEdit host;host.UndoName=undoName;
            host.Stage=[&](auto& tx,const auto& results){
                const auto root=scene.Database().GetMetaEntity();
                if(Require<CFiveAxisJobComponent>(scene.Database(),root->GetID())->GetIntentDocumentID().is_nil())
                    tx.ModifyComponent(root->GetID(),CFiveAxisJobComponent::S_ClassName,{{"IntentDocumentID",document.Id()}});
                if(stage)stage(tx,results);
            };
            receipts=document.ExecuteBatch(commands,host);
        }
        for(const auto& setup:regenerate)QueueIntentGeneration(scene,setup);
        return receipts;
    }
    std::vector<Map> RetireIntentPlanCommands(Scene& scene,const ID& setup,const ID& commandNamespace)
    {
        const auto planId=IntentPlan(scene,setup);const auto plan=IntentDocument(scene).ReadPlan(planId);iCAX::Data::uuid_name_generator names(commandNamespace);
        std::vector<Map> commands{{{"kind",Text("SetPlanRoots")},{"commandId",names("retire-plan/roots/"+iCAX::Data::to_string(planId))},
            {"planId",planId},{"roots",Array{}},{"expectedPlanFingerprint",plan.at("definitionFingerprint")}}};
        for(const auto& value:plan.at("operations").To<Array>())
        {
            const auto operation=IntentAccessId(value);const auto definition=IntentDocument(scene).ReadOperation(operation);
            if(definition.contains("enabled")&&!definition.at("enabled").To<bool>())continue;
            commands.push_back({{"kind",Text("SetOperationEnabled")},{"commandId",names("retire-plan/operation/"+iCAX::Data::to_string(operation))},
                {"planId",planId},{"operationId",operation},{"enabled",false},{"dependencyPolicy",Text("KeepInvalid")},
                {"expectedOperationFingerprint",definition.at("definitionFingerprint")}});
        }
        return commands;
    }
    std::vector<Map> StaleIntentSourceCommands(Scene& scene,const std::set<ID>& placements,const ID& commandNamespace)
    {
        using namespace iCAX::CAM::Intent;
        std::map<ID,ID> sources;std::set<ID> visited;iCAX::Data::uuid_name_generator names(commandNamespace);
        const auto walk=[&](auto&& self,const ID& operation,const ID& plan)->void
        {
            if(!visited.insert(operation).second)return;const auto definition=IntentDocument(scene).ReadOperation(operation);
            if(String(definition,"typeId")=="icax.source"){sources[operation]=plan;return;}
            for(const auto& item:definition.at("inputs").To<Array>())self(self,PortFromData(item.To<Map>().at("source").To<Map>()).OperationId,plan);
        };
        for(const auto& cut:IntentCuts(scene))if(placements.contains(cut->GetPlacementID()))walk(walk,cut->Path.Port.OperationId,IntentPlan(scene,cut->GetSetupID()));
        std::vector<Map> commands;
        for(const auto& [source,plan]:sources)
        {
            const auto definition=IntentDocument(scene).ReadOperation(source);auto binding=definition.at("sourceBinding").To<Map>();binding["mode"]=Text("LiveLinked");binding["state"]=Text("Stale");
            commands.push_back({{"kind",Text("UpdateSource")},{"commandId",names("stale-source/"+iCAX::Data::to_string(source))},{"planId",plan},
                {"operationId",source},{"value",definition.at("parameters").To<Map>().at("value")},{"sourceBinding",binding},
                {"expectedOperationFingerprint",definition.at("definitionFingerprint")}});
        }
        return commands;
    }
}
