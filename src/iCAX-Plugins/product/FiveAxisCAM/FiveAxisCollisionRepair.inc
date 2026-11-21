// A repair is an owner-defined, bounded local search over frozen values. No
// candidate publishes a resource or touches the repository. The selected
// window, its two joins, and a necessary adjacent rapid are the only geometry
// queries; other certificates are retained only for the same actual motion.
#include "FiveAxisRapidOrientationRepair.inc"
struct CollisionRepairSnapshot {
    SimulationCollisionSnapshot Collision;
    GenerationSnapshot Generation;
    std::shared_ptr<const RapidSnapshot> Rapids;
    Map Geometry,Report,Settings,Overrides;
    Array Operations,Transitions,Order;
    std::map<Text,Array> Placements;
    std::map<Text,bool> Locked;
    std::size_t Selected=0;
    double MaximumDegrees=15;
    double MaximumTranslationMm=0;
    bool KeyposeRepair=false;
    Text RepairPhase="All";
    Array RepairLocations,RepairOrigins;
    bool LocalDeviationAuthorized=false;
};
struct CollisionRepairResult {
    bool Solved=false;
    bool Verified=false;
    ID CandidateUndoID;
    Text VerificationKey;
    Map PoseEdits,Overrides;
    Array Operations,Transitions,Frames,Segments,Adjustments,Unresolved,CheckedIndices,CollisionControls;
    std::size_t Candidates=0,Reused=0,ChangedRapids=0,DistanceQueries=0;
    std::size_t FixedEntryFrame=0,FixedExitFrame=0,TransportSteps=0;
    Text RepairKind;
    Array RepairOrigins;
    Map Rejections;
    double MaximumAppliedDegrees=0,MaximumRotationCorrectionRad=0;
};
namespace {
    namespace RepairMath=iCAX::GeometryServices::RigidMotion;
    struct RepairSample { Text Key,Intent,Role,Pair;Map Anchor;RepairMath::Vector Axis{};std::size_t Operation=0,Part=0,Point=0; };
    struct RepairMotion { iCAX::GeometryServices::RigidMotionCollisionQuery Query;Array Frames;std::vector<RepairSample> Samples;double MaximumRotationCorrectionRad=0; };
    void RepairProgress(const std::shared_ptr<CollisionRepairProgress>& progress,const Text& stage,std::size_t candidates,double degrees,std::size_t checked=0,std::size_t total=0){
        std::lock_guard lock(progress->Mutex);progress->State={{"stage",stage},{"scope",Text("Local")},{"candidateCount",std::to_string(candidates)},{"maxDeviationDeg",degrees},
            {"completedIssueCount",Text("0")},{"totalIssueCount",Text("1")},{"checkedSegmentCount",std::to_string(checked)},{"completedSegmentCount",std::to_string(checked)},{"totalSegmentCount",std::to_string(total)}};
    }
    RepairMotion RepairTrajectory(const CollisionRepairSnapshot& input,const Array& operations,const Array& transitions){
        RepairMotion result;result.Query.Boxes=input.Collision.Query.Boxes;result.Query.BodyCalibration=input.Collision.Query.BodyCalibration;
        result.Query.Obstacles=input.Collision.Query.Obstacles;result.Query.MaxSegmentTimeMs=SimulationCollisionSegmentBudgetMs;
        std::vector<iCAX::GeometryServices::RigidMotionPose> poses;std::vector<bool> breaks;std::vector<double> clearances;
        RepairMath::Vector previous{0,0,1};RepairMath::Quaternion rotation{0,0,0,1};const auto height=RegistrationNumber(input.Settings.at("followHeightMm")),threshold=RegistrationNumber(input.Settings.at("collisionThresholdMm"));bool missing=false,seededFirst=false;
        const auto append=[&](const Map& part,const Array& transform,bool tcp,const Text& intent,const Variant& order,std::size_t op,std::size_t pi,const Text& pair,double clearance,bool force){
            const auto points=part.at("points").To<Array>(),directions=part.at("directions").To<Array>();
            if(points.empty()||points.size()!=directions.size())throw std::invalid_argument("CAM_REPAIR_PATH_INVALID");
            const auto source=part.contains("sourceSamples")?part.at("sourceSamples").To<Array>():Array{};
            for(std::size_t i=0;i<points.size();++i){const auto axis=RepairMath::Unit(RapidTransform(transform,directions[i],true));auto position=RapidTransform(transform,points[i],false);if(!tcp)position=RepairMath::Add(position,RepairMath::Scale(axis,height));
                if(!(seededFirst&&axis==previous))rotation=CollisionRotation::Step(previous,axis,rotation,&result.MaximumRotationCorrectionRad);seededFirst=false;const bool disconnected=!poses.empty()&&i==0&&(force||RepairMath::Length(RepairMath::Sub(position,poses.back().Position))>1e-4);
                poses.push_back({position,rotation});breaks.push_back(disconnected);clearances.push_back(clearance);previous=axis;
                result.Frames.emplace_back(Map{{"position",GenPoint(position)},{"rotation",Array{rotation[0],rotation[1],rotation[2],rotation[3]}},{"role",part.at("role")},{"intentId",intent},{"order",order}});
                RepairSample sample;sample.Intent=intent;sample.Role=String(part,"role");sample.Pair=pair;sample.Axis=axis;sample.Operation=op;sample.Part=pi;sample.Point=i;
                sample.Key=pair.empty()?intent+":"+std::to_string(pi)+":"+std::to_string(i):"rapid:"+pair+":"+std::to_string(i);
                if(i<source.size())
                {
                    const auto s=source[i].To<Map>();
                    const auto cut=std::find_if(input.Generation.Cuts.begin(),input.Generation.Cuts.end(),[&](const auto& cut){return cut.IDText==intent;});
                    if(s.contains("anchor"))sample.Anchor=s.at("anchor").To<Map>();
                    else if(cut!=input.Generation.Cuts.end()&&s.contains("segmentId")&&s.contains("nativeParameter"))
                        sample.Anchor=iCAX::CAM::Intent::ToData(iCAX::CAM::Intent::SPathAnchor{cut->IntentPath.Port,String(s,"segmentId"),RegistrationNumber(s.at("nativeParameter"))});
                }
                result.Samples.push_back(std::move(sample));
            }
        };
        for(std::size_t i=0;i<operations.size();++i){const auto op=operations[i].To<Map>();const auto parts=op.at("parts").To<Array>();
            if(op.contains("simulationStartFrame")){const auto seed=op.at("simulationStartFrame").To<Map>();if(seed.at("placementTransform")==Variant(input.Placements.at(String(op,"placementId")))){previous=RegistrationPoint(seed.at("direction"));const auto q=seed.at("rotation").To<Array>();for(int j=0;j<4;++j)rotation[j]=RegistrationNumber(q[j]);seededFirst=true;}}
            for(std::size_t j=0;j<parts.size();++j){append(parts[j].To<Map>(),input.Placements.at(String(op,"placementId")),false,String(op,"intentId"),op.at("order"),i,j,"",threshold,missing);missing=false;}
            if(i+1==operations.size())continue;const auto next=operations[i+1].To<Map>();bool found=false;
            for(const auto& v:transitions){auto t=v.To<Map>();if(t.at("from")!=op.at("intentId")||t.at("to")!=next.at("intentId"))continue;
                if((String(t,"geometryStatus")=="Generated"||String(t,"geometryStatus")=="Draft")&&t.contains("frame")&&String(t,"frame")=="Setup"&&t.contains("referencePoint")&&String(t,"referencePoint")=="ToolTip"&&t.at("points").To<Array>().size()>1){
                    t["role"]=Text("Rapid");append(t,Identity(),true,String(next,"intentId"),Text(String(op,"order")+" → "+String(next,"order")),i,0,RapidPair(t),threshold+RegistrationNumber(t.at("policy").To<Map>().at("marginMm")),false);found=true;}break;
            }missing=!found;
        }
        for(std::size_t i=0;i+1<poses.size();++i)result.Query.Segments.push_back({poses[i],poses[i+1],std::max(clearances[i],clearances[i+1]),!breaks[i+1]});
        if(result.Query.Segments.empty()&&!poses.empty())result.Query.Segments.push_back({poses[0],poses[0],clearances[0],true});return result;
    }
    bool RepairSamePose(const iCAX::GeometryServices::RigidMotionPose& a,const iCAX::GeometryServices::RigidMotionPose& b){
        if(a.Position!=b.Position)return false;if(a.Rotation==b.Rotation)return true;
        auto opposite=b.Rotation;for(auto& value:opposite)value=-value;return a.Rotation==opposite;
    }
    bool RepairSameCurve(const Map& a,const Map& b){
        const auto first=a.at("parts").To<Array>(),second=b.at("parts").To<Array>();if(first.size()!=second.size())return false;
        for(std::size_t i=0;i<first.size();++i){const auto x=first[i].To<Map>(),y=second[i].To<Map>();if(x.at("role")!=y.at("role")||x.at("points")!=y.at("points"))return false;}return true;
    }
    double RepairMaximumAngle(const Map& original,const Map& candidate,std::size_t* count=nullptr){
        double largest=0;std::size_t changed=0;const auto first=original.at("parts").To<Array>(),second=candidate.at("parts").To<Array>();
        for(std::size_t i=0;i<first.size();++i){const auto a=first[i].To<Map>().at("directions").To<Array>(),b=second[i].To<Map>().at("directions").To<Array>();if(a.size()!=b.size())return 1e100;
            for(std::size_t j=0;j<a.size();++j){const auto x=RegistrationPoint(a[j]),y=RegistrationPoint(b[j]);const auto delta=RepairMath::Length(RepairMath::Sub(x,y));const auto angle=2*std::asin(std::min(1.,delta/2));largest=std::max(largest,angle);changed+=delta>1e-12;}}
        if(count)*count=changed;return largest;
    }
    Array RepairDirections(const RepairMath::Vector& axis,double maximum){
        constexpr double pi=3.14159265358979323846;int k=0;for(int j=1;j<3;++j)if(std::abs(axis[j])<std::abs(axis[k]))k=j;
        RepairMath::Vector basis{};basis[k]=1;const auto u=RepairMath::Unit(RepairMath::Sub(basis,RepairMath::Scale(axis,RepairMath::Dot(axis,basis)))),v=RepairMath::Cross(axis,u);Array result;std::vector<double> angles;
        for(double degrees:{2.,5.,10.,15.,20.,30.})if(degrees*pi/180<=maximum+1e-12)angles.push_back(degrees*pi/180);
        if(maximum>1e-8&&(angles.empty()||maximum-angles.back()>1e-8))angles.push_back(maximum);
        for(auto angle:angles)for(unsigned i=0;i<8;++i){const auto azimuth=i*pi/4;const auto tangent=RepairMath::Add(RepairMath::Scale(u,std::cos(azimuth)),RepairMath::Scale(v,std::sin(azimuth)));
            result.emplace_back(GenPoint(RepairMath::Unit(RepairMath::Add(RepairMath::Scale(axis,std::cos(angle)),RepairMath::Scale(tangent,std::sin(angle))))));}return result;
    }
    Map RepairRapidResult(const Map& transition){
        Map result;for(const auto* key:{"points","directions","surfacePoints","heightsMm","frame","referencePoint","geometryStatus","validation","reason","obstacleId","planningMethod","candidateCount","detourOffset","certifiedClearanceMm","lengthMm","estimatedSeconds","scope","representation","headBoxCount","meshDeflectionMm","machineValidation","productionQualified","laser"})
            if(transition.contains(key))result[key]=transition.at(key);return result;
    }
    iCAX::GeometryData::ParameterRange RepairSegmentRange(const iCAX::CAM::Intent::SEditableSegment& segment)
    {return std::visit([](const auto& curve){if constexpr(std::is_same_v<std::decay_t<decltype(curve)>,iCAX::GeometryData::DirectionalCurveOffset3>)return curve.Source.Range;else return curve.Range;},segment.Curve);}
    double RepairAnchorPosition(const GenerationCut& cut,const Map& data)
    {
        const auto anchor=iCAX::CAM::Intent::AnchorFromData(data);const auto& geometry=cut.IntentPath.Value.Geometry;
        if(anchor.Port!=cut.IntentPath.Port)throw std::invalid_argument("CAM_REPAIR_ANCHOR_NOT_ON_INPUT");
        for(std::size_t i=0;i<geometry.Segments.size();++i)if(geometry.Segments[i].Id==anchor.SegmentId)
        {
            const auto range=RepairSegmentRange(geometry.Segments[i]);if(anchor.Parameter<range.First-1e-12||anchor.Parameter>range.Last+1e-12)break;
            const auto first=range.Reversed?range.Last:range.First,last=range.Reversed?range.First:range.Last;
            return i+(anchor.Parameter-first)/(last-first);
        }
        throw std::invalid_argument("CAM_REPAIR_ANCHOR_NOT_ON_INPUT");
    }
    bool RepairSameAnchor(const GenerationCut& cut,const Map& a,const Map& b)
    {return std::abs(RepairAnchorPosition(cut,a)-RepairAnchorPosition(cut,b))<1e-10;}
    Array RepairFieldStations(const GenerationCut& cut)
    {
        Array result;const auto& channels=cut.IntentPath.Value.Channels;
        if(channels.contains("poseControls"))for(const auto& value:channels.at("poseControls").To<Map>().at("entries").To<Array>())
        {const auto point=value.To<Map>();if(!point.contains("set")||String(point,"set")=="active")result.push_back(point.at("anchor"));}
        if(channels.contains("poseField"))for(const auto& value:channels.at("poseField").To<Map>().at("entries").To<Array>())
        {const auto range=iCAX::CAM::Intent::RangeFromData(value.To<Map>().at("range").To<Map>());result.emplace_back(iCAX::CAM::Intent::ToData(range.Start));result.emplace_back(iCAX::CAM::Intent::ToData(range.End));}
        return result;
    }
    struct RepairAxisStation { bool Valid=false;std::size_t Segment=0;double Parameter=0; };
    std::vector<std::vector<RepairAxisStation>> RepairAxisStations(const GenerationCut& input,const Map& operation){
        std::vector<std::vector<RepairAxisStation>> result;const auto parts=operation.at("parts").To<Array>();
        for(const auto& value:parts){const auto part=value.To<Map>();std::vector<RepairAxisStation> stations;
            for(const auto& sample:part.contains("sourceSamples")?part.at("sourceSamples").To<Array>():Array{}){const auto s=sample.To<Map>();RepairAxisStation station;
                if(s.contains("segmentId")&&s.contains("nativeParameter"))for(std::size_t index=0;index<input.IntentPath.Value.Geometry.Segments.size();++index)
                    if(input.IntentPath.Value.Geometry.Segments[index].Id==String(s,"segmentId"))
                    {station.Segment=index;station.Parameter=RegistrationNumber(s.at("nativeParameter"));station.Valid=true;break;}
                stations.push_back(std::move(station));
            }result.push_back(std::move(stations));
        }return result;
    }
    Map RepairOperationAxes(const GenerationCut& input,const std::vector<std::vector<RepairAxisStation>>& stations,const Map& original){
        CFiveAxisIntentPoseField field(*input.Context,input.IntentPath.Value);auto parts=original.at("parts").To<Array>();
        for(std::size_t i=0;i<parts.size();++i){if(stations[i].empty())continue;auto part=parts[i].To<Map>();auto directions=part.at("directions").To<Array>();if(stations[i].size()!=directions.size())throw std::invalid_argument("CAM_REPAIR_SOURCE_SAMPLES_REQUIRED");
            for(std::size_t j=0;j<directions.size();++j){const auto& at=stations[i][j];if(!at.Valid)throw std::invalid_argument("CAM_REPAIR_SOURCE_SAMPLES_REQUIRED");const auto evaluated=field.At(at.Segment,at.Parameter);const RepairMath::Vector direction{evaluated.X,evaluated.Y,evaluated.Z};
                // Retain unchanged source vectors bit-for-bit. The exact field
                // and its sampled representation can differ only by roundoff.
                if(RepairMath::Length(RepairMath::Sub(direction,RegistrationPoint(directions[j])))>1e-13)directions[j]=GenPoint(direction);
            }part["directions"]=directions;parts[i]=part;
        }
        auto operation=original;operation["parts"]=parts;return OperationInSetup(std::move(operation),input.Transform);
    }
    std::shared_ptr<CollisionRepairResult> CalculateCollisionRepair(const CollisionRepairSnapshot& input,
        const std::shared_ptr<iCAX::GeometryServices::ICurveOffset3DService>& offset,const std::shared_ptr<iCAX::GeometryServices::IRigidMotionCollisionService>& collision,
        const std::shared_ptr<iCAX::GeometryServices::IRapidPathPlanningService>& rapid,const std::shared_ptr<iCAX::GeometryServices::IRapidPathGeometryService>& rapidGeometry,
        const std::shared_ptr<std::atomic_bool>& cancelled,const std::shared_ptr<CollisionRepairProgress>& progress){
        auto output=std::make_shared<CollisionRepairResult>();output->Operations=input.Operations;output->Transitions=input.Transitions;output->Overrides=input.Overrides;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);const auto stop=[&]{return cancelled->load()||std::chrono::steady_clock::now()>deadline;};
        std::map<Text,std::size_t> rejected;const auto reject=[&](const Text& reason){++rejected[reason];};
        const auto unresolved=[&](const Text& reason){for(const auto& [key,count]:rejected)output->Rejections[key]=std::to_string(count);output->Unresolved={Map{{"index",std::to_string(input.Selected)},{"reason",reason}}};return output;};
        const auto baseline=RepairTrajectory(input,input.Operations,input.Transitions);if(baseline.Frames!=input.Collision.Frames)return unresolved("CAM_REPAIR_SOURCE_POSES_CHANGED");
        const auto oldSegments=input.Report.at("segments").To<Array>();if(input.Selected>=oldSegments.size()||input.Selected>=baseline.Query.Segments.size())return unresolved("CAM_REPAIR_SEGMENT_NOT_FOUND");
        const auto selected=oldSegments[input.Selected].To<Map>();if(String(selected,"status")!="Collision")return unresolved("CAM_REPAIR_SELECT_COLLISION");
        const auto fraction=RegistrationNumber(selected.at("fraction"));auto target=std::min(input.Selected+(fraction>=.5?1:0),baseline.Samples.size()-1);auto station=baseline.Samples[target];
        std::size_t first=input.Selected,last=input.Selected;while(first&&String(oldSegments[first-1].To<Map>(),"status")=="Collision"&&baseline.Samples[first-1].Intent==station.Intent&&baseline.Samples[first-1].Pair==station.Pair)--first;
        while(last+1<oldSegments.size()&&String(oldSegments[last+1].To<Map>(),"status")=="Collision"&&baseline.Samples[last+1].Intent==station.Intent&&baseline.Samples[last+1].Pair==station.Pair)++last;
        std::set<Text> allowed;Text rapidPair=station.Pair;std::vector<RepairSample> blockedControls;
        // A cutting operation (or one rapid) owns its interior. Its two full
        // endpoint poses belong to the existing joins and stay fixed.
        const auto samePath=[&](const RepairSample& sample){return sample.Operation==station.Operation&&sample.Pair==rapidPair;};
        std::size_t pathFirst=target,pathLast=target;
        while(pathFirst&&samePath(baseline.Samples[pathFirst-1]))--pathFirst;
        while(pathLast+1<baseline.Samples.size()&&samePath(baseline.Samples[pathLast+1]))++pathLast;
        std::size_t entryFrame=pathFirst,closureFrame=pathLast;
        const auto poseAt=[&](std::size_t i)->const iCAX::GeometryServices::RigidMotionPose&{
            return i<baseline.Query.Segments.size()?baseline.Query.Segments[i].Start:baseline.Query.Segments.back().End;
        };
        if(rapidPair.empty()){
            const auto cut=std::find_if(input.Generation.Cuts.begin(),input.Generation.Cuts.end(),[&](const auto& c){return c.IDText==station.Intent;});
            if(cut==input.Generation.Cuts.end())return unresolved("CAM_REPAIR_SOURCE_ANCHOR_REQUIRED");
            if(input.Locked.at(station.Intent))return unresolved("CAM_REPAIR_LOCKED_CUT");
            if(!input.LocalDeviationAuthorized)for(const auto* key:{"angularToleranceRad","leadDeviationLimitRad","sideDeviationLimitRad"})if(RegistrationNumber(cut->Metadata.Pose.at(key))<1e-8)return unresolved("CAM_REPAIR_POSE_LIMIT_ZERO");
            const auto controls=RepairFieldStations(*cut);
            std::vector<double> coordinates;for(const auto& at:controls)coordinates.push_back(RepairAnchorPosition(*cut,at.To<Map>()));
            std::sort(coordinates.begin(),coordinates.end());
            struct Control {std::size_t First,Last;};std::vector<Control> points;
            for(std::size_t i=pathFirst;i<=pathLast;++i){bool control=i==pathFirst||i==pathLast;
                if(!baseline.Samples[i].Anchor.empty()){
                    const auto position=RepairAnchorPosition(*cut,baseline.Samples[i].Anchor);
                    const auto found=std::lower_bound(coordinates.begin(),coordinates.end(),position-1e-10);control=control||(found!=coordinates.end()&&std::abs(*found-position)<1e-10);
                }
                if(!control)continue;
                if(!points.empty()&&points.back().Last+1==i&&RepairSamePose(poseAt(points.back().Last),poseAt(i)))points.back().Last=i;
                else points.push_back({i,i});
            }
            std::size_t right=0;while(right<points.size()&&points[right].Last<input.Selected+fraction)++right;
            if(right>=points.size())right=points.size()-1;
            const auto left=right&&points[right].First>input.Selected+fraction?right-1:right;
            // The full check visits controls before intervals. Classify the
            // selected bracket and immediate neighbors first;
            // colliding blocks lazily inspect controls until clear bounds.
            const auto checkFirst=left?left-1:0,checkLast=std::min(right+1,points.size()-1);
            auto keyQuery=baseline.Query;keyQuery.Segments.clear();
            for(std::size_t i=checkFirst;i<=checkLast;++i){const auto& point=points[i];const auto pose=poseAt(point.First);keyQuery.Segments.push_back({pose,pose,baseline.Query.Segments[std::min(point.First,baseline.Query.Segments.size()-1)].Clearance,true});}
            RepairProgress(progress,"CheckingKeypoints",0,input.MaximumDegrees,0,keyQuery.Segments.size());
            const auto checked=collision->Check(keyQuery,stop,[&](auto completed,auto total){RepairProgress(progress,"CheckingKeypoints",0,input.MaximumDegrees,completed,total);});
            if(checked.Segments.size()!=keyQuery.Segments.size())return unresolved("CAM_REPAIR_KEYPOINT_CHECK_INCOMPLETE");
            std::map<std::size_t,Text> controlStatus;
            for(std::size_t i=checkFirst;i<=checkLast;++i)controlStatus[i]=checked.Segments.at(i-checkFirst).Status;
            const auto status=[&](std::size_t i)->Text{
                if(const auto found=controlStatus.find(i);found!=controlStatus.end())return found->second;
                if(stop()||controlStatus.size()>=128)return "Budget";
                auto query=baseline.Query;const auto pose=poseAt(points.at(i).First);
                query.Segments={{pose,pose,baseline.Query.Segments[std::min(points[i].First,baseline.Query.Segments.size()-1)].Clearance,true}};
                RepairProgress(progress,"CheckingKeypoints",0,input.MaximumDegrees,controlStatus.size(),controlStatus.size()+1);
                const auto result=collision->Check(query,stop);return controlStatus[i]=result.Segments.size()==1?result.Segments.front().Status:Text("Unchecked");
            };
            std::optional<std::size_t> blocked;
            for(const auto i:{left,right})if(status(i)=="Collision"&&(!blocked||std::abs(double(points[i].First)-target)<std::abs(double(points[*blocked].First)-target)))blocked=i;
            if(blocked){
                auto low=*blocked,high=*blocked;
                for(;;){if(!low)return unresolved("CAM_REPAIR_FIXED_ENDPOINT_COLLISION");const auto s=status(low-1);if(s=="Clear")break;
                    if(s!="Collision")return unresolved(s=="Budget"?"CAM_REPAIR_CONTROL_BLOCK_BUDGET":"CAM_REPAIR_KEYPOINT_CHECK_INCOMPLETE");
                    if(high-low+1>=64)return unresolved("CAM_REPAIR_CONTROL_BLOCK_BUDGET");--low;
                }
                for(;;){if(high+1>=points.size())return unresolved("CAM_REPAIR_FIXED_ENDPOINT_COLLISION");const auto s=status(high+1);if(s=="Clear")break;
                    if(s!="Collision")return unresolved(s=="Budget"?"CAM_REPAIR_CONTROL_BLOCK_BUDGET":"CAM_REPAIR_KEYPOINT_CHECK_INCOMPLETE");
                    if(high-low+1>=64)return unresolved("CAM_REPAIR_CONTROL_BLOCK_BUDGET");++high;
                }
                entryFrame=points[low-1].Last;closureFrame=points[high+1].First;target=points[*blocked].First;station=baseline.Samples[target];output->RepairKind=low==high?"Keypoint":"KeypointBlock";
                for(auto i=low;i<=high;++i){const auto& sample=baseline.Samples[points[i].First];if(sample.Anchor.empty())return unresolved("CAM_REPAIR_SOURCE_ANCHOR_REQUIRED");
                    blockedControls.push_back(sample);Text existing;const auto sets=ProjectFiveAxisPoseControls(cut->IntentPath.Value);
                    if(sets.contains("active"))for(const auto& value:sets.at("active").To<Array>()){const auto point=value.To<Map>();if(String(point,"kind")=="Control"&&RepairSameAnchor(*cut,sample.Anchor,point.at("anchor").To<Map>())){existing=String(point,"id");break;}}
                    output->CollisionControls.emplace_back(Map{{"sourceFrameIndex",std::to_string(points[i].First)},{"sourceAnchor",sample.Anchor},{"existingPointId",existing}});
                }
                output->FixedEntryFrame=entryFrame;output->FixedExitFrame=closureFrame;
            }else{
                if(left==right||status(left)!="Clear"||status(right)!="Clear")return unresolved("CAM_REPAIR_KEYPOINT_CHECK_UNRESOLVED");
                entryFrame=points[left].Last;closureFrame=points[right].First;output->RepairKind="Interpolation";
                if(target<=entryFrame||target>=closureFrame||baseline.Samples[target].Anchor.empty()){
                    target=entryFrame+1;while(target<closureFrame&&baseline.Samples[target].Anchor.empty())++target;
                    if(target>=closureFrame)return unresolved("CAM_REPAIR_INTERIOR_SAMPLE_REQUIRED");station=baseline.Samples[target];
                }
            }
            // A block is bounded by the nearest proven Clear controls. Every
            // interval inside it is required to pass, including an unchanged
            // direction parallel to a trial rotor; its old alarm is not reused.
            first=output->RepairKind=="KeypointBlock"?entryFrame:std::max(input.Selected,entryFrame);
            last=output->RepairKind=="KeypointBlock"?closureFrame-1:std::min(input.Selected,closureFrame-1);
        }else output->RepairKind="RapidInterior";
        for(std::size_t i=entryFrame+1;i<closureFrame;++i)allowed.insert(baseline.Samples[i].Key);
        if(allowed.empty())return unresolved("CAM_REPAIR_NO_EDITABLE_INTERIOR");
        output->FixedEntryFrame=entryFrame;output->FixedExitFrame=closureFrame;
        const auto boundaryRoll=[&](const auto& axisAt){
            // Start from the captured full entry pose, not the first cut of
            // the setup. Solve only this interior and require its exit to
            // close onto the captured full exit pose. Earlier cuts/rapids
            // cannot contribute work to a candidate's transport calculation.
            auto previous=baseline.Samples[entryFrame].Axis;auto after=poseAt(entryFrame).Rotation;
            for(std::size_t i=entryFrame+1;i<=closureFrame;++i){const auto axis=axisAt(baseline.Samples[i]);after=CollisionRotation::Step(previous,axis,after);previous=axis;++output->TransportSteps;}
            const auto before=poseAt(closureFrame).Rotation;
            const auto delta=RepairMath::Multiply(after,{-before[0],-before[1],-before[2],before[3]});const auto axis=baseline.Samples[closureFrame].Axis;
            return 2*std::atan2(delta[0]*axis[0]+delta[1]*axis[1]+delta[2]*axis[2],delta[3]);
        };
        const auto validate=[&](const Array& operations,const Array& transitions,RepairMotion& motion)->bool{
            if(stop())return false;motion=RepairTrajectory(input,operations,transitions);std::map<Text,std::size_t> oldFrames;for(std::size_t i=0;i<baseline.Samples.size();++i)oldFrames[baseline.Samples[i].Key]=i;
            // Closing the actual quaternion at the boundary is mandatory. A
            // direction-only match would miss downstream transport holonomy.
            for(std::size_t i=0;i<motion.Samples.size();++i)if(!allowed.contains(motion.Samples[i].Key)){
                const auto found=oldFrames.find(motion.Samples[i].Key);if(found==oldFrames.end())return false;
                const auto& old=baseline.Samples[found->second];if(old.Axis!=motion.Samples[i].Axis){reject("OutsideDirectionChanged");return false;}
                const auto a=RegistrationPoint(std::get<Map>(baseline.Frames[found->second].m_Value).at("position")),b=RegistrationPoint(std::get<Map>(motion.Frames[i].m_Value).at("position"));
                const auto qa=std::get<Map>(baseline.Frames[found->second].m_Value).at("rotation").To<Array>(),qb=std::get<Map>(motion.Frames[i].m_Value).at("rotation").To<Array>();
                const iCAX::GeometryServices::RigidMotionPose pa{a,{qa[0].To<double>(),qa[1].To<double>(),qa[2].To<double>(),qa[3].To<double>()}},pb{b,{qb[0].To<double>(),qb[1].To<double>(),qb[2].To<double>(),qb[3].To<double>()}};
                if(!RepairSamePose(pa,pb)){reject("BoundaryQuaternionNotClosed");return false;}
            }
            std::map<std::pair<Text,Text>,std::size_t> oldIndices;for(std::size_t i=0;i<baseline.Query.Segments.size();++i)oldIndices[{baseline.Samples[i].Key,baseline.Samples[std::min(i+1,baseline.Samples.size()-1)].Key}]=i;
            std::set<std::size_t> changedSegments;
            for(std::size_t i=0;i<motion.Query.Segments.size();++i){const auto end=std::min(i+1,motion.Samples.size()-1);const auto found=oldIndices.find({motion.Samples[i].Key,motion.Samples[end].Key});const auto& segment=motion.Query.Segments[i];bool same=false;
                if(found!=oldIndices.end()){const auto& old=baseline.Query.Segments[found->second];same=old.Connected==segment.Connected&&old.Clearance==segment.Clearance&&RepairSamePose(old.Start,segment.Start)&&RepairSamePose(old.End,segment.End);}
                if(!same){if(!segment.Connected){reject("ChangedDisconnectedInterval");return false;}changedSegments.insert(i);}
                if(found!=oldIndices.end()&&found->second>=first&&found->second<=last)changedSegments.insert(i);
            }
            // The allowance bounds candidate search. It is not the check set:
            // certify changed motion plus its two connected joins; unchanged
            // missing connectors and unrelated alarms retain their exact rows.
            auto checkedSegments=changedSegments;for(auto i:changedSegments){if(i&&motion.Query.Segments[i-1].Connected)checkedSegments.insert(i-1);if(i+1<motion.Query.Segments.size()&&motion.Query.Segments[i+1].Connected)checkedSegments.insert(i+1);}
            iCAX::GeometryServices::RigidMotionCollisionQuery query=motion.Query;query.Segments.clear();std::vector<std::size_t> indices;
            for(auto i:checkedSegments){if(!motion.Query.Segments[i].Connected){reject("DisconnectedLocalInterval");return false;}query.Segments.push_back(motion.Query.Segments[i]);indices.push_back(i);}
            if(query.Segments.empty())return false;
            // Cheap actual witness-pose screening precedes the full local
            // continuous query. It never authorizes a repair by itself.
            const auto witnessPose=motion.Query.Segments[std::min(target,motion.Query.Segments.size()-1)].Start;auto pointQuery=query;pointQuery.Segments={{witnessPose,witnessPose,motion.Query.Segments[std::min(input.Selected,motion.Query.Segments.size()-1)].Clearance,true}};
            const auto pointResult=collision->Check(pointQuery,stop);if(pointResult.Segments.size()!=1||pointResult.Segments.front().Status!="Clear"){reject("WitnessPoseStillBlocked");return false;}
            RepairProgress(progress,"Verifying",output->Candidates,input.MaximumDegrees,0,indices.size());
            const auto checked=collision->Check(query,stop,[&](auto completed,auto total){RepairProgress(progress,"Verifying",output->Candidates,input.MaximumDegrees,completed,total);});
            if(checked.Segments.size()!=indices.size())return false;for(const auto& result:checked.Segments)if(result.Status!="Clear"){reject(result.Status=="Unchecked"?"LocalUnchecked":"LocalCollision");return false;}
            Array segments(motion.Query.Segments.size()),checkedIndices;std::set<std::size_t> checkedSet(indices.begin(),indices.end());std::size_t reused=0;
            for(std::size_t i=0;i<indices.size();++i){segments[indices[i]]=CollisionSegmentState(indices[i],checked.Segments[i]);checkedIndices.emplace_back(std::to_string(indices[i]));}
            for(std::size_t i=0;i<motion.Query.Segments.size();++i)if(!checkedSet.contains(i)){
                const auto found=oldIndices.find({motion.Samples[i].Key,motion.Samples[std::min(i+1,motion.Samples.size()-1)].Key});if(found==oldIndices.end())return false;
                const auto& a=baseline.Query.Segments[found->second];const auto& b=motion.Query.Segments[i];
                if(a.Connected!=b.Connected||a.Clearance!=b.Clearance||!RepairSamePose(a.Start,b.Start)||!RepairSamePose(a.End,b.End))return false;
                auto prior=oldSegments[found->second].To<Map>();prior["index"]=std::to_string(i);segments[i]=prior;++reused;
            }
            output->Frames=motion.Frames;output->Segments=std::move(segments);output->CheckedIndices=std::move(checkedIndices);output->Reused=reused;output->DistanceQueries=checked.DistanceQueries;output->MaximumRotationCorrectionRad=motion.MaximumRotationCorrectionRad;return true;
        };
        try{
            if(!rapidPair.empty()){
                const auto original=input.Transitions[station.Operation].To<Map>();
                if(input.Locked.at(String(original,"from"))||input.Locked.at(String(original,"to")))return unresolved("CAM_REPAIR_LOCKED_RAPID");
                if(!input.Rapids||input.Rapids->Queries.size()!=1)return unresolved("CAM_REPAIR_RAPID_NOT_FOUND");
                const auto& query=input.Rapids->Queries.front();const auto points=original.at("points").To<Array>(),directions=original.at("directions").To<Array>();
                std::vector<iCAX::GeometryServices::TravelPose> poses;for(std::size_t i=0;i<points.size();++i)poses.push_back({RegistrationPoint(points[i]),RegistrationPoint(directions[i])});
                const auto replaceDirections=[&](const RapidOrientationRepair::DirectionField& values){auto replacement=original;Array encoded;for(const auto& value:values)encoded.emplace_back(GenPoint(value));replacement["directions"]=encoded;auto transitions=input.Transitions;transitions[station.Operation]=replacement;return transitions;};
                const auto closure=[&](const RapidOrientationRepair::DirectionField& values)->std::optional<double>{
                    return boundaryRoll([&](const RepairSample& sample){return sample.Pair==rapidPair?RepairMath::Unit(values.at(sample.Point)):sample.Axis;});
                };
                const auto accept=[&](const RapidOrientationRepair::DirectionField& values){
                    // Internal rapid attitudes have no cutting cone limit.
                    // Keep the authored TCP path and both cutting connectors.
                    auto construction=query.Geometry;construction.Manual=true;construction.ConstructedManual=false;construction.TurnPoint.reset();construction.Waypoints.clear();
                    for(std::size_t i=1;i+1<poses.size();++i)construction.Waypoints.push_back({poses[i].Point,values[i]});
                    const auto constructed=rapidGeometry->Generate({construction},stop);if(constructed.size()!=1||!constructed.front().Generated||constructed.front().Poses.size()!=poses.size()){reject("RapidConstructionFailed");return false;}
                    for(std::size_t i=0;i<poses.size();++i)if(constructed.front().Poses[i].Point!=poses[i].Point||constructed.front().Poses[i].Direction!=values[i]){reject("RapidSpatialPathChanged");return false;}
                    auto transitions=replaceDirections(values);RepairMotion motion;if(!validate(input.Operations,transitions,motion))return false;
                    auto replacement=transitions[station.Operation].To<Map>();Array manualPoints,manualDirections;double largest=0;std::size_t changed=0;
                    for(std::size_t i=1;i+1<poses.size();++i){manualPoints.emplace_back(GenPoint(poses[i].Point));manualDirections.emplace_back(GenPoint(values[i]));const auto delta=RepairMath::Length(RepairMath::Sub(poses[i].Direction,values[i]));largest=std::max(largest,2*std::asin(std::min(1.,delta/2)));changed+=delta>1e-12;}
                    const Map manual{{"points",manualPoints},{"directions",manualDirections}};replacement["manualPath"]=manual;replacement["lengthMm"]=constructed.front().Length;replacement["estimatedSeconds"]=constructed.front().EstimatedSeconds;
                    replacement["surfacePoints"]=Array{};replacement["heightsMm"]=Array{};replacement["geometryStatus"]=Text("Generated");replacement["validation"]=Text("Clear");replacement["reason"]=Text{};replacement["obstacleId"]=Text{};replacement["planningMethod"]=Text("LocalSmoothOrientationRepair");replacement["representation"]=Text("TransportedBoxCompoundMesh");
                    replacement["candidateCount"]=std::to_string(output->Candidates);replacement["certifiedClearanceMm"]=query.Clearance;
                    output->Transitions=std::move(transitions);output->Transitions[station.Operation]=replacement;output->Overrides[rapidPair]=Map{{"from",replacement.at("from")},{"to",replacement.at("to")},{"policy",replacement.at("policy")},{"manualPath",manual}};
                    output->MaximumAppliedDegrees=largest*180/3.14159265358979323846;output->Adjustments={Map{{"from",replacement.at("from")},{"to",replacement.at("to")},{"frame",Text("Setup")},{"maxAppliedAngleDeg",output->MaximumAppliedDegrees},{"changedPointCount",std::to_string(changed)}}};
                    output->ChangedRapids=1;output->Solved=true;return true;
                };
                const auto peak=poses.size()>2?std::clamp(station.Point,std::size_t{1},poses.size()-2):std::size_t{1};
                const auto searched=RapidOrientationRepair::Search(poses,peak,closure,accept,stop,[&](auto count){output->Candidates=count;RepairProgress(progress,"Searching",count,input.MaximumDegrees);});
                if(output->Solved)return output;if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();if(stop())return unresolved("CAM_REPAIR_LOCAL_TIME_BUDGET");
                // A hand-authored spatial path is never replaced by a detour.
                if(query.Geometry.Manual)return unresolved("CAM_REPAIR_RAPID_NO_LOCAL_ORIENTATION_SOLUTION");
                RepairProgress(progress,"RebuildingRapids",++output->Candidates,input.MaximumDegrees);const auto result=rapid->Plan({query},stop);
                if(result.size()!=1||!result.front().Generated||result.front().Status!="Clear")return unresolved("CAM_REPAIR_RAPID_NO_LOCAL_SOLUTION");
                auto transition=original;const auto replacement=RapidResult(result.front());for(const auto& [key,value]:replacement)transition[key]=value;
                auto transitions=input.Transitions;transitions[station.Operation]=transition;RepairMotion motion;if(!validate(input.Operations,transitions,motion))return unresolved("CAM_REPAIR_LOCAL_BOUNDARY_NOT_CLOSED");
                output->Transitions=std::move(transitions);output->ChangedRapids=1;output->Solved=true;return output;
            }
            const auto searchAt=[&](const RepairSample& station)->std::shared_ptr<CollisionRepairResult>{
            if(input.Locked.at(station.Intent))return unresolved("CAM_REPAIR_LOCKED_CUT");
            const auto found=std::find_if(input.Generation.Cuts.begin(),input.Generation.Cuts.end(),[&](const auto& cut){return cut.IDText==station.Intent;});
            if(found==input.Generation.Cuts.end()||station.Anchor.empty())return unresolved("CAM_REPAIR_SOURCE_ANCHOR_REQUIRED");
            auto cut=*found;const auto originalPose=cut.Metadata.Pose;if(String(originalPose,"mode")!="Continuous")return unresolved("CAM_REPAIR_LOCAL_POSE_REQUIRED");
            if(RegistrationNumber(cut.Assignment.at("kerf").To<Map>().at("widthMm"))!=0)return unresolved("CAM_REPAIR_COMPENSATED_LOCAL_POSE_UNSUPPORTED");
            const auto controls=blockedControls.empty()?std::vector<RepairSample>{station}:blockedControls;
            const auto sets=ProjectFiveAxisPoseControls(cut.IntentPath.Value);const auto savedPoints=sets.contains("active")?sets.at("active").To<Array>():Array{};
            for(const auto& value:savedPoints){const auto point=value.To<Map>();if(!point.contains("driver"))continue;
                for(const auto& control:controls)if(RepairSameAnchor(cut,control.Anchor,point.at("anchor").To<Map>()))return unresolved("CAM_REPAIR_LINKED_KEYPOINT_CONSTRAINT");}
            constexpr double pi=3.14159265358979323846;double maximum=input.MaximumDegrees*pi/180;
            if(!input.LocalDeviationAuthorized)for(const auto* key:{"angularToleranceRad","leadDeviationLimitRad","sideDeviationLimitRad"})maximum=std::min(maximum,RegistrationNumber(originalPose.at(key)));
            if(maximum<1e-8)return unresolved("CAM_REPAIR_POSE_LIMIT_ZERO");
            const auto originalOp=input.Operations[station.Operation].To<Map>(),originalPart=originalOp.at("parts").To<Array>()[station.Part].To<Map>();const auto direction=RegistrationPoint(originalPart.at("directions").To<Array>()[station.Point]);
            std::vector<RepairMath::Vector> controlDirections;const auto& originalParts=std::get<Array>(originalOp.at("parts").m_Value);
            for(const auto& control:controls){const auto& part=std::get<Map>(originalParts.at(control.Part).m_Value);controlDirections.push_back(RegistrationPoint(std::get<Array>(part.at("directions").m_Value).at(control.Point)));}
            const auto stations=RepairAxisStations(cut,originalOp);std::vector<Map> newAnchors;
            for(const auto& control:controls){if(control.Anchor.empty())return unresolved("CAM_REPAIR_SOURCE_ANCHOR_REQUIRED");
                bool exists=false;for(const auto& value:savedPoints)exists|=String(value.To<Map>(),"kind")=="Control"&&RepairSameAnchor(cut,control.Anchor,value.To<Map>().at("anchor").To<Map>());
                for(const auto& anchor:newAnchors)exists|=RepairSameAnchor(cut,control.Anchor,anchor);if(!exists)newAnchors.push_back(control.Anchor);
            }
            if(savedPoints.size()+newAnchors.size()>256)return unresolved("CAM_REPAIR_KEYPOINT_BUDGET");
            if(baseline.Samples[entryFrame].Anchor.empty()||baseline.Samples[closureFrame].Anchor.empty())return unresolved("CAM_REPAIR_BOUNDARY_ANCHOR_REQUIRED");
            int basisIndex=0;for(int j=1;j<3;++j)if(std::abs(direction[j])<std::abs(direction[basisIndex]))basisIndex=j;RepairMath::Vector basis{};basis[basisIndex]=1;
            const auto u=RepairMath::Unit(RepairMath::Sub(basis,RepairMath::Scale(direction,RepairMath::Dot(direction,basis)))),v=RepairMath::Cross(direction,u);
            struct Trial { Map Operation,Field;Array Controls;double Roll=0,Applied=0;std::size_t Changed=0; };
            const auto trial=[&](double angle,double azimuth)->std::optional<Trial>{
                if(stop())return {};RepairProgress(progress,"Searching",++output->Candidates,input.MaximumDegrees);
                const auto trialStarted=std::chrono::steady_clock::now();
                {std::lock_guard lock(progress->Mutex);progress->State["candidateAngleDeg"]=angle*180/pi;progress->State["candidateAzimuthDeg"]=azimuth*180/pi;progress->State["candidateAnchor"]=station.Anchor;}
                const auto tangent=RepairMath::Add(RepairMath::Scale(u,std::cos(azimuth)),RepairMath::Scale(v,std::sin(azimuth)));
                const auto desired=RepairMath::Unit(RepairMath::Add(RepairMath::Scale(direction,std::cos(angle)),RepairMath::Scale(tangent,std::sin(angle))));
                // All controls are rotated in the original operation's local
                // Workpiece frame. A setup/world axis must not enter here.
                const auto rotorAxis=RepairMath::Unit(RepairMath::Cross(direction,tangent));
                Array points;
                for(const auto boundary:{entryFrame,closureFrame})points.emplace_back(Map{{"kind",Text("Reference")},{"set",Text("active")},
                    {"id",Text("repair-boundary-")+RapidInputDigest(station.Intent+":"+iCAX::Data::VariantSerializer::Serialize(baseline.Samples[boundary].Anchor)).substr(7,24)},
                    {"anchor",baseline.Samples[boundary].Anchor},{"boundaryPolicy",Text("Duplicate")}});
                for(std::size_t ci=0;ci<controls.size();++ci){const auto& control=controls[ci];const auto& originalDirection=controlDirections[ci];
                    const auto vector=controls.size()==1?desired:RepairMath::Unit(RepairMath::Add(RepairMath::Add(RepairMath::Scale(originalDirection,std::cos(angle)),RepairMath::Scale(RepairMath::Cross(rotorAxis,originalDirection),std::sin(angle))),RepairMath::Scale(rotorAxis,RepairMath::Dot(rotorAxis,originalDirection)*(1-std::cos(angle)))));
                    auto id=Text("repair-")+RapidInputDigest(station.Intent+":"+iCAX::Data::VariantSerializer::Serialize(control.Anchor)).substr(7,24);
                    for(const auto& value:savedPoints){const auto point=value.To<Map>();if(String(point,"kind")=="Control"&&RepairSameAnchor(cut,control.Anchor,point.at("anchor").To<Map>())){id=String(point,"id");break;}}
                    points.emplace_back(Map{{"kind",Text("Control")},{"set",Text("active")},{"id",id},{"anchor",control.Anchor},
                        {"direction",GenPoint(vector)},{"transition",originalPose.at("transitionMetric")},{"boundaryPolicy",Text("Duplicate")}});
                }
                cut.IntentPath.Value=found->IntentPath.Value;
                Trial candidate;candidate.Field=BuildFiveAxisIntentPoseDelta(*cut.Context,found->IntentPath.Value,found->IntentPath.Port,points,"active");
                cut.IntentPath.Value.Channels["poseField"]=candidate.Field;candidate.Controls=points;candidate.Operation=RepairOperationAxes(cut,stations,originalOp);
                const auto fieldFinished=std::chrono::steady_clock::now();
                candidate.Applied=RepairMaximumAngle(originalOp,candidate.Operation,&candidate.Changed);if(candidate.Applied>maximum+1e-10||!candidate.Changed){reject("DeviationLimit");return {};}
                for(const auto* key:{"setupStart","setupEnd","setupStartDirection","setupEndDirection"})if(candidate.Operation.at(key)!=originalOp.at(key)){reject("ConnectorEndpointChanged");return {};}
                const auto& parts=std::get<Array>(candidate.Operation.at("parts").m_Value);
                const auto& transform=input.Placements.at(String(candidate.Operation,"placementId"));
                std::vector<RepairMath::Vector> axes;axes.reserve(pathLast-pathFirst+1);
                for(std::size_t i=pathFirst;i<=pathLast;++i){const auto& sample=baseline.Samples[i];const auto& part=std::get<Map>(parts.at(sample.Part).m_Value);const auto& directions=std::get<Array>(part.at("directions").m_Value);
                    const auto axis=RepairMath::Unit(RapidTransform(transform,directions.at(sample.Point),true));
                    if(!allowed.contains(sample.Key)&&axis!=sample.Axis){reject("OutsideDirectionChanged");return {};}
                    axes.push_back(axis);
                }
                candidate.Roll=boundaryRoll([&](const RepairSample& sample){return axes.at(&sample-baseline.Samples.data()-pathFirst);});
                {std::lock_guard lock(progress->Mutex);progress->State["fieldMilliseconds"]=std::chrono::duration<double,std::milli>(fieldFinished-trialStarted).count();progress->State["candidateMilliseconds"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-trialStarted).count();}
                return candidate;
            };
            const auto accept=[&](const Trial& candidate)->bool{
                auto operations=input.Operations;operations[station.Operation]=candidate.Operation;RepairMotion motion;if(!validate(operations,input.Transitions,motion))return false;
                output->Operations=std::move(operations);output->PoseEdits[station.Intent]=Map{{"controls",candidate.Controls},{"field",candidate.Field}};output->MaximumAppliedDegrees=candidate.Applied*180/pi;
                Array edits;for(const auto& value:candidate.Controls){const auto point=value.To<Map>();if(String(point,"kind")!="Control")continue;const auto old=std::find_if(savedPoints.begin(),savedPoints.end(),[&](const auto& p){return String(p.template To<Map>(),"id")==String(point,"id");});
                    if(old==savedPoints.end()||old->To<Map>().at("direction")!=point.at("direction"))edits.emplace_back(Map{{"pointId",point.at("id")},{"sourceAnchor",point.at("anchor")},{"action",Text(old==savedPoints.end()?"Inserted":"Updated")}});
                }
                output->Adjustments={Map{{"intentId",station.Intent},{"sourceAnchor",station.Anchor},{"frame",Text("Workpiece")},{"maxAppliedAngleDeg",output->MaximumAppliedDegrees},{"changedPointCount",std::to_string(candidate.Changed)},{"keypointCount",std::to_string(savedPoints.size()+newAnchors.size())},{"keypointEdits",edits}}};output->Unresolved.clear();output->Solved=true;return true;
            };
            std::vector<double> angles;for(double degrees:{2.,5.,10.,15.,20.,30.})if(degrees*pi/180<=maximum+1e-12)angles.push_back(degrees*pi/180);
            if(angles.empty()||maximum-angles.back()>1e-8)angles.push_back(maximum);
            const auto geometryRejections=[&]{std::size_t count=0;for(const auto* key:{"WitnessPoseStillBlocked","LocalCollision","LocalUnchecked"})if(const auto it=rejected.find(key);it!=rejected.end())count+=it->second;return count;};
            for(auto angle:angles){auto previous=trial(angle,0);double previousAzimuth=0;if(previous&&std::abs(previous->Roll)<1e-14&&accept(*previous))return output;
                for(unsigned i=1;i<=8&&!stop();++i){const auto azimuth=i*pi/4;auto next=trial(angle,azimuth);
                    if(next&&std::abs(next->Roll)<1e-14&&accept(*next))return output;
                    // Net transported roll is a scalar once the source axis
                    // returns at the exit. Root-search it without geometry
                    // queries; only an exactly closed actual pose is checked.
                    if(previous&&next&&previous->Roll*next->Roll<0){double low=previousAzimuth,high=azimuth,lowRoll=previous->Roll;
                        for(unsigned step=0;step<48&&!stop();++step){const auto middle=(low+high)/2;auto candidate=trial(angle,middle);if(!candidate)break;
                            if(std::abs(candidate->Roll)<1e-14){const auto before=geometryRejections();if(accept(*candidate))return output;
                                // Once this closed root has a real blocked
                                // witness, try another angle/root rather than
                                // recertifying numerical neighbors of it.
                                if(geometryRejections()!=before)break;}
                            if(candidate->Roll*lowRoll<=0)high=middle;else{low=middle;lowRoll=candidate->Roll;}
                        }
                    }previous=std::move(next);previousAzimuth=azimuth;
                }
            }
            if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();return unresolved(stop()?"CAM_REPAIR_LOCAL_TIME_BUDGET":"CAM_REPAIR_NO_CLOSED_LOCAL_SOLUTION");
            };
            auto searched=searchAt(station);
            if(output->RepairKind=="Keypoint"||output->RepairKind=="KeypointBlock")return searched;
            // Preconditions on the user's actual selected station are hard
            // constraints. Only a finite local search failure may try another
            // source anchor within this SAME captured collision cluster.
            if(searched->Solved||stop()||searched->Unresolved.empty()||String(searched->Unresolved.front().To<Map>(),"reason")!="CAM_REPAIR_NO_CLOSED_LOCAL_SOLUTION")return searched;
            std::vector<Map> attempted{station.Anchor};
            for(const auto index:{(first+last+1)/2,last,first,last+1}){
                if(attempted.size()>=3||stop())break;if(index>=baseline.Samples.size())continue;const auto& candidate=baseline.Samples[index];
                if(candidate.Intent!=station.Intent||!candidate.Pair.empty()||candidate.Anchor.empty()||!allowed.contains(candidate.Key)||std::find(attempted.begin(),attempted.end(),candidate.Anchor)!=attempted.end())continue;
                attempted.push_back(candidate.Anchor);searched=searchAt(candidate);if(searched->Solved)return searched;
            }
            if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();return unresolved(stop()?"CAM_REPAIR_LOCAL_TIME_BUDGET":"CAM_REPAIR_NO_CLOSED_LOCAL_SOLUTION");
        }catch(const iCAX::Tasks::TaskCanceledException&){throw;}
        catch(const std::exception& error){if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();return unresolved(stop()?"CAM_REPAIR_LOCAL_TIME_BUDGET":Text("CAM_REPAIR_LOCAL_QUERY_FAILED: ")+error.what());}
    }
    std::shared_ptr<CollisionRepairResult> VerifyPublishedCollisionRepair(const CollisionRepairSnapshot& input,
        const CollisionRepairResult& candidate,const GenerationSnapshot& generation,
        const std::shared_ptr<iCAX::GeometryServices::ICurveOffset3DService>& offset,
        const std::shared_ptr<iCAX::GeometryServices::IRigidMotionCollisionService>& collision,
        const std::shared_ptr<std::atomic_bool>& cancelled,const std::shared_ptr<CollisionRepairProgress>& progress)
    {
        auto result=std::make_shared<CollisionRepairResult>(candidate);result->Solved=false;
        const auto reject=[&](const Text& reason){result->Unresolved={Map{{"reason",reason}}};return result;};
        const auto generated=CalculateGeneration(generation,offset,cancelled);
        for(const auto& value:generated->Operations)
        {
            auto operation=value.To<Map>();const auto id=String(operation,"intentId");
            auto previous=std::find_if(result->Operations.begin(),result->Operations.end(),[&](const auto& item){return String(item.template To<Map>(),"intentId")==id;});
            if(previous==result->Operations.end())return reject("CAM_REPAIR_PUBLISHED_PATH_MISSING");
            const auto original=previous->To<Map>();
            for(const auto* key:{"setupStart","setupEnd","setupStartDirection","setupEndDirection"})
                if(operation.at(key)!=original.at(key))return reject("CAM_REPAIR_PUBLISHED_ENDPOINT_CHANGED");
            const auto baseline=std::find_if(input.Generation.Cuts.begin(),input.Generation.Cuts.end(),[&](const auto& cut){return cut.IDText==id;});
            if(baseline==input.Generation.Cuts.end())return reject("CAM_REPAIR_BASELINE_FIELD_MISSING");
            CFiveAxisIntentPoseField baselineField(*baseline->Context,baseline->IntentPath.Value);
            double maximum=input.MaximumDegrees*std::numbers::pi/180;
            if(!input.LocalDeviationAuthorized)for(const auto* key:{"angularToleranceRad","leadDeviationLimitRad","sideDeviationLimitRad"})maximum=std::min(maximum,RegistrationNumber(baseline->Metadata.Pose.at(key)));
            for(const auto& partValue:operation.at("parts").To<Array>())
            {
                const auto part=partValue.To<Map>();const auto samples=part.at("sourceSamples").To<Array>(),axes=part.at("directions").To<Array>();
                if(samples.size()!=axes.size())return reject("CAM_REPAIR_PUBLISHED_SAMPLES_INVALID");
                for(std::size_t i=0;i<samples.size();++i)
                {
                    const auto sample=samples[i].To<Map>();const auto id=String(sample,"segmentId");
                    const auto& segments=baseline->IntentPath.Value.Geometry.Segments;
                    const auto segment=std::find_if(segments.begin(),segments.end(),[&](const auto& item){return item.Id==id;});
                    if(segment==segments.end())return reject("CAM_REPAIR_PUBLISHED_GEOMETRY_CHANGED");
                    const auto old=baselineField.At(segment-segments.begin(),RegistrationNumber(sample.at("nativeParameter")));const auto axis=RegistrationPoint(axes[i]);
                    const auto angle=2*std::asin(std::min(1.,RepairMath::Length(RepairMath::Sub({old.X,old.Y,old.Z},axis))/2));
                    if(angle>maximum+1e-10)return reject("CAM_REPAIR_PUBLISHED_DEVIATION_LIMIT");
                    result->MaximumAppliedDegrees=std::max(result->MaximumAppliedDegrees,angle*180/std::numbers::pi);
                }
            }
            operation["order"]=original.at("order");*previous=std::move(operation);
        }
        const auto originalMotion=RepairTrajectory(input,input.Operations,input.Transitions);
        const auto motion=RepairTrajectory(input,result->Operations,result->Transitions);
        const auto pose=[&](const RepairMotion& path,std::size_t index)->const iCAX::GeometryServices::RigidMotionPose&
        {return index<path.Query.Segments.size()?path.Query.Segments[index].Start:path.Query.Segments.back().End;};
        const auto matching=[&](const RepairSample& sample)->std::optional<std::size_t>
        {
            for(std::size_t i=0;i<motion.Samples.size();++i)
            {
                const auto& current=motion.Samples[i];if(current.Intent!=sample.Intent||current.Pair!=sample.Pair)continue;
                if(!sample.Anchor.empty()&&!current.Anchor.empty()&&IntentSameNativeAnchor(sample.Anchor,current.Anchor))return i;
                if(sample.Anchor.empty()&&current.Key==sample.Key)return i;
            }
            return {};
        };
        const auto first=matching(originalMotion.Samples.at(candidate.FixedEntryFrame)),last=matching(originalMotion.Samples.at(candidate.FixedExitFrame));
        if(!first||!last||*last<=*first)return reject("CAM_REPAIR_PUBLISHED_BOUNDARY_STATION_MISSING");
        // A changed display grid is never proof that the original full boundary
        // poses or the unchanged exterior motion were preserved.
        for(std::size_t i=0;i<originalMotion.Samples.size();++i)
            if(i<=candidate.FixedEntryFrame||i>=candidate.FixedExitFrame)
            {const auto current=matching(originalMotion.Samples[i]);if(!current||!RepairSamePose(pose(originalMotion,i),pose(motion,*current)))return reject("CAM_REPAIR_PUBLISHED_OUTSIDE_POSE_CHANGED");}
        if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();
        RepairProgress(progress,"VerifyingPublishedGeometry",result->Candidates,input.MaximumDegrees,0,motion.Query.Segments.size());
        const auto checked=collision->CheckParallel(motion.Query,std::max(1u,input.Collision.ParallelWorkers),[&]{return cancelled->load();},
            [&](auto){},[&](auto index,const auto&){RepairProgress(progress,"VerifyingPublishedGeometry",result->Candidates,input.MaximumDegrees,index+1,motion.Query.Segments.size());});
        if(checked.Segments.size()!=motion.Query.Segments.size())return reject("CAM_REPAIR_PUBLISHED_CHECK_INCOMPLETE");
        for(std::size_t i=*first;i<*last;++i)if(checked.Segments.at(i).Status!="Clear")return reject("CAM_REPAIR_PUBLISHED_LOCAL_INTERVAL_NOT_CLEAR");
        result->Frames=motion.Frames;result->Segments.clear();result->CheckedIndices.clear();
        for(std::size_t i=0;i<checked.Segments.size();++i){result->Segments.emplace_back(CollisionSegmentState(i,checked.Segments[i]));result->CheckedIndices.emplace_back(std::to_string(i));}
        result->FixedEntryFrame=*first;result->FixedExitFrame=*last;result->Reused=0;result->DistanceQueries=checked.DistanceQueries;
        result->MaximumRotationCorrectionRad=motion.MaximumRotationCorrectionRad;result->Verified=true;result->Solved=true;result->Unresolved.clear();return result;
    }
    #include "FiveAxisKeyposeRepair.inc"
    void FinishCollisionRepair(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        if(task.RepairInput&&task.RepairInput->KeyposeRepair){FinishKeyposeRepair(scene,runtime,task);return;}
        const auto rollback=[&]()
        {
            if(!task.RepairCandidate||task.Sequence!=runtime.Sequence)return false;
            const auto history=scene.Database().GetUndoArray();
            if(history.empty()||std::get<0>(history.front())!=task.RepairCandidate->CandidateUndoID)return false;
            const auto undone=scene.Database().Undo();if(undone)QueueIntentGeneration(scene,task.SetupID);return undone;
        };
        try
        {
            if(task.Cancelled->load())
            {const auto reverted=rollback();task.Status="Cancelled";task.Result={{"status",Text("Unrepaired")},{"candidateReverted",reverted}};}
            else if(task.Sequence!=runtime.Sequence)
            {task.Status="Superseded";task.Result={{"status",Text("Unverified")},{"changed",bool(task.RepairCandidate)}};}
            else
            {
                const auto& input=*task.RepairInput;std::shared_ptr<const CollisionRepairResult> output;
                if(task.RepairCandidate)
                {
                    if(runtime.IntentGenerations.contains(IntentPlan(scene,task.SetupID)))return;
                    if(!task.GenerationInput)
                    {
                        const auto resolved=IntentDocument(scene).Resolve(IntentPlan(scene,task.SetupID));
                        if(resolved.Status!=iCAX::CAM::Intent::EOutputCompleteness::Complete)throw std::runtime_error("CAM_REPAIR_CANDIDATE_PUBLICATION_FAILED");
                        std::set<ID> selected;
                        for(const auto& [id,edit]:task.RepairCandidate->PoseEdits)
                        {
                            const auto path=ReadIntentPath(scene,ID::from_string(id).value());selected.insert(path.PathId);
                            if(path.Value.Channels.at("poseField")!=edit.To<Map>().at("field"))throw std::runtime_error("CAM_REPAIR_PUBLISHED_FIELD_CHANGED");
                        }
                        task.GenerationInput=std::make_shared<const GenerationSnapshot>(CaptureGeneration(scene,task.SetupID,&selected));
                        auto candidate=std::make_shared<CollisionRepairResult>(*task.RepairCandidate);candidate->VerificationKey=GenerationInputKey(scene,task.SetupID);task.RepairCandidate=candidate;
                        const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();const auto collision=scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>();
                        task.RepairWorker=iCAX::Tasks::Run([input=task.RepairInput,candidate,generation=task.GenerationInput,offset,collision,cancelled=task.Cancelled,progress=task.RepairProgress]
                            {return VerifyPublishedCollisionRepair(*input,*candidate,*generation,offset,collision,cancelled,progress);});
                        task.Result={{"status",Text("Candidate")},{"verification",Text("Running")}};return;
                    }
                    if(!task.RepairWorker.IsCompleted())return;output=task.RepairWorker.Result();
                    if(task.RepairCandidate->VerificationKey!=GenerationInputKey(scene,task.SetupID))throw std::runtime_error("CAM_REPAIR_VERIFICATION_INPUT_CHANGED");
                    if(!output->Solved||!output->Verified)throw std::runtime_error(output->Unresolved.empty()?"CAM_REPAIR_VERIFICATION_FAILED":String(output->Unresolved.front().To<Map>(),"reason"));
                }
                else
                {
                    output=task.RepairWorker.Result();
                    if(input.Collision.InputKey!=SimulationCollisionKey(scene,task.SetupID)){task.Status="Superseded";task.Result={{"status",Text("Unverified")}};}
                    else if(!output->Solved)
                    {task.Result={{"scope",Text("Local")},{"status",Text("Unresolved")},{"changed",false},{"unresolved",output->Unresolved},{"candidateCount",std::to_string(output->Candidates)},{"rejectionCounts",output->Rejections}};task.Status="Completed";}
                    else if(!output->PoseEdits.empty())
                    {
                        using namespace iCAX::CAM::Intent;std::vector<Map> commands;iCAX::Data::uuid_name_generator names(ID::from_string(task.CommandID).value());
                        for(const auto& [id,edit]:output->PoseEdits)
                        {
                            const auto path=ReadIntentPath(scene,ID::from_string(id).value());RequireCutUnlocked(scene,path.PathId);
                            const auto operation=names("repair/delta/"+id);
                            commands.push_back({{"kind",Text("AddOperation")},{"commandId",operation},{"operationId",operation},{"planId",IntentPlan(scene,task.SetupID)},
                                {"typeId",Text("fiveaxis.pose.delta")},{"parameters",Map{{"controls",edit.To<Map>().at("controls")}}},
                                {"context",Map{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}}},
                                {"inputs",Array{Map{{"slot",Text("input/0")},{"source",ToData(path.Port)},{"replace",true},{"ranges",Array{}}}}},
                                {"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint}}}});
                        }
                        CommitIntentCommands(scene,commands,{task.SetupID},"局部自动避碰修复");task.Sequence=runtime.Sequence;
                        auto candidate=std::make_shared<CollisionRepairResult>(*output);task.RepairCandidate=candidate;
                        const auto history=scene.Database().GetUndoArray();if(history.empty())throw std::runtime_error("CAM_REPAIR_CANDIDATE_UNDO_MISSING");candidate->CandidateUndoID=std::get<0>(history.front());
                        task.RepairWorker={};task.GenerationInput.reset();task.Result={{"status",Text("Candidate")},{"verification",Text("AwaitingPublication")}};return;
                    }
                }
                if(task.Status=="Running")
                {
                    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID);const auto generationKey=GenerationInputKey(scene,task.SetupID);
                    auto geometry=input.Geometry;geometry["inputKey"]=generationKey;geometry["operations"]=output->Operations;geometry["transitions"]=output->Transitions;
                    const auto generatedRevision=NewMachiningDataRevision(),rapidsRevision=NewMachiningDataRevision();Map entries;const auto collisionInputKey=RapidCollisionInputKey(scene,task.SetupID);
                    for(const auto& value:output->Transitions){auto transition=value.To<Map>();const auto policy=transition.at("policy").To<Map>();Map manual;if(transition.contains("manualPath"))manual=transition.at("manualPath").To<Map>();
                        const auto key=RapidInputKey(scene,task.SetupID,transition,policy,manual,&geometry);Map record{{"from",transition.at("from")},{"to",transition.at("to")},{"dependencyVersion",Text("7")},{"inputKey",key},{"collisionInputKey",collisionInputKey},{"collisionMarginMm",RegistrationNumber(policy.at("marginMm"))},{"policy",policy},{"result",RepairRapidResult(transition)}};if(!manual.empty())record["manualPath"]=manual;entries[key]=record;}
                    const Map signature{{"generation",generationKey},{"geometry",generatedRevision},{"rapids",rapidsRevision},{"overrides",output->Overrides}};
                    auto report=input.Report;report.erase("validity");report["inputKey"]=SimulationCollisionKey(scene,task.SetupID,&signature);report["frames"]=output->Frames;report["segments"]=output->Segments;
                    UInt collisions=0,unchecked=0;for(const auto& value:output->Segments){const auto segment=value.To<Map>();collisions+=String(segment,"status")=="Collision";unchecked+=String(segment,"status")=="Unchecked";}
                    report["status"]=Text(collisions?"Collision":unchecked?"Unchecked":"Clear");report["collisionCount"]=std::to_string(collisions);report["uncheckedCount"]=std::to_string(unchecked);
                    report["checkMode"]=Text(output->Verified?"PublishedRepairFullRecheck":"LocalRepair");report["scopeOfLastCheck"]=Text(output->Verified?"Full":"Local");report["checkedSegmentCount"]=std::to_string(output->CheckedIndices.size());report["reusedSegmentCount"]=std::to_string(output->Reused);report["distanceQueries"]=std::to_string(output->DistanceQueries);
                    report["frameCount"]=std::to_string(output->Frames.size());report["segmentCount"]=std::to_string(output->Segments.size());report["localCheckedSegmentIndices"]=output->CheckedIndices;
                    report["rotationNumerics"]=Map{{"convention",Text("AxisAndScalarRollV1")},{"rollQuantumRad",CollisionRotation::RollQuantumRad},
                        {"maximumStepCorrectionRad",CollisionRotation::MaximumStepCorrectionRad},{"maximumObservedStepCorrectionRad",output->MaximumRotationCorrectionRad}};
                    report["localRepairPolicy"]=Map{{"verification",Text("Verified")},{"scope",Text("FixedKeypointBoundaries")},{"selectedSegmentIndex",std::to_string(input.Selected)},
                        {"fixedEntryFrameIndex",std::to_string(output->FixedEntryFrame)},{"fixedExitFrameIndex",std::to_string(output->FixedExitFrame)},{"maxDeviationDeg",input.MaximumDegrees},{"localPoseDeviationAuthorized",input.LocalDeviationAuthorized}};
                    for(const auto* key:{"keypoints","keypointCollisionCount","keypointUncheckedCount","keypointCheckCount"})report.erase(key);report["keypointResultsCurrent"]=false;
                    const auto collision=EngineeringData(report);const bool authored=bool(task.RepairCandidate);
                    Transaction(scene.Database(),"发布已验证避碰修复",[&](auto& tx){
                        StoreGeneratedToolpathData(tx,scene,task.SetupID,geometry,generatedRevision);StoreGeneratedRapidData(tx,task.SetupID,entries,rapidsRevision);
                        tx.ModifyComponent(task.SetupID,CFiveAxisSetupComponent::S_ClassName,{{"RapidOverrides",output->Overrides},{"SimulationCheckData",collision},{"PlanValidity",Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});
                    },!authored);
                    Array changed;for(const auto& [id,edit]:output->PoseEdits)changed.emplace_back(id);
                    task.Result={{"scope",Text("Local")},{"status",Text("Repaired")},{"verification",Text("Verified")},{"changed",true},{"repairedSegmentIndex",std::to_string(input.Selected)},
                        {"changedIntentIds",changed},{"changedRapidCount",std::to_string(output->ChangedRapids)},{"adjustments",output->Adjustments},{"maxDeviationDeg",input.MaximumDegrees},{"maxAppliedAngleDeg",output->MaximumAppliedDegrees},
                        {"checkedSegmentCount",std::to_string(output->CheckedIndices.size())},{"reusedSegmentCount",std::to_string(output->Reused)},{"remainingCollisionCount",std::to_string(collisions)},{"remainingUncheckedCount",std::to_string(unchecked)}};
                    task.Status="Completed";{std::lock_guard lock(task.RepairProgress->Mutex);task.RepairProgress->State["completedIssueCount"]=Text("1");}
                }
            }
        }
        catch(const iCAX::Tasks::TaskCanceledException&){const auto reverted=rollback();task.Status="Cancelled";task.Result={{"status",Text("Unrepaired")},{"candidateReverted",reverted}};}
        catch(const std::exception& error){const auto reverted=rollback();task.Status=task.Sequence!=runtime.Sequence&&!reverted?"Superseded":"Failed";task.Error=error.what();task.Result={{"status",Text("Unrepaired")},{"verification",Text("Failed")},{"candidateReverted",reverted}};}
        task.RepairWorker={};task.RepairCandidate.reset();task.RepairInput.reset();task.GenerationInput.reset();
    }
}
Map AutoRepairCollision(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","segmentIndex","keyposeFrameIndex","intervalFirstFrameIndex","maxDeviationDeg","maxTranslationMm","localPoseDeviationAuthorized","repairPhase","toleranceOrigins"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload=Text("AutoRepairCollision:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,prior]:runtime->Tasks)if(prior.CommandID==command){if(prior.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,prior);}
    if(runtime->Receipts.contains(command))throw std::invalid_argument("CAM_COMMAND_REUSED");CheckExpected(*runtime,request);CheckCapacity(*runtime);
    ImportTask task;task.Kind="AutoRepairCollision";task.CommandID=command;task.Payload=payload;task.Sequence=runtime->Sequence;task.SetupID=Identifier(request,"setupId");RequireSetupUnlocked(scene,task.SetupID);
    if(IndependentArticulatedHead(EffectiveHeadDefinition(scene,*Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID))))return StartKeyposeRepair(scene,*runtime,request,std::move(task));
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),task.SetupID);auto input=std::make_shared<CollisionRepairSnapshot>();input->MaximumDegrees=request.contains("maxDeviationDeg")?RegistrationNumber(request.at("maxDeviationDeg")):15.;
    if(!std::isfinite(input->MaximumDegrees)||input->MaximumDegrees<=0||input->MaximumDegrees>180)throw std::invalid_argument("CAM_REPAIR_DEVIATION_INVALID");input->LocalDeviationAuthorized=request.contains("localPoseDeviationAuthorized")&&request.at("localPoseDeviationAuthorized").To<bool>();input->Selected=static_cast<std::size_t>(Integer(request,"segmentIndex"));input->Report=SimulationCollisionState(scene,task.SetupID);
    if(String(input->Report,"validity")!="Current")throw std::invalid_argument("CAM_COLLISION_CURRENT_REPORT_REQUIRED");const auto segments=input->Report.at("segments").To<Array>();
    if(input->Selected>=segments.size()||String(segments[input->Selected].To<Map>(),"status")!="Collision")throw std::invalid_argument("CAM_REPAIR_SELECT_COLLISION");
    input->Collision=*CaptureSimulationCollision(scene,task.SetupID);
    if(input->Collision.Scope!=String(input->Report,"scope"))throw std::invalid_argument("CAM_COLLISION_SCOPE_MISMATCH: 自动修复尚未接入完整切割头检测，请手动调整后重新检测本条。");
    const auto generated=GeneratedGeometryState(scene,task.SetupID);input->Operations=generated.at("operations").To<Array>();input->Transitions=generated.at("transitions").To<Array>();input->Order=CutTreeOrder(scene,task.SetupID,&generated);input->Settings=RapidSettings(*setup);input->Overrides=setup->GetRapidOverrides();
    input->Geometry=ReadGeneratedToolpathData(scene,task.SetupID);
    const auto target=std::min(input->Selected+(RegistrationNumber(segments[input->Selected].To<Map>().at("fraction"))>=.5?1:0),input->Collision.Frames.size()-1);std::set<ID> selected;const auto& frame=std::get<Map>(input->Collision.Frames.at(target).m_Value);selected.insert(ID::from_string(String(frame,"intentId")).value());
    if(String(frame,"role")!="Rapid")input->Generation=CaptureGeneration(scene,task.SetupID,&selected);for(const auto& value:input->Operations){const auto op=value.To<Map>();input->Placements[String(op,"placementId")]=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(op,"placementId"))->GetTransform();input->Locked[String(op,"intentId")]=CutLocked(scene,Identifier(op,"intentId"));}
    if(String(frame,"role")=="Rapid")for(const auto& value:input->Transitions){const auto transition=value.To<Map>();if(transition.at("to")!=frame.at("intentId"))continue;Map preview{{"geometryRevision",generated.at("revision")},{"from",transition.at("from")},{"to",transition.at("to")},{"policy",transition.at("policy")}};if(transition.contains("manualPath"))preview["manualPath"]=transition.at("manualPath");input->Rapids=CaptureRapids(scene,task.SetupID,preview,true);break;}
    task.RepairInput=input;task.RepairProgress=std::make_shared<CollisionRepairProgress>();RepairProgress(task.RepairProgress,"Preparing",0,input->MaximumDegrees);
    const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
    const auto collision=scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>();const auto rapid=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathPlanningService>();const auto rapidGeometry=scene.Services().Resolve<iCAX::GeometryServices::IRapidPathGeometryService>();
    task.RepairWorker=iCAX::Tasks::Run([input,offset,collision,rapid,rapidGeometry,cancelled=task.Cancelled,progress=task.RepairProgress]{return CalculateCollisionRepair(*input,offset,collision,rapid,rapidGeometry,cancelled,progress);});
    const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
}
