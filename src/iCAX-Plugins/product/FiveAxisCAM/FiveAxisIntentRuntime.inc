// Owner-thread bridge between the scene and the reusable toolpath document.
// Only FreezeIntentContext reads resources. Workers receive sealed values.
namespace
{
    SFiveAxisIntentProjection ProjectIntentInspectionPath(Scene&,const iCAX::CAM::Intent::SResolvedPath&);
    SFiveAxisIntentMetadata ReadIntentInspectionMetadata(Scene&,const iCAX::CAM::Intent::SResolvedPath&);
    const Map& IntentOperationDefinition(Scene& scene,const ID& operation)
    {
        auto& definitions=Runtime(scene)->IntentDefinitions;
        if(const auto found=definitions.find(operation);found!=definitions.end())return found->second;
        auto definition=IntentDocument(scene).ReadOperationDefinition(operation);
        return definitions.emplace(operation,std::move(definition)).first->second;
    }
    iCAX::CAM::Intent::SResolvedPath IntentDesignPath(Scene&,const iCAX::CAM::Intent::SResolvedPath&);
    ID IntentProcessingOperation(Scene&,const iCAX::CAM::Intent::SPortRef&,const Text&);
    bool IntentReversed(Scene& scene,const iCAX::CAM::Intent::SPortRef& port)
    {
        using namespace iCAX::CAM::Intent;auto current=port;std::set<ID> visited;bool reversed=false;
        while(visited.insert(current.OperationId).second)
        {
            const auto& definition=IntentOperationDefinition(scene,current.OperationId);const auto type=String(definition,"typeId");
            if(type=="icax.edit.reverse"||type=="fiveaxis.reverse")reversed=!reversed;
            const auto inputs=definition.at("inputs").To<Array>();if(inputs.empty())return reversed;
            std::vector<SPortRef> replacing;for(const auto& input:inputs){const auto binding=input.To<Map>();if(!binding.contains("replace")||binding.at("replace").To<bool>())replacing.push_back(PortFromData(binding.at("source").To<Map>()));}
            if(replacing.size()!=1)return false;current=replacing.front();
        }
        throw std::invalid_argument("CAM_INTENT_GRAPH_CYCLE");
    }
    iCAX::GeometryData::CompositeOffsetCurve3 IntentExactCurve(const iCAX::CAM::Intent::SToolpathValue& value)
    {
        using namespace iCAX::GeometryData;
        CompositeOffsetCurve3 result;result.Closed=value.Geometry.Closed;
        for(std::size_t i=0;i<value.Geometry.Segments.size();++i)
        {
            const auto& segment=value.Geometry.Segments[i];OffsetCurvePiece3 piece;
            if(const auto* curve=std::get_if<CurveSegment3>(&segment.Curve))piece.Geometry=*curve;
            else if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve))piece.Geometry=*offset;
            else throw std::invalid_argument("CAM_INTENT_EXECUTION_REQUIRES_3D_CURVE");
            if(segment.ConstructionRole.empty())piece.SourceSegment=i;result.Segments.push_back(std::move(piece));
        }
        return result;
    }
    Array IntentPreviewPoints(const iCAX::GeometryServices::ICurveOffset3DService& service,const iCAX::GeometryData::CompositeOffsetCurve3& curve,
        const std::shared_ptr<std::atomic_bool>& cancelled={})
    {
        if(curve.Segments.empty())return {};
        iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(service,[cancelled]{return cancelled&&cancelled->load();});
        const auto samples=evaluator.SamplePoints(curve,.05,5.);Array points;
        for(const auto& sample:samples.Points){const auto& p=sample.Value.OffsetPoint;points.emplace_back(Array{p.X,p.Y,p.Z});}
        return points;
    }
    Array IntentPreviewPoints(const iCAX::GeometryServices::ICurveOffset3DService& service,const iCAX::CAM::Intent::SResolvedPath& path,
        const std::shared_ptr<std::atomic_bool>& cancelled={})
    {return IntentPreviewPoints(service,IntentExactCurve(path.Value),cancelled);}
    Array IntentPreviewPoints(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {return IntentPreviewPoints(*scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),path);}
    Array IntentCutStartPoint(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        if(path.Value.Geometry.Segments.empty())return {};
        const auto exact=IntentExactCurve(path.Value);
        const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
        iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*service);
        for(std::size_t i=0;i<path.Value.Geometry.Segments.size();++i)
        {
            const auto& role=path.Value.Geometry.Segments[i].ConstructionRole;
            if(role=="LeadIn"||role=="LeadOut")continue;
            const auto point=evaluator.At(exact.Segments[i],iCAX::GeometryServices::OffsetTraversal::Start(exact.Segments[i])).OffsetPoint;
            return {point.X,point.Y,point.Z};
        }
        return {};
    }
    Map IntentPathView(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        using namespace iCAX::CAM::Intent;
        const auto metadata=ReadIntentInspectionMetadata(scene,path);const auto& feature=metadata.Feature;
        if(path.ResultState!="Current"&&path.Value.Geometry.Segments.empty())
            return {{"intentId",iCAX::Data::to_string(path.PathId)},{"feature",Map{{"revision",path.Fingerprint}}},{"geometryKind",feature.at("geometryKind")},
                {"cutType",Text("Contour")},{"canBeStandardHole",false},{"closed",false},{"exactClosed",false},{"reversed",false},
                {"retainedSide",feature.at("retainedSide")},{"sourceSpans",Array{}},{"start",Map{}},{"sourceState",path.ResultState}};
        const auto design=IntentDesignPath(scene,path);
        Array spans;for(const auto& segment:design.Value.Geometry.Segments)
        {
            const auto range=std::visit([](const auto& curve){if constexpr(std::is_same_v<std::decay_t<decltype(curve)>,iCAX::GeometryData::DirectionalCurveOffset3>)return curve.Source.Range;else return curve.Range;},segment.Curve);
            spans.emplace_back(Map{{"segmentId",segment.Id},{"first",range.First},{"last",range.Last},{"reversed",range.Reversed}});
        }
        Map start;
        if(!design.Value.Geometry.Segments.empty())start=ToData(SPathAnchor{design.Port,design.Value.Geometry.Segments.front().Id,
            iCAX::GeometryServices::OffsetTraversal::Start(IntentExactCurve(design.Value).Segments.front())});
        try{const auto& trace=IntentOperationDefinition(scene,IntentProcessingOperation(scene,path.Port,"fiveaxis.trace"));const auto& parameters=std::get<Map>(trace.at("parameters").m_Value);if(parameters.contains("start"))start=parameters.at("start").To<Map>();}catch(const std::invalid_argument&){}
        return {{"intentId",iCAX::Data::to_string(path.PathId)},{"feature",Map{{"revision",path.Fingerprint}}},
            {"geometryKind",feature.at("geometryKind")},{"cutType",Text(String(metadata.Pose,"mode")=="FixedDirection"?"StandardHole":"Contour")},
            {"canBeStandardHole",feature.contains("standardHole")&&design.Value.Geometry.Closed},{"closed",design.Value.Geometry.Closed},{"exactClosed",path.Value.Geometry.Closed},
            {"reversed",IntentReversed(scene,path.Port)},{"retainedSide",feature.at("retainedSide")},{"sourceSpans",spans},
            {"start",start}};
    }
    Map IntentStandardHoleView(const iCAX::CAM::Intent::SResolvedPath& path)
    {
        if(!path.Value.Channels.contains("fiveaxis"))return {};
        const auto metadata=ReadFiveAxisIntentMetadata(path.Value);if(!metadata.Feature.contains("standardHole"))return {};auto hole=metadata.Feature.at("standardHole").To<Map>();
        if(hole.contains("manual")&&hole.at("manual").To<bool>())return hole;
        for(const auto& segment:path.Value.Geometry.Segments){if(!segment.ConstructionRole.empty())continue;const auto* curve=std::get_if<iCAX::GeometryData::CurveSegment3>(&segment.Curve);const auto* offset=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve);if(!curve&&!offset)continue;std::visit([&](const auto& c){using T=std::decay_t<decltype(c)>;if constexpr(std::is_same_v<T,iCAX::GeometryData::Circle3>)hole["diameterMm"]=2*c.Radius;else if constexpr(std::is_same_v<T,iCAX::GeometryData::Arc3>)hole["diameterMm"]=2*c.Basis.Radius;else if constexpr(std::is_same_v<T,iCAX::GeometryData::Ellipse3>){hole["majorDiameterMm"]=2*c.MajorRadius;hole["minorDiameterMm"]=2*c.MinorRadius;}else if constexpr(std::is_same_v<T,iCAX::GeometryData::EllipseArc3>){hole["majorDiameterMm"]=2*c.Basis.MajorRadius;hole["minorDiameterMm"]=2*c.Basis.MinorRadius;}},curve?curve->Curve:offset->Source.Curve);break;}
        return hole;
    }
    iCAX::CAM::Intent::SPathAnchor IntentAnchor(const iCAX::CAM::Intent::SResolvedPath& path,const Map& data)
    {
        using namespace iCAX::CAM::Intent;
        auto anchor=AnchorFromData(data);
        if(anchor.Port!=path.Port)throw std::invalid_argument("CAM_INTENT_ANCHOR_SOURCE_CHANGED");
        if(!anchor.ObservedGeometryFingerprint.empty()&&anchor.ObservedGeometryFingerprint!=ToolpathFingerprint(path.Value,{"geometry","topology","traversal","frame"}))
            throw std::invalid_argument("CAM_INTENT_STALE_PICK");
        // Capture performs current-geometry and occurrence validation in the
        // public command. The observed fingerprint also fences delayed picks.
        return anchor;
    }
    Map ResolveIntentPoint(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,const Map& request,bool split)
    {
        using namespace iCAX::CAM::Intent;namespace Walk=iCAX::GeometryServices::OffsetTraversal;
        const auto nearPosition=RegistrationPoint(request.at("nearPoint"));const auto maximum=RegistrationNumber(request.at("maximumDistanceMm"));
        if(maximum<=0)throw std::invalid_argument("CAM_REFERENCE_DISTANCE_INVALID");
        const auto mode=String(request,"mode");if(mode!="Auto"&&mode!="ClosestPoint"&&mode!="Endpoint")throw std::invalid_argument("CAM_REFERENCE_MODE_UNSUPPORTED");
        const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();Walk::Evaluator evaluator(*service);
        const auto exact=IntentExactCurve(path.Value);const auto samples=evaluator.Sample(exact,.02,2.);
        struct Pick{std::size_t Index;double U,Distance; iCAX::GeometryData::Point3 Point;};std::vector<Pick> picks,endpoints;
        const auto distance=[&](const auto& point){return std::hypot(point.X-nearPosition[0],point.Y-nearPosition[1],point.Z-nearPosition[2]);};
        const auto add=[&](auto& list,std::size_t index,double parameter)
        {const auto sample=evaluator.At(exact.Segments[index],parameter);list.push_back({index,parameter,distance(sample.OffsetPoint),sample.OffsetPoint});};
        for(std::size_t i=0;i<exact.Segments.size();++i)
        {
            const auto start=Walk::Start(exact.Segments[i]),end=Walk::End(exact.Segments[i]);add(endpoints,i,start);add(endpoints,i,end);
            std::vector<double> stations;for(const auto& sample:samples.Points)if(sample.PieceIndex==i)stations.push_back(sample.Value.SourceParameter);
            if(stations.size()<2)continue;std::sort(stations.begin(),stations.end());
            std::size_t best=0;double bestDistance=std::numeric_limits<double>::infinity();
            for(std::size_t j=0;j<stations.size();++j){const auto d=distance(evaluator.At(exact.Segments[i],stations[j]).OffsetPoint);if(d<bestDistance){bestDistance=d;best=j;}}
            double lo=stations[best?best-1:0],hi=stations[std::min(best+1,stations.size()-1)];
            // Numerical minimization locates a UI pick on an exact expression;
            // it never becomes an authoritative replacement curve.
            for(int iteration=0;iteration<48;++iteration)
            {const auto a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;if(distance(evaluator.At(exact.Segments[i],a).OffsetPoint)<distance(evaluator.At(exact.Segments[i],b).OffsetPoint))hi=b;else lo=a;}
            add(picks,i,(lo+hi)/2);
        }
        const auto canonicalize=[&](Pick pick)
        {if(std::abs(pick.U-Walk::End(exact.Segments[pick.Index]))<=1e-11&&
            (pick.Index+1<exact.Segments.size()||exact.Closed)){pick.Index=(pick.Index+1)%exact.Segments.size();pick.U=Walk::Start(exact.Segments[pick.Index]);}return pick;};
        for(auto& pick:endpoints)pick=canonicalize(pick);for(auto& pick:picks)pick=canonicalize(pick);
        const auto order=[](const Pick& a,const Pick& b){return a.Distance<b.Distance;};std::sort(endpoints.begin(),endpoints.end(),order);std::sort(picks.begin(),picks.end(),order);
        const auto& candidates=mode=="Endpoint"||(mode=="Auto"&&!endpoints.empty()&&endpoints.front().Distance<maximum*.4)?endpoints:picks;
        if(candidates.empty()||candidates.front().Distance>maximum)throw std::invalid_argument("CAD_REFERENCE_NOT_FOUND");const auto selected=candidates.front();
        for(const auto& other:candidates)if(std::abs(other.Distance-selected.Distance)<1e-8&&(other.Index!=selected.Index||std::abs(other.U-selected.U)>1e-9))throw std::invalid_argument("CAD_REFERENCE_AMBIGUOUS");
        if(split&&((selected.Index==0&&selected.U==Walk::Start(exact.Segments.front()))||
            (!exact.Closed&&selected.Index+1==exact.Segments.size()&&selected.U==Walk::End(exact.Segments.back()))))throw std::invalid_argument(exact.Closed?"CAM_SPLIT_EXISTING_START":"CURVE_SPLIT_ENDPOINT");
        SPathAnchor anchor{path.Port,path.Value.Geometry.Segments[selected.Index].Id,selected.U};
        anchor.ObservedGeometryFingerprint=ToolpathFingerprint(path.Value,{"geometry","topology","traversal","frame"});
        return {{"anchor",ToData(anchor)},{"point",Array{selected.Point.X,selected.Point.Y,selected.Point.Z}},{"distanceMm",selected.Distance}};
    }
    ID IntentPlan(Scene& scene,const ID& setup)
    {
        const auto id=Require<CFiveAxisSetupComponent>(scene.Database(),setup)->GetIntentPlanID();
        if(id.is_nil())throw std::invalid_argument("CAM_INTENT_SETUP_PLAN_MISSING");
        return id;
    }
    iCAX::CAM::Intent::CEditableToolpathDocument& IntentDocument(Scene& scene)
    {
        const auto runtime=Runtime(scene);
        if(!runtime->IntentDocument)InitializeIntentDocument(scene);
        if(!runtime->IntentDocument)throw std::runtime_error("CAM_INTENT_DOCUMENT_NOT_READY");
        return *runtime->IntentDocument;
    }
    const iCAX::CAM::Intent::SEffectiveToolpaths& InspectIntentPlan(Scene& scene,const ID& setup)
    {
        const auto plan=IntentPlan(scene,setup);auto& inspections=Runtime(scene)->IntentInspections;
        if(const auto found=inspections.find(plan);found!=inspections.end())return found->second;
        return inspections.emplace(plan,IntentDocument(scene).InspectPlan(plan)).first->second;
    }
    const std::vector<iCAX::CAM::Intent::SResolvedPath>& IntentPaths(Scene& scene,const ID& setup)
    {return InspectIntentPlan(scene,setup).Paths;}
    const iCAX::CAM::Intent::SEffectiveToolpaths& ResolveIntentPlan(Scene& scene,const ID& setup)
    {
        const auto plan=IntentPlan(scene,setup);auto& resolutions=Runtime(scene)->IntentResolutions;
        if(const auto found=resolutions.find(plan);found!=resolutions.end())return found->second;
        return resolutions.emplace(plan,IntentDocument(scene).Resolve(plan)).first->second;
    }
    const iCAX::CAM::Intent::SResolvedPath& ReadIntentPort(Scene& scene,const ID& plan,const iCAX::CAM::Intent::SPortRef& port)
    {
        const auto key=std::make_pair(plan,port);auto& paths=Runtime(scene)->IntentPortPaths;
        if(const auto found=paths.find(key);found!=paths.end())return found->second;
        return paths.emplace(key,IntentDocument(scene).ReadPath(plan,port)).first->second;
    }
    const iCAX::CAM::Intent::SResolvedPath& FindIntentPath(Scene& scene,const ID& id)
    {
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto setup=Component<CFiveAxisSetupComponent>(entity);setup&&!setup->GetIntentPlanID().is_nil())
                for(const auto& path:InspectIntentPlan(scene,entity->GetID()).Paths)if(path.PathId==id)return path;
        throw std::invalid_argument("CAM_INTENT_PATH_NOT_FOUND");
    }
    iCAX::CAM::Intent::SResolvedPath ReadIntentPath(Scene& scene,const ID& id)
    {return FindIntentPath(scene,id);}
    ID IntentSourceOperation(Scene& scene,const iCAX::CAM::Intent::SPortRef& port)
    {
        using namespace iCAX::CAM::Intent;
        auto& document=IntentDocument(scene);std::set<ID> sources,visited;
        const auto walk=[&](auto&& self,const ID& operation)->void
        {
            if(!visited.insert(operation).second)return;const auto& definition=IntentOperationDefinition(scene,operation);
            if(String(definition,"typeId")=="icax.source"||String(definition,"typeId")=="fiveaxis.merge"){sources.insert(operation);return;}
            for(const auto& value:definition.at("inputs").To<Array>())self(self,PortFromData(value.To<Map>().at("source").To<Map>()).OperationId);
        };
        walk(walk,port.OperationId);
        if(sources.size()!=1)throw std::invalid_argument("CAM_INTENT_SINGLE_SOURCE_CONTEXT_REQUIRED");
        return *sources.begin();
    }
    ID IntentProcessingOperation(Scene& scene,const iCAX::CAM::Intent::SPortRef& port,const Text& type)
    {
        using namespace iCAX::CAM::Intent;std::set<ID> found,visited;
        const auto walk=[&](auto&& self,ID operation)->void
        {
            if(!visited.insert(operation).second)return;const auto& definition=IntentOperationDefinition(scene,operation);
            if(String(definition,"typeId")==type){found.insert(operation);return;}
            if(String(definition,"typeId")=="fiveaxis.merge")return;
            for(const auto& value:definition.at("inputs").To<Array>())self(self,PortFromData(value.To<Map>().at("source").To<Map>()).OperationId);
        };
        walk(walk,port.OperationId);if(found.size()!=1)throw std::invalid_argument("CAM_INTENT_PROCESS_OPERATION_SELECTION_REQUIRED");return *found.begin();
    }
    iCAX::CAM::Intent::SPortRef IntentDesignPort(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        using namespace iCAX::CAM::Intent;if(path.ResultState!="Current")return path.Port;auto port=path.Port;std::set<ID> visited;
        const std::set<Text> preserve{"fiveaxis.correction","fiveaxis.trace","fiveaxis.leads","fiveaxis.pose","fiveaxis.pose.delta","fiveaxis.pose.reverse","fiveaxis.pose.policy","fiveaxis.pose.remove","fiveaxis.cutType","fiveaxis.metadata","fiveaxis.reverse","icax.edit.reverse","icax.edit.channels"};
        while(visited.insert(port.OperationId).second)
        {
            const auto& definition=IntentOperationDefinition(scene,port.OperationId);const auto type=String(definition,"typeId");
            if(type!="fiveaxis.kerf"&&!preserve.contains(type))return path.Port;
            const auto inputs=definition.at("inputs").To<Array>();if(inputs.size()!=1)return path.Port;port=PortFromData(inputs.front().To<Map>().at("source").To<Map>());
            if(type=="fiveaxis.kerf")return port;
        }
        throw std::invalid_argument("CAM_INTENT_PREVIEW_GRAPH_CYCLE");
    }
    iCAX::CAM::Intent::SResolvedPath IntentDesignPath(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        const auto port=IntentDesignPort(scene,path);if(port==path.Port)return path;
        return ReadIntentPort(scene,IntentPlan(scene,ReadFiveAxisIntentMetadata(path.Value).SetupID),port);
    }
    void PrimeIntentDesignPaths(Scene& scene,const ID& setup)
    {
        using namespace iCAX::CAM::Intent;
        const auto plan=IntentPlan(scene,setup);auto& cached=Runtime(scene)->IntentPortPaths;std::set<SPortRef> missing;
        for(const auto& path:IntentPaths(scene,setup))if(path.ResultState=="Current")
        {
            const auto port=IntentDesignPort(scene,path);if(port!=path.Port&&!cached.contains({plan,port}))missing.insert(port);
            // Geometry edits after processing retain the effective path as
            // their design result. Their nominal process input is still needed
            // for previews; collect it in this same validated batch instead of
            // reloading and validating the complete plan on the first click.
            if(path.Value.Channels.contains("fiveaxis.geometryEdits"))try
            {
                const auto operation=IntentProcessingOperation(scene,path.Port,"fiveaxis.kerf");
                const auto inputs=IntentOperationDefinition(scene,operation).at("inputs").To<Array>();
                if(inputs.size()==1){const auto nominal=PortFromData(inputs.front().To<Map>().at("source").To<Map>());if(!cached.contains({plan,nominal}))missing.insert(nominal);}
            }
            catch(const std::invalid_argument& error)
            {if(Text(error.what())!="CAM_INTENT_PROCESS_OPERATION_SELECTION_REQUIRED")throw;}
        }
        if(missing.empty())return;
        const std::vector<SPortRef> ports(missing.begin(),missing.end());
        for(auto& path:IntentDocument(scene).ReadPaths(plan,ports))cached.emplace(std::make_pair(plan,path.Port),std::move(path));
    }
    void RequireIntentOperationEditScope(Scene& scene,const ID& operation,const std::set<ID>& selection)
    {
        using namespace iCAX::CAM::Intent;
        const auto uses=[&](auto&& self,ID node,std::set<ID>& seen)->bool
        {
            if(node==operation)return true;if(!seen.insert(node).second)return false;const auto& definition=IntentOperationDefinition(scene,node);
            for(const auto& value:definition.at("inputs").To<Array>())if(self(self,PortFromData(value.To<Map>().at("source").To<Map>()).OperationId,seen))return true;return false;
        };
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto setup=Component<CFiveAxisSetupComponent>(entity);setup&&!setup->GetIntentPlanID().is_nil())
                for(const auto& path:IntentPaths(scene,entity->GetID()))
                {std::set<ID> seen;if(uses(uses,path.Port.OperationId,seen)&&!selection.contains(path.PathId))throw std::invalid_argument("CAM_INTENT_SHARED_OPERATION_SELECT_ALL_RESULTS");}
    }
    Map IntentOperationUpdate(Scene& scene,const ID& operation,const ID& plan,const ID& command,Map parameters)
    {
        const auto& definition=IntentOperationDefinition(scene,operation);
        return {{"kind",Text("UpdateOperation")},{"commandId",command},{"planId",plan},{"operationId",operation},
            {"expectedOperationFingerprint",definition.at("definitionFingerprint")},{"parameters",std::move(parameters)}};
    }
    std::vector<Map> AppendIntentPathOperation(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,
        const Text& typeId,Map parameters,const ID& command,Map context={})
    {
        using namespace iCAX::CAM::Intent;
        if(path.ResultState!="Current")throw std::invalid_argument("CAM_INTENT_PATH_NOT_CURRENT");
        const auto plan=IntentPlan(scene,ReadFiveAxisIntentMetadata(path.Value).SetupID);std::vector<Map> commands;
        iCAX::Data::uuid_name_generator names(command);
        if(!path.OwnParticipating)
        {
            if(path.ParticipationReason!="Disabled")throw std::invalid_argument("CAM_INTENT_DELETED_PATH_REQUIRES_RESTORE");
            const auto type=Runtime(scene)->IntentRegistry->Find(typeId);
            if(!type||type->DeclaredOutputRoles!=std::set<Text>{"main"}||type->DeclaredOutputIdentityInputs!=std::map<Text,std::size_t>{{"main",0}})
                throw std::invalid_argument("CAM_INTENT_DISABLED_EDIT_REQUIRES_INHERITED_TERMINAL");
            commands.push_back({{"kind",Text("SetParticipation")},{"commandId",names("edit-disabled/restore")},{"planId",plan},{"targets",Array{ToData(path.Port)}},{"enabled",true},
                {"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint}}}});
        }
        commands.push_back({{"kind",Text("AddOperation")},{"commandId",command},{"planId",plan},{"typeId",typeId},{"parameters",std::move(parameters)},{"context",std::move(context)},
            {"inputs",Array{ToData(SOperationInput{"input/0",path.Port,true,{}})}},{"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint}}}});
        if(!path.OwnParticipating)commands.push_back({{"kind",Text("SetParticipation")},{"commandId",names("edit-disabled/retain-disabled")},{"planId",plan},
            {"targets",Array{Map{{"fromCommand",command},{"field",Text("port")}}}},{"enabled",false}});
        return commands;
    }
    std::shared_ptr<const CFiveAxisIntentOperationContext> FreezeIntentContext(Scene& scene,const ID& source)
    {
        using namespace iCAX::CAM::Intent;
        const auto runtime=Runtime(scene);
        // The only persistent binding is a source operation identity. Read its
        // current authoritative definition on the owner thread at every freeze.
        const auto& definition=IntentOperationDefinition(scene,source);
        if(String(definition,"typeId")=="fiveaxis.merge")
        {
            const auto sources=definition.at("context").To<Map>().at("sourceOperationIds").To<Array>();std::shared_ptr<const CFiveAxisIntentOperationContext> first;CFiveAxisIntentOperationContext::Bindings bindings;
            for(const auto& item:sources){auto context=FreezeIntentContext(scene,item.Is<ID>()?item.To<ID>():Identifier(Map{{"id",item}},"id"));if(!first)first=context;else if(first->FrozenModel()->Fingerprint()!=context->FrozenModel()->Fingerprint()||first->Frame()!=context->Frame()||first->OutwardSign()!=context->OutwardSign())throw std::invalid_argument("CAM_MERGE_SAME_WORKPIECE_REQUIRED");bindings.insert(context->SourceBindings().begin(),context->SourceBindings().end());}
            if(!first)throw std::invalid_argument("CAM_MERGE_REQUIRES_TWO_PATHS");return std::make_shared<const CFiveAxisIntentOperationContext>(first->FrozenModel(),std::move(bindings),first->FrozenModel()->Fingerprint(),"fiveaxis-intent-services/1",first->Frame(),first->OutwardSign(),scene.Services().Resolve<iCAX::GeometryServices::IBoundaryLeadService>(),scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),first->Editing());
        }
        if(String(definition,"typeId")!="icax.source")throw std::invalid_argument("CAM_INTENT_CONTEXT_SOURCE_REQUIRED");
        const auto key=iCAX::Data::to_string(source)+"/"+String(definition,"definitionFingerprint")+"/fiveaxis-intent-services/1";
        if(const auto found=runtime->IntentContexts.find(source);found!=runtime->IntentContexts.end()&&found->second.first==key)return found->second.second;
        const auto& parameters=std::get<Map>(definition.at("parameters").m_Value);
        const auto value=ToolpathFromData(std::get<Map>(parameters.at("value").m_Value));
        const auto metadata=ReadFiveAxisIntentMetadata(value);
        const auto geometryRef=ReadWireRef(WireRef(metadata.SourceGeometryRef));
        const auto geometryKey=iCAX::Data::VariantSerializer::Serialize(StoredRef(geometryRef));
        auto& frozen=runtime->IntentModels[geometryKey];
        if(!frozen)
        {
            const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(geometryRef.URL,geometryRef.nVersion);
            if(!model)throw std::runtime_error("CAM_GEOMETRY_MISSING");
            frozen=std::make_shared<const CFrozenFiveAxisIntentModel>(*model);
        }
        auto recipe=metadata.SourceData;std::size_t depth=0;
        while(recipe.contains("kind")&&String(recipe,"kind")=="Split")
        {
            if(++depth>256)throw std::invalid_argument("CAM_CUT_SOURCE_MAP_MISMATCH");
            const auto parent=recipe.at("parentSourceData").To<Map>();
            recipe=parent.contains("value")?EngineeringBody(parent,"cam.source-recipe"):parent;
        }
        const auto sign=static_cast<int>(RegistrationNumber(recipe.at("outwardSign")));
        auto context=std::make_shared<const CFiveAxisIntentOperationContext>(frozen,BuildFiveAxisIntentCadBindings(metadata.SourceSpans),
            geometryKey,"fiveaxis-intent-services/1",value.Geometry.FrameId,sign,
            scene.Services().Resolve<iCAX::GeometryServices::IBoundaryLeadService>(),
            scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),
            scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>());
        runtime->IntentContexts[source]={key,context};return context;
    }
    std::vector<Map> BuildIntentProcessing(const SFiveAxisIntentSource& source,ID plan,ID parent)
    {
        using namespace iCAX::CAM::Intent;
        const auto metadata=ReadFiveAxisIntentMetadata(source.Value);const auto& process=metadata.Process;
        std::vector<Map> commands;
        const auto append=[&](const Text& type,Map parameters)
        {
            const auto command=iCAX::Data::GenerateNewUUID();
            commands.push_back({{"kind",Text("AddOperation")},{"commandId",command},{"operationId",command},{"planId",plan},
                {"typeId",type},{"parameters",std::move(parameters)},
                {"context",Map{{"sourceOperationId",source.SourceOperationId}}},
                {"inputs",Array{Map{{"slot",Text("input/0")},{"source",Map{{"fromCommand",parent},{"field",Text("port")}}},{"replace",true},{"ranges",Array{}}}}}});
            parent=command;
        };
        append("fiveaxis.pose",{{"policy",metadata.Pose}});
        append("fiveaxis.kerf",{{"policy",process.at("kerf")}});
        append("fiveaxis.trace",{{"gapLengthMm",process.at("gapLengthMm")},{"overcutLengthMm",process.at("overcutLengthMm")},{"allowTrimmedStart",true}});
        append("fiveaxis.leads",{{"leadIn",process.at("leadIn")},{"leadOut",process.at("leadOut")}});
        return commands;
    }
    void QueueIntentGeneration(Scene& scene,const ID& setup,std::size_t concurrency)
    {
        const auto runtime=Runtime(scene);const auto plan=IntentPlan(scene,setup);
        if(const auto it=runtime->IntentGenerations.find(plan);it!=runtime->IntentGenerations.end())it->second->Cancel();
        if(const auto old=runtime->IntentGenerationTasks.find(plan);old!=runtime->IntentGenerationTasks.end())
            if(runtime->Tasks.contains(old->second)&&runtime->Tasks.at(old->second).Status=="Running")runtime->Tasks.at(old->second).Status="Superseded";
        runtime->IntentGenerations[plan]=IntentDocument(scene).GenerateAsync(plan,concurrency);
        ImportTask task;task.Kind="IntentRefresh";task.SetupID=setup;task.Sequence=runtime->Sequence;
        const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));runtime->IntentGenerationTasks[plan]=id;
    }
    void InitializeIntentDocument(Scene& scene)
    {
        using namespace iCAX::CAM::Intent;
        const auto runtime=Runtime(scene);if(runtime->IntentDocument||runtime->PreparingIntents)return;
        runtime->PreparingIntents=true;
        try
        {
            auto registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*registry);
            auto reverse=*registry->Find("icax.edit.reverse");const auto reverseGeometry=reverse.Evaluate;
            reverse.TypeId="fiveaxis.reverse";reverse.WriteChannels.insert("fiveaxis.leads");
            reverse.Evaluate=[reverseGeometry](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
            {
                auto candidate=reverseGeometry(input,geometry);
                for(auto& output:candidate.Outputs)
                {
                    for(auto& segment:output.Value.Geometry.Segments)
                    {if(segment.ConstructionRole=="LeadIn")segment.ConstructionRole="LeadOut";else if(segment.ConstructionRole=="LeadOut")segment.ConstructionRole="LeadIn";}
                    if(const auto it=output.Value.Channels.find("fiveaxis.leads");it!=output.Value.Channels.end())
                    {auto leads=it->second.To<Map>();std::swap(leads.at("leadIn"),leads.at("leadOut"));it->second=std::move(leads);}
                }
                return candidate;
            };
            registry->Register(std::move(reverse));
            auto metadataEdit=*registry->Find("icax.edit.channels");const auto editChannels=metadataEdit.Evaluate;
            metadataEdit.TypeId="fiveaxis.metadata";
            metadataEdit.ParameterSchema={{"fields",Map{{"process",Map{{"type",Text("object")},{"required",false}}},
                {"beforeCommands",Map{{"type",Text("string")},{"required",false}}},{"afterCommands",Map{{"type",Text("string")},{"required",false}}}}}};
            metadataEdit.WriteChannels={"fiveaxis"};metadataEdit.PropagateUnwrittenChannels=true;metadataEdit.DeclaredOutputIdentityInputs={{"main",0}};
            metadataEdit.ValidateParameters=[](const Map& parameters){if(parameters.empty())throw std::invalid_argument("CAM_EMPTY_PATCH");};
            metadataEdit.Evaluate=[editChannels](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
            {
                auto metadata=ReadFiveAxisIntentMetadata(input.Inputs.at(0).Value);
                if(input.Parameters.contains("process"))metadata.Process=input.Parameters.at("process").To<Map>();
                if(input.Parameters.contains("beforeCommands"))metadata.BeforeCommands=String(input.Parameters,"beforeCommands");
                if(input.Parameters.contains("afterCommands"))metadata.AfterCommands=String(input.Parameters,"afterCommands");
                ++metadata.InputRevision;auto edit=input;edit.Parameters={{"patch",Map{{"fiveaxis",ToFiveAxisIntentChannel(metadata)}}}};
                return editChannels(edit,geometry);
            };
            registry->Register(std::move(metadataEdit));
            RegisterFiveAxisIntentOperations(*registry,[&scene](const Map& context)
            {
                if(context.contains("sourceOperationIds"))
                {
                    std::shared_ptr<const CFiveAxisIntentOperationContext> first;CFiveAxisIntentOperationContext::Bindings bindings;
                    for(const auto& item:context.at("sourceOperationIds").To<Array>()){auto current=FreezeIntentContext(scene,item.Is<ID>()?item.To<ID>():Identifier(Map{{"id",item}},"id"));if(!first)first=current;else if(first->FrozenModel()->Fingerprint()!=current->FrozenModel()->Fingerprint())throw std::invalid_argument("CAM_MERGE_SAME_WORKPIECE_REQUIRED");bindings.insert(current->SourceBindings().begin(),current->SourceBindings().end());}
                    if(!first)throw std::invalid_argument("CAM_MERGE_REQUIRES_TWO_PATHS");return std::make_shared<const CFiveAxisIntentOperationContext>(first->FrozenModel(),std::move(bindings),first->FrozenModel()->Fingerprint(),"fiveaxis-intent-services/1",first->Frame(),first->OutwardSign(),scene.Services().Resolve<iCAX::GeometryServices::IBoundaryLeadService>(),scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>(),first->Editing());
                }
                const auto& id=context.at("sourceOperationId");
                return FreezeIntentContext(scene,id.Is<ID>()?id.To<ID>():Identifier(context,"sourceOperationId"));
            });
            auto geometry=CreateFiveAxisIntentGeometryServices(scene.Services().Resolve<iCAX::Geometry::ICadReferenceService>(),
                scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>());
            runtime->IntentRegistry=registry;runtime->IntentGeometry=geometry;
            runtime->IntentDocument=PrepareFiveAxisIntentScene(scene,registry,geometry);
            runtime->PreparingIntents=false;runtime->IntentInputsChanged=false;
            // Opening a document restores authored definitions and cached
            // results. Missing/stale results wait for an explicit calculation.
        }
        catch(...){runtime->PreparingIntents=false;ReleaseIntentToolpaths(scene,runtime);throw;}
    }
    void PumpIntentGenerations(Scene& scene)
    {
        const auto runtime=Runtime(scene);if(!runtime->IntentDocument)return;
        if(runtime->IntentInputsChanged&&!scene.Database().IsWriteInProgress())
        {
            runtime->IntentInputsChanged=false;
            // Pose undo/redo obeys the same deferred contract as pose saves.
            if(!runtime->DeferredPoseInputsChanged)for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
                if(const auto setup=Component<CFiveAxisSetupComponent>(entity);setup&&!setup->GetIntentPlanID().is_nil())QueueIntentGeneration(scene,entity->GetID());
            runtime->DeferredPoseInputsChanged=false;
        }
        for(auto it=runtime->IntentGenerations.begin();it!=runtime->IntentGenerations.end();)
        {
            if(!it->second->IsReady()){++it;continue;}
            try
            {
                if(runtime->IntentDocument->Publish(it->second))
                {
                    if(const auto taskId=runtime->IntentGenerationTasks.find(it->first);taskId!=runtime->IntentGenerationTasks.end())
                    {
                        auto& task=runtime->Tasks.at(taskId->second);const auto resolvedSequence=runtime->Sequence;
                        auto result=runtime->IntentDocument->Resolve(it->first);
                        task.Status=task.Cancelled->load()?"Cancelled":result.Status==iCAX::CAM::Intent::EOutputCompleteness::Complete?"Completed":"Failed";
                        task.Result={{"pathCount",std::to_string(result.Paths.size())}};
                        if(!result.Diagnostics.empty())task.Error=result.Diagnostics.front().Message;
                        // Publication invalidated the old scene projections.
                        // Keep this fresh validated result for subsequent reads
                        // in the same unchanged scene; never refill the cache
                        // if a reentrant mutation occurred while resolving it.
                        if(result.Status==iCAX::CAM::Intent::EOutputCompleteness::Complete&&!task.Cancelled->load()
                            &&runtime->Sequence==resolvedSequence&&!scene.Database().IsWriteInProgress())
                            runtime->IntentResolutions.insert_or_assign(it->first,std::move(result));
                    }
                    it=runtime->IntentGenerations.erase(it);
                }
                else ++it;
            }
            catch(const std::exception& error)
            {
                if(const auto taskId=runtime->IntentGenerationTasks.find(it->first);taskId!=runtime->IntentGenerationTasks.end())
                {
                    auto& task=runtime->Tasks.at(taskId->second);task.Status=task.Cancelled->load()?"Cancelled":"Failed";
                    task.Error=error.what();
                }
                it=runtime->IntentGenerations.erase(it);
            }
        }
    }
    std::vector<Map> CommitIntentCommands(Scene& scene,const std::vector<Map>& commands,const std::set<ID>& setups,const Text& undoName,bool regenerate=true)
    {
        iCAX::CAM::Intent::SEditableToolpathHostEdit host;host.UndoName=undoName;
        host.Stage=[&](iCAX::Database::ITransaction& tx,const auto&)
        {
            std::set<ID> jobs;
            for(const auto& id:setups)
            {
                const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);jobs.insert(setup->GetJobID());
                tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"InputRevision",Next(setup->GetInputRevision())},
                    {"PlanValidity",Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});
            }
            for(const auto& id:jobs){const auto job=Require<CFiveAxisJobComponent>(scene.Database(),id);tx.ModifyComponent(id,CFiveAxisJobComponent::S_ClassName,{{"InputRevision",Next(job->GetInputRevision())}});}
        };
        auto receipts=IntentDocument(scene).ExecuteBatch(commands,host);
        Runtime(scene)->IntentInputsChanged=false;
        Runtime(scene)->DeferredPoseInputsChanged=false;
        // Organization edits change participation/order, not operation inputs.
        // Keep existing results and any already-running generation intact.
        if(regenerate)for(const auto& id:setups)QueueIntentGeneration(scene,id);
        return receipts;
    }
}
