namespace orientation_evaluation {
using ompl_evaluation::Point;using ompl_evaluation::Transform;using ompl_evaluation::Env;
struct RealStation {std::size_t Piece=0;double Parameter=0;};
struct RealPath {
    std::string ID,Number,Kind;
    std::vector<OrientationWaypoint> Points;
    std::vector<RealStation> Stations;
    std::vector<Handle(Geom_Curve)> Curves;
    std::shared_ptr<const iCAX::Resource::CBinaryResource> Resource;
    std::vector<std::uint8_t> OriginalBytes;
};
struct RealCollision {
    std::shared_ptr<ICollisionDistanceService> Service;
    CollisionDistanceModelRef Fixed,Head,Fixture;
    TopoDS_Shape Shape,ExactHead;
    std::vector<TopoDS_Shape> ExactParts;
    Bnd_Box Bounds;
    std::vector<std::pair<TopoDS_Face,Bnd_Box>> Faces;
    std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> Solids;
    std::map<std::array<double,6>,double> Cache;
    const RealPath* Path=nullptr;
    double Follow=1,Clearance=.3,Radius=std::hypot(56.,12.);
    double MinimumCertified=std::numeric_limits<double>::infinity();
    std::size_t CoalCalls=0,ExactCalls=0,Intervals=0,ExactFaces=0;
    double CoalMs=0,ExactMs=0,ClassifyMs=0,BoundMs=0;
    Clock::time_point Deadline=Clock::now()+std::chrono::hours(1);
    double Distance(V p,V n){
        if(Clock::now()>=Deadline)return 0;
        const std::array<double,6> key{p[0],p[1],p[2],n[0],n[1],n[2]};if(auto i=Cache.find(key);i!=Cache.end())return i->second;
        const auto tip=Add(p,Scale(n,Follow));const auto pose=Pose(tip,n);++CoalCalls;const auto coalStart=Clock::now();
        double lower=Service->LowerBound(Fixed,Head,pose,Clearance+1e-5,[&]{return Clock::now()>=Deadline;}).LowerBound;
        CoalMs+=Ms(coalStart);
        if(lower<=Clearance+1e-5){
            // Large CAD tolerances/mesh bounds may be unresolved. Do not weaken
            // padding or call this a collision: resolve against nearby EXACT
            // CAD faces; faces outside the head AABB expanded by 2 mm have a
            // certified lower bound of at least 2 mm already.
            gp_Trsf trsf;trsf.SetValues(pose[0],pose[1],pose[2],pose[3],pose[4],pose[5],pose[6],pose[7],pose[8],pose[9],pose[10],pose[11]);
            // Analytic support of each circular frustum along every world
            // coordinate. Its radius varies linearly, so endpoints bound it.
            V lo{1e99,1e99,1e99},hi{-1e99,-1e99,-1e99};
            for(const auto& ring:std::vector<std::pair<double,double>>{{0,0},{16,7},{32,11},{32,12},{56,12}})for(int j=0;j<3;++j){const auto c=tip[j]+n[j]*ring.first,r=ring.second*std::sqrt(std::max(0.,1-n[j]*n[j]));lo[j]=std::min(lo[j],c-r);hi[j]=std::max(hi[j],c+r);}
            Bnd_Box headBounds;headBounds.Add(gp_Pnt(lo[0],lo[1],lo[2]));headBounds.Add(gp_Pnt(hi[0],hi[1],hi[2]));
            BRep_Builder builder;TopoDS_Compound nearFaces;builder.MakeCompound(nearFaces);std::size_t count=0;
            for(const auto& [face,box]:Faces)if(box.Distance(headBounds)<2.00001){builder.Add(nearFaces,face);++count;}
            lower=2.;if(count){const auto exactStart=Clock::now();for(const auto& part:ExactParts){++ExactCalls;ExactFaces+=count;BRepExtrema_DistShapeShape exact;exact.LoadS1(part.Moved(TopLoc_Location(trsf)));exact.LoadS2(nearFaces);exact.SetMultiThread(false);exact.Perform();
                    lower=std::min(lower,exact.IsDone()?std::max(0.,exact.Value()-1e-5):0.);if(lower<=Clearance+1e-5||Clock::now()>=Deadline)break;}ExactMs+=Ms(exactStart);}
        }
        if(lower>Clearance+1e-5){
            const auto classifyStart=Clock::now();for(double z:{0.,8.,24.,44.}){const auto probe=Add(tip,Scale(n,z));const gp_Pnt point(probe[0],probe[1],probe[2]);if(Bounds.IsOut(point))continue;
                for(const auto& solid:Solids){solid->Perform(point,1e-7);if(solid->State()==TopAbs_IN||solid->State()==TopAbs_ON){lower=0;break;}}if(lower==0)break;}ClassifyMs+=Ms(classifyStart);
        }
        if(Fixture&&lower>Clearance+1e-5){++CoalCalls;lower=std::min(lower,Service->LowerBound(Fixture,Head,pose,Clearance+1e-5,[&]{return Clock::now()>=Deadline;}).LowerBound);}
        Cache[key]=lower;return lower;
    }
    V At(std::size_t edge,double t)const{
        const auto a=Path->Stations[edge],b=Path->Stations[edge+1];
        if(a.Piece!=b.Piece){if(Length(Sub(Path->Points[edge].Position,Path->Points[edge+1].Position))>1e-5)throw std::runtime_error("REAL_CURVE_JUNCTION_NOT_EXACT");return Path->Points[edge].Position;}
        const auto p=Path->Curves[a.Piece]->Value(a.Parameter+(b.Parameter-a.Parameter)*t);return {p.X(),p.Y(),p.Z()};
    }
    double PositionBound(std::size_t edge,double a,double b,V mid){
        const auto x=Path->Stations[edge],y=Path->Stations[edge+1];if(x.Piece!=y.Piece)return 1e-5;
        const auto first=x.Parameter+(y.Parameter-x.Parameter)*a,last=x.Parameter+(y.Parameter-x.Parameter)*b;
        if(std::abs(last-first)<1e-14)return 1e-5;
        const auto boundStart=Clock::now();GeomAdaptor_Curve curve(Path->Curves[x.Piece],std::min(first,last),std::max(first,last));Bnd_Box box;BndLib_Add3dCurve::Add(curve,1e-7,box);BoundMs+=Ms(boundStart);
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);double radius=0;
        for(double xx:{x0,x1})for(double yy:{y0,y1})for(double zz:{z0,z1})radius=std::max(radius,Length(Sub({xx,yy,zz},mid)));return radius+1e-5;
    }
    bool Interval(std::size_t edge,V na,V nb,double a,double b,int depth){
        ++Intervals;if(Clock::now()>=Deadline)return false;const auto t=(a+b)*.5;const auto p=At(edge,t),n=Slerp(na,nb,t);const auto distance=Distance(p,n);
        if(distance<=Clearance+1e-5)return false;
        const auto rotation=2*(Radius+Follow)*std::sin(Angle(na,nb)*(b-a)*.25);
        const auto bound=distance-PositionBound(edge,a,b,p)-rotation;
        if(bound>Clearance+1e-5){MinimumCertified=std::min(MinimumCertified,bound);return true;}
        return depth<20&&Interval(edge,na,nb,a,t,depth+1)&&Interval(edge,na,nb,t,b,depth+1);
    }
    bool State(std::size_t i,const PathOrientation& p){return Distance(Path->Points[i].Position,p.Direction)>Clearance+1e-5;}
    bool Motion(std::size_t i,const PathOrientation& a,const PathOrientation& b){return Interval(i,a.Direction,b.Direction,0,1,0);}
    void Bind(PathOrientationQuery& q,const RealPath& path){Path=&path;q.CheckState=[&](auto i,const auto& p){return State(i,p);};q.CheckMotion=[&](auto i,const auto& a,const auto& b){return Motion(i,a,b);};}
};
Array VectorValues(V p){return {p[0],p[1],p[2]};}
Map ResultRecord(const PathOrientationResult& r){return {{"status",r.Status},{"elapsedMs",r.ElapsedMs},{"graphMs",r.GraphMs},{"validationMs",r.ValidationMs},
    {"candidateCount",std::to_string(r.CandidateCount)},{"graphBuilds",std::to_string(r.GraphBuilds)},{"stateChecks",std::to_string(r.StateChecks)},{"motionChecks",std::to_string(r.MotionChecks)}};}
std::vector<PathOrientation> Cone(V n){const auto ref=std::abs(n[2])<.8?V{0,0,1}:V{1,0,0};
    const auto x=Unit(V{n[1]*ref[2]-n[2]*ref[1],n[2]*ref[0]-n[0]*ref[2],n[0]*ref[1]-n[1]*ref[0]});const auto y=Unit(V{n[1]*x[2]-n[2]*x[1],n[2]*x[0]-n[0]*x[2],n[0]*x[1]-n[1]*x[0]});
    std::vector<PathOrientation> out;for(double degrees:{10.,20.,30.})for(int i=0;i<4;++i){const auto angle=degrees*Pi/180,phase=i*Pi/2;out.push_back({Unit(Add(Scale(n,std::cos(angle)),Scale(Add(Scale(x,std::cos(phase)),Scale(y,std::sin(phase))),std::sin(angle)))),{}});}return out;}
bool Audit(RealCollision& geometry,const PathOrientationQuery& q,const PathOrientationResult& r){
    geometry.Cache.clear();geometry.Deadline=Clock::now()+std::chrono::seconds(60);geometry.MinimumCertified=std::numeric_limits<double>::infinity();
    if(r.Status!="Solved"||r.Orientations.size()!=q.Waypoints.size())return false;
    for(std::size_t i=0;i<r.Orientations.size();++i){bool editable=false;for(const auto& range:q.EditableRanges)editable|=i>range.First&&i<range.Last;
        if((!editable||q.Waypoints[i].Locked)&&(q.Waypoints[i].Original.Direction!=r.Orientations[i].Direction||q.Waypoints[i].Original.Axes!=r.Orientations[i].Axes))return false;
        if(!geometry.State(i,r.Orientations[i])||(i&&!geometry.Motion(i-1,r.Orientations[i-1],r.Orientations[i])))return false;
    }
    return true;
}
}
TEST(PathOrientationEvaluation, DISABLED_RealSavedModel)
{
    using namespace orientation_evaluation;
    const auto projectName=Env(L"ICAX_ORIENTATION_PROJECT"),reportName=Env(L"ICAX_ORIENTATION_REPORT");ASSERT_FALSE(projectName.empty());ASSERT_FALSE(reportName.empty());
    const auto began=Clock::now();iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(projectName);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);const auto loadMs=Ms(began);
    const auto readStart=Clock::now();const auto state=Read(scene);Map setup,generated;for(const auto& v:state.at("setups").To<Array>())if(v.To<Map>().at("id")==state.at("activeSetupId")){setup=v.To<Map>();generated=setup.at("generatedGeometry").To<Map>();}
    const auto readMs=Ms(readStart);const auto originalCuts=state.at("intentToolpaths");const auto originalGenerated=generated;const auto history=scene.Database().GetUndoArray().size();
    ASSERT_EQ(1u,state.at("placements").To<Array>().size());ASSERT_TRUE(state.at("fixtures").To<Array>().empty());ASSERT_EQ("Builtin",setup.at("headModel").To<Map>().at("kind").To<Text>());
    const auto placement=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();gp_Trsf trsf;
    trsf.SetValues(placement[0].To<double>(),placement[1].To<double>(),placement[2].To<double>(),placement[3].To<double>(),placement[4].To<double>(),placement[5].To<double>(),placement[6].To<double>(),placement[7].To<double>(),placement[8].To<double>(),placement[9].To<double>(),placement[10].To<double>(),placement[11].To<double>());
    std::vector<RealPath> paths;std::size_t markCount=0,operationCount=0;
    for(const auto& value:generated.at("operations").To<Array>()){const auto op=value.To<Map>();++operationCount;if(op.at("machiningKind").To<Text>()!="Cut"){++markCount;continue;}
        for(const auto& partValue:op.at("parts").To<Array>()){const auto part=partValue.To<Map>();if(part.at("role").To<Text>()!="Cut")continue;
            RealPath path;path.ID=op.at("intentId").To<Text>();path.Number=op.at("cutNumber").To<Text>();path.Kind="Cut";const auto bytes=DataBytes(op.at("curveData").To<Map>());
            auto resource=std::make_shared<iCAX::Resource::CBinaryResource>();resource->Content=bytes;path.Resource=resource;path.OriginalBytes=bytes;
            const auto exact=GeneratedExact(scene,op);for(const auto& piece:exact.Segments){ASSERT_TRUE(std::holds_alternative<iCAX::GeometryData::CurveSegment3>(piece.Geometry))<<"Evaluation requires nominal, uncompensated curves";
                const auto& segment=std::get<iCAX::GeometryData::CurveSegment3>(piece.Geometry);auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve(segment.Curve);ASSERT_FALSE(curve.IsNull());curve->Transform(trsf);path.Curves.push_back(curve);}
            const auto points=part.at("points").To<Array>(),directions=part.at("directions").To<Array>(),samples=part.at("sourceSamples").To<Array>();ASSERT_EQ(points.size(),directions.size());ASSERT_EQ(points.size(),samples.size());
            for(std::size_t i=0;i<points.size();++i){const auto sample=samples[i].To<Map>();RealStation station{static_cast<std::size_t>(std::stoull(sample.at("segmentIndex").To<Text>())),sample.at("nativeParameter").To<double>()};
                const auto p=path.Curves[station.Piece]->Value(station.Parameter);OrientationWaypoint waypoint;waypoint.Position=Transform(placement,Point(points[i]));waypoint.Original.Direction=Unit(Transform(placement,Point(directions[i]),true));waypoint.MaximumDeviationRadians=30*Pi/180;
                ASSERT_LT(Length(Sub(waypoint.Position,{p.X(),p.Y(),p.Z()})),1e-5);path.Points.push_back(waypoint);path.Stations.push_back(station);}
            ASSERT_GT(path.Points.size(),3u);paths.push_back(std::move(path));}
    }
    ASSERT_FALSE(paths.empty());std::clog<<"Orientation input: "<<operationCount<<" operations, "<<paths.size()<<" cut contours, "<<markCount<<" marks; load="<<loadMs<<" read="<<readMs<<" ms"<<std::endl;
    const auto ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();const auto brep=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(brep);
    if(const auto limit=Env(L"ICAX_ORIENTATION_MAX_CUTS");!limit.empty())paths.resize(std::min(paths.size(),static_cast<std::size_t>(std::stoull(limit))));
    const auto rebuildStart=Clock::now();const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*brep);ASSERT_TRUE(built.bOK);RealCollision geometry;geometry.Service=scene.Services().Resolve<ICollisionDistanceService>();geometry.Shape=built.Shape.Moved(TopLoc_Location(trsf));BRepBndLib::AddOptimal(geometry.Shape,geometry.Bounds,false,true);const auto rebuildMs=Ms(rebuildStart);
    const auto settings=setup.at("rapidSettings").To<Map>();geometry.Follow=settings.at("followHeightMm").To<double>();geometry.Clearance=settings.at("collisionThresholdMm").To<double>()+settings.at("marginMm").To<double>();
    const auto meshStart=Clock::now();IMeshTools_Parameters parameters;parameters.Deflection=parameters.DeflectionInterior=.05;parameters.Angle=parameters.AngleInterior=.35;parameters.Relative=false;parameters.InParallel=true;parameters.MinSize=.005;parameters.ControlSurfaceDeflection=true;parameters.EnableControlSurfaceDeflectionAllSurfaces=true;
    BRepMesh_IncrementalMesh mesh(geometry.Shape,parameters);ASSERT_TRUE(mesh.IsDone());ASSERT_EQ(0,mesh.GetStatusFlags());const auto meshMs=Ms(meshStart);
    const auto coalStart=Clock::now();std::vector<CollisionDistanceModelRef> models;std::size_t triangles=0;double maximumPadding=0;
    for(TopExp_Explorer e(geometry.Shape,TopAbs_FACE);e.More();e.Next()){const auto face=TopoDS::Face(e.Current());TopLoc_Location location;const auto mesh=BRep_Tool::Triangulation(face,location);ASSERT_FALSE(mesh.IsNull());
        double tolerance=BRep_Tool::Tolerance(face)+1e-6;for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(edge.Current()))+1e-6);
        for(TopExp_Explorer vertex(face,TopAbs_VERTEX);vertex.More();vertex.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(vertex.Current()))+1e-6);
        CollisionMesh part;part.Padding=std::max(.05,mesh->Deflection())+tolerance;maximumPadding=std::max(maximumPadding,part.Padding);
        for(int i=1;i<=mesh->NbNodes();++i){const auto p=mesh->Node(i).Transformed(location.Transformation());part.Vertices.push_back({p.X(),p.Y(),p.Z()});}
        for(int i=1;i<=mesh->NbTriangles();++i){int a,b,c;mesh->Triangle(i).Get(a,b,c);part.Triangles.push_back({static_cast<unsigned>(a-1),static_cast<unsigned>(b-1),static_cast<unsigned>(c-1)});}triangles+=part.Triangles.size();models.push_back(geometry.Service->PrepareMesh(part));
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,true);geometry.Faces.emplace_back(face,box);}
    geometry.Fixed=geometry.Service->Combine(models);geometry.Head=geometry.Service->PrepareFrusta({{0,16,0,7},{16,16,7,11},{32,24,12,12}});
    BRep_Builder builder;TopoDS_Compound head;builder.MakeCompound(head);geometry.ExactParts={BRepPrimAPI_MakeCone(0,7,16).Shape(),BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,16),gp::DZ()),7,11,16).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,32),gp::DZ()),12,24).Shape()};for(const auto& part:geometry.ExactParts)builder.Add(head,part);geometry.ExactHead=head;
    for(TopExp_Explorer solid(geometry.Shape,TopAbs_SOLID);solid.More();solid.Next())geometry.Solids.push_back(std::make_unique<BRepClass3d_SolidClassifier>(solid.Current()));const auto coalMs=Ms(coalStart);
    std::clog<<"Orientation prepared "<<triangles<<" triangles: CAD="<<rebuildMs<<" mesh="<<meshMs<<" Coal="<<coalMs<<" ms"<<std::endl;
    const auto service=scene.Services().Resolve<IPathOrientationPlanningService>();Array baseline;std::size_t passed=0;const auto solveStart=Clock::now();
    for(const auto& path:paths){PathOrientationQuery q;q.Waypoints=path.Points;q.MaximumSeconds=30;geometry.Bind(q,path);geometry.Deadline=Clock::now()+std::chrono::seconds(30);const auto result=service->Plan(q);
        auto row=ResultRecord(result);row["intentId"]=path.ID;row["cutNumber"]=path.Number;row["stations"]=std::to_string(path.Points.size());const auto auditStart=Clock::now();const bool audited=Audit(geometry,q,result);row["audited"]=audited;row["auditMs"]=Ms(auditStart);if(audited)++passed;baseline.emplace_back(row);
        std::clog<<"Orientation baseline #"<<path.Number<<" "<<result.Status<<" "<<result.ElapsedMs<<" ms audit="<<audited<<" states="<<result.StateChecks<<" edges="<<result.MotionChecks<<" CoalMs="<<geometry.CoalMs<<" exactMs="<<geometry.ExactMs<<" classifyMs="<<geometry.ClassifyMs<<" boundMs="<<geometry.BoundMs<<" intervals="<<geometry.Intervals<<std::endl;}
    if(!Env(L"ICAX_ORIENTATION_BASELINE_ONLY").empty()){Map diagnostic{{"diagnosticOnly",true},{"baseline",baseline},{"coalMs",geometry.CoalMs},{"exactMs",geometry.ExactMs},{"classifyMs",geometry.ClassifyMs},{"coalCalls",std::to_string(geometry.CoalCalls)},{"exactCalls",std::to_string(geometry.ExactCalls)}};std::ofstream output(std::filesystem::path(reportName),std::ios::binary);output<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(diagnostic);EXPECT_EQ(paths.size(),passed);return;}
    const auto baselineMs=Ms(solveStart);const auto longest=std::max_element(paths.begin(),paths.end(),[](const auto& a,const auto& b){return a.Points.size()<b.Points.size();});const auto& path=*longest;
    const auto center=path.Points.size()/2;const auto anchor=path.Points[center].Position,normal=path.Points[center].Original.Direction;
    std::size_t first=center,last=center;while(first>0&&Length(Sub(path.Points[first].Position,anchor))<35)--first;while(last+1<path.Points.size()&&Length(Sub(path.Points[last].Position,anchor))<35)++last;
    ASSERT_GT(last-first,6u);PathOrientationQuery q;q.Waypoints=path.Points;q.EditableRanges={{first,last}};q.MaximumDirectionStepRadians=15*Pi/180;q.MaximumSeconds=60;q.MaximumSearchIterations=1024;
    for(std::size_t i=first+1;i<last;++i)q.Waypoints[i].Candidates=Cone(q.Waypoints[i].Original.Direction);q.Waypoints[first+1].Locked=true;
    const auto fixturePosition=Add(anchor,Scale(normal,45));geometry.Fixture=geometry.Service->PrepareBoxes({CollisionBox{fixturePosition,{1,1,1}}});geometry.Cache.clear();geometry.Bind(q,path);geometry.Deadline=Clock::now()+std::chrono::seconds(60);
    const auto originalBlocked=!geometry.State(center,q.Waypoints[center].Original);const auto exactBefore=geometry.ExactCalls,coalBefore=geometry.CoalCalls;const auto result=service->Plan(q);
    auto stress=ResultRecord(result);stress["fixtureCenter"]=VectorValues(fixturePosition);stress["fixtureHalfExtents"]=Array{1.,1.,1.};stress["virtualFixture"]=true;stress["intentId"]=path.ID;stress["originalBlocked"]=originalBlocked;stress["rangeFirst"]=std::to_string(first);stress["rangeLast"]=std::to_string(last);stress["lockedPoint"]=std::to_string(first+1);stress["coalCalls"]=std::to_string(geometry.CoalCalls-coalBefore);stress["exactFallbackCalls"]=std::to_string(geometry.ExactCalls-exactBefore);
    const auto auditStart=Clock::now();const bool audited=Audit(geometry,q,result);stress["audited"]=audited;stress["auditMs"]=Ms(auditStart);stress["certifiedClearanceMm"]=std::isfinite(geometry.MinimumCertified)?geometry.MinimumCertified:0.;Array poses;std::size_t changed=0;
    for(std::size_t i=0;i<result.Orientations.size();++i){const auto& pose=result.Orientations[i];if(pose.Direction!=q.Waypoints[i].Original.Direction)++changed;poses.emplace_back(Map{{"station",std::to_string(i)},{"position",VectorValues(q.Waypoints[i].Position)},{"original",VectorValues(q.Waypoints[i].Original.Direction)},{"direction",VectorValues(pose.Direction)}});}stress["changedStations"]=std::to_string(changed);stress["poses"]=poses;
    q.Waypoints[center].Locked=true;geometry.Deadline=Clock::now()+std::chrono::seconds(60);const auto impossible=service->Plan(q);auto lockedFailure=ResultRecord(impossible);lockedFailure["noPartialResult"]=impossible.Orientations.empty();
    bool bytesUnchanged=true;for(const auto& p:paths)bytesUnchanged&=p.OriginalBytes==p.Resource->Content;const auto after=Read(scene);bool generatedUnchanged=false;for(const auto& v:after.at("setups").To<Array>())if(v.To<Map>().at("id")==after.at("activeSetupId"))generatedUnchanged=v.To<Map>().at("generatedGeometry")==originalGenerated;
    const bool unchanged=bytesUnchanged&&after.at("intentToolpaths")==originalCuts&&scene.Database().GetUndoArray().size()==history&&generatedUnchanged;
    const bool functional=passed==paths.size()&&originalBlocked&&audited&&changed>0&&impossible.Status=="NoSolution"&&impossible.Orientations.empty()&&unchanged;
    Map report{{"schema",Text("icax.descartes-coal-orientation-evaluation.v1")},{"functionalPassed",functional},{"productionEnabled",false},{"scope",Text("Post-path orientation only; original exact curves fixed; head envelope and CAD; no installed-machine qualification")},
        {"operationCount",std::to_string(operationCount)},{"cutContourCount",std::to_string(paths.size())},{"markOperationCount",std::to_string(markCount)},{"baselinePassed",std::to_string(passed)},{"baseline",baseline},{"baselineWithAuditsMs",baselineMs},{"virtualFixtureStress",stress},{"lockedCollisionFailure",lockedFailure},
        {"originalPathAndProjectUnchanged",unchanged},{"sourceCurveBytesUnchanged",bytesUnchanged},{"generatedOperationsUnchanged",generatedUnchanged},{"cadFaces",std::to_string(brep->Faces.size())},{"collisionTriangles",std::to_string(triangles)},{"maximumFacePaddingMm",maximumPadding},
        {"coldCosts",Map{{"projectOpenMs",loadMs},{"firstReadStateMs",readMs},{"cadRebuildMs",rebuildMs},{"meshingMs",meshMs},{"coalPrepareMs",coalMs}}},{"coalCalls",std::to_string(geometry.CoalCalls)},{"exactFallbackCalls",std::to_string(geometry.ExactCalls)},{"exactFacesTested",std::to_string(geometry.ExactFaces)},{"totalMs",Ms(began)}};
    std::ofstream output(std::filesystem::path(reportName),std::ios::binary);output<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);output.close();
    EXPECT_EQ(paths.size(),passed);EXPECT_TRUE(originalBlocked);EXPECT_TRUE(audited);EXPECT_GT(changed,0u);EXPECT_EQ("NoSolution",impossible.Status);EXPECT_TRUE(impossible.Orientations.empty());EXPECT_TRUE(unchanged);
}
