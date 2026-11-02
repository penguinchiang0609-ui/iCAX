TEST(FiveAxisRapidManual, EndpointXYZAndIJKBelongOnlyToThisRapidAndSurviveHistoryAndReopen)
{
    for(const Text strategy:{Text("Quick"),Text("Frog"),Text("Fly")}){
        SCOPED_TRACE(strategy);Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene);const auto cad=Read(scene).at("workpieces");
        auto read=RapidRequest(scene,true,strategy);read.erase("commandId");std::clog<<"Endpoint acceptance "<<strategy<<" read baseline"<<std::endl;const auto source=Invoke(&scene,"CamRapid","ReadEditPath",read);
        ASSERT_FALSE(source.at("endpointLocked").To<bool>());auto start=source.at("points").To<Array>().front().To<Array>(),end=source.at("points").To<Array>().back().To<Array>();
        start[0]=start[0].To<double>()+.2;end[1]=end[1].To<double>()+.3;
        const Array axis{0.,.1,std::sqrt(.99)};Map manual{{"start",Map{{"point",start},{"direction",axis}}},{"end",Map{{"point",end},{"direction",axis}}}};
        if(source.contains("construction"))manual["construction"]=source.at("construction");
        auto request=read;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["manualPath"]=manual;
        const auto history=scene.Database().GetUndoArray().size();std::clog<<"Endpoint acceptance "<<strategy<<" edit"<<std::endl;Invoke(&scene,"CamRapid","EditPath",request);const auto after=Generated(scene);const auto transitions=after.at("transitions").To<Array>();
        const auto t=transitions.front().To<Map>();EXPECT_EQ(Variant(start),t.at("points").To<Array>().front());EXPECT_EQ(Variant(end),t.at("points").To<Array>().back());EXPECT_EQ(Variant(axis),t.at("directions").To<Array>().front());EXPECT_EQ(Variant(axis),t.at("directions").To<Array>().back());
        EXPECT_EQ(before.at("operations"),after.at("operations"));EXPECT_EQ(cad,Read(scene).at("workpieces"));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
        EXPECT_EQ("Draft",t.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",t.at("validation").To<Text>());
        for(std::size_t i=1;i<transitions.size();++i){const auto old=before.at("transitions").To<Array>()[i].To<Map>(),now=transitions[i].To<Map>();for(const auto* field:{"from","to","points","directions"})if(old.contains(field))EXPECT_EQ(old.at(field),now.at(field))<<field;}
        if(source.contains("construction"))EXPECT_EQ(source.at("construction"),t.at("construction"));
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Generated(scene));
        read["expected"]=Read(scene).at("expected");const auto restored=Invoke(&scene,"CamRapid","ReadEditPath",read);EXPECT_EQ(t.at("points"),restored.at("points"));EXPECT_EQ(t.at("directions"),restored.at("directions"));ASSERT_TRUE(restored.at("endpointOverrides").To<Map>().contains("start"));
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Rapid endpoints";
        const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(after,Generated(reopened));std::filesystem::remove(path);
    }
}
TEST(FiveAxisRapidManual, ExactWaypointsAreCheckedWithoutMovingThemOrSearchingAlternatives)
{
    using namespace rapid_avoidance_tests;Scene scene;auto q=Query(scene);q.Geometry.Manual=true;
    q.Geometry.Waypoints={{{-5,5,8},{0,0,1}},{{5,5,8},{0,0,1}}};const auto original=q.Geometry.Waypoints;
    const auto service=scene.Services().Resolve<IRapidPathPlanningService>();const auto safe=service->Plan({q}).front();ASSERT_TRUE(safe.Generated)<<safe.Reason;
    EXPECT_EQ("Manual",safe.Method);EXPECT_EQ(1u,safe.Candidates);ASSERT_EQ(4u,safe.Poses.size());
    for(std::size_t i=0;i<original.size();++i){EXPECT_EQ(original[i].Point,safe.Poses[i+1].Point);EXPECT_EQ(original[i].Direction,safe.Poses[i+1].Direction);}EXPECT_EQ(2,q.Geometry.LiftHeight);IndependentCheck(scene,q,safe);
    q.Geometry.Waypoints.clear();const auto blocked=service->Plan({q}).front();EXPECT_FALSE(blocked.Generated);EXPECT_EQ(1u,blocked.Candidates);EXPECT_EQ("RAPID_MANUAL_PATH_BLOCKED",blocked.Reason);ExpectOriginalEditingRoute(scene,q,blocked);
}
TEST(FiveAxisRapidManual, StaleConnectionKeepsAuthoredGeometryAndUpdateOnlyRebindsEndpoints)
{
    for(const bool constructed:{false,true}){
        Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");
        // Existing authored Retract constructions remain editable after Quick
        // switched to a box-face Polyline without a construction descriptor.
        if(constructed)request["manualPath"]=Map{{"construction",Map{{"kind",Text("Retract")},{"followHeightMm",1.},{"liftHeightMm",10.},{"lateralOffset",Array{0.,0.,0.}}}}};
        const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);
        Map manual;
        if(constructed){auto construction=source.at("construction").To<Map>();construction["liftHeightMm"]=23.;construction["lateralOffset"]=Array{0.,7.,0.};manual={{"construction",construction}};}
        else{auto points=source.at("points").To<Array>(),directions=source.at("directions").To<Array>();auto moved=points[1].To<Array>();moved[1]=17.;moved[2]=23.;points[1]=moved;
            manual={{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};}
        request["manualPath"]=manual;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());Invoke(&scene,"CamRapid","EditPath",request);
        const auto saved=Generated(scene).at("transitions").To<Array>().front().To<Map>();
        const auto from=request.at("from");const Array shift{1.,0.,0.,.5,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};
        ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{from},{{"matrix",shift}}))).at("status").To<Text>());
        const auto stale=Generated(scene).at("transitions").To<Array>().front().To<Map>();
        EXPECT_EQ("Stale",stale.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",stale.at("validation").To<Text>());
        EXPECT_EQ(manual,stale.at("manualPath").To<Map>());EXPECT_EQ(saved.at("points"),stale.at("points"));EXPECT_EQ(saved.at("directions"),stale.at("directions"));EXPECT_TRUE(stale.at("editingOnly").To<bool>());
        // The actual metadata/resource transport must retain stale manual samples.
        const auto wire=Invoke(&scene,"CamScene","ReadState"),projected=wire.at("setups").To<Array>().front().To<Map>().at("generatedGeometry").To<Map>().at("transitions").To<Array>().front().To<Map>();
        EXPECT_TRUE(projected.contains("pointCount"));EXPECT_FALSE(projected.contains("points"));
        for(const auto& value:wire.at("intentToolpaths").To<Array>())EXPECT_EQ(0.,value.To<Map>().at("kerfWidthMm").To<double>());
        ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
        const auto before=Read(scene);request=RapidRequest(scene);request["manualPath"]=manual;
        const auto task=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
        const auto record=task.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>(),result=record.at("result").To<Map>();
        EXPECT_EQ(manual,record.at("manualPath").To<Map>());EXPECT_EQ("Manual",result.at("planningMethod").To<Text>());EXPECT_EQ("1",result.at("candidateCount").To<Text>());
        if(constructed)EXPECT_EQ(manual.at("construction"),result.at("construction"));
        else{const auto points=result.at("points").To<Array>(),directions=result.at("directions").To<Array>();EXPECT_EQ(manual.at("points").To<Array>(),Array(points.begin()+1,points.end()-1));EXPECT_EQ(manual.at("directions").To<Array>(),Array(directions.begin()+1,directions.end()-1));}
        EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
        EXPECT_EQ(manual,Generated(scene).at("transitions").To<Array>().front().To<Map>().at("manualPath").To<Map>());
    }
}
TEST(FiveAxisRapidManual, CutRevisionChangeWithIdenticalEndpointPosesDoesNotInvalidateTheConnection)
{
    Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");
    const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);const auto points=source.at("points").To<Array>(),directions=source.at("directions").To<Array>();
    const Map manual{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};
    request["manualPath"]=manual;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    const auto task=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
    auto apply=GenerateRequest(scene);apply["taskId"]=task.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);
    const auto before=Generated(scene).at("transitions").To<Array>().front().To<Map>();const auto expected=Read(scene).at("expected");
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{request.at("from")},{}));
    const auto state=Read(scene),after=Generated(scene).at("transitions").To<Array>().front().To<Map>();
    EXPECT_NE(expected,state.at("expected"));EXPECT_EQ("Current",after.at("endpointStatus").To<Text>());EXPECT_EQ("Generated",after.at("geometryStatus").To<Text>());EXPECT_EQ("Clear",after.at("validation").To<Text>());
    EXPECT_EQ(before.at("inputKey"),after.at("inputKey"));EXPECT_EQ(before.at("points"),after.at("points"));EXPECT_EQ(before.at("directions"),after.at("directions"));EXPECT_EQ(manual,after.at("manualPath").To<Map>());
    // No CamPlan.Generate or automatic rapid planning is required to keep it.
    auto check=RapidRequest(scene);check["manualPath"]=manual;const auto checked=Wait(scene,Invoke(&scene,"CamRapid","CheckPath",check));EXPECT_EQ("Completed",checked.at("status").To<Text>());
}
TEST(FiveAxisRapidManual, EditedConstructionIsCheckedOnceWithoutSearchingOrReplacingItsFormula){
    using namespace rapid_avoidance_tests;Scene scene;auto query=Query(scene);query.Geometry.Manual=true;query.Geometry.ConstructedManual=true;query.Geometry.LateralOffset={0,20,0};
    const auto exact=scene.Services().Resolve<IRapidPathGeometryService>()->Generate({query.Geometry},{}).front();ASSERT_TRUE(exact.Generated);
    const auto service=scene.Services().Resolve<IRapidPathPlanningService>();const auto safe=service->Plan({query}).front();ASSERT_TRUE(safe.Generated)<<safe.Reason;EXPECT_EQ("Manual",safe.Method);EXPECT_EQ(1u,safe.Candidates);ASSERT_EQ(exact.Poses.size(),safe.Poses.size());
    for(size_t i=0;i<exact.Poses.size();++i)EXPECT_EQ(exact.Poses[i].Point,safe.Poses[i].Point);ASSERT_TRUE(safe.Construction);EXPECT_EQ(query.Geometry.LateralOffset,safe.Construction->LateralOffset);IndependentCheck(scene,query,safe);
    query.Geometry.LateralOffset={0,0,0};const auto blocked=service->Plan({query}).front();EXPECT_FALSE(blocked.Generated);EXPECT_EQ(1u,blocked.Candidates);EXPECT_EQ("RAPID_MANUAL_PATH_BLOCKED",blocked.Reason);ExpectOriginalEditingRoute(scene,query,blocked);
}
TEST(FiveAxisRapidManual, ConstructionDraftPreviewUndoAndReopenPreserveCompactSourceAndEndpointAuthority){
    Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene),stateBefore=Read(scene);auto request=RapidRequest(scene,true,"Frog");request.erase("commandId");
    const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);ASSERT_TRUE(source.contains("construction"));ASSERT_TRUE(source.contains("shapeHandle"));ASSERT_TRUE(source.contains("sourceStart"));ASSERT_TRUE(source.contains("sourceEnd"));
    auto construction=source.at("construction").To<Map>();construction["liftHeightMm"]=19.;construction["lateralOffset"]=Array{0.,8.,0.};const Map manual{{"construction",construction}};
    request["manualPath"]=manual;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());const auto history=scene.Database().GetUndoArray().size();Invoke(&scene,"CamRapid","EditPath",request);
    const auto saved=Generated(scene),connection=saved.at("transitions").To<Array>().front().To<Map>();EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());EXPECT_EQ(manual,connection.at("manualPath").To<Map>());EXPECT_EQ(construction,connection.at("construction").To<Map>());EXPECT_EQ("Draft",connection.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",connection.at("validation").To<Text>());
    EXPECT_EQ(source.at("points").To<Array>().front(),connection.at("points").To<Array>().front());EXPECT_EQ(source.at("points").To<Array>().back(),connection.at("points").To<Array>().back());EXPECT_EQ(before.at("operations"),saved.at("operations"));EXPECT_EQ(stateBefore.at("workpieces"),Read(scene).at("workpieces"));
    auto read=RapidRequest(scene);read.erase("commandId");const auto reopenedEditor=Invoke(&scene,"CamRapid","ReadEditPath",read);EXPECT_EQ(construction,reopenedEditor.at("construction").To<Map>());EXPECT_EQ(connection.at("points"),reopenedEditor.at("points"));
    auto invalid=RapidRequest(scene);auto changedHeight=construction;changedHeight["followHeightMm"]=construction.at("followHeightMm").To<double>()+1.;invalid["manualPath"]=Map{{"construction",changedHeight}};
    EXPECT_THROW(Invoke(&scene,"CamRapid","EditPath",invalid),std::exception);EXPECT_EQ(saved,Generated(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(saved,Generated(scene));
    request=RapidRequest(scene);request["manualPath"]=manual;const auto preview=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",preview.at("status").To<Text>());const auto result=preview.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>().at("result").To<Map>();
    EXPECT_EQ("1",result.at("candidateCount").To<Text>());EXPECT_EQ(connection.at("points"),result.at("points"));EXPECT_EQ(construction,result.at("construction").To<Map>());EXPECT_EQ(saved,Generated(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Constructed rapid source";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(saved,Generated(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisRapidManual, TurnPointDefinitionParticipatesInInputIdentityAndSurvivesHistoryAndReopen){
    Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");
    request["manualPath"]=Map{{"construction",Map{{"kind",Text("Retract")},{"followHeightMm",1.},{"liftHeightMm",10.},{"lateralOffset",Array{0.,0.,0.}}}}};
    const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);const auto points=source.at("points").To<Array>();ASSERT_EQ(4u,points.size());auto construction=source.at("construction").To<Map>();ASSERT_EQ("Retract",construction.at("kind").To<Text>());
    const auto a=points[1].To<Array>(),b=points[2].To<Array>();construction["turnPoint"]=Array{b[0],a[1],a[2]};construction["lateralOffset"]=Array{0.,0.,0.};
    const auto save=[&](){auto command=RapidRequest(scene);command["manualPath"]=Map{{"construction",construction}};Invoke(&scene,"CamRapid","EditPath",command);return Generated(scene);};
    const auto first=save(),firstConnection=first.at("transitions").To<Array>().front().To<Map>();ASSERT_EQ(5u,firstConnection.at("points").To<Array>().size());EXPECT_EQ(points.front(),firstConnection.at("points").To<Array>().front());EXPECT_EQ(points.back(),firstConnection.at("points").To<Array>().back());EXPECT_EQ(points[1],firstConnection.at("points").To<Array>()[1]);EXPECT_EQ(points[2],firstConnection.at("points").To<Array>()[3]);
    EXPECT_EQ(construction.at("turnPoint"),firstConnection.at("points").To<Array>()[2]);EXPECT_EQ(firstConnection.at("sourceEnd").To<Map>().at("direction"),firstConnection.at("directions").To<Array>()[2]);EXPECT_EQ(construction.at("turnPoint"),firstConnection.at("shapeHandle").To<Map>().at("point"));EXPECT_EQ("Unchecked",firstConnection.at("validation").To<Text>());
    auto turn=construction.at("turnPoint").To<Array>();turn[0]=turn[0].To<double>()+.25;construction["turnPoint"]=turn;const auto second=save(),secondConnection=second.at("transitions").To<Array>().front().To<Map>();EXPECT_NE(firstConnection.at("inputKey"),secondConnection.at("inputKey"));EXPECT_EQ(construction,secondConnection.at("construction").To<Map>());
    auto mixed=construction;mixed["lateralOffset"]=Array{0.,1.,0.};auto invalid=RapidRequest(scene);invalid["manualPath"]=Map{{"construction",mixed}};
    EXPECT_THROW(Invoke(&scene,"CamRapid","EditPath",invalid),std::exception);EXPECT_EQ(second,Generated(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(first,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(second,Generated(scene));
    auto read=RapidRequest(scene);read.erase("commandId");const auto restoredDefinition=Invoke(&scene,"CamRapid","ReadEditPath",read);EXPECT_EQ(construction,restoredDefinition.at("construction").To<Map>());EXPECT_EQ(secondConnection.at("points"),restoredDefinition.at("points"));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Retract turn point";const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(second,Generated(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisRapidManual, IntegralPolicyOverridesPublishTheSameCurrentInputAndPreserveRealValueInvalidation){
    Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);auto points=source.at("points").To<Array>(),directions=source.at("directions").To<Array>();ASSERT_GE(points.size(),4u);
    auto firstInterior=points[1].To<Array>();firstInterior[1]=17.;firstInterior[2]=23.;points[1]=firstInterior;
    const Map manual{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};
    const Map integerPolicy{{"strategy",Text("Quick")},{"marginMm",0},{"liftHeightMm",10}};request["policy"]=integerPolicy;request["manualPath"]=manual;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());Invoke(&scene,"CamRapid","EditPath",request);
    const auto state=Read(scene);const auto setupID=ID::from_string(state.at("activeSetupId").To<Text>()).value();const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const auto pair=request.at("from").To<Text>()+"/"+request.at("to").To<Text>();auto overrides=setup->GetRapidOverrides();auto record=overrides.at(pair).To<Map>();const auto writtenPolicy=record.at("policy").To<Map>();EXPECT_TRUE(writtenPolicy.at("marginMm").Is<double>());EXPECT_TRUE(writtenPolicy.at("liftHeightMm").Is<double>());
    // Both numeric wire kinds are legal in the current schema. Preserve the
    // same physical values in a stored override while changing only its kinds;
    // the task and its published projection must agree on this input identity.
    record["policy"]=integerPolicy;overrides[pair]=record;auto& preparation=scene.Database().BeginTransaction("Prepare integral physical policy representation");preparation.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"RapidOverrides",overrides}});ASSERT_TRUE(scene.Database().CommitTransaction(preparation));
    auto certify=RapidRequest(scene);certify["policy"]=integerPolicy;certify["manualPath"]=manual;const auto checked=Wait(scene,Invoke(&scene,"CamRapid","Preview",certify));ASSERT_EQ("Completed",checked.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(checked);const auto checkedRecord=checked.at("result").To<Map>().at("connections").To<Map>().at(pair).To<Map>();ASSERT_EQ("Clear",checkedRecord.at("result").To<Map>().at("validation").To<Text>());auto apply=GenerateRequest(scene);apply["taskId"]=checked.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);
    const auto before=Read(scene);const auto generated=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);const auto taskRecord=generated.at("result").To<Map>().at("connections").To<Map>().at(pair).To<Map>();ASSERT_EQ("Clear",taskRecord.at("result").To<Map>().at("validation").To<Text>());EXPECT_EQ(checkedRecord.at("inputKey"),taskRecord.at("inputKey"));
    const auto published=[&](Scene& context){for(const auto& item:Read(context).at("setups").To<Array>()){const auto current=item.To<Map>();if(current.at("id").To<Text>()!=state.at("activeSetupId").To<Text>())continue;for(const auto& value:current.at("generatedGeometry").To<Map>().at("transitions").To<Array>()){const auto row=value.To<Map>();if(row.at("from")==request.at("from")&&row.at("to")==request.at("to"))return row;}}throw std::runtime_error("Published rapid connection missing");};
    const auto current=published(scene);EXPECT_EQ("Generated",current.at("geometryStatus").To<Text>());EXPECT_EQ("Clear",current.at("validation").To<Text>());EXPECT_EQ(taskRecord.at("inputKey"),current.at("inputKey"));EXPECT_EQ(manual,current.at("manualPath").To<Map>());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));
    const auto repeat=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",repeat.at("status").To<Text>());EXPECT_EQ("0",repeat.at("result").To<Map>().at("calculatedCount").To<Text>());EXPECT_EQ(current.at("inputKey"),published(scene).at("inputKey"));
    auto noop=RapidRequest(scene);noop["policy"]=integerPolicy;noop["manualPath"]=manual;const auto history=scene.Database().GetUndoArray().size();EXPECT_FALSE(Invoke(&scene,"CamRapid","EditPath",noop).at("changed").To<bool>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    auto changed=RapidRequest(scene);auto changedPolicy=integerPolicy;changedPolicy["marginMm"]=.1;changed["policy"]=changedPolicy;changed["automatic"]=false;Invoke(&scene,"CamRapid","EditPolicy",changed);const auto invalidated=published(scene);EXPECT_EQ("Unchecked",invalidated.at("validation").To<Text>());EXPECT_EQ(current.at("inputKey"),invalidated.at("inputKey"));EXPECT_EQ(current.at("points"),invalidated.at("points"));
    auto restore=RapidRequest(scene);restore["policy"]=integerPolicy;restore["automatic"]=false;Invoke(&scene,"CamRapid","EditPolicy",restore);const auto restored=published(scene);EXPECT_EQ("Generated",restored.at("geometryStatus").To<Text>());EXPECT_EQ("Clear",restored.at("validation").To<Text>());EXPECT_EQ(current.at("inputKey"),restored.at("inputKey"));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Physical rapid policy identity";const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(restored,published(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisRapidManual, ReadEditPreviewSaveRegenerateUndoAndReopenKeepTheSelectedPoints)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene);const auto original=Read(scene).at("setups").To<Array>()[0].To<Map>().at("rapidSettings");
    auto request=RapidRequest(scene);request.erase("commandId");const auto history=scene.Database().GetUndoArray().size();
    const auto edit=Invoke(&scene,"CamRapid","ReadEditPath",request);EXPECT_EQ(before,Generated(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    auto points=edit.at("points").To<Array>(),directions=edit.at("directions").To<Array>();ASSERT_GE(points.size(),4u);
    const auto start=points.front(),end=points.back();auto p=points[1].To<Array>();p[1]=17.;p[2]=23.;points[1]=p;
    Map manual{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["manualPath"]=manual;
    const auto task=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto record=task.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>(),result=record.at("result").To<Map>();
    ASSERT_EQ("Generated",result.at("geometryStatus").To<Text>());EXPECT_EQ("Manual",result.at("planningMethod").To<Text>());EXPECT_EQ(manual,record.at("manualPath").To<Map>());
    EXPECT_EQ(start,result.at("points").To<Array>().front());EXPECT_EQ(end,result.at("points").To<Array>().back());EXPECT_EQ(points[1],result.at("points").To<Array>()[1]);EXPECT_EQ(before,Generated(scene));
    auto apply=GenerateRequest(scene);apply["taskId"]=task.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);const auto saved=Generated(scene);
    EXPECT_EQ(before.at("operations"),saved.at("operations"));EXPECT_EQ(original,Read(scene).at("setups").To<Array>()[0].To<Map>().at("rapidSettings"));EXPECT_EQ(manual,saved.at("transitions").To<Array>()[0].To<Map>().at("manualPath").To<Map>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(saved,Generated(scene));
    const auto batch=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",batch.at("status").To<Text>());EXPECT_EQ("1",batch.at("result").To<Map>().at("reusedCount").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto regenerated=Generated(scene),connection=regenerated.at("transitions").To<Array>()[0].To<Map>();EXPECT_EQ(manual,connection.at("manualPath").To<Map>());EXPECT_EQ(result.at("points"),connection.at("points"));EXPECT_EQ("Manual",connection.at("planningMethod").To<Text>());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Manual rapid";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(regenerated,Generated(reopened));std::filesystem::remove(path);
    auto malformed=RapidRequest(scene);malformed["manualPath"]=Map{{"points",Array{Array{1.,2.,3.}}},{"directions",Array{}}};EXPECT_THROW(Invoke(&scene,"CamRapid","Preview",malformed),std::exception);
}
TEST(FiveAxisRapidManual, RestoringCollisionSettingsKeepsGeometryAndRequiresFreshCollisionEvidence)
{
    Scene scene;PrepareOrderPart(scene);auto checked=RapidRequest(scene,false);checked["safe"]=true;
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",checked)).at("status").To<Text>());
    const auto settings=Read(scene).at("setups").To<Array>()[0].To<Map>().at("rapidSettings").To<Map>();auto blocked=settings;blocked["collisionThresholdMm"]=2.;auto configure=GenerateRequest(scene);configure["settings"]=blocked;Invoke(&scene,"CamRapid","Configure",configure);
    checked=RapidRequest(scene,false);checked["safe"]=true;
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",checked)).at("status").To<Text>());
    const auto high=Invoke(&scene,"CamScene","ReadState").at("setups").To<Array>()[0].To<Map>().at("generatedGeometry").To<Map>();EXPECT_EQ("0",high.at("rapidClearCount").To<Text>());
    configure=GenerateRequest(scene);configure["settings"]=settings;Invoke(&scene,"CamRapid","Configure",configure);
    const auto restored=Invoke(&scene,"CamScene","ReadState").at("setups").To<Array>()[0].To<Map>().at("generatedGeometry").To<Map>();EXPECT_EQ("0",restored.at("rapidClearCount").To<Text>());EXPECT_EQ(high.at("rapidGeneratedCount"),restored.at("rapidGeneratedCount"));EXPECT_NE("0",restored.at("rapidGeneratedCount").To<Text>());
    const auto repeat=Invoke(&scene,"CamScene","ReadState").at("setups").To<Array>()[0].To<Map>().at("generatedGeometry").To<Map>();EXPECT_EQ(restored.at("sampleGeometryRef"),repeat.at("sampleGeometryRef")); checked=RapidRequest(scene,false);checked["safe"]=true;ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",checked)).at("status").To<Text>());EXPECT_NE("0",Generated(scene).at("rapidClearCount").To<Text>());
}
TEST(FiveAxisRapidManual, DirectEditsUseRepositoryHistoryAndNeverClaimCollisionClear)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene);
    auto request=RapidRequest(scene);request.erase("commandId");const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);
    auto points=source.at("points").To<Array>(),directions=source.at("directions").To<Array>();ASSERT_GE(points.size(),3u);
    auto point=points[1].To<Array>();point[2]=29.;points[1]=point;
    Array innerPoints(points.begin()+1,points.end()-1),innerDirections(directions.begin()+1,directions.end()-1);
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["manualPath"]=Map{{"points",innerPoints},{"directions",innerDirections}};
    const auto count=scene.Database().GetUndoArray().size();Invoke(&scene,"CamRapid","EditPath",request);
    const auto saved=Generated(scene),connection=saved.at("transitions").To<Array>()[0].To<Map>();
    EXPECT_EQ(count+1,scene.Database().GetUndoArray().size());EXPECT_EQ("Draft",connection.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",connection.at("validation").To<Text>());
    EXPECT_EQ(points,connection.at("points").To<Array>());EXPECT_EQ(before.at("operations"),saved.at("operations"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(saved,Generated(scene));
    request["expected"]=Read(scene).at("expected");request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    Invoke(&scene,"CamRapid","EditPath",request);EXPECT_EQ(count+1,scene.Database().GetUndoArray().size());
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["manualPath"]=Map{{"points",innerPoints},{"directions",Array{}}};
    EXPECT_THROW(Invoke(&scene,"CamRapid","EditPath",request),std::exception);EXPECT_EQ(saved,Generated(scene));
}

TEST(FiveAxisRapidManual, RestoreAutomaticAndPolicyChangesUseNativeHistoryAndSurviveReopen)
{
    Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");
    const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);auto points=source.at("points").To<Array>(),directions=source.at("directions").To<Array>();
    auto moved=points[1].To<Array>();moved[2]=28.;points[1]=moved;
    request["manualPath"]=Map{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};
    const auto invoke=[&](const Text& method){request["expected"]=Read(scene).at("expected");request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());return Invoke(&scene,"CamRapid",method,request);};
    invoke("EditPath");std::vector<Map> states{Generated(scene)};const auto count=scene.Database().GetUndoArray().size();
    request.erase("manualPath");request["automatic"]=true;invoke("EditPolicy");states.push_back(Generated(scene));
    const auto automatic=states.back().at("transitions").To<Array>()[0].To<Map>();
    EXPECT_FALSE(automatic.contains("manualPath"));EXPECT_EQ("Draft",automatic.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",automatic.at("validation").To<Text>());
    EXPECT_NE(states.front().at("transitions").To<Array>()[0].To<Map>().at("points"),automatic.at("points"));
    request["automatic"]=false;auto policy=request.at("policy").To<Map>();policy["strategy"]=Text("Frog");policy["liftHeightMm"]=16.;request["policy"]=policy;invoke("EditPolicy");states.push_back(Generated(scene));
    policy["marginMm"]=.7;request["policy"]=policy;invoke("EditPolicy");states.push_back(Generated(scene));
    EXPECT_EQ(count+3,scene.Database().GetUndoArray().size());invoke("EditPolicy");EXPECT_EQ(count+3,scene.Database().GetUndoArray().size());
    for(int i=2;i>=0;--i){ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(states[i],Generated(scene));}
    for(std::size_t i=1;i<states.size();++i){ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(states[i],Generated(scene));}
    policy["liftHeightMm"]=-1.;request["policy"]=policy;EXPECT_THROW(invoke("EditPolicy"),std::exception);EXPECT_EQ(states.back(),Generated(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Rapid policy history";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(states.back(),Generated(reopened));std::filesystem::remove(path);
}

