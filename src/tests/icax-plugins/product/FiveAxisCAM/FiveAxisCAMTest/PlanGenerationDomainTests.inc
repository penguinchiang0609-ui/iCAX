namespace
{
    Map GenerateRequest(Scene& scene){const auto state=Read(scene);return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",state.at("activeSetupId")}};}
    Map Generated(Scene& scene){const auto state=Read(scene);for(const auto& value:state.at("setups").To<Array>())if(value.To<Map>().at("id")==state.at("activeSetupId"))return value.To<Map>().at("generatedGeometry").To<Map>();throw std::runtime_error("Active setup missing");}
    void ZeroGenerationPolicies(Scene& scene,const Array& ids){Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Leads"),Text("Kerf"),Text("Gap"),Text("Overcut")}}}));}
}
TEST(FiveAxisGeneration, ExactCurvesLeadsClosureNormalsPersistenceAndInputInvalidation)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    auto start=DefinitionItem(scene,ids[0]).at("start").To<Map>();start["nativeParameter"]=1.2;
    const Map patch{{"start",start},{"gapLengthMm",1.},{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Arc",.2)}};
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",patch}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[1]},{{"patch",Map{{"overcutLengthMm",2.}}}}));
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[1]},{}));
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();const auto request=GenerateRequest(scene);
    const auto task=Invoke(&scene,"CamPlan","Generate",request),result=Wait(scene,task);
    ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    EXPECT_EQ(result,Invoke(&scene,"CamPlan","Generate",request));
    EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    const auto generated=Generated(scene);ASSERT_EQ("Current",generated.at("status").To<Text>());
    EXPECT_EQ("Missing",Read(scene).at("setups").To<Array>().front().To<Map>().at("planValidity").To<Text>());
    EXPECT_FALSE(generated.at("productionQualified").To<bool>());EXPECT_EQ("NotPlanned",generated.at("motionState").To<Text>());
    const auto ops=generated.at("operations").To<Array>();ASSERT_EQ(2u,ops.size());ASSERT_EQ(1u,generated.at("transitions").To<Array>().size());
    for(const auto& value:ops){const auto op=value.To<Map>();const auto bytes=DataBytes(op.at("curveData").To<Map>());
        iCAX::GeometryData::Persistence::Reader reader(bytes);std::uint32_t version;reader.Read(version);EXPECT_EQ(2u,version);
        iCAX::GeometryData::CompositeOffsetCurve3 exact;std::vector<iCAX::GeometryData::CompositeOffsetCurve3> removed;reader.Read(exact);reader.Read(removed);EXPECT_TRUE(reader.End());ASSERT_FALSE(exact.Segments.empty());
        const auto offset=scene.Services().Resolve<iCAX::GeometryServices::ICurveOffset3DService>();
        iCAX::GeometryServices::OffsetTraversal::Evaluator evaluator(*offset);const auto samples=evaluator.Sample(exact,.01,1.);
        double length=0;for(auto l:samples.Lengths)length+=l;EXPECT_NEAR(length,op.at("lengthMm").To<double>(),1e-7);
        const auto parts=op.at("parts").To<Array>();
        for(const auto& part:parts){const auto p=part.To<Map>();EXPECT_EQ(p.at("points").To<Array>().size(),p.at("directions").To<Array>().size());
            for(const auto& n:p.at("directions").To<Array>()){double norm=0;for(const auto& v:n.To<Array>())norm+=v.To<double>()*v.To<double>();EXPECT_NEAR(1.,norm,1e-10);}}
    }
    const auto transition=generated.at("transitions").To<Array>().front().To<Map>();EXPECT_FALSE(transition.at("laser").To<bool>());
    EXPECT_EQ("Setup",transition.at("frame").To<Text>());
    EXPECT_EQ(ops[0].To<Map>().at("setupEnd"),transition.at("points").To<Array>()[0]);
    EXPECT_EQ(ops[1].To<Map>().at("setupStart"),transition.at("points").To<Array>()[1]);
    // Import now centers the workpiece. Curves remain in source coordinates,
    // while rapid endpoints must include that placement exactly once.
    for(const auto& operation:ops){const auto op=operation.To<Map>();
        const auto placement=*ID::from_string(op.at("placementId").To<Text>());
        const auto component=std::dynamic_pointer_cast<CFiveAxisPlacementComponent>(scene.Database().GetEntity(placement)->GetComponent("CFiveAxisPlacementComponent"));
        ASSERT_TRUE(component);const auto matrix=component->GetTransform();
        for(const auto& names:{std::pair{"start","setupStart"},std::pair{"end","setupEnd"}}){
            const auto local=op.at(names.first).To<Array>(),world=op.at(names.second).To<Array>();
            for(int axis=0;axis<3;++axis){double expected=matrix[axis*4+3].To<double>();
                for(int column=0;column<3;++column)expected+=matrix[axis*4+column].To<double>()*local[column].To<double>();
                EXPECT_NEAR(expected,world[axis].To<double>(),1e-9);}
        }
    }
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(generated,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Generated paths";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(generated,Generated(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisGeneration, FailedCancelledSupersededAndUnsupportedNeverPublishPartialGeometry)
{
    Scene scene;EXPECT_THROW(Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)),std::exception);
    const auto ids=ConfirmPolicyPair(scene);
    ZeroGenerationPolicies(scene,ids);const auto request=GenerateRequest(scene);
    EXPECT_THROW(Invoke(nullptr,"CamPlan","Generate",request),std::exception);
    auto task=Invoke(&scene,"CamPlan","Generate",request);
    auto reused=request;reused["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamPlan","Generate",reused),std::exception);
    Invoke(&scene,"CamPlan","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    task=Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene));Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));
    EXPECT_EQ("Superseded",Wait(scene,task).at("status").To<Text>());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{ids[1]},{{"patch",Map{{"leadAngleRad",.1}}}}));
    task=Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene));const auto result=Wait(scene,task);EXPECT_EQ("Failed",result.at("status").To<Text>());
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
}
TEST(FiveAxisGeneration, RepeatedSplitKeepsOriginalMachiningSideAndCommitFailureKeepsLastResult)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    auto selection=CircleSplitSelection(scene,ids[0]);selection["points"]=Array{selection.at("points").To<Array>()[1]};
    Invoke(&scene,"CamCut","Split",SplitRequest(scene,{selection}));
    const auto first=DefinitionItem(scene,ids[0]);const auto span=first.at("sourceSpans").To<Array>().front().To<Map>();
    Map point{{"sourceSpanId",span.at("sourceSpanId")},{"nativeParameter",(span.at("first").To<double>()+span.at("last").To<double>())/2.}};
    Invoke(&scene,"CamCut","Split",SplitRequest(scene,{Map{{"intentId",ids[0]},{"points",Array{point}}}}));
    const auto task=Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)),result=Wait(scene,task);
    ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    const auto generated=Generated(scene);ASSERT_EQ(4u,generated.at("operations").To<Array>().size());
    struct RejectDerived final:iCAX::Database::IChecker
    {
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet& fields,Text& error) override{if(fields.contains("GeneratedToolpaths")){error="Injected generation publication failure";return false;}return true;}
    };
    scene.Database().GetMetaRegistry()->RegistChecker<CFiveAxisSetupComponent>(std::make_shared<RejectDerived>());
    const auto history=scene.Database().GetUndoArray().size();
    const auto rejected=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
    EXPECT_EQ("Failed",rejected.at("status").To<Text>());EXPECT_EQ(generated,Generated(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
