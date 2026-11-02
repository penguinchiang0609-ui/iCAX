namespace
{
    Map PolicyBody(Scene& scene,const Map& ref)
    {
        if(ref.contains("value"))return ref.at("value").To<Map>();
        if(ref.contains("revision"))return ResolveEngineeringValue(scene,ref).at("value").To<Map>();
        const auto binary=scene.Resources().Get<iCAX::Resource::CBinaryResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
        return iCAX::Data::VariantSerializer::Deserialize(Text(binary->Content.begin(),binary->Content.end())).To<Map>();
    }
    Map PrepareRequest(Scene& scene,const Map& extraction,const Map& terminal)
    {
        const auto fixture=CreateRequest(scene,extraction,terminal);
        auto assignment=PolicyBody(scene,fixture.at("process").To<Map>()),pose=PolicyBody(scene,fixture.at("pose").To<Map>());
        for(const auto* key:{"schema","schemaVersion","libraryData"})assignment.erase(key);
        pose.erase("schema");pose.erase("schemaVersion");
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
            {"setupId",extraction.at("setupId")},{"placementId",extraction.at("placementId")},
            {"library",Map{{"name",Text("Custom unqualified process")},{"material",Text("Test steel")},{"thicknessMm",10.},
                {"gas",Text("N2")},{"parameters",Map{{"feedMmPerSecond",20.},{"laserPowerW",1500.},{"gasPressureBar",1.5},
                {"standOffMm",1.},{"focusOffsetMm",0.},{"pierceTimeSeconds",.2},{"pierceFootprintRadiusMm",.05}}}}},
            {"assignment",assignment},{"pose",pose}};
    }
    Map WithPolicies(Map create,const Map& prepared)
    {create["process"]=prepared.at("process");create["pose"]=prepared.at("pose");return create;}
    Map ReadPolicyRequest(Scene& scene,const Map& created)
    {return {{"expected",Read(scene).at("expected")},{"intentIds",created.at("createdIntentIds")}};}
}
TEST(FiveAxisProcessDomain, PrepareDoesNotMutateAndCreatePersistsFixedPoliciesInOneUndo)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    const auto request=PrepareRequest(scene,extraction,terminal),before=Read(scene);
    const auto prepared=Invoke(&scene,"CamProcess","Prepare",request);
    EXPECT_EQ("Completed",Invoke(&scene,"CamScene","QueryCommand",{{"epoch",before.at("expected").To<Map>().at("epoch")},{"commandId",request.at("commandId")}}).at("status").To<Text>());
    EXPECT_EQ("NotFound",Invoke(&scene,"CamScene","QueryCommand",{{"epoch",before.at("expected").To<Map>().at("epoch")},{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())}}).at("status").To<Text>());
    EXPECT_EQ("Unqualified",prepared.at("readiness").To<Map>().at("status").To<Text>());
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(1u,scene.Database().GetUndoArray().size());
    EXPECT_EQ(prepared,Invoke(&scene,"CamProcess","Prepare",request));
    auto changed=request;auto library=request.at("library").To<Map>();library["name"]=Text("Different");changed["library"]=library;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",changed),std::exception);
    const auto created=Invoke(&scene,"CamCut","Create",WithPolicies(CreateRequest(scene,extraction,terminal),prepared));
    const auto details=Invoke(&scene,"CamProcess","Read",ReadPolicyRequest(scene,created));
    const auto item=details.at("items").To<Array>().front().To<Map>();
    EXPECT_EQ(request.at("library").To<Map>().at("parameters"),item.at("library").To<Map>().at("parameters"));
    EXPECT_EQ(prepared.at("process"),item.at("process"));EXPECT_EQ(2u,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(details,Invoke(&scene,"CamProcess","Read",ReadPolicyRequest(scene,created)));
    iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Process policy persistence";
    auto path=file.Path;path.replace_extension(".i5cam");project.Save(path,info,scene.Database(),scene.Resources());std::filesystem::remove(file.Path);
    Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(details,Invoke(&reopened,"CamProcess","Read",ReadPolicyRequest(reopened,created)));std::filesystem::remove(path);
}
TEST(FiveAxisProcessDomain, MissingTechnologyRemainsExplicitAndInvalidInputsDoNotConsumeCandidates)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));auto request=PrepareRequest(scene,extraction,terminal);
    request["library"]=Map{{"name",Text("Geometry definition only")}};
    const auto prepared=Invoke(&scene,"CamProcess","Prepare",request);
    EXPECT_EQ("Incomplete",prepared.at("readiness").To<Map>().at("status").To<Text>());
    EXPECT_FALSE(PolicyBody(scene,PolicyBody(scene,prepared.at("process").To<Map>()).at("libraryData").To<Map>()).contains("parameters"));
    request=PrepareRequest(scene,extraction,terminal);const auto before=Read(scene);
    auto invalid=request;auto library=request.at("library").To<Map>();library["productionQualified"]=true;invalid["library"]=library;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",invalid),std::exception);
    library=request.at("library").To<Map>();library["parameters"]=Map{{"feedMmPerSecond",-1.}};invalid["library"]=library;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",invalid),std::exception);
    library["parameters"]=Map{{"unknown",1.}};invalid["library"]=library;EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",invalid),std::exception);
    invalid=request;auto assignment=request.at("assignment").To<Map>();assignment["leadIn"]=Map{{"kind",Text("Line")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};
    invalid["assignment"]=assignment;EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",invalid),std::exception);
    invalid=request;auto pose=request.at("pose").To<Map>();pose["mode"]=Text("FixedRotary");invalid["pose"]=pose;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",invalid),std::exception);EXPECT_EQ(before,Read(scene));
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","Create",WithPolicies(CreateRequest(scene,extraction,terminal),prepared)));
}
TEST(FiveAxisProcessDomain, OverrideTypesDuplicatesAndSceneVersionAreValidated)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto request=PrepareRequest(scene,extraction,terminal);
    auto assignment=request.at("assignment").To<Map>();assignment["overrides"]=Array{Map{{"parameterId",Text("feedMmPerSecond")},{"value",30.}}};
    auto valid=request;valid["assignment"]=assignment;EXPECT_NO_THROW(Invoke(&scene,"CamProcess","Prepare",valid));
    valid["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    const auto override=assignment.at("overrides").To<Array>().front();assignment["overrides"]=Array{override,override};valid["assignment"]=assignment;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",valid),std::exception);
    assignment["overrides"]=Array{Map{{"parameterId",Text("feedMmPerSecond")},{"value",Text("30")}}};valid["assignment"]=assignment;
    EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",valid),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamProcess","Prepare",request),std::exception);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));EXPECT_THROW(Invoke(&scene,"CamProcess","Prepare",request),std::exception);
}
TEST(FiveAxisProcessDomain, EngineeringValuesRejectResourceHandlesAndInvalidNestedPoliciesBeforeCreation)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto create=CreateRequest(scene,extraction,terminal),before=Read(scene);
    const auto assignment=PolicyBody(scene,create.at("process").To<Map>());auto attempt=[&](const Map& bad){auto request=create;request["process"]=bad;EXPECT_THROW(Invoke(&scene,"CamCut","Create",request),std::exception);EXPECT_EQ(before,Read(scene));};
    attempt(WirePolicy(PolicyFixture(scene,assignment)));attempt(Map{});
    auto invalid=assignment;invalid["schema"]=Text("cam.pose-policy");attempt(PolicyDataFixture(invalid));
    invalid=assignment;invalid["libraryData"]=Map{};attempt(PolicyDataFixture(invalid));
    invalid=assignment;auto kerf=invalid.at("kerf").To<Map>();kerf["widthMm"]=-1.;invalid["kerf"]=kerf;attempt(PolicyDataFixture(invalid));
    const auto resourceCount=scene.Resources().Count();const auto created=Invoke(&scene,"CamCut","Create",create);EXPECT_FALSE(created.at("createdIntentIds").To<Array>().empty());EXPECT_EQ(resourceCount,scene.Resources().Count());
}

namespace
{
    Map ConfirmWithPolicies(Scene& scene,double width=0.2,const Map* placement=nullptr,size_t candidateIndex=0,bool standard=false)
    {
        Map ids;
        if(placement) ids=*placement;
        else { File file(RecognitionPart());ids=Imported(scene,file); }
        const auto extraction=ExtractRequest(scene,ids);
        const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
        auto request=PrepareRequest(scene,extraction,terminal),assignment=request.at("assignment").To<Map>(),kerf=assignment.at("kerf").To<Map>();
        kerf["widthMm"]=width;assignment["kerf"]=kerf;request["assignment"]=assignment;
        auto create=CreateRequest(scene,extraction,terminal);
        create["candidateIds"]=Array{create.at("candidateIds").To<Array>().at(candidateIndex)};
        const auto created=Invoke(&scene,"CamCut","Create",WithPolicies(create,Invoke(&scene,"CamProcess","Prepare",request)));
        // These existing editor/compiler cases explicitly exercise ordinary contours.
        if(!standard)Invoke(&scene,"CamCut","SetType",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
            {"expected",Read(scene).at("expected")},{"intentIds",created.at("createdIntentIds")},{"cutType",Text("Contour")}});
        return created;
    }
    Array ConfirmPolicyPair(Scene& scene)
    {
        const auto secondHole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(12,0,-1),gp::DZ()),3,12).Shape();
        File file(BRepAlgoAPI_Cut(RecognitionPart(),secondHole).Shape());
        const auto placement=Imported(scene,file);
        const auto first=ConfirmWithPolicies(scene,.2,&placement,0).at("createdIntentIds").To<Array>().front();
        const auto second=ConfirmWithPolicies(scene,.4,&placement,1).at("createdIntentIds").To<Array>().front();
        return {first,second};
    }
    Map EditRequest(Scene& scene,const Array& ids,Map fields)
    {
        fields["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());fields["expected"]=Read(scene).at("expected");fields["intentIds"]=ids;return fields;
    }
    Map ProcessItem(Scene& scene,const iCAX::Data::Variant& id)
    {return Invoke(&scene,"CamProcess","Read",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("items").To<Array>().front().To<Map>();}
}
TEST(FiveAxisProcessDomain, BatchPatchIsAtomicAndClearRestoresEachFixedLibrary)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto first=ids[0],second=ids[1];
    const auto before=Read(scene);const auto firstPolicy=ProcessItem(scene,first),secondPolicy=ProcessItem(scene,second);
    const auto beforeUndo=scene.Database().GetUndoArray().size();
    auto bad=EditRequest(scene,{first,Text("00000000-0000-0000-0000-000000000001")},{{"patch",Map{{"overcutLengthMm",1.}}}});
    EXPECT_THROW(Invoke(&scene,"CamProcess","Update",bad),std::exception);EXPECT_EQ(before,Read(scene));
    const auto edit=EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",.6}}},{"overcutLengthMm",1.}}}});
    const auto result=Invoke(&scene,"CamProcess","Update",edit);EXPECT_EQ(result,Invoke(&scene,"CamProcess","Update",edit));
    EXPECT_EQ(beforeUndo+1,scene.Database().GetUndoArray().size());
    for(const auto& id:ids){const auto p=ProcessItem(scene,id);EXPECT_DOUBLE_EQ(.6,p.at("assignment").To<Map>().at("kerf").To<Map>().at("widthMm").To<double>());}
    EXPECT_EQ(firstPolicy.at("pose"),ProcessItem(scene,first).at("pose"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(firstPolicy,ProcessItem(scene,first));EXPECT_EQ(secondPolicy,ProcessItem(scene,second));ASSERT_TRUE(scene.Database().Redo());
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Kerf")}}}));
    for(const auto& id:ids){const auto a=ProcessItem(scene,id).at("assignment").To<Map>();EXPECT_DOUBLE_EQ(0.,a.at("kerf").To<Map>().at("widthMm").To<double>());EXPECT_DOUBLE_EQ(1.,a.at("overcutLengthMm").To<double>());}
    Invoke(&scene,"CamProcess","RestoreInherited",EditRequest(scene,ids,{{"categories",Array{Text("Kerf")}}}));
    EXPECT_EQ(firstPolicy.at("assignment").To<Map>().at("kerf"),ProcessItem(scene,first).at("assignment").To<Map>().at("kerf"));
    EXPECT_EQ(secondPolicy.at("assignment").To<Map>().at("kerf"),ProcessItem(scene,second).at("assignment").To<Map>().at("kerf"));
    const auto after=Read(scene).at("intentToolpaths").To<Array>();const auto prior=before.at("intentToolpaths").To<Array>();
    for(size_t i=0;i<after.size();++i)for(const auto* key:{"curveRef","sourceRecipeRef","featureRef","poseRef"})EXPECT_EQ(prior[i].To<Map>().at(key),after[i].To<Map>().at(key));
}
TEST(FiveAxisProcessDomain, PosePatchPreservesProcessAndRejectsUnknownOrNonNumericValues)
{
    Scene scene;const auto created=ConfirmWithPolicies(scene),before=Read(scene);const auto ids=created.at("createdIntentIds").To<Array>();const auto prior=ProcessItem(scene,ids.front());
    for(const auto& patch:std::vector<Map>{{{"leadAngleRad",Text("0.2")}},{{"mode",Text("FixedRotary")}},{{"angularToleranceRad",-1.}},{{"surfaceNormalPreferenceWeight",1.1}}})
        EXPECT_THROW(Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,ids,{{"patch",patch}})),std::exception);
    EXPECT_EQ(before,Read(scene));
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,ids,{{"patch",Map{{"leadAngleRad",.2},{"transitionMetric",Text("DirectionChange")}}}}));
    const auto after=ProcessItem(scene,ids.front());EXPECT_EQ(prior.at("process"),after.at("process"));
    EXPECT_DOUBLE_EQ(.2,after.at("posePolicy").To<Map>().at("leadAngleRad").To<double>());
    EXPECT_EQ(prior.at("posePolicy").To<Map>().at("keypoints"),after.at("posePolicy").To<Map>().at("keypoints"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(prior,ProcessItem(scene,ids.front()));
}
TEST(FiveAxisProcessDomain, FailedBatchCommitCanRetryAndAssignPreservesGeometry)
{
    class RejectChange final:public iCAX::Database::IChecker
    {
    public:
        bool Once=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text& error) override{if(Once){Once=false;error="Injected process failure";return false;}return true;}
    };
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto first=ids[0],second=ids[1];
    const auto before=Read(scene),source=ProcessItem(scene,second);const auto undoSize=scene.Database().GetUndoArray().size();
    scene.Database().GetMetaRegistry()->RegistChecker<iCAX::CAM::Intent::CCutIntentComponent>(std::make_shared<RejectChange>());
    const auto request=EditRequest(scene,ids,{{"patch",Map{{"overcutLengthMm",2.}}}});
    EXPECT_THROW(Invoke(&scene,"CamProcess","Update",request),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(undoSize,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamProcess","Update",request));
    Invoke(&scene,"CamProcess","Assign",EditRequest(scene,{first},{{"libraryData",source.at("assignment").To<Map>().at("libraryData")},{"overrides",Array{}}}));
    EXPECT_DOUBLE_EQ(.4,ProcessItem(scene,first).at("assignment").To<Map>().at("kerf").To<Map>().at("widthMm").To<double>());
    EXPECT_EQ(source.at("assignment").To<Map>().at("libraryData"),ProcessItem(scene,first).at("assignment").To<Map>().at("libraryData"));
}
TEST(FiveAxisProcessDomain, CompilationReceiptAndModelQueriesHaveSeparateResponsibilities)
{
    Scene scene;const auto created=ConfirmWithPolicies(scene,.2);const auto id=created.at("createdIntentIds").To<Array>().front();const auto before=Read(scene);
    const Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"intentId",id}};
    const auto task=Invoke(&scene,"CamProcess","Compile",request);EXPECT_TRUE(task.at("result").To<Map>().empty());
    const auto terminal=WaitCut(scene,task);ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<terminal.at("error").To<Text>();
    const auto receipt=terminal.at("result").To<Map>();EXPECT_EQ(3u,receipt.size());EXPECT_EQ(id,receipt.at("intentId"));EXPECT_FALSE(receipt.contains("compiled"));
    const Map query{{"epoch",terminal.at("epoch")},{"taskId",terminal.at("taskId")}};const auto model=Invoke(&scene,"CamProcess","QueryCompiled",query);
    EXPECT_EQ("cam.compiled-process-preview",model.at("schema").To<Text>());EXPECT_TRUE(model.contains("exactCurveData"));EXPECT_FALSE(model.at("spans").To<Array>().empty());
    EXPECT_EQ(model,Invoke(&scene,"CamProcess","QueryCompiled",query));EXPECT_EQ(before,Read(scene));
    const auto recovered=Invoke(&scene,"CamScene","QueryCommand",{{"epoch",terminal.at("epoch")},{"commandId",request.at("commandId")}});EXPECT_EQ(receipt,recovered.at("result").To<Map>());
    auto invalid=query;invalid["epoch"]=Text("another-epoch");EXPECT_THROW(Invoke(&scene,"CamProcess","QueryCompiled",invalid),std::exception);
}
TEST(FiveAxisProcessDomain, CompilationUsesStoredPoliciesAndPublishesOnlyRuntimeProcessGeometry)
{
    Scene scene;const auto created=ConfirmWithPolicies(scene,.2);const auto id=created.at("createdIntentIds").To<Array>().front();
    const auto before=Read(scene);const auto request=Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"intentId",id}};
    const auto task=Invoke(&scene,"CamProcess","Compile",request);EXPECT_EQ(task,Invoke(&scene,"CamProcess","Compile",request));
    const auto terminal=WaitCut(scene,task);ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<terminal.at("error").To<Text>();
    EXPECT_TRUE(task.at("result").To<Map>().empty());
    const auto receipt=terminal.at("result").To<Map>();EXPECT_EQ(3u,receipt.size());EXPECT_EQ(id,receipt.at("intentId"));EXPECT_FALSE(receipt.contains("compiled"));
    const Map query{{"epoch",terminal.at("epoch")},{"taskId",terminal.at("taskId")}};
    const auto result=Invoke(&scene,"CamProcess","QueryCompiled",query);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ("NotPlanned",result.at("motionState").To<Text>());EXPECT_FALSE(result.at("productionQualified").To<bool>());
    EXPECT_DOUBLE_EQ(3.9,result.at("compensatedRadiusMm").To<double>());EXPECT_FALSE(result.at("remainingChecks").To<Array>().empty());
    const auto path=result.at("path").To<Map>(),events=result.at("events").To<Map>();
    EXPECT_TRUE(scene.Resources().GetInfo(path.at("url").To<Text>(),1)->IsRuntimeOnly());
    const auto curve=scene.Resources().Get<iCAX::CAM::Process::CProcessPathCurveResource>(path.at("url").To<Text>(),1);
    ASSERT_TRUE(curve);ASSERT_EQ(1u,curve->Curve.Segments.size());EXPECT_DOUBLE_EQ(3.9,std::get<iCAX::GeometryData::Circle3>(curve->Curve.Segments.front().Curve).Radius);
    const auto eventResource=scene.Resources().Get<iCAX::CAM::Process::CProcessEventFieldResource>(events.at("url").To<Text>(),1);
    ASSERT_TRUE(eventResource);ASSERT_EQ(7u,eventResource->Events.size());EXPECT_EQ("CutOff",eventResource->Events[5].Kind);
    iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Process runtime scope";
    const auto file=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");project.Save(file,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(file,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_FALSE(reopened.Resources().GetInfo(path.at("url").To<Text>(),1));EXPECT_THROW(Invoke(&reopened,"CamProcess","QueryCompiled",query),std::exception);std::filesystem::remove(file);
    EXPECT_THROW(Invoke(nullptr,"CamProcess","Compile",request),std::exception);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"overcutLengthMm",1.}}}}));
    EXPECT_THROW(Invoke(&scene,"CamProcess","QueryCompiled",query),std::exception);ASSERT_TRUE(scene.Database().Undo());
    EXPECT_THROW(Invoke(&scene,"CamProcess","QueryCompiled",query),std::exception);
}
TEST(FiveAxisProcessDomain, EditUndoAndNewRequestSupersedeCompilationAndIncompleteProcessDoesNotRun)
{
    Scene scene;const auto created=ConfirmWithPolicies(scene);const auto id=created.at("createdIntentIds").To<Array>().front();
    auto request=Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",id}};
    const auto first=Invoke(&scene,"CamProcess","Compile",request);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"overcutLengthMm",1.}}}}));ASSERT_TRUE(scene.Database().Undo());
    EXPECT_EQ("Superseded",WaitCut(scene,first).at("status").To<Text>());
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");
    const auto second=Invoke(&scene,"CamProcess","Compile",request);request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    const auto latest=Invoke(&scene,"CamProcess","Compile",request);EXPECT_EQ("Superseded",WaitCut(scene,second).at("status").To<Text>());
    EXPECT_EQ("Completed",WaitCut(scene,latest).at("status").To<Text>());
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());const auto cancelling=Invoke(&scene,"CamProcess","Compile",request);
    Invoke(&scene,"CamPlan","CancelTask",{{"epoch",cancelling.at("epoch")},{"taskId",cancelling.at("taskId")}});
    EXPECT_EQ("Cancelled",WaitCut(scene,cancelling).at("status").To<Text>());
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"leadAngleRad",.2}}}}));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");
    const auto failed=WaitCut(scene,Invoke(&scene,"CamProcess","Compile",request));EXPECT_EQ("Failed",failed.at("status").To<Text>());
    EXPECT_EQ("PROCESS_POSE_COUPLING_UNSUPPORTED",failed.at("error").To<Text>());
    Scene incomplete;File file(RecognitionPart());const auto ids=Imported(incomplete,file),extraction=ExtractRequest(incomplete,ids);
    const auto terminal=WaitCut(incomplete,Invoke(&incomplete,"CamCut","ExtractCandidates",extraction));
    const auto result=Invoke(&incomplete,"CamCut","Create",CreateRequest(incomplete,extraction,terminal));
    EXPECT_THROW(Invoke(&incomplete,"CamProcess","Compile",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"expected",Read(incomplete).at("expected")},{"intentId",result.at("createdIntentIds").To<Array>().front()}}),std::exception);
}
