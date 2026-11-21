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
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);in.MaximumConcurrency=1;const auto serial=service->Build(in,{});
    for(size_t workers:{2u,4u}){SCOPED_TRACE(workers);in.MaximumConcurrency=workers;std::set<std::thread::id> readers;
        const auto parallel=service->Build(in,[&]{readers.insert(std::this_thread::get_id());return false;});ASSERT_EQ(serial.size(),parallel.size());EXPECT_GT(readers.size(),1u);
        for(size_t i=0;i<serial.size();++i)EXPECT_EQ(fixture_parallel_tests::Snapshot(serial[i]),fixture_parallel_tests::Snapshot(parallel[i]))<<i;
        EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));}
    auto invalid=in;invalid.TenonWidth=200;invalid.TenonPitch=300;Text error;
    for(size_t workers:{1u,2u,4u}){invalid.MaximumConcurrency=workers;try{service->Build(invalid,{});ADD_FAILURE()<<"Expected an unbuildable tenon";}catch(const std::exception& e){if(error.empty())error=e.what();else EXPECT_EQ(error,e.what());}}
}
TEST(FiveAxisFixtureParallelism, CurvedSheetAndSolidChecksPreserveExactResultsAcrossWorkerCounts){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    for(bool solid:{false,true}){SCOPED_TRACE(solid?"curved solid":"curved sheet");
        const auto sheet=FixtureCurvedSheet();const auto work=solid?BRepPrimAPI_MakePrism(sheet,gp_Vec(0,0,8)).Shape():sheet;
        auto input=AutoFaceInput(work);const auto original=iCAX::GeometryData::Persistence::Serialize(*input.Geometry);
        input.FaceIDs=service->FindLowerEnvelopeFaces(input,{});ASSERT_FALSE(input.FaceIDs.empty());input.Pattern="Parallel";input.JointMode="TabSlot";input.TenonRadius=1;input.BaseDrop=80;input.WindowMode="Automatic";input.WindowWeb=8;
        input.Stations={{-20.,-60.,60.,true},{20.,-60.,60.,true}};input.MaximumConcurrency=1;
        const auto serial=service->Build(input,{});ASSERT_EQ(serial.size(),3u);ASSERT_TRUE(serial.back().Base);ASSERT_FALSE(serial.front().Contacts.empty());ASSERT_FALSE(serial.front().Joints.empty());ASSERT_FALSE(serial.front().ExactProfile.Boundaries.empty());
        EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*input.Geometry));
        for(size_t workers:{2u,4u}){SCOPED_TRACE(workers);input.MaximumConcurrency=workers;const auto parallel=service->Build(input,{});ASSERT_EQ(serial.size(),parallel.size());
            for(size_t i=0;i<serial.size();++i)EXPECT_EQ(fixture_parallel_tests::Snapshot(serial[i]),fixture_parallel_tests::Snapshot(parallel[i]))<<i;
            EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*input.Geometry));}
    }
}
TEST(FiveAxisFixtureParallelism, WorkerCancellationDrainsBeforeReturningAndLeavesCadUnchanged){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.MaximumConcurrency=4;in.Stations=service->SuggestStations(in,35);
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto caller=std::this_thread::get_id();bool cancelled=false;
    EXPECT_THROW(service->Build(in,[&]{if(std::this_thread::get_id()!=caller)cancelled=true;return cancelled;}),std::exception);EXPECT_TRUE(cancelled);
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
    const auto recovered=service->Build(in,{});EXPECT_GT(recovered.size(),1u);
}
TEST(FiveAxisFixtureExact, CalculationHasNoMeshAndIgnoresDisplayTriangulations){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto input=ClearanceInput();
    const auto work=FixtureCurvedSheet();auto geometry=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(work,"Exact support test","generated",.2);
    input.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(geometry);input.FaceIDs.clear();for(const auto& face:geometry.Faces)input.FaceIDs.push_back(face.Id);
    input.Thickness=10;input.Stations={{2.,-60.,60.,true}};input.MaximumConcurrency=1;
    const auto original=iCAX::GeometryData::Persistence::Serialize(*input.Geometry);const auto withDisplay=service->Build(input,{});
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*input.Geometry));
    geometry.Triangulations3.clear();for(auto& face:geometry.Faces)face.Triangulation3Id=0;input.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(geometry);input.MaximumConcurrency=4;
    const auto withoutDisplay=service->Build(input,{});ASSERT_EQ(withDisplay.size(),withoutDisplay.size());
    for(size_t i=0;i<withDisplay.size();++i){const auto& plate=withoutDisplay[i];EXPECT_EQ(fixture_parallel_tests::Snapshot(withDisplay[i]),fixture_parallel_tests::Snapshot(plate));EXPECT_TRUE(plate.Geometry.Triangulations3.empty());for(const auto& face:plate.Geometry.Faces)EXPECT_EQ(0u,face.Triangulation3Id);EXPECT_DOUBLE_EQ(0.,plate.TopRetreat);EXPECT_DOUBLE_EQ(0.,plate.MeshDeflection);EXPECT_FALSE(plate.ExactProfile.Boundaries.empty());}
    const auto solid=ClearanceShape(withoutDisplay.front());EXPECT_TRUE(BRepCheck_Analyzer(solid).IsValid());
    // The entire slab is x=[-3,7]. Its true minimum lies at x=0 inside
    // the slab, below both end sections. The retained top is a spline.
    BRepExtrema_DistShapeShape contact(solid,BRepBuilderAPI_MakeVertex(gp_Pnt(0,0,16)).Vertex());ASSERT_TRUE(contact.IsDone());EXPECT_LE(contact.Value(),1e-6);
    bool hasCurve=false;for(TopExp_Explorer e(solid,TopAbs_EDGE);e.More();e.Next())hasCurve|=BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()!=GeomAbs_Line;EXPECT_TRUE(hasCurve);
    BRepExtrema_DistShapeShape separation(solid,work);ASSERT_TRUE(separation.IsDone());EXPECT_LE(separation.Value(),1e-6);
    const auto above=BRepPrimAPI_MakePrism(work,gp_Vec(0,0,100)).Shape();BRepAlgoAPI_Common overlap(solid,above);ASSERT_TRUE(overlap.IsDone());GProp_GProps volume;BRepGProp::VolumeProperties(overlap.Shape(),volume);EXPECT_LE(std::abs(volume.Mass()),1e-8);
    const auto display=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();ASSERT_TRUE(display);
    iCAX::GeometryServices::BRepDisplayOptions options;options.Deflection=.1;auto shown=display->RefineMesh(withoutDisplay.front().Geometry,options);EXPECT_FALSE(shown.Triangulations3.empty());shown.Triangulations3.clear();for(auto& face:shown.Faces)face.Triangulation3Id=0;
    EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(withoutDisplay.front().Geometry),iCAX::GeometryData::Persistence::Serialize(shown));
}
TEST(FiveAxisFixtureExact, InteriorContactFollowsTheSurfaceEquationAcrossParameters){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto input=ClearanceInput();
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v){const double x=(u-2)*50.,y=(v-2)*60.;poles.SetValue(u,v,gp_Pnt(x,y,(u==2?0.:12.)+(v==2?0.:20.)+.002*x*y));}
    NCollection_Array1<double> knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    const occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,knots,knots,multiplicities,multiplicities,2,2);
    const auto work=BRepBuilderAPI_MakeFace(surface,1e-7).Face();const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(work,"Implicit contour test","generated",.2);
    input.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(model);input.FaceIDs={model.Faces.front().Id};input.Thickness=50;input.Stations={{0.,-60.,60.,true}};input.BaseDrop=80;
    const auto original=iCAX::GeometryData::Persistence::Serialize(model);const auto plates=service->Build(input,{});ASSERT_EQ(2u,plates.size());const auto solid=ClearanceShape(plates.front());
    // z=16+.0024*x*x+(1/360)*y*y+.002*x*y. The interior
    // minimum x=-5*y/12 varies across the original surface parameters.
    for(double y:{-30.,0.,30.}){const double x=-5*y/12,z=16+.0024*x*x+y*y/360+.002*x*y;BRepExtrema_DistShapeShape contact(solid,BRepBuilderAPI_MakeVertex(gp_Pnt(x,y,z)).Vertex());ASSERT_TRUE(contact.IsDone());EXPECT_LE(contact.Value(),1e-6);}
    const auto above=BRepPrimAPI_MakePrism(work,gp_Vec(0,0,100)).Shape();BRepAlgoAPI_Common overlap(solid,above);ASSERT_TRUE(overlap.IsDone());GProp_GProps volume;BRepGProp::VolumeProperties(overlap.Shape(),volume);EXPECT_LE(std::abs(volume.Mass()),1e-8);
    EXPECT_TRUE(BRepCheck_Analyzer(solid).IsValid());EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*input.Geometry));EXPECT_TRUE(plates.front().Geometry.Triangulations3.empty());
}
TEST(FiveAxisFixtureExact, SplineExchangeTrimsRationalBasisAndPreservesTraversal){
    using namespace iCAX::GeometryData;NURBS2 basis;basis.Degree=2;basis.Poles={{-30,0},{-10,20},{10,-5},{30,25},{50,0}};basis.Weights={1,.7,1.2,.9,1};basis.Knots={0,.4,.7,1};basis.Multiplicities={3,1,1,3};
    const auto source=iCAX::OpenCascade::BuildOpenCascadeCurve2(basis);
    for(bool reversed:{false,true}){const auto finite=iCAX::Geometry::BoundedPlanarSpline({basis,{.2,.85,reversed}});ASSERT_FALSE(finite.Periodic);EXPECT_EQ(finite.Multiplicities.front(),3);EXPECT_EQ(finite.Multiplicities.back(),3);EXPECT_NEAR(finite.Knots.front(),.2,1e-14);EXPECT_NEAR(finite.Knots.back(),.85,1e-14);
        const auto actual=iCAX::OpenCascade::BuildOpenCascadeCurve2(finite);ASSERT_FALSE(actual.IsNull());
        for(int i=0;i<=50;++i){const double t=.2+.65*i/50;EXPECT_LE(actual->Value(t).Distance(source->Value(reversed?1.05-t:t)),1e-11);}
    }
}
TEST(FiveAxisFixtureExact, ThinExtrusionDisplayPreservesTheComputationalBRep){
    NCollection_Array1<gp_Pnt> poles(1,3);poles(1)=gp_Pnt(-1,0,0);poles(2)=gp_Pnt(0,1,0);poles(3)=gp_Pnt(1,0,0);NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    const occ::handle<Geom_BSplineCurve> curve=new Geom_BSplineCurve(poles,knots,multiplicities,2);const auto edge=BRepBuilderAPI_MakeEdge(curve,.5,.5000003).Edge();const auto strip=BRepPrimAPI_MakePrism(edge,gp_Vec(0,0,3)).Shape();BRep_Builder builder;
    for(TopExp_Explorer f(strip,TopAbs_FACE);f.More();f.Next())builder.UpdateFace(TopoDS::Face(f.Current()),5e-6);
    for(TopExp_Explorer e(strip,TopAbs_EDGE);e.More();e.Next())builder.UpdateEdge(TopoDS::Edge(e.Current()),5e-6);
    for(TopExp_Explorer v(strip,TopAbs_VERTEX);v.More();v.Next())builder.UpdateVertex(TopoDS::Vertex(v.Current()),5e-6);
    const auto original=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(strip,"Thin display strip","generated");const auto bytes=iCAX::GeometryData::Persistence::Serialize(original);Scene scene;const auto service=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();iCAX::GeometryServices::BRepDisplayOptions options;options.Deflection=.01;
    auto display=service->RefineMesh(original,options);ASSERT_EQ(display.Triangulations3.size(),1u);const auto& mesh=display.Triangulations3.front().Geometry;ASSERT_FALSE(mesh.Triangles.empty());EXPECT_EQ(bytes,iCAX::GeometryData::Persistence::Serialize(original));
    for(const auto& p:mesh.Vertices){BRepExtrema_DistShapeShape distance(strip,BRepBuilderAPI_MakeVertex(gp_Pnt(p.X,p.Y,p.Z)).Vertex());ASSERT_TRUE(distance.IsDone());EXPECT_LE(distance.Value(),1e-10);}
    display.Triangulations3.clear();for(auto& face:display.Faces)face.Triangulation3Id=0;EXPECT_EQ(bytes,iCAX::GeometryData::Persistence::Serialize(display));
}
TEST(FiveAxisFixtureExact, SerialAndParallelDisplayMeshingCoverEveryFaceWithoutChangingComputationalBRep){
    const auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepGeometry(BRepPrimAPI_MakeCylinder(20,40).Shape(),"Display parallelism","generated");const auto original=iCAX::GeometryData::Persistence::Serialize(model);
    ASSERT_TRUE(model.Triangulations3.empty());Scene scene;const auto service=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();ASSERT_TRUE(service);
    const std::array<std::pair<bool,size_t>,4> modes{{{true,0},{false,1},{true,2},{true,4}}};
    for(const auto& [parallel,workers]:modes){SCOPED_TRACE(::testing::Message()<<"parallel="<<parallel<<" workers="<<workers);iCAX::GeometryServices::BRepDisplayOptions options;options.Deflection=.05;options.ParallelMeshing=parallel;options.MaximumConcurrency=workers;auto shown=service->RefineMesh(model,options);
        EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(model));ASSERT_EQ(shown.Faces.size(),model.Faces.size());
        for(const auto& face:shown.Faces){const auto found=std::find_if(shown.Triangulations3.begin(),shown.Triangulations3.end(),[&](const auto& mesh){return mesh.Id==face.Triangulation3Id;});ASSERT_NE(found,shown.Triangulations3.end());
            const auto& mesh=found->Geometry;ASSERT_FALSE(mesh.Triangles.empty());ASSERT_FALSE(mesh.Vertices.empty());EXPECT_EQ(mesh.Normals.size(),mesh.Vertices.size());ASSERT_EQ(mesh.TriangleFaceIds.size(),mesh.Triangles.size());
            for(auto id:mesh.TriangleFaceIds)EXPECT_EQ(id,face.Id);for(const auto& triangle:mesh.Triangles)for(auto index:triangle)EXPECT_LT(index,mesh.Vertices.size());}
        shown.Triangulations3.clear();for(auto& face:shown.Faces)face.Triangulation3Id=0;EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(shown));
    }
}
TEST(FiveAxisFixtureExact, CurvedSupportDXFReimportsTheTrimmedNativeEdges){
    Scene scene;File work(FixtureCurvedSheet());Imported(scene,work);SetPlateRecipe(scene,PlateRecipe(scene));const auto generated=GeneratePlates(scene);AdoptPlates(scene,generated);Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));
    const auto folder=std::filesystem::current_path()/"output/acceptance/fixture-exact-dxf";std::filesystem::create_directories(folder);auto request=FixtureRequest(scene);request["destination"]=folder.string();const auto delivered=Invoke(&scene,"CamFixture","Export",request);const auto directory=std::filesystem::path(delivered.at("directory").To<Text>());
    const auto reader=scene.Services().Resolve<iCAX::CAD::ICadDocumentImportService>();size_t curvedEdges=0;
    for(const auto& value:generated.at("parts").To<Array>()){const auto part=value.To<Map>();if(part.at("role").To<Text>()=="Base")continue;const auto profile=iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CPlanarRegion2>(part.at("exactProfile"));const auto document=reader->Load(directory/(part.at("number").To<Text>()+".dxf")).Document;size_t ordinal=0;
        for(const auto& entity:document.Sections.at("ENTITIES")){Text layer;for(const auto& g:entity.Values)if(g.Code==8)layer=g.Value;if(layer!="CUT_OUTER")continue;ASSERT_EQ(entity.Type,"SPLINE");ASSERT_LT(ordinal,profile.Boundaries.front().Curves.size());
            iCAX::GeometryData::NURBS2 spline;std::vector<double> xs,ys;
            for(const auto& g:entity.Values){if(g.Code==71)spline.Degree=std::stoi(g.Value);if(g.Code==10)xs.push_back(std::stod(g.Value));if(g.Code==20)ys.push_back(std::stod(g.Value));if(g.Code==41)spline.Weights.push_back(std::stod(g.Value));if(g.Code==40){const double k=std::stod(g.Value);if(spline.Knots.empty()||spline.Knots.back()!=k){spline.Knots.push_back(k);spline.Multiplicities.push_back(1);}else ++spline.Multiplicities.back();}}
            ASSERT_EQ(xs.size(),ys.size());for(size_t i=0;i<xs.size();++i)spline.Poles.push_back({xs[i],ys[i]});const auto actual=iCAX::OpenCascade::BuildOpenCascadeCurve2(spline);ASSERT_FALSE(actual.IsNull());const auto& expected=profile.Boundaries.front().Curves[ordinal++].Segment;const auto source=iCAX::OpenCascade::BuildOpenCascadeCurve2(expected.Curve);ASSERT_FALSE(source.IsNull());double a=expected.Range.First,b=expected.Range.Last;
            if(const auto* line=std::get_if<iCAX::GeometryData::Segment2>(&expected.Curve)){const double length=std::hypot(line->End.X-line->Start.X,line->End.Y-line->Start.Y);a*=length;b*=length;}else ++curvedEdges;
            EXPECT_LE(actual->Value(actual->FirstParameter()).Distance(source->Value(expected.Range.Reversed?b:a)),1e-8);EXPECT_LE(actual->Value(actual->LastParameter()).Distance(source->Value(expected.Range.Reversed?a:b)),1e-8);
            for(int i=0;i<=20;++i){const auto p=actual->Value(actual->FirstParameter()+(actual->LastParameter()-actual->FirstParameter())*i/20);Geom2dAPI_ProjectPointOnCurve distance(p,source,a,b);ASSERT_GT(distance.NbPoints(),0);EXPECT_LE(distance.LowerDistance(),1e-8);}
        }EXPECT_EQ(ordinal,profile.Boundaries.front().Curves.size());
    }EXPECT_GT(curvedEdges,0u);
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
