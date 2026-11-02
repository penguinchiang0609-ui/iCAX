namespace
{
    iCAX::CAM::Intent::SToolpathValue PublishedBranchValue(Scene&,const iCAX::Data::Variant&);
    Map DefinitionItem(Scene& scene,const iCAX::Data::Variant& id)
    {return Invoke(&scene,"CamCut","Read",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("items").To<Array>().front().To<Map>();}
    Map CompiledItem(Scene& scene,const iCAX::Data::Variant& id)
    {
        const auto task=WaitCut(scene,Invoke(&scene,"CamProcess","Compile",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
            {"expected",Read(scene).at("expected")},{"intentId",id}}));
        if(task.at("status").To<Text>()!="Completed")throw std::runtime_error(task.at("error").To<Text>());
        return Invoke(&scene,"CamProcess","QueryCompiled",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    }
}
TEST(FiveAxisCutEditing, ResolvingStartIsReadOnlyAndAnchorsTheExactSourceCurve)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene).at("createdIntentIds").To<Array>().front();
    const auto before=Read(scene);const auto undoSize=scene.Database().GetUndoArray().size();
    Map request{{"expected",before.at("expected")},{"intentId",id},{"nearPoint",Array{0.,4.1,10.}},
        {"maximumDistanceMm",.5},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}};
    const auto result=Invoke(&scene,"CamCut","ResolveStart",request);const auto p=result.at("point").To<Array>();
    EXPECT_NEAR(0,p[0].To<double>(),1e-8);EXPECT_NEAR(4,p[1].To<double>(),1e-8);EXPECT_NEAR(10,p[2].To<double>(),1e-8);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undoSize,scene.Database().GetUndoArray().size());
    auto invalid=request;invalid["frame"]=Text("Setup");EXPECT_THROW(Invoke(&scene,"CamCut","ResolveStart",invalid),std::exception);
    invalid=request;invalid["nearPoint"]=Array{100.,100.,100.};EXPECT_THROW(Invoke(&scene,"CamCut","ResolveStart",invalid),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamCut","ResolveStart",request),std::exception);
    Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{id},{{"start",result.at("start")}}));
    EXPECT_EQ(result.at("start"),DefinitionItem(scene,id).at("start"));
    EXPECT_EQ(result.at("point"),Read(scene).at("intentToolpaths").To<Array>().front().To<Map>().at("startPoint"));
    EXPECT_THROW(Invoke(&scene,"CamCut","ResolveStart",request),std::exception);
}
TEST(FiveAxisCutEditing, StartAndReversePreservePhysicalAnchorMaterialAndExactSource)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,.2).at("createdIntentIds").To<Array>().front();
    const auto before=Read(scene),original=DefinitionItem(scene,id),policy=ProcessItem(scene,id);
    EXPECT_EQ(before,Read(scene));const auto undoCount=scene.Database().GetUndoArray().size();
    auto start=original.at("start").To<Map>();start["nativeParameter"]=1.25;
    const auto setStart=EditRequest(scene,{id},{{"start",start}});
    const auto applied=Invoke(&scene,"CamCut","SetStart",setStart);
    EXPECT_EQ(applied,Invoke(&scene,"CamCut","SetStart",setStart));EXPECT_EQ(undoCount+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(start,DefinitionItem(scene,id).at("start").To<Map>());
    const auto forward=CompiledItem(scene,id);const auto first=forward.at("spans").To<Array>().front().To<Map>().at("sourceRange").To<Map>();
    EXPECT_DOUBLE_EQ(1.25,first.at(first.at("reversed").To<bool>()?"last":"first").To<double>());
    const auto reverse=EditRequest(scene,{id},{});const auto result=Invoke(&scene,"CamCut","Reverse",reverse);
    EXPECT_EQ(result,Invoke(&scene,"CamCut","Reverse",reverse));
    const auto reversed=DefinitionItem(scene,id),backward=CompiledItem(scene,id);
    const auto startPoint=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>().at("startPoint").To<Array>();
    EXPECT_NEAR(4*std::cos(1.25),startPoint[0].To<double>(),1e-8);
    EXPECT_NEAR(4*std::sin(1.25),startPoint[1].To<double>(),1e-8);
    EXPECT_TRUE(reversed.at("reversed").To<bool>());EXPECT_EQ(start,reversed.at("start").To<Map>());
    EXPECT_EQ(original.at("retainedSide"),reversed.at("retainedSide"));EXPECT_EQ(original.at("sourceSpans"),reversed.at("sourceSpans"));
    EXPECT_EQ(forward.at("compensatedRadiusMm"),backward.at("compensatedRadiusMm"));
    EXPECT_EQ(forward.at("assumedToolDirection"),backward.at("assumedToolDirection"));
    const auto reversedFirst=backward.at("spans").To<Array>().front().To<Map>().at("sourceRange").To<Map>();
    EXPECT_NE(first.at("reversed"),reversedFirst.at("reversed"));
    EXPECT_DOUBLE_EQ(1.25,reversedFirst.at(reversedFirst.at("reversed").To<bool>()?"last":"first").To<double>());
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));EXPECT_FALSE(DefinitionItem(scene,id).at("reversed").To<bool>());
    EXPECT_EQ(policy,ProcessItem(scene,id));
    EXPECT_EQ(before.at("intentToolpaths").To<Array>().front().To<Map>().at("curveRef"),Read(scene).at("intentToolpaths").To<Array>().front().To<Map>().at("curveRef"));
    Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{id},{{"retainedSide",Text("Interior")}}));
    EXPECT_DOUBLE_EQ(4.1,CompiledItem(scene,id).at("compensatedRadiusMm").To<double>());
    EXPECT_EQ(start,DefinitionItem(scene,id).at("start").To<Map>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(Text("Exterior"),DefinitionItem(scene,id).at("retainedSide").To<Text>());
}
TEST(FiveAxisCutEditing, InvalidSelectionsAnchorsAndFailedBatchHaveNoPartialEffects)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto a=ids[0],b=ids[1];const auto before=Read(scene);
    const auto original=DefinitionItem(scene,a);auto anchor=original.at("start").To<Map>();
    anchor["nativeParameter"]=100.;EXPECT_THROW(Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{a},{{"start",anchor}})),std::exception);
    anchor["nativeParameter"]=1.;anchor["sourceSpanId"]=Text("unknown");EXPECT_THROW(Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{a},{{"start",anchor}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{a,a},{})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{a,Text(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()))},{})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{a},{{"retainedSide",Text("Left")}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{a,b},{{"start",original.at("start")}})),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamCut","Read",{{"expected",before.at("expected")},{"intentIds",Array{a}}}),std::exception);
    EXPECT_EQ(before,Read(scene));
    class RejectSecond final:public iCAX::Database::IChecker {
    public:int Remaining=2;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text& error)override {
            if(--Remaining==0){error="Injected second definition failure";return false;}return true;
        }
    };
    scene.Database().GetMetaRegistry()->RegistChecker<iCAX::CAM::Intent::CIntentGeometryComponent>(std::make_shared<RejectSecond>());
    const auto request=EditRequest(scene,{a,b},{});const auto undoSize=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Invoke(&scene,"CamCut","Reverse",request),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(undoSize,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","Reverse",request));EXPECT_TRUE(DefinitionItem(scene,a).at("reversed").To<bool>());EXPECT_TRUE(DefinitionItem(scene,b).at("reversed").To<bool>());
    auto conflict=request;conflict["intentIds"]=Array{a};EXPECT_THROW(Invoke(&scene,"CamCut","Reverse",conflict),std::exception);
    auto stale=EditRequest(scene,{a},{});stale["expected"]=before.at("expected");EXPECT_THROW(Invoke(&scene,"CamCut","Reverse",stale),std::exception);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,DefinitionItem(scene,a));ASSERT_TRUE(scene.Database().Redo());EXPECT_TRUE(DefinitionItem(scene,a).at("reversed").To<bool>());
}
TEST(FiveAxisCutEditing, SavedDefinitionsKeepOriginalCurveClosureAndRecompileAfterReopen)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,.2).at("createdIntentIds").To<Array>().front();
    auto anchor=DefinitionItem(scene,id).at("start").To<Map>();anchor["nativeParameter"]=2.;
    Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{id},{{"start",anchor}}));
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
    Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{id},{{"retainedSide",Text("Interior")}}));
    const auto definition=DefinitionItem(scene,id),compiled=CompiledItem(scene,id);
    iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Edited definition";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");project.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(Read(scene).at("intentToolpaths"),Read(reopened).at("intentToolpaths"));
    EXPECT_EQ(definition,DefinitionItem(reopened,id));const auto restored=CompiledItem(reopened,id);
    EXPECT_EQ(compiled.at("spans"),restored.at("spans"));EXPECT_EQ(compiled.at("eventSequence"),restored.at("eventSequence"));std::filesystem::remove(path);
}

TEST(FiveAxisLeads, StartClosureAndLeadOutShareOneGeometryAndAtomicUndo)
{
    for(const bool reverse:{false,true})for(const bool gap:{false,true}){
        Scene scene;const auto id=ConfirmWithPolicies(scene,0.).at("createdIntentIds").To<Array>().front();
        if(reverse)Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
        const auto before=Read(scene),definition=DefinitionItem(scene,id);const auto undo=scene.Database().GetUndoArray().size();
        auto start=definition.at("start").To<Map>();start["parameter"]=6.1;
        const Map patch{{"start",start},{"leadIn",Lead("None",0)},{"leadOut",Lead("Line",.3)},
            {"overcutLengthMm",gap?0.:2.},{"gapLengthMm",gap?2.:0.}};
        const auto preview=LeadPreview(scene,{id},patch).at("items").To<Array>().front().To<Map>();
        EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
        const auto range=definition.at("sourceSpans").To<Array>().front().To<Map>();
        const double direction=range.at("reversed").To<bool>()?-1.:1.;
        const double angle=6.1+direction*(gap?-.5:.5);const auto end=preview.at("end").To<Array>();
        EXPECT_NEAR(4*std::cos(angle),end[0].To<double>(),1e-7);EXPECT_NEAR(4*std::sin(angle),end[1].To<double>(),1e-7);
        const auto exit=preview.at("leads").To<Array>().front().To<Map>();EXPECT_EQ(end,exit.at("anchor").To<Array>());
        EXPECT_EQ(gap?"Gap":"Overcut",preview.at("closure").To<Map>().at("kind").To<Text>());
        if(gap){const auto cut=preview.at("cutPoints").To<Array>();ASSERT_GT(cut.size(),2u);
            for(int k=0;k<3;++k)EXPECT_NEAR(end[k].To<double>(),cut.back().To<Array>()[k].To<double>(),1e-7);}
        const auto request=EditRequest(scene,{id},{{"patch",patch}});const auto result=Invoke(&scene,"CamProcess","Update",request);
        EXPECT_EQ(result,Invoke(&scene,"CamProcess","Update",request));EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
        EXPECT_EQ(start,DefinitionItem(scene,id).at("start").To<Map>());
        EXPECT_EQ(preview,LeadPreview(scene,{id}).at("items").To<Array>().front().To<Map>());
        const auto saved=Read(scene);ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
        ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(saved.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
        // Embedded persistence retains source domains and rebuilds the same closure and exit.
        iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Unified leads";
        const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");project.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
        EXPECT_EQ(preview,LeadPreview(reopened,{id}).at("items").To<Array>().front().To<Map>());std::filesystem::remove(path);
    }
}
TEST(FiveAxisLeads, InvalidClosureOrCombinedStartCannotPartiallyCommit)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto id=ids[0];const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    auto start=DefinitionItem(scene,id).at("start").To<Map>();start["parameter"]=2.;
    for(const auto patch:{Map{{"start",start},{"gapLengthMm",1.},{"overcutLengthMm",1.}},Map{{"gapLengthMm",-1.}}}){
        EXPECT_THROW(LeadPreview(scene,{id},patch),std::exception);
        EXPECT_THROW(Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",patch}})),std::exception);
        EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    }
    // A valid authored policy may fail geometry in the background. Its combined
    // start/process edit and batch assignments still form one undo transaction.
    const Map infeasible{{"start",start},{"gapLengthMm",100.}};
    EXPECT_THROW(LeadPreview(scene,{id},infeasible),std::exception);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",infeasible}}));
    try{(void)Read(scene);}catch(const std::exception&){}
    auto failed=iCAX::FiveAxisCAM::ReadState(scene);EXPECT_FALSE(failed.at("intentGenerationErrors").To<Array>().empty());
    auto authored=Invoke(&scene,"CamProcess","Read",{{"expected",failed.at("expected")},{"intentIds",Array{id}}}).at("items").To<Array>().front().To<Map>();
    EXPECT_DOUBLE_EQ(100.,authored.at("assignment").To<Map>().at("gapLengthMm").To<double>());EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"gapLengthMm",20.}}}}));
    try{(void)Read(scene);}catch(const std::exception&){}
    failed=iCAX::FiveAxisCAM::ReadState(scene);EXPECT_FALSE(failed.at("intentGenerationErrors").To<Array>().empty());
    const auto assignments=Invoke(&scene,"CamProcess","Read",{{"expected",failed.at("expected")},{"intentIds",ids}}).at("items").To<Array>();ASSERT_EQ(ids.size(),assignments.size());
    for(const auto& item:assignments)EXPECT_DOUBLE_EQ(20.,item.To<Map>().at("assignment").To<Map>().at("gapLengthMm").To<double>());
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    // The start-only menu reaches the same edit pipeline, with no process rewrite.
    const auto policy=ProcessItem(scene,id);Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{id},{{"start",start}}));
    const auto updated=ProcessItem(scene,id);for(const auto* key:{"assignment","library","posePolicy","process"})EXPECT_EQ(policy.at(key),updated.at(key))<<key;
    const auto preview=LeadPreview(scene,{id}).at("items").To<Array>().front().To<Map>();EXPECT_TRUE(preview.at("leads").To<Array>().empty());
    const auto entry=preview.at("start").To<Array>(),exit=preview.at("end").To<Array>();ASSERT_EQ(3u,entry.size());ASSERT_EQ(3u,exit.size());
    const auto tolerance=PublishedBranchValue(scene,id).Geometry.Tolerance;
    EXPECT_LE(std::hypot(entry[0].To<double>()-exit[0].To<double>(),entry[1].To<double>()-exit[1].To<double>(),entry[2].To<double>()-exit[2].To<double>()),tolerance);
    const auto after=Read(scene);EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));EXPECT_EQ(before.at("placements"),after.at("placements"));
}

TEST(FiveAxisCutEditing, ReverseRebuildsExistingClosureAndExitWithoutChangingProcess)
{
    for(const bool gap:{false,true}){
        Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();
        auto start=DefinitionItem(scene,id).at("start").To<Map>();start["nativeParameter"]=1.;
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"start",start},{"leadIn",Lead("Arc",.3)},{"leadOut",Lead("Line",.4)},
            {"overcutLengthMm",gap?0.:2.},{"gapLengthMm",gap?2.:0.}}}}));
        const auto before=LeadPreview(scene,{id}).at("items").To<Array>().front().To<Map>(),policy=ProcessItem(scene,id),definition=DefinitionItem(scene,id);
        Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
        const auto after=LeadPreview(scene,{id}).at("items").To<Array>().front().To<Map>();
        EXPECT_EQ(before.at("start"),after.at("start"));EXPECT_NE(before.at("end"),after.at("end"));
        EXPECT_EQ(after.at("end"),after.at("leads").To<Array>().back().To<Map>().at("anchor"));
        EXPECT_EQ(policy,ProcessItem(scene,id));EXPECT_EQ(definition.at("feature"),DefinitionItem(scene,id).at("feature"));
        Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));EXPECT_EQ(before,LeadPreview(scene,{id}).at("items").To<Array>().front().To<Map>());
    }
}
