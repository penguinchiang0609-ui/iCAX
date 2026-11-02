struct RapidOuterBox {
    using V=RapidRotationMath::Vector;
    V Min{1e100,1e100,1e100},Max{-1e100,-1e100,-1e100};
    void Add(const V& point){for(int j=0;j<3;++j){if(!std::isfinite(point[j]))throw std::invalid_argument("CAM_RAPID_BOX_INVALID");Min[j]=std::min(Min[j],point[j]);Max[j]=std::max(Max[j],point[j]);}}
    bool Valid()const{return Min[0]<=Max[0]&&Min[1]<=Max[1]&&Min[2]<=Max[2];}
};
RapidOuterBox::V RapidBoxPoint(const std::array<double,16>& m,const RapidOuterBox::V& p){RapidOuterBox::V result{};for(int r=0;r<3;++r){result[r]=m[r*4+3];for(int c=0;c<3;++c)result[r]+=m[r*4+c]*p[c];}return result;}
RapidOuterBox RapidWorkpieceOuterBox(Scene& scene,const CFiveAxisPlacementComponent& placement){
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement.GetWorkpieceID());const auto ref=workpiece->GetGeometryRef();
    const auto key=iCAX::Data::VariantSerializer::Serialize(WireRef(ref));const auto runtime=Runtime(scene);auto found=runtime->RapidWorkpieceBounds.find(key);
    if(found==runtime->RapidWorkpieceBounds.end()){
        const auto geometry=FixtureGeometry(scene,ref);RapidOuterBox local;
        if(geometry->Faces.empty()){for(const auto& record:geometry->Triangulations3)for(const auto& p:record.Geometry.Vertices)local.Add({p.X,p.Y,p.Z});}
        else{
            const auto bounds=scene.Services().Resolve<iCAX::GeometryServices::IBRepCollisionBoundsService>();if(!bounds)throw std::runtime_error("CAM_RAPID_BOX_SERVICE_REQUIRED");
            // Conservative CAD bounds require no refined collision mesh.
            for(const auto& box:bounds->Boxes(*geometry,[&]{return scene.IsStopRequested();}))for(int bits=0;bits<8;++bits){auto p=box.Center;for(int j=0;j<3;++j)p=RapidRotationMath::Add(p,RapidRotationMath::Scale(box.Axes[j],box.HalfExtents[j]*(bits&(1<<j)?1:-1)));local.Add(p);}
        }
        if(!local.Valid())throw std::runtime_error("CAM_RAPID_WORKPIECE_BOUNDS_REQUIRED");
        if(runtime->RapidWorkpieceBounds.size()>=128)runtime->RapidWorkpieceBounds.erase(runtime->RapidWorkpieceBounds.begin());
        found=runtime->RapidWorkpieceBounds.emplace(key,std::array<double,6>{local.Min[0],local.Min[1],local.Min[2],local.Max[0],local.Max[1],local.Max[2]}).first;
    }
    RapidOuterBox result;const auto matrix=RigidMatrix(placement.GetTransform());
    for(int bits=0;bits<8;++bits){RapidOuterBox::V p{};for(int j=0;j<3;++j)p[j]=found->second[j+(bits&(1<<j)?3:0)];result.Add(RapidBoxPoint(matrix,p));}return result;
}
// Defaults are suggested once from the setup bounds. Saved coordinates are
// absolute setup-frame values, never expanded by head size or collision checks.
RapidOuterBox RapidSetupOuterBox(Scene& scene,const ID& setupID){
    RapidOuterBox box;
    for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
        if(const auto placement=Component<CFiveAxisPlacementComponent>(entity);placement&&placement->GetSetupID()==setupID){
            const auto bounds=RapidWorkpieceOuterBox(scene,*placement);box.Add(bounds.Min);box.Add(bounds.Max);
        }
    if(!box.Valid())box.Add({0,0,0});return box;
}
// The prescribed torch direction points out of the workpiece; +Z therefore
// corresponds to the neutral beam direction (0,0,-1). Retract along the actual
// tool axis first, then clear the side, square, lift, and transfer over the part.
Map SetRapidQuickWaypoints(iCAX::GeometryServices::RapidPathGeometryQuery& query,double normalRetraction,double sideRetraction,double safeZ){
    namespace M=RapidRotationMath;using V=RapidOuterBox::V;const V neutral{0,0,1};
    const auto start=M::Add(query.Start.Point,M::Scale(query.Start.Direction,query.FollowHeight)),end=M::Add(query.End.Point,M::Scale(query.End.Direction,query.FollowHeight));
    const auto face=[](const V& direction){int axis=2;for(int j=0;j<3;++j)if(std::abs(direction[j])>std::abs(direction[axis])+1e-12)axis=j;return axis*2+(direction[axis]>0?1:0);};
    const auto startFace=face(query.Start.Direction),endFace=face(query.End.Direction);
    if(startFace==4||endFace==4)throw std::invalid_argument("CAM_RAPID_BOTTOM_APPROACH_UNSUPPORTED");
    const auto normalStart=M::Add(start,M::Scale(query.Start.Direction,normalRetraction)),normalEnd=M::Add(end,M::Scale(query.End.Direction,normalRetraction));
    auto withdrawnStart=normalStart,withdrawnEnd=normalEnd;
    if(startFace/2<2)withdrawnStart[startFace/2]+=(startFace%2?1:-1)*sideRetraction;
    if(endFace/2<2)withdrawnEnd[endFace/2]+=(endFace%2?1:-1)*sideRetraction;
    if(safeZ<std::max(withdrawnStart[2],withdrawnEnd[2])-1e-8)throw std::invalid_argument("CAM_RAPID_SAFE_Z_BELOW_RETRACT");
    query.Manual=true;query.ConstructedManual=false;query.Waypoints.clear();
    const auto append=[&](const V& point,const V& direction){if(query.Waypoints.empty()||M::Length(M::Sub(query.Waypoints.back().Point,point))>1e-10||M::Length(M::Sub(query.Waypoints.back().Direction,direction))>1e-10)query.Waypoints.push_back({point,direction});};
    append(normalStart,query.Start.Direction);append(withdrawnStart,query.Start.Direction);append(withdrawnStart,neutral);
    auto top=withdrawnStart;top[2]=safeZ;append(top,neutral);
    // Each transfer changes just one machine coordinate. All traversal above
    // the workpiece uses the same neutral orientation, including opposite sides.
    top[0]=withdrawnEnd[0];append(top,neutral);top[1]=withdrawnEnd[1];append(top,neutral);
    append(withdrawnEnd,neutral);append(withdrawnEnd,query.End.Direction);append(normalEnd,query.End.Direction);
    return {{"safeZMm",safeZ},{"sideRetractDistanceMm",sideRetraction},{"normalRetractDistanceMm",normalRetraction},{"startFace",std::to_string(startFace)},{"endFace",std::to_string(endFace)}};
}
void QualifyQuickRapidMethod(const Map& record,iCAX::GeometryServices::RapidPathPlanningResult& result){
    if(!record.contains("quickMotion"))return;result.Method="QuickRetract";
    if(result.Reason=="RAPID_MANUAL_PATH_BLOCKED")result.Reason="RAPID_QUICK_ROUTE_BLOCKED";
}