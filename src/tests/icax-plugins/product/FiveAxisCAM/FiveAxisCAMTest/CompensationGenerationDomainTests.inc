namespace
{
    iCAX::GeometryData::CompositeOffsetCurve3 GeneratedExact(Scene& scene,const Map& operation)
    {
        const auto bytes=DataBytes(operation.at("curveData").To<Map>());
        iCAX::GeometryData::Persistence::Reader reader(bytes);std::uint32_t version;reader.Read(version);EXPECT_EQ(2u,version);
        iCAX::GeometryData::CompositeOffsetCurve3 curve;std::vector<iCAX::GeometryData::CompositeOffsetCurve3> removed;reader.Read(curve);reader.Read(removed);EXPECT_TRUE(reader.End());return curve;
    }
    void ClearGenerationClosure(Scene& scene,const Array& ids)
    {Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Leads"),Text("Gap"),Text("Overcut")}}}));}
}
TEST(FiveAxisCompensation, CircleRadiusClosureLeadsReverseAndExactReopen)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ClearGenerationClosure(scene,ids);
    auto start=DefinitionItem(scene,ids[0]).at("start").To<Map>();start["nativeParameter"]=1.2;
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"start",start},{"gapLengthMm",1.},{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Arc",.2)}}}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[1]},{{"patch",Map{{"overcutLengthMm",2.}}}}));
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[1]},{}));
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();const auto request=GenerateRequest(scene);
    const auto result=Wait(scene,Invoke(&scene,"CamPlan","Generate",request));ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    EXPECT_EQ(result,Invoke(&scene,"CamPlan","Generate",request));EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto generated=Generated(scene);const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
    for(const auto& value:generated.at("operations").To<Array>()){
        const auto op=value.To<Map>();const bool first=op.at("intentId")==ids[0];
        // Recognition candidate order is not geometric identity. Compare with
        // the selected intent's nominal analytic circle, never its list index.
        iCAX::GeometryData::Circle3 nominal;
        for(const auto& item:before.at("intentToolpaths").To<Array>()){const auto intent=item.To<Map>();if(intent.at("id")!=op.at("intentId"))continue;
            const auto original=IntentCurveValue(scene,intent);
            nominal=std::get<iCAX::GeometryData::Circle3>(original->Curve3.Segments.front().Curve);}
        const double radius=nominal.Radius-(first?.1:.2),cx=nominal.Placement.Location.X,cy=nominal.Placement.Location.Y;
        EXPECT_NEAR(op.at("lengthMm").To<double>(),2*M_PI*radius+(first?-.6:2.),2e-7);
        EXPECT_NEAR(op.at("compensation").To<Map>().at("perimeterMm").To<double>(),2*M_PI*radius,2e-7);
        const auto parts=op.at("parts").To<Array>();for(const auto& part:parts){const auto p=part.To<Map>();if(p.at("role").To<Text>()!="Cut")continue;
            for(const auto& point:p.at("points").To<Array>()){const auto xyz=point.To<Array>();EXPECT_NEAR(std::hypot(xyz[0].To<double>()-cx,xyz[1].To<double>()-cy),radius,1e-8);}}
        if(first){ASSERT_EQ(3u,parts.size());EXPECT_EQ(parts[0].To<Map>().at("points").To<Array>().back(),parts[1].To<Map>().at("points").To<Array>().front());
            const auto a=parts[1].To<Map>().at("points").To<Array>().back().To<Array>(),b=parts[2].To<Map>().at("points").To<Array>().front().To<Array>();for(int i=0;i<3;++i)EXPECT_NEAR(a[i].To<double>(),b[i].To<double>(),1e-9);}
        auto exact=GeneratedExact(scene,op);iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*offset);EXPECT_NEAR(evaluator.Length(exact),op.at("lengthMm").To<double>(),2e-7);
        EXPECT_TRUE(std::any_of(exact.Segments.begin(),exact.Segments.end(),[](const auto& p){return std::holds_alternative<iCAX::GeometryData::DirectionalCurveOffset3>(p.Geometry);}));
    }
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Compensated exact paths";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(generated,Generated(reopened));
    for(const auto& value:Generated(reopened).at("operations").To<Array>()){auto op=value.To<Map>();iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*offset);EXPECT_NEAR(evaluator.Length(GeneratedExact(reopened,op)),op.at("lengthMm").To<double>(),2e-7);}std::filesystem::remove(path);
}
TEST(FiveAxisCompensation, ReadOnlyPreviewMatchesGeneratedGeometryAndIsolatesFailures)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ClearGenerationClosure(scene,ids);
    const auto preview=[&]{return Invoke(&scene,"CamProcess","PreviewCompensation",{{"expected",Read(scene).at("expected")},{"intentIds",ids}});};
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();const auto shown=preview();
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    ASSERT_EQ(2u,shown.at("items").To<Array>().size());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    for(const auto& value:shown.at("items").To<Array>()){
        const auto item=value.To<Map>();ASSERT_FALSE(item.contains("error"))<<iCAX::Data::VariantSerializer::Serialize(item);
        for(const auto& v:Generated(scene).at("operations").To<Array>()){const auto op=v.To<Map>();if(op.at("intentId")==item.at("intentId")){
            EXPECT_EQ(op.at("parts"),item.at("parts"));EXPECT_EQ(op.at("compensation"),item.at("compensation"));}}
    }
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"kerf",Map{{"widthMm",20.}}}}}}));
    auto failed=preview().at("items").To<Array>();EXPECT_TRUE(failed[0].To<Map>().contains("error"));EXPECT_FALSE(failed[1].To<Map>().contains("error"));
    EXPECT_FALSE(failed[1].To<Map>().at("parts").To<Array>().empty());
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Kerf")}}}));
    for(const auto& value:preview().at("items").To<Array>()){const auto item=value.To<Map>();EXPECT_EQ(0.,item.at("widthMm").To<double>());EXPECT_TRUE(item.at("parts").To<Array>().empty());}
}
TEST(FiveAxisCompensation, RetainedSideChangesRadiusButReverseDoesNotAndCollapseKeepsLastResult)
{
    Scene scene;const auto ids=ConfirmWithPolicies(scene,.2).at("createdIntentIds").To<Array>();ClearGenerationClosure(scene,ids);
    const auto generate=[&]{return Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));};
    ASSERT_EQ("Completed",generate().at("status").To<Text>());auto op=Generated(scene).at("operations").To<Array>().front().To<Map>();EXPECT_NEAR(2*M_PI*3.9,op.at("lengthMm").To<double>(),1e-7);
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,ids,{}));ASSERT_EQ("Completed",generate().at("status").To<Text>());EXPECT_NEAR(2*M_PI*3.9,Generated(scene).at("operations").To<Array>().front().To<Map>().at("lengthMm").To<double>(),1e-7);
    Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,ids,{{"retainedSide",Text("Interior")}}));ASSERT_EQ("Completed",generate().at("status").To<Text>());EXPECT_NEAR(2*M_PI*4.1,Generated(scene).at("operations").To<Array>().front().To<Map>().at("lengthMm").To<double>(),1e-7);
    Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,ids,{{"retainedSide",Text("Exterior")}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",20.}}}}}}));const auto before=Generated(scene);
    EXPECT_EQ("Failed",generate().at("status").To<Text>());EXPECT_EQ(before,Generated(scene));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",.2},{"owner",Text("Executor")}}}}}}));EXPECT_THROW(Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)),std::exception);
}
TEST(FiveAxisCompensation, CancellationAndSupersededInputsNeverPublish)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ClearGenerationClosure(scene,ids);auto task=Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene));
    Invoke(&scene,"CamPlan","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    task=Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene));Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));EXPECT_EQ("Superseded",Wait(scene,task).at("status").To<Text>());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
}
TEST(FiveAxisCompensation, ManufacturerBracketFromImportThroughGeneratedSpatialOffset)
{
    char path[32768];ASSERT_GT(GetEnvironmentVariableA("ICAX_STAMPED_BRACKET_STEP",path,sizeof(path)),0u);
    Scene scene;const auto initial=Read(scene);const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"expected",initial.at("expected")},{"setupId",initial.at("activeSetupId")},{"sourcePath",Text(path)}}));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    const auto ids=imported.at("result").To<Map>();const auto state=Read(scene);
    const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});Array faces;
    for(const auto& value:sources.at("faceUses").To<Array>()){const auto id=value.To<Map>().at("localId").To<Text>();if(id=="4"||id=="16"||id=="7")faces.push_back(value);}ASSERT_EQ(3u,faces.size());
    Map extraction{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
        {"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},{"selection",Map{{"mode",Text("Faces")},{"faceUses",faces}}},
        {"representation",Text("Solid")},{"workpieceFamily",Text("GeneralSolid")},{"side",Map{{"outwardSign",1.}}},{"geometryOnly",true},
        {"requestedKinds",Array{Text("Circle"),Text("Rectangle"),Text("Slot"),Text("GeneralContour")}},{"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"draftRevision",Text("1")},{"draftDigest",Text("compensation-real-bracket")}};
    const auto terminal=Wait(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);
    auto create=CreateRequest(scene,extraction,terminal);Array selected;
    for(const auto& v:CandidatePage(scene,terminal).at("candidates").To<Array>()){const auto c=v.To<Map>();if(c.at("confirmationEligibility").To<Text>()=="Ready"&&c.at("machiningKind").To<Text>()=="Cut")selected.push_back(c.at("candidateId"));}
    ASSERT_GE(selected.size(),10u);create["candidateIds"]=selected;Invoke(&scene,"CamCut","Create",create);
    const auto before=Read(scene);const auto result=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
    ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    const auto generated=Generated(scene);EXPECT_EQ(selected.size(),generated.at("operations").To<Array>().size());unsigned surfacePieces=0,connectors=0;
    const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
    for(const auto& v:generated.at("operations").To<Array>()){const auto op=v.To<Map>();const auto exact=GeneratedExact(scene,op);iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*offset);
        EXPECT_NEAR(evaluator.Length(exact),op.at("lengthMm").To<double>(),1e-6);
        for(const auto& piece:exact.Segments){if(const auto* law=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&piece.Geometry)){
                if(std::holds_alternative<iCAX::GeometryData::SurfaceNormalField3>(law->ReferenceDirection))++surfacePieces;
                const auto r=offset->Evaluate(*law,{(law->Source.Range.First+law->Source.Range.Last)/2});ASSERT_EQ(iCAX::GeometryData::ECurveOffsetStatus::SamplesEvaluated,r.Status)<<r.Diagnostic;
                EXPECT_NEAR(.05,iCAX::GeometryServices::OffsetTraversal::Distance(r.Samples.front().SourcePoint,r.Samples.front().OffsetPoint),1e-8);
            }else ++connectors;}
    }
    EXPECT_GT(surfacePieces,0u);std::cout<<"COMPENSATION_MODEL operations="<<selected.size()<<" surfacePieces="<<surfacePieces<<" connectors="<<connectors<<std::endl;
}
TEST(FiveAxisCompensation, OuterTrimHonorsEachCornerPolicyAndKeepsConnectorProvenance)
{
    Scene scene;File file(RecognitionPart());const auto placement=Imported(scene,file);auto extraction=ExtractRequest(scene,placement);extraction["geometryOnly"]=true;
    const auto terminal=Wait(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto ids=Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal)).at("createdIntentIds").To<Array>();
    for(const auto& mode:std::vector<Text>{"Miter","Round","Bevel"}){
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"cornerMode",mode}}}}}}));
        const auto result=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
        bool found=false;for(const auto& value:Generated(scene).at("operations").To<Array>()){const auto op=value.To<Map>();if(op.at("boundaryRole").To<Text>()!="OuterTrim")continue;found=true;
            const auto expected=160+(mode=="Miter"?.4:mode=="Round"?2*M_PI*.05:4*std::sqrt(2.)*.05);EXPECT_NEAR(expected,op.at("lengthMm").To<double>(),2e-6);
            const auto exact=GeneratedExact(scene,op);EXPECT_GT(std::count_if(exact.Segments.begin(),exact.Segments.end(),[](const auto& p){return !p.SourceSegment;}),0);
            for(const auto& part:op.at("parts").To<Array>())for(const auto& sample:part.To<Map>().at("sourceSamples").To<Array>()){const auto s=sample.To<Map>();if(s.at("kind").To<Text>()=="Connector")EXPECT_FALSE(s.contains("sourceSpanId"));}
        }EXPECT_TRUE(found);
    }
}
