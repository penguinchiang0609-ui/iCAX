// Scene owns policies, version fencing, persistence and result qualification.
// Geometry construction stays neutral; the planning worker additionally checks
// obstacles and searches lateral alternatives under immutable user parameters.
#include "FiveAxisCollisionRotation.inc"
struct RapidSnapshot
{
    std::vector<iCAX::GeometryServices::RapidPathPlanningQuery> Queries;
    Array Connections;
    Map Reused;
    Map SharedInputs,ReusableRecords;
    Text CollisionInputKey;
    std::vector<std::vector<iCAX::GeometryServices::TravelPoint>> PrecedingDirections;
    bool AllowReuse=false,Preview=false,GeometryOnly=false;
    Map Performance;
    double CalculationMs=0;
    double CollisionIndexPreparationMs=0;
    unsigned ParallelWorkers=1,PeakParallelTasks=0,PlanningPasses=0;
    std::size_t ReplannedCount=0;
};
namespace
{
    namespace RapidRotationMath=iCAX::GeometryServices::RigidMotion;
    struct RapidTransport {
        RapidRotationMath::Vector Previous{0,0,1};RapidRotationMath::Quaternion Rotation{0,0,0,1};
        void Append(const RapidRotationMath::Vector& value){const auto direction=RapidRotationMath::Unit(value);Rotation=CollisionRotation::Step(Previous,direction,Rotation);Previous=direction;}
    };
    Array RapidRotationValue(const RapidRotationMath::Quaternion& rotation){Array result;for(double value:rotation)result.emplace_back(value);return result;}
    Map GeneratedRapidEntries(Scene&,const ID&);
    Map RapidSettings(Scene&,const ID&);
    bool HasRapidEditingGeometry(const Map&);
    Map RapidSettings(const CFiveAxisSetupComponent& setup){
        auto settings=setup.GetRapidSettings();
        if(settings.empty())settings={{"strategy",Text("Quick")},{"followHeightMm",1.},{"liftHeightMm",10.},{"collisionThresholdMm",.1},{"marginMm",.2},
            {"envelopeRadiusMm",0.},{"envelopeLengthMm",0.},{"linearSpeedMmS",100.},{"linearAccelerationMmS2",1000.},{"angularSpeedRadS",1.},{"angularAccelerationRadS2",2.}};
        return settings;
    }
    void CheckRapidStrategy(const Text& strategy){if(strategy!="Quick"&&strategy!="Frog"&&strategy!="Fly")throw std::invalid_argument("CAM_RAPID_STRATEGY_INVALID");}
    bool IsRapidSafeCoordinate(const Text& key){return key=="safeZMm";}
    void ValidateRapidPolicy(const Map& p){
        CutFields(p,{"strategy","marginMm","liftHeightMm"});CheckRapidStrategy(String(p,"strategy"));
        const double margin=RegistrationNumber(p.at("marginMm")),lift=RegistrationNumber(p.at("liftHeightMm"));
        if(margin<0||margin>1000||lift<=0||lift>100000)throw std::invalid_argument("CAM_RAPID_PARAMETER_RANGE");
    }
    Map CanonicalRapidPolicy(const Map& policy){
        ValidateRapidPolicy(policy);
        return {{"strategy",String(policy,"strategy")},{"marginMm",RegistrationNumber(policy.at("marginMm"))},{"liftHeightMm",RegistrationNumber(policy.at("liftHeightMm"))}};
    }
    Map DefaultRapidPolicy(const Map& settings){return {{"strategy",settings.at("strategy")},{"marginMm",settings.at("marginMm")},{"liftHeightMm",settings.at("liftHeightMm")}};}
    Map DefaultConnectionPolicy(const Map& settings,const Map&){return DefaultRapidPolicy(settings);}
    Text RapidPair(const Map& connection){return String(connection,"from")+"/"+String(connection,"to");}
    iCAX::GeometryServices::RapidPathKind RapidConstructionKind(const Text& kind){
        using K=iCAX::GeometryServices::RapidPathKind;
        if(kind=="Retract")return K::Retract;if(kind=="Arch")return K::Arch;if(kind=="Follow")return K::Follow;
        throw std::invalid_argument("CAM_RAPID_CONSTRUCTION_KIND_INVALID");
    }
    Map RapidConstructionValue(const iCAX::GeometryServices::RapidPathConstruction& construction){
        using K=iCAX::GeometryServices::RapidPathKind;
        Map value{{"kind",Text(construction.Kind==K::Retract?"Retract":construction.Kind==K::Arch?"Arch":"Follow")},
            {"followHeightMm",construction.FollowHeight},{"liftHeightMm",construction.LiftHeight},{"lateralOffset",GenPoint(construction.LateralOffset)}};
        if(construction.TurnPoint)value["turnPoint"]=GenPoint(*construction.TurnPoint);return value;
    }
    iCAX::GeometryServices::RapidPathConstruction ReadRapidConstruction(const Map& value){
        CutFields(value,{"kind","followHeightMm","liftHeightMm","lateralOffset","turnPoint"});
        iCAX::GeometryServices::RapidPathConstruction construction;construction.Kind=RapidConstructionKind(String(value,"kind"));
        construction.FollowHeight=RegistrationNumber(value.at("followHeightMm"));construction.LiftHeight=RegistrationNumber(value.at("liftHeightMm"));construction.LateralOffset=RegistrationPoint(value.at("lateralOffset"));
        for(double x:{construction.FollowHeight,construction.LiftHeight})if(!std::isfinite(x)||x<=0||x>1e6)throw std::invalid_argument("CAM_RAPID_CONSTRUCTION_INVALID");
        for(double x:construction.LateralOffset)if(!std::isfinite(x)||std::abs(x)>1e6)throw std::invalid_argument("CAM_RAPID_CONSTRUCTION_INVALID");
        if(value.contains("turnPoint")){construction.TurnPoint=RegistrationPoint(value.at("turnPoint"));
            if(construction.Kind!=iCAX::GeometryServices::RapidPathKind::Retract||std::any_of(construction.LateralOffset.begin(),construction.LateralOffset.end(),[](double x){return x!=0;}))throw std::invalid_argument("CAM_RAPID_TURN_POINT_CONSTRUCTION_INVALID");
            for(double x:*construction.TurnPoint)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("CAM_RAPID_CONSTRUCTION_INVALID");
        }
        return construction;
    }
    Map RapidPoseValue(const iCAX::GeometryServices::TravelPose& pose){return {{"point",GenPoint(pose.Point)},{"direction",GenPoint(pose.Direction)}};}
    void AppendRapidDefinition(Map& value,const iCAX::GeometryServices::RapidPathGeometryResult& result){
        if(result.Poses.size()<2)return;
        value["sourceStart"]=RapidPoseValue(result.SourceStart);value["sourceEnd"]=RapidPoseValue(result.SourceEnd);
        if(result.Construction)value["construction"]=RapidConstructionValue(*result.Construction);
        if(result.ShapeHandle)value["shapeHandle"]=RapidPoseValue(*result.ShapeHandle);
    }
    Map ManualRapidPath(const Map& value){
        CutFields(value,{"construction","points","directions","start","end"});Map result;
        const auto validate=[](const Variant& position,const Variant& direction){const auto point=RegistrationPoint(position),axis=RegistrationPoint(direction);
            for(double x:point)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");
            double length=0;for(double x:axis)length+=x*x;if(!std::isfinite(length)||std::abs(length-1)>1e-8)throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");
            return Map{{"point",GenPoint(point)},{"direction",GenPoint(axis)}};};
        for(const auto* end:{"start","end"})if(value.contains(end)){const auto pose=value.at(end).To<Map>();CutFields(pose,{"point","direction"});result[end]=validate(pose.at("point"),pose.at("direction"));}
        if(value.contains("construction")){if(value.contains("points")||value.contains("directions"))throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");result["construction"]=RapidConstructionValue(ReadRapidConstruction(value.at("construction").To<Map>()));return result;}
        if(!value.contains("points")&&!value.contains("directions"))return result;
        const auto points=value.at("points").To<Array>(),directions=value.at("directions").To<Array>();
        if(points.size()!=directions.size()||points.size()>2048)throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");
        Array p,d;for(std::size_t i=0;i<points.size();++i){const auto point=RegistrationPoint(points[i]),axis=RegistrationPoint(directions[i]);
            for(double x:point)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");
            double length=0;for(double x:axis)length+=x*x;if(!std::isfinite(length)||std::abs(length-1)>1e-8)throw std::invalid_argument("CAM_RAPID_EDIT_POINTS_INVALID");
            p.emplace_back(GenPoint(point));d.emplace_back(GenPoint(axis));}
        result["points"]=std::move(p);result["directions"]=std::move(d);return result;
    }
    Text RapidInputDigest(const Text& input){
        // Hash every immutable input without storing the entire setup again
        // in each connection and in each cache map key.
        if(input.size()>std::numeric_limits<ULONG>::max())throw std::runtime_error("CAM_RAPID_INPUT_TOO_LARGE");
        unsigned char hash[32]{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),static_cast<ULONG>(input.size()),hash,sizeof(hash))<0)
            throw std::runtime_error("CAM_RAPID_INPUT_DIGEST_FAILED");
        constexpr char hex[]="0123456789abcdef";Text result="sha256:";for(auto byte:hash){result+=hex[byte>>4];result+=hex[byte&15];}return result;
    }
    Text RapidInputKeyWithRotation(const Map& shared,const Map& connection,const Map& policy,const Map& manual,const Array& rotation){
        Map inputs;for(const auto& [key,value]:shared)if(key!="endpoints"&&key!="orientationPrefixes"&&key!="cutDirections"&&key!="rapidDirections")inputs[key]=value;
        const auto& endpoints=std::get<Map>(shared.at("endpoints").m_Value);
        const auto& from=std::get<Map>(endpoints.at(String(connection,"from")).m_Value);const auto& to=std::get<Map>(endpoints.at(String(connection,"to")).m_Value);
        inputs["start"]=Map{{"placementId",from.at("placementId")},{"point",from.at("end")},{"direction",from.at("endDirection")}};
        inputs["end"]=Map{{"placementId",to.at("placementId")},{"point",to.at("start")},{"direction",to.at("startDirection")}};
        inputs["startRotation"]=rotation;inputs["pair"]=RapidPair(connection);inputs["policy"]=Map{{"strategy",policy.at("strategy")},{"liftHeightMm",RegistrationNumber(policy.at("liftHeightMm"))}};inputs["manualPath"]=manual;
        return RapidInputDigest(iCAX::Data::VariantSerializer::Serialize(inputs));
    }
    bool CertifiedRapidRecord(const Map& record,const Text& key){
        if(!record.contains("inputKey")||!record.contains("result")||String(record,"inputKey")!=key)return false;
        const auto result=record.at("result").To<Map>();return result.contains("geometryStatus")&&result.contains("validation")&&String(result,"geometryStatus")=="Generated"&&String(result,"validation")=="Clear"&&HasRapidEditingGeometry(result);
    }
    Text RapidCollisionInputKey(Scene& scene,const ID& setupID){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto settings=RapidSettings(*setup);
        return RapidInputDigest(iCAX::Data::VariantSerializer::Serialize(Map{{"algorithm",Text("RapidCollisionEvidenceV1")},{"head",EffectiveHeadDefinition(scene,*setup)},{"obstacles",FixtureInputKey(scene,setupID)},
            {"threshold",settings.at("collisionThresholdMm")},{"envelopeRadius",settings.at("envelopeRadiusMm")},{"envelopeLength",settings.at("envelopeLengthMm")}}));
    }
    void InvalidateRapidCollisionEvidence(Map& result){
        result["validation"]=Text("Unchecked");result.erase("certifiedClearanceMm");result["reason"]=Text("碰撞条件已变化，路径保留，待重新检测。");
    }
    void QualifyRapidCollisionEvidence(Scene& scene,const ID& setupID,const Map& record,Map& result,const Map& policy,std::optional<Text>& current){
        if(!result.contains("validation")||String(result,"validation")!="Clear")return;
        // Qualification belongs to collision evidence. A missing/invalid head
        // can invalidate a verdict, but must never discard generated geometry.
        if(!current){try{current=RapidCollisionInputKey(scene,setupID);}catch(const std::exception&){current=Text{};}}
        if(current->empty()||!record.contains("collisionInputKey")||String(record,"collisionInputKey")!=*current||!record.contains("collisionMarginMm")||RegistrationNumber(record.at("collisionMarginMm"))!=RegistrationNumber(policy.at("marginMm")))InvalidateRapidCollisionEvidence(result);
    }
    bool ReusableRapidCertificate(const Map& record,const Text& key,const RapidSnapshot& input,const Map& policy){
        return CertifiedRapidRecord(record,key)&&record.contains("collisionInputKey")&&String(record,"collisionInputKey")==input.CollisionInputKey&&record.contains("collisionMarginMm")&&RegistrationNumber(record.at("collisionMarginMm"))==RegistrationNumber(policy.at("marginMm"));
    }
    Map PreferRapidRecord(Scene& scene,const ID& setupID,const Map& authored,const Map& cached,const Text& key,const Map& policy,std::optional<Text>& collisionKey){
        if(!cached.contains(key))return authored;
        if(CertifiedRapidRecord(authored,key)){auto verdict=authored.at("result").To<Map>();QualifyRapidCollisionEvidence(scene,setupID,authored,verdict,policy,collisionKey);if(String(verdict,"validation")=="Clear")return authored;}
        return cached.at(key).To<Map>();
    }
    Map RapidSharedInputs(Scene& scene,const ID& setupID,const Map* geometryData=nullptr){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        Map endpoints,cutDirections,surfaces;
        const auto ownedGeometry=geometryData?Map{}:GeneratedGeometryBaseState(scene,setupID);const auto& geometry=geometryData?*geometryData:ownedGeometry;
        for(const auto& value:std::get<Array>(geometry.at("operations").m_Value)){
            const auto& op=std::get<Map>(value.m_Value);const auto p=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"));const auto matrix=RigidMatrix(p->GetTransform());
            surfaces[String(op,"placementId")]=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID())->GetGeometryRef();
            const auto transform=[&](const Variant& point,bool direction){const auto v=RegistrationPoint(point);Array result;
                for(int i=0;i<3;++i){double n=direction?0:matrix[i*4+3];for(int j=0;j<3;++j)n+=matrix[i*4+j]*v[j];result.emplace_back(n);}return result;};
            const auto& parts=std::get<Array>(op.at("parts").m_Value);const auto& first=std::get<Map>(parts.front().m_Value);const auto& last=std::get<Map>(parts.back().m_Value);
            endpoints[String(op,"intentId")]=Map{{"placementId",op.at("placementId")},{"start",transform(op.at("start"),false)},{"end",transform(op.at("end"),false)},
                {"startDirection",transform(std::get<Array>(first.at("directions").m_Value).front(),true)},{"endDirection",transform(std::get<Array>(last.at("directions").m_Value).back(),true)}};
            Array directions;for(const auto& part:parts)for(const auto& direction:part.To<Map>().at("directions").To<Array>())directions.emplace_back(transform(direction,true));
            cutDirections[String(op,"intentId")]=std::move(directions);
        }
        auto settings=RapidSettings(scene,setupID);for(const auto* key:{"collisionThresholdMm","marginMm","envelopeRadiusMm","envelopeLengthMm"})settings.erase(key);
        // Geometry identity contains no cutting head or collision environment.
        Map shared{{"algorithm",Text("RapidNeutralGeometryV13")},{"endpoints",endpoints},{"cutDirections",cutDirections},{"surfaces",surfaces},{"settings",settings}};
        const auto cached=GeneratedRapidEntries(scene,setupID),overrides=setup->GetRapidOverrides();const auto operations=geometry.at("operations").To<Array>(),transitions=geometry.at("transitions").To<Array>();
        Map prefixes,rapids,connections;for(const auto& value:transitions){const auto transition=value.To<Map>();connections[RapidPair(transition)]=transition;}RapidTransport transport;std::optional<Text> collisionKey;
        for(std::size_t i=0;i<operations.size();++i){const auto op=operations[i].To<Map>();
            if(op.contains("simulationStartFrame")){const auto seed=op.at("simulationStartFrame").To<Map>();
                if(seed.at("placementTransform")==Variant(Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"))->GetTransform())){
                    transport.Previous=RegistrationPoint(seed.at("direction"));const auto q=seed.at("rotation").To<Array>();for(int j=0;j<4;++j)transport.Rotation[j]=RegistrationNumber(q[j]);}}
            for(const auto& direction:cutDirections.at(String(op,"intentId")).To<Array>())transport.Append(RegistrationPoint(direction));
            if(i+1==operations.size())break;
            const Map connection{{"from",op.at("intentId")},{"to",operations[i+1].To<Map>().at("intentId")}};const auto pair=RapidPair(connection);
            prefixes[pair]=RapidRotationValue(transport.Rotation);Map record;if(overrides.contains(pair))record=overrides.at(pair).To<Map>();
            Map supplied;if(connections.contains(pair)){const auto transition=connections.at(pair).To<Map>();
                if(transition.contains("endpointStatus")&&String(transition,"endpointStatus")!="Current")continue;
                if(geometryData&&transition.contains("geometryStatus")){const auto status=String(transition,"geometryStatus");if((status=="Generated"||status=="Draft")&&HasRapidEditingGeometry(transition))supplied=transition;}
            }
            const auto policy=record.empty()?DefaultConnectionPolicy(RapidSettings(*setup),connection):record.at("policy").To<Map>();
            const auto manual=record.contains("manualPath")?record.at("manualPath").To<Map>():Map{};
            const auto key=RapidInputKeyWithRotation(shared,connection,policy,manual,prefixes.at(pair).To<Array>());
            record=PreferRapidRecord(scene,setupID,record,cached,key,policy,collisionKey);
            Map result;if(!supplied.empty())result=std::move(supplied);else if(record.contains("inputKey")&&record.contains("result")&&String(record,"inputKey")==key)result=record.at("result").To<Map>();else continue;
            if(!result.contains("geometryStatus")||!HasRapidEditingGeometry(result))continue;
            const auto status=String(result,"geometryStatus");
            if((status!="Generated"&&status!="Draft")||!result.contains("directions"))continue;
            rapids[pair]=result.at("directions");for(const auto& direction:result.at("directions").To<Array>())transport.Append(RegistrationPoint(direction));
        }
        shared["orientationPrefixes"]=std::move(prefixes);shared["rapidDirections"]=std::move(rapids);return shared;
    }
    Text RapidInputKey(Scene& scene,const ID& setupID,const Map& connection,const Map& policy,const Map& manual={},const Map* geometryData=nullptr,const Map* sharedInputs=nullptr){
        const auto owned=sharedInputs?Map{}:RapidSharedInputs(scene,setupID,geometryData);const auto& shared=sharedInputs?*sharedInputs:owned;
        return RapidInputKeyWithRotation(shared,connection,policy,manual,shared.at("orientationPrefixes").To<Map>().at(RapidPair(connection)).To<Array>());
    }
    Map GeneratedRapidEntries(Scene& scene,const ID& setupID){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        return setup->GetGeneratedRapidPaths().empty()?Map{}:setup->GetGeneratedRapidPaths().at("entries").To<Map>();
    }
    bool HasRapidEditingGeometry(const Map& value){
        if(!value.contains("points")||!value.contains("directions")||!value.contains("frame")||!value.contains("referencePoint")||String(value,"frame")!="Setup"||String(value,"referencePoint")!="ToolTip")return false;
        const auto& points=std::get<Array>(value.at("points").m_Value);const auto& directions=std::get<Array>(value.at("directions").m_Value);
        return points.size()>=2&&points.size()==directions.size();
    }
    void CopyRapidEditingGeometry(Map& target,const Map& source){
        // Geometry remains editable after invalidation; old clearance never does.
        for(const auto* name:{"points","directions","surfacePoints","heightsMm","frame","referencePoint","sourceStart","sourceEnd","construction","shapeHandle","lengthMm","estimatedSeconds"})
            if(source.contains(name))target[name]=source.at(name);
        target["editingOnly"]=true;target["productionQualified"]=false;target["machineValidation"]=Text("Unchecked");
    }
    void PopulateFailedRapidEditingGeometry(Scene&,const ID&,Map&);
    void ApplyRapidState(Scene& scene,const ID& setupID,Map& generated){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto overrides=setup->GetRapidOverrides();
        const auto cached=GeneratedRapidEntries(scene,setupID);
        // All connections belong to this same immutable Scene read snapshot.
        const auto sharedInputs=RapidSharedInputs(scene,setupID,&generated);
        Array transitions;UInt constructed=0,clear=0;std::optional<Text> collisionKey;
        for(const auto& value:std::get<Array>(generated.at("transitions").m_Value)){
            auto connection=value.To<Map>();const auto pair=RapidPair(connection);Map record;
            // A regenerated result may refresh a persisted manual policy.
            if(overrides.contains(pair))record=overrides.at(pair).To<Map>();
            const auto policy=record.empty()?DefaultConnectionPolicy(RapidSettings(*setup),connection):record.at("policy").To<Map>();ValidateRapidPolicy(policy);
            const auto manual=record.contains("manualPath")?record.at("manualPath").To<Map>():Map{};
            const auto key=(cached.empty()&&record.empty())?Text{}:RapidInputKey(scene,setupID,connection,policy,manual,nullptr,&sharedInputs);
            connection["inputKey"]=key;
            record=PreferRapidRecord(scene,setupID,record,cached,key,policy,collisionKey);
            // Preserve a previously generated automatic route as editable
            // data when its endpoint inputs change. Never pick arbitrarily
            // between different historical geometries for the same pair.
            if(!record.contains("result")){
                Map retained;bool ambiguous=false;
                for(const auto& [_,value]:cached){const auto candidate=value.To<Map>();
                    if(RapidPair(candidate)!=pair||!candidate.contains("result")||!HasRapidEditingGeometry(candidate.at("result").To<Map>()))continue;
                    if(retained.empty())retained=candidate;
                    else{const auto a=retained.at("result").To<Map>(),b=candidate.at("result").To<Map>();
                        if(a.at("points")!=b.at("points")||a.at("directions")!=b.at("directions")){ambiguous=true;break;}}
                }
                if(!ambiguous&&!retained.empty())record=std::move(retained);
            }
            connection["policy"]=policy;connection["strategy"]=policy.at("strategy");
            if(!manual.empty())connection["manualPath"]=manual;
            if(record.contains("inputKey")&&record.contains("result")&&String(record,"inputKey")==key&&String(connection,"endpointStatus")=="Current"){
                const auto& result=std::get<Map>(record.at("result").m_Value);for(const auto& [k,v]:result)connection[k]=v;
                QualifyRapidCollisionEvidence(scene,setupID,record,connection,policy,collisionKey);
            }else{
                connection["geometryStatus"]=Text(record.empty()?"Missing":"Stale");connection["validation"]=Text("Unchecked");connection["reason"]=Text(record.empty()?"CAM_RAPID_NOT_PLANNED":"CAM_RAPID_STALE");
                if(record.contains("result")&&HasRapidEditingGeometry(record.at("result").To<Map>()))CopyRapidEditingGeometry(connection,record.at("result").To<Map>());
                if(!manual.empty()){
                    if(record.contains("result"))CopyRapidEditingGeometry(connection,record.at("result").To<Map>());
                    connection["planningMethod"]=Text("Manual");
                    connection["reason"]=Text("手动路径已保留，需更新连接并重新检查碰撞。");
                }
            }
            if(String(connection,"geometryStatus")=="Generated")++constructed;
            if(String(connection,"validation")=="Clear")++clear;
            transitions.emplace_back(connection);
        }
        generated["transitions"]=transitions;generated["rapidClearCount"]=std::to_string(clear);generated["rapidGeneratedCount"]=std::to_string(constructed);
        generated["rapidScope"]=Text("RapidGeometry");generated["machineValidation"]=Text("Unchecked");
        generated["revision"]=GeneratedDataRevision(*setup);
        PopulateFailedRapidEditingGeometry(scene,setupID,generated);
    }
    iCAX::GeometryServices::TravelPoint RapidTransform(const Array& transform,const Variant& value,bool direction){
        const auto m=RigidMatrix(transform);const auto p=RegistrationPoint(value);std::array<double,3> result{};
        for(int i=0;i<3;++i){result[i]=direction?0:m[i*4+3];for(int j=0;j<3;++j)result[i]+=m[i*4+j]*p[j];}return result;
    }
    iCAX::GeometryServices::RapidPathPlanningQuery RapidWithStartRotation(iCAX::GeometryServices::RapidPathPlanningQuery query,const RapidRotationMath::Quaternion& desired){
        namespace M=RapidRotationMath;
        const auto base=M::Between({0,0,1},query.Geometry.Start.Direction),correction=M::Multiply({-base[0],-base[1],-base[2],base[3]},desired);
        const auto x=M::Rotate(correction,{1,0,0}),y=M::Rotate(correction,{0,1,0}),z=M::Rotate(correction,{0,0,1});
        const std::array<double,16> matrix{x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,0,0,0,1};
        auto collision=std::make_shared<iCAX::GeometryServices::RapidCollisionGeometry>(*query.Collision);const auto previous=collision->Calibration;
        collision->Calibration.fill(0);for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)collision->Calibration[r*4+c]+=matrix[r*4+k]*previous[k*4+c];
        query.Collision=std::move(collision);return query;
    }
    #include "FiveAxisBoxRapids.inc"
    Map RapidSettings(Scene& scene,const ID& id){
        auto settings=RapidSettings(*Require<CFiveAxisSetupComponent>(scene.Database(),id));
        if(!settings.contains("normalRetractDistanceMm"))settings["normalRetractDistanceMm"]=2.;
        if(!settings.contains("sideRetractDistanceMm"))settings["sideRetractDistanceMm"]=RegistrationNumber(settings.at("liftHeightMm"));
        if(!settings.contains("safeZMm")){const auto bounds=RapidSetupOuterBox(scene,id);settings["safeZMm"]=std::ceil((bounds.Max[2]+RegistrationNumber(settings.at("liftHeightMm")))*1000)/1000;}
        return settings;
    }
    std::shared_ptr<RapidSnapshot> CaptureRapids(Scene& scene,const ID& setupID,const Map& request,bool preview,bool geometryOnly=false,bool force=false,const Map* generatedOverride=nullptr){
        RequireMachiningSetup(scene,setupID);
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto ownedGenerated=generatedOverride?Map{}:GeneratedGeometryState(scene,setupID);const auto& generated=generatedOverride?*generatedOverride:ownedGenerated;
        if(generated.at("revision")!=request.at("geometryRevision"))throw std::invalid_argument("CAM_RAPID_GEOMETRY_REQUIRED");
        if(String(generated,"status")!="Current"){
            bool available=false;if(preview)for(const auto& value:std::get<Array>(generated.at("transitions").m_Value)){const auto& t=std::get<Map>(value.m_Value);
                if(t.at("from")==request.at("from")&&t.at("to")==request.at("to")&&String(t,"endpointStatus")=="Current")available=true;}
            if(!available)throw std::invalid_argument("CAM_RAPID_GEOMETRY_REQUIRED");
        }
        const auto settings=RapidSettings(scene,setupID),overrides=setup->GetRapidOverrides();auto input=std::make_shared<RapidSnapshot>();
        Map cached;if(!preview)cached=GeneratedRapidEntries(scene,setupID);
        std::map<ID,std::shared_ptr<const iCAX::GeometryData::BRepModel>> surfaces;
        std::shared_ptr<iCAX::GeometryServices::RapidCollisionGeometry> environment;Map head;
        if(!geometryOnly){
        head=EffectiveHeadDefinition(scene,*setup);environment=std::make_shared<iCAX::GeometryServices::RapidCollisionGeometry>();input->CollisionInputKey=RapidCollisionInputKey(scene,setupID);
        environment->Boxes=HeadCollisionBoxes(scene,head);AppendAdditionalHeadEnvelope(environment->Boxes,settings);
        // Keep the declared offsets, axes and extents. CAM transports the
        // head axis; solved machine roll is checked by machine simulation.
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setupID){const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
                environment->Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(entity->GetID()),w->GetGeometryRef(),RigidMatrix(p->GetTransform())));}
            if(const auto f=Component<CFiveAxisFixtureComponent>(entity);f&&f->GetSetupID()==setupID&&(f->GetEnabled()&&f->GetCollisionEnabled())){
                environment->Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(entity->GetID()),f->GetGeometryRef(),RigidMatrix(f->GetTransform())));}
        }
        }
        const auto sharedInputs=RapidSharedInputs(scene,setupID,&generated);
        input->SharedInputs=sharedInputs;input->AllowReuse=!preview&&!force;input->Preview=preview;input->GeometryOnly=geometryOnly;
        if(input->AllowReuse){input->ReusableRecords=cached;std::optional<Text> collisionKey;for(const auto& [_,value]:overrides){const auto record=value.To<Map>();if(record.contains("inputKey")){const auto key=String(record,"inputKey");input->ReusableRecords[key]=PreferRapidRecord(scene,setupID,record,cached,key,record.at("policy").To<Map>(),collisionKey);}}}
        if(geometryOnly&&input->AllowReuse){
            std::set<Text> currentClear;for(const auto& value:generated.at("transitions").To<Array>()){const auto t=value.To<Map>();if(t.contains("inputKey")&&String(t,"validation")=="Clear")currentClear.insert(String(t,"inputKey"));}
            for(auto& [key,value]:input->ReusableRecords){auto record=value.To<Map>();if(!record.contains("result"))continue;auto result=record.at("result").To<Map>();if(result.contains("validation")&&String(result,"validation")=="Clear"&&!currentClear.contains(key)){InvalidateRapidCollisionEvidence(result);record["result"]=std::move(result);value=std::move(record);}}
        }
        const auto cutDirections=sharedInputs.at("cutDirections").To<Map>(),rapidDirections=sharedInputs.at("rapidDirections").To<Map>();
        std::vector<iCAX::GeometryServices::TravelPoint> preceding;
        const auto& ops=std::get<Array>(generated.at("operations").m_Value);const auto& transitions=std::get<Array>(generated.at("transitions").m_Value);
        for(std::size_t i=0;i<transitions.size();++i){const auto& connection=std::get<Map>(transitions[i].m_Value);
            for(const auto& direction:cutDirections.at(String(ops.at(i).To<Map>(),"intentId")).To<Array>())preceding.push_back(RegistrationPoint(direction));
            if(preview&&(connection.at("from")!=request.at("from")||connection.at("to")!=request.at("to"))){
                const auto pair=RapidPair(connection);if(rapidDirections.contains(pair))for(const auto& direction:rapidDirections.at(pair).To<Array>())preceding.push_back(RegistrationPoint(direction));continue;
            }
            const auto pair=RapidPair(connection);auto policy=preview?request.at("policy").To<Map>():overrides.contains(pair)?std::get<Map>(overrides.at(pair).m_Value).at("policy").To<Map>():DefaultConnectionPolicy(settings,connection);ValidateRapidPolicy(policy);
            Map manual;if(preview&&request.contains("manualPath"))manual=ManualRapidPath(request.at("manualPath").To<Map>());
            else if(!preview&&overrides.contains(pair)){const auto& record=std::get<Map>(overrides.at(pair).m_Value);if(record.contains("manualPath"))manual=ManualRapidPath(record.at("manualPath").To<Map>());}
            if(manual.empty()&&connection.at("fromPlacementId")!=connection.at("toPlacementId")&&String(policy,"strategy")!="Quick")throw std::invalid_argument("CAM_RAPID_CROSS_WORKPIECE_REQUIRES_RETRACT");
            policy["marginMm"]=RegistrationNumber(policy.at("marginMm"));policy["liftHeightMm"]=RegistrationNumber(policy.at("liftHeightMm"));
            const auto inputKey=RapidInputKey(scene,setupID,connection,policy,manual,nullptr,&sharedInputs);
            const auto savedRotation=sharedInputs.at("orientationPrefixes").To<Map>().at(pair).To<Array>();RapidRotationMath::Quaternion initial{};for(int k=0;k<4;++k)initial[k]=savedRotation[k].To<double>();
            const auto& from=std::get<Map>(ops[i].m_Value);const auto& to=std::get<Map>(ops[i+1].m_Value);
            const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(from,"placementId"));
            const auto targetPlacement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(to,"placementId"));
            const auto& fromParts=std::get<Array>(from.at("parts").m_Value);const auto& toParts=std::get<Array>(to.at("parts").m_Value);
            const auto& last=std::get<Map>(fromParts.back().m_Value);const auto& first=std::get<Map>(toParts.front().m_Value);
            iCAX::GeometryServices::RapidPathGeometryQuery q;
            q.Start={RapidTransform(placement->GetTransform(),from.at("end"),false),RapidTransform(placement->GetTransform(),std::get<Array>(last.at("directions").m_Value).back(),true)};
            q.End={RapidTransform(targetPlacement->GetTransform(),to.at("start"),false),RapidTransform(targetPlacement->GetTransform(),std::get<Array>(first.at("directions").m_Value).front(),true)};
            if(!geometryOnly){RequireRigidHeadDirection(head,q.Start.Direction);RequireRigidHeadDirection(head,q.End.Direction);}
            const auto strategy=String(policy,"strategy");q.Kind=strategy=="Fly"?iCAX::GeometryServices::RapidPathKind::Follow:strategy=="Frog"?iCAX::GeometryServices::RapidPathKind::Arch:iCAX::GeometryServices::RapidPathKind::Retract;
            q.FollowHeight=RegistrationNumber(settings.at("followHeightMm"));q.LiftHeight=RegistrationNumber(policy.at("liftHeightMm"));
            // Endpoint overrides are TCP poses owned by this rapid. They never
            // rewrite either neighboring cut's authored endpoint.
            const auto endpoint=[&](const char* name,auto& target){if(!manual.contains(name))return;const auto pose=manual.at(name).To<Map>();target.Direction=RegistrationPoint(pose.at("direction"));target.Point=RegistrationPoint(pose.at("point"));for(int k=0;k<3;++k)target.Point[k]-=target.Direction[k]*q.FollowHeight;};
            endpoint("start",q.Start);endpoint("end",q.End);
            if((manual.contains("start")||manual.contains("end"))&&!manual.contains("points")&&!manual.contains("construction")&&strategy!="Quick"){q.Manual=true;q.ConstructedManual=true;}
            Map quickMotion;
            if(!manual.contains("points")&&!manual.contains("construction")&&strategy=="Quick"){
                quickMotion=SetRapidQuickWaypoints(q,RegistrationNumber(settings.at("normalRetractDistanceMm")),RegistrationNumber(settings.at("sideRetractDistanceMm")),RegistrationNumber(settings.at("safeZMm")));
            }
            if(manual.contains("points")||manual.contains("construction")){q.Manual=true;
                if(manual.contains("construction")){const auto construction=ReadRapidConstruction(manual.at("construction").To<Map>());
                    if(construction.FollowHeight!=q.FollowHeight)throw std::invalid_argument("CAM_RAPID_ENDPOINT_HEIGHT_LOCKED: 跟随高度由当前刀路端点确定，不能在形状编辑中修改。");
                    q.ConstructedManual=true;q.Kind=construction.Kind;q.LiftHeight=construction.LiftHeight;q.LateralOffset=construction.LateralOffset;q.TurnPoint=construction.TurnPoint;
                }else{const auto points=manual.at("points").To<Array>(),directions=manual.at("directions").To<Array>();for(std::size_t j=0;j<points.size();++j)q.Waypoints.push_back({RegistrationPoint(points[j]),RegistrationPoint(directions[j])});}
            }
            if((!q.Manual||q.ConstructedManual)&&q.Kind==iCAX::GeometryServices::RapidPathKind::Follow){
                const auto workpiece=placement->GetWorkpieceID();
                if(!surfaces.contains(workpiece)){const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),workpiece);surfaces[workpiece]=FixtureGeometry(scene,w->GetGeometryRef());}
                q.Surface=surfaces.at(workpiece);q.SurfaceTransform=RigidMatrix(placement->GetTransform());
            }
            q.LinearSpeed=RegistrationNumber(settings.at("linearSpeedMmS"));q.LinearAcceleration=RegistrationNumber(settings.at("linearAccelerationMmS2"));q.AngularSpeed=RegistrationNumber(settings.at("angularSpeedRadS"));q.AngularAcceleration=RegistrationNumber(settings.at("angularAccelerationRadS2"));
            auto query=iCAX::GeometryServices::RapidPathPlanningQuery{std::move(q),environment,geometryOnly?0.:RegistrationNumber(settings.at("collisionThresholdMm"))+RegistrationNumber(policy.at("marginMm"))};
            input->Queries.push_back(preview&&!geometryOnly?RapidWithStartRotation(std::move(query),initial):std::move(query));input->PrecedingDirections.push_back(std::move(preceding));preceding.clear();
            Map record{{"from",connection.at("from")},{"to",connection.at("to")},{"dependencyVersion",Text("8")},{"generation",Text(geometryOnly?"GeometryOnly":"CollisionChecked")},
                {"scope",Text(geometryOnly?"RuleBasedGeometry":String(head,"kind")=="Imported"?"ImportedHeadEnvelope":"BuiltinHeadEnvelope")},{"representation",Text(geometryOnly?"GeometryOnly":"DeclaredBoxCompoundMesh")},{"headBoxCount",std::to_string(geometryOnly?0:environment->Boxes.size())},{"meshDeflectionMm",CollisionMeshDeflectionMm},{"policy",policy},{"inputKey",inputKey}};if(!geometryOnly){record["collisionInputKey"]=input->CollisionInputKey;record["collisionMarginMm"]=RegistrationNumber(policy.at("marginMm"));}if(!manual.empty())record["manualPath"]=manual;if(!quickMotion.empty())record["quickMotion"]=quickMotion;input->Connections.emplace_back(std::move(record));
        }
        if(preview&&input->Queries.size()!=1)throw std::invalid_argument("CAM_RAPID_CONNECTION_MISSING");
        if(input->AllowReuse){bool complete=true;Map reused;
            for(const auto& value:input->Connections){const auto record=value.To<Map>();const auto key=String(record,"inputKey");
                if(!input->ReusableRecords.contains(key)){complete=false;break;}const auto candidate=input->ReusableRecords.at(key).To<Map>();
                if(!candidate.contains("result")){complete=false;break;}const auto result=candidate.at("result").To<Map>();
                if(!result.contains("geometryStatus")||String(result,"geometryStatus")!="Generated"||!HasRapidEditingGeometry(result)||(!geometryOnly&&!ReusableRapidCertificate(candidate,key,*input,record.at("policy").To<Map>()))){complete=false;break;}reused[RapidPair(record)]=candidate;
            }
            if(complete){input->Reused=std::move(reused);input->Queries.clear();input->Connections.clear();input->PrecedingDirections.clear();}
        }return input;
    }
    Map RapidResult(const iCAX::GeometryServices::RapidPathPlanningResult& result){
        Array points,directions,surface,heights;for(const auto& p:result.Poses){points.emplace_back(GenPoint(p.Point));directions.emplace_back(GenPoint(p.Direction));}
        for(const auto& p:result.SurfacePoints)surface.emplace_back(GenPoint(p));for(auto h:result.Heights)heights.emplace_back(h);
        Map value{{"points",points},{"directions",directions},{"surfacePoints",surface},{"heightsMm",heights},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},
            {"geometryStatus",Text(result.Generated?"Generated":"Failed")},{"validation",result.Status},{"reason",result.Reason},{"obstacleId",result.Obstacle},
            {"editingOnly",!result.Generated&&result.Poses.size()>=2},
            {"planningMethod",result.Method},{"candidateCount",std::to_string(result.Candidates)},{"detourOffset",GenPoint(result.DetourOffset)},{"certifiedClearanceMm",result.CertifiedClearance},
            {"lengthMm",result.Length},{"estimatedSeconds",result.EstimatedSeconds},
            {"scope",Text("ConstrainedHeadEnvelope")},{"machineValidation",Text("Unchecked")},{"productionQualified",false},{"laser",false}};
        AppendRapidDefinition(value,result);return value;
    }
    void PopulateFailedRapidEditingGeometry(Scene& scene,const ID& setupID,Map& generated){
        // Older projects persisted only the failed verdict. Construct just
        // those missing editing routes from current native endpoints; reading
        // never reruns avoidance or changes the saved project/qualification.
        if(!Require<CFiveAxisSetupComponent>(scene.Database(),setupID)->GetEnabled())return;
        const auto runtime=Runtime(scene);
        for(auto& value:std::get<Array>(generated.at("transitions").m_Value)){
            auto& connection=std::get<Map>(value.m_Value);
            if(String(connection,"geometryStatus")!="Failed"||String(connection,"endpointStatus")!="Current")continue;
            if(HasRapidEditingGeometry(connection)){connection["editingOnly"]=true;continue;}
            const auto key=String(connection,"inputKey");if(key.empty())continue;
            if(!runtime->RapidEditingGeometry.contains(key)){
                Map request{{"geometryRevision",generated.at("revision")},{"from",connection.at("from")},{"to",connection.at("to")},{"policy",connection.at("policy")}};
                if(connection.contains("manualPath"))request["manualPath"]=connection.at("manualPath");
                const auto input=CaptureRapids(scene,setupID,request,true,true,false,&generated);
                const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();
                const auto constructed=service->Generate({input->Queries.front().Geometry},{}).front();
                Map editing;
                if(constructed.Generated){iCAX::GeometryServices::RapidPathPlanningResult candidate;static_cast<iCAX::GeometryServices::RapidPathGeometryResult&>(candidate)=constructed;candidate.Generated=false;
                    const auto geometry=RapidResult(candidate);
                    CopyRapidEditingGeometry(editing,geometry);
                }
                if(runtime->RapidEditingGeometry.size()>=4096)runtime->RapidEditingGeometry.erase(runtime->RapidEditingGeometry.begin());
                runtime->RapidEditingGeometry[key]=std::move(editing);
            }
            for(const auto& [name,field]:runtime->RapidEditingGeometry.at(key))connection[name]=field;
        }
    }
    Map RapidSummary(const Map& connections,std::size_t calculated,std::size_t reused){
        UInt constructed=0,clear=0;for(const auto& [_,value]:connections){const auto result=value.To<Map>().at("result").To<Map>();constructed+=String(result,"geometryStatus")=="Generated";clear+=String(result,"validation")=="Clear";}
        return {{"connections",connections},{"clearCount",std::to_string(clear)},{"generatedCount",std::to_string(constructed)},{"count",std::to_string(connections.size())},
            {"calculatedCount",std::to_string(calculated)},{"reusedCount",std::to_string(reused)}};
    }
    std::shared_ptr<std::vector<iCAX::GeometryServices::RapidPathPlanningResult>> PlanRapidsSequential(RapidSnapshot& input,const iCAX::GeometryServices::IRapidPathPlanningService* collisionService,const iCAX::GeometryServices::IRapidPathGeometryService* geometryService,const std::function<bool()>& stop){
        auto results=std::make_shared<std::vector<iCAX::GeometryServices::RapidPathPlanningResult>>();Array calculated;Map reused;RapidTransport transport;
        for(std::size_t i=0;i<input.Queries.size();++i){
            if(stop&&stop())throw std::runtime_error("TRAVEL_CANCELLED");
            for(const auto& direction:input.PrecedingDirections.at(i))transport.Append(direction);
            auto record=input.Connections.at(i).To<Map>();const auto manual=record.contains("manualPath")?record.at("manualPath").To<Map>():Map{};
            const auto key=RapidInputKeyWithRotation(input.SharedInputs,record,record.at("policy").To<Map>(),manual,RapidRotationValue(transport.Rotation));record["inputKey"]=key;
            if(input.AllowReuse&&input.ReusableRecords.contains(key)){
                const auto candidate=input.ReusableRecords.at(key).To<Map>();
                const auto result=candidate.contains("result")?candidate.at("result").To<Map>():Map{};
                if(result.contains("geometryStatus")&&String(result,"geometryStatus")=="Generated"&&HasRapidEditingGeometry(result)&&(input.GeometryOnly||ReusableRapidCertificate(candidate,key,input,record.at("policy").To<Map>()))){
                    reused[RapidPair(record)]=candidate;for(const auto& direction:result.at("directions").To<Array>())transport.Append(RegistrationPoint(direction));continue;
                }
            }
            iCAX::GeometryServices::RapidPathPlanningResult result;
            auto prepared=input.Queries[i];
            input.PeakParallelTasks=1;input.PlanningPasses=1;
            if(input.GeometryOnly){
                const auto built=geometryService->Generate({prepared.Geometry},stop);if(built.size()!=1)throw std::runtime_error("CAM_RAPID_RESULT_INCOMPLETE");
                static_cast<iCAX::GeometryServices::RapidPathGeometryResult&>(result)=built.front();result.Status="Unchecked";result.Method=input.Queries[i].Geometry.Manual?"Manual":"RuleBased";
                if(result.Generated)result.Reason="CAM_RAPID_COLLISION_CHECK_REQUIRED";
            }else{
                const auto query=input.Preview?prepared:RapidWithStartRotation(std::move(prepared),transport.Rotation);
                const auto checked=collisionService->Plan({query},stop);if(checked.size()!=1)throw std::runtime_error("CAM_RAPID_RESULT_INCOMPLETE");result=checked.front();
            }
            QualifyQuickRapidMethod(record,result);
            if(result.Generated)for(const auto& pose:result.Poses)transport.Append(pose.Direction);
            calculated.emplace_back(std::move(record));results->push_back(std::move(result));
        }
        input.Connections=std::move(calculated);input.Reused=std::move(reused);return results;
    }
    std::shared_ptr<std::vector<iCAX::GeometryServices::RapidPathPlanningResult>> PlanTransportedRapids(RapidSnapshot& input,const iCAX::GeometryServices::IRapidPathPlanningService* collisionService,const iCAX::GeometryServices::IRapidPathGeometryService* geometryService,const std::function<bool()>& stop){
        if(input.Preview||input.ParallelWorkers==1)return PlanRapidsSequential(input,collisionService,geometryService,stop);
        using Result=iCAX::GeometryServices::RapidPathPlanningResult;
        struct Candidate{Map Record,Reused;Text Key;RapidRotationMath::Quaternion Rotation;std::optional<Result> Calculated;};
        std::vector<Candidate> candidates;std::vector<std::size_t> pending;
        const auto prefixes=input.SharedInputs.at("orientationPrefixes").To<Map>();
        for(std::size_t i=0;i<input.Queries.size();++i){
            Candidate candidate;candidate.Record=input.Connections.at(i).To<Map>();candidate.Key=String(candidate.Record,"inputKey");
            const auto rotation=prefixes.at(RapidPair(candidate.Record)).To<Array>();for(int k=0;k<4;++k)candidate.Rotation[k]=rotation[k].To<double>();
            candidates.push_back(std::move(candidate));pending.push_back(i);
        }
        const auto& reusable=input.ReusableRecords;
        // Plan independent connections together. Only cheap prefix arithmetic
        // follows execution order. A detour can change transported roll; retry
        // those affected connections in parallel before publishing any verdict.
        // The first inconsistent prefix is fixed on every pass, so convergence
        // is bounded by the number of connections, even for nonplanar loops.
        for(std::size_t pass=0;!pending.empty();++pass){
            if(pass>candidates.size())throw std::runtime_error("CAM_RAPID_TRANSPORT_NOT_CONVERGED");
            if(pass)input.ReplannedCount+=pending.size();++input.PlanningPasses;
            const auto peak=RapidParallel::Run(pending.size(),input.ParallelWorkers,stop,[&](std::size_t job,const auto& cancelled){
                const auto i=pending[job];auto& candidate=candidates[i];candidate.Reused.clear();candidate.Calculated.reset();
                if(input.AllowReuse&&reusable.contains(candidate.Key)){
                    const auto saved=reusable.at(candidate.Key).To<Map>();const auto result=saved.contains("result")?saved.at("result").To<Map>():Map{};
                    if(result.contains("geometryStatus")&&String(result,"geometryStatus")=="Generated"&&HasRapidEditingGeometry(result)&&(input.GeometryOnly||ReusableRapidCertificate(saved,candidate.Key,input,candidate.Record.at("policy").To<Map>()))){candidate.Reused=saved;return;}
                }
                Result result;
                auto prepared=input.Queries[i];
                if(input.GeometryOnly){
                    const auto built=geometryService->Generate({prepared.Geometry},cancelled);if(built.size()!=1)throw std::runtime_error("CAM_RAPID_RESULT_INCOMPLETE");
                    static_cast<iCAX::GeometryServices::RapidPathGeometryResult&>(result)=built.front();result.Status="Unchecked";result.Method=input.Queries[i].Geometry.Manual?"Manual":"RuleBased";
                    if(result.Generated)result.Reason="CAM_RAPID_COLLISION_CHECK_REQUIRED";
                }else{
                    const auto checked=collisionService->Plan({RapidWithStartRotation(std::move(prepared),candidate.Rotation)},cancelled);
                    if(checked.size()!=1)throw std::runtime_error("CAM_RAPID_RESULT_INCOMPLETE");result=checked.front();
                }
                QualifyQuickRapidMethod(candidate.Record,result);candidate.Calculated=std::move(result);
            });
            input.PeakParallelTasks=std::max(input.PeakParallelTasks,peak);pending.clear();RapidTransport transport;
            for(std::size_t i=0;i<candidates.size();++i){
                if(stop&&stop())throw std::runtime_error("TRAVEL_CANCELLED");
                for(const auto& direction:input.PrecedingDirections.at(i))transport.Append(direction);
                auto& candidate=candidates[i];const auto& record=candidate.Record;const auto manual=record.contains("manualPath")?record.at("manualPath").To<Map>():Map{};
                const auto actual=RapidInputKeyWithRotation(input.SharedInputs,record,record.at("policy").To<Map>(),manual,RapidRotationValue(transport.Rotation));
                if(candidate.Key!=actual){candidate.Key=actual;candidate.Rotation=transport.Rotation;candidate.Record["inputKey"]=actual;pending.push_back(i);}
                if(!candidate.Reused.empty())for(const auto& direction:candidate.Reused.at("result").To<Map>().at("directions").To<Array>())transport.Append(RegistrationPoint(direction));
                else if(candidate.Calculated->Generated)for(const auto& pose:candidate.Calculated->Poses)transport.Append(pose.Direction);
            }
        }
        auto results=std::make_shared<std::vector<Result>>();Array calculated;Map reused;
        for(auto& candidate:candidates){
            if(!candidate.Reused.empty())reused[RapidPair(candidate.Record)]=std::move(candidate.Reused);
            else{calculated.emplace_back(std::move(candidate.Record));results->push_back(std::move(*candidate.Calculated));}
        }
        input.Connections=std::move(calculated);input.Reused=std::move(reused);return results;
    }
    void FinishRapids(Scene& scene,SceneRuntime& runtime,ImportTask& task){
        try{const auto results=task.RapidWorker.Result();
            if(task.Cancelled->load())task.Status="Cancelled";
            else if(task.Sequence!=runtime.Sequence)task.Status="Superseded";
            else{
                const auto published=task.Kind=="RapidGenerate"?GeneratedRapidEntries(scene,task.SetupID):Map{};
                Map connections=task.RapidInput->Reused;for(std::size_t i=0;i<results->size();++i){auto record=task.RapidInput->Connections[i].To<Map>();auto result=RapidResult(results->at(i));for(const auto* name:{"generation","scope","representation","headBoxCount","meshDeflectionMm"})result[name]=record.at(name);if(record.contains("quickMotion"))result["quickMotion"]=record.at("quickMotion");
                    // A failed check updates its verdict, never destroys an
                    // already generated route with the same geometry inputs.
                    const auto key=String(record,"inputKey");if(!task.RapidInput->GeometryOnly&&String(result,"geometryStatus")=="Failed"&&published.contains(key)){
                        const auto previous=published.at(key).To<Map>().at("result").To<Map>();if(String(previous,"geometryStatus")=="Generated"&&HasRapidEditingGeometry(previous)){
                            for(const auto* name:{"points","directions","surfacePoints","heightsMm","frame","referencePoint","sourceStart","sourceEnd","construction","shapeHandle","lengthMm","estimatedSeconds"})if(previous.contains(name))result[name]=previous.at(name);
                            result["geometryStatus"]=Text("Generated");result["editingOnly"]=false;
                        }}record["result"]=result;connections[RapidPair(record)]=record;}
                task.Result=RapidSummary(connections,results->size(),task.RapidInput->Reused.size());
                task.Result["generation"]=Text(task.RapidInput->GeometryOnly?"GeometryOnly":"CollisionChecked");
                auto performance=task.RapidInput->Performance;performance["calculationMs"]=task.RapidInput->CalculationMs;
                performance["collisionIndexPreparationMs"]=task.RapidInput->CollisionIndexPreparationMs;
                performance["parallelWorkers"]=static_cast<double>(task.RapidInput->ParallelWorkers);performance["peakParallelTasks"]=static_cast<double>(task.RapidInput->PeakParallelTasks);
                performance["planningPasses"]=static_cast<double>(task.RapidInput->PlanningPasses);performance["replannedCount"]=static_cast<double>(task.RapidInput->ReplannedCount);task.Result["performance"]=performance;
                if(task.Kind=="RapidGenerate"){
                    // Cache by complete immutable inputs, not by the latest
                    // order. Undo can restore earlier constructed pairs.
                    auto entries=GeneratedRapidEntries(scene,task.SetupID);
                    std::set<Text> fresh;for(const auto& [_,value]:connections){const auto key=String(value.To<Map>(),"inputKey");entries[key]=value;fresh.insert(key);}
                    for(auto it=entries.begin();entries.size()>4096&&it!=entries.end();)if(!fresh.contains(it->first))it=entries.erase(it);else ++it;
                    Transaction(scene.Database(),"更新空移规划",[&](auto& tx){StoreGeneratedRapidData(tx,task.SetupID,entries,NewMachiningDataRevision());},false);
                }
                task.Status="Completed";
            }
        }catch(const std::exception& e){task.Status=task.Cancelled->load()?"Cancelled":"Failed";task.Error=e.what();}
        task.RapidWorker={};task.RapidInput.reset();
    }
    Map RapidCheckGeometry(Scene& scene,const ID& setupID,const Map& request){
        // A single-route check also needs all preceding directions: the head's
        // transported roll belongs to the machining sequence, not this pair.
        // The base projection already qualifies both endpoints and applies the
        // effective execution order in this same Scene snapshot. Reuse it;
        // resolving inputs and qualifying the prefix again only duplicates work.
        auto ordered=GeneratedGeometryBaseState(scene,setupID);
        if(!ordered.contains("operations"))throw std::invalid_argument("CAM_RAPID_GEOMETRY_REQUIRED");
        Array operations;bool valid=true,foundFrom=false,foundTo=false;
        for(auto& value:std::get<Array>(ordered.at("operations").m_Value)){
                const auto& source=std::get<Map>(value.m_Value);
                if(source.at("intentId")==request.at("from")){foundFrom=true;valid&=String(source,"endStatus")=="Current";}
                if(source.at("intentId")==request.at("to")){foundTo=true;valid&=String(source,"startStatus")=="Current";}
                operations.emplace_back(std::move(value));if(foundTo)break;
        }
        if(!foundFrom||!foundTo)throw std::invalid_argument("CAM_RAPID_CONNECTION_MISSING");
        auto transitions=OperationTransitions(operations);
        return {{"status",Text(valid?"Current":"Stale")},{"revision",ordered.at("revision")},{"operations",std::move(operations)},{"transitions",std::move(transitions)}};
    }
    Map StartRapids(Scene& scene,const Map& request,bool preview,bool checkOnly=false){
        if(preview)CutFields(request,{"commandId","expected","setupId","geometryRevision","from","to","policy","manualPath"});else CutFields(request,{"commandId","expected","setupId","geometryRevision","safe","maxParallelTasks"});
        const bool checked=preview||(request.contains("safe")&&request.at("safe").To<bool>());
        const auto hardware=std::max(1u,std::thread::hardware_concurrency());
        const auto workers=request.contains("maxParallelTasks")?Integer(request,"maxParallelTasks"):UInt(std::min(8u,hardware>1?hardware-1:1u));
        if(!workers||workers>32)throw std::invalid_argument("CAM_RAPID_PARALLEL_BUDGET_INVALID");
        if(checkOnly&&!request.contains("manualPath"))throw std::invalid_argument("CAM_RAPID_GEOMETRY_REQUIRED");
        const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text(checkOnly?"RapidCheck:":preview?"RapidPreview:":"RapidGenerate:")+iCAX::Data::VariantSerializer::Serialize(request);
        for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
        if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
        ImportTask task;task.Kind=checkOnly?"RapidCheck":preview?"RapidPreview":"RapidGenerate";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");
        const auto captureStarted=std::chrono::steady_clock::now();const auto hits=runtime->CollisionMeshHits,misses=runtime->CollisionMeshMisses;const auto meshMs=runtime->CollisionMeshPreparationMs;
        const auto diskHits=runtime->CollisionMeshDiskHits,diskMisses=runtime->CollisionMeshDiskMisses;const auto refinementMs=runtime->CollisionMeshRefinementMs;
        const auto checkGeometry=checkOnly?RapidCheckGeometry(scene,task.SetupID,request):Map{};
        task.RapidInput=CaptureRapids(scene,task.SetupID,request,preview,!checked,false,checkOnly?&checkGeometry:nullptr);
        task.RapidInput->ParallelWorkers=preview?1:static_cast<unsigned>(std::min<UInt>(workers,std::max<std::size_t>(1,task.RapidInput->Queries.size())));
        task.RapidInput->Performance={{"captureMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-captureStarted).count()},
            {"meshPreparationMs",runtime->CollisionMeshPreparationMs-meshMs},{"meshCacheHits",static_cast<double>(runtime->CollisionMeshHits-hits)},{"meshCacheMisses",static_cast<double>(runtime->CollisionMeshMisses-misses)},
            {"meshDiskCacheHits",static_cast<double>(runtime->CollisionMeshDiskHits-diskHits)},{"meshDiskCacheMisses",static_cast<double>(runtime->CollisionMeshDiskMisses-diskMisses)},
            {"meshRefinementMs",runtime->CollisionMeshRefinementMs-refinementMs}};
        if(task.RapidInput->Queries.empty()){
            task.Result=RapidSummary(task.RapidInput->Reused,0,task.RapidInput->Reused.size());task.Result["generation"]=Text(checked?"CollisionChecked":"GeometryOnly");task.Result["performance"]=task.RapidInput->Performance;task.Status="Completed";task.RapidInput.reset();
        }else{
            const auto collisionService=checked?scene.Services().Resolve<iCAX::GeometryServices::IRapidPathPlanningService>():nullptr;
            const auto geometryService=checked?nullptr:scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();
            const auto indexService=checked&&!preview&&task.RapidInput->ParallelWorkers>1?scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>():nullptr;
            if(checked&&!collisionService||!checked&&!geometryService)throw std::runtime_error("CAM_RAPID_SERVICE_UNAVAILABLE");
            task.RapidWorker=iCAX::Tasks::Run([input=task.RapidInput,collisionService,geometryService,indexService,cancelled=task.Cancelled]{
                const auto started=std::chrono::steady_clock::now();const std::function<bool()> stop=[&]{return cancelled->load();};
                // Populate immutable obstacle indices once before releasing the
                // queue. Concurrent cold misses otherwise build the same BVHs
                // per worker. Each route still creates its own mutable session.
                std::unique_ptr<iCAX::GeometryServices::IRigidMotionCollisionSession> prepared;
                if(indexService){iCAX::GeometryServices::RigidMotionCollisionQuery warm;const auto& environment=*input->Queries.front().Collision;
                    warm.Boxes=environment.Boxes;warm.Obstacles=environment.Obstacles;
                    prepared=indexService->CreatePrimitiveMeshSession(warm,stop);
                    input->CollisionIndexPreparationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                }
                auto result=PlanTransportedRapids(*input,collisionService.get(),geometryService.get(),stop);
                input->CalculationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;
            });
        }
        const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
    }
    template<class Action> Map RapidMutation(Scene& scene,const Map& request,const Text& operation,Action action){
        const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=operation+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
        for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
        const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);auto result=action(setupID,setup);
        runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
    }
    void RapidChange(Scene& scene,const ID& setupID,const iCAX::Data::PropertySet& fields,const Text& label){
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());auto changes=fields;
        changes["PlanValidity"]=Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale");
        Transaction(scene.Database(),label,[&](auto& tx){tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,changes);tx.ModifyComponent(setup->GetJobID(),CFiveAxisJobComponent::S_ClassName,{{"InputRevision",Next(job->GetInputRevision())}});});
    }
}
Map PreviewRapid(Scene& scene,const Map& request){return StartRapids(scene,request,true);}
Map CheckRapidPath(Scene& scene,const Map& request){return StartRapids(scene,request,true,true);}
Map ReadRapidEditPath(Scene& scene,const Map& request){
    CutFields(request,{"expected","setupId","geometryRevision","from","to","policy","manualPath"});CheckExpected(*Runtime(scene),request);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto overrides=setup->GetRapidOverrides();const auto pair=RapidPair(request);Map read=request;
    // A saved Polyline is an authored route, not a sampled approximation of an
    // automatic construction. Reading its editor must retain every point.
    if(!read.contains("manualPath")&&overrides.contains(pair)){const auto record=overrides.at(pair).To<Map>();if(record.contains("manualPath"))read["manualPath"]=record.at("manualPath");}
    const auto snapshot=CaptureRapids(scene,setupID,read,true,true);auto query=snapshot->Queries.front().Geometry;
    if(!read.contains("manualPath")){
        const auto key=String(snapshot->Connections.front().To<Map>(),"inputKey");const auto cached=GeneratedRapidEntries(scene,setupID);Map stored;
        if(overrides.contains(pair)){const auto record=overrides.at(pair).To<Map>();if(record.contains("inputKey")&&String(record,"inputKey")==key)stored=record;}
        std::optional<Text> collisionKey;stored=PreferRapidRecord(scene,setupID,stored,cached,key,snapshot->Connections.front().To<Map>().at("policy").To<Map>(),collisionKey);
        if(stored.contains("result")){auto result=stored.at("result").To<Map>();
            const auto runtime=Runtime(scene);
            if(String(result,"geometryStatus")=="Failed"&&!HasRapidEditingGeometry(result)&&runtime->RapidEditingGeometry.contains(key))
                for(const auto& [name,field]:runtime->RapidEditingGeometry.at(key))result[name]=field;
            if(HasRapidEditingGeometry(result)){
                Map value{{"points",result.at("points")},{"directions",result.at("directions")},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},{"endpointLocked",false},
                    {"sourceStart",RapidPoseValue(query.Start)},{"sourceEnd",RapidPoseValue(query.End)}};
                for(const auto* name:{"construction","shapeHandle","geometryStatus","validation","reason","editingOnly"})if(result.contains(name))value[name]=result.at(name);
                QualifyRapidCollisionEvidence(scene,setupID,stored,value,snapshot->Connections.front().To<Map>().at("policy").To<Map>(),collisionKey);
                return value;
            }
            if(result.contains("construction")){const auto construction=ReadRapidConstruction(result.at("construction").To<Map>());query.Kind=construction.Kind;query.LiftHeight=construction.LiftHeight;query.LateralOffset=construction.LateralOffset;query.TurnPoint=construction.TurnPoint;}
            else if(result.contains("detourOffset"))query.LateralOffset=RegistrationPoint(result.at("detourOffset"));
        }
    }
    const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();auto result=service->Generate({query},{}).front();
    if(!result.Generated)throw std::runtime_error(result.Reason);
    Array points,directions;for(const auto& pose:result.Poses){points.emplace_back(GenPoint(pose.Point));directions.emplace_back(GenPoint(pose.Direction));}
    Map value{{"points",points},{"directions",directions},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},{"endpointLocked",false}};AppendRapidDefinition(value,result);
    if(read.contains("manualPath")){Map endpoints;const auto manual=read.at("manualPath").To<Map>();for(const auto* name:{"start","end"})if(manual.contains(name))endpoints[name]=manual.at(name);value["endpointOverrides"]=std::move(endpoints);}return value;
}
Map GenerateRapids(Scene& scene,const Map& request){return StartRapids(scene,request,false);}
Map EditRapidPolicy(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","geometryRevision","from","to","policy","automatic"});
    return RapidMutation(scene,request,"EditRapidPolicy:",[&](const ID& id,const auto& setup){
        RequireSetupUnlocked(scene,id);RequireCutUnlocked(scene,Identifier(request,"from"));RequireCutUnlocked(scene,Identifier(request,"to"));
        const auto policy=CanonicalRapidPolicy(request.at("policy").To<Map>());
        const bool automatic=request.contains("automatic")&&request.at("automatic").To<bool>();
        const auto pair=RapidPair(request);auto overrides=setup->GetRapidOverrides();Map read=request;read["policy"]=policy;
        if(overrides.contains(pair)){
            const auto old=overrides.at(pair).To<Map>();
            if(!automatic&&old.contains("manualPath"))read["manualPath"]=old.at("manualPath");
            if(CanonicalRapidPolicy(old.at("policy").To<Map>())==policy&&(!automatic||!old.contains("manualPath")))return Map{{"changed",false}};
        }
        const auto input=CaptureRapids(scene,id,read,true,true);
        auto record=input->Connections.front().To<Map>();
        const auto service=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();
        const auto result=service->Generate({input->Queries.front().Geometry},{}).front();
        Array points,directions;for(const auto& pose:result.Poses){points.emplace_back(GenPoint(pose.Point));directions.emplace_back(GenPoint(pose.Direction));}
        record["result"]=Map{{"points",points},{"directions",directions},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},
            {"planningMethod",Text(read.contains("manualPath")?"Manual":String(policy,"strategy"))},
            {"geometryStatus",Text(result.Generated?"Draft":"Failed")},{"validation",Text("Unchecked")},
            {"reason",Text(result.Generated?"路径已保存，尚未通过碰撞检查。":result.Reason)}};
        auto draft=record.at("result").To<Map>();AppendRapidDefinition(draft,result);record["result"]=std::move(draft);
        overrides[pair]=record;RapidChange(scene,id,{{"RapidOverrides",overrides}},automatic?"恢复自动空移路径":"修改空移策略");
        return Map{{"changed",true}};
    });
}
Map EditRapidPath(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","geometryRevision","from","to","policy","manualPath"});
    return RapidMutation(scene,request,"EditRapidPath:",[&](const ID& id,const auto& setup){
        RequireSetupUnlocked(scene,id);RequireCutUnlocked(scene,Identifier(request,"from"));RequireCutUnlocked(scene,Identifier(request,"to"));
        const auto policy=CanonicalRapidPolicy(request.at("policy").To<Map>());
        const auto manual=ManualRapidPath(request.at("manualPath").To<Map>());
        // The unedited ends still follow machining geometry. Explicit endpoint
        // edits belong to this one rapid and are stored with its manual recipe.
        Map read=request;read.erase("commandId");const auto path=ReadRapidEditPath(scene,read);
        Map record{{"from",request.at("from")},{"to",request.at("to")},{"policy",policy},{"manualPath",manual}};
        const auto pair=RapidPair(record);auto overrides=setup->GetRapidOverrides();
        if(overrides.contains(pair)){const auto old=overrides.at(pair).To<Map>();
            if(old.contains("manualPath")&&old.at("manualPath")==Variant(manual)&&CanonicalRapidPolicy(old.at("policy").To<Map>())==policy)return Map{{"changed",false}};
        }
        record["inputKey"]=RapidInputKey(scene,id,record,policy,manual);
        record["result"]=Map{{"points",path.at("points")},{"directions",path.at("directions")},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},
            {"planningMethod",Text("Manual")},{"geometryStatus",Text("Draft")},{"validation",Text("Unchecked")},{"reason",Text("手动路径已保存，尚未通过碰撞检查。")}};
        auto draft=record.at("result").To<Map>();for(const auto* name:{"construction","shapeHandle","sourceStart","sourceEnd"})if(path.contains(name))draft[name]=path.at(name);record["result"]=std::move(draft);
        overrides[pair]=record;
        RapidChange(scene,id,{{"RapidOverrides",overrides}},"编辑空移路径");return Map{{"changed",true}};
    });
}
Map ConfigureRapids(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","settings"});return RapidMutation(scene,request,"ConfigureRapids:",[&](const ID& id,const auto& setup){
        auto settings=request.at("settings").To<Map>();const auto defaults=RapidSettings(scene,id);if(settings.size()!=defaults.size())throw std::invalid_argument("CAM_RAPID_SETTINGS_REQUIRED");
        for(const auto& [key,value]:settings){if(!defaults.contains(key))throw std::invalid_argument("CAM_RAPID_UNKNOWN_SETTING");if(key=="strategy"){CheckRapidStrategy(value.To<Text>());continue;}
            const auto n=RegistrationNumber(value);if(IsRapidSafeCoordinate(key)){if(std::abs(n)>1e8)throw std::invalid_argument("CAM_RAPID_SAFE_COORDINATE_RANGE");}
            else if(n<0||n>100000||(!key.starts_with("envelope")&&key!="marginMm"&&key!="collisionThresholdMm"&&n==0))throw std::invalid_argument("CAM_RAPID_PARAMETER_RANGE");}
        if((RegistrationNumber(settings.at("envelopeRadiusMm"))==0)!=(RegistrationNumber(settings.at("envelopeLengthMm"))==0))throw std::invalid_argument("CAM_RAPID_ENVELOPE_REQUIRED");
        for(auto& [key,value]:settings)if(key!="strategy")value=RegistrationNumber(value);
        ValidateRapidPolicy(DefaultRapidPolicy(settings));const bool changed=settings!=setup->GetRapidSettings();if(changed)RapidChange(scene,id,{{"RapidSettings",settings}},"修改空移设置");return Map{{"changed",changed}};
    });
}
Map ApplyRapid(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","taskId"});return RapidMutation(scene,request,"ApplyRapid:",[&](const ID& id,const auto& setup){
        const auto runtime=Runtime(scene);const auto found=runtime->Tasks.find(String(request,"taskId"));if(found==runtime->Tasks.end())throw std::invalid_argument("CAM_RAPID_PREVIEW_REQUIRED");
        const auto& task=found->second;if(task.SetupID!=id||task.Kind!="RapidPreview"||task.Status!="Completed"||task.Sequence!=runtime->Sequence)throw std::invalid_argument("CAM_RAPID_PREVIEW_STALE");
        const auto records=task.Result.at("connections").To<Map>();if(records.size()!=1)throw std::invalid_argument("CAM_RAPID_PREVIEW_REQUIRED");
        const auto& [pair,value]=*records.begin();const auto record=value.To<Map>();const auto manual=record.contains("manualPath")?record.at("manualPath").To<Map>():Map{};
        if(String(record,"inputKey")!=RapidInputKey(scene,id,record,record.at("policy").To<Map>(),manual))throw std::invalid_argument("CAM_RAPID_PREVIEW_STALE");
        if(!record.contains("collisionInputKey")||String(record,"collisionInputKey")!=RapidCollisionInputKey(scene,id))throw std::invalid_argument("CAM_RAPID_PREVIEW_STALE");
        if(String(record.at("result").To<Map>(),"geometryStatus")!="Generated"||String(record.at("result").To<Map>(),"validation")!="Clear")throw std::invalid_argument("CAM_RAPID_NO_VALID_ROUTE");
        RequireCutUnlocked(scene,Identifier(record,"from"));RequireCutUnlocked(scene,Identifier(record,"to"));
        // Only a constrained, collision-cleared route may be committed.
        auto overrides=setup->GetRapidOverrides();const bool changed=!overrides.contains(pair)||overrides.at(pair)!=value;
        if(changed){overrides[pair]=value;RapidChange(scene,id,{{"RapidOverrides",overrides}},"调整空移");}return Map{{"changed",changed},{"validation",record.at("result").To<Map>().at("validation")}};
    });
}
