TEST(FiveAxisDomain, NumericObjectFrameRejectsInvalidAxesAndCommitsAtomicallyWithoutSetup)
{
    Scene scene;File file(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto ids=ImportedWorkpiece(scene,file),before=Read(scene);
    const Map frame{{"origin",Array{10.,20.,30.}},{"x",Array{0.,1.,0.}},{"y",Array{-1.,0.,0.}},{"z",Array{0.,0.,1.}}};
    Map specification{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},{"frame",frame}};
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"setupId",Text()},{"placementId",ids.at("placementId")},{"resetWorkpieceFrame",true},{"registration",specification}};
    for(const auto& [field,value]:std::vector<std::pair<Text,Array>>{{"x",{0.,0.,0.}},{"x",{0.,2.,0.}},{"y",{0.,1.,0.}},{"z",{0.,0.,-1.}},{"origin",{1.,2.}},{"origin",{INFINITY,0.,0.}}}){
        auto bad=request,invalid=frame;invalid[field]=value;auto spec=specification;spec["frame"]=invalid;bad["registration"]=spec;
        EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",bad),std::exception);EXPECT_EQ(before,Read(scene));
    }
    const auto solved=Invoke(&scene,"CamPlacement","SolveRegistration",request);
    EXPECT_EQ((Array{0.,1.,0.,-20.,-1.,0.,0.,10.,0.,0.,1.,-30.,0.,0.,0.,1.}),solved.at("transform").To<Array>());EXPECT_EQ(before,Read(scene));
    const auto applied=Invoke(&scene,"CamPlacement","ApplyRegistration",request),after=Read(scene);
    EXPECT_EQ(applied,Invoke(&scene,"CamPlacement","ApplyRegistration",request));EXPECT_EQ(after,Read(scene));
    EXPECT_TRUE(after.at("setups").To<Array>().empty());EXPECT_NE(before.at("workpieces"),after.at("workpieces"));
    EXPECT_EQ(solved.at("transform"),applied.at("report").To<Map>().at("alignedTransform"));
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(after.at("placements"),Read(scene).at("placements"));
}

TEST(FiveAxisDomain, WorkpieceCoordinateParametersSurviveScaleAndRemainAbsolute)
{
    Scene scene;File file(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto ids=ImportedWorkpiece(scene,file);
    const Map frame{{"origin",Array{10.,20.,30.}},{"x",Array{0.,1.,0.}},{"y",Array{-1.,0.,0.}},{"z",Array{0.,0.,1.}}};
    auto request=[&](const Map& value){return Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"setupId",Text()},{"placementId",ids.at("placementId")},{"resetWorkpieceFrame",true},
        {"registration",Map{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},{"frame",value}}}};};
    Invoke(&scene,"CamPlacement","ApplyRegistration",request(frame));const auto aligned=Read(scene);
    EXPECT_EQ(frame,aligned.at("workpieces").To<Array>().front().To<Map>().at("coordinateFrame").To<Map>());
    const auto same=Invoke(&scene,"CamPlacement","SolveRegistration",request(frame)).at("transform").To<Array>();
    EXPECT_EQ((Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}),same);
    const auto geometry=aligned.at("workpieces").To<Array>().front().To<Map>().at("geometryRef");
    const auto scaled=Wait(scene,Invoke(&scene,"CamWorkpiece","Scale",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",aligned.at("expected")},{"setupId",Text()},{"placementId",ids.at("placementId")},{"sourceGeometry",geometry},{"factors",Array{1.1,1.1,1.1}},{"center",Array{10.,-10.,-20.}}}));
    ASSERT_EQ("Completed",scaled.at("status").To<Text>());const auto after=Read(scene);
    EXPECT_EQ(frame,after.at("workpieces").To<Array>().front().To<Map>().at("coordinateFrame").To<Map>());
    EXPECT_EQ(same,Invoke(&scene,"CamPlacement","SolveRegistration",request(frame)).at("transform").To<Array>());
    auto moved=frame;moved["origin"]=Array{11.,20.,30.};
    EXPECT_EQ((Array{1.,0.,0.,0.,0.,1.,0.,1.,0.,0.,1.,0.,0.,0.,0.,1.}),Invoke(&scene,"CamPlacement","SolveRegistration",request(moved)).at("transform").To<Array>());
    Invoke(&scene,"CamPlacement","ApplyRegistration",request(moved));
    EXPECT_EQ(moved,Read(scene).at("workpieces").To<Array>().front().To<Map>().at("coordinateFrame").To<Map>());
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(moved,Read(scene).at("workpieces").To<Array>().front().To<Map>().at("coordinateFrame").To<Map>());
}

TEST(FiveAxisDomain, AlignmentFrameResetInvalidatesOldCutsAndIsAtomicIdempotentAndPersistent)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);
    const auto extraction=ExtractRequest(scene,ids),terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    ASSERT_EQ("Completed",terminal.at("status").To<Text>());Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal));
    const auto before=Read(scene);ASSERT_FALSE(before.at("intentToolpaths").To<Array>().empty());
    const auto features=Invoke(&scene,"CamScene","ReadSnapFeatures",{{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"objectId",ids.at("placementId")}});
    Map chosen;for(const auto& v:features.at("features").To<Array>()){const auto f=v.To<Map>();if(f.at("mode").To<Text>()=="plane"){chosen={{"featureId",f.at("featureId")},{"mode",f.at("mode")},{"frame",Text("Workpiece")}};break;}}
    ASSERT_FALSE(chosen.empty());
    const Map alignmentStep{{"kind",Text("plane")},{"source",chosen},{"target",Map{{"axis",Text("Y")}}},{"contact",true}};
    const Map specification{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},{"steps",Array{alignmentStep}}};
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},{"resetWorkpieceFrame",true},{"registration",specification}};
    auto bad=request;auto invalid=bad.at("registration").To<Map>();auto steps=invalid.at("steps").To<Array>();auto step=steps[0].To<Map>();step["source"]=Map{{"featureId",Text("plane:invalid")},{"mode",Text("plane")}};steps[0]=step;invalid["steps"]=steps;bad["registration"]=invalid;
    EXPECT_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",bad),std::exception);EXPECT_EQ(before,Read(scene));
    const auto applied=Invoke(&scene,"CamPlacement","ApplyRegistration",request),after=Read(scene);
    EXPECT_EQ(applied,Invoke(&scene,"CamPlacement","ApplyRegistration",request));EXPECT_EQ(after,Read(scene));
    EXPECT_EQ((Array{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}),after.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>());
    for(const auto& cut:after.at("intentToolpaths").To<Array>())EXPECT_EQ("Stale",cut.To<Map>().at("sourceState").To<Text>());
    EXPECT_NE(before.at("workpieces"),after.at("workpieces"));
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(after.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    iCAX::ProjectFile::CProjectFile document({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="重设物坐标系";
    auto output=file.Path;output.replace_extension(".i5cam");document.Save(output,info,scene.Database(),scene.Resources());ASSERT_TRUE(std::filesystem::remove(file.Path));
    Scene reopened(scene.Project,scene.GetSceneID(),false);document.Open(output,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    const auto restored=Read(reopened);EXPECT_EQ(after.at("workpieces"),restored.at("workpieces"));EXPECT_EQ(after.at("placements"),restored.at("placements"));EXPECT_EQ(after.at("intentToolpaths"),restored.at("intentToolpaths"));
    const auto read=Invoke(&reopened,"CamPlacement","ReadRegistration",{{"expected",restored.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});EXPECT_EQ(applied.at("report"),read.at("report"));
    const auto ref=restored.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();
    const auto geometry=reopened.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(geometry);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*geometry);ASSERT_TRUE(built.bOK);EXPECT_TRUE(BRepCheck_Analyzer(built.Shape).IsValid());std::filesystem::remove(output);
}

TEST(FiveAxisDomain, FrameAlignmentProjectsSlantedDirectionsAndConstructsPlaneFromExactSurfacePoints)
{
    Scene scene;File file(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto ids=Imported(scene,file);const auto before=Read(scene);
    Map query{{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"objectId",ids.at("placementId")}};
    const auto catalog=Invoke(&scene,"CamScene","ReadSnapFeatures",query);Map top,a,b;
    for(const auto& v:catalog.at("features").To<Array>()){const auto f=v.To<Map>();const auto p=f.at("point").To<Array>();
        if(f.at("featureId").To<Text>().starts_with("plane:")&&f.at("direction").To<Array>()[2].To<double>()>.99)top=f;
        if(f.at("mode").To<Text>()=="endpoint"&&std::abs(p[0].To<double>())<1e-7&&std::abs(p[1].To<double>())<1e-7&&std::abs(p[2].To<double>())<1e-7)a=f;
        if(f.at("mode").To<Text>()=="endpoint"&&std::abs(p[0].To<double>()-40)<1e-7&&std::abs(p[1].To<double>())<1e-7&&std::abs(p[2].To<double>()-20)<1e-7)b=f;
    }
    ASSERT_FALSE(top.empty());ASSERT_FALSE(a.empty());ASSERT_FALSE(b.empty());
    auto reference=[](const Map& f){return Map{{"mode",f.at("mode")},{"featureId",f.at("featureId")},{"frame",Text("Workpiece")}};};
    const Map planeStep{{"kind",Text("plane")},{"source",reference(top)},{"target",Map{{"axis",Text("Z")}}}};
    const Map direction{{"mode",Text("direction")},{"construction",Map{{"kind",Text("TwoPointDirection")},{"sources",Array{reference(a),reference(b)}}}}};
    const Map xStep{{"kind",Text("direction")},{"source",direction},{"projectToPlane",true},{"target",Map{{"axis",Text("X")}}}};
    Map request{{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
        {"registration",Map{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},{"steps",Array{planeStep,xStep}}}}};
    const auto solved=Invoke(&scene,"CamPlacement","SolveRegistration",request);const auto matrix=solved.at("transform").To<Array>();EXPECT_NEAR(1.,matrix[0].To<double>(),1e-8);EXPECT_NEAR(1.,matrix[10].To<double>(),1e-8);EXPECT_EQ(before,Read(scene));
    Array points;for(const auto& p:{Array{5.,5.,20.},Array{15.,5.,20.},Array{5.,15.,20.}})points.emplace_back(Map{{"mode",Text("point")},{"frame",Text("Workpiece")},{"faceId",top.at("faceIds").To<Array>().front()},{"nearPoint",p},{"maximumDistanceMm",1.}});
    query["query"]=Map{{"mode",Text("plane")},{"construction",Map{{"kind",Text("ThreePointPlane")},{"sources",points}}}};
    const auto constructed=Invoke(&scene,"CamScene","ResolvePickReference",query);EXPECT_NEAR(1.,constructed.at("direction").To<Array>()[2].To<double>(),1e-8);EXPECT_EQ((Array{5.,5.,20.}),constructed.at("point").To<Array>());EXPECT_EQ(before,Read(scene));
}
