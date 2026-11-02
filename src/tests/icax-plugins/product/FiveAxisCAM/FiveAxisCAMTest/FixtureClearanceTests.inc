namespace {
iCAX::Geometry::SectionPlateInput ClearanceInput(){
    iCAX::Geometry::SectionPlateInput in;
    in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face(),"Clearance workpiece","generated",.05));
    in.Transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};for(const auto& face:in.Geometry->Faces)in.FaceIDs.push_back(face.Id);
    for(double x:{-40.,0.,40.})in.Stations.push_back({x,-60,60,true});return in;
}
TEST(FiveAxisFixtureClearance, DISABLED_ReplaySavedProject){
    wchar_t path[32768]{};ASSERT_GT(GetEnvironmentVariableW(L"ICAX_FIXTURE_REPLAY_PROJECT",path,32768),0u);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(std::filesystem::path(path));const auto info=prepared.Result().Info;
    Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    auto recipe=Read(scene).at("setups").To<Array>().front().To<Map>().at("fixtureDesign").To<Map>().at("recipe").To<Map>();
    recipe["margin"]=15.;recipe["cutClearanceMode"]=Text("Toolpath");SetPlateRecipe(scene,recipe);
    const auto candidate=GeneratePlates(scene);std::cout<<"Fixture replay parts="<<candidate.at("parts").To<Array>().size()<<std::endl;
    EXPECT_EQ("Supported",candidate.at("supportCheck").To<Map>().at("status").To<Text>());
}
TEST(FiveAxisFixtureClearance, DISABLED_DenseSavedWorkpiece){
    wchar_t path[32768]{};ASSERT_GT(GetEnvironmentVariableW(L"ICAX_FIXTURE_REPLAY_PROJECT",path,32768),0u);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(std::filesystem::path(path));const auto info=prepared.Result().Info;
    Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto before=Read(scene),workpiece=before.at("workpieces").To<Array>().front().To<Map>();
    const auto candidate=GeneratePlates(scene);
    ASSERT_GT(candidate.at("parts").To<Array>().size(),1u);
    const auto after=Read(scene).at("workpieces").To<Array>().front().To<Map>();
    EXPECT_EQ(workpiece.at("geometryRef"),after.at("geometryRef"));
    EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
}
iCAX::Geometry::SectionPlateKeepOut ClearanceBox(double x0,double y0,double z0,double x1,double y1,double z1,double margin=.25){
    iCAX::Geometry::SectionPlateKeepOut cell;cell.Margin=margin;for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1})cell.Vertices.push_back({x,y,z});return cell;
}
TopoDS_Shape ClearanceShape(const iCAX::Geometry::SectionPlate& plate){auto built=iCAX::OpenCascade::BuildOpenCascadeShape(plate.Geometry);if(!built.bOK)throw std::runtime_error("Clearance test BRep invalid");return built.Shape;}
double ClearanceArea(const std::vector<std::array<double,2>>& loop){double result=0;for(size_t i=0;i<loop.size();++i){auto a=loop[i],b=loop[(i+1)%loop.size()];result+=a[0]*b[1]-a[1]*b[0];}return std::abs(result)/2;}
void CheckClearancePlate(const iCAX::Geometry::SectionPlate& plate,const std::vector<TopoDS_Shape>& forbidden){
    const auto shape=ClearanceShape(plate);ASSERT_TRUE(BRepCheck_Analyzer(shape).IsValid());size_t solids=0;for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++solids;EXPECT_EQ(solids,1u);
    double area=ClearanceArea(plate.Outline);for(const auto& hole:plate.Holes)area-=ClearanceArea(hole);
    // Filleted joint roots retain exact arcs; the sampled outline's chord
    // area is only a display approximation, not the extrusion volume oracle.
    if(!plate.ExactProfile.Boundaries.empty())area=JointExactArea(Map{{"exactProfile",iCAX::CAD::TubeCadJson::Encode(plate.ExactProfile)}});
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);EXPECT_NEAR(std::abs(properties.Mass()),area*plate.Thickness,1e-4);
    for(const auto& obstacle:forbidden){BRepAlgoAPI_Common common(shape,obstacle);ASSERT_TRUE(common.IsDone());BRepGProp::VolumeProperties(common.Shape(),properties);EXPECT_LT(std::abs(properties.Mass()),1e-6);}
    if(!plate.Base){EXPECT_FALSE(plate.Contacts.empty());EXPECT_LE(plate.Distance,2*plate.TopRetreat+.001);}
}
}
TEST(FiveAxisFixtureClearance, ContinuousSweepAndOuterThicknessIntersectWithoutSampleAtPlate){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    // Only the two remote end planes are supplied. Every middle support must
    // still avoid the entire connecting convex sweep, not only its samples.
    in.KeepOuts.push_back(ClearanceBox(-70,-4,-25,70,4,-15));
    // This lies away from the station's centre plane but inside its thickness.
    in.KeepOuts.push_back(ClearanceBox(1.1,18,-25,1.4,24,-15,.05));
    const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);
    const std::vector<TopoDS_Shape> forbidden{BRepPrimAPI_MakeBox(gp_Pnt(-70.25,-4.25,-25.25),140.5,8.5,10.5).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(1.05,17.95,-25.05),.4,6.1,10.1).Shape()};
    for(const auto& plate:parts)CheckClearancePlate(plate,forbidden);
    EXPECT_EQ(parts[0].Holes.size(),1u);EXPECT_EQ(parts[1].Holes.size(),2u);EXPECT_EQ(parts[2].Holes.size(),1u);EXPECT_TRUE(parts.back().Holes.empty());
    EXPECT_EQ(JointState(ClearanceShape(parts[1]),gp_Pnt(0,21,-20)),TopAbs_OUT);
}
TEST(FiveAxisFixtureClearance, BaseBeamAndHeadOpeningShareContoursWithSolid){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    in.KeepOuts={ClearanceBox(10,10,-60,20,20,10),ClearanceBox(34,20,-18,46,30,20)};
    const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);ASSERT_EQ(parts.back().Holes.size(),1u);EXPECT_GT(parts.back().KeepOutReliefCount,0u);
    const std::vector<TopoDS_Shape> forbidden{BRepPrimAPI_MakeBox(gp_Pnt(9.75,9.75,-60.25),10.5,10.5,70.5).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(33.75,19.75,-18.25),12.5,10.5,38.5).Shape()};
    for(const auto& plate:parts)CheckClearancePlate(plate,forbidden);
    EXPECT_EQ(JointState(ClearanceShape(parts.back()),gp_Pnt(15,15,-42)),TopAbs_OUT);
    EXPECT_EQ(JointState(ClearanceShape(parts[2]),gp_Pnt(40,25,-1)),TopAbs_OUT);
}
TEST(FiveAxisFixtureClearance, CompromisedLapStationIsRemovedAndAllJointsRebuilt){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.Stations=service->SuggestStations(in,50);
    in.KeepOuts={ClearanceBox(-2,-2,-22,2,2,-18,.2)};const auto parts=service->Build(in,{});
    ASSERT_GT(parts.size(),3u);EXPECT_TRUE(parts.back().Supported);EXPECT_GT(parts.back().RejectedStationCount,0u);
    std::set<size_t> stations;for(const auto& p:parts)if(!p.Base)stations.insert(p.StationIndex);
    const std::vector<TopoDS_Shape> forbidden{BRepPrimAPI_MakeBox(gp_Pnt(-2.2,-2.2,-22.2),4.4,4.4,4.4).Shape()};
    for(const auto& plate:parts){CheckClearancePlate(plate,forbidden);for(const auto& joint:plate.Joints)if(!joint.MateBase)EXPECT_TRUE(stations.contains(joint.MateStationIndex));}
    in.Automatic=false;EXPECT_THROW(service->Build(in,{}),std::exception);
}
TEST(FiveAxisFixtureClearance, ClosedSweepDiscardsLooseBaseIslandAndKeepsUsableCore){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    // A closed contour separates a narrow outside frame from the larger core.
    // Deliver one connected base and shortened supports, never a loose island.
    in.KeepOuts={ClearanceBox(-56,-56,-60,-54,56,10,0),ClearanceBox(54,-56,-60,56,56,10,0),ClearanceBox(-56,-56,-60,56,-54,10,0),ClearanceBox(-56,54,-60,56,56,10,0)};
    const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);
    for(const auto& plate:parts)CheckClearancePlate(plate,{});
    EXPECT_EQ(JointState(ClearanceShape(parts.back()),gp_Pnt(0,0,-42)),TopAbs_IN);
    EXPECT_EQ(JointState(ClearanceShape(parts.back()),gp_Pnt(70,0,-42)),TopAbs_OUT);
    EXPECT_TRUE(parts.back().Holes.empty());
}
TEST(FiveAxisFixtureClearance, DegenerateSweepsMarginsCancellationAndUnsafeSupportFailClosed){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    // A line, duplicated points and a coplanar quad are legal dilated hulls.
    in.KeepOuts={{{{-70,0,-20},{70,0,-20},{70,0,-20}},1.}};
    const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),4u);for(size_t i=0;i+1<parts.size();++i)EXPECT_EQ(parts[i].Holes.size(),1u);
    auto bad=in;bad.KeepOuts.front().Margin=-1;EXPECT_THROW(service->Build(bad,{}),std::exception);
    EXPECT_THROW(service->Build(in,[]{return true;}),std::exception);
    in.KeepOuts={{{{-100,30,-60},{100,30,-60},{100,30,10},{-100,30,10}},.1}};
    const auto coplanar=service->Build(in,{});ASSERT_EQ(coplanar.size(),4u);
    const std::vector<TopoDS_Shape> beam{BRepPrimAPI_MakeBox(gp_Pnt(-100.1,29.9,-60.1),200.2,.2,70.2).Shape()};
    for(const auto& plate:coplanar)CheckClearancePlate(plate,beam);
    in.Automatic=true;in.KeepOuts={ClearanceBox(-100,-100,-60,100,100,10)};EXPECT_THROW(service->Build(in,{}),std::exception);
    // A 2 mm ligament would connect these closed holes: reject this station.
    in=ClearanceInput();in.KeepOuts={ClearanceBox(-2,-20,-30,2,-10,-10,0),ClearanceBox(-2,-8,-30,2,2,-10,0)};EXPECT_THROW(service->Build(in,{}),std::exception);
}
TEST(FiveAxisFixtureClearance, OpenReliefCannotQualifyANarrowBearingFinger){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    // At x=0 these two open cuts leave a full-width 10 mm lower beam and a
    // 1 mm wide, 30 mm tall bearing finger. The lower beam survives erosion,
    // but its connected core cannot qualify the distant finger's top contact.
    in.KeepOuts={ClearanceBox(-2,-70,-30,2,-.5,10,0),ClearanceBox(-2,.5,-30,2,70,10,0)};
    EXPECT_THROW(service->Build(in,{}),std::exception);
    in.Automatic=true;const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),3u);EXPECT_EQ(parts.back().RejectedStationCount,1u);EXPECT_TRUE(parts.back().Supported);
    for(const auto& plate:parts){CheckClearancePlate(plate,{});if(!plate.Base)EXPECT_NE(plate.StationIndex,1u);}
}
TEST(FiveAxisFixtureClearance, BaseMergesCloseOpeningsWithoutRefillingForbiddenMaterial){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    // Two through openings leave a 2 mm ligament in a 5 mm minimum-web base.
    // Removing that ligament merges the openings; it must not fill either cut.
    in.KeepOuts={ClearanceBox(10,10,-46,20,20,-40.5,0),ClearanceBox(10,22,-46,20,32,-40.5,0)};
    const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);EXPECT_EQ(parts.back().Holes.size(),1u);
    const auto solid=ClearanceShape(parts.back());EXPECT_EQ(JointState(solid,gp_Pnt(15,21,-42)),TopAbs_OUT);
    const std::vector<TopoDS_Shape> forbidden{BRepPrimAPI_MakeBox(gp_Pnt(10,10,-46),10,10,5.5).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(10,22,-46),10,10,5.5).Shape()};
    for(const auto& plate:parts)CheckClearancePlate(plate,forbidden);
    EXPECT_EQ(JointState(solid,gp_Pnt(0,0,-42)),TopAbs_IN);EXPECT_EQ(JointState(solid,gp_Pnt(74,74,-42)),TopAbs_IN);
}
