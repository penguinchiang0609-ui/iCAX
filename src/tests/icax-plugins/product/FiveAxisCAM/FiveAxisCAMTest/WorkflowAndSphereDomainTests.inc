namespace {
    Map WorkflowRequest(Scene& scene,const Map& part,Text action){
        auto request=ActivateRequest(scene,part);request["action"]=action;if(action=="create")request["name"]=Text("第二装夹");return request;
    }
}
TEST(FiveAxisWorkflow, ImportedCreatedAndCopiedSetupsAllowCamWithoutConfirmation)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto part=Imported(scene,source);
    const auto query=[&](const Map& ids){return Invoke(&scene,"CamCut","QuerySources",{{"expected",Read(scene).at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});};
    EXPECT_NO_THROW(query(part));
    const auto created=Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,part,"create"));
    EXPECT_NO_THROW(query(created));
    const auto copied=Invoke(&scene,"CamSetup","Copy",ActivateRequest(scene,created));
    EXPECT_NO_THROW(query(copied));
    auto lock=ActivateRequest(scene,part);lock["locked"]=true;
    Invoke(&scene,"CamSetup","SetLocked",lock);EXPECT_NO_THROW(query(part));
    lock=ActivateRequest(scene,part);lock["locked"]=false;Invoke(&scene,"CamSetup","SetLocked",lock);
    Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,part,"disable"));EXPECT_THROW(query(part),std::exception);
    Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,part,"enable"));EXPECT_NO_THROW(query(part));
    Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,copied,"remove"));
    Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,created,"remove"));
    Invoke(&scene,"CamSetup","Manage",WorkflowRequest(scene,part,"remove"));
    EXPECT_EQ(1u,Read(scene).at("workpieces").To<Array>().size());EXPECT_NO_THROW(query(part));
    EXPECT_EQ("Missing",SetupState(scene,part.at("setupId")).at("registrationPlan").To<Map>().at("status").To<Text>());
    EXPECT_THROW(Invoke(&scene,"CamSetup","ExportProgram",{{"expected",Read(scene).at("expected")}}),std::exception);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_NO_THROW(query(part));
}
TEST(FiveAxisSphere, ExteriorFacesYieldBasalTrimAndAllMarkingPathsHaveEditableToolAxes)
{
    Scene scene;const auto source=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()/u8"../../../../../../TestData/FA/刀路/刀路/AI构造刀路/带底座球体-球面底座孔-多面空移.step").u8string();
    auto state=Read(scene);const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",state.at("activeSetupId")},{"sourcePath",Text(reinterpret_cast<const char*>(source.data()),source.size())}}));
    ASSERT_EQ("Completed",imported.at("status").To<Text>());const auto part=imported.at("result").To<Map>();
    state=Read(scene);const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",part.at("setupId")},{"placementId",part.at("placementId")}});
    // Exact user selection on this fixture: exterior cylinder, annulus, sphere;
    // bottom cap and blind-hole wall/bottom faces are intentionally omitted.
    Array selected;for(const auto& v:sources.at("faceUses").To<Array>()){const auto f=v.To<Map>();if(std::set<Text>{"1","4","6"}.contains(f.at("localId").To<Text>()))selected.push_back(v);}ASSERT_EQ(3u,selected.size());
    Map extraction{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",part.at("setupId")},{"placementId",part.at("placementId")},
        {"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},{"geometryOnly",true},{"selection",Map{{"mode",Text("Faces")},{"faceUses",selected}}},
        {"representation",Text("SkinSurface")},{"workpieceFamily",Text("SheetSurface")},{"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("Circle"),Text("Rectangle"),Text("Slot"),Text("GeneralContour")}},
        {"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"draftRevision",Text("1")},{"draftDigest",Text("sphere-exterior-regression")}};
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));ASSERT_EQ("Completed",terminal.at("status").To<Text>());
    unsigned marks=0,trims=0;for(const auto& v:CandidatePage(scene,terminal).at("candidates").To<Array>()){
        const auto c=v.To<Map>();EXPECT_EQ("Ready",c.at("confirmationEligibility").To<Text>());
        if(c.at("machiningKind").To<Text>()=="Mark")++marks;
        if(c.at("boundaryRole").To<Text>()=="OuterTrim"){++trims;EXPECT_TRUE(c.at("termination").To<Map>().at("singleFaceCap").To<bool>());}
    }EXPECT_EQ(3u,marks);EXPECT_EQ(1u,trims);
    const auto created=Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal)).at("createdIntentIds").To<Array>();ASSERT_EQ(4u,created.size());
    auto axes=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",created}});EXPECT_TRUE(axes.at("errors").To<Array>().empty());ASSERT_EQ(4u,axes.at("cuts").To<Array>().size());
    for(const auto& value:axes.at("cuts").To<Array>()){const auto row=value.To<Map>();ASSERT_FALSE(row.at("samples").To<Array>().empty());
        EXPECT_NO_THROW(Invoke(&scene,"CamCut","ReadToolAxis",{{"expected",Read(scene).at("expected")},{"intentId",row.at("intentId")}}));}
    const auto mark=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();ASSERT_EQ("Mark",mark.at("machiningKind").To<Text>());
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,mark.at("id"),"Fixed",{0.,.6,.8},"Workpiece"));
    AxisNear(AxisRead(scene,mark.at("id")).at("direction"),{0.,.6,.8});
    // A blind-hole floor also has a one-face cap, but its concave joint is not
    // evidence for an exterior trim. Never promote it by wire coverage alone.
    Array blindWall;for(const auto& f:sources.at("faceUses").To<Array>())if(f.To<Map>().at("localId").To<Text>()=="2")blindWall.push_back(f);
    extraction["commandId"]=NewAxisPoint();extraction["draftId"]=NewAxisPoint();extraction["expected"]=Read(scene).at("expected");extraction["selection"]=Map{{"mode",Text("Faces")},{"faceUses",blindWall}};
    const auto blind=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));bool concaveCap=false;
    for(const auto& v:CandidatePage(scene,blind).at("candidates").To<Array>()){const auto c=v.To<Map>(),t=c.at("termination").To<Map>();
        if(t.at("singleFaceCap").To<bool>()&&t.at("entranceRelation").To<Text>()=="Concave"){concaveCap=true;EXPECT_NE("Cut",c.at("machiningKind").To<Text>());EXPECT_EQ("Blocked",c.at("confirmationEligibility").To<Text>());}}
    EXPECT_TRUE(concaveCap);
}
