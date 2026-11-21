Array SetupLocatingQueries(Scene& scene,const Map& placement)
{
    const auto features=Invoke(&scene,"CamScene","ReadSnapFeatures",{{"expected",Read(scene).at("expected")},{"setupId",placement.at("setupId")},{"objectId",placement.at("id")}}).at("features").To<Array>();
    Array queries;std::vector<std::array<double,3>> points;
    for(const auto& value:features){
        const auto f=value.To<Map>();const auto mode=f.at("mode").To<Text>();
        if(mode!="endpoint"&&mode!="center")continue;
        const auto xyz=f.at("point").To<Array>();std::array<double,3> p{xyz[0].To<double>(),xyz[1].To<double>(),xyz[2].To<double>()};
        if(points.size()==1){double length=0;for(int i=0;i<3;++i)length+=(p[i]-points[0][i])*(p[i]-points[0][i]);if(length<1.)continue;}
        if(points.size()==2){std::array<double,3>a,b;for(int i=0;i<3;++i){a[i]=p[i]-points[0][i];b[i]=points[1][i]-points[0][i];}double c=0;for(int i=0;i<3;++i){const double v=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];c+=v*v;}if(c<1.)continue;}
        queries.emplace_back(Map{{"frame",Text("Workpiece")},{"mode",mode},{"featureId",f.at("featureId")}});points.push_back(p);if(queries.size()==3)break;
    }
    return queries;
}
TEST(FiveAxisSetups, SequenceIsExactAtomicUndoableAndDoesNotMutateLocalPaths)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene),first=before.at("placements").To<Array>().front().To<Map>();
    Map placement{{"placementId",first.at("id")}};
    auto create=ActivateRequest(scene,placement);create["action"]=Text("create");create["name"]=Text("第二装夹");create["machineType"]=Text("HeadHead");
    const auto second=Invoke(&scene,"CamSetup","Manage",create);
    const auto state=Read(scene);const auto cuts=state.at("intentToolpaths"),geometry=state.at("setups").To<Array>().front().To<Map>().at("generatedGeometry");
    auto request=GenerateRequest(scene);request.erase("setupId");request["setupIds"]=Array{second.at("setupId"),first.at("setupId")};
    const auto result=Invoke(&scene,"CamSetup","SetSequence",request);
    EXPECT_EQ(result,Invoke(&scene,"CamSetup","SetSequence",request));
    EXPECT_EQ(second.at("setupId"),Read(scene).at("setups").To<Array>().front().To<Map>().at("id"));
    EXPECT_EQ(cuts,Read(scene).at("intentToolpaths"));
    EXPECT_EQ(geometry,Read(scene).at("setups").To<Array>().back().To<Map>().at("generatedGeometry"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(first.at("setupId"),Read(scene).at("setups").To<Array>().front().To<Map>().at("id"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(second.at("setupId"),Read(scene).at("setups").To<Array>().front().To<Map>().at("id"));
    auto bad=GenerateRequest(scene);bad.erase("setupId");bad["setupIds"]=Array{second.at("setupId"),second.at("setupId")};
    EXPECT_THROW(Invoke(&scene,"CamSetup","SetSequence",bad),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamSetup","SetSequence",request),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
}
TEST(FiveAxisSetups, ProgramRequiresCurrentContinuousRapidsAndMarksNeutralOutput)
{
    Scene scene;PrepareOrderPart(scene);
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
    RapidConfig(scene);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    // Generated paths alone cannot produce machine coordinates.
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
    const auto before=Read(scene),placement=before.at("placements").To<Array>().front().To<Map>();
    const auto queries=SetupLocatingQueries(scene,placement);ASSERT_EQ(3u,queries.size());
    auto request=GenerateRequest(scene);request.erase("setupId");request["placementId"]=placement.at("id");request["queries"]=queries;
    const auto plan=Invoke(&scene,"CamSetup","SetRegistrationPlan",request);
    Array measured;for(const auto& p:plan.at("points").To<Array>())measured.push_back(p.To<Map>().at("point"));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");request["measuredPoints"]=measured;request["maximumResidualMm"]=.01;
    const auto identityPlan=Invoke(&scene,"CamSetup","SetRegistrationPlan",request);
    const auto result=Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}});
    EXPECT_FALSE(result.at("machineReady").To<bool>());EXPECT_EQ("ICAX-CL",result.at("format").To<Text>());
    const auto text=result.at("content").To<Text>();
    // A disabled setup is independently skipped, even when its shared workpiece is enabled.
    auto extra=ActivateRequest(scene,Map{{"placementId",placement.at("id")}});extra["action"]=Text("create");extra["name"]=Text("跳过工序");extra["machineType"]=Text("HeadHead");
    const auto second=Invoke(&scene,"CamSetup","Manage",extra);
    auto toggle=ActivateRequest(scene,second);toggle["action"]=Text("disable");
    const auto receipt=Invoke(&scene,"CamSetup","Manage",toggle);EXPECT_EQ(receipt,Invoke(&scene,"CamSetup","Manage",toggle));
    EXPECT_EQ(text,Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}).at("content").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
    ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ(text,Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}).at("content").To<Text>());
    auto cleanup=ActivateRequest(scene,second);cleanup["action"]=Text("remove");Invoke(&scene,"CamSetup","Manage",cleanup);

    EXPECT_NE(Text::npos,text.find("\nRAPID\n"));EXPECT_NE(Text::npos,text.find("\nGOTO/"));EXPECT_NE(Text::npos,text.find("\nFROM/"));
    EXPECT_EQ(Text::npos,text.find("\nM00\n"));EXPECT_NE(Text::npos,text.find("POSTPROCESSING REQUIRED"));
    EXPECT_EQ("Machine",result.at("coordinateFrame").To<Text>());EXPECT_NE(Text::npos,text.find("FRAME/MACHINE"));
    // Rotate 90 degrees about Z and translate. Compare EVERY cut and rapid TCP
    // and axis against the independent analytic transform.
    Array transformed;for(const auto& p:measured){const auto xyz=p.To<Array>();transformed.emplace_back(Array{100.-xyz[1].To<double>(),200.+xyz[0].To<double>(),300.+xyz[2].To<double>()});}
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");request["measuredPoints"]=transformed;
    const auto machinePlan=Invoke(&scene,"CamSetup","SetRegistrationPlan",request);
    EXPECT_EQ(machinePlan,Invoke(&scene,"CamSetup","SetRegistrationPlan",request));
    EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    const auto changed=Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}).at("content").To<Text>();
    const auto rows=[](const Text& body){std::vector<std::array<double,6>> points;std::istringstream lines(body);Text line;while(std::getline(lines,line))if(line.starts_with("FROM/")||line.starts_with("GOTO/")){line=line.substr(5);std::replace(line.begin(),line.end(),',',' ');std::istringstream values(line);std::array<double,6> p{};for(auto& v:p)values>>v;points.push_back(p);}return points;};
    const auto a=rows(text),b=rows(changed);ASSERT_EQ(a.size(),b.size());ASSERT_GT(a.size(),3u);
    for(size_t i=0;i<a.size();++i){const std::array<double,6> wanted{100.-a[i][1],200.+a[i][0],300.+a[i][2],-a[i][4],a[i][3],a[i][5]};for(size_t j=0;j<6;++j)EXPECT_NEAR(wanted[j],b[i][j],1e-7);}
    auto bad=request;bad["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());bad["expected"]=Read(scene).at("expected");bad["measuredPoints"]=Array{transformed[0],transformed[0],transformed[0]};
    EXPECT_THROW(Invoke(&scene,"CamSetup","SetRegistrationPlan",bad),std::exception);
    bad["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());auto wrong=transformed;auto point=wrong[2].To<Array>();point[2]=point[2].To<double>()+10.;wrong[2]=point;bad["measuredPoints"]=wrong;
    EXPECT_THROW(Invoke(&scene,"CamSetup","SetRegistrationPlan",bad),std::exception);
    EXPECT_EQ(machinePlan,SetupState(scene,placement.at("setupId")).at("registrationPlan").To<Map>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(identityPlan,SetupState(scene,placement.at("setupId")).at("registrationPlan").To<Map>());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(machinePlan,SetupState(scene,placement.at("setupId")).at("registrationPlan").To<Map>());
    EXPECT_THROW(Invoke(nullptr,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
}
TEST(FiveAxisSetups, CutPointProgramUsesSetupFrameWithoutRegistrationOrRapids)
{
    const auto projectID=iCAX::Data::GenerateNewUUID();Scene scene(projectID,projectID);PrepareOrderPart(scene);
    const auto initial=Read(scene),placement=initial.at("placements").To<Array>().front().To<Map>();
    const Map output{{"expected",initial.at("expected")},{"coordinateFrame",Text("Setup")},{"referencePoint",Text("CutPoint")},{"curveMode",Text("Sampled")}};
    const auto exported=Invoke(&scene,"CamSetup","ExportProgram",output);
    EXPECT_EQ("Setup",exported.at("coordinateFrame").To<Text>());EXPECT_EQ("CutPoint",exported.at("referencePoint").To<Text>());
    EXPECT_FALSE(exported.at("machineReady").To<bool>());
    EXPECT_EQ(Text::npos,exported.at("content").To<Text>().find("\nRAPID\n"));
    EXPECT_EQ(Text::npos,exported.at("content").To<Text>().find("\nM00\n"));
    EXPECT_NE(Text::npos,exported.at("content").To<Text>().find("FRAME/SETUP"));
    // A 90-degree rotation about X plus translation must affect every point
    // and rotate every unit axis without adding nozzle follow height.
    auto move=GenerateRequest(scene);move["placementId"]=placement.at("id");
    move["transform"]=Array{1.,0.,0.,100.,0.,0.,-1.,200.,0.,1.,0.,300.,0.,0.,0.,1.};
    Invoke(&scene,"CamPlacement","SetTransform",move);
    auto stale=output;stale["expected"]=Read(scene).at("expected");
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",stale),std::exception);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto state=Read(scene),geometry=state.at("setups").To<Array>().front().To<Map>().at("generatedGeometry").To<Map>();
    auto ready=output;ready["expected"]=state.at("expected");
    const auto text=Invoke(&scene,"CamSetup","ExportProgram",ready).at("content").To<Text>();
    std::vector<std::array<double,6>> rows;std::istringstream lines(text);Text line;size_t starts=0;
    std::array<double,3> modal{};bool hasAxis=false;
    while(std::getline(lines,line))if(line.starts_with("FROM/")||line.starts_with("GOTO/")){
        if(line.starts_with("FROM/"))++starts;line=line.substr(5);std::replace(line.begin(),line.end(),',',' ');
        std::array<double,6> row{};std::istringstream values(line);for(size_t j=0;j<3;++j)ASSERT_TRUE(static_cast<bool>(values>>row[j]));
        if(values>>row[3]){ASSERT_TRUE(static_cast<bool>(values>>row[4]>>row[5]));modal={row[3],row[4],row[5]};hasAxis=true;}else{ASSERT_TRUE(hasAxis);for(size_t j=0;j<3;++j)row[j+3]=modal[j];}rows.push_back(row);
    }
    EXPECT_EQ(geometry.at("operations").To<Array>().size(),starts);size_t index=0;
    for(const auto& operation:geometry.at("operations").To<Array>())for(const auto& part:operation.To<Map>().at("parts").To<Array>()){
        const auto points=part.To<Map>().at("points").To<Array>(),axes=part.To<Map>().at("directions").To<Array>();
        for(size_t i=0;i<points.size();++i){
            const auto p=points[i].To<Array>(),d=axes[i].To<Array>();const auto x=p[0].To<double>(),y=p[1].To<double>(),z=p[2].To<double>();
            const auto dx=d[0].To<double>(),dy=d[1].To<double>(),dz=d[2].To<double>(),length=std::hypot(dx,dy,dz);
            const std::array<double,6> expected{100.+x,200.-z,300.+y,dx/length,-dz/length,dy/length};
            ASSERT_LT(index,rows.size());for(size_t j=0;j<6;++j)EXPECT_NEAR(expected[j],rows[index][j],1e-7);++index;
        }
    }
    EXPECT_EQ(index,rows.size());EXPECT_EQ(state.at("placements"),Read(scene).at("placements"));
    auto exactRequest=ready;exactRequest["curveMode"]=Text("PreferExact");
    const auto exactText=Invoke(&scene,"CamSetup","ExportProgram",exactRequest).at("content").To<Text>();
    EXPECT_NE(Text::npos,exactText.find("\nCURVE/"));EXPECT_LT(exactText.size(),text.size());
    std::istringstream exactLines(exactText);size_t axisRows=0,curveRows=0;
    while(std::getline(exactLines,line)){
        if(line.starts_with("CURVE/")){const auto record=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line.substr(6)).To<Map>();EXPECT_EQ("icax.cl-curve/1",record.at("schema").To<Text>());EXPECT_TRUE(record.contains("range"));++curveRows;}
        if((line.starts_with("FROM/")||line.starts_with("GOTO/"))&&std::count(line.begin(),line.end(),',')==5)++axisRows;
    }
    EXPECT_GT(curveRows,0u);EXPECT_EQ(1u,axisRows);
    auto invalid=ready;invalid["coordinateFrame"]=Text("Machine");EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",invalid),std::exception);
    auto copy=GenerateRequest(scene);copy.erase("setupId");copy["placementId"]=placement.at("id");
    const auto second=Invoke(&scene,"CamSetup","Copy",copy);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    auto order=GenerateRequest(scene);order.erase("setupId");order["setupIds"]=Array{second.at("setupId"),placement.at("setupId")};
    Invoke(&scene,"CamSetup","SetSequence",order);ready["expected"]=Read(scene).at("expected");
    const auto two=Invoke(&scene,"CamSetup","ExportProgram",ready);const auto body=two.at("content").To<Text>();
    EXPECT_EQ(2u,two.at("setupCount").To<UInt>());
    const auto boundary=body.find("\nM00\n");ASSERT_NE(Text::npos,boundary);EXPECT_EQ(Text::npos,body.find("\nM00\n",boundary+1));
    EXPECT_LT(body.find(second.at("setupId").To<Text>()),boundary);EXPECT_GT(body.find(placement.at("setupId").To<Text>()),boundary);
    EXPECT_NE(Text::npos,body.substr(boundary).find("FRAME/SETUP\n$$ PATH 1"));
    char* exportPath=nullptr;std::size_t exportPathLength=0;_dupenv_s(&exportPath,&exportPathLength,"ICAX_CUTPOINT_TEST_PROJECT");
    const std::unique_ptr<char,decltype(&std::free)> ownedExportPath(exportPath,&std::free);
    if(const auto destination=ownedExportPath.get()){
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="双装夹曲线与模态 IJK 验收";
        file.Save(std::filesystem::path(destination),info,scene.Database(),scene.Resources());
    }
    auto disable=GenerateRequest(scene);disable.erase("setupId");disable["placementId"]=second.at("placementId");disable["action"]=Text("disable");
    Invoke(&scene,"CamSetup","Manage",disable);ready["expected"]=Read(scene).at("expected");
    const auto single=Invoke(&scene,"CamSetup","ExportProgram",ready);
    EXPECT_EQ(1u,single.at("setupCount").To<UInt>());EXPECT_EQ(Text::npos,single.at("content").To<Text>().find("\nM00\n"));
}
TEST(FiveAxisSetups, SharedGeometryIndependentPoseLockAndDeletion)
{
    Scene scene;File source;const auto first=Imported(scene,source);
    auto create=ActivateRequest(scene,first);create["action"]=Text("create");create["name"]=Text("背面装夹");create["machineType"]=Text("HeadHead");
    const auto second=Invoke(&scene,"CamSetup","Manage",create);
    EXPECT_EQ(second,Invoke(&scene,"CamSetup","Manage",create));
    EXPECT_EQ(1u,Read(scene).at("workpieces").To<Array>().size());EXPECT_EQ(3u,Read(scene).at("placements").To<Array>().size());
    EXPECT_EQ(first.at("workpieceId"),second.at("workpieceId"));
    auto lock=ActivateRequest(scene,first);lock["locked"]=true;Invoke(&scene,"CamSetup","SetLocked",lock);
    EXPECT_THROW(Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,first)),std::exception);
    EXPECT_NO_THROW(Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,second)));
    auto geometry=Read(scene).at("workpieces").To<Array>().front().To<Map>().at("geometryRef");
    auto scale=ActivateRequest(scene,second);scale["setupId"]=second.at("setupId");scale["sourceGeometry"]=geometry;scale["factors"]=Array{2.,2.,2.};scale["center"]=Array{0.,0.,0.};
    EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Scale",scale),std::exception);
    lock=ActivateRequest(scene,first);lock["locked"]=false;Invoke(&scene,"CamSetup","SetLocked",lock);
    scale["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());scale["expected"]=Read(scene).at("expected");
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamWorkpiece","Scale",scale)).at("status").To<Text>());
    const auto state=Read(scene);const auto preview=state.at("workpieces").To<Array>().front().To<Map>().at("previewRef").To<Map>();
    for(const auto& v:state.at("placements").To<Array>()){
        const auto p=v.To<Map>();const auto id=*ID::from_string(p.at("id").To<Text>());
        const auto render=scene.Database().GetEntity(id)->GetComponent("CRenderInstanceComponent");
        EXPECT_EQ(preview.at("url"),render->GetProperty("GeometryResourceID"));
    }
    auto remove=ActivateRequest(scene,first);remove["setupId"]=first.at("setupId");Invoke(&scene,"CamWorkpiece","Remove",remove);
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());EXPECT_TRUE(Read(scene).at("placements").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(state.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(3u,Read(scene).at("placements").To<Array>().size());
}
TEST(FiveAxisSetups, LocatingPointsAreExactSetupCoordinatesAndPersistWithoutMachineMeasurement)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40.,60.,20.).Shape());const auto part=Imported(scene,source);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,part));
    const auto catalog=Invoke(&scene,"CamScene","ReadSnapFeatures",{{"expected",Read(scene).at("expected")},{"setupId",part.at("setupId")},{"objectId",part.at("placementId")}}).at("features").To<Array>();
    Array queries;std::vector<std::array<double,3>> points;
    for(const auto& value:catalog){const auto f=value.To<Map>();if(f.at("mode").To<Text>()!="endpoint")continue;
        const auto xyz=f.at("point").To<Array>();std::array<double,3> p{xyz[0].To<double>(),xyz[1].To<double>(),xyz[2].To<double>()};
        bool accept=points.empty();if(points.size()==1){double length=0;for(int i=0;i<3;++i)length+=(p[i]-points[0][i])*(p[i]-points[0][i]);accept=length>1.;}
        if(points.size()==2){std::array<double,3>a,b;for(int i=0;i<3;++i){a[i]=p[i]-points[0][i];b[i]=points[1][i]-points[0][i];}double c=0;for(int i=0;i<3;++i){const double v=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];c+=v*v;}accept=c>1.;}
        if(accept){queries.emplace_back(Map{{"frame",Text("Workpiece")},{"mode",Text("endpoint")},{"featureId",f.at("featureId")}});points.push_back(p);}if(queries.size()==3)break;
    }
    ASSERT_EQ(3u,queries.size());auto request=ActivateRequest(scene,part);request["queries"]=queries;
    const auto plan=Invoke(&scene,"CamSetup","SetRegistrationPlan",request);EXPECT_EQ(plan,Invoke(&scene,"CamSetup","SetRegistrationPlan",request));
    EXPECT_EQ("Prepared",plan.at("status").To<Text>());EXPECT_EQ("Unmeasured",plan.at("machineStatus").To<Text>());
    const auto matrix=PlacementRequest(scene,part).at("transform").To<Array>();
    for(int i=0;i<3;++i){const auto xyz=plan.at("points").To<Array>()[i].To<Map>().at("point").To<Array>();for(int j=0;j<3;++j){double v=matrix[j*4+3].To<double>();for(int k=0;k<3;++k)v+=matrix[j*4+k].To<double>()*points[i][k];EXPECT_NEAR(v,xyz[j].To<double>(),1e-8);}}
    const auto exported=Invoke(&scene,"CamSetup","ExportRegistrationPlan",{{"expected",Read(scene).at("expected")},{"setupId",part.at("setupId")}});
    const auto document=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(exported.at("content").To<Text>()).To<Map>();EXPECT_EQ("Setup",document.at("coordinateFrame").To<Text>());EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("points")),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(document.at("points")));
    auto bad=ActivateRequest(scene,part);bad["queries"]=Array{queries[0],queries[0],queries[2]};EXPECT_THROW(Invoke(&scene,"CamSetup","SetRegistrationPlan",bad),std::exception);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="装夹三点定位";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(plan,SetupState(reopened,part.at("setupId")).at("registrationPlan").To<Map>());
    auto move=PlacementRequest(scene,part);auto m=move.at("transform").To<Array>();m[3]=126.;move["transform"]=m;Invoke(&scene,"CamPlacement","SetTransform",move);
    EXPECT_EQ("Stale",SetupState(scene,part.at("setupId")).at("registrationPlan").To<Map>().at("status").To<Text>());
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportRegistrationPlan",{{"expected",Read(scene).at("expected")},{"setupId",part.at("setupId")}}),std::exception);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(plan,SetupState(scene,part.at("setupId")).at("registrationPlan").To<Map>());
    std::filesystem::remove(path);
}

TEST(FiveAxisSetups, ResetObjectFrameKeepsSharedGeometryAndOtherSetupWorldPose)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40.,60.,20.).Shape());const auto first=Imported(scene,source);
    auto create=ActivateRequest(scene,first);create["action"]=Text("create");create["name"]=Text("第二次装夹");create["machineType"]=Text("HeadHead");const auto second=Invoke(&scene,"CamSetup","Manage",create);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,second));
    auto state=Read(scene);auto ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();
    const auto old=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(old);const auto vertex=old->Vertices.front().Position;
    const auto features=Invoke(&scene,"CamScene","ReadSnapFeatures",{{"expected",state.at("expected")},{"setupId",first.at("setupId")},{"objectId",first.at("placementId")}});
    Map query;for(const auto& v:features.at("features").To<Array>()){auto f=v.To<Map>();if(f.at("mode").To<Text>()=="plane"){query={{"featureId",f.at("featureId")},{"mode",Text("plane")},{"frame",Text("Workpiece")}};break;}}
    ASSERT_FALSE(query.empty());auto request=ActivateRequest(scene,first);request["setupId"]=first.at("setupId");request["resetWorkpieceFrame"]=true;
    request["registration"]=Map{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},
        {"steps",Array{Map{{"kind",Text("plane")},{"source",query},{"target",Map{{"axis",Text("Y")}}},{"contact",true}}}}};
    Invoke(&scene,"CamPlacement","ApplyRegistration",request);const auto after=Read(scene);EXPECT_EQ(1u,after.at("workpieces").To<Array>().size());
    ref=after.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(geometry);
    const auto p=geometry->Vertices.front().Position;Array matrix;for(const auto& v:after.at("placements").To<Array>()){auto placement=v.To<Map>();EXPECT_EQ(first.at("workpieceId"),placement.at("workpieceId"));if(placement.at("id")==second.at("placementId"))matrix=placement.at("transform").To<Array>();}
    ASSERT_EQ(16u,matrix.size());const auto original=PlacementRequest(scene,second).at("transform").To<Array>();
    for(int i=0;i<3;++i){const double before=original[i*4].To<double>()*vertex.X+original[i*4+1].To<double>()*vertex.Y+original[i*4+2].To<double>()*vertex.Z+original[i*4+3].To<double>();
        const double now=matrix[i*4].To<double>()*p.X+matrix[i*4+1].To<double>()*p.Y+matrix[i*4+2].To<double>()*p.Z+matrix[i*4+3].To<double>();EXPECT_NEAR(before,now,1e-7);}
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(state.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(state.at("placements"),Read(scene).at("placements"));
}
