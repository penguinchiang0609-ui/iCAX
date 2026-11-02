namespace ompl_evaluation {
using namespace iCAX::GeometryServices;
using V=std::array<double,3>;using Clock=std::chrono::steady_clock;
constexpr double Pi=3.14159265358979323846;
double Ms(Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}
std::wstring Env(const wchar_t* name){const auto count=GetEnvironmentVariableW(name,nullptr,0);if(!count)return {};std::wstring value(count,L'\0');value.resize(GetEnvironmentVariableW(name,value.data(),count));return value;}
V Add(V a,V b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
V Sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
V Scale(V a,double s){for(auto& v:a)v*=s;return a;}
double Length(V a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
V Unit(V a){return Scale(a,1/Length(a));}
V Point(const Variant& v){const auto a=v.To<Array>();return {a[0].To<double>(),a[1].To<double>(),a[2].To<double>()};}
V Transform(const Array& m,V p,bool direction=false){V out{};for(int i=0;i<3;++i){out[i]=direction?0:m[i*4+3].To<double>();for(int j=0;j<3;++j)out[i]+=m[i*4+j].To<double>()*p[j];}return out;}
PathConfiguration Config(V p,V n){n=Unit(n);auto yaw=std::atan2(n[1],n[0]);if(yaw>=Pi)yaw-=2*Pi;return {p[0],p[1],p[2],yaw,std::acos(std::clamp(n[2],-1.,1.))};}
V Position(const PathConfiguration& p){return {p[0],p[1],p[2]};}
V Direction(const PathConfiguration& p){return p.size()<5?V{0,0,1}:V{std::sin(p[4])*std::cos(p[3]),std::sin(p[4])*std::sin(p[3]),std::cos(p[4])};}
PathConfiguration Mid(const PathConfiguration& a,const PathConfiguration& b){auto c=a;for(std::size_t i=0;i<c.size();++i)c[i]+=((i==3)?std::remainder(b[i]-a[i],2*Pi):b[i]-a[i])*.5;return c;}
struct Validator {
    std::shared_ptr<ICollisionDistanceService> Service;
    CollisionDistanceModelRef Fixed,Head;double Clearance=.3,Radius=1;
    std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> Solids;Bnd_Box Bounds;
    std::vector<V> Probes;
    std::map<PathConfiguration,double> Cache;
    Clock::time_point Deadline=Clock::now()+std::chrono::hours(1);
    std::size_t Queries=0,Pairs=0,Intervals=0,ClassifyCount=0;
    std::string LastCause;
    double Certified=std::numeric_limits<double>::infinity();
    double Distance(const PathConfiguration& p){
        if(Clock::now()>=Deadline)return 0;
        if(const auto it=Cache.find(p);it!=Cache.end())return it->second;
        ++Queries;const auto n=Direction(p);gp_Trsf pose;pose.SetRotation(gp_Quaternion(gp_Vec(0,0,1),gp_Vec(n[0],n[1],n[2])));pose.SetTranslationPart(gp_Vec(p[0],p[1],p[2]));
        // A complete head fits inside this sphere. A disjoint sphere/obstacle
        // AABB gives an inexpensive conservative lower bound in the corridor.
        if(!Bounds.IsVoid()){double x0,y0,z0,x1,y1,z1;Bounds.Get(x0,y0,z0,x1,y1,z1);double squared=0;const V lo{x0,y0,z0},hi{x1,y1,z1};
            for(int i=0;i<3;++i){const auto gap=std::max({0.,lo[i]-p[i],p[i]-hi[i]});squared+=gap*gap;}const auto lower=std::sqrt(squared)-Radius;
            if(lower>Clearance+1e-6){LastCause="SphereAABBSeparation";Cache[p]=lower;return lower;}}
        CollisionTransform transform{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};for(int i=0;i<3;++i)for(int j=0;j<4;++j)transform[i*4+j]=pose.Value(i+1,j+1);
        const auto r=Service->LowerBound(Fixed,Head,transform,Clearance+1e-6,[&]{return Clock::now()>=Deadline;});Pairs+=r.NarrowPhaseQueries;
        // Boundary overlap already rejects this state. Exact containment is
        // needed only after separation, to catch a head wholly inside a solid.
        if(r.LowerBound<=Clearance+1e-6){LastCause="CoalBoundsUnresolved";Cache[p]=r.LowerBound;return r.LowerBound;}
        for(const auto& probe:Probes){const auto world=gp_Pnt(probe[0],probe[1],probe[2]).Transformed(pose);if(Bounds.IsVoid()||Bounds.IsOut(world))continue;
            for(const auto& classifier:Solids){++ClassifyCount;classifier->Perform(world,1e-7);if(classifier->State()==TopAbs_IN||classifier->State()==TopAbs_ON){LastCause="InsideCADSolid";Cache[p]=0;return 0;}}}
        LastCause="CoalSeparation";Cache[p]=r.LowerBound;return r.LowerBound;
    }
    bool State(const PathConfiguration& p){return Distance(p)>Clearance+1e-6;}
    bool Interval(const PathConfiguration& a,const PathConfiguration& b,int depth){
        ++Intervals;if(Clock::now()>=Deadline)return false;const auto mid=Mid(a,b);const auto distance=Distance(mid);if(distance<=Clearance+1e-6)return false;
        double rotation=0;if(a.size()>=5)rotation=std::abs(std::remainder(b[3]-a[3],2*Pi))+std::abs(b[4]-a[4]);
        // Translation is linear. The sum of yaw/tilt changes bounds EVERY
        // envelope point's rotation along this 5-DOF segment, not just samples.
        const auto movement=Length(Sub(Position(a),Position(b)))*.5+2*Radius*std::sin(std::min(Pi,rotation*.5)*.5);
        const auto lower=distance-movement-1e-6;if(lower>=Clearance){Certified=std::min(Certified,lower);return true;}
        return depth<18&&Interval(a,mid,depth+1)&&Interval(mid,b,depth+1);
    }
    bool Motion(const PathConfiguration& a,const PathConfiguration& b){return State(b)&&Interval(a,b,0);}
};
ConfigurationPathQuery Query(Validator& v,const PathConfiguration& a,const PathConfiguration& b,V low,V high,double seconds=.25){
    ConfigurationPathQuery q;q.Start=a;q.Goal=b;q.MaximumSeconds=seconds;for(int i=0;i<3;++i)q.Axes.push_back({low[i],high[i],1,false});
    if(a.size()==5){q.Axes.push_back({-Pi,Pi,v.Radius,true});q.Axes.push_back({0,Pi,v.Radius,false});}
    q.CheckState=[&](const auto& p){return v.State(p);};q.CheckMotion=[&](const auto& x,const auto& y){return v.Motion(x,y);};return q;
}
Array Values(const PathConfiguration& p){Array a;for(auto v:p)a.emplace_back(v);return a;}
Array PathValues(const std::vector<PathConfiguration>& path){Array a;for(const auto& p:path)a.emplace_back(Values(p));return a;}
Validator Wall(Scene& scene){Validator v;v.Service=scene.Services().Resolve<ICollisionDistanceService>();
    v.Fixed=v.Service->PrepareBoxes({CollisionBox{{.781,0,0},{.02,2,2}}});v.Head=v.Service->PrepareFrusta({{0,.4,.2,.2}});v.Radius=.45;return v;}
}
TEST(MotionPlanning,CoalContinuousValidatorFindsDetourAndPreservesEndpoints)
{
    using namespace ompl_evaluation;Scene scene;auto v=Wall(scene);auto q=Query(v,{-3,0,0},{3,0,0},{-5,-4,-4},{5,4,4},1);
    ASSERT_FALSE(v.Motion(q.Start,q.Goal));const auto r=scene.Services().Resolve<IConfigurationPathPlanningService>()->Plan(q);
    ASSERT_EQ("Solved",r.Status);EXPECT_EQ("RRTConnect",r.Method);ASSERT_GT(r.Path.size(),2u);EXPECT_EQ(q.Start,r.Path.front());EXPECT_EQ(q.Goal,r.Path.back());
    v.Cache.clear();for(std::size_t i=1;i<r.Path.size();++i)EXPECT_TRUE(v.Motion(r.Path[i-1],r.Path[i]));EXPECT_GE(v.Certified,v.Clearance);
}
TEST(MotionPlanning,LimitsTimeoutAndCancellationDoNotProduceSuccessfulPaths)
{
    using namespace ompl_evaluation;Scene scene;auto v=Wall(scene);auto q=Query(v,{-3,0,0},{3,0,0},{-5,-.1,-.1},{5,.1,.1},.02);
    const auto service=scene.Services().Resolve<IConfigurationPathPlanningService>();auto r=service->Plan(q);EXPECT_EQ("Timeout",r.Status);EXPECT_TRUE(r.Path.empty());
    r=service->Plan(q,[]{return true;});EXPECT_EQ("Cancelled",r.Status);EXPECT_TRUE(r.Path.empty());
    q.Axes[0].Minimum=-2;r=service->Plan(q);EXPECT_EQ("InvalidStart",r.Status);EXPECT_TRUE(r.Path.empty());
    q.MaximumSeconds=0;EXPECT_THROW(service->Plan(q),std::invalid_argument);
}
TEST(MotionPlanning,PeriodicAxesUseShortConnectionAcrossWrap)
{
    using namespace ompl_evaluation;Scene scene;ConfigurationPathQuery q;q.Axes={{-Pi,Pi,1,true}};q.Start={Pi-.1};q.Goal={-Pi+.1};
    q.CheckState=[](const auto&){return true;};q.CheckMotion=[](const auto& a,const auto& b){return std::abs(std::remainder(b[0]-a[0],2*Pi))<.21;};
    const auto r=scene.Services().Resolve<IConfigurationPathPlanningService>()->Plan(q);EXPECT_EQ("Solved",r.Status);EXPECT_EQ("Direct",r.Method);
}
TEST(MotionPlanningEvaluation,DISABLED_RealEggConnections)
{
    using namespace ompl_evaluation;
    const auto projectName=Env(L"ICAX_OMPL_PROJECT"),reportName=Env(L"ICAX_OMPL_REPORT");ASSERT_FALSE(projectName.empty());ASSERT_FALSE(reportName.empty());
    const auto began=Clock::now();iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(projectName);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto loadMs=Ms(began);const auto readStart=Clock::now();const auto state=Read(scene);Map generated,activeSetup;
    for(const auto& value:state.at("setups").To<Array>())if(value.To<Map>().at("id")==state.at("activeSetupId")){activeSetup=value.To<Map>();generated=activeSetup.at("generatedGeometry").To<Map>();}
    const auto sourceCuts=state.at("intentToolpaths");const auto readMs=Ms(readStart);
    ASSERT_EQ("Builtin",activeSetup.at("headModel").To<Map>().at("kind").To<Text>());
    ASSERT_TRUE(state.at("fixtures").To<Array>().empty());const auto settings=activeSetup.at("rapidSettings").To<Map>();
    const auto followHeight=settings.at("followHeightMm").To<double>(),lift=settings.at("liftHeightMm").To<double>();
    const auto& ops=std::get<Array>(generated.at("operations").m_Value);ASSERT_EQ(480u,ops.size());
    const auto& placements=std::get<Array>(state.at("placements").m_Value);ASSERT_EQ(1u,placements.size());const auto placement=placements.front().To<Map>();const auto transform=placement.at("transform").To<Array>();
    const auto ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();
    const auto brep=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(brep);ASSERT_EQ(3996u,brep->Faces.size());
    const auto buildStart=Clock::now();const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*brep);ASSERT_TRUE(built.bOK);gp_Trsf placementTransform;
    placementTransform.SetValues(transform[0].To<double>(),transform[1].To<double>(),transform[2].To<double>(),transform[3].To<double>(),transform[4].To<double>(),transform[5].To<double>(),transform[6].To<double>(),transform[7].To<double>(),transform[8].To<double>(),transform[9].To<double>(),transform[10].To<double>(),transform[11].To<double>());
    const auto shape=built.Shape.Moved(TopLoc_Location(placementTransform));const auto rebuildMs=Ms(buildStart);std::clog<<"Evaluation rebuilt CAD "<<rebuildMs<<" ms"<<std::endl;
    const auto meshStart=Clock::now();IMeshTools_Parameters parameters;parameters.Deflection=parameters.DeflectionInterior=.05;parameters.Angle=parameters.AngleInterior=.35;
    parameters.Relative=false;parameters.InParallel=true;parameters.MinSize=.005;parameters.ControlSurfaceDeflection=true;parameters.EnableControlSurfaceDeflectionAllSurfaces=true;
    BRepMesh_IncrementalMesh mesher(shape,parameters);ASSERT_TRUE(mesher.IsDone());ASSERT_EQ(0,mesher.GetStatusFlags());const auto meshMs=Ms(meshStart);std::clog<<"Evaluation meshed CAD "<<meshMs<<" ms"<<std::endl;
    Validator v;v.Service=scene.Services().Resolve<ICollisionDistanceService>();v.Clearance=settings.at("collisionThresholdMm").To<double>()+settings.at("marginMm").To<double>();
    CollisionMesh mesh;mesh.Padding=.05;std::size_t triangleCount=0;double maximumTolerance=0,maximumDeflection=0;std::vector<CollisionDistanceModelRef> faceModels;const auto coalStart=Clock::now();
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()){
        const auto face=TopoDS::Face(e.Current());double tolerance=BRep_Tool::Tolerance(face)+1e-6;TopLoc_Location location;const auto triangles=BRep_Tool::Triangulation(face,location);ASSERT_FALSE(triangles.IsNull());
        for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(edge.Current()))+1e-6);
        for(TopExp_Explorer vertex(face,TopAbs_VERTEX);vertex.More();vertex.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(vertex.Current()))+1e-6);
        CollisionMesh part;part.Padding=std::max(.05,triangles->Deflection())+tolerance;maximumTolerance=std::max(maximumTolerance,tolerance);maximumDeflection=std::max(maximumDeflection,triangles->Deflection());mesh.Padding=std::max(mesh.Padding,part.Padding);
        part.Vertices.reserve(triangles->NbNodes());part.Triangles.reserve(triangles->NbTriangles());
        for(int i=1;i<=triangles->NbNodes();++i){const auto p=triangles->Node(i).Transformed(location.Transformation());part.Vertices.push_back({p.X(),p.Y(),p.Z()});}
        for(int i=1;i<=triangles->NbTriangles();++i){int a,b,c;triangles->Triangle(i).Get(a,b,c);part.Triangles.push_back({static_cast<unsigned>(a-1),static_cast<unsigned>(b-1),static_cast<unsigned>(c-1)});}
        triangleCount+=part.Triangles.size();faceModels.push_back(v.Service->PrepareMesh(part));
    }
    v.Fixed=v.Service->Combine(faceModels);v.Head=v.Service->PrepareFrusta({{0,16,0,7},{16,16,7,11},{32,24,12,12}});
    const auto coalMs=Ms(coalStart);v.Radius=std::hypot(56.,12.);v.Probes={{0,0,8},{0,0,24},{0,0,44}};BRepBndLib::Add(shape,v.Bounds,false);
    for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())v.Solids.push_back(std::make_unique<BRepClass3d_SolidClassifier>(e.Current()));
    std::clog<<"Evaluation Coal BVH "<<triangleCount<<" triangles "<<coalMs<<" ms; maximum face padding="<<mesh.Padding<<" tolerance="<<maximumTolerance<<" deflection="<<maximumDeflection<<std::endl;
    V low,high;v.Bounds.Get(low[0],low[1],low[2],high[0],high[1],high[2]);const auto originalLow=low,originalHigh=high;const auto pad=v.Radius+v.Clearance+1.;
    low=Sub(low,{pad,pad,pad});high=Add(high,{pad,pad,pad});
    const auto exit=[&](V p,V n){bool outside=false;for(int j=0;j<3;++j)outside|=p[j]<low[j]||p[j]>high[j];double distance=outside?0:std::numeric_limits<double>::infinity();
        if(!outside)for(int j=0;j<3;++j)if(std::abs(n[j])>1e-10)distance=std::min(distance,((n[j]>0?high[j]:low[j])-p[j])/n[j]);return Add(p,Scale(n,std::max(lift,distance+1e-4)));};
    std::vector<std::size_t> indices;const auto selected=Env(L"ICAX_OMPL_INDICES");if(!selected.empty()){std::wistringstream list(selected);for(std::wstring s;std::getline(list,s,L',');)indices.push_back(std::stoull(s));}else for(std::size_t i=0;i+1<ops.size();++i)indices.push_back(i);
    Array records;std::vector<double> durations;std::map<Text,std::size_t> statuses;std::size_t audited=0,centralAudited=0,searches=0,baselineClear=0;std::vector<std::pair<std::size_t,PathConfiguration>> diagnostics;const auto planningStart=Clock::now();
    const auto service=scene.Services().Resolve<IConfigurationPathPlanningService>();
    for(const auto index:indices){ASSERT_LT(index+1,ops.size());const auto from=ops[index].To<Map>(),to=ops[index+1].To<Map>();const auto last=from.at("parts").To<Array>().back().To<Map>(),first=to.at("parts").To<Array>().front().To<Map>();
        const auto na=Unit(Transform(transform,Point(last.at("directions").To<Array>().back()),true)),nb=Unit(Transform(transform,Point(first.at("directions").To<Array>().front()),true));
        const auto a=Add(Transform(transform,Point(from.at("end"))),Scale(na,followHeight)),b=Add(Transform(transform,Point(to.at("start"))),Scale(nb,followHeight));
        const auto start=Config(a,na),goal=Config(b,nb),withdrawn=Config(exit(a,na),na),approach=Config(exit(b,nb),nb);
        v.Cache.clear();v.Deadline=Clock::now()+std::chrono::seconds(2);v.Certified=std::numeric_limits<double>::infinity();const auto queryCount=v.Queries,pairCount=v.Pairs;
        const auto caseStart=Clock::now();const bool startClear=v.State(start);const auto startCause=v.LastCause;const bool goalClear=v.State(goal);const auto goalCause=v.LastCause;
        if(index==0||index==256||index==478)diagnostics.emplace_back(index,start);
        const bool fixedLegsClear=startClear&&goalClear&&v.Motion(start,withdrawn)&&v.Motion(approach,goal);
        const auto fixedA=Config(Add(a,Scale(na,lift)),na),fixedB=Config(Add(b,Scale(nb,lift)),nb);
        baselineClear+=startClear&&goalClear&&v.Motion(start,fixedA)&&v.Motion(fixedA,fixedB)&&v.Motion(fixedB,goal);
        // Evaluate path SEARCH even when near-surface endpoints are unresolved.
        // Report central-stage success separately from full-path qualification.
        const V extra{pad,pad,pad};auto q=Query(v,withdrawn,approach,Sub(low,extra),Add(high,extra),.25);v.Deadline=Clock::now()+std::chrono::seconds(2);
        auto result=service->Plan(q);searches+=result.Method=="RRTConnect";std::vector<PathConfiguration> path;
        if(result.Status=="Solved"){
            v.Cache.clear();v.Deadline=Clock::now()+std::chrono::seconds(2);bool valid=true;
            for(std::size_t j=1;j<result.Path.size();++j)valid&=v.Motion(result.Path[j-1],result.Path[j]);
            if(valid){++centralAudited;path.push_back(start);path.insert(path.end(),result.Path.begin(),result.Path.end());path.push_back(goal);if(fixedLegsClear)++audited;}
            else result.Status="AuditFailed";
        }
        double length=0;for(std::size_t j=1;j<path.size();++j)length+=Length(Sub(Position(path[j]),Position(path[j-1])));
        const auto elapsed=Ms(caseStart);durations.push_back(elapsed);++statuses[result.Status];records.emplace_back(Map{{"index",static_cast<double>(index)},{"status",result.Status},{"fullPathQualified",fixedLegsClear&&result.Status=="Solved"},{"startClear",startClear},{"goalClear",goalClear},{"startCause",startCause},{"goalCause",goalCause},{"start",Values(start)},{"goal",Values(goal)},{"method",result.Method},{"elapsedMs",elapsed},{"searchMs",result.ElapsedMs},{"lengthMm",length},{"states",static_cast<double>(path.size())},{"coalQueries",static_cast<double>(v.Queries-queryCount)},{"coalPairs",static_cast<double>(v.Pairs-pairCount)},{"certifiedClearanceMm",std::isfinite(v.Certified)?v.Certified:0},{"path",PathValues(path)}});
        if(index%25==0||result.Status!="Solved")std::clog<<"Evaluation connection "<<index<<" "<<result.Status<<" "<<elapsed<<" ms; search="<<result.ElapsedMs<<std::endl;
    }
    const auto planningMs=Ms(planningStart);std::sort(durations.begin(),durations.end());const auto percentile=[&](double q){return durations.at(std::min(durations.size()-1,static_cast<std::size_t>(std::ceil(q*durations.size())-1)));};
    Map counts;for(const auto& [key,count]:statuses)counts[key]=static_cast<double>(count);const bool unchanged=Read(scene).at("intentToolpaths")==sourceCuts;
    Array exactDiagnostics;TopoDS_Compound exactHead;BRep_Builder headBuilder;headBuilder.MakeCompound(exactHead);headBuilder.Add(exactHead,BRepPrimAPI_MakeCone(0,7,16).Shape());headBuilder.Add(exactHead,BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,16),gp::DZ()),7,11,16).Shape());headBuilder.Add(exactHead,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,32),gp::DZ()),12,24).Shape());
    for(const auto& [index,p]:diagnostics){const auto n=Direction(p);gp_Trsf pose;pose.SetRotation(gp_Quaternion(gp_Vec(0,0,1),gp_Vec(n[0],n[1],n[2])));pose.SetTranslationPart(gp_Vec(p[0],p[1],p[2]));
        const auto stamp=Clock::now();BRepExtrema_DistShapeShape exact;exact.SetMultiThread(true);exact.LoadS1(exactHead.Moved(TopLoc_Location(pose)));exact.LoadS2(shape);exact.Perform();
        exactDiagnostics.emplace_back(Map{{"index",static_cast<double>(index)},{"done",exact.IsDone()},{"boundaryDistanceMm",exact.IsDone()?exact.Value():0},{"elapsedMs",Ms(stamp)}});std::clog<<"Evaluation exact endpoint "<<index<<" distance="<<(exact.IsDone()?exact.Value():0)<<std::endl;}
    // Controlled obstruction on the REAL CAD model. Adjacent natural paths
    // above all happen to connect directly in the exterior corridor, so they
    // do not exercise RRTConnect. Keep these fixture stress cases separate.
    const auto originalFixed=v.Fixed;V center=Scale(Add(originalLow,originalHigh),.5);V clampHalf{10,30,30};
    v.Fixed=v.Service->Combine({originalFixed,v.Service->PrepareBoxes({CollisionBox{center,clampHalf}})});
    v.Bounds.Add(gp_Pnt(center[0]-10,center[1]-30,center[2]-30));v.Bounds.Add(gp_Pnt(center[0]+10,center[1]+30,center[2]+30));
    V stressLow=Sub(originalLow,{pad*3,pad*3,pad*3}),stressHigh=Add(originalHigh,{pad*3,pad*3,pad*3});
    const auto stressStart=Config({originalLow[0]-pad*2,center[1],center[2]},{0,0,1}),stressGoal=Config({originalHigh[0]+pad*2,center[1],center[2]},{0,0,1});
    Array stressRecords;std::size_t stressPassed=0;
    for(int repeat=0;repeat<3;++repeat){v.Cache.clear();v.Deadline=Clock::now()+std::chrono::seconds(3);auto q=Query(v,stressStart,stressGoal,stressLow,stressHigh,1.);
        const bool direct=v.Motion(q.Start,q.Goal);const auto stamp=Clock::now();const auto result=service->Plan(q);const auto searchMs=Ms(stamp);
        v.Cache.clear();v.Deadline=Clock::now()+std::chrono::seconds(3);bool audited=result.Status=="Solved";double length=0;
        for(std::size_t j=1;j<result.Path.size();++j){audited&=v.Motion(result.Path[j-1],result.Path[j]);length+=Length(Sub(Position(result.Path[j]),Position(result.Path[j-1])));}
        const bool passed=!direct&&result.Method=="RRTConnect"&&audited;if(passed)++stressPassed;
        stressRecords.emplace_back(Map{{"repeat",static_cast<double>(repeat)},{"directBlocked",!direct},{"status",result.Status},{"method",result.Method},{"audited",audited},{"elapsedMs",searchMs},{"states",static_cast<double>(result.Path.size())},{"lengthMm",length},{"path",PathValues(result.Path)}});
        std::clog<<"Evaluation real-model fixture stress "<<repeat<<" "<<result.Status<<" "<<searchMs<<" ms; audited="<<audited<<std::endl;
    }
    Map report{{"ompl",Text("2.0.2")},{"coal",Text("3.0.4")},{"operations",480.},{"totalConnections",479.},{"testedConnections",static_cast<double>(indices.size())},{"auditedSolved",static_cast<double>(audited)},{"centralAuditedSolved",static_cast<double>(centralAudited)},{"statuses",counts},{"rrtSearches",static_cast<double>(searches)},{"fixed10mmClear",static_cast<double>(baselineClear)},{"sourceCutsUnchanged",unchanged},{"exactEndpointDiagnostics",exactDiagnostics},
        {"loadProjectMs",loadMs},{"readStateMs",readMs},{"rebuildCadMs",rebuildMs},{"meshMs",meshMs},{"coalPreparationMs",coalMs},{"meshTriangles",static_cast<double>(triangleCount)},{"maximumFacePaddingMm",mesh.Padding},{"maximumCadToleranceMm",maximumTolerance},{"maximumReportedDeflectionMm",maximumDeflection},{"planningMs",planningMs},{"p50Ms",percentile(.5)},{"p95Ms",percentile(.95)},{"maximumMs",durations.back()},{"connections",records},
        {"scope",Text("Central connection search audited separately from withdrawal/approach. Actual STEP and builtin head; XYZ/yaw/tilt surrogate; no machine joint limits, IK or NC validation")},{"fixtureStress",stressRecords},{"fixtureStressPassed",stressPassed==3},{"planningStagePassed",centralAudited==indices.size()&&unchanged},{"functionalPassed",audited==indices.size()&&unchanged}};
    const std::filesystem::path destination(reportName);std::filesystem::create_directories(destination.parent_path());{std::ofstream output(destination);output<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);}
    EXPECT_TRUE(unchanged);EXPECT_EQ(3u,stressPassed);EXPECT_EQ(indices.size(),audited)<<"Full-path qualification includes unresolved withdrawal/approach, independently of central-stage success";
}
