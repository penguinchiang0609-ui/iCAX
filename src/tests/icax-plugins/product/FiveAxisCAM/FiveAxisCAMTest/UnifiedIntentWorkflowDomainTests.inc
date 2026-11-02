namespace
{
    void AssertUnifiedCutAuthority(Scene& scene)
    {
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
            for(const auto* name:{"CIntentToolpathComponent","CIntentGeometryComponent","CCutIntentComponent","CFiveAxisCutFeatureComponent"})
                ASSERT_FALSE(entity->HasComponent(name))<<name;
        const auto root=scene.Database().GetMetaEntity();
        const auto job=std::dynamic_pointer_cast<CFiveAxisJobComponent>(root->GetComponent(CFiveAxisJobComponent::S_ClassName));
        ASSERT_TRUE(job);ASSERT_FALSE(job->GetIntentDocumentID().is_nil());
    }
}
TEST(FiveAxisIntentWorkflow, PublicCreationCompensationSplitLocalMetadataUndoAndProjectReopenUseOneModel)
{
    Text stage="create recognized source paths";
    auto stageStarted=std::chrono::steady_clock::now();
    const auto nextStage=[&](Text next){const auto now=std::chrono::steady_clock::now();
        std::cerr<<"Intent workflow: "<<stage<<" = "<<std::chrono::duration_cast<std::chrono::milliseconds>(now-stageStarted).count()<<" ms"<<std::endl;
        stage=std::move(next);stageStarted=now;};
    try
    {
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ASSERT_EQ(2,ids.size());ZeroGenerationPolicies(scene,ids);
    const auto id=ids.front();const auto initial=Read(scene);AssertUnifiedCutAuthority(scene);
    nextStage("edit and compile exact compensation");
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"kerf",Map{{"widthMm",.4}}},{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Line",.2)}}}}));
    const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);
    const auto geometry=Generated(scene);ASSERT_EQ("Current",geometry.at("status").To<Text>());
    Map operation;for(const auto& value:geometry.at("operations").To<Array>())if(value.To<Map>().at("intentId")==id)operation=value.To<Map>();ASSERT_FALSE(operation.empty());
    const auto bytes=DataBytes(operation.at("curveData").To<Map>());iCAX::GeometryData::Persistence::Reader reader(bytes);std::uint32_t version;reader.Read(version);
    iCAX::GeometryData::CompositeOffsetCurve3 exact;std::vector<iCAX::GeometryData::CompositeOffsetCurve3> removed;reader.Read(exact);reader.Read(removed);ASSERT_TRUE(reader.End());
    std::size_t laws=0;for(const auto& segment:exact.Segments)if(const auto* law=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Geometry)){++laws;EXPECT_NEAR(.2,std::abs(law->SignedDistance),1e-12);}
    EXPECT_GT(laws,0) << "The execution projection must retain one exact compensation law, without a second offset.";
    Map middle;
    for(const auto& value:operation.at("parts").To<Array>())
    {
        const auto part=value.To<Map>();if(part.at("role").To<Text>()!="Cut")continue;
        const auto samples=part.at("sourceSamples").To<Array>();ASSERT_GT(samples.size(),4);middle=samples[samples.size()/3].To<Map>();break;
    }
    ASSERT_TRUE(middle.contains("anchor"));const auto beforeSplit=Read(scene);const auto undoCount=scene.Database().GetUndoArray().size();
    nextStage("split the processed path");
    const auto receipt=Invoke(&scene,"CamCut","Split",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",beforeSplit.at("expected")},
        {"cuts",Array{Map{{"intentId",id},{"points",Array{middle.at("anchor")}}}}}});
    const auto split=Wait(scene,receipt);ASSERT_EQ("Completed",split.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(split);
    const auto children=split.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2,children.size());EXPECT_NE(children.end(),std::find(children.begin(),children.end(),id));
    EXPECT_EQ(undoCount+1,scene.Database().GetUndoArray().size());AssertUnifiedCutAuthority(scene);
    const auto after=Read(scene);EXPECT_EQ(initial.at("intentToolpaths").To<Array>().size()+1,after.at("intentToolpaths").To<Array>().size());
    auto child=children.front()==id?children.back():children.front();
    nextStage("edit one child and undo redo");
    Invoke(&scene,"CamCut","SetCommands",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
        {"intentId",child},{"beforeCommands",Text("M10")},{"afterCommands",Text("M11")}});
    const auto edited=Read(scene);for(const auto& value:edited.at("intentToolpaths").To<Array>())
    {const auto path=value.To<Map>();if(path.at("id")==child)EXPECT_EQ("M10",path.at("beforeCommands").To<Text>());else EXPECT_TRUE(path.at("beforeCommands").To<Text>().empty());}
    ASSERT_TRUE(scene.Database().Undo());for(const auto& value:Read(scene).at("intentToolpaths").To<Array>())EXPECT_TRUE(value.To<Map>().at("beforeCommands").To<Text>().empty());
    ASSERT_TRUE(scene.Database().Redo());const auto expected=Read(scene).at("intentToolpaths");
    nextStage("save and reopen the common model");
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Unified intent workflow";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(expected,Read(reopened).at("intentToolpaths"));AssertUnifiedCutAuthority(reopened);std::filesystem::remove(path);
    nextStage("complete");
    }
    catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
