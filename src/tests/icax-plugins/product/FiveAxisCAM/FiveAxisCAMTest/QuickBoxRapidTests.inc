namespace quick_box_tests {
    namespace M=iCAX::GeometryServices::RigidMotion;
    void Prepare(Scene& scene){
        PrepareOrderPart(scene,false);const auto sources=InstalledHeadSources();BindBuiltinHeadSources(scene,sources);bool selected=false;
        for(const auto& value:sources){const auto item=value.To<Map>();if(std::filesystem::path(item.at("sourcePath").To<Text>()).stem()=="GAC-3015"){SelectInstalledHead(scene,item);selected=true;break;}}
        ASSERT_TRUE(selected);GenerateAxis(scene);
    }
    M::Vector Point(const Variant& value){const auto p=value.To<Array>();return {p[0].To<double>(),p[1].To<double>(),p[2].To<double>()};}
    Map Settings(Scene& scene){return Read(scene).at("setups").To<Array>().front().To<Map>().at("rapidSettings").To<Map>();}
    void Configure(Scene& scene,const Map& settings){auto request=GenerateRequest(scene);request["settings"]=settings;Invoke(&scene,"CamRapid","Configure",request);}
    void Check(const Map& result){
        ASSERT_TRUE(result.contains("quickMotion"));EXPECT_EQ("QuickRetract",result.at("planningMethod").To<Text>());
        const auto motion=result.at("quickMotion").To<Map>();const auto points=result.at("points").To<Array>(),directions=result.at("directions").To<Array>();ASSERT_GE(points.size(),4u);ASSERT_EQ(points.size(),directions.size());
        const auto normal=motion.at("normalRetractDistanceMm").To<double>(),side=motion.at("sideRetractDistanceMm").To<double>(),z=motion.at("safeZMm").To<double>();
        const auto start=Point(points.front()),end=Point(points.back()),a=Point(points[1]),b=Point(points[points.size()-2]),ds=Point(directions.front()),de=Point(directions.back());
        EXPECT_LT(M::Length(M::Sub(a,M::Add(start,M::Scale(ds,normal)))),1e-7);EXPECT_LT(M::Length(M::Sub(b,M::Add(end,M::Scale(de,normal)))),1e-7);
        EXPECT_EQ(directions.front(),directions[1]);EXPECT_EQ(directions.back(),directions[directions.size()-2]);
        auto exit=a,entry=b;const auto sf=std::stoi(motion.at("startFace").To<Text>()),ef=std::stoi(motion.at("endFace").To<Text>());
        if(sf/2<2)exit[sf/2]+=(sf%2?1:-1)*side;if(ef/2<2)entry[ef/2]+=(ef%2?1:-1)*side;
        bool exitRaw=false,exitNeutral=false,entryRaw=false,entryNeutral=false,atTop=false;const M::Vector neutral{0,0,1};
        for(std::size_t i=1;i+1<points.size();++i){const auto p=Point(points[i]),d=Point(directions[i]);EXPECT_LE(p[2],z+1e-7);
            if(M::Length(M::Sub(p,exit))<1e-7){exitRaw|=M::Length(M::Sub(d,ds))<1e-7;exitNeutral|=M::Length(M::Sub(d,neutral))<1e-7;}
            if(M::Length(M::Sub(p,entry))<1e-7){entryRaw|=M::Length(M::Sub(d,de))<1e-7;entryNeutral|=M::Length(M::Sub(d,neutral))<1e-7;}
            atTop|=std::abs(p[2]-z)<1e-7;
        }EXPECT_TRUE(exitRaw&&exitNeutral&&entryRaw&&entryNeutral&&atTop);
        for(std::size_t i=1;i<points.size();++i){const auto p=Point(points[i-1]),q=Point(points[i]),d=Point(directions[i]);const bool moved=M::Length(M::Sub(p,q))>1e-7,rotates=M::Length(M::Sub(d,Point(directions[i-1])))>1e-7;
            if(rotates)EXPECT_FALSE(moved)<<"Rotate only after retracting, in place";
            if(i>1&&i+1<points.size()){
                int changes=0;for(int j=0;j<3;++j)changes+=std::abs(p[j]-q[j])>1e-7;EXPECT_LE(changes,1)<<"Only the first and last normal-axis segments may be diagonal";
                if(std::abs(p[2]-q[2])>1e-7)EXPECT_LT(M::Length(M::Sub(d,neutral)),1e-7)<<"Lift and descend with the torch neutral";
                if(moved&&(std::abs(p[2]-z)<1e-7&&std::abs(q[2]-z)<1e-7))EXPECT_LT(M::Length(M::Sub(d,neutral)),1e-7);
            }
        }
    }
}
TEST(FiveAxisQuickBox, FastGenerationUsesOnlyNormalSideAndZParametersWithoutHeadPreparation)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);const auto batch=fast_rapid_tests::Batch(scene);fast_rapid_tests::ExpectUncheckedGeometry(batch);const auto settings=quick_box_tests::Settings(scene);
    for(const auto& [_,value]:batch.at("connections").To<Map>()){const auto result=value.To<Map>().at("result").To<Map>();quick_box_tests::Check(result);const auto motion=result.at("quickMotion").To<Map>();for(const Text key:{"normalRetractDistanceMm","sideRetractDistanceMm","safeZMm"})EXPECT_EQ(settings.at(key),motion.at(key));}
    EXPECT_EQ(0.,batch.at("performance").To<Map>().at("meshPreparationMs").To<double>());
}
TEST(FiveAxisQuickBox, ParameterChangesInvalidateCachedRoutesAndPreserveCAD)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);fast_rapid_tests::Batch(scene);const auto before=Read(scene),old=Generated(scene);auto settings=quick_box_tests::Settings(scene);
    settings["normalRetractDistanceMm"]=3.5;settings["sideRetractDistanceMm"]=17.25;settings["safeZMm"]=80.;quick_box_tests::Configure(scene,settings);const auto batch=fast_rapid_tests::Batch(scene);EXPECT_NE("0",batch.at("calculatedCount").To<Text>());
    for(const auto& [_,value]:batch.at("connections").To<Map>())quick_box_tests::Check(value.To<Map>().at("result").To<Map>());
    const auto after=Read(scene);EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));EXPECT_EQ(before.at("placements"),after.at("placements"));EXPECT_EQ(old.at("operations"),Generated(scene).at("operations"));
}
TEST(FiveAxisQuickBox, AllTwentyFiveFacePairsRetractAlongActualAxisThenSideThenNeutralThenZ)
{
    namespace M=iCAX::GeometryServices::RigidMotion;
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);const auto ids=OrderedIDs(Generated(scene));ASSERT_GE(ids.size(),2u);Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    for(int from:{0,1,2,3,5})for(int to:{0,1,2,3,5}){
        SCOPED_TRACE(std::to_string(from)+" -> "+std::to_string(to));
        const auto direction=[](int face){M::Vector d{.1,.12,.08};d[face/2]=face%2?1.:-1.;d=M::Unit(d);return Array{d[0],d[1],d[2]};};
        Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[0],"Fixed",direction(from),"Workpiece"));Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[1],"Fixed",direction(to),"Workpiece"));GenerateAxis(scene);
        const auto batch=fast_rapid_tests::Batch(scene);bool found=false;for(const auto& [_,value]:batch.at("connections").To<Map>()){const auto record=value.To<Map>();if(record.at("from")!=ids[0]||record.at("to")!=ids[1])continue;found=true;const auto result=record.at("result").To<Map>();quick_box_tests::Check(result);const auto motion=result.at("quickMotion").To<Map>();EXPECT_EQ(std::to_string(from),motion.at("startFace").To<Text>());EXPECT_EQ(std::to_string(to),motion.at("endFace").To<Text>());}EXPECT_TRUE(found);
    }
}
TEST(FiveAxisQuickBox, CollisionCheckNeverChangesThePrescribedDistancesHeightOrNeutralOrientation)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);const auto fast=fast_rapid_tests::Batch(scene);auto request=RapidRequest(scene,false);request["safe"]=true;
    const auto task=Wait(scene,Invoke(&scene,"CamRapid","Generate",request));ASSERT_EQ("Completed",task.at("status").To<Text>());const auto batch=task.at("result").To<Map>();EXPECT_EQ("CollisionChecked",batch.at("generation").To<Text>());
    for(const auto& [pair,value]:batch.at("connections").To<Map>()){const auto result=value.To<Map>().at("result").To<Map>();quick_box_tests::Check(result);const auto original=fast.at("connections").To<Map>().at(pair).To<Map>().at("result").To<Map>();EXPECT_EQ(original.at("points"),result.at("points"));EXPECT_EQ(original.at("directions"),result.at("directions"));if(result.at("geometryStatus").To<Text>()=="Generated")EXPECT_EQ("Clear",result.at("validation").To<Text>());}
}
TEST(FiveAxisQuickBox, RejectsNonpositiveRetractionAndSafetyHeightBelowRetractedPoses)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);const auto original=quick_box_tests::Settings(scene);auto settings=original;settings["normalRetractDistanceMm"]=0.;auto request=GenerateRequest(scene);request["settings"]=settings;EXPECT_THROW(Invoke(&scene,"CamRapid","Configure",request),std::exception);EXPECT_EQ(original,quick_box_tests::Settings(scene));
    settings=original;settings["safeZMm"]=-100.;quick_box_tests::Configure(scene,settings);EXPECT_THROW(Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)),std::exception);
}
TEST(FiveAxisQuickBox, UnassignedDefaultHeadGeneratesTiltedRapidsWithoutCollisionSource)
{
    Scene scene;PrepareOrderPart(scene);const auto head=Head(scene);EXPECT_FALSE(head.contains("collisionSource"));
    const auto before=Read(scene);const auto ids=OrderedIDs(Generated(scene));ASSERT_GE(ids.size(),2u);
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[0],"Fixed",Array{.8,0.,.6},"Workpiece"));
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[1],"Fixed",Array{0.,-.8,.6},"Workpiece"));GenerateAxis(scene);
    const auto batch=fast_rapid_tests::Batch(scene);fast_rapid_tests::ExpectUncheckedGeometry(batch);
    for(const auto& [_,value]:batch.at("connections").To<Map>())quick_box_tests::Check(value.To<Map>().at("result").To<Map>());
    const auto after=Read(scene);EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));EXPECT_EQ(before.at("placements"),after.at("placements"));
    EXPECT_FALSE(Head(scene).contains("collisionSource"));
}
TEST(FiveAxisQuickBox, SelectedRigidHeadDoesNotAffectGeometryAndOnlyConstrainsCollisionChecks)
{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));ASSERT_FALSE(ids.empty());
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[0],"Fixed",Array{.8,0.,.6},"Workpiece"));GenerateAxis(scene);
    const auto original=fast_rapid_tests::Batch(scene);const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const Map box{{"pose",Array{0.,0.,.01,0.,0.,0.}},{"geometry",Map{{"type",Text("box")},{"size",Array{.001,.001,.001}}}},{"enabled",true}};
    SetProjectTestHead(scene,setup,Map{{"kind",Text("Builtin")},{"name",Text("Selected rigid collision head")},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}},{"collisionSource",Text("MachineDefinition")},{"collisionBoxes",Array{box}}});
    EXPECT_EQ("Rigid",Head(scene).at("motionType").To<Text>());const auto repeated=fast_rapid_tests::Batch(scene);fast_rapid_tests::ExpectUncheckedGeometry(repeated);EXPECT_EQ("0",repeated.at("calculatedCount").To<Text>());EXPECT_EQ(original.at("connections"),repeated.at("connections"));
    Text reason;try{Invoke(&scene,"CamRapid","Preview",RapidRequest(scene));ADD_FAILURE()<<"Only collision checking may enforce the selected rigid head";}catch(const std::exception& error){reason=error.what();}
    EXPECT_NE(Text::npos,reason.find("CAM_HEAD_FIXED_AXIS_REQUIRED"))<<reason;
}
TEST(FiveAxisQuickBox, ChangingHeadInvalidatesCollisionEvidenceWhileKeepingGeneratedGeometry)
{
    Scene scene;PrepareOrderPart(scene);fast_rapid_tests::Batch(scene);const auto request=RapidRequest(scene);
    const auto preview=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",preview.at("status").To<Text>());
    auto apply=GenerateRequest(scene);apply["taskId"]=preview.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);
    const auto original=fast_rapid_tests::Row(scene,request.at("from"),request.at("to"));ASSERT_EQ("Clear",original.at("validation").To<Text>());
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const Map box{{"pose",Array{0.,0.,.01,0.,0.,0.}},{"geometry",Map{{"type",Text("box")},{"size",Array{.001,.001,.001}}}},{"enabled",true}};
    SetProjectTestHead(scene,setup,Map{{"kind",Text("Builtin")},{"name",Text("Changed collision head")},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}},{"collisionSource",Text("MachineDefinition")},{"collisionBoxes",Array{box}}});
    const auto changed=fast_rapid_tests::Row(scene,request.at("from"),request.at("to"));EXPECT_EQ("Generated",changed.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",changed.at("validation").To<Text>());EXPECT_EQ(original.at("inputKey"),changed.at("inputKey"));EXPECT_EQ(original.at("points"),changed.at("points"));
    const auto repeated=fast_rapid_tests::Batch(scene);EXPECT_EQ("0",repeated.at("calculatedCount").To<Text>());EXPECT_EQ("Unchecked",fast_rapid_tests::Row(scene,request.at("from"),request.at("to")).at("validation").To<Text>());
    auto checked=RapidRequest(scene,false);checked["safe"]=true;const auto terminal=Wait(scene,Invoke(&scene,"CamRapid","Generate",checked));ASSERT_EQ("Completed",terminal.at("status").To<Text>());EXPECT_NE("0",terminal.at("result").To<Map>().at("calculatedCount").To<Text>());EXPECT_EQ("Clear",fast_rapid_tests::Row(scene,request.at("from"),request.at("to")).at("validation").To<Text>());
}
TEST(FiveAxisQuickBox, ActualMachineMotionKeepsTheOrthogonalRouteAndItsFixedDirections)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);quick_box_tests::Prepare(scene);CreateProductPlanMachine(scene);GenerateAxis(scene);fast_rapid_tests::Batch(scene);
    const auto task=WaitMachinePlan(scene,Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene)));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Ready",result.at("status").To<Text>())<<result.at("reason").To<Text>();
    Map previous;std::size_t rapidFrames=0;
    for(const auto& value:result.at("frames").To<Array>()){
        const auto frame=value.To<Map>();if(frame.at("role").To<Text>()!="Rapid"){previous.clear();continue;}++rapidFrames;
        ASSERT_TRUE(frame.contains("rapidDirectionLocked"));EXPECT_TRUE(frame.at("rapidDirectionLocked").To<bool>());
        const auto beam=quick_box_tests::Point(frame.at("actualBeamDirection"));EXPECT_NEAR(0,beam[0],1e-7);EXPECT_NEAR(0,beam[1],1e-7);EXPECT_NEAR(-1,beam[2],1e-7);
        if(!previous.empty()&&previous.at("intentId")==frame.at("intentId")){
            const auto a=previous.at("actualToolPose").To<Array>(),b=frame.at("actualToolPose").To<Array>();int changes=0;
            for(const auto j:{3,7,11})changes+=std::abs(a[j].To<double>()-b[j].To<double>())>1e-5;EXPECT_LE(changes,1)<<"Machine interpolation must not blend the orthogonal corners";
        }previous=frame;
    }EXPECT_GT(rapidFrames,0u);
}
