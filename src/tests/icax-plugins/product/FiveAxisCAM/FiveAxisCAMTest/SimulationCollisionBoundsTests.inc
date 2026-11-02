namespace {
CollisionQuery AnalyticHead(){CollisionQuery q;q.Solids={{0,16,0,7},{16,16,7,11},{32,24,12,12}};return q;}
}
TEST(FiveAxisCollisionBounds, DISABLED_ReplaySavedFixtureTimeouts){
    wchar_t path[32768]{};ASSERT_GT(GetEnvironmentVariableW(L"ICAX_COLLISION_REPLAY_PROJECT",path,32768),0u);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(std::filesystem::path(path));const auto info=prepared.Result().Info;
    Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto state=Read(scene),setup=state.at("setups").To<Array>().front().To<Map>(),report=setup.at("simulationCheck").To<Map>();
    ASSERT_EQ(report.at("validity").To<Text>(),"Current");const auto frames=report.at("frames").To<Array>();auto q=AnalyticHead();q.MaxSegmentTimeMs=5000;
    const auto add=[&](const Map& object,const Map& ref){iCAX::GeometryServices::TravelObstacle obstacle;obstacle.Id=object.at("id").To<Text>();
        obstacle.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
        const auto values=object.at("transform").To<Array>();for(size_t i=0;i<16;++i)obstacle.Transform[i]=values[i].To<double>();q.Obstacles.push_back(std::move(obstacle));};
    for(const auto& value:state.at("placements").To<Array>()){const auto p=value.To<Map>();for(const auto& w:state.at("workpieces").To<Array>())if(w.To<Map>().at("id")==p.at("workpieceId"))add(p,w.To<Map>().at("geometryRef").To<Map>());}
    for(const auto& value:state.at("fixtures").To<Array>()){const auto f=value.To<Map>();if(f.at("enabled").To<bool>()&&f.at("collisionEnabled").To<bool>())add(f,f.at("geometryRef").To<Map>());}
    // Reproduce the production snapshot's entity enumeration, which need not
    // put the nearest workpiece before far-away high-complexity fixtures.
    const auto unordered=q.Obstacles;q.Obstacles.clear();for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))for(const auto& o:unordered)if(o.Id==iCAX::Data::to_string(e->GetID()))q.Obstacles.push_back(o);
    const auto pose=[](const Map& f){iCAX::GeometryServices::RigidMotionPose p;const auto position=f.at("position").To<Array>(),rotation=f.at("rotation").To<Array>();for(size_t i=0;i<3;++i)p.Position[i]=position[i].To<double>();for(size_t i=0;i<4;++i)p.Rotation[i]=rotation[i].To<double>();return p;};
    std::vector<size_t> indices;for(const auto& value:report.at("segments").To<Array>()){const auto r=value.To<Map>();if(r.at("status").To<Text>()!="Unchecked"||r.at("reason").To<Text>()!="MOTION_TIME_BUDGET")continue;
        const auto i=std::stoull(r.at("index").To<Text>());const auto a=frames[i].To<Map>(),b=frames[i+1].To<Map>();ASSERT_EQ(a.at("role").To<Text>(),"Cut");ASSERT_EQ(b.at("role").To<Text>(),"Cut");
        indices.push_back(i);q.Segments.push_back({pose(a),pose(b),setup.at("rapidSettings").To<Map>().at("collisionThresholdMm").To<double>()});}
    ASSERT_FALSE(q.Segments.empty());std::cout<<"Exact fixture replay obstacles="<<q.Obstacles.size()<<" segments="<<q.Segments.size()<<" maxSegmentTimeMs="<<q.MaxSegmentTimeMs<<std::endl;
    size_t index=0;const auto started=std::chrono::steady_clock::now();auto previous=started;
    const auto result=CollisionService(scene)->CheckIncremental(q,[&]()->std::optional<size_t>{return index<q.Segments.size()?std::optional(index++):std::nullopt;},[&](size_t i,const auto& r){
        const auto now=std::chrono::steady_clock::now();std::cout<<"Exact fixture segment="<<indices[i]<<" status="<<r.Status<<" reason="<<r.Reason<<" obstacle="<<r.Obstacle<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(now-previous).count()<<std::endl;previous=now;
        EXPECT_EQ(r.Status,"Clear")<<"recorded index="<<indices[i]<<" reason="<<r.Reason;},{});
    std::cout<<"Exact fixture replay queries="<<result.DistanceQueries<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
}
TEST(FiveAxisCollisionBounds, ObstacleOrderRetainsClearanceAndContainedCollisions){
    Scene scene;auto q=AnalyticHead();q.Solids={{0,4,1,1}};q.Segments={{{},{},.1}};
    const auto imported=TravelModel(scene,BRepPrimAPI_MakeCylinder(1,4).Shape());
    const auto farObstacle=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-50,-50,-60),100,100,1).Shape());
    const auto nearObstacle=TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,-.3),gp_Dir(0,0,1)),-10,10,-10,10).Face());
    const auto inside=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,-.1,2),.2,.2,.2).Shape());
    for(bool useImported:{false,true}){
        q.Body=useImported?imported:nullptr;q.Solids=useImported?decltype(q.Solids){}:decltype(q.Solids){{0,4,1,1}};
        for(bool reverse:{false,true}){
            q.Obstacles={{"far",farObstacle},{"near",nearObstacle}};if(reverse)std::reverse(q.Obstacles.begin(),q.Obstacles.end());
            const auto clear=CollisionService(scene)->Check(q,{});ASSERT_EQ(clear.Segments.size(),1u);EXPECT_EQ(clear.Segments[0].Status,"Clear");EXPECT_GE(clear.Segments[0].CertifiedClearance,.1);
            q.Obstacles.insert(q.Obstacles.begin()+(reverse?0:q.Obstacles.size()),{"contained",inside});
            const auto collision=CollisionService(scene)->Check(q,{});ASSERT_EQ(collision.Segments.size(),1u);EXPECT_EQ(collision.Segments[0].Status,"Collision");EXPECT_EQ(collision.Segments[0].Obstacle,"contained");
        }
    }
}
TEST(FiveAxisCollisionBounds, CalibratedFrustaRemainSeparatedFromAnObliqueSheet){
    Scene scene;auto q=AnalyticHead();const auto normal=iCAX::GeometryServices::RigidMotion::Unit(iCAX::GeometryServices::RigidMotion::Vector{1,2,3});
    q.Obstacles.push_back({"sheet",TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(4,-3,8),gp_Dir(normal[0],normal[1],normal[2])),-100,100,-100,100).Face())});
    // The calibration and motion both rotate and translate. Their product
    // places local +Z along the sheet normal with exactly 1 mm tip clearance.
    const auto axis=iCAX::GeometryServices::RigidMotion::Between({0,0,1},normal);
    q.BodyCalibration.Rotation={0,0,std::sin(.3),std::cos(.3)};q.BodyCalibration.Position={2,1,0};
    auto rotation=iCAX::GeometryServices::RigidMotion::Multiply(axis,{0,0,-std::sin(.3),std::cos(.3)});
    const auto shift=iCAX::GeometryServices::RigidMotion::Rotate(rotation,q.BodyCalibration.Position);
    iCAX::GeometryServices::RigidMotionPose a{{4+normal[0]-shift[0],-3+normal[1]-shift[1],8+normal[2]-shift[2]},rotation},b=a;
    b.Position[0]+=.2;b.Position[1]-=.1;q.Segments.push_back({a,b,.1});
    const auto result=CollisionService(scene)->Check(q,{});ASSERT_EQ(result.Segments.size(),1u);EXPECT_EQ(result.Segments[0].Status,"Clear");EXPECT_GE(result.Segments[0].CertifiedClearance,.1);
    a.Position=iCAX::GeometryServices::RigidMotion::Sub(a.Position,iCAX::GeometryServices::RigidMotion::Scale(normal,2));q.Segments={{a,a,.1}};
    EXPECT_EQ(CollisionService(scene)->Check(q,{}).Segments[0].Status,"Collision");
}
TEST(FiveAxisCollisionBounds, AnalyticContainmentAndContinuousRotationCannotBeCertifiedClear){
    Scene scene;auto q=AnalyticHead();q.Segments.push_back({{}, {},0});
    q.Obstacles={{"enclosing",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,-100),200,200,200).Shape())}};
    EXPECT_EQ(CollisionService(scene)->Check(q,{}).Segments[0].Status,"Collision");
    q.Obstacles={{"inside",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,-.1,40),.2,.2,.2).Shape())}};
    EXPECT_EQ(CollisionService(scene)->Check(q,{}).Segments[0].Status,"Collision");
    q.Solids={{0,10,1,1}};q.BodyCalibration.Position={8,0,0};q.Obstacles={{"middle",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.2,7.8,4),.4,.4,1).Shape())}};
    q.Segments={{{{0,0,0},{0,0,0,1}},{{0,0,0},{0,0,1,0}},.01}};
    const auto r=CollisionService(scene)->Check(q,{});EXPECT_EQ(r.Segments[0].Status,"Collision");EXPECT_GT(r.Segments[0].Fraction,0.);EXPECT_LT(r.Segments[0].Fraction,1.);
    q.MaxSegmentTimeMs=0;EXPECT_THROW(CollisionService(scene)->Check(q,{}),std::invalid_argument);
    q.MaxSegmentTimeMs=60001;EXPECT_THROW(CollisionService(scene)->Check(q,{}),std::invalid_argument);
}
TEST(FiveAxisCollisionBounds, RealStampedRailReplaysFormerTimeoutsAndActualCollisions){
    auto root=std::filesystem::path(__FILE__).parent_path();while(!root.empty()&&!std::filesystem::exists(root/"TestData"))root=root.parent_path();ASSERT_FALSE(root.empty());
    const auto directory=root/"TestData/automotive-stamping/video-hat-rail";
    std::ifstream stream(directory/"collision-replay.json");ASSERT_TRUE(stream);std::string json((std::istreambuf_iterator<char>(stream)),{});if(json.starts_with("\xef\xbb\xbf"))json.erase(0,3);
    const auto replay=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<Map>();
    const auto file=iCAX::OpenCascade::ReadBRepFile((directory/"Video_HatRail_1200x220x70_t1p2.step").string());ASSERT_TRUE(file.bOK);
    Scene scene;auto q=AnalyticHead();q.Obstacles.push_back({"rail",std::make_shared<iCAX::GeometryData::BRepModel>(file.Geometry)});
    const auto number=[](const iCAX::Data::Variant& value){return value.Is<double>()?value.To<double>():static_cast<double>(value.To<long long>());};
    const auto transform=replay.at("transform").To<Array>();for(size_t i=0;i<16;++i)q.Obstacles[0].Transform[i]=number(transform[i]);
    const auto rows=replay.at("segments").To<Array>();
    const auto pose=[&](const Map& row){iCAX::GeometryServices::RigidMotionPose p;const auto position=row.at("position").To<Array>(),rotation=row.at("rotation").To<Array>();for(size_t i=0;i<3;++i)p.Position[i]=number(position[i]);for(size_t i=0;i<4;++i)p.Rotation[i]=number(rotation[i]);return p;};
    for(const auto& value:rows){const auto row=value.To<Map>();q.Segments.push_back({pose(row.at("start").To<Map>()),pose(row.at("end").To<Map>()),.1});}
    q.MaxSegmentTimeMs=5000;
    const auto started=std::chrono::steady_clock::now();const auto result=CollisionService(scene)->Check(q,{});ASSERT_EQ(result.Segments.size(),rows.size());size_t clear=0,collision=0,unchecked=0;
    for(size_t i=0;i<rows.size();++i){const auto& r=result.Segments[i];const auto row=rows[i].To<Map>();if(r.Status=="Clear")++clear;else if(r.Status=="Collision")++collision;else ++unchecked;
        EXPECT_NE(r.Status,"Unchecked")<<"recorded index="<<number(row.at("index"))<<" reason="<<r.Reason;
        if(row.at("previousStatus").To<Text>()=="Collision")EXPECT_EQ(r.Status,"Collision")<<number(row.at("index"));
    }
    std::cout<<"Collision replay clear="<<clear<<" collision="<<collision<<" unchecked="<<unchecked<<" queries="<<result.DistanceQueries<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
}
