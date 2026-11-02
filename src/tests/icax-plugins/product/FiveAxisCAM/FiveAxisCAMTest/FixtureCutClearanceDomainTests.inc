namespace {
Map CuttingFixtureRecipe(Scene& scene){
    auto recipe=PlateRecipe(scene);const auto work=Read(scene).at("workpieces").To<Array>().front().To<Map>(),ref=work.at("geometryRef").To<Map>();
    const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto analysis=scene.Services().Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>();Array faces;
    for(const auto& face:model->Faces){const auto analyzed=analysis->AnalyzeFace(*model,face.Id,{});if(!analyzed.Planar)continue;
        if(std::any_of(analyzed.Boundaries.begin(),analyzed.Boundaries.end(),[](const auto& b){return b.RegionRelation=="InnerLoop"&&std::abs(b.Center[2])<1e-6;}))faces.emplace_back(std::to_string(face.Id));}
    if(faces.empty())throw std::runtime_error("Cutting fixture test bottom face missing");recipe["supportFaces"]=faces;
    recipe["family"]=Text("ParallelY");recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",-15.}},Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",15.}}};
    recipe["cutClearanceMode"]=Text("Toolpath");recipe["cutClearanceMm"]=2.;recipe["beamRadiusMm"]=.5;return recipe;
}
Map CuttingFixtureUnit(Scene& scene){return Read(scene).at("fixtureUnits").To<Array>().front().To<Map>();}
Map CuttingFixtureUnitRequest(Scene& scene){auto request=FixtureRequest(scene);request["unitId"]=CuttingFixtureUnit(scene).at("id");return request;}
Text CuttingFixtureValidity(Scene& scene){return CuttingFixtureUnit(scene).at("design").To<Map>().at("assembly").To<Map>().at("cutClearance").To<Map>().at("validity").To<Text>();}
}
TEST(FiveAxisFixtureCutClearance, ActualProcessSweepsPreserveSolidBaseAndSurviveAdoptionUndoReopen){
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    ASSERT_EQ(Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>(),"Completed");
    const auto generated=Generated(scene);SetPlateRecipe(scene,CuttingFixtureRecipe(scene));const auto candidate=GeneratePlates(scene),clearance=candidate.at("cutClearance").To<Map>();
    EXPECT_EQ(clearance.at("status").To<Text>(),"Clear");EXPECT_EQ(clearance.at("scope").To<Text>(),"CutAndLeadsOnly");EXPECT_EQ(clearance.at("rapidStatus").To<Text>(),"Unchecked");
    EXPECT_EQ(clearance.at("operationCount").To<UInt>(),2u);EXPECT_GT(clearance.at("beamSweepCells").To<UInt>(),0u);EXPECT_GT(clearance.at("headSweepCells").To<UInt>(),0u);
    EXPECT_EQ(clearance.at("beamScope").To<Text>(),"VerticalPlates");
    Map base;for(const auto& value:candidate.at("parts").To<Array>())if(value.To<Map>().at("role").To<Text>()=="Base")base=value.To<Map>();ASSERT_FALSE(base.empty());ASSERT_TRUE(base.at("holes").To<Array>().empty());
    EXPECT_EQ(base.at("keepOutReliefCount").To<UInt>(),0u);const auto solid=JointPartShape(scene,base);const auto baseFrame=base.at("frame").To<Array>();const auto z=baseFrame[11].To<double>()+base.at("thickness").To<double>()/2;
    // The beam can geometrically continue onto the solid base. It must not
    // cause an optical hole or reject generation; physical head avoidance stays.
    size_t checked=0;const auto state=Read(scene);
    for(const auto& operation:generated.at("operations").To<Array>()){const auto op=operation.To<Map>();Array transform;
        for(const auto& p:state.at("placements").To<Array>())if(p.To<Map>().at("id")==op.at("placementId"))transform=p.To<Map>().at("transform").To<Array>();ASSERT_EQ(transform.size(),16u);
        for(const auto& pv:op.at("parts").To<Array>()){const auto points=pv.To<Map>().at("points").To<Array>();for(size_t i=0;i+1<points.size();++i)for(double t:{0.,.5,1.}){
            const auto a=points[i].To<Array>(),b=points[i+1].To<Array>();std::array<double,3> p{};for(int row=0;row<3;++row){p[row]=transform[row*4+3].To<double>();for(int col=0;col<3;++col)p[row]+=transform[row*4+col].To<double>()*(a[col].To<double>()+(b[col].To<double>()-a[col].To<double>())*t);}
            EXPECT_EQ(JointState(solid,gp_Pnt(p[0],p[1],z)),TopAbs_IN);++checked;
        }}
    }EXPECT_GT(checked,20u);
    AdoptPlates(scene,candidate);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");const auto accepted=Read(scene).at("fixtures");
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(Read(scene).at("fixtures"),accepted);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    Invoke(&scene,"CamFixture","Breakdown",CuttingFixtureUnitRequest(scene));const auto unitBefore=CuttingFixtureUnit(scene);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Cut-aware fixture";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(unitBefore,CuttingFixtureUnit(reopened));EXPECT_EQ(CuttingFixtureValidity(reopened),"Current");std::filesystem::remove(path);
}
TEST(FiveAxisFixtureCutClearance, CutHeadAndPlacementChangesInvalidateFrozenUnitAndBlockDelivery){
    const auto stage=[&](const char* value){RecordProperty("last_stage",value);std::cout<<"Fixture invalidation stage: "<<value<<std::endl;};
    stage("prepare policy pair");Scene scene;const auto ids=ConfirmPolicyPair(scene);
    stage("zero generation policies");ZeroGenerationPolicies(scene,ids);
    stage("set cutting fixture recipe");SetPlateRecipe(scene,CuttingFixtureRecipe(scene));
    stage("generate cutting fixture");const auto candidate=GeneratePlates(scene);
    stage("adopt cutting fixture");AdoptPlates(scene,candidate);
    stage("initial breakdown");
    Invoke(&scene,"CamFixture","Breakdown",CuttingFixtureUnitRequest(scene));EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    const auto unitID=CuttingFixtureUnit(scene).at("id").To<Text>();
    stage("rename cut");
    const auto renamed=Invoke(&scene,"CamCut","Rename",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",ids[0]},{"name",Text("Renamed clearance cut")}}).at("statePatch").To<Map>();
    EXPECT_EQ(renamed.at("expected"),Read(scene).at("expected"));
    const auto renameValidity=renamed.at("fixtureValidity").To<Map>().at("units").To<Map>().at(unitID).To<Map>().at("assembly").To<Map>();
    EXPECT_EQ(renameValidity.at("id"),candidate.at("id"));EXPECT_EQ(renameValidity.at("validity").To<Text>(),"Current");EXPECT_EQ(renameValidity.at("cutClearance").To<Map>().at("status").To<Text>(),"Clear");EXPECT_FALSE(renameValidity.contains("parts"));
    const auto reject=[&]{EXPECT_EQ(CuttingFixtureValidity(scene),"Stale");
        EXPECT_THROW(AdoptPlates(scene,candidate),std::exception);
        EXPECT_THROW(Invoke(&scene,"CamFixture","Breakdown",CuttingFixtureUnitRequest(scene)),std::exception);
        auto request=CuttingFixtureUnitRequest(scene);const auto directory=std::filesystem::temp_directory_path().u8string();request["destination"]=Text(reinterpret_cast<const char*>(directory.data()),directory.size());EXPECT_THROW(Invoke(&scene,"CamFixture","Export",request),std::exception);};
    stage("reverse cut");Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));reject();stage("undo reversed cut");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    stage("change tool axis");
    const auto posePatch=Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[0],"Fixed",{0.,.1,1.},"Setup")).at("statePatch").To<Map>();
    EXPECT_EQ(posePatch.at("expected"),Read(scene).at("expected"));const auto poseValidity=posePatch.at("fixtureValidity").To<Map>().at("units").To<Map>().at(unitID).To<Map>().at("assembly").To<Map>();
    EXPECT_EQ(poseValidity.at("id"),candidate.at("id"));EXPECT_EQ(poseValidity.at("validity").To<Text>(),"Stale");EXPECT_EQ(poseValidity.at("cutClearance").To<Map>().at("status").To<Text>(),"Unchecked");
    reject();stage("undo changed tool axis");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    stage("change lead policy");Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"leadIn",Lead("Line",.2)}}}}));reject();stage("undo changed lead policy");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    stage("import cutting head");File head(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),2,2,4).Shape());auto importRequest=GenerateRequest(scene);importRequest["sourcePath"]=head.Utf8();ASSERT_EQ(Wait(scene,Invoke(&scene,"CamHead","Import",importRequest)).at("status").To<Text>(),"Completed");reject();stage("undo imported cutting head");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    stage("move workpiece placement");
    const auto placement=Read(scene).at("placements").To<Array>().front().To<Map>();auto request=FixtureRequest(scene);request["placementId"]=placement.at("id");auto matrix=placement.at("transform").To<Array>();matrix[3]=matrix[3].To<double>()+1.;request["transform"]=matrix;
    Invoke(&scene,"CamPlacement","SetTransform",request);reject();stage("undo moved workpiece placement");ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");
    stage("reject generated unit transform");
    auto move=CuttingFixtureUnitRequest(scene);auto unitMatrix=CuttingFixtureUnit(scene).at("transform").To<Array>();unitMatrix[3]=3.;move["transform"]=unitMatrix;
    ExpectFixedGeneratedFixtureTransform(scene,"UnitUpdate",move);EXPECT_EQ(CuttingFixtureValidity(scene),"Current");Invoke(&scene,"CamFixture","Breakdown",CuttingFixtureUnitRequest(scene));
}
TEST(FiveAxisFixtureCutClearance, NoCutsAndInvalidPolicyCannotProduceAQualifiedCandidate){
    Scene scene;File sheet(FixtureCurvedSheet());Imported(scene,sheet);auto recipe=PlateRecipe(scene);recipe["cutClearanceMode"]=Text("Toolpath");recipe["cutClearanceMm"]=2.;recipe["beamRadiusMm"]=.5;SetPlateRecipe(scene,recipe);
    EXPECT_THROW(GeneratePlates(scene),std::exception);EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());const auto before=Read(scene).at("setups");
    for(double invalid:{-.1,0.,101.}){auto bad=recipe;bad["cutClearanceMm"]=invalid;EXPECT_THROW(SetPlateRecipe(scene,bad),std::exception);EXPECT_EQ(Read(scene).at("setups"),before);}
    auto bad=recipe;bad["cutClearanceMode"]=Text("CadHolesOnly");EXPECT_THROW(SetPlateRecipe(scene,bad),std::exception);EXPECT_EQ(Read(scene).at("setups"),before);
}
TEST(FiveAxisFixtureCutClearance, TiltedActualBeamReachesRemoteAutomaticCircularBase){
    Scene scene;const auto box=BRepPrimAPI_MakeBox(gp_Pnt(-600,-110,0),1200,220,10).Shape();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,100,-1),gp::DZ()),3,12).Shape();File part(BRepAlgoAPI_Cut(box,hole).Shape());
    const auto placement=Imported(scene,part);const auto id=ConfirmWithPolicies(scene,0.,&placement,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{0.,4.15,1.},"Setup"));
    ASSERT_EQ(Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>(),"Completed");const auto generated=Generated(scene);
    auto recipe=CuttingFixtureRecipe(scene);recipe["family"]=Text("ParallelX");recipe["baseShape"]=Text("Circle");recipe["baseDrop"]=150.;
    recipe["stations"]=Array{Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",-300.}},Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",300.}}};
    SetPlateRecipe(scene,recipe);const auto candidate=GeneratePlates(scene);Map base;
    for(const auto& value:candidate.at("parts").To<Array>())if(value.To<Map>().at("role").To<Text>()=="Base")base=value.To<Map>();ASSERT_FALSE(base.empty());
    EXPECT_GT(base.at("keepOutReliefCount").To<UInt>(),0u);const auto solid=JointPartShape(scene,base);const auto z=base.at("frame").To<Array>()[11].To<double>()+base.at("thickness").To<double>()/2;
    const auto state=Read(scene);size_t remoteChecks=0;
    for(const auto& value:generated.at("operations").To<Array>()){const auto op=value.To<Map>();Array transform;
        for(const auto& p:state.at("placements").To<Array>())if(p.To<Map>().at("id")==op.at("placementId"))transform=p.To<Map>().at("transform").To<Array>();ASSERT_EQ(transform.size(),16u);
        for(const auto& pv:op.at("parts").To<Array>()){const auto partMap=pv.To<Map>();const auto points=partMap.at("points").To<Array>(),directions=partMap.at("directions").To<Array>();
            for(size_t i=0;i<points.size();++i){const auto local=points[i].To<Array>(),axis=directions[i].To<Array>();std::array<double,3> p{},d{};
                for(int row=0;row<3;++row){p[row]=transform[row*4+3].To<double>();for(int col=0;col<3;++col){p[row]+=transform[row*4+col].To<double>()*local[col].To<double>();d[row]+=transform[row*4+col].To<double>()*axis[col].To<double>();}}
                ASSERT_GT(d[2],0.);const auto distance=(p[2]-z)/d[2];const gp_Pnt beam(p[0]-d[0]*distance,p[1]-d[1]*distance,z);
                EXPECT_EQ(JointState(solid,beam),TopAbs_OUT);if(beam.Y()<-500.)++remoteChecks;
            }
        }
    }EXPECT_GT(remoteChecks,10u);
}
