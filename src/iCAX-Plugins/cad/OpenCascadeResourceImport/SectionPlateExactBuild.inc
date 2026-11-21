std::vector<SectionPlate> BuildExactPlates(const SectionPlateInput& input,const std::function<bool()>& cancelled){
    using namespace plate_exact;const PlateDirectionFrame direction(input.SupportDirection);auto in=direction.Align(input);
    const auto started=std::chrono::steady_clock::now();std::mutex cancellationMutex,traceMutex;
    const Check check=[&]{std::lock_guard lock(cancellationMutex);if(cancelled&&cancelled())throw std::runtime_error("PLATE_CANCELLED");};
    const auto trace=[&](const std::string& text){if(std::getenv("ICAX_FIXTURE_TRACE")){std::lock_guard lock(traceMutex);std::cerr<<"fixture "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<" ms "<<text<<std::endl;}};
    const bool skeleton=in.Automatic&&in.Pattern=="Skeleton";
    if(skeleton&&!in.CountLayout&&(!std::isfinite(in.MaximumSupportSpan)||in.MaximumSupportSpan<=in.Thickness+.001))throw std::invalid_argument("PLATE_SPACING_INVALID");
    if(skeleton&&in.CountLayout&&(in.SupportCount<2||in.ConnectionCount<2||in.SupportCount>32||in.ConnectionCount>32||in.SupportCount+in.ConnectionCount>40))throw std::invalid_argument("PLATE_COUNT_INVALID");
    if(!in.Geometry||in.FaceIDs.empty()||in.Stations.empty()||in.Stations.size()>128)throw std::invalid_argument("PLATE_INPUT_REQUIRED");
    for(double n:{in.Thickness,in.BaseThickness,in.BaseDrop,in.Margin,in.Gap,in.MinimumWeb})if(!std::isfinite(n)||n<=0||n>10000)throw std::invalid_argument("PLATE_DIMENSION_INVALID");
    if(in.Gap<.01||in.BaseDrop<in.MinimumWeb+in.Gap)throw std::invalid_argument("PLATE_DIMENSION_INVALID");ValidateJoints(in);ValidateWindows(in);ValidateKeepOuts(in);
    if(in.AvoidOpeningBoundaries&&(!std::isfinite(in.OpeningBoundaryMargin)||in.OpeningBoundaryMargin<0||in.OpeningBoundaryMargin>10000))throw std::invalid_argument("PLATE_OPENING_MARGIN_INVALID");
    if(!std::isfinite(in.OpeningReliefDepth)||in.OpeningReliefDepth<0||in.OpeningReliefDepth>1000)throw std::invalid_argument("PLATE_OPENING_DEPTH_INVALID");
    check();const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);Require(built.bOK&&!built.Shape.IsNull(),"PLATE_BREP_INVALID");
    const TopLoc_Location location(PlateDirectionFrame::Transform(in.Transform));const auto work=built.Shape.Moved(location);const auto bounds=Bounds(work);const double x0=bounds[0],y0=bounds[1],z0=bounds[2],x1=bounds[3],y1=bounds[4],floor=z0-in.BaseDrop;
    trace("BRep ready");
    double deviation=0;size_t nonExit=0;const auto openingCounts=AddOpeningBoundaryKeepOuts(in,built,location,floor-in.BaseThickness,deviation,nonExit,check);ValidateKeepOuts(in);trace("opening keep-outs ready");
    std::vector<std::pair<TopoDS_Face,bool>> faces;for(auto id:in.FaceIDs)Require(built.Faces.contains(id),"PLATE_FACE_MISSING");for(const auto& [id,face]:built.Faces)faces.emplace_back(TopoDS::Face(face.Moved(location)),std::find(in.FaceIDs.begin(),in.FaceIDs.end(),id)!=in.FaceIDs.end());
    const double cx=in.AreaWidth>0?in.AreaX:(x0+x1)/2,cy=in.AreaDepth>0?in.AreaY:(y0+y1)/2,width=in.AreaWidth>0?in.AreaWidth:(in.BaseShape=="Circle"?std::max(x1-x0,y1-y0):x1-x0)+2*in.Margin,depth=in.BaseShape=="Circle"?width:in.AreaDepth>0?in.AreaDepth:y1-y0+2*in.Margin;
    const double lower=z0-floor-in.Gap;Require(lower>=in.MinimumWeb,"PLATE_MINIMUM_WEB");
    std::vector<std::optional<Plate>> prepared(in.Stations.size());std::vector<size_t> rejected(in.Stations.size(),0);
    const auto workers=ForPlateIndices(in.Stations.size(),in.MaximumConcurrency,[&](size_t i,size_t){check();const auto station=in.Stations[i];if(!std::isfinite(station.Coordinate))throw std::invalid_argument("PLATE_STATION_OVERLAP");const double delta=std::abs(station.Coordinate-(station.AlongX?cx:cy))+in.Thickness/2,radius=(station.AlongX?width:depth)/2-in.Margin;
        if(delta>=radius){if(in.Automatic)return;throw std::invalid_argument("PLATE_SUPPORT_OUTSIDE_BASE");}
        const double center=station.AlongX?cy:cx,reach=in.BaseShape=="Circle"?std::sqrt(radius*radius-delta*delta):(station.AlongX?depth:width)/2-in.Margin,from=std::max(station.From,center-reach),to=std::min(station.To,center+reach);
        try{auto p=Section(work,faces,in,i,floor,from,to,lower,check);KeepOuts(p,in,check);Contacts(p,check);if(!p.Data.Connection&&p.Data.Contacts.empty())throw std::runtime_error("PLATE_SELECTED_SUPPORT_OBSTRUCTED");prepared[i]=std::move(p);trace("BRep profile "+std::to_string(i)+" span="+std::to_string(prepared[i]->Data.Span[0])+","+std::to_string(prepared[i]->Data.Span[1]));}
        catch(const std::runtime_error& e){const std::string error=e.what();if(in.Automatic&&(error=="PLATE_STATION_NO_SUPPORT"||error=="PLATE_SUPPORT_TOO_NARROW"||error=="PLATE_SELECTED_SUPPORT_OBSTRUCTED"||error.starts_with("PLATE_KEEPOUT_"))){rejected[i]=1;return;}throw;}
    });trace("BRep profiles complete workers="+std::to_string(workers));
    std::vector<Plate> candidates;size_t rejectedCount=0;for(size_t i=0;i<prepared.size();++i){rejectedCount+=rejected[i];if(prepared[i])candidates.push_back(std::move(*prepared[i]));}
    const auto select=[&]{if(!skeleton)return candidates;std::vector<SectionPlate> data;for(const auto& p:candidates)data.push_back(p.Data);const auto selected=SelectSkeletonPlates(in,data,x0,y0,x1,y1);std::vector<Plate> result;for(const auto& d:selected){const auto p=std::find_if(candidates.begin(),candidates.end(),[&](const auto& p){return p.Data.StationIndex==d.StationIndex;});result.push_back(*p);}return result;};
    Plate base;base.Data.Base=true;base.Data.Thickness=in.BaseThickness;base.Data.Distance=in.BaseDrop;base.Data.Frame={1,0,0,0,0,1,0,0,0,0,1,floor-in.BaseThickness,0,0,0,1};
    if(in.BaseShape=="Rectangle")base.Material=Rect(cx-width/2,cy-depth/2,cx+width/2,cy+depth/2);
    else if(in.BaseShape=="Circle"){const auto edge=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(cx,cy,0),gp_Dir(0,0,1)),width/2)).Edge();base.Material=BRepBuilderAPI_MakeFace(Plane,BRepBuilderAPI_MakeWire(edge).Wire(),true).Face();}
    else throw std::invalid_argument("PLATE_BASE_SHAPE_INVALID");RefreshSpan(base);KeepOuts(base,in,check);
    if(in.JointMode=="Weld"&&!in.KeepOuts.empty())for(auto p=candidates.begin();p!=candidates.end();){try{Foot(*p,base,in,check);++p;}catch(const std::runtime_error&){if(!in.Automatic)throw;++rejectedCount;p=candidates.erase(p);}}
    auto plates=select();trace("BRep layout selected count="+std::to_string(plates.size()));const auto pristineBase=base;
    if(in.JointMode=="TabSlot"||skeleton)for(;;){try{Joints(plates,base,in,check);break;}catch(const PlateStationError& e){trace("joint rejected station="+std::to_string(e.StationIndex)+" "+e.what());if(!in.Automatic||(!skeleton&&in.KeepOuts.empty()))throw;const auto before=candidates.size();candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& p){return p.Data.StationIndex==e.StationIndex;}),candidates.end());Require(!candidates.empty()&&candidates.size()!=before,"PLATE_KEEPOUT_NO_SUPPORT");++rejectedCount;plates=select();base=pristineBase;}}
    trace("BRep joints complete");
    const auto layout=plates;plates.push_back(base);
    // Weld foot queries read the pre-window support layout. Keep it private
    // while support preparation may rebuild edges using its original vertices.
    std::vector<Plate> footLayout;if(in.JointMode=="Weld"){footLayout=layout;std::vector<TopoDS_Shape> materials;for(const auto& foot:footLayout)materials.push_back(foot.Material);const auto copies=CheckCopies(materials);for(size_t i=0;i<footLayout.size();++i)footLayout[i].Material=TopoDS::Face(copies[i]);}
    // Prepare every immutable solid, then put independent checks on the same
    // bounded queue. A slow plate can use idle workers without nested pools.
    // Keep failures until all readers drain, in original plate/check order.
    std::vector<std::array<std::exception_ptr,5>> failures(plates.size());std::vector<double> distances(plates.size());
    const auto run=[&](size_t i,size_t stage,const std::string& label,const auto& action){const auto begin=std::chrono::steady_clock::now();try{action();trace("plate "+std::to_string(i)+" "+label+" complete duration="+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count())+" ms");}
        catch(const std::exception& e){failures[i][stage]=std::current_exception();trace("plate "+std::to_string(i)+" "+label+" failed: "+e.what());}catch(...){failures[i][stage]=std::current_exception();}};
    ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){run(i,0,"preparation",[&]{auto& p=plates[i];const auto phase=[&](const std::string& label){trace("plate "+std::to_string(i)+" "+label);};Windows(p,layout,in,check);phase("windows complete");if(in.JointMode=="Weld"&&p.Data.Base)for(const auto& foot:footLayout)Foot(foot,p,in,check);PrepareFinish(p,in,check,phase);distances[i]=p.Data.Distance;});});
    struct ValidationJob {size_t Index,Stage,Cost;};std::vector<ValidationJob> jobs;
    const auto topologySize=[](const TopoDS_Shape& shape){if(shape.IsNull())return size_t{0};TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);return static_cast<size_t>(faces.Extent())+edges.Extent();};
    for(size_t i=0;i<plates.size();++i)if(!failures[i][0]){const auto& p=plates[i];const auto sides=topologySize(p.Solid);if(!p.BlankRemainder.IsNull())jobs.push_back({i,1,sides*topologySize(p.BlankRemainder)});if(!p.Data.Base){const auto distanceCost=p.Data.Contacts.empty()?sides*topologySize(Has(p.WorkInSlab,TopAbs_VERTEX)?p.WorkInSlab:p.Work):0;jobs.push_back({i,2,distanceCost});jobs.push_back({i,3,sides*topologySize(p.WorkInSlab)});}}
    // Topological face/edge pairs estimate work without evaluating geometry.
    // Start larger independent queries first so the final plate is not a
    // serial tail; results and errors still follow the original plate order.
    std::stable_sort(jobs.begin(),jobs.end(),[](const auto& a,const auto& b){return a.Cost>b.Cost;});
    const auto checkWorkers=ForPlateIndices(jobs.size(),in.MaximumConcurrency,[&](size_t index,size_t){const auto job=jobs[index];const auto& p=plates[job.Index];
        if(job.Stage==1)run(job.Index,1,"stock check",[&]{CheckStock(p,check);});
        else if(job.Stage==2)run(job.Index,2,"distance",[&]{distances[job.Index]=CheckDistance(p,in,check);});
        else run(job.Index,3,"intersection",[&]{CheckPenetration(p,check);});});trace("BRep checks complete workers="+std::to_string(checkWorkers));
    ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){for(const auto& failure:failures[i])if(failure)return;run(i,4,"profile",[&]{plates[i].Data.Distance=distances[i];FinishProfile(plates[i],check);trace("BRep solid "+std::to_string(i));});});
    for(const auto& plateFailures:failures)for(const auto& failure:plateFailures)if(failure)std::rethrow_exception(failure);
    auto& bd=plates.back().Data;bd.RejectedStationCount=rejectedCount;bd.OpeningBoundaryCount=openingCounts.first;bd.UnclassifiedOpeningFaceCount=openingCounts.second;bd.OpeningNonExitBoundaryCount=nonExit;bd.OpeningPlaneDeviationUpper=deviation;
    GProp_GProps mass;bool volume=false;if(Has(work,TopAbs_SOLID)){BRepGProp::VolumeProperties(work,mass);volume=std::abs(mass.Mass())>1e-9;}if(!volume)BRepGProp::SurfaceProperties(work,mass);Require(mass.Mass()>0,"PLATE_BREP_EMPTY");const auto center=mass.CentreOfMass();bd.CenterOfMass={center.X(),center.Y(),center.Z()};bd.CenterMethod=volume?"UniformSolid":"UniformSheet";
    std::vector<P2> contactPoints;size_t bearings=0;std::vector<SectionPlate> data;for(size_t i=0;i+1<plates.size();++i){const auto& d=plates[i].Data;data.push_back(d);if(!d.Contacts.empty())++bearings;for(auto p:d.Contacts)contactPoints.push_back({p[0],p[1]});}bd.SupportHull=ContactHull(std::move(contactPoints));bd.SupportMargin=-1;
    if(bd.SupportHull.size()>=3){bd.SupportMargin=INFINITY;for(size_t i=0;i<bd.SupportHull.size();++i){const auto a=bd.SupportHull[i],b=bd.SupportHull[(i+1)%bd.SupportHull.size()];bd.SupportMargin=std::min(bd.SupportMargin,Cross(a,b,{center.X(),center.Y()})/std::hypot(b[0]-a[0],b[1]-a[1]));}}bd.Supported=bearings>=2&&bd.SupportMargin>=in.Thickness;if(!in.KeepOuts.empty())Require(bd.Supported,"PLATE_KEEPOUT_INSUFFICIENT_SUPPORT");if(skeleton)RecordSkeletonCoverage(in,data,bd,x0,y0,x1,y1);
    trace("all BRep calculations complete");
    std::vector<SectionPlate> result(plates.size());ForPlateIndices(plates.size(),in.MaximumConcurrency,[&](size_t i,size_t){check();Export(plates[i],direction.ToWorld,check);direction.Restore(plates[i].Data);result[i]=std::move(plates[i].Data);});trace("BRep export and preview curves complete");return result;
}
