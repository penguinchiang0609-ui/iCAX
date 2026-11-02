// Display samples are queries on the exact common field, never saved geometry.
namespace
{
Map ToolAxisSamples(Scene& scene,const AxisPath& path)
{
 namespace Walk=iCAX::GeometryServices::OffsetTraversal;const auto context=FreezeIntentContext(scene,IntentSourceOperation(scene,path.Port));CFiveAxisIntentPoseField field(*context,path.Value);const auto service=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();Walk::Evaluator evaluator(*service);const auto exact=IntentExactCurve(path.Value);
 // Every station uses the same immutable exact path. Integrate completed
 // segments once, preserving AxisDistance's trimmed current-segment domain
 // and integration tolerance instead of measuring the whole prefix again.
 std::vector<double> prefix{0.};
 for(const auto& segment:exact.Segments){iCAX::GeometryData::CompositeOffsetCurve3 one;one.Segments={segment};prefix.push_back(prefix.back()+evaluator.Length(one));}
 const auto distanceAt=[&](std::size_t index,double u){
  const auto start=Walk::Start(exact.Segments[index]);if(std::abs(start-u)<1e-12)return prefix[index];
  if(u==Walk::End(exact.Segments[index]))return prefix[index+1];
  iCAX::GeometryData::CompositeOffsetCurve3 partial;partial.Segments={exact.Segments[index]};
  auto& range=Walk::Range(partial.Segments.front());range.First=std::min(start,u);range.Last=std::max(start,u);
  return prefix[index]+evaluator.Length(partial);
 };
 const auto length=prefix.back();const auto sampled=evaluator.SamplePoints(exact,std::max(.01,length/3000.),std::max(.05,length/36.));std::vector<std::pair<std::size_t,double>> locations;double since=std::numeric_limits<double>::infinity();iCAX::GeometryData::Point3 previous;bool first=true;
 for(std::size_t i=0;i<sampled.Points.size();++i){const auto& sample=sampled.Points[i];if(!first)since+=Walk::Distance(previous,sample.Value.OffsetPoint);previous=sample.Value.OffsetPoint;first=false;const auto& role=path.Value.Geometry.Segments[sample.PieceIndex].ConstructionRole;if(role=="LeadIn"||role=="LeadOut")continue;const bool begin=i==0||sampled.Points[i-1].PieceIndex!=sample.PieceIndex,end=i+1==sampled.Points.size()||sampled.Points[i+1].PieceIndex!=sample.PieceIndex;if(begin||end||since>=length/36.){locations.emplace_back(sample.PieceIndex,sample.Value.SourceParameter);since=0;}}
 PositionOffsets positionOffsets(path.Value,exact,evaluator);const auto points=AxisPointList(scene,path);for(const auto& item:points)locations.push_back(AxisLocation(path,item.To<Map>().at("anchor").To<Map>()));for(const auto& item:AxisActiveControls(path))locations.push_back(AxisLocation(path,item.To<Map>().at("anchor").To<Map>()));if(path.Value.Channels.contains("poseField"))for(const auto& item:path.Value.Channels.at("poseField").To<Map>().at("entries").To<Array>()){const auto range=iCAX::CAM::Intent::RangeFromData(item.To<Map>().at("range").To<Map>());locations.push_back(AxisLocation(path,iCAX::CAM::Intent::ToData(range.Start)));locations.push_back(AxisLocation(path,iCAX::CAM::Intent::ToData(range.End)));}
 std::sort(locations.begin(),locations.end());locations.erase(std::unique(locations.begin(),locations.end(),[](const auto& a,const auto& b){return a.first==b.first&&std::abs(a.second-b.second)<1e-12;}),locations.end());if(locations.size()>4096)throw std::invalid_argument("CAM_POSE_DISPLAY_SAMPLE_BUDGET");Array samples;
 for(const auto& at:locations){const auto& role=path.Value.Geometry.Segments[at.first].ConstructionRole;if(role=="LeadIn"||role=="LeadOut")continue;const auto anchor=AxisAnchor(path,at.first,at.second);Array ids;for(const auto& item:points){const auto point=item.To<Map>();if(SameAxisAnchor(path,anchor,point.at("anchor").To<Map>()))ids.emplace_back(point.at("id"));}const auto position=positionOffsets.At(at.first,at.second);samples.emplace_back(Map{{"point",GenPoint(position)},{"surfaceNormal",GenPoint(GenVec(field.NormalAt(at.first,at.second)))},{"direction",GenPoint(GenVec(field.At(at.first,at.second)))},{"pathDistanceMm",distanceAt(at.first,at.second)},{"anchor",anchor},{"keypointIds",ids}});}
 return {{"intentId",iCAX::Data::to_string(path.PathId)},{"placementId",iCAX::Data::to_string(ReadFiveAxisIntentMetadata(path.Value).PlacementID)},{"closed",path.Value.Geometry.Closed},{"lengthMm",length},{"samples",samples}};
}
}
Map ReadToolAxisSamples(Scene& scene,const Map& request)
{
 CutFields(request,{"expected","intentIds"});CheckExpected(*Runtime(scene),request);std::set<ID> requested;if(request.contains("intentIds")){const auto ids=request.at("intentIds").To<Array>();if(ids.size()>256)throw std::invalid_argument("CAM_POSE_DISPLAY_SCOPE_BUDGET");for(const auto& item:ids){const auto id=CutID(item);(void)ReadIntentPath(scene,id);if(!requested.insert(id).second)throw std::invalid_argument("CAM_CUT_DUPLICATE");}}Array cuts,errors;std::size_t stations=0,considered=0;
 for(const auto& cut:IntentCuts(scene)){if(request.contains("intentIds")&&!requested.contains(cut->GetID())||!cut->Path.Participating)continue;if(++considered>256)throw std::invalid_argument("CAM_POSE_DISPLAY_SCOPE_BUDGET");try{auto row=ToolAxisSamples(scene,cut->Path);stations+=row.at("samples").To<Array>().size();if(stations>32768)throw std::invalid_argument("CAM_POSE_DISPLAY_SAMPLE_BUDGET");cuts.emplace_back(std::move(row));}catch(const std::exception& error){errors.emplace_back(Map{{"intentId",iCAX::Data::to_string(cut->GetID())},{"error",Text(error.what())}});}}
 return {{"frame",Text("Workpiece")},{"geometry",Text("ExactIntentToolpath")},{"cuts",cuts},{"errors",errors}};
}
