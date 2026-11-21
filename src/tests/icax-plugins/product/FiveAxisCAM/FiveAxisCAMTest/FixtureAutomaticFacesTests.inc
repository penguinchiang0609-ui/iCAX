namespace {
iCAX::Geometry::SectionPlateInput AutoFaceInput(const TopoDS_Shape& shape){
    iCAX::Geometry::SectionPlateInput in;in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,"Automatic bearing","generated",.05));
    in.Transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};return in;
}
TopoDS_Shape AutoFaceCompound(const TopoDS_Shape& a,const TopoDS_Shape& b){TopoDS_Compound shape;BRep_Builder builder;builder.MakeCompound(shape);builder.Add(shape,a);builder.Add(shape,b);return shape;}
TopoDS_Face AutoFacePlane(double x0,double x1,double z){return BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,z),gp_Dir(0,0,1)),x0,x1,-50,50).Face();}
Map AutoFaceRequest(Scene& scene){auto r=FixtureRequest(scene);r["placementId"]=Read(scene).at("placements").To<Array>().front().To<Map>().at("id");return r;}
}
TEST(FiveAxisFixtureAutomaticFaces, ClosedSolidUsesCurrentPlacementAndLeavesCadUnchanged){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=AutoFaceInput(BRepPrimAPI_MakeBox(gp_Pnt(-60,-50,0),120,100,20).Shape());
    const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);
    const auto bottom=service->FindLowerEnvelopeFaces(in,{});ASSERT_EQ(bottom.size(),1u);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);GProp_GProps mass;BRepGProp::SurfaceProperties(built.Faces.at(bottom.front()),mass);EXPECT_NEAR(mass.CentreOfMass().Z(),0,1e-7);
    in.Transform={1,0,0,7,0,0,-1,9,0,1,0,11,0,0,0,1};const auto rotated=service->FindLowerEnvelopeFaces(in,{});ASSERT_EQ(rotated.size(),1u);EXPECT_NE(rotated,bottom);
    BRepGProp::SurfaceProperties(built.Faces.at(rotated.front()),mass);EXPECT_NEAR(mass.CentreOfMass().Y(),-50,1e-7);
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureAutomaticFaces, OccludedFacesAreExcludedButPartiallyExposedPatchesRemain){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    auto hidden=AutoFaceInput(AutoFaceCompound(AutoFacePlane(-50,50,0),AutoFacePlane(-40,40,10)));
    EXPECT_EQ(service->FindLowerEnvelopeFaces(hidden,{}).size(),1u);
    // Exposed strip is smaller than both triangles' centroid offsets.
    auto partial=AutoFaceInput(AutoFaceCompound(AutoFacePlane(-50,49,0),AutoFacePlane(-50,50,10)));
    EXPECT_EQ(service->FindLowerEnvelopeFaces(partial,{}).size(),2u);
    auto stacked=AutoFaceInput(AutoFaceCompound(BRepPrimAPI_MakeBox(gp_Pnt(-30,-30,0),60,60,10).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-50,-50,20),100,100,10).Shape()));
    EXPECT_EQ(service->FindLowerEnvelopeFaces(stacked,{}).size(),2u);
}
TEST(FiveAxisFixtureAutomaticFaces, HolesPreserveVisibilityAndSheetsDoNotDependOnOrientation){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    const auto lower=BRepAlgoAPI_Cut(AutoFacePlane(-50,50,0),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-5),gp_Dir(0,0,1)),8,20).Shape()).Shape();
    auto in=AutoFaceInput(AutoFaceCompound(lower,AutoFacePlane(-40,40,10)));EXPECT_EQ(service->FindLowerEnvelopeFaces(in,{}).size(),2u);
    in=AutoFaceInput(AutoFacePlane(-50,50,0).Reversed());EXPECT_EQ(service->FindLowerEnvelopeFaces(in,{}).size(),1u);
    in=AutoFaceInput(FixtureCurvedSheet());EXPECT_EQ(service->FindLowerEnvelopeFaces(in,{}).size(),1u);
    in=AutoFaceInput(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-50,0,0),gp_Dir(1,0,0)),20,100).Shape());EXPECT_EQ(service->FindLowerEnvelopeFaces(in,{}).size(),1u);
    EXPECT_THROW(service->FindLowerEnvelopeFaces(in,[]{return true;}),std::exception);
}
TEST(FiveAxisFixtureAutomaticFaces, NativeSuggestionIsReadOnlyCancelableAndFeedsExplicitGeneration){
    Scene scene;File file(BRepPrimAPI_MakeBox(gp_Pnt(-60,-60,0),120,120,20).Shape());Imported(scene,file);const auto before=Read(scene);
    const auto request=AutoFaceRequest(scene);const auto task=Invoke(&scene,"CamFixture","FindSupportFaces",request);EXPECT_EQ(task.at("taskId"),Invoke(&scene,"CamFixture","FindSupportFaces",request).at("taskId"));
    const auto completed=Wait(scene,task);ASSERT_EQ(completed.at("status").To<Text>(),"Completed");EXPECT_FALSE(completed.at("result").To<Map>().contains("supportFaces"));EXPECT_FALSE(completed.at("result").To<Map>().contains("faces"));const auto result=Invoke(&scene,"CamFixture","ReadSupportSelection",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});ASSERT_EQ(result.at("supportFaces").To<Array>().size(),1u);EXPECT_FALSE(Invoke(&scene,"CamPlacement","ReadReferenceFaces",request).at("faces").To<Array>().empty());
    EXPECT_EQ(result.at("basis").To<Text>(),"TrimmedBRepLowerEnvelope");EXPECT_FALSE(result.contains("meshDeflectionMm"));
    const auto after=Read(scene);for(const auto* key:{"expected","workpieces","placements","setups","fixtures","intentToolpaths"})EXPECT_EQ(before.at(key),after.at(key))<<key;
    auto wrong=AutoFaceRequest(scene);wrong["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamFixture","FindSupportFaces",wrong),std::exception);
    const auto cancelled=Invoke(&scene,"CamFixture","FindSupportFaces",AutoFaceRequest(scene));Invoke(&scene,"CamFixture","CancelTask",{{"epoch",cancelled.at("epoch")},{"taskId",cancelled.at("taskId")}});EXPECT_EQ(Wait(scene,cancelled).at("status").To<Text>(),"Cancelled");
    auto recipe=PlateRecipe(scene);recipe["supportFaces"]=result.at("supportFaces");recipe["layout"]=Text("Automatic");recipe["stations"]=Array{};SetPlateRecipe(scene,recipe);
    const auto candidate=GeneratePlates(scene);EXPECT_EQ(candidate.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());
}
void CheckRealHatRailRecognition(bool interactiveBudget){
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path("TestData/FA/Video_HatRail_1200x220x70_t1p2.step");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}
    ASSERT_TRUE(std::filesystem::exists(root/relative));Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());
    ASSERT_EQ(Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request)).at("status").To<Text>(),"Completed");
    const auto state=Read(scene),ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();iCAX::Geometry::SectionPlateInput input;
    input.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto transform=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();for(size_t i=0;i<16;++i)input.Transform[i]=transform[i].To<double>();
    const auto original=iCAX::GeometryData::Persistence::Serialize(*input.Geometry);const auto started=std::chrono::steady_clock::now();
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();std::vector<std::uint64_t> faces;
    ASSERT_NO_THROW(faces=service->FindLowerEnvelopeFaces(input,[&]{return interactiveBudget&&std::chrono::steady_clock::now()-started>std::chrono::seconds(10);}));
    EXPECT_FALSE(faces.empty());EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*input.Geometry));
    ::testing::Test::RecordProperty("recognitionMs",static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()));::testing::Test::RecordProperty("selectedFaces",static_cast<int>(faces.size()));
}
TEST(FiveAxisFixtureAutomaticFaces, RealHatRailRecognitionFinishesWithinInteractiveBudget){CheckRealHatRailRecognition(true);}
TEST(FiveAxisFixtureAutomaticFaces, DISABLED_RealHatRailExactSelectionPreservesCad){CheckRealHatRailRecognition(false);}
