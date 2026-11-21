namespace {
TopoDS_Shape JointPartShape(Scene& scene,const Map& part){const auto ref=part.at("geometryRef").To<Map>();const auto& version=ref.at("version");const UInt revision=version.Is<UInt>()?version.To<UInt>():static_cast<UInt>(version.To<long long>());const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),revision);auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);if(!built.bOK)throw std::runtime_error("Joint test BRep invalid");return built.Shape;}
gp_Pnt JointPoint(const Map& part,double u,double v){const auto f=part.at("frame").To<Array>();double t=part.at("thickness").To<double>()/2;return gp_Pnt(f[0].To<double>()*u+f[1].To<double>()*v+f[2].To<double>()*t+f[3].To<double>(),f[4].To<double>()*u+f[5].To<double>()*v+f[6].To<double>()*t+f[7].To<double>(),f[8].To<double>()*u+f[9].To<double>()*v+f[10].To<double>()*t+f[11].To<double>());}
TopAbs_State JointState(const TopoDS_Shape& shape,const gp_Pnt& point){BRepClass3d_SolidClassifier classify(shape);classify.Perform(point,1e-6);return classify.State();}
double JointArea(const Array& loop){double area=0;for(size_t i=0;i<loop.size();++i){auto a=loop[i].To<Array>(),b=loop[(i+1)%loop.size()].To<Array>();area+=a[0].To<double>()*b[1].To<double>()-a[1].To<double>()*b[0].To<double>();}return std::abs(area)/2;}
double JointExactArea(const Map& part){
    using namespace iCAX::GeometryData;const auto profile=iCAX::CAD::TubeCadJson::Decode<Tube::CPlanarRegion2>(part.at("exactProfile"));double area=0;
    bool spline=false;for(const auto& loop:profile.Boundaries)for(const auto& edge:loop.Curves)spline|=!std::holds_alternative<Segment2>(edge.Segment.Curve)&&!std::holds_alternative<Circle2>(edge.Segment.Curve)&&!std::holds_alternative<Arc2>(edge.Segment.Curve);
    if(spline){const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));for(size_t i=0;i<profile.Boundaries.size();++i){BRepBuilderAPI_MakeWire wire;for(const auto& edge:profile.Boundaries[i].Curves){const auto& s=edge.Segment;auto curve=iCAX::OpenCascade::BuildOpenCascadeCurve2(s.Curve);if(curve.IsNull())throw std::runtime_error("Fixture test curve invalid");double a=s.Range.First,b=s.Range.Last;if(const auto* line=std::get_if<Segment2>(&s.Curve)){const double length=std::hypot(line->End.X-line->Start.X,line->End.Y-line->Start.Y);a*=length;b*=length;}BRepBuilderAPI_MakeEdge made(GeomAPI::To3d(curve,plane),a,b);if(!made.IsDone())throw std::runtime_error("Fixture test edge invalid");auto value=made.Edge();if(s.Range.Reversed)value.Reverse();wire.Add(value);}if(!wire.IsDone())throw std::runtime_error("Fixture test wire invalid");BRepBuilderAPI_MakeFace face(plane,wire.Wire(),true);if(!face.IsDone())throw std::runtime_error("Fixture test face invalid");GProp_GProps mass;BRepGProp::SurfaceProperties(face.Face(),mass,1e-10);area+=(i?-1:1)*std::abs(mass.Mass());}return area;}
    for(size_t i=0;i<profile.Boundaries.size();++i){double integral=0;
        for(const auto& edge:profile.Boundaries[i].Curves){const auto& s=edge.Segment;const double a=s.Range.Reversed?s.Range.Last:s.Range.First,b=s.Range.Reversed?s.Range.First:s.Range.Last;
            if(const auto line=std::get_if<Segment2>(&s.Curve)){const double x0=line->Start.X+(line->End.X-line->Start.X)*a,y0=line->Start.Y+(line->End.Y-line->Start.Y)*a,x1=line->Start.X+(line->End.X-line->Start.X)*b,y1=line->Start.Y+(line->End.Y-line->Start.Y)*b;integral+=x0*y1-x1*y0;continue;}
            const auto* c=std::get_if<Circle2>(&s.Curve);if(const auto arc=std::get_if<Arc2>(&s.Curve))c=&arc->Basis;if(!c)throw std::runtime_error("Unexpected fixture profile curve");
            const auto& p=c->Placement;const auto point=[&](double t){return std::array<double,2>{p.Location.X+c->Radius*(p.XDirection.X*std::cos(t)+p.YDirection.X*std::sin(t)),p.Location.Y+c->Radius*(p.XDirection.Y*std::cos(t)+p.YDirection.Y*std::sin(t))};};
            const auto first=point(a),last=point(b);integral+=p.Location.X*(last[1]-first[1])-p.Location.Y*(last[0]-first[0])+c->Radius*c->Radius*(p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X)*(b-a);
        }area+=(i?-1:1)*std::abs(integral)/2;
    }return area;
}
Map JointRecipe(Scene& scene){auto recipe=PlateRecipe(scene);recipe["jointMode"]=Text("TabSlot");recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Grid");recipe["spacing"]=50.;recipe["sizing"]=Text("Manual");recipe["areaWidth"]=180.;recipe["areaDepth"]=180.;recipe["stations"]=Array();return recipe;}
}
TEST(FiveAxisFixtureJoinery, ActualHalfLapsBaseTenonsNoInterpenetrationAndInnerDXFReadback){
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);
    auto recipe=SetPlateRecipe(scene,JointRecipe(scene));const auto result=GeneratePlates(scene);const auto parts=result.at("parts").To<Array>();ASSERT_EQ(result.at("joint").To<Text>(),"TabSlot");ASSERT_EQ(parts.size(),7u);ASSERT_EQ(result.at("joinery").To<Map>().at("crossLaps").To<UInt>(),9u);ASSERT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");
    std::map<Text,size_t> byID;std::vector<TopoDS_Shape> shapes;std::map<Text,std::vector<std::pair<size_t,Map>>> pairs;
    for(size_t i=0;i<parts.size();++i){auto p=parts[i].To<Map>();byID[p.at("partId").To<Text>()]=i;shapes.push_back(JointPartShape(scene,p));ASSERT_TRUE(BRepCheck_Analyzer(shapes.back()).IsValid());
        double area;if(p.contains("exactProfile"))area=JointExactArea(p);else{area=JointArea(p.at("outline").To<Array>());for(auto h:p.at("holes").To<Array>())area-=JointArea(h.To<Array>());}GProp_GProps mass;BRepGProp::VolumeProperties(shapes.back(),mass);EXPECT_NEAR(std::abs(mass.Mass()),area*p.at("thickness").To<double>(),1e-4);
        for(auto j:p.at("joints").To<Array>()){auto joint=j.To<Map>();pairs[joint.at("id").To<Text>()].emplace_back(i,joint);}
    }
    ASSERT_FALSE(parts.back().To<Map>().at("holes").To<Array>().empty());
    for(const auto& [id,pair]:pairs){ASSERT_EQ(pair.size(),2u)<<id;auto [a,ja]=pair[0];auto [b,jb]=pair[1];EXPECT_EQ(byID.at(ja.at("matePartId").To<Text>()),b);EXPECT_EQ(byID.at(jb.at("matePartId").To<Text>()),a);
        if(ja.at("type").To<Text>()=="BaseMortise"){std::swap(a,b);std::swap(ja,jb);}
        if(ja.at("type").To<Text>()=="BaseTenon"){
            const auto boundary=ja.at("boundary").To<Array>();auto p0=boundary[0].To<Array>(),p2=boundary[2].To<Array>();auto point=JointPoint(parts[a].To<Map>(),(p0[0].To<double>()+p2[0].To<double>())/2,(p0[1].To<double>()+p2[1].To<double>())/2);
            EXPECT_EQ(JointState(shapes[a],point),TopAbs_IN);EXPECT_EQ(JointState(shapes[b],point),TopAbs_OUT);
            const auto hole=jb.at("boundary").To<Array>();double x0=1e100,x1=-1e100,y0=1e100,y1=-1e100;for(auto q:hole){auto p=q.To<Array>();x0=std::min(x0,p[0].To<double>());x1=std::max(x1,p[0].To<double>());y0=std::min(y0,p[1].To<double>());y1=std::max(y1,p[1].To<double>());}EXPECT_NEAR(std::min(x1-x0,y1-y0),3.2,1e-6);EXPECT_NEAR(std::max(x1-x0,y1-y0),8.2,1e-6);
        }else{
            if(ja.at("type").To<Text>()=="LapBottom"){std::swap(a,b);std::swap(ja,jb);}ASSERT_EQ(ja.at("type").To<Text>(),"LapTop");
            const auto boundary=ja.at("boundary").To<Array>();const double u=(boundary[0].To<Array>()[0].To<double>()+boundary[1].To<Array>()[0].To<double>())/2,seat=boundary[0].To<Array>()[1].To<double>()+.1;auto point=JointPoint(parts[a].To<Map>(),u,seat);
            EXPECT_EQ(JointState(shapes[a],point),TopAbs_OUT);EXPECT_EQ(JointState(shapes[b],point),TopAbs_OUT);auto above=point.Translated(gp_Vec(0,0,1)),below=point.Translated(gp_Vec(0,0,-1));EXPECT_EQ(JointState(shapes[a],above),TopAbs_OUT);EXPECT_EQ(JointState(shapes[b],above),TopAbs_IN);EXPECT_EQ(JointState(shapes[a],below),TopAbs_IN);EXPECT_EQ(JointState(shapes[b],below),TopAbs_OUT);
        }
    }
    for(size_t a=0;a<shapes.size();++a)for(size_t b=a+1;b<shapes.size();++b){BRepAlgoAPI_Common common(shapes[a],shapes[b]);ASSERT_TRUE(common.IsDone());GProp_GProps mass;BRepGProp::VolumeProperties(common.Shape(),mass);EXPECT_LT(std::abs(mass.Mass()),1e-5)<<a<<','<<b;}
    // Independently test the specified Y-first, X-down assembly direction at
    // intermediate heights, including the base-tenon entry region.
    for(size_t a=0;a+1<shapes.size();++a){const auto p=parts[a].To<Map>();if(p.at("frame").To<Array>()[2].To<double>()<.5)continue;for(double lift:{.5,10.,50.}){gp_Trsf move;move.SetTranslation(gp_Vec(0,0,lift));const auto lifted=shapes[a].Moved(TopLoc_Location(move));for(size_t b=0;b<shapes.size();++b){if(b==a||parts[b].To<Map>().at("frame").To<Array>()[2].To<double>()>.5)continue;BRepAlgoAPI_Common common(lifted,shapes[b]);ASSERT_TRUE(common.IsDone());GProp_GProps mass;BRepGProp::VolumeProperties(common.Shape(),mass);EXPECT_LT(std::abs(mass.Mass()),1e-5)<<"assembly insertion "<<a<<','<<b<<','<<lift;}}}
    AdoptPlates(scene,result);Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));
    const auto destination=std::filesystem::current_path()/"fixture-joinery-native-delivery";std::filesystem::create_directories(destination);auto request=FixtureRequest(scene);auto pathText=destination.u8string();request["destination"]=Text(reinterpret_cast<const char*>(pathText.data()),pathText.size());auto delivery=Invoke(&scene,"CamFixture","Export",request);EXPECT_FALSE(delivery.at("files").To<Array>().empty());
    const auto before=Read(scene);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="榫卯装配";auto path=part.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));EXPECT_EQ(before.at("fixtures"),Read(reopened).at("fixtures"));EXPECT_TRUE(BRepCheck_Analyzer(JointPartShape(reopened,parts.back().To<Map>())).IsValid());std::filesystem::remove(path);
    RecordProperty("lap_pairs",9);RecordProperty("base_mortises",static_cast<int>(parts.back().To<Map>().at("holes").To<Array>().size()));
}
TEST(FiveAxisFixtureJoinery, InvalidFitOrUnbuildableJointsCannotReplaceAssembly){
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);auto recipe=SetPlateRecipe(scene,JointRecipe(scene));auto good=GeneratePlates(scene);AdoptPlates(scene,good);const auto before=Read(scene).at("fixtures");
    for(auto [key,value]:std::array<std::pair<const char*,double>,4>{{{"jointClearance",-1},{"tenonWidth",1},{"tenonPitch",8},{"jointWeb",0}}}){auto bad=recipe;bad[key]=value;EXPECT_THROW(SetPlateRecipe(scene,bad),std::exception)<<key;EXPECT_EQ(Read(scene).at("fixtures"),before);}
    auto bad=recipe;bad["tenonWidth"]=200.;bad["tenonPitch"]=300.;SetPlateRecipe(scene,bad);
    EXPECT_THROW(GeneratePlates(scene),std::exception);
    EXPECT_EQ(Read(scene).at("fixtures"),before);
    EXPECT_THROW(AdoptPlates(scene,good),std::exception);
    recipe["jointClearance"]=.4;recipe=SetPlateRecipe(scene,recipe);const auto changed=GeneratePlates(scene);EXPECT_EQ(changed.at("parts").To<Array>().size(),good.at("parts").To<Array>().size());for(size_t i=0;i<changed.at("parts").To<Array>().size();++i)EXPECT_EQ(changed.at("parts").To<Array>()[i].To<Map>().at("partId"),good.at("parts").To<Array>()[i].To<Map>().at("partId"));AdoptPlates(scene,changed);EXPECT_EQ(Read(scene).at("fixtures").To<Array>().size(),before.To<Array>().size());
}
TEST(FiveAxisFixtureJoinery, StaggeredBoundaryContactsKeepAFullLowTenonFoot){
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-40,40,-30,30).Face());Imported(scene,part);
    auto recipe=JointRecipe(scene);recipe["pattern"]=Text("Staggered");recipe["spacing"]=35.;recipe["postWidth"]=20.;recipe["tenonWidth"]=16.;recipe["areaWidth"]=120.;recipe["areaDepth"]=120.;recipe=SetPlateRecipe(scene,recipe);
    const auto result=GeneratePlates(scene);ASSERT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");bool checkedBoundary=false;
    for(auto v:result.at("parts").To<Array>()){const auto p=v.To<Map>();if(p.at("role").To<Text>()=="Base")continue;const auto span=p.at("span").To<Array>();EXPECT_NEAR(span[1].To<double>()-span[0].To<double>(),20.,1e-6);EXPECT_FALSE(p.at("joints").To<Array>().empty());
        for(auto q:p.at("contactPoints").To<Array>())EXPECT_LE(std::abs(q.To<Array>()[1].To<double>()),30.00001);
        if(span[0].To<double>()<-30){const auto shape=JointPartShape(scene,p);EXPECT_EQ(JointState(shape,JointPoint(p,-31.4,2.)),TopAbs_IN);EXPECT_EQ(JointState(shape,JointPoint(p,-31.4,10.)),TopAbs_OUT);checkedBoundary=true;}
    }EXPECT_TRUE(checkedBoundary);
}

namespace {
TopoDS_Wire WindowWire(const Map& part,const Array& loop){BRepBuilderAPI_MakePolygon polygon;for(auto value:loop){auto p=value.To<Array>();polygon.Add(JointPoint(part,p[0].To<double>(),p[1].To<double>()));}polygon.Close();if(!polygon.IsDone())throw std::runtime_error("Window test wire invalid");return polygon.Wire();}
double WindowVolume(Scene& scene,const Map& part){GProp_GProps mass;BRepGProp::VolumeProperties(JointPartShape(scene,part),mass);return std::abs(mass.Mass());}
void CheckWindowMaterial(Scene& scene,const Map& part,double web,double unperforatedVolume){
    const auto shape=JointPartShape(scene,part);ASSERT_TRUE(BRepCheck_Analyzer(shape).IsValid());size_t solids=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++solids;EXPECT_EQ(solids,1u);
    const auto outline=WindowWire(part,part.at("outline").To<Array>());const auto holes=part.at("holes").To<Array>();std::vector<TopoDS_Wire> wires;double removedArea=0;
    for(const auto& value:holes){auto loop=value.To<Array>();auto wire=WindowWire(part,loop);BRepExtrema_DistShapeShape outside(wire,outline);ASSERT_TRUE(outside.IsDone());EXPECT_GE(outside.Value(),web-1e-5);removedArea+=JointArea(loop);
        for(const auto& previous:wires){BRepExtrema_DistShapeShape distance(wire,previous);ASSERT_TRUE(distance.IsDone());EXPECT_GE(distance.Value(),web-1e-5);}wires.push_back(wire);
        for(auto jointValue:part.at("joints").To<Array>()){auto joint=jointValue.To<Map>();const auto type=joint.at("type").To<Text>();if(type!="LapTop"&&type!="LapBottom")continue;
            double first=1e100,last=-1e100,left=1e100,right=-1e100;for(auto point:joint.at("boundary").To<Array>()){const auto p=point.To<Array>();first=std::min(first,p[0].To<double>());last=std::max(last,p[0].To<double>());}for(auto point:loop){const auto p=point.To<Array>();left=std::min(left,p[0].To<double>());right=std::max(right,p[0].To<double>());}EXPECT_TRUE(right<=first-web+1e-5||left>=last+web-1e-5);
        }
    }
    // The display outline samples exact tenon arcs into chords. Use the original
    // solid so the volume check measures only the material removed by windows.
    GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);EXPECT_NEAR(std::abs(mass.Mass()),unperforatedVolume-removedArea*part.at("thickness").To<double>(),1e-4);
}
}
TEST(FiveAxisFixtureWindows, ClosedOpeningsPreserveJointsContactsUndoRedoAndDelivery){
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);
    auto recipe=JointRecipe(scene);recipe["baseDrop"]=80.;recipe=SetPlateRecipe(scene,recipe);const auto first=GeneratePlates(scene);AdoptPlates(scene,first);const auto fixturesBefore=Read(scene).at("fixtures");
    recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe=SetPlateRecipe(scene,recipe);const auto result=GeneratePlates(scene);const auto parts=result.at("parts").To<Array>(),previous=first.at("parts").To<Array>();ASSERT_EQ(parts.size(),previous.size());EXPECT_EQ(result.at("joinery"),first.at("joinery"));EXPECT_EQ(result.at("supportCheck"),first.at("supportCheck"));size_t windows=0;
    for(size_t i=0;i<parts.size();++i){const auto p=parts[i].To<Map>(),prior=previous[i].To<Map>();EXPECT_EQ(p.at("outline"),prior.at("outline"));EXPECT_EQ(p.at("joints"),prior.at("joints"));EXPECT_EQ(p.at("contactPoints"),prior.at("contactPoints"));if(p.at("role").To<Text>()=="Base"){const auto holes=p.at("holes").To<Array>(),mortises=prior.at("holes").To<Array>();ASSERT_EQ(holes.size(),mortises.size());EXPECT_TRUE(p.at("windowHoleIndices").To<Array>().empty());for(size_t j=0;j<mortises.size();++j)EXPECT_EQ(holes[j],mortises[j]);windows+=holes.size()-mortises.size();continue;}ASSERT_TRUE(prior.at("holes").To<Array>().empty());windows+=p.at("holes").To<Array>().size();CheckWindowMaterial(scene,p,8.,WindowVolume(scene,prior));}
    ASSERT_GT(windows,0u);AdoptPlates(scene,result);const auto accepted=Read(scene).at("fixtures");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(Read(scene).at("fixtures"),fixturesBefore);ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(Read(scene).at("fixtures"),accepted);
    Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));const auto destination=std::filesystem::current_path()/"fixture-windows-native-delivery";std::filesystem::create_directories(destination);auto request=FixtureRequest(scene);auto pathText=destination.u8string();request["destination"]=Text(reinterpret_cast<const char*>(pathText.data()),pathText.size());const auto delivery=Invoke(&scene,"CamFixture","Export",request);EXPECT_FALSE(delivery.at("files").To<Array>().empty());
    // Export performs independent DXF import and checks every closed inner loop.
    const auto before=Read(scene);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="自动开窗支撑";auto path=part.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));EXPECT_EQ(before.at("fixtures"),Read(reopened).at("fixtures"));for(size_t i=0;i<parts.size();++i){const auto p=parts[i].To<Map>();if(p.at("role").To<Text>()=="Support")CheckWindowMaterial(reopened,p,8.,WindowVolume(scene,previous[i].To<Map>()));}std::filesystem::remove(path);RecordProperty("closed_windows",static_cast<int>(windows));
}
TEST(FiveAxisFixtureWindows, TiltedWeldProfilesKeepBoundaryBandsAndCrossingPosts){
    BRepBuilderAPI_MakePolygon boundary;for(auto p:std::array<std::array<double,2>,4>{{{-60,-60},{60,-60},{60,60},{-60,60}}})boundary.Add(gp_Pnt(p[0],p[1],.2*p[0]+.3*p[1]));boundary.Close();Scene scene;File part(BRepBuilderAPI_MakeFace(boundary.Wire()).Face());Imported(scene,part);
    auto recipe=JointRecipe(scene);recipe["jointMode"]=Text("Weld");recipe["baseDrop"]=80.;recipe=SetPlateRecipe(scene,recipe);const auto baseline=GeneratePlates(scene);recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe=SetPlateRecipe(scene,recipe);const auto result=GeneratePlates(scene);const auto parts=result.at("parts").To<Array>(),prior=baseline.at("parts").To<Array>();ASSERT_EQ(parts.size(),prior.size());size_t windows=0;
    for(size_t i=0;i<parts.size();++i){const auto p=parts[i].To<Map>();EXPECT_EQ(p.at("outline"),prior[i].To<Map>().at("outline"));EXPECT_EQ(p.at("contactPoints"),prior[i].To<Map>().at("contactPoints"));if(p.at("role").To<Text>()=="Base")continue;windows+=p.at("holes").To<Array>().size();ASSERT_TRUE(prior[i].To<Map>().at("holes").To<Array>().empty());CheckWindowMaterial(scene,p,8.,WindowVolume(scene,prior[i].To<Map>()));
        const auto frame=p.at("frame").To<Array>();if(frame[2].To<double>()<.5)continue;const double coordinate=frame[3].To<double>()+p.at("thickness").To<double>()/2;const auto shape=JointPartShape(scene,p);
        for(auto otherValue:parts){auto other=otherValue.To<Map>();if(other.at("role").To<Text>()=="Base")continue;const auto otherFrame=other.at("frame").To<Array>(),span=other.at("span").To<Array>();if(otherFrame[2].To<double>()>.5||coordinate+1.5<span[0].To<double>()-1e-5||coordinate-1.5>span[1].To<double>()+1e-5)continue;
            const double crossing=otherFrame[7].To<double>()-other.at("thickness").To<double>()/2;EXPECT_EQ(JointState(shape,JointPoint(p,crossing,20.)),TopAbs_IN)<<"weld crossing must retain a post";
        }
    }EXPECT_GT(windows,0u);
}
TEST(FiveAxisFixtureWindows, NarrowPostsRemainSolidAndInvalidParametersCannotChangeRecipe){
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);auto recipe=JointRecipe(scene);recipe["pattern"]=Text("Staggered");recipe["postWidth"]=12.;recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe=SetPlateRecipe(scene,recipe);const auto result=GeneratePlates(scene);
    for(auto value:result.at("parts").To<Array>()){auto p=value.To<Map>();if(p.at("role").To<Text>()=="Support")EXPECT_TRUE(p.at("holes").To<Array>().empty());}AdoptPlates(scene,result);const auto before=Read(scene);
    for(double web:{-1.,0.,2.9,501.}){auto bad=recipe;bad["windowWeb"]=web;EXPECT_THROW(SetPlateRecipe(scene,bad),std::exception);EXPECT_EQ(Read(scene).at("setups"),before.at("setups"));EXPECT_EQ(Read(scene).at("fixtures"),before.at("fixtures"));}
    auto bad=recipe;bad["windowMode"]=Text("LaserRelief");EXPECT_THROW(SetPlateRecipe(scene,bad),std::exception);EXPECT_EQ(Read(scene).at("setups"),before.at("setups"));
    recipe["windowMode"]=Text("None");recipe["windowWeb"]=-1.;SetPlateRecipe(scene,recipe);const auto disabled=GeneratePlates(scene);for(auto value:disabled.at("parts").To<Array>()){auto p=value.To<Map>();if(p.at("role").To<Text>()=="Support")EXPECT_TRUE(p.at("holes").To<Array>().empty());}
}
