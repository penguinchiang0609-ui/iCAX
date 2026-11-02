Map FixtureRequest(Scene& scene,const Text& id={})
{
    const auto state=Read(scene);
    Map result{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",state.at("activeSetupId")}};
    if(!id.empty())result["fixtureId"]=id;
    return result;
}
TopoDS_Shape FixtureCurvedSheet() {
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)poles.SetValue(u,v,gp_Pnt((u-2)*50.,(v-2)*60.,(u==2?0.:12.)+(v==2?0.:20.)));
    NCollection_Array1<double> knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);NCollection_Array1<int> mult(1,2);mult.Init(3);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,knots,knots,mult,mult,2,2);
    return BRepBuilderAPI_MakeFace(surface,1e-7).Face();
}
Map PlateRecipe(Scene& scene) {
    const auto state=Read(scene),w=state.at("workpieces").To<Array>()[0].To<Map>(),ref=w.at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    Array faces;for(const auto& face:geometry->Faces)faces.emplace_back(std::to_string(face.Id));
    Array stations;for(double x:{-20.,0.,20.})stations.emplace_back(Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",x}});
    return {{"supportFaces",faces},{"stations",stations},{"layout",Text("Manual")},{"pattern",Text("Parallel")},{"spacing",35.},{"baseShape",Text("Rectangle")},{"sizing",Text("Automatic")},{"areaWidth",240.},{"areaDepth",240.},{"areaX",0.},{"areaY",0.},{"postWidth",12.},{"family",Text("ParallelX")},{"material",Text("CarbonSteel")},{"thickness",3.},{"baseThickness",5.},{"baseDrop",40.},{"margin",15.},{"gap",.2},{"minimumWeb",5.},{"jointMode",Text("Weld")},{"jointClearance",.2},{"tenonWidth",8.},{"tenonPitch",60.},{"jointWeb",1.5}};
}
Map SetPlateRecipe(Scene& scene,const Map& recipe){auto r=FixtureRequest(scene);r["recipe"]=recipe;return Invoke(&scene,"CamFixture","SetRecipe",r);}
Map GeneratePlates(Scene& scene){
    const auto task=Invoke(&scene,"CamFixture","Generate",FixtureRequest(scene));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    while(std::chrono::steady_clock::now()<deadline){auto result=Invoke(&scene,"CamFixture","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
        if(result.at("status").To<Text>()=="Completed")return FixtureQueryDesign(scene).at("candidate").To<Map>();
        if(result.at("status").To<Text>()!="Running")throw std::runtime_error(result.at("error").To<Text>());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }throw std::runtime_error("Fixture generation timed out");
}
Map AdoptPlates(Scene& scene,const Map& candidate){auto r=FixtureRequest(scene);r["candidateId"]=candidate.at("id");r["supportReviewed"]=true;return Invoke(&scene,"CamFixture","Confirm",r);}
TEST(FiveAxisFixtureDesign, CurvedSheetMeshClearanceRegenerationUndoAndEmbeddedReopen) {
    Scene scene;File part(FixtureCurvedSheet());
    const auto imported=Imported(scene,part);auto recipe=SetPlateRecipe(scene,PlateRecipe(scene));
    const auto workBefore=Read(scene).at("workpieces").To<Array>().front().To<Map>();
    const auto first=GeneratePlates(scene);EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
    ASSERT_EQ(first.at("parts").To<Array>().size(),4u);
    const auto workAfter=Read(scene).at("workpieces").To<Array>().front().To<Map>();
    EXPECT_EQ(workBefore.at("geometryRef"),workAfter.at("geometryRef"));
    EXPECT_NE(workBefore.at("previewRef"),workAfter.at("previewRef"));
    auto pick=FixtureRequest(scene);pick["placementId"]=imported.at("placementId");
    const auto displayFaces=Invoke(&scene,"CamPlacement","ReadReferenceFaces",pick).at("faces");
    EXPECT_EQ(displayFaces,workAfter.at("importMetadata").To<Map>().at("displayMesh").To<Map>().at("faces"));
    for(const auto& value:first.at("parts").To<Array>()){const auto p=value.To<Map>();if(p.at("role").To<Text>()=="Base")EXPECT_GE(p.at("minimumDistance").To<double>(),.2-1e-6);else{EXPECT_LE(p.at("minimumDistance").To<double>(),2*p.at("topRetreat").To<double>()+.001);EXPECT_FALSE(p.at("contactPoints").To<Array>().empty());}}
    EXPECT_EQ(first.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");
    AdoptPlates(scene,first);auto original=Read(scene);ASSERT_EQ(original.at("fixtures").To<Array>().size(),4u);
    auto breakdown=Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));EXPECT_EQ(breakdown.at("parts").To<Array>().size(),4u);
    recipe["thickness"]=6.;SetPlateRecipe(scene,recipe);EXPECT_THROW(Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene)),std::exception);
    const auto second=GeneratePlates(scene);for(size_t i=0;i<4;++i)EXPECT_EQ(first.at("parts").To<Array>()[i].To<Map>().at("partId"),second.at("parts").To<Array>()[i].To<Map>().at("partId"));
    AdoptPlates(scene,second);EXPECT_EQ(Read(scene).at("fixtures").To<Array>().size(),4u);
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(original.at("fixtures"),Read(scene).at("fixtures"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);
    Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));
    const auto before=Read(scene);iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="夹具制造";
    auto path=part.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());std::filesystem::remove(part.Path);
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("fixtures"),Read(reopened).at("fixtures"));EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));EXPECT_EQ(before.at("workpieces"),Read(reopened).at("workpieces"));
    const auto destination=std::filesystem::current_path()/"fixture-design-acceptance";std::filesystem::create_directories(destination);
    const auto str=destination.u8string();auto request=FixtureRequest(reopened);request["destination"]=Text(reinterpret_cast<const char*>(str.data()),str.size());
    auto delivery=Invoke(&reopened,"CamFixture","Export",request);EXPECT_EQ(delivery,Invoke(&reopened,"CamFixture","Export",request));
    std::cout<<"Fixture delivery: "<<delivery.at("directory").To<Text>()<<std::endl;
    std::filesystem::remove(path);
}
TEST(FiveAxisFixtureDesign, FailureCancellationAndChangedSourceCannotReplaceAssembly) {
    Scene scene;File part(FixtureCurvedSheet());const auto imported=Imported(scene,part);auto recipe=SetPlateRecipe(scene,PlateRecipe(scene));
    const auto candidate=GeneratePlates(scene);AdoptPlates(scene,candidate);const auto before=Read(scene).at("fixtures");
    auto stations=recipe.at("stations").To<Array>();auto s=stations.back().To<Map>();s["coordinate"]=1000.;stations.back()=s;recipe["stations"]=stations;SetPlateRecipe(scene,recipe);
    EXPECT_THROW(GeneratePlates(scene),std::exception);
    EXPECT_EQ(before,Read(scene).at("fixtures"));
    EXPECT_THROW(AdoptPlates(scene,candidate),std::exception);
    auto t=Invoke(&scene,"CamFixture","Generate",FixtureRequest(scene));Invoke(&scene,"CamFixture","CancelTask",{{"epoch",t.at("epoch")},{"taskId",t.at("taskId")}});EXPECT_EQ(Wait(scene,t).at("status").To<Text>(),"Cancelled");
    auto request=FixtureRequest(scene);request["recipe"]=recipe;EXPECT_THROW(Invoke(nullptr,"CamFixture","SetRecipe",request),std::exception);
    EXPECT_EQ(before,Read(scene).at("fixtures"));
}
Text ImportedFixture(Scene& scene,const File& file)
{
    auto request=FixtureRequest(scene);request["sourcePath"]=file.Utf8();
    const auto task=Invoke(&scene,"CamFixture","Import",request);
    EXPECT_EQ(task,Invoke(&scene,"CamFixture","Import",request));
    const auto result=Wait(scene,task);
    if(result.at("status").To<Text>()!="Completed")throw std::runtime_error(result.at("error").To<Text>());
    return result.at("result").To<Map>().at("fixtureId").To<Text>();
}
Map CheckedFixtures(Scene& scene,double gap=2.)
{
    auto request=FixtureRequest(scene);request["clearanceMm"]=gap;
    const auto result=Wait(scene,Invoke(&scene,"CamFixture","Check",request));
    if(result.at("status").To<Text>()!="Completed")throw std::runtime_error(result.at("error").To<Text>());
    const auto descriptor=Invoke(&scene,"CamFixture","ReadCheckDetails",FixtureRequest(scene));return FixtureQueryDocument(scene,descriptor.at("reportResourceRef").To<Map>());
}
Array FixtureTransform(double x=0,double y=0,double z=0) { return {1.,0.,0.,x,0.,1.,0.,y,0.,0.,1.,z,0.,0.,0.,1.}; }
void MoveFixture(Scene& scene,const Text& id,const Array& transform)
{
    auto request=FixtureRequest(scene,id);request["transform"]=transform;Invoke(&scene,"CamFixture","Update",request);
}
void ExpectFixedGeneratedFixtureTransform(Scene& scene,const Text& method,const Map& request) {
    const auto before=Read(scene);const auto resources=scene.Resources().Count();const auto undo=scene.Database().GetUndoArray(),redo=scene.Database().GetRedoArray();
    try{Invoke(&scene,"CamFixture",method,request);FAIL()<<"Generated support transforms must remain fixed";}
    catch(const std::exception& error){EXPECT_NE(Text(error.what()).find("CAM_FIXTURE_GENERATED_TRANSFORM_FIXED"),Text::npos)<<error.what();}
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(resources,scene.Resources().Count());EXPECT_EQ(undo,scene.Database().GetUndoArray());EXPECT_EQ(redo,scene.Database().GetRedoArray());
}
TEST(FiveAxisFixtures, GeneratedUnitsAndIndividualPlatesRejectRigidEditsAtomicallyButAllowNames) {
    Scene scene;File part(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,part);
    SetPlateRecipe(scene,PlateRecipe(scene));AdoptPlates(scene,GeneratePlates(scene));const auto before=Read(scene),unit=before.at("fixtureUnits").To<Array>().front().To<Map>();
    ASSERT_EQ(unit.at("kind").To<Text>(),"Generated");const auto unitID=unit.at("id"),partID=unit.at("partIds").To<Array>().front();
    for(const auto& matrix:std::array<Array,3>{FixtureTransform(),FixtureTransform(12,-4,8),Array{0.,-1.,0.,0.,1.,0.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}}){
        auto move=FixtureRequest(scene);move["unitId"]=unitID;move["transform"]=matrix;move["name"]=Text("Must not rename rejected unit");ExpectFixedGeneratedFixtureTransform(scene,"UnitUpdate",move);
        auto plate=FixtureRequest(scene,partID.To<Text>());plate["transform"]=matrix;plate["name"]=Text("Must not rename rejected plate");ExpectFixedGeneratedFixtureTransform(scene,"Update",plate);
    }
    auto rename=FixtureRequest(scene);rename["unitId"]=unitID;rename["name"]=Text("固定拼装支撑架");const auto receipt=Invoke(&scene,"CamFixture","UnitUpdate",rename);EXPECT_EQ(receipt,Invoke(&scene,"CamFixture","UnitUpdate",rename));
    auto plateRename=FixtureRequest(scene,partID.To<Text>());plateRename["name"]=Text("固定支撑板");Invoke(&scene,"CamFixture","Update",plateRename);const auto named=Read(scene);
    EXPECT_EQ(named.at("fixtureUnits").To<Array>().front().To<Map>().at("name").To<Text>(),"固定拼装支撑架");EXPECT_EQ(before.at("fixtureUnits").To<Array>().front().To<Map>().at("transform"),named.at("fixtureUnits").To<Array>().front().To<Map>().at("transform"));
    bool found=false;for(const auto& value:named.at("fixtures").To<Array>()){const auto plate=value.To<Map>();for(const auto& original:before.at("fixtures").To<Array>())if(original.To<Map>().at("id")==plate.at("id"))EXPECT_EQ(original.To<Map>().at("transform"),plate.at("transform"));if(plate.at("id")==partID){found=true;EXPECT_EQ(plate.at("name").To<Text>(),"固定支撑板");}}EXPECT_TRUE(found);
    ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(before.at("fixtureUnits"),Read(scene).at("fixtureUnits"));
    ASSERT_TRUE(scene.Database().Redo());ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(named.at("fixtures"),Read(scene).at("fixtures"));EXPECT_EQ(named.at("fixtureUnits"),Read(scene).at("fixtureUnits"));
}
TEST(FiveAxisFixtures, ImportedUnitsAndPartsRemainRigidlyEditable) {
    Scene scene;File part(BRepPrimAPI_MakeBox(40,60,20).Shape()),tool(BRepPrimAPI_MakeBox(8,9,10).Shape());Imported(scene,part);const auto partID=ImportedFixture(scene,tool);
    const auto unit=Read(scene).at("fixtureUnits").To<Array>().front().To<Map>();ASSERT_EQ(unit.at("kind").To<Text>(),"Imported");
    auto move=FixtureRequest(scene);move["unitId"]=unit.at("id");const Array pose{0.,-1.,0.,12.,1.,0.,0.,24.,0.,0.,1.,36.,0.,0.,0.,1.};move["transform"]=pose;
    Invoke(&scene,"CamFixture","UnitUpdate",move);const auto moved=Read(scene);EXPECT_EQ(moved.at("fixtureUnits").To<Array>().front().To<Map>().at("transform").To<Array>(),pose);EXPECT_EQ(moved.at("fixtures").To<Array>().front().To<Map>().at("transform").To<Array>(),pose);
    const auto single=FixtureTransform(-8,4,2);MoveFixture(scene,partID,single);EXPECT_EQ(Read(scene).at("fixtures").To<Array>().front().To<Map>().at("transform").To<Array>(),single);
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(moved.at("fixtures"),Read(scene).at("fixtures"));ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(Read(scene).at("fixtures").To<Array>().front().To<Map>().at("transform").To<Array>(),single);
}
TEST(FiveAxisFixtures, MultipleFixturesStaySeparateFromWorkpieceAndUndoRedoRestoreThem)
{
    Scene scene;File part(BRepPrimAPI_MakeBox(40,60,20).Shape()),support(BRepPrimAPI_MakeBox(10,10,10).Shape());
    Imported(scene,part);const auto original=Read(scene);
    const auto a=ImportedFixture(scene,support),b=ImportedFixture(scene,support);
    EXPECT_EQ(2u,Read(scene).at("fixtures").To<Array>().size());
    EXPECT_EQ(original.at("workpieces"),Read(scene).at("workpieces"));
    EXPECT_EQ(original.at("placements"),Read(scene).at("placements"));
    // Centered workpiece spans X [-20, 20]; leave one millimetre clearance.
    MoveFixture(scene,b,FixtureTransform(21));
    auto report=CheckedFixtures(scene);
    EXPECT_EQ(3u,report.at("pairs").To<Array>().size());
    EXPECT_EQ(1ull,report.at("contactCount").To<UInt>());
    EXPECT_EQ(1ull,report.at("clearanceCount").To<UInt>());
    const auto before=Read(scene);
    Invoke(&scene,"CamFixture","Remove",FixtureRequest(scene,a));
    EXPECT_EQ(1u,Read(scene).at("fixtures").To<Array>().size());
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);
    EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);
    EXPECT_EQ(1u,Read(scene).at("fixtures").To<Array>().size());
}
TEST(FiveAxisFixtures, ExactDistancesRespectRigidTransformsVisibilityAndDisabledChecks)
{
    Scene scene;File part(BRepPrimAPI_MakeBox(40,60,20).Shape()),support(BRepPrimAPI_MakeBox(10,10,10).Shape());
    Imported(scene,part);const auto id=ImportedFixture(scene,support);
    EXPECT_EQ(1ull,CheckedFixtures(scene).at("contactCount").To<UInt>());
    MoveFixture(scene,id,FixtureTransform(21));
    auto report=CheckedFixtures(scene);
    EXPECT_EQ(1ull,report.at("clearanceCount").To<UInt>());
    EXPECT_NEAR(1.,report.at("pairs").To<Array>()[0].To<Map>().at("distanceMm").To<double>(),1e-6);
    auto hide=FixtureRequest(scene,id);hide["visible"]=false;
    const auto before=Read(scene).at("setups");Invoke(&scene,"CamFixture","Update",hide);
    EXPECT_EQ(before,Read(scene).at("setups"));
    const auto entity=scene.Database().GetEntity(*ID::from_string(id));
    auto render=entity->GetComponent("CRenderInstanceComponent");
    ASSERT_NE(nullptr,render);EXPECT_FALSE(render->GetProperty("Visible").To<bool>());
    EXPECT_EQ(1ull,CheckedFixtures(scene).at("clearanceCount").To<UInt>());
    MoveFixture(scene,id,FixtureTransform(30));
    report=CheckedFixtures(scene);
    EXPECT_EQ(0ull,report.at("contactCount").To<UInt>());EXPECT_EQ(0ull,report.at("clearanceCount").To<UInt>());
    EXPECT_NEAR(10.,report.at("pairs").To<Array>()[0].To<Map>().at("distanceMm").To<double>(),1e-6);
    MoveFixture(scene,id,{0.,-1.,0.,30.,1.,0.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.});
    EXPECT_EQ(1ull,CheckedFixtures(scene).at("contactCount").To<UInt>());
    auto disable=FixtureRequest(scene,id);disable["collisionEnabled"]=false;Invoke(&scene,"CamFixture","Update",disable);
    EXPECT_THROW(CheckedFixtures(scene),std::exception);
}
TEST(FiveAxisFixtures, CheckIsInvalidatedByWorkpieceMoveAndSurvivesReopenWithoutSources)
{
    Scene scene;File part(BRepPrimAPI_MakeBox(40,60,20).Shape()),support(BRepPrimAPI_MakeBox(10,10,10).Shape());
    const auto ids=Imported(scene,part);const auto id=ImportedFixture(scene,support);
    MoveFixture(scene,id,FixtureTransform(50));const auto report=CheckedFixtures(scene);
    auto placement=PlacementRequest(scene,ids);placement["transform"]=FixtureTransform(100);Invoke(&scene,"CamPlacement","SetTransform",placement);
    auto setup=Read(scene).at("setups").To<Array>()[0].To<Map>();
    EXPECT_NE(setup.at("inputRevision"),setup.at("fixtureCheck").To<Map>().at("inputRevision"));
    const auto current=CheckedFixtures(scene);
    EXPECT_NEAR(40.,current.at("pairs").To<Array>()[0].To<Map>().at("distanceMm").To<double>(),1e-6);
    const auto before=Read(scene);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="装夹";
    auto output=part.Path;output.replace_extension(".i5cam");file.Save(output,info,scene.Database(),scene.Resources());
    std::filesystem::remove(part.Path);std::filesystem::remove(support.Path);
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(output,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("fixtures"),Read(reopened).at("fixtures"));EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));
    EXPECT_EQ(current,CheckedFixtures(reopened));std::filesystem::remove(output);
}
TEST(FiveAxisFixtures, RejectsStaleWrongChannelAndInvalidMatricesAndRecoversLostReplies)
{
    Scene scene;File part,support(BRepPrimAPI_MakeBox(10,10,10).Shape());Imported(scene,part);const auto id=ImportedFixture(scene,support);
    auto request=FixtureRequest(scene,id);request["transform"]=FixtureTransform(40);
    const auto result=Invoke(&scene,"CamFixture","Update",request),after=Read(scene);
    EXPECT_EQ(result,Invoke(&scene,"CamFixture","Update",request));EXPECT_EQ(after,Read(scene));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    EXPECT_THROW(Invoke(&scene,"CamFixture","Update",request),std::exception);
    request=FixtureRequest(scene,id);auto matrix=FixtureTransform();matrix[0]=2.;request["transform"]=matrix;
    EXPECT_THROW(Invoke(&scene,"CamFixture","Update",request),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamFixture","Update",request),std::exception);
    EXPECT_EQ(after,Read(scene));
    request=FixtureRequest(scene);request["clearanceMm"]=2;
    const auto task=Invoke(&scene,"CamFixture","Check",request);
    auto update=FixtureRequest(scene,id);update["transform"]=FixtureTransform(90);Invoke(&scene,"CamFixture","Update",update);
    EXPECT_EQ("Superseded",Wait(scene,task).at("status").To<Text>());
    EXPECT_TRUE(Read(scene).at("setups").To<Array>()[0].To<Map>().at("fixtureCheck").To<Map>().empty());
    request=FixtureRequest(scene);request["clearanceMm"]=2;
    const auto cancel=Invoke(&scene,"CamFixture","Check",request);
    Invoke(&scene,"CamFixture","CancelTask",{{"epoch",cancel.at("epoch")},{"taskId",cancel.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,cancel).at("status").To<Text>());
}
TEST(FiveAxisFixtures, UndoThenNewBranchCannotReuseReportForDifferentGeometry)
{
    Scene scene;File part(BRepPrimAPI_MakeBox(40,60,20).Shape()),support(BRepPrimAPI_MakeBox(10,10,10).Shape());
    Imported(scene,part);const auto id=ImportedFixture(scene,support);
    MoveFixture(scene,id,FixtureTransform(21));CheckedFixtures(scene);
    MoveFixture(scene,id,FixtureTransform(60));CheckedFixtures(scene);
    const auto original=Read(scene).at("setups").To<Array>()[0].To<Map>();
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);
    EXPECT_EQ("Stale",Read(scene).at("setups").To<Array>()[0].To<Map>().at("fixtureCheck").To<Map>().at("validity").To<Text>());
    const auto stale=Invoke(&scene,"CamFixture","ReadCheckDetails",FixtureRequest(scene));
    EXPECT_EQ("Stale",FixtureQueryDocument(scene,stale.at("reportResourceRef").To<Map>()).at("validity").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);
    EXPECT_EQ("Current",Read(scene).at("setups").To<Array>()[0].To<Map>().at("fixtureCheck").To<Map>().at("validity").To<Text>());
    MoveFixture(scene,id,FixtureTransform(70));
    const auto branched=Read(scene).at("setups").To<Array>()[0].To<Map>();
    EXPECT_EQ(original.at("inputRevision"),branched.at("inputRevision"));
    EXPECT_EQ("Stale",branched.at("fixtureCheck").To<Map>().at("validity").To<Text>());
    const auto report=CheckedFixtures(scene);
    EXPECT_NEAR(50.,report.at("pairs").To<Array>()[0].To<Map>().at("distanceMm").To<double>(),1e-6);
}
TEST(FiveAxisFixtureDesign, TiltedPlaneAnalyticDistanceChecksFullThicknessInBothDirections) {
    BRepBuilderAPI_MakePolygon boundary;
    for(auto xy:std::array<std::array<double,2>,4>{{{-50,-60},{50,-60},{50,60},{-50,60}}})boundary.Add(gp_Pnt(xy[0],xy[1],.3*xy[0]+.4*xy[1]));
    boundary.Close();Scene scene;File file(BRepBuilderAPI_MakeFace(boundary.Wire()).Face());Imported(scene,file);
    auto recipe=PlateRecipe(scene);recipe["thickness"]=10.;recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",0.}}};
    for(const auto& family:std::array<Text,2>{"ParallelX","ParallelY"}){
        recipe["family"]=family;recipe=SetPlateRecipe(scene,recipe);auto result=GeneratePlates(scene),plate=result.at("parts").To<Array>()[0].To<Map>();
        const auto frame=plate.at("frame").To<Array>();for(auto q:plate.at("outline").To<Array>()){auto p=q.To<Array>();if(p[1].To<double>()<10)continue;double expected=(family=="ParallelX"?.4:.3)*p[0].To<double>()-(family=="ParallelX"?.3:.4)*5;EXPECT_NEAR(frame[11].To<double>()+p[1].To<double>(),expected,.0001);}
        EXPECT_LE(plate.at("minimumDistance").To<double>(),.001);
        for(const auto& value:plate.at("contactPoints").To<Array>()){auto p=value.To<Array>();EXPECT_NEAR(p[2].To<double>(),.3*p[0].To<double>()+.4*p[1].To<double>(),1e-6);}
        EXPECT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Insufficient");EXPECT_THROW(AdoptPlates(scene,result),std::exception);
        EXPECT_EQ(plate.at("clearanceAllowanceMm").To<double>(),0.);
        EXPECT_EQ(plate.at("clearanceMethod").To<Text>(),"UnderlyingSurfaceEnvelope");
    }
}
TEST(FiveAxisFixtureDesign, RejectsUpperFaceObstructionAndOverlappingStations) {
    Scene scene;File file(BRepPrimAPI_MakeBox(100,120,20).Shape());Imported(scene,file);
    auto recipe=PlateRecipe(scene);auto state=Read(scene),ref=state.at("workpieces").To<Array>()[0].To<Map>().at("geometryRef").To<Map>();
    auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);Array top;
    for(const auto& [id,face]:built.Faces){Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);double a,b,c,d,e,f;bounds.Get(a,b,c,d,e,f);if(std::abs(c-20)<1e-5&&std::abs(f-20)<1e-5)top.emplace_back(std::to_string(id));}
    ASSERT_EQ(top.size(),1u);recipe["supportFaces"]=top;recipe=SetPlateRecipe(scene,recipe);
    EXPECT_THROW(GeneratePlates(scene),std::exception);EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
    auto stations=recipe.at("stations").To<Array>();auto middle=stations[1].To<Map>();middle["coordinate"]=-19.;stations[1]=middle;recipe["stations"]=stations;
    EXPECT_THROW(SetPlateRecipe(scene,recipe),std::exception);EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
}
TEST(FiveAxisFixtureDesign, PerforatedCoverOriginalFileCreatesConnectedRelievedSupport) {
    auto root=std::filesystem::current_path();
    const auto relative=std::filesystem::path(L"TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){auto parent=root.parent_path();if(parent==root)break;root=parent;}
    ASSERT_TRUE(std::filesystem::exists(root/relative))<<"Original perforated cover is required";
    Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();
    request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());
    const auto task=Invoke(&scene,"CamWorkpiece","Import",request);const auto imported=Wait(scene,task);
    ASSERT_EQ(imported.at("status").To<Text>(),"Completed");AddSetup(scene,imported.at("result").To<Map>());
    auto recipe=PlateRecipe(scene);
    recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",0.}}};
    auto state=Read(scene),ref=state.at("workpieces").To<Array>()[0].To<Map>().at("geometryRef").To<Map>();
    auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_EQ(geometry->Faces.size(),4u);
    recipe=SetPlateRecipe(scene,recipe);
    const auto result=GeneratePlates(scene);ASSERT_EQ(result.at("parts").To<Array>().size(),2u);
    // The surface ceiling continues across the zero seam and inner slots.
    const auto support=result.at("parts").To<Array>()[0].To<Map>();
    EXPECT_EQ(support.at("clearanceMethod").To<Text>(),"UnderlyingSurfaceEnvelope");
    for(const auto& value:result.at("parts").To<Array>()){
        const auto part=value.To<Map>(),partRef=part.at("geometryRef").To<Map>();
        auto plateGeometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(partRef.at("url").To<Text>(),partRef.at("version").To<UInt>());
        auto plate=iCAX::OpenCascade::BuildOpenCascadeShape(*plateGeometry);ASSERT_TRUE(plate.bOK);EXPECT_TRUE(BRepCheck_Analyzer(plate.Shape).IsValid());
        int count=0;for(TopExp_Explorer e(plate.Shape,TopAbs_SOLID);e.More();e.Next())++count;EXPECT_EQ(count,1);
    }
    EXPECT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Insufficient");EXPECT_THROW(AdoptPlates(scene,result),std::exception);
    EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
    for(const auto& value:result.at("parts").To<Array>()){const auto p=value.To<Map>();if(p.at("role").To<Text>()=="Support")EXPECT_LE(p.at("minimumDistance").To<double>(),2*p.at("topRetreat").To<double>()+.001);}
    const auto accepted=Read(scene).at("fixtures");
    recipe["family"]=Text("ParallelY");recipe=SetPlateRecipe(scene,recipe);
    const auto across=GeneratePlates(scene);EXPECT_EQ(across.at("parts").To<Array>()[0].To<Map>().at("clearanceMethod").To<Text>(),"UnderlyingSurfaceEnvelope");
    EXPECT_EQ(Read(scene).at("fixtures"),accepted);
}
TEST(FiveAxisFixtureDesign, DisconnectedSupportsKeepAWebAndDoNotBridgeOpeningsAtContactHeight) {
    BRep_Builder builder;TopoDS_Compound shape;builder.MakeCompound(shape);
    const auto face=[&](double first,double last,double z){BRepBuilderAPI_MakePolygon p;for(auto xy:std::array<std::array<double,2>,4>{{{-50,first},{50,first},{50,last},{-50,last}}})p.Add(gp_Pnt(xy[0],xy[1],z));p.Close();return BRepBuilderAPI_MakeFace(p.Wire()).Face();};
    builder.Add(shape,face(-60,-10,0));builder.Add(shape,face(10,60,0));
    // A coincident source seam and an overlapping upper branch must not lift
    // the reachable underside of the support.
    builder.Add(shape,face(-60,-10,0));builder.Add(shape,face(-50,-20,10));
    Scene scene;File file(shape);Imported(scene,file);auto recipe=PlateRecipe(scene);
    recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",0.}}};recipe=SetPlateRecipe(scene,recipe);
    const auto result=GeneratePlates(scene),part=result.at("parts").To<Array>()[0].To<Map>(),ref=part.at("geometryRef").To<Map>();
    EXPECT_EQ(part.at("reliefCount").To<UInt>(),1u);
    auto plate=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),ref.at("version").To<UInt>());
    auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*plate);ASSERT_TRUE(built.bOK);
    const auto stateAt=[&](double y,double z){BRepClass3d_SolidClassifier classifier(built.Shape);classifier.Perform(gp_Pnt(0,y,z),1e-7);return classifier.State();};
    EXPECT_EQ(stateAt(0,-20),TopAbs_OUT); // opening has no raised plate material
    EXPECT_EQ(stateAt(0,-42),TopAbs_IN); // five millimetre connecting web
    EXPECT_EQ(stateAt(-30,-6),TopAbs_IN);EXPECT_EQ(stateAt(30,-6),TopAbs_IN);
    EXPECT_EQ(stateAt(-30,-4),TopAbs_OUT); // upper branch never becomes support
    EXPECT_NEAR(part.at("minimumDistance").To<double>(),0.,.001);
}
TEST(FiveAxisFixtureDesign, PrintingStylePatternsFitOriginalCoverAndRespectBaseFootprint) {
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(L"TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));
    Scene scene;auto request=FixtureRequest(scene);auto source=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(source.data()),source.size());
    auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request));ASSERT_EQ(imported.at("status").To<Text>(),"Completed");AddSetup(scene,imported.at("result").To<Map>());
    auto recipe=PlateRecipe(scene);recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Grid");recipe["sizing"]=Text("Manual");recipe["areaWidth"]=240.;recipe["areaDepth"]=240.;
    for(const auto& pattern:std::array<Text,3>{"Grid","Staggered","Parallel"}){
        recipe["pattern"]=pattern;recipe["stations"]=Array();recipe["baseShape"]=Text(pattern=="Staggered"?"Circle":"Rectangle");recipe=SetPlateRecipe(scene,recipe);
        const auto result=GeneratePlates(scene);const auto parts=result.at("parts").To<Array>();ASSERT_GT(parts.size(),3u);auto check=result.at("supportCheck").To<Map>();
        EXPECT_EQ(check.at("status").To<Text>(),"Supported")<<pattern;EXPECT_GT(check.at("marginMm").To<double>(),3.);
        bool x=false,y=false;for(const auto& v:parts){auto p=v.To<Map>();if(p.at("role").To<Text>()=="Base")continue;
            EXPECT_LE(p.at("minimumDistance").To<double>(),2*p.at("topRetreat").To<double>()+.001);EXPECT_FALSE(p.at("contactPoints").To<Array>().empty());auto frame=p.at("frame").To<Array>();x|=frame[2].To<double>()>.5;y|=frame[6].To<double>()<-.5;
            for(auto q:p.at("contactPoints").To<Array>()){auto point=q.To<Array>();EXPECT_LE(std::abs(point[0].To<double>()),120.);EXPECT_LE(std::abs(point[1].To<double>()),120.);if(pattern=="Staggered")EXPECT_LE(std::hypot(point[0].To<double>(),point[1].To<double>()),105.001);}
        }EXPECT_TRUE(x);if(pattern=="Grid")EXPECT_TRUE(y);
        if(pattern=="Grid"){
            struct Footprint {double X0=1e100,Y0=1e100,X1=-1e100,Y1=-1e100;};std::vector<Footprint> footprints;
            for(const auto& value:parts){const auto p=value.To<Map>();if(p.at("role").To<Text>()=="Base")continue;auto frame=p.at("frame").To<Array>();Footprint footprint;
                for(auto v:p.at("outline").To<Array>())for(double z:{0.,p.at("thickness").To<double>()}){auto q=v.To<Array>();double x=frame[0].To<double>()*q[0].To<double>()+frame[1].To<double>()*q[1].To<double>()+frame[2].To<double>()*z+frame[3].To<double>(),y=frame[4].To<double>()*q[0].To<double>()+frame[5].To<double>()*q[1].To<double>()+frame[6].To<double>()*z+frame[7].To<double>();footprint.X0=std::min(footprint.X0,x);footprint.X1=std::max(footprint.X1,x);footprint.Y0=std::min(footprint.Y0,y);footprint.Y1=std::max(footprint.Y1,y);}footprints.push_back(footprint);
                const auto ref=p.at("geometryRef").To<Map>();auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),ref.at("version").To<UInt>());auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);
                // Independent CAD contact-to-plate check with recorded chord retreat. A solid classifier's
                // ON flag is sensitive to sub-tolerance rounding at cap edges.
                const auto contacts=p.at("contactPoints").To<Array>();if(footprints.size()==1||footprints.size()==parts.size()/2||footprints.size()==parts.size()-1)for(size_t index:std::array<size_t,3>{0,contacts.size()/2,contacts.size()-1}){auto q=contacts[index].To<Array>();BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(q[0].To<double>(),q[1].To<double>(),q[2].To<double>())).Shape(),built.Shape);ASSERT_TRUE(distance.IsDone());EXPECT_LE(distance.Value(),2*p.at("topRetreat").To<double>()+.00005)<<p.at("number").To<Text>();}
            }
            for(size_t a=0;a<footprints.size();++a)for(size_t b=a+1;b<footprints.size();++b){const auto p=footprints[a],q=footprints[b];EXPECT_TRUE(std::min(p.X1,q.X1)-std::max(p.X0,q.X0)<=1e-5||std::min(p.Y1,q.Y1)-std::max(p.Y0,q.Y0)<=1e-5)<<"Grid members interpenetrate";}
        }
        const auto base=parts.back().To<Map>();double minx=1e100,maxx=-1e100;for(auto q:base.at("outline").To<Array>()){auto p=q.To<Array>();minx=std::min(minx,p[0].To<double>());maxx=std::max(maxx,p[0].To<double>());}EXPECT_NEAR(minx,-120.,1e-5);EXPECT_NEAR(maxx,120.,1e-5);
        AdoptPlates(scene,result);Invoke(&scene,"CamFixture","Breakdown",FixtureRequest(scene));EXPECT_EQ(Read(scene).at("fixtures").To<Array>().size(),parts.size());
        RecordProperty(pattern+"_parts",static_cast<int>(parts.size()));
    }
    recipe["stations"]=Array();recipe["areaX"]=1000.;EXPECT_THROW(SetPlateRecipe(scene,recipe),std::exception);
}
TEST(FiveAxisFixtureDesign, NoToolpathGenerationReservesOpeningsInSupportsAndKeepsSolidBase) {
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,18,-10),gp_Dir(0,0,1)),8,20).Shape();
    BRepAlgoAPI_Cut perforated(face,hole);ASSERT_TRUE(perforated.IsDone());ASSERT_TRUE(BRepCheck_Analyzer(perforated.Shape()).IsValid());
    Scene scene;File part(perforated.Shape());const auto imported=Imported(scene,part);ASSERT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
    const auto originalGeometry=Read(scene).at("workpieces").To<Array>().front().To<Map>().at("geometryRef");
    auto recipe=PlateRecipe(scene);recipe["baseDrop"]=100.;recipe["sizing"]=Text("Manual");recipe["areaWidth"]=180.;recipe["areaDepth"]=180.;
    recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe["cutClearanceMode"]=Text("None");
    Array stations;for(double x:{-35.,0.,35.})stations.emplace_back(Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",x}});recipe["stations"]=stations;
    SetPlateRecipe(scene,recipe);const auto result=GeneratePlates(scene),opening=result.at("openingPreclearance").To<Map>(),cut=result.at("cutClearance").To<Map>();
    EXPECT_EQ(opening.at("mode").To<Text>(),"ExistingOpeningBoundaries");EXPECT_EQ(opening.at("status").To<Text>(),"Prepared");EXPECT_EQ(opening.at("validity").To<Text>(),"Current");
    EXPECT_EQ(opening.at("basis").To<Text>(),"ConservativeSupportDirectionBoundaryEnvelope");EXPECT_EQ(opening.at("boundaryCount").To<UInt>(),1u);
    EXPECT_EQ(opening.at("unclassifiedFaceCount").To<UInt>(),0u);EXPECT_EQ(opening.at("nonExitBoundaryCount").To<UInt>(),0u);EXPECT_DOUBLE_EQ(opening.at("marginMm").To<double>(),1.);
    EXPECT_DOUBLE_EQ(opening.at("planarityToleranceMm").To<double>(),1e-7);EXPECT_DOUBLE_EQ(opening.at("maximumPlaneDeviationUpperMm").To<double>(),0.);
    EXPECT_FALSE(opening.at("toolpathRequired").To<bool>());EXPECT_EQ(opening.at("toolpathStatus").To<Text>(),"NotChecked");EXPECT_FALSE(opening.at("productionQualified").To<bool>());
    EXPECT_EQ(cut.at("mode").To<Text>(),"None");EXPECT_EQ(cut.at("status").To<Text>(),"Unchecked");EXPECT_FALSE(cut.at("productionQualified").To<bool>());
    // Independent analytic bound of the exact source circle plus 1 mm. Check
    // published native solids through their whole thickness, not hole counts.
    const auto corridor=BRepPrimAPI_MakeBox(gp_Pnt(-9,9,-105),18,18,106).Shape();
    size_t supportWindows=0,baseWindows=0,relievedSupports=0;
    const auto parts=result.at("parts").To<Array>();ASSERT_GE(parts.size(),3u);
    for(const auto& value:parts){const auto p=value.To<Map>(),ref=p.at("geometryRef").To<Map>();
        const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),ref.at("version").To<UInt>());
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);EXPECT_TRUE(BRepCheck_Analyzer(built.Shape).IsValid());
        size_t bodies=0;for(TopExp_Explorer explorer(built.Shape,TopAbs_SOLID);explorer.More();explorer.Next())++bodies;EXPECT_EQ(bodies,1u);
        BRepAlgoAPI_Common common(built.Shape,corridor);ASSERT_TRUE(common.IsDone());GProp_GProps overlap;BRepGProp::VolumeProperties(common.Shape(),overlap);if(p.at("role").To<Text>()=="Base")EXPECT_GT(std::abs(overlap.Mass()),1e-6);else EXPECT_LT(std::abs(overlap.Mass()),1e-6);
        if(p.at("role").To<Text>()=="Base")baseWindows=p.at("holes").To<Array>().size();
        else {supportWindows+=p.at("holes").To<Array>().size();if(p.at("keepOutReliefCount").To<UInt>())++relievedSupports;EXPECT_FALSE(p.at("contactPoints").To<Array>().empty());}
    }
    EXPECT_EQ(baseWindows,0u);EXPECT_GE(supportWindows,2u);EXPECT_GT(relievedSupports,0u);
    EXPECT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");
    EXPECT_EQ(originalGeometry,Read(scene).at("workpieces").To<Array>().front().To<Map>().at("geometryRef"));EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
    AdoptPlates(scene,result);
    const auto currentOpening=[&]{return Read(scene).at("fixtureUnits").To<Array>().front().To<Map>().at("design").To<Map>().at("assembly").To<Map>().at("openingPreclearance").To<Map>();};
    EXPECT_EQ(currentOpening().at("validity").To<Text>(),"Current");
    auto move=PlacementRequest(scene,imported);move["transform"]=FixtureTransform(1);Invoke(&scene,"CamPlacement","SetTransform",move);
    EXPECT_EQ(currentOpening().at("validity").To<Text>(),"Stale");EXPECT_EQ(currentOpening().at("status").To<Text>(),"Unchecked");
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(currentOpening().at("validity").To<Text>(),"Current");EXPECT_EQ(currentOpening().at("status").To<Text>(),"Prepared");
    auto unitMove=FixtureRequest(scene);unitMove["unitId"]=Read(scene).at("fixtureUnits").To<Array>().front().To<Map>().at("id");unitMove["transform"]=FixtureTransform(3);
    ExpectFixedGeneratedFixtureTransform(scene,"UnitUpdate",unitMove);EXPECT_EQ(currentOpening().at("validity").To<Text>(),"Current");EXPECT_EQ(currentOpening().at("status").To<Text>(),"Prepared");const auto before=Read(scene);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="无刀路孔界预避让";
    auto path=part.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));EXPECT_EQ(before.at("fixtureUnits"),Read(reopened).at("fixtureUnits"));EXPECT_EQ(before.at("fixtures"),Read(reopened).at("fixtures"));
    EXPECT_TRUE(Read(reopened).at("intentToolpaths").To<Array>().empty());std::filesystem::remove(path);
    RecordProperty("existing_opening_boundaries",1);RecordProperty("base_inner_loops",static_cast<int>(baseWindows));RecordProperty("support_windows",static_cast<int>(supportWindows));RecordProperty("relieved_supports",static_cast<int>(relievedSupports));
}
TEST(FiveAxisFixtureDesign, OriginalWallLampSTEPReservesRoundOpeningsBeforeAnyToolpath) {
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(L"TestData/FA/壁灯盘 挂板.STEP");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));
    Scene scene;auto request=FixtureRequest(scene);const auto source=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(source.data()),source.size());
    const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request));ASSERT_EQ(imported.at("status").To<Text>(),"Completed");AddSetup(scene,imported.at("result").To<Map>());
    const auto state=Read(scene);ASSERT_TRUE(state.at("intentToolpaths").To<Array>().empty());const auto work=state.at("workpieces").To<Array>().front().To<Map>(),ref=work.at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);
    const auto matrix=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();gp_Trsf transform;
    transform.SetValues(matrix[0].To<double>(),matrix[1].To<double>(),matrix[2].To<double>(),matrix[3].To<double>(),matrix[4].To<double>(),matrix[5].To<double>(),matrix[6].To<double>(),matrix[7].To<double>(),matrix[8].To<double>(),matrix[9].To<double>(),matrix[10].To<double>(),matrix[11].To<double>());
    Bnd_Box workBounds;BRepBndLib::AddOptimal(built.Shape.Moved(TopLoc_Location(transform)),workBounds,false,false);double wx0,wy0,wz0,wx1,wy1,wz1;workBounds.Get(wx0,wy0,wz0,wx1,wy1,wz1);
    struct RoundOpening {double X0,Y0,X1,Y1,Top;std::uint64_t FaceID;};std::vector<RoundOpening> roundOpenings;size_t horizontalInnerWires=0,largeInnerWires=0;
    for(const auto& [id,sourceFace]:built.Faces){const auto face=TopoDS::Face(sourceFace.Moved(TopLoc_Location(transform)));BRepAdaptor_Surface surface(face);
        bool horizontal=surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().Z())>1-1e-10;
        if(surface.GetType()==GeomAbs_BSplineSurface){const auto spline=surface.BSpline();double first=1e100,last=-1e100;bool positive=true;
            for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){const auto point=spline->Pole(u,v);first=std::min(first,point.Z());last=std::max(last,point.Z());positive&=spline->Weight(u,v)>0;}horizontal=positive&&last-first<1e-7;}
        if(!horizontal)continue;const auto outer=BRepTools::OuterWire(face);
        for(TopExp_Explorer explorer(face,TopAbs_WIRE);explorer.More();explorer.Next()){const auto wire=TopoDS::Wire(explorer.Current());if(wire.IsSame(outer))continue;
            Bnd_Box box;BRepBndLib::AddOptimal(wire,box,false,true);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);++horizontalInnerWires;
            // These are independently known dimensions of this original test
            // file, not production hole classification or a fixture filter.
            const double width=x1-x0,depth=y1-y0;
            if((std::abs(width-9.3)<.001||std::abs(width-10.5)<.001)&&std::abs(width-depth)<.0001)roundOpenings.push_back({x0,y0,x1,y1,z1,id});
            else if(width>100&&depth>100)++largeInnerWires;
        }
    }
    size_t sourceSolids=0;for(TopExp_Explorer explorer(built.Shape,TopAbs_SOLID);explorer.More();explorer.Next())++sourceSolids;
    std::cout<<"Wall lamp source faces="<<geometry->Faces.size()<<" solids="<<sourceSolids<<" horizontal inner wires="<<horizontalInnerWires<<" large inner boundaries="<<largeInnerWires<<" workpiece z=["<<wz0<<','<<wz1<<"]"<<std::endl;
    for(const auto& hole:roundOpenings)std::cout<<"Wall lamp round opening face="<<hole.FaceID<<" bounds=["<<hole.X0<<','<<hole.Y0<<','<<hole.X1<<','<<hole.Y1<<"] top="<<hole.Top<<std::endl;
    std::vector<RoundOpening> distinctOpenings;for(const auto& hole:roundOpenings){bool duplicate=false;for(const auto& prior:distinctOpenings)if(std::abs(hole.X0-prior.X0)<.0001&&std::abs(hole.Y0-prior.Y0)<.0001&&std::abs(hole.X1-prior.X1)<.0001&&std::abs(hole.Y1-prior.Y1)<.0001)duplicate=true;if(!duplicate)distinctOpenings.push_back(hole);}
    ASSERT_EQ(distinctOpenings.size(),3u)<<"The imported authoritative BRep must retain all three distinct original round holes";ASSERT_GT(largeInnerWires,0u)<<"The original bowl/lip inner boundary must remain in the tested workpiece";
    auto recipe=PlateRecipe(scene);recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Grid");recipe["stations"]=Array();recipe["jointMode"]=Text("TabSlot");recipe["spacing"]=35.;recipe["baseDrop"]=40.;recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe["cutClearanceMode"]=Text("None");
    RecordProperty("joint_mode","TabSlot");RecordProperty("spacing_mm",35);RecordProperty("base_drop_mm",40);RecordProperty("window_web_mm",8);
    recipe=SetPlateRecipe(scene,recipe);const auto result=GeneratePlates(scene),opening=result.at("openingPreclearance").To<Map>();const auto parts=result.at("parts").To<Array>();
    EXPECT_GE(opening.at("boundaryCount").To<UInt>(),3u);EXPECT_GE(opening.at("nonExitBoundaryCount").To<UInt>(),4u);EXPECT_EQ(opening.at("status").To<Text>(),opening.at("unclassifiedFaceCount").To<UInt>()?"Partial":"Prepared");
    EXPECT_LE(opening.at("maximumPlaneDeviationUpperMm").To<double>(),opening.at("planarityToleranceMm").To<double>());
    EXPECT_EQ(opening.at("toolpathStatus").To<Text>(),"NotChecked");EXPECT_FALSE(opening.at("productionQualified").To<bool>());EXPECT_EQ(result.at("cutClearance").To<Map>().at("status").To<Text>(),"Unchecked");
    const double bottom=wz0-106;std::vector<TopoDS_Shape> corridors;for(const auto& hole:roundOpenings)corridors.push_back(BRepPrimAPI_MakeBox(gp_Pnt(hole.X0-1,hole.Y0-1,bottom),hole.X1-hole.X0+2,hole.Y1-hole.Y0+2,hole.Top+1-bottom).Shape());
    size_t relieved=0,supportWindows=0,baseWindows=0;ASSERT_GT(parts.size(),3u);
    for(const auto& value:parts){const auto p=value.To<Map>(),plateRef=p.at("geometryRef").To<Map>();const auto plate=scene.Resources().Get<iCAX::GeometryData::BRepModel>(plateRef.at("url").To<Text>(),plateRef.at("version").To<UInt>());
        const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*plate);ASSERT_TRUE(shape.bOK);EXPECT_TRUE(BRepCheck_Analyzer(shape.Shape).IsValid());
        size_t bodies=0;for(TopExp_Explorer explorer(shape.Shape,TopAbs_SOLID);explorer.More();explorer.Next())++bodies;EXPECT_EQ(bodies,1u);
        for(const auto& corridor:corridors){BRepAlgoAPI_Common common(shape.Shape,corridor);ASSERT_TRUE(common.IsDone());GProp_GProps overlap;BRepGProp::VolumeProperties(common.Shape(),overlap);EXPECT_LT(std::abs(overlap.Mass()),1e-6)<<p.at("number").To<Text>();}
        if(p.at("role").To<Text>()=="Base")baseWindows=p.at("holes").To<Array>().size();else {supportWindows+=p.at("holes").To<Array>().size();if(p.at("keepOutReliefCount").To<UInt>())++relieved;EXPECT_FALSE(p.at("contactPoints").To<Array>().empty());}
    }
    EXPECT_EQ(baseWindows,0u);EXPECT_GE(supportWindows,2u);EXPECT_EQ(result.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");AdoptPlates(scene,result);EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
    RecordProperty("round_opening_corridors",static_cast<int>(corridors.size()));RecordProperty("source_large_inner_boundaries",static_cast<int>(largeInnerWires));
    RecordProperty("opening_boundaries",std::to_string(opening.at("boundaryCount").To<UInt>()));RecordProperty("nonexit_boundaries",std::to_string(opening.at("nonExitBoundaryCount").To<UInt>()));RecordProperty("unclassified_faces",std::to_string(opening.at("unclassifiedFaceCount").To<UInt>()));RecordProperty("base_inner_loops",static_cast<int>(baseWindows));RecordProperty("support_windows",static_cast<int>(supportWindows));RecordProperty("relieved_supports",static_cast<int>(relieved));
}
TEST(FiveAxisFixtureDesign, OriginalWallLampSingleBearingFaceAfterOwnFrameResetKeepsDistributedSupport) {
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(L"TestData/FA/壁灯盘 挂板.STEP");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));
    Scene scene;auto request=FixtureRequest(scene);const auto source=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(source.data()),source.size());
    const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request));ASSERT_EQ(imported.at("status").To<Text>(),"Completed");AddSetup(scene,imported.at("result").To<Map>());
    const auto placementID=Read(scene).at("placements").To<Array>().front().To<Map>().at("id");
    // This is the actual alignment specification used before the reported
    // failure. Resetting the object frame rebases the BRep, so merely setting
    // an identity placement would not reproduce the selected bearing face.
    const Map plane{{"featureId",Text("bounds:OBB:face:0:1")},{"frame",Text("Workpiece")},{"mode",Text("plane")}};
    const Map specification{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},
        {"steps",Array{
            Map{{"flip",true},{"kind",Text("plane")},{"projectToPlane",false},{"source",plane},{"target",Map{{"axis",Text("Z")}}}},
            Map{{"flip",false},{"kind",Text("direction")},{"projectToPlane",true},{"source",Map{{"construction",Map{{"kind",Text("PlaneU")},{"sources",Array{plane}}}},{"frame",Text("Workpiece")},{"mode",Text("direction")}}},{"target",Map{{"axis",Text("X")}}}},
            Map{{"flip",false},{"kind",Text("point")},{"projectToPlane",false},{"source",Map{{"construction",Map{{"kind",Text("PlaneCenter")},{"sources",Array{plane}}}},{"frame",Text("Workpiece")},{"mode",Text("datum-point")}}},{"target",Map{{"point",Text("origin")}}}}
        }}};
    auto align=FixtureRequest(scene);align["placementId"]=placementID;align["resetWorkpieceFrame"]=true;align["registration"]=specification;
    const auto aligned=Invoke(&scene,"CamPlacement","ApplyRegistration",align);EXPECT_TRUE(aligned.at("report").To<Map>().at("resetWorkpieceFrame").To<bool>());
    const auto before=Read(scene);EXPECT_EQ((Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}),before.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>());
    const auto workBefore=before.at("workpieces").To<Array>().front().To<Map>(),geometryRef=workBefore.at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(geometryRef.at("url").To<Text>(),std::stoull(geometryRef.at("version").To<Text>()));ASSERT_TRUE(geometry);
    const auto geometryBefore=iCAX::GeometryData::Persistence::Serialize(*geometry);const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);ASSERT_TRUE(built.Faces.contains(16));
    const auto printBounds=[](const char* name,const TopoDS_Shape& shape){Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);std::cout<<name<<" bounds=["<<x0<<','<<y0<<','<<z0<<';'<<x1<<','<<y1<<','<<z1<<']'<<std::endl;};
    printBounds("Wall lamp aligned workpiece",built.Shape);printBounds("Wall lamp selected face 16",built.Faces.at(16));
    BRepAdaptor_Surface selectedSurface(TopoDS::Face(built.Faces.at(16)));GProp_GProps properties;BRepGProp::SurfaceProperties(built.Faces.at(16),properties);const auto center=properties.CentreOfMass();
    std::cout<<"Wall lamp selected face 16 surfaceType="<<selectedSurface.GetType()<<" area="<<properties.Mass()<<" center=["<<center.X()<<','<<center.Y()<<','<<center.Z()<<']';
    if(selectedSurface.GetType()==GeomAbs_Plane){const auto normal=selectedSurface.Plane().Axis().Direction();std::cout<<" nativeNormal=["<<normal.X()<<','<<normal.Y()<<','<<normal.Z()<<']';}std::cout<<std::endl;
    Bnd_Box workBox;BRepBndLib::AddOptimal(built.Shape,workBox,false,false);double wx0,wy0,wz0,wx1,wy1,wz1;workBox.Get(wx0,wy0,wz0,wx1,wy1,wz1);
    struct SmallOpening {double X0,Y0,X1,Y1,Top;};std::vector<SmallOpening> smallOpenings;size_t largeInnerWires=0,backedLargeInnerWires=0;
    for(const auto& [faceID,sourceFace]:built.Faces){const auto face=TopoDS::Face(sourceFace);const auto outer=BRepTools::OuterWire(face);BRepAdaptor_Surface surface(face);
        for(TopExp_Explorer explorer(face,TopAbs_WIRE);explorer.More();explorer.Next()){const auto wire=TopoDS::Wire(explorer.Current());if(wire.IsSame(outer))continue;
            Bnd_Box box;BRepBndLib::AddOptimal(wire,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);const double width=x1-x0,depth=y1-y0;
            std::cout<<"Wall lamp inner boundary face="<<faceID<<" surfaceType="<<surface.GetType()<<" orientation="<<face.Orientation()<<" bounds=["<<x0<<','<<y0<<','<<z0<<';'<<x1<<','<<y1<<','<<z1<<']';
            if(surface.GetType()==GeomAbs_Plane){auto normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();std::cout<<" orientedNormal=["<<normal.X()<<','<<normal.Y()<<','<<normal.Z()<<']';}std::cout<<std::endl;
            // The three original 9.3/10.5 mm bores are independent source-file
            // dimensions, not a substitute for production hole classification.
            if((std::abs(width-9.3)<.001||std::abs(width-10.5)<.001)&&std::abs(width-depth)<.001&&z1-z0<.02){
                auto existing=std::find_if(smallOpenings.begin(),smallOpenings.end(),[&](const auto& h){return std::abs(h.X0-x0)<.01&&std::abs(h.Y0-y0)<.01&&std::abs(h.X1-x1)<.01&&std::abs(h.Y1-y1)<.01;});
                if(existing==smallOpenings.end())smallOpenings.push_back({x0,y0,x1,y1,z1});else existing->Top=std::max(existing->Top,z1);
            }
            if(width>100&&depth>100&&z1-z0<.1){++largeInnerWires;IntCurvesFace_ShapeIntersector ray;ray.Load(built.Shape,1e-7);ray.Perform(gp_Lin(gp_Pnt((x0+x1)/2,(y0+y1)/2,wz0-50),gp_Dir(0,0,1)),0.,wz1-wz0+100);ASSERT_TRUE(ray.IsDone());
                bool backed=false;std::cout<<"Wall lamp large mouth face="<<faceID<<" central-ray material hits="<<ray.NbPnt()<<" z=[";
                for(int i=1;i<=ray.NbPnt();++i){const auto z=ray.Pnt(i).Z();std::cout<<z<<',';if(z>z1+.1||z<z0-.1)backed=true;}std::cout<<"] remoteBacking="<<backed<<std::endl;if(backed)++backedLargeInnerWires;
            }
        }
    }
    ASSERT_EQ(smallOpenings.size(),3u);ASSERT_GT(largeInnerWires,0u);EXPECT_GT(backedLargeInnerWires,0u)<<"The large bowl mouth has backing material and is not a through opening";
    for(const auto& hole:smallOpenings){IntCurvesFace_ShapeIntersector ray;ray.Load(built.Shape,1e-7);ray.Perform(gp_Lin(gp_Pnt((hole.X0+hole.X1)/2,(hole.Y0+hole.Y1)/2,wz0-50),gp_Dir(0,0,1)),0.,wz1-wz0+100);ASSERT_TRUE(ray.IsDone());EXPECT_EQ(ray.NbPnt(),0)<<"The known small bore must remain a through opening";}
    auto recipe=PlateRecipe(scene);recipe["supportFaces"]=Array{Text("16")};recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Grid");recipe["stations"]=Array();
    recipe["spacing"]=35.;recipe["baseDrop"]=40.;recipe["jointMode"]=Text("TabSlot");recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe["cutClearanceMode"]=Text("None");
    SetPlateRecipe(scene,recipe);Map candidate;try{candidate=GeneratePlates(scene);}catch(const std::exception& error){FAIL()<<error.what();}
    EXPECT_EQ(candidate.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");EXPECT_GT(candidate.at("parts").To<Array>().size(),3u);
    const auto opening=candidate.at("openingPreclearance").To<Map>();EXPECT_GE(opening.at("boundaryCount").To<UInt>(),3u);std::cout<<"Wall lamp single-face opening preclearance "<<iCAX::Data::VariantSerializer::Serialize(opening)<<std::endl;
    const double bottom=wz0-106;std::vector<TopoDS_Shape> corridors;for(const auto& hole:smallOpenings)corridors.push_back(BRepPrimAPI_MakeBox(gp_Pnt(hole.X0-1,hole.Y0-1,bottom),hole.X1-hole.X0+2,hole.Y1-hole.Y0+2,hole.Top+1-bottom).Shape());
    for(const auto& value:candidate.at("parts").To<Array>()){const auto part=value.To<Map>(),ref=part.at("geometryRef").To<Map>();const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),ref.at("version").To<UInt>());ASSERT_TRUE(model);const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*model);ASSERT_TRUE(shape.bOK);EXPECT_TRUE(BRepCheck_Analyzer(shape.Shape).IsValid());
        for(const auto& corridor:corridors){BRepAlgoAPI_Common overlap(shape.Shape,corridor);ASSERT_TRUE(overlap.IsDone());GProp_GProps volume;BRepGProp::VolumeProperties(overlap.Shape(),volume);EXPECT_LT(std::abs(volume.Mass()),1e-6)<<part.at("number").To<Text>();}
    }
    const auto after=Read(scene),workAfter=after.at("workpieces").To<Array>().front().To<Map>();EXPECT_EQ(workBefore.at("geometryRef"),workAfter.at("geometryRef"));EXPECT_EQ(workBefore.at("sourceRef"),workAfter.at("sourceRef"));
    EXPECT_EQ(geometryBefore,iCAX::GeometryData::Persistence::Serialize(*geometry));EXPECT_EQ(before.at("placements"),after.at("placements"));EXPECT_TRUE(after.at("intentToolpaths").To<Array>().empty());
}
TEST(FiveAxisFixtureDesign, OpeningPreclearanceRejectsSingleBearingWithoutReplacingAnAdoptedAssembly) {
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,18,-10),gp_Dir(0,0,1)),8,20).Shape();
    BRepAlgoAPI_Cut perforated(face,hole);ASSERT_TRUE(perforated.IsDone());Scene scene;File part(perforated.Shape());Imported(scene,part);
    auto recipe=PlateRecipe(scene);recipe["baseDrop"]=100.;recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;recipe["cutClearanceMode"]=Text("None");
    Array stations;for(double x:{-35.,0.,35.})stations.emplace_back(Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",x}});recipe["stations"]=stations;
    recipe=SetPlateRecipe(scene,recipe);const auto candidate=GeneratePlates(scene);AdoptPlates(scene,candidate);const auto accepted=Read(scene).at("fixtures");ASSERT_FALSE(accepted.To<Array>().empty());
    recipe["stations"]=Array{recipe.at("stations").To<Array>()[1]};SetPlateRecipe(scene,recipe);
    const auto before=Read(scene);const auto resources=scene.Resources().Count();const auto undo=scene.Database().GetUndoArray(),redo=scene.Database().GetRedoArray();
    try{GeneratePlates(scene);FAIL()<<"A single remaining bearing cannot qualify a precleared fixture";}
    catch(const std::exception& error){EXPECT_NE(Text(error.what()).find("PLATE_KEEPOUT_INSUFFICIENT_SUPPORT"),Text::npos)<<error.what();}
    const auto after=Read(scene);EXPECT_EQ(accepted,after.at("fixtures"));EXPECT_EQ(before.at("fixtureUnits"),after.at("fixtureUnits"));EXPECT_EQ(before.at("setups"),after.at("setups"));EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));
    EXPECT_EQ(resources,scene.Resources().Count());EXPECT_EQ(undo,scene.Database().GetUndoArray());EXPECT_EQ(redo,scene.Database().GetRedoArray());EXPECT_TRUE(after.at("intentToolpaths").To<Array>().empty());
}
