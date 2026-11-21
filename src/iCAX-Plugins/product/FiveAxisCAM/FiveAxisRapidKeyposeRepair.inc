std::shared_ptr<CollisionRepairResult> CalculateRapidKeyposeRepair(const CollisionRepairSnapshot& input,
    const std::shared_ptr<iCAX::GeometryServices::IRapidPathGeometryService>& geometry,
    const std::shared_ptr<std::atomic_bool>& cancelled,const std::shared_ptr<CollisionRepairProgress>& progress){
    auto result=std::make_shared<CollisionRepairResult>();result->RepairKind="RapidKeyposes";
    const auto firstLocation=input.RepairLocations.front().To<Map>();const auto from=String(firstLocation,"from"),to=String(firstLocation,"to"),pair=from+"/"+to;
    Map original;for(const auto& value:input.Transitions){const auto t=value.To<Map>();if(String(t,"from")==from&&String(t,"to")==to){original=t;break;}}
    if(original.empty()||!input.Rapids||input.Rapids->Queries.size()!=1)throw std::invalid_argument("CAM_REPAIR_SINGLE_PATH_REQUIRED");
    const auto positions=original.at("points").To<Array>(),axes=original.at("directions").To<Array>();
    const auto baseQuery=input.Rapids->Queries.front().Geometry;
    const bool polyline=baseQuery.Manual&&!baseQuery.ConstructedManual||baseQuery.Kind==iCAX::GeometryServices::RapidPathKind::Retract;
    struct Point{std::size_t Index;RepairMath::Vector Position,Direction;Array Directions;};std::vector<Point> points;
    for(const auto& value:input.RepairLocations){const auto location=value.To<Map>();const auto index=static_cast<std::size_t>(Integer(location,"rapidPointIndex"));
        if(index>=positions.size()||!polyline&&index&&index+1<positions.size())throw std::invalid_argument("CAM_REPAIR_EDITABLE_KEYPOSE_REQUIRED");
        auto position=RegistrationPoint(positions[index]),direction=RegistrationPoint(axes[index]);
        for(const auto& origin:input.RepairOrigins){const auto p=origin.To<Map>();if(String(p,"from")==from&&String(p,"to")==to&&Integer(p,"rapidPointIndex")==index){position=RegistrationPoint(p.at("position"));direction=RepairMath::Unit(RegistrationPoint(p.at("direction")));break;}}
        result->RepairOrigins.emplace_back(Map{{"from",from},{"to",to},{"rapidPointIndex",std::to_string(index)},{"position",GenPoint(position)},{"direction",GenPoint(direction)}});
        auto choices=RepairDirections(direction,input.MaximumDegrees*std::numbers::pi/180);choices.insert(choices.begin(),GenPoint(direction));
        points.push_back({index,position,direction,std::move(choices)});}
    std::set<std::size_t> targets;for(const auto& point:points)targets.insert(point.Index);
    const auto unresolved=[&](const Text& reason){result->Unresolved=Array{Map{{"reason",reason},{"from",from},{"to",to},{"index",std::to_string(input.Selected)}}};return result;};
    if(input.RepairPhase!="Limits")for(const auto& value:input.Report.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>(),location=row.at("location").To<Map>();
        if(String(location,"role")!="Rapid"||String(location,"from")!=from||String(location,"to")!=to)continue;
        const auto index=static_cast<std::size_t>(Integer(location,"rapidPointIndex"));if(targets.contains(index))continue;
        const bool adjacent=std::any_of(points.begin(),points.end(),[&](const auto& p){return polyline?(p.Index&&index+1==p.Index||index==p.Index+1):true;});
        if(adjacent&&String(row,"status")!="Clear"&&(input.RepairPhase=="All"||String(row,"reason")=="CAM_HEAD_LIMIT_NO_SOLUTION"))return unresolved("CAM_REPAIR_NEIGHBOR_KEYPOINT_NOT_CLEAR");}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);bool incomplete=false;
    const auto stop=[&]{return cancelled->load()||std::chrono::steady_clock::now()>=deadline;};
    const auto attempt=[&](unsigned mask,std::size_t direction,double distance){
        if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();if(stop()){incomplete=true;return false;}
        ++result->Candidates;RepairProgress(progress,distance==0?"SearchingAngles":"SearchingPosition",result->Candidates,input.MaximumDegrees);
        auto query=baseQuery;Map manual=original.contains("manualPath")?original.at("manualPath").To<Map>():Map{};
        if(!polyline){query.Manual=true;query.ConstructedManual=true;if(!manual.contains("construction"))manual["construction"]=original.at("construction");}
        if(polyline){manual.erase("construction");Array innerPositions(positions.begin()+1,positions.end()-1),innerAxes(axes.begin()+1,axes.end()-1);manual["points"]=innerPositions;manual["directions"]=innerAxes;
            query.Manual=true;query.ConstructedManual=false;query.TurnPoint.reset();query.Waypoints.clear();for(std::size_t i=1;i+1<positions.size();++i)query.Waypoints.push_back({RegistrationPoint(positions[i]),RegistrationPoint(axes[i])});}
        for(std::size_t i=0;i<points.size();++i)if(mask&(1u<<i)){const auto& point=points[i];const auto axis=RegistrationPoint(point.Directions.at(direction));const auto position=RepairMath::Add(point.Position,RepairMath::Scale(point.Direction,distance));
            if(point.Index==0||point.Index+1==positions.size()){const auto name=point.Index==0?"start":"end";manual[name]=Map{{"point",GenPoint(position)},{"direction",GenPoint(axis)}};
                auto& end=point.Index==0?query.Start:query.End;end.Direction=axis;end.Point=RepairMath::Sub(position,RepairMath::Scale(axis,query.FollowHeight));}
            else{auto p=manual.at("points").To<Array>(),a=manual.at("directions").To<Array>();p[point.Index-1]=GenPoint(position);a[point.Index-1]=GenPoint(axis);manual["points"]=std::move(p);manual["directions"]=std::move(a);query.Waypoints[point.Index-1]={position,axis};}}
        const auto calculated=geometry->Generate({query},stop).front();if(!calculated.Generated){incomplete=true;return false;}const auto& poses=calculated.Poses;if(poses.size()<2)throw std::runtime_error("CAM_REPAIR_PATH_INVALID");
        auto candidate=input.Collision;candidate.Frames.clear();candidate.Query.Segments.clear();candidate.SegmentIndices.clear();candidate.KeypointFrames.clear();candidate.KeypointSegments.clear();candidate.KinematicKeyframes.clear();candidate.KeyposeLocations.clear();
        const auto seed=input.Collision.Frames.at(Integer(firstLocation,"frameIndex")-Integer(firstLocation,"rapidPointIndex")).To<Map>();const auto rotationValues=seed.at("rotation").To<Array>();RepairMath::Quaternion rotation{};for(int k=0;k<4;++k)rotation[k]=RegistrationNumber(rotationValues[k]);auto previous=RegistrationPoint(axes.front());
        const auto clearance=RegistrationNumber(input.Settings.at("collisionThresholdMm"))+RegistrationNumber(original.at("policy").To<Map>().at("marginMm"));
        std::vector<iCAX::GeometryServices::RigidMotionPose> frames;std::set<std::size_t> selected;
        for(std::size_t i=0;i<poses.size();++i){rotation=CollisionRotation::Step(previous,poses[i].Direction,rotation);previous=poses[i].Direction;frames.push_back({poses[i].Point,rotation});
            candidate.Frames.emplace_back(Map{{"position",GenPoint(poses[i].Point)},{"rotation",Array{rotation[0],rotation[1],rotation[2],rotation[3]}},{"role",Text("Rapid")},{"intentId",to},{"order",firstLocation.at("order")}});
            if(polyline||i==0||i+1==poses.size()){candidate.KinematicKeyframes.push_back(i);auto location=firstLocation;location["frameIndex"]=std::to_string(i);location["rapidPointIndex"]=std::to_string(i);location["rapidPointCount"]=std::to_string(poses.size());location["label"]=i==0?Text("起点"):i+1==poses.size()?Text("终点"):Text("关键点 ")+std::to_string(i);candidate.KeyposeLocations[i]=std::move(location);}
        }
        for(const auto& point:points)selected.insert(point.Index+1==positions.size()?poses.size()-1:point.Index);
        std::set<std::size_t> rows,keys;for(const auto index:selected){const auto at=std::lower_bound(candidate.KinematicKeyframes.begin(),candidate.KinematicKeyframes.end(),index);const auto a=at==candidate.KinematicKeyframes.begin()?index:*std::prev(at),b=std::next(at)==candidate.KinematicKeyframes.end()?index:*std::next(at);
            for(auto j=a;j<b;++j)rows.insert(j);for(auto key:candidate.KinematicKeyframes)if(key>=a&&key<=b)keys.insert(key);}
        candidate.FullSegmentCount=poses.size()-1;for(const auto index:rows){candidate.SegmentIndices.push_back(index);candidate.Query.Segments.push_back({frames[index],frames[index+1],clearance,true});}
        for(const auto index:keys){candidate.KeypointFrames.push_back(index);candidate.KeypointSegments.push_back({frames[index],frames[index],clearance,true});}
        if(input.RepairPhase=="Limits"){
            candidate.Query.Segments.clear();candidate.SegmentIndices.clear();candidate.KeypointFrames.clear();candidate.KeypointSegments.clear();
            for(const auto index:selected){candidate.KeypointFrames.push_back(index);candidate.KeypointSegments.push_back({frames[index],frames[index],clearance,true});}
        }
        candidate.ConfigureFullHead(candidate);RepairProgress(progress,"CheckingLimits",result->Candidates,input.MaximumDegrees);
        if(!candidate.CheckHeadLimits(stop)){incomplete|=candidate.HeadLimitsUnverified;return false;}
        if(stop()){incomplete=true;return false;}
        if(input.RepairPhase!="Limits"){
        RepairProgress(progress,"CheckingCollision",result->Candidates,input.MaximumDegrees);
        const auto checkedKeys=candidate.CheckFullHead(candidate.KeypointSegments,true,{},stop,{},{});
        if(stop()||checkedKeys.Segments.size()!=candidate.KeypointSegments.size()){incomplete=true;return false;}
        for(std::size_t i=0;i<checkedKeys.Segments.size();++i){const auto& row=checkedKeys.Segments[i];if(input.RepairPhase=="Collision"&&!selected.contains(candidate.KeypointFrames[i]))continue;
        if(row.Status!="Clear"){if(row.Status=="Unchecked"&&row.Reason!="CAM_HEAD_LIMIT_NO_SOLUTION")incomplete=true;return false;}}
        const auto checked=candidate.CheckFullHead(candidate.Query.Segments,false,{},stop,{},{});if(stop()||checked.Segments.size()!=candidate.Query.Segments.size()){incomplete=true;return false;}for(const auto& value:candidate.InterpolationChecks){const auto row=value.To<Map>();if(input.RepairPhase=="Collision"&&String(row,"status")=="Skipped")continue;if(String(row,"status")!="Clear"){if(String(row,"status")=="Unchecked")incomplete=true;return false;}}
        }
        Array p,a;for(const auto& pose:poses){p.emplace_back(GenPoint(pose.Point));a.emplace_back(GenPoint(pose.Direction));}
        Map output{{"points",p},{"directions",a},{"frame",Text("Setup")},{"referencePoint",Text("ToolTip")},{"planningMethod",Text("Manual")},{"geometryStatus",Text("Draft")},{"validation",Text("Unchecked")},{"lengthMm",calculated.Length},{"estimatedSeconds",calculated.EstimatedSeconds}};AppendRapidDefinition(output,calculated);
        result->Overrides[pair]=Map{{"from",from},{"to",to},{"policy",original.at("policy")},{"manualPath",ManualRapidPath(manual)},{"result",output}};result->Solved=true;result->Verified=true;
        result->Adjustments=Array{Map{{"from",from},{"to",to},{"translationMm",distance}}};return true;
    };
    const auto masks=1u<<points.size();
    try{for(std::size_t d=1;d<points.front().Directions.size();++d)for(unsigned mask=1;mask<masks;++mask)if(attempt(mask,d,0))return result;
        if(input.MaximumTranslationMm>0)for(double fraction:{.25,.5,.75,1.})for(std::size_t d=0;d<points.front().Directions.size();++d)for(unsigned mask=1;mask<masks;++mask)if(attempt(mask,d,input.MaximumTranslationMm*fraction))return result;
    }catch(const iCAX::Tasks::TaskCanceledException&){throw;}catch(const std::exception&){if(cancelled->load())throw iCAX::Tasks::TaskCanceledException();if(!stop())throw;incomplete=true;}
    return unresolved(incomplete?"CAM_REPAIR_SEARCH_UNVERIFIED":"CAM_REPAIR_NO_SOLUTION_IN_TOLERANCE");
}
