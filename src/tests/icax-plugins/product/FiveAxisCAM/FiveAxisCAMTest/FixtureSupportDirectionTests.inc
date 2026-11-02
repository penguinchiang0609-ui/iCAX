namespace {
std::array<double,16> DirectionMatrix(const gp_Trsf& t){std::array<double,16> result{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};for(int i=0;i<3;++i)for(int j=0;j<4;++j)result[i*4+j]=t.Value(i+1,j+1);return result;}
std::array<double,3> DirectionPoint(const gp_Trsf& t,const std::array<double,3>& p){const auto q=gp_Pnt(p[0],p[1],p[2]).Transformed(t);return {q.X(),q.Y(),q.Z()};}
gp_Trsf DirectionTransform(const std::array<double,16>& m){gp_Trsf t;t.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return t;}
}
TEST(FiveAxisFixtureDirection, FaceRecognitionUsesRequestedSignRatherThanGlobalVertical){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=AutoFaceInput(BRepPrimAPI_MakeBox(gp_Pnt(-60,-50,-10),120,100,20).Shape());const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    for(const auto direction:std::array<std::array<double,3>,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}}){
        in.SupportDirection=direction;const auto faces=service->FindLowerEnvelopeFaces(in,{});ASSERT_EQ(faces.size(),1u);GProp_GProps mass;BRepGProp::SurfaceProperties(built.Faces.at(faces.front()),mass);const auto p=mass.CentreOfMass();const int axis=direction[0]?0:direction[1]?1:2;EXPECT_NEAR(p.Coord(axis+1)*direction[axis],axis==0?-60:axis==1?-50:-10,1e-7);
    }
    in.SupportDirection={1,2,3};EXPECT_EQ(service->FindLowerEnvelopeFaces(in,{}).size(),3u);
    in.SupportDirection={0,0,0};EXPECT_THROW(service->FindLowerEnvelopeFaces(in,{}),std::exception);
    in.SupportDirection={std::numeric_limits<double>::quiet_NaN(),0,1};EXPECT_THROW(service->SuggestStations(in,40),std::exception);
}
TEST(FiveAxisFixtureDirection, RotatedSupportFramePreservesHolesKeepOutsContactsAndPlateGeometry){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    const auto work=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-60,-60,0),120,120,20).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25,25,-5),gp_Dir(0,0,1)),6,30).Shape()).Shape();
    auto a=AutoFaceInput(work);a.Automatic=true;a.AvoidOpeningBoundaries=true;a.Transform[3]=17;a.Transform[7]=-23;a.Transform[11]=41;
    a.FaceIDs=service->FindLowerEnvelopeFaces(a,{});a.KeepOuts.push_back(ClearanceBox(12,-28,-10,22,-18,100));a.Stations=service->SuggestStations(a,40);
    const auto source=iCAX::GeometryData::Persistence::Serialize(*a.Geometry);const auto baseline=service->Build(a,{});ASSERT_GT(baseline.size(),2u);ASSERT_TRUE(baseline.back().Supported);EXPECT_GT(baseline.back().OpeningBoundaryCount,0u);
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),.47);auto b=a;b.Transform=DirectionMatrix(rotation*DirectionTransform(a.Transform));const auto axis=gp_Dir(0,0,1).Transformed(rotation);b.SupportDirection={axis.X(),axis.Y(),axis.Z()};
    for(auto& cell:b.KeepOuts)for(auto& p:cell.Vertices)p=DirectionPoint(rotation,p);EXPECT_EQ(service->FindLowerEnvelopeFaces(b,{}),a.FaceIDs);
    b.Stations=service->SuggestStations(b,40);ASSERT_EQ(b.Stations.size(),a.Stations.size());for(size_t i=0;i<a.Stations.size();++i)EXPECT_NEAR(b.Stations[i].Coordinate,a.Stations[i].Coordinate,1e-6);
    const auto rotated=service->Build(b,{});ASSERT_EQ(rotated.size(),baseline.size());EXPECT_TRUE(rotated.back().Supported);
    const auto worldWork=work.Moved(TopLoc_Location(DirectionTransform(b.Transform)));
    for(size_t i=0;i<baseline.size();++i){const auto& p=baseline[i];const auto& q=rotated[i];const auto expected=DirectionMatrix(rotation*DirectionTransform(p.Frame));for(size_t j=0;j<16;++j)EXPECT_NEAR(q.Frame[j],expected[j],1e-6);
        ASSERT_EQ(q.Contacts.size(),p.Contacts.size());for(size_t j=0;j<p.Contacts.size();++j){const auto point=DirectionPoint(rotation,p.Contacts[j]);for(int k=0;k<3;++k)EXPECT_NEAR(q.Contacts[j][k],point[k],1e-5);}
        const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(q.Geometry);ASSERT_TRUE(shape.bOK);const auto common=BRepAlgoAPI_Common(shape.Shape,worldWork).Shape();GProp_GProps overlap;BRepGProp::VolumeProperties(common,overlap);EXPECT_LT(std::abs(overlap.Mass()),1e-5);
        const auto canonical=iCAX::OpenCascade::BuildOpenCascadeShape(p.Geometry);GProp_GProps first,second;BRepGProp::VolumeProperties(canonical.Shape,first);BRepGProp::VolumeProperties(shape.Shape,second);EXPECT_NEAR(std::abs(first.Mass()),std::abs(second.Mass()),.001);
    }
    EXPECT_EQ(source,iCAX::GeometryData::Persistence::Serialize(*a.Geometry));
}
TEST(FiveAxisFixtureDirection, NativeDirectionNormalizesPersistsAndInvalidatesPriorPreview){
    Scene scene;File file(BRepPrimAPI_MakeBox(gp_Pnt(-60,-60,-60),120,120,120).Shape());Imported(scene,file);const auto original=Read(scene);
    auto request=AutoFaceRequest(scene);request["supportDirection"]=Array{0.,0.,0.};EXPECT_THROW(Invoke(&scene,"CamFixture","FindSupportFaces",request),std::exception);EXPECT_EQ(Read(scene).at("expected"),original.at("expected"));
    request=AutoFaceRequest(scene);request["supportDirection"]=Array{4.,0.,0.};const auto task=Wait(scene,Invoke(&scene,"CamFixture","FindSupportFaces",request));ASSERT_EQ(task.at("status").To<Text>(),"Completed");
    auto recipe=PlateRecipe(scene);recipe["supportDirection"]=Array{4.,0.,0.};recipe["supportFaces"]=Invoke(&scene,"CamFixture","ReadSupportSelection",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}}).at("supportFaces");recipe["layout"]=Text("Automatic");recipe["stations"]=Array{};
    const auto saved=SetPlateRecipe(scene,recipe);EXPECT_EQ(saved.at("supportDirection"),(Variant(Array{1.,0.,0.})));const auto candidate=GeneratePlates(scene);ASSERT_EQ(candidate.at("supportCheck").To<Map>().at("status").To<Text>(),"Supported");
    const auto base=candidate.at("parts").To<Array>().back().To<Map>().at("frame").To<Array>();EXPECT_NEAR(base[2].To<double>(),1,1e-7);EXPECT_NEAR(base[6].To<double>(),0,1e-7);EXPECT_NEAR(base[10].To<double>(),0,1e-7);AdoptPlates(scene,candidate);
    iCAX::ProjectFile::CProjectFile archive({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Support direction";auto path=file.Path;path.replace_extension(".direction.i5cam");archive.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);archive.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);const auto reopenedDesign=Read(reopened).at("setups").To<Array>().front().To<Map>().at("fixtureDesign").To<Map>();EXPECT_EQ(reopenedDesign.at("recipe").To<Map>().at("supportDirection"),(Variant(Array{1.,0.,0.})));EXPECT_EQ(reopenedDesign.at("assembly").To<Map>().at("validity").To<Text>(),"Current");
    recipe=saved;recipe["supportDirection"]=Array{-1.,0.,0.};SetPlateRecipe(reopened,recipe);EXPECT_EQ(Read(reopened).at("setups").To<Array>().front().To<Map>().at("fixtureDesign").To<Map>().at("candidate").To<Map>().at("validity").To<Text>(),"Stale");EXPECT_TRUE(reopened.Database().Undo());EXPECT_EQ(Read(reopened).at("workpieces").To<Array>().front().To<Map>().at("geometryRef"),original.at("workpieces").To<Array>().front().To<Map>().at("geometryRef"));std::filesystem::remove(path);
}
