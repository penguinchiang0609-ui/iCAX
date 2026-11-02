namespace fixture_parallel_tests {
Text Environment(const char* name){char* value=nullptr;size_t length=0;_dupenv_s(&value,&length,name);Text result=value?value:"";std::free(value);return result;}
template<class Bytes> Text Digest(const Bytes& bytes){
    unsigned char hash[32]{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<typename Bytes::value_type*>(bytes.data())),static_cast<ULONG>(bytes.size()),hash,sizeof(hash))<0)throw std::runtime_error("Fixture digest failed");
    constexpr char hex[]="0123456789abcdef";Text result;for(auto byte:hash){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
template<class Points> Array PointsValue(const Points& points){Array result;for(const auto& point:points){Array row;for(auto n:point)row.emplace_back(n);result.emplace_back(row);}return result;}
Map Snapshot(const iCAX::Geometry::SectionPlate& p){
    auto exact=p.Geometry;exact.Triangulations3.clear();Array frame,holes,joints,windows;
    for(auto n:p.Frame)frame.emplace_back(n);for(const auto& h:p.Holes)holes.emplace_back(PointsValue(h));for(auto n:p.WindowHoleIndices)windows.emplace_back(static_cast<UInt>(n));
    for(const auto& j:p.Joints)joints.emplace_back(Map{{"type",j.Type},{"index",static_cast<UInt>(j.Index)},{"mate",static_cast<UInt>(j.MateStationIndex)},{"mateBase",j.MateBase},{"boundary",PointsValue(j.Boundary)}});
    return {{"station",static_cast<UInt>(p.StationIndex)},{"base",p.Base},{"frame",frame},{"outline",PointsValue(p.Outline)},{"holes",holes},{"joints",joints},{"windows",windows},{"contacts",PointsValue(p.Contacts)},
        {"contactLength",p.ContactLength},{"distance",p.Distance},{"supported",p.Supported},{"supportMargin",p.SupportMargin},{"hull",PointsValue(p.SupportHull)},
        {"exactProfile",iCAX::CAD::TubeCadJson::Encode(p.ExactProfile)},{"topologyDigest",Digest(iCAX::GeometryData::Persistence::Serialize(exact))},{"meshDigest",Digest(iCAX::GeometryData::Persistence::Serialize(p.Geometry))}};
}
}
TEST(FiveAxisFixtureParallelism, SerialAndParallelPreserveGeometryJointsAndOrdering){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.TenonRadius=1;in.BaseDrop=80;in.WindowMode="Automatic";in.WindowWeb=8;in.Stations=service->SuggestStations(in,50);
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);in.MaximumConcurrency=1;const auto serial=service->Build(in,{});in.MaximumConcurrency=4;std::set<std::thread::id> readers;
    const auto parallel=service->Build(in,[&]{readers.insert(std::this_thread::get_id());return false;});ASSERT_EQ(serial.size(),parallel.size());EXPECT_GT(readers.size(),1u);
    for(size_t i=0;i<serial.size();++i)EXPECT_EQ(fixture_parallel_tests::Snapshot(serial[i]),fixture_parallel_tests::Snapshot(parallel[i]))<<i;
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
    auto invalid=in;invalid.TenonWidth=200;invalid.TenonPitch=300;Text error;
    for(size_t workers:{1u,4u}){invalid.MaximumConcurrency=workers;try{service->Build(invalid,{});ADD_FAILURE()<<"Expected an unbuildable tenon";}catch(const std::exception& e){if(error.empty())error=e.what();else EXPECT_EQ(error,e.what());}}
}
TEST(FiveAxisFixtureParallelism, WorkerCancellationDrainsBeforeReturningAndLeavesCadUnchanged){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.MaximumConcurrency=4;in.Stations=service->SuggestStations(in,35);
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto caller=std::this_thread::get_id();bool cancelled=false;
    EXPECT_THROW(service->Build(in,[&]{if(std::this_thread::get_id()!=caller)cancelled=true;return cancelled;}),std::exception);EXPECT_TRUE(cancelled);
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
    const auto recovered=service->Build(in,{});EXPECT_GT(recovered.size(),1u);
}
TEST(FiveAxisFixtureParallelism, DISABLED_RealHatRailGridBenchmark){
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path("TestData/FA/Video_HatRail_1200x220x70_t1p2.step");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}
    ASSERT_TRUE(std::filesystem::exists(root/relative));Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());
    ASSERT_EQ(Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request)).at("status").To<Text>(),"Completed");
    const auto state=Read(scene),ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();iCAX::Geometry::SectionPlateInput in;
    in.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));const auto transform=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();for(size_t i=0;i<16;++i)in.Transform[i]=transform[i].To<double>();
    in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.TenonRadius=1;in.WindowMode="Automatic";in.WindowWeb=8;in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;
    const auto workerSetting=fixture_parallel_tests::Environment("ICAX_FIXTURE_PROBE_WORKERS");if(!workerSetting.empty())in.MaximumConcurrency=std::stoull(workerSetting);
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();in.FaceIDs=service->FindLowerEnvelopeFaces(in,{});ASSERT_EQ(in.FaceIDs.size(),46u);in.Stations=service->SuggestStations(in,35);
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto started=std::chrono::steady_clock::now();const auto parts=service->Build(in,{});const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));ASSERT_EQ(parts.size(),35u);EXPECT_TRUE(parts.back().Supported);Array snapshots;for(const auto& part:parts)snapshots.emplace_back(fixture_parallel_tests::Snapshot(part));
    const Map report{{"passed",true},{"buildMs",elapsed},{"parts",snapshots},{"sourceDigest",fixture_parallel_tests::Digest(original)}};const auto json=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
    const auto destination=fixture_parallel_tests::Environment("ICAX_FIXTURE_PARALLEL_REPORT");if(!destination.empty()){std::ofstream file(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(destination.data()),destination.size())),std::ios::binary);file<<json;ASSERT_TRUE(file.good());}
    RecordProperty("buildMs",elapsed);std::cout<<"fixture grid benchmark "<<elapsed<<" ms"<<std::endl;
}
