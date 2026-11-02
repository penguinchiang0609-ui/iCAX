TEST(FiveAxisStandardHoles, FixedDirectionRoundTripPreservesGeometryProcessAndContourPose)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,.2,nullptr,0,true).at("createdIntentIds").To<Array>().front();
    const auto original=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();
    EXPECT_EQ("StandardHole",original.at("cutType").To<Text>());EXPECT_TRUE(original.at("canBeStandardHole").To<bool>());
    const auto policy=ProcessItem(scene,id);const auto pose=policy.at("posePolicy").To<Map>();
    EXPECT_EQ("FixedDirection",pose.at("mode").To<Text>());EXPECT_EQ(Array({0.,0.,1.}),pose.at("direction").To<Array>());
    EXPECT_EQ("NotPlanned",CompiledItem(scene,id).at("motionState").To<Text>());
    const auto before=Read(scene);const auto definition=DefinitionItem(scene,id);
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","ResolveStart",{{"expected",before.at("expected")},{"intentId",id},{"nearPoint",Array{0.,4.,10.}},
        {"maximumDistanceMm",.2},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}}));
    EXPECT_THROW(Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"leadAngleRad",.2}}}})),std::exception);
    EXPECT_EQ(before,Read(scene));
    const auto request=EditRequest(scene,{id},{{"cutType",Text("Contour")}});const auto undo=scene.Database().GetUndoArray().size();
    const auto result=Invoke(&scene,"CamCut","SetType",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","SetType",request));
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());EXPECT_EQ("Continuous",ProcessItem(scene,id).at("posePolicy").To<Map>().at("mode").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));ASSERT_TRUE(scene.Database().Redo());
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"leadAngleRad",.2}}}}));
    const auto editedPose=ProcessItem(scene,id).at("pose");
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("StandardHole")}}));
    const auto after=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();
    for(const auto* key:{"id","number","curveRef","sourceRecipeRef","sourceKey","processRef","start","reversed"})EXPECT_EQ(original.at(key),after.at(key));
    const auto noOpUndo=scene.Database().GetUndoArray().size();Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("StandardHole")}}));
    EXPECT_EQ(noOpUndo,scene.Database().GetUndoArray().size());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Standard hole round trip";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(after,Read(reopened).at("intentToolpaths").To<Array>().front().To<Map>());
    Invoke(&reopened,"CamCut","SetType",EditRequest(reopened,{id},{{"cutType",Text("Contour")}}));
    EXPECT_EQ(editedPose,ProcessItem(reopened,id).at("pose"));EXPECT_DOUBLE_EQ(.2,ProcessItem(reopened,id).at("posePolicy").To<Map>().at("leadAngleRad").To<double>());
    std::filesystem::remove(path);
}
TEST(FiveAxisStandardHoles, CircleRectangleSlotAndOuterContourUseProvenGeometry)
{
    auto part=BRepPrimAPI_MakeBox(gp_Pnt(-40,-30,0),100,60,10).Shape();
    part=BRepAlgoAPI_Cut(part,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),4,12).Shape()).Shape();
    part=BRepAlgoAPI_Cut(part,BRepPrimAPI_MakeBox(gp_Pnt(-30,-5,-1),10,10,12).Shape()).Shape();
    const auto a=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25,0,-1),gp::DZ()),4,12).Shape();
    const auto b=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(35,0,-1),gp::DZ()),4,12).Shape();
    const auto slot=BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(a,b).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(25,-4,-1),10,8,12).Shape()).Shape();
    BRepAlgoAPI_Cut cutSlot(part,slot);cutSlot.SimplifyResult();part=cutSlot.Shape();Scene scene;File source(part);const auto placement=Imported(scene,source);
    auto extraction=ExtractRequest(scene,placement);extraction["geometryOnly"]=true;
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal));
    const auto state=Read(scene);std::set<Text> shapes;Array standards;Variant outer;
    for(const auto& value:state.at("intentToolpaths").To<Array>()) {
        const auto cut=value.To<Map>();
        if(PolicyBody(scene,cut.at("featureRef").To<Map>()).at("boundary").To<Map>().at("regionRelation").To<Text>()=="InnerLoop") {
            EXPECT_EQ("StandardHole",cut.at("cutType").To<Text>());shapes.insert(cut.at("geometryKind").To<Text>());standards.push_back(cut.at("id"));
        } else {outer=cut.at("id");EXPECT_EQ("Contour",cut.at("cutType").To<Text>());EXPECT_FALSE(cut.at("canBeStandardHole").To<bool>());}
    }
    EXPECT_EQ((std::set<Text>{"Circle","Rectangle","Slot"}),shapes);ASSERT_EQ(3u,standards.size());
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,standards,{{"cutType",Text("Contour")}}));
    auto bad=standards;bad.push_back(outer);const auto before=Read(scene);
    EXPECT_THROW(Invoke(&scene,"CamCut","SetType",EditRequest(scene,bad,{{"cutType",Text("StandardHole")}})),std::exception);EXPECT_EQ(before,Read(scene));
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,standards,{{"cutType",Text("StandardHole")}}));
    for(const auto& id:standards) {
        const auto prior=Read(scene),definition=DefinitionItem(scene,id);
        const auto span=definition.at("sourceSpans").To<Array>().front().To<Map>();
        const Map anchor{{"sourceSpanId",span.at("sourceSpanId")},{"nativeParameter",(span.at("first").To<double>()+span.at("last").To<double>())/2}};
        const auto pieces=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{Map{{"intentId",id},{"points",Array{anchor}}}})).at("intentIds").To<Array>();
        ASSERT_EQ(2u,pieces.size());
        for(const auto& piece:pieces) {
            const auto fragment=DefinitionItem(scene,piece);
            EXPECT_EQ("Contour",fragment.at("cutType").To<Text>());EXPECT_FALSE(fragment.at("closed").To<bool>());
            EXPECT_EQ("Continuous",ProcessItem(scene,piece).at("posePolicy").To<Map>().at("mode").To<Text>());
        }
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(prior.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    }
}
TEST(FiveAxisStandardHoles, DirectSplitRestoresSavedContourPoseAtomicallyAndReopens)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0.,nullptr,0,true).at("createdIntentIds").To<Array>().front();
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("Contour")}}));
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"leadAngleRad",.2}}}}));
    const auto contourPose=ProcessItem(scene,id).at("pose");
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("StandardHole")}}));
    const auto before=Read(scene),old=before.at("intentToolpaths").To<Array>().front().To<Map>();
    const auto undo=scene.Database().GetUndoArray().size();
    const auto pick=Invoke(&scene,"CamCut","ResolveSplitPoint",{{"expected",before.at("expected")},{"intentId",id},
        {"nearPoint",Array{0.,4.05,10.}},{"maximumDistanceMm",.2},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}});
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    const auto request=SplitRequest(scene,{Map{{"intentId",id},{"points",Array{pick.at("anchor")}}}});
    class RejectSplit final:public iCAX::Database::IChecker {
    public:bool Reject=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text& error)override{if(Reject){Reject=false;error="Injected standard hole split failure";return false;}return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&)override{return true;}
    };
    scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<RejectSplit>(),"CEditableOperationComponent");
    EXPECT_THROW(Invoke(&scene,"CamCut","Split",request),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    const auto result=Invoke(&scene,"CamCut","Split",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","Split",request));
    ASSERT_EQ(2u,result.at("intentIds").To<Array>().size());EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    const auto after=Read(scene).at("intentToolpaths").To<Array>();
    for(const auto& value:after) {
        const auto fragment=value.To<Map>(),definition=DefinitionItem(scene,fragment.at("id")),process=ProcessItem(scene,fragment.at("id"));
        EXPECT_EQ("Contour",definition.at("cutType").To<Text>());EXPECT_FALSE(definition.at("closed").To<bool>());
        EXPECT_FALSE(definition.at("canBeStandardHole").To<bool>());EXPECT_EQ(contourPose,process.at("pose"));
        EXPECT_DOUBLE_EQ(.2,process.at("posePolicy").To<Map>().at("leadAngleRad").To<double>());
        EXPECT_EQ(old.at("processRef"),fragment.at("processRef"));EXPECT_EQ(old.at("sourceKey"),fragment.at("sourceKey"));
    }
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Read(scene).at("intentToolpaths").To<Array>());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Direct standard hole split";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(after,Read(reopened).at("intentToolpaths").To<Array>());std::filesystem::remove(path);
}
TEST(FiveAxisStandardHoles, SplitLosesStandardProvenanceAndFailedTransactionIsRetryable)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto request=EditRequest(scene,ids,{{"cutType",Text("StandardHole")}});
    class RejectChange final:public iCAX::Database::IChecker {
    public:int Count=0;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text& error)override{if(++Count==2){error="Injected second hole failure";return false;}return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&)override{return true;}
    };
    scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<RejectChange>(),"CEditableOperationComponent");
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Invoke(&scene,"CamCut","SetType",request),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","SetType",request));
    auto conflict=request;conflict["cutType"]=Text("Contour");EXPECT_THROW(Invoke(&scene,"CamCut","SetType",conflict),std::exception);
    auto stale=request;stale["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamCut","SetType",stale),std::exception);
    const auto split=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,ids.front())}));
    for(const auto& id:split.at("intentIds").To<Array>()) {
        EXPECT_FALSE(DefinitionItem(scene,id).at("canBeStandardHole").To<bool>());
        EXPECT_THROW(Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("StandardHole")}})),std::exception);
    }
}

TEST(FiveAxisStandardHoles, StartAnchorEditsKeepFixedAxisAndCompile)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,.2,nullptr,0,true).at("createdIntentIds").To<Array>().front();
    const auto before=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();
    const auto hit=Invoke(&scene,"CamCut","ResolveStart",{{"expected",Read(scene).at("expected")},{"intentId",id},{"nearPoint",Array{0.,4.,10.}},
        {"maximumDistanceMm",.2},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}});
    Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{id},{{"start",hit.at("start")}}));
    const auto after=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();
    EXPECT_EQ("StandardHole",after.at("cutType").To<Text>());EXPECT_EQ(before.at("poseRef"),after.at("poseRef"));
    EXPECT_EQ(before.at("curveRef"),after.at("curveRef"));EXPECT_EQ(before.at("processRef"),after.at("processRef"));
    EXPECT_EQ(hit.at("point"),after.at("startPoint"));EXPECT_NE(before.at("start"),after.at("start"));
    EXPECT_EQ("NotPlanned",CompiledItem(scene,id).at("motionState").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Read(scene).at("intentToolpaths").To<Array>().front().To<Map>());
}
