namespace
{
    TopoDS_Shape BulkRecognitionPart()
    {
        BRepBuilderAPI_MakePolygon outer;
        for(const auto& p:{gp_Pnt(0,0,10),gp_Pnt(200,0,10),gp_Pnt(200,240,10),gp_Pnt(0,240,10)})outer.Add(p);
        outer.Close();BRepBuilderAPI_MakeFace face(outer.Wire());
        for(int x=0;x<20;++x)for(int y=0;y<24;++y) {
            auto wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(5+10*x,5+10*y,10),gp::DZ()),2))).Wire();
            wire.Reverse();face.Add(wire);
        }
        return face.Shape();
    }
    TopoDS_Shape RecognitionPart()
    {
        const auto plate=BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),40,40,10).Shape();
        const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),4,12).Shape();
        return BRepAlgoAPI_Cut(plate,hole).Shape();
    }
    Map ExtractRequest(Scene& scene,const Map& ids)
    {
        const auto state=Read(scene);Map query{{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}};
        const auto sources=Invoke(&scene,"CamCut","QuerySources",query);const auto ref=sources.at("sourceGeometry").To<Map>();
        const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
        auto geometry=scene.Services().Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>();
        Map chosen;
        for(const auto& value:sources.at("faceUses").To<Array>())
        {
            const auto face=value.To<Map>();const auto analysis=geometry->AnalyzeFace(*model,std::stoull(face.at("localId").To<Text>()),{});
            for(const auto& b:analysis.Boundaries)if(b.RegionRelation=="InnerLoop"&&b.GeometryKind=="Circle"&&std::abs(b.Center[2]-10)<1e-6)chosen=face;
        }
        if(chosen.empty())throw std::runtime_error("test top opening missing");
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",ids.at("setupId")},
            {"placementId",ids.at("placementId")},{"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},
            {"selection",Map{{"mode",Text("Faces")},{"faceUses",Array{chosen}}}},{"representation",Text("Solid")},{"workpieceFamily",Text("GeneralSolid")},
            {"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("Circle"),Text("Rectangle"),Text("Slot"),Text("GeneralContour")}},
            {"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"draftRevision",Text("1")},{"draftDigest",Text("native-extraction-draft-1")}};
    }
    Map WaitCut(Scene& scene,const Map& task)
    {
        for(int i=0;i<1000;++i)
        {
            const auto r=Invoke(&scene,"CamPlan","QueryStatus",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
            if(r.at("status").To<Text>()!="Running")return r;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("test recognition timeout");
    }
    Map CandidatePage(Scene& scene,const Map& terminal)
    {return Invoke(&scene,"CamCut","QueryCandidates",{{"epoch",terminal.at("epoch")},{"candidateSet",terminal.at("result").To<Map>().at("candidateSet")}});}
    iCAX::Resource::CResourceReference PolicyFixture(Scene& scene,Map body,const std::vector<iCAX::Resource::CResourceReference>& dependencies={})
    {
        body["schemaVersion"]=Text("1");const auto text=iCAX::Data::VariantSerializer::Serialize(body);
        auto binary=std::make_shared<iCAX::Resource::CBinaryResource>();binary->Content.assign(text.begin(),text.end());
        iCAX::Resource::CResourceInfo info;info.ResourceTypeID="resource.binary";info.Name="Acceptance policy";info.nVersion=1;
        info.nSchemaVersion=1;info.nMinimumReaderVersion=1;info.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;info.Dependencies=dependencies;
        iCAX::Resource::CResourceReference ref{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(ref.URL,binary,info);return ref;
    }
    Map WirePolicy(const iCAX::Resource::CResourceReference& ref){return {{"url",ref.URL},{"version",std::to_string(ref.nVersion)}};}
    Map PolicyDataFixture(Map body){body["schemaVersion"]=Text("1");return {{"revision",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"value",body}};}
    Map WirePolicy(const Map& data){return data;}
    Map CreateRequest(Scene& scene,const Map& extraction,const Map& terminal)
    {
        const auto library=PolicyDataFixture({{"schema",Text("cam.process-library")},{"name",Text("Unqualified native acceptance process")},{"productionQualified",false}});
        const Map lead{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};
        const auto process=PolicyDataFixture({{"schema",Text("cam.process-assignment")},{"libraryData",WirePolicy(library)},
            {"overrides",Array{}},{"kerf",Map{{"owner",Text("Cam")},{"widthMm",.1},{"measurementSection",Text("Surface")},{"cornerMode",Text("Miter")},{"miterLimit",4.}}},
            {"leadIn",lead},{"leadOut",lead},{"overcutLengthMm",0.},{"gapLengthMm",0.},{"layerKey",Text("cut")}});
        const auto pose=PolicyDataFixture({{"schema",Text("cam.pose-policy")},{"mode",Text("Continuous")},{"initialization",Text("SurfaceNormal")},
            {"transitionMetric",Text("ArcLength")},{"leadAngleRad",0.},{"sideAngleRad",0.},{"leadDeviationLimitRad",.5},
            {"sideDeviationLimitRad",.5},{"angularToleranceRad",.75},{"surfaceNormalPreferenceWeight",.5},{"keypoints",Array{}}});
        Array selected;for(const auto& v:CandidatePage(scene,terminal).at("candidates").To<Array>())
        {const auto c=v.To<Map>();if(c.at("confirmationEligibility").To<Text>()=="Ready")selected.push_back(c.at("candidateId"));}
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
            {"setupId",extraction.at("setupId")},{"placementId",extraction.at("placementId")},{"draftDigest",extraction.at("draftDigest")},
            {"candidateSet",terminal.at("result").To<Map>().at("candidateSet")},{"candidateIds",selected},{"decisions",Array{}},
            {"process",WirePolicy(process)},{"pose",WirePolicy(pose)}};
    }
}
TEST(FiveAxisRecognitionDomain, ExtractionIsReadOnlyIdempotentAndReturnsVersionedEvidence)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto request=ExtractRequest(scene,ids);const auto before=Read(scene);
    const auto initialUndo=scene.Database().GetUndoArray().size();
    const auto task=Invoke(&scene,"CamCut","ExtractCandidates",request);EXPECT_EQ("Extract",task.at("kind").To<Text>());
    EXPECT_EQ(task,Invoke(&scene,"CamCut","ExtractCandidates",request));
    const auto terminal=WaitCut(scene,task);ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<terminal.at("error").To<Text>();
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(initialUndo,scene.Database().GetUndoArray().size()); // extraction adds no edit
    const auto page=CandidatePage(scene,terminal);EXPECT_TRUE(page.at("coverageComplete").To<bool>());
    EXPECT_EQ(request.at("draftDigest"),page.at("draftDigest"));ASSERT_EQ(2u,page.at("candidates").To<Array>().size());
    size_t ready=0;
    for(const auto& v:page.at("candidates").To<Array>())
    {
        const auto c=v.To<Map>();if(c.at("confirmationEligibility").To<Text>()=="Ready")
        {++ready;EXPECT_EQ("ThroughLocalWall",c.at("passage").To<Map>().at("kind").To<Text>());EXPECT_EQ("NeedsProcessValidation",c.at("processEligibility").To<Text>());}
    }
    EXPECT_EQ(1u,ready);
    const auto ref=page.at("candidateSet").To<Map>();
    const auto info=scene.Resources().GetInfo(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    ASSERT_TRUE(info);EXPECT_EQ(iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,info->Persistence);
    auto changed=request;changed["draftDigest"]=Text("changed");EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",changed),std::exception);
}
TEST(FiveAxisRecognitionDomain, OppositeSideCreatesIndependentPathsAndSameSideStillRejectsDuplicates)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);
    auto first=ExtractRequest(scene,ids);first["geometryOnly"]=true;
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",first));
    ASSERT_EQ("Completed",terminal.at("status").To<Text>());
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,first,terminal));
    const auto before=Read(scene);ASSERT_EQ(2u,before.at("intentToolpaths").To<Array>().size());
    auto second=ExtractRequest(scene,ids);second["geometryOnly"]=true;
    const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});
    const auto ref=sources.at("sourceGeometry").To<Map>();
    const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    auto geometry=scene.Services().Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>();Map back;
    for(const auto& use:sources.at("faceUses").To<Array>()){
        const auto face=use.To<Map>();for(const auto& b:geometry->AnalyzeFace(*model,std::stoull(face.at("localId").To<Text>()),{}).Boundaries)
            if(b.RegionRelation=="InnerLoop"&&b.GeometryKind=="Circle"&&std::abs(b.Center[2])<1e-6)back=face;
    }
    ASSERT_FALSE(back.empty());second["selection"]=Map{{"mode",Text("Faces")},{"faceUses",Array{back}}};
    const auto backTerminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",second));
    ASSERT_EQ("Completed",backTerminal.at("status").To<Text>());
    const auto candidates=CandidatePage(scene,backTerminal).at("candidates").To<Array>();ASSERT_EQ(2u,candidates.size());
    std::set<Text> saved;for(const auto& path:before.at("intentToolpaths").To<Array>())saved.insert(path.To<Map>().at("sourceKey").To<Text>());
    for(const auto& c:candidates)EXPECT_FALSE(saved.contains(c.To<Map>().at("sourceKey").To<Text>()));
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,second,backTerminal));
    const auto both=Read(scene);ASSERT_EQ(4u,both.at("intentToolpaths").To<Array>().size());
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(both.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    second["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());second["draftId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());second["expected"]=Read(scene).at("expected");
    const auto repeated=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",second));
    const auto duplicate=CreateRequest(scene,second,repeated);
    const auto unchanged=Read(scene);
    try {Invoke(&scene,"CamCut","Create",duplicate);FAIL()<<"same-side boundary duplicated an existing path";}
    catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find("CAM_EXISTING_INTENT_SOURCE"));}
    EXPECT_EQ(unchanged,Read(scene));
}
TEST(FiveAxisRecognitionDomain, InputVersionUsePathProfileAndSceneChannelAreValidated)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto request=ExtractRequest(scene,ids);
    const auto initialUndo=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Invoke(nullptr,"CamCut","ExtractCandidates",request),std::exception);
    auto changed=request;auto ref=changed.at("sourceGeometry").To<Map>();ref["version"]=Text("999");changed["sourceGeometry"]=ref;
    EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",changed),std::exception);
    changed=request;ref=changed.at("extractionRuleRef").To<Map>();ref["version"]=Text("999");changed["extractionRuleRef"]=ref;
    EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",changed),std::exception);
    changed=request;auto selection=changed.at("selection").To<Map>();auto uses=selection.at("faceUses").To<Array>();auto use=uses.front().To<Map>();
    use["usePath"]=Array{Text("999")};uses.front()=use;selection["faceUses"]=uses;changed["selection"]=selection;
    EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",changed),std::exception);
    changed=request;changed["forcePass"]=true;EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",changed),std::exception);
    EXPECT_EQ(initialUndo,scene.Database().GetUndoArray().size()); // invalid requests add no transaction
}
TEST(FiveAxisRecognitionDomain, EditThenUndoAndNewDraftBothSupersedeOldExtraction)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);auto request=ExtractRequest(scene,ids);
    auto task=Invoke(&scene,"CamCut","ExtractCandidates",request);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));ASSERT_TRUE(scene.Database().Undo());
    EXPECT_EQ("Superseded",WaitCut(scene,task).at("status").To<Text>());
    request=ExtractRequest(scene,ids);task=Invoke(&scene,"CamCut","ExtractCandidates",request);
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["draftRevision"]=Text("2");request["draftDigest"]=Text("second-draft");
    const auto latest=Invoke(&scene,"CamCut","ExtractCandidates",request);
    EXPECT_EQ("Superseded",WaitCut(scene,task).at("status").To<Text>());
    const auto result=WaitCut(scene,latest);ASSERT_EQ("Completed",result.at("status").To<Text>());EXPECT_EQ(Text("second-draft"),CandidatePage(scene,result).at("draftDigest").To<Text>());
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));EXPECT_THROW(CandidatePage(scene,result),std::exception);
}
TEST(FiveAxisRecognitionDomain, CancelAndReopenDoNotReviveCandidateTickets)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);auto request=ExtractRequest(scene,ids);
    auto task=Invoke(&scene,"CamCut","ExtractCandidates",request);
    Invoke(&scene,"CamPlan","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",WaitCut(scene,task).at("status").To<Text>());
    request=ExtractRequest(scene,ids);task=Invoke(&scene,"CamCut","ExtractCandidates",request);const auto terminal=WaitCut(scene,task);ASSERT_EQ("Completed",terminal.at("status").To<Text>());
    const auto before=Read(scene);
    iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Recognition persistence";
    auto path=file.Path;path.replace_extension(".i5cam");project.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("setups"),Read(reopened).at("setups"));EXPECT_THROW(CandidatePage(reopened,terminal),std::exception);
    const auto again=WaitCut(reopened,Invoke(&reopened,"CamCut","ExtractCandidates",ExtractRequest(reopened,ids)));EXPECT_EQ("Completed",again.at("status").To<Text>());
    std::filesystem::remove(path);
}
TEST(FiveAxisRecognitionDomain, ConfirmationCommitsOneUndoAndConsumesTheWholeTicket)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto extraction=ExtractRequest(scene,ids);
    const auto initialUndo=scene.Database().GetUndoArray().size();
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto request=CreateRequest(scene,extraction,terminal);
    const auto result=Invoke(&scene,"CamCut","Create",request);EXPECT_EQ(1u,result.at("createdIntentIds").To<Array>().size());
    EXPECT_EQ(result,Invoke(&scene,"CamCut","Create",request));EXPECT_EQ(initialUndo+1,scene.Database().GetUndoArray().size()); // import + one creation
    const auto after=Read(scene);ASSERT_EQ(1u,after.at("intentToolpaths").To<Array>().size());
    auto reused=request;reused["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());reused["expected"]=after.at("expected");
    EXPECT_THROW(Invoke(&scene,"CamCut","Create",reused),std::exception);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
    reused["expected"]=Read(scene).at("expected");EXPECT_THROW(Invoke(&scene,"CamCut","Create",reused),std::exception);
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    const auto again=ExtractRequest(scene,ids);const auto duplicate=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",again));
    EXPECT_THROW(Invoke(&scene,"CamCut","Create",CreateRequest(scene,again,duplicate)),std::exception);
}
TEST(FiveAxisRecognitionDomain, ConfirmationRejectsStaleBlockedDuplicateAndForeignSelections)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto request=CreateRequest(scene,extraction,terminal);
    auto invalid=request;invalid["candidateIds"]=Array{Text("foreign")};EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);
    invalid=request;auto selected=request.at("candidateIds").To<Array>();selected.push_back(selected.front());invalid["candidateIds"]=selected;
    EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);
    for(const auto& value:CandidatePage(scene,terminal).at("candidates").To<Array>())
    {const auto c=value.To<Map>();if(c.at("confirmationEligibility").To<Text>()!="Ready"){invalid=request;invalid["candidateIds"]=Array{c.at("candidateId")};EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);}}
    invalid=request;invalid["draftDigest"]=Text("changed locally");EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);
    invalid=request;invalid["pose"]=invalid.at("process");EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamCut","Create",request),std::exception);
    Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,ids));invalid=request;invalid["expected"]=Read(scene).at("expected");
    EXPECT_THROW(Invoke(&scene,"CamCut","Create",invalid),std::exception);EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
}
TEST(FiveAxisRecognitionDomain, FailedCommitLeavesTicketUsableAndNoPartialIntent)
{
    class RejectCut final:public iCAX::Database::IChecker
    {
    public:
        bool Once=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text& error) override{if(Once){Once=false;error="Injected cut commit failure";return false;}return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&) override{return true;}
    };
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto extraction=ExtractRequest(scene,ids);
    const auto initialUndo=scene.Database().GetUndoArray().size();
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto request=CreateRequest(scene,extraction,terminal);
    scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<RejectCut>(),"CEditableOperationComponent");
    const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","Create",request),std::exception);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(initialUndo,scene.Database().GetUndoArray().size()); // failed creation adds no transaction
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","Create",request));EXPECT_EQ(1u,Read(scene).at("intentToolpaths").To<Array>().size());
}
TEST(FiveAxisRecognitionDomain, ConfirmedCurveAndEvidenceReopenWithoutRuntimeCandidateOrSourceFile)
{
    using namespace iCAX::CAM::Intent;using namespace iCAX::GeometryData;
    const auto exactValue=[](Scene& context,const Map& row)->SToolpathValue{
        const auto fingerprint=row.at("curveRef").To<Map>().at("revision");
        for(const auto& entity:context.Database().FilterEntities([](auto){return true;}))
            if(const auto derived=std::dynamic_pointer_cast<Editable::CEditableDerivedComponent>(entity->GetComponent(Editable::CEditableDerivedComponent::S_ClassName))){
                const auto data=derived->GetData();if(!data.contains("outputs"))continue;
                for(const auto& output:data.at("outputs").To<Array>()){const auto entry=output.To<Map>();if(entry.at("fingerprint")==fingerprint)return ToolpathFromData(entry.at("value").To<Map>());}
            }
        throw std::runtime_error("Persisted exact path missing from unified database");
    };
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto request=CreateRequest(scene,extraction,terminal);
    Invoke(&scene,"CamCut","Create",request);const auto before=Read(scene);const auto intent=before.at("intentToolpaths").To<Array>().front().To<Map>();
    // CreateRequest deliberately has a nonzero 0.1 mm kerf. The authority is
    // therefore the exact offset expression plus its nominal source/evidence.
    const auto curve=exactValue(scene,intent);ASSERT_TRUE(curve.Geometry.Closed);ASSERT_EQ(1u,curve.Geometry.Segments.size());
    ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(curve.Geometry.Segments.front().Curve));const auto law=std::get<DirectionalCurveOffset3>(curve.Geometry.Segments.front().Curve);
    ASSERT_TRUE(std::holds_alternative<Circle3>(law.Source.Curve));const auto circle=std::get<Circle3>(law.Source.Curve);EXPECT_NEAR(4.,circle.Radius,1e-12);EXPECT_NEAR(.05,std::abs(law.SignedDistance),1e-12);
    const auto evidence=ReadFiveAxisIntentMetadata(curve);EXPECT_FALSE(evidence.SourceGeometryRef.empty());EXPECT_FALSE(evidence.SourceSpans.empty());EXPECT_FALSE(evidence.SourceData.empty());EXPECT_FALSE(evidence.Feature.empty());
    std::vector<double> parameters;for(int i=0;i<=16;++i)parameters.push_back(law.Source.Range.First+(law.Source.Range.Last-law.Source.Range.First)*i/16.);
    const auto evaluate=[&](Scene& context,const SToolpathValue& path){OffsetCurvePiece3 piece;piece.Geometry=std::get<DirectionalCurveOffset3>(path.Geometry.Segments.front().Curve);piece.SourceSegment=0;return context.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>()->EvaluatePiece(piece,parameters);};
    const auto samples=evaluate(scene,curve);ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,samples.Status);ASSERT_EQ(parameters.size(),samples.Samples.size());
    for(const auto& sample:samples.Samples){EXPECT_NEAR(4.,std::hypot(sample.SourcePoint.X-circle.Placement.Location.X,sample.SourcePoint.Y-circle.Placement.Location.Y),1e-10);EXPECT_NEAR(3.95,std::hypot(sample.OffsetPoint.X-circle.Placement.Location.X,sample.OffsetPoint.Y-circle.Placement.Location.Y),1e-10);EXPECT_NEAR(circle.Placement.Location.Z,sample.OffsetPoint.Z,1e-10);}
    iCAX::ProjectFile::CProjectFile project({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Confirmed cut persistence";
    auto path=file.Path;path.replace_extension(".i5cam");project.Save(path,info,scene.Database(),scene.Resources());std::filesystem::remove(file.Path);
    Scene reopened(scene.Project,scene.GetSceneID(),false);project.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before.at("intentToolpaths"),Read(reopened).at("intentToolpaths"));
    const auto restored=exactValue(reopened,intent);EXPECT_EQ(ToData(curve),ToData(restored));
    const auto restoredSamples=evaluate(reopened,restored);ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,restoredSamples.Status);ASSERT_EQ(samples.Samples.size(),restoredSamples.Samples.size());
    for(size_t i=0;i<samples.Samples.size();++i){const auto& a=samples.Samples[i];const auto& b=restoredSamples.Samples[i];EXPECT_EQ(a.SourceParameter,b.SourceParameter);EXPECT_DOUBLE_EQ(a.OffsetPoint.X,b.OffsetPoint.X);EXPECT_DOUBLE_EQ(a.OffsetPoint.Y,b.OffsetPoint.Y);EXPECT_DOUBLE_EQ(a.OffsetPoint.Z,b.OffsetPoint.Z);}
    EXPECT_THROW(CandidatePage(reopened,terminal),std::exception);std::filesystem::remove(path);
}

TEST(FiveAxisRecognitionDomain, GeometryDefinitionChecksOppositeEdgesWithoutGrantingProcessQualification)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);auto request=ExtractRequest(scene,ids);
    request["geometryOnly"]=true;
    const auto before=Read(scene);const auto task=Invoke(&scene,"CamCut","ExtractCandidates",request);const auto terminal=WaitCut(scene,task);
    ASSERT_EQ("Completed",terminal.at("status").To<Text>());EXPECT_EQ(before,Read(scene));
    const auto page=CandidatePage(scene,terminal);ASSERT_EQ(2u,page.at("candidates").To<Array>().size());
    for(const auto& v:page.at("candidates").To<Array>()) {
        const auto c=v.To<Map>();EXPECT_EQ("Ready",c.at("confirmationEligibility").To<Text>());
        const bool opening=c.at("boundaryRole").To<Text>()=="Opening";
        EXPECT_EQ(opening?"ThroughLocalWall":"NotApplicable",c.at("passage").To<Map>().at("kind").To<Text>());
        EXPECT_EQ(opening?"OppositeEdgeConvexity":"None",c.at("passage").To<Map>().at("proof").To<Text>());
        EXPECT_EQ("Cut",c.at("machiningKind").To<Text>());
        EXPECT_EQ("NeedsProcessValidation",c.at("processEligibility").To<Text>());
        EXPECT_GT(c.at("boundary").To<Map>().at("previewPoints").To<Array>().size(),2u);
    }
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,request,terminal));
    const auto created=Read(scene);ASSERT_EQ(2u,created.at("intentToolpaths").To<Array>().size());
    for(const auto& v:created.at("intentToolpaths").To<Array>()){const auto c=v.To<Map>();EXPECT_EQ(c.at("boundaryRole").To<Text>()=="Opening"?"Exterior":"Interior",c.at("retainedSide").To<Text>());}
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(0u,Read(scene).at("intentToolpaths").To<Array>().size());
}
TEST(FiveAxisRecognitionDomain, BatchObservationDirectionOrientsTheMachiningSideWithoutChangingCad)
{
    for(const double z:{-1.,1.}){
        Scene scene;File source(RecognitionPart());const auto ids=Imported(scene,source);auto request=ExtractRequest(scene,ids);
        const auto before=Read(scene).at("workpieces");request["geometryOnly"]=true;request["side"]=Map{{"outwardSign",1.},{"observationDirection",Array{0.,0.,z*5}}};
        const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",request));
        Invoke(&scene,"CamCut","Create",CreateRequest(scene,request,terminal));
        bool hole=false;for(const auto& value:Read(scene).at("intentToolpaths").To<Array>()){
            const auto row=value.To<Map>();if(row.at("geometryKind").To<Text>()!="Circle")continue;
            hole=true;const auto direction=row.at("standardHole").To<Map>().at("direction").To<Array>();
            EXPECT_NEAR(-z,direction[2].To<double>(),1e-10);
        }
        EXPECT_TRUE(hole);EXPECT_EQ(before,Read(scene).at("workpieces"));
    }
    Scene scene;File source(RecognitionPart());const auto ids=Imported(scene,source);auto request=ExtractRequest(scene,ids);request["geometryOnly"]=true;
    request["side"]=Map{{"outwardSign",1.},{"observationDirection",Array{0.,0.,0.}}};
    EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",request),std::exception);
    request["side"]=Map{{"outwardSign",1.},{"observationDirection",Array{1.,0.}}};
    EXPECT_THROW(Invoke(&scene,"CamCut","ExtractCandidates",request),std::exception);
}
TEST(FiveAxisRecognitionDomain, AutomaticSolidSelectionIsExplicitlyRejectedInsteadOfReturningEmptySuccess)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);auto request=ExtractRequest(scene,ids);
    request["geometryOnly"]=true;request["selection"]=Map{{"mode",Text("Automatic")}};
    try {Invoke(&scene,"CamCut","ExtractCandidates",request);FAIL()<<"closed solid must not be reported as automatic surface extraction";}
    catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find("CAM_AUTOMATIC_SOLID_REQUIRES_FACE_SELECTION"));}
}
TEST(FiveAxisManualSelectionDomain, EdgeLoopUsesAuthoritativeTopologyAndPersistsCreation)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);auto request=ExtractRequest(scene,ids);
    const auto selection=request.at("selection").To<Map>();const auto face=selection.at("faceUses").To<Array>().front().To<Map>();
    const auto ref=request.at("sourceGeometry").To<Map>();
    const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto analysis=scene.Services().Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>()->AnalyzeFace(*model,std::stoull(face.at("localId").To<Text>()),{});
    Array edges;for(const auto& b:analysis.Boundaries)if(b.RegionRelation=="OuterLoop")for(const auto& span:b.Spans)
        edges.emplace_back(Map{{"kind",Text("Edge")},{"localId",std::to_string(span.EdgeID)},{"faceUse",face},{"geometryRef",ref}});
    ASSERT_EQ(4u,edges.size());std::reverse(edges.begin(),edges.end());
    request["geometryOnly"]=true;request["selection"]=Map{{"mode",Text("Edges")},{"edgeUses",edges}};
    auto broken=request;auto partial=edges;partial.pop_back();broken["selection"]=Map{{"mode",Text("Edges")},{"edgeUses",partial}};
    const auto failed=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",broken));EXPECT_EQ("Failed",failed.at("status").To<Text>());
    EXPECT_EQ(0u,Read(scene).at("intentToolpaths").To<Array>().size());
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["draftRevision"]=Text("2");
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",request));ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<terminal.at("error").To<Text>();
    const auto page=CandidatePage(scene,terminal);ASSERT_EQ(1u,page.at("candidates").To<Array>().size());
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,request,terminal));
    ASSERT_EQ(1u,Read(scene).at("intentToolpaths").To<Array>().size());
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(0u,Read(scene).at("intentToolpaths").To<Array>().size());
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(1u,Read(scene).at("intentToolpaths").To<Array>().size());
}
