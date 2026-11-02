TEST(FiveAxisPoseOrder, StaleFixtureThenPoseEditPreservesAutomaticThroughUndoRapidsAndReopen)
{
    for(const bool local:{false,true}){
        SCOPED_TRACE(local?"local keypoint":"whole path");
        Scene scene;PrepareOrderPart(scene);const auto original=OrderedIDs(Generated(scene));
        auto order=OrderRequest(scene,"Automatic");order["options"]=Map{{"pattern",Text("Z")},{"corner",Text("BottomLeft")}};Invoke(&scene,"CamPlan","SetOrder",order);
        if(OrderedIDs(Generated(scene))==original){order=OrderRequest(scene,"Automatic");order["options"]=Map{{"pattern",Text("Z")},{"corner",Text("BottomRight")}};Invoke(&scene,"CamPlan","SetOrder",order);}
        ASSERT_NE(original,OrderedIDs(Generated(scene)))<<"Exercise an explicit spatial order distinct from the initial route";RapidConfig(scene);
        const auto before=Generated(scene);const auto expected=OrderedIDs(before);const auto id=expected.back();
        ASSERT_EQ("Automatic",before.at("ordering").To<Map>().at("mode").To<Text>());
        // This is the same native fixture command that invalidates the real
        // project before its first outer-trim keypoint is edited.
        SetPlateRecipe(scene,PlateRecipe(scene));
        const auto stale=Generated(scene);ASSERT_EQ("Stale",stale.at("status").To<Text>());
        EXPECT_EQ(expected,OrderedIDs(stale))<<"Stale geometry must retain the stored execution order too";
        const auto axisBefore=AxisRead(scene,id);Map edited;
        if(local){
            const auto samples=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();
            ASSERT_GT(samples.size(),4u);const auto anchor=samples[samples.size()/2].To<Map>().at("anchor").To<Map>();
            edited=Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor,{0.,.05,1.}));
        }else edited=Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{0.,0.,1.}));
        ASSERT_TRUE(edited.at("changed").To<bool>());
        EXPECT_EQ("Automatic",Generated(scene).at("ordering").To<Map>().at("mode").To<Text>());
        const auto axisAfter=AxisRead(scene,id);
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(axisBefore,AxisRead(scene,id));
        EXPECT_EQ("Automatic",Generated(scene).at("ordering").To<Map>().at("mode").To<Text>());
        ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(axisAfter,AxisRead(scene,id));
        EXPECT_EQ("Automatic",Generated(scene).at("ordering").To<Map>().at("mode").To<Text>());
        GenerateAxis(scene);const auto regenerated=Generated(scene);EXPECT_EQ(expected,OrderedIDs(regenerated));CheckOrderedConnections(regenerated);
        const auto rapid=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));
        ASSERT_EQ("Completed",rapid.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(rapid);
        const auto complete=Generated(scene);EXPECT_EQ(std::to_string(expected.size()-1),complete.at("rapidGeneratedCount").To<Text>());
        EXPECT_EQ("0",complete.at("rapidClearCount").To<Text>())<<"Geometry-only generation does not certify collision clearance";
        // Execution generation may rebind observed sample fingerprints. Compare
        // persistence against the authoritative reading immediately before save.
        const auto axisSaved=AxisRead(scene,id);for(const auto* key:{"mode","baseDirection","keypointCount"})EXPECT_EQ(axisAfter.at(key),axisSaved.at(key));
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Automatic pose order";
        const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
        EXPECT_EQ(complete,Generated(reopened));EXPECT_EQ(axisSaved,AxisRead(reopened,id));std::filesystem::remove(path);
    }
}
TEST(FiveAxisPoseOrder, WholeAndLocalPoseEditsPreserveExplicitAutomaticAndManualPolicies)
{
    for(const Text mode:{Text("Automatic"),Text("Manual")}){
        SCOPED_TRACE(mode);Scene scene;PrepareOrderPart(scene);auto ids=OrderedIDs(Generated(scene));std::reverse(ids.begin(),ids.end());
        Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,mode,ids));const auto policy=Generated(scene).at("ordering");
        const auto expected=OrderedIDs(Generated(scene));const auto id=expected.front();SetPlateRecipe(scene,PlateRecipe(scene));
        ASSERT_EQ("Stale",Generated(scene).at("status").To<Text>());
        Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{0.,0.,1.}));EXPECT_EQ(policy,Generated(scene).at("ordering"));
        const auto samples=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();
        ASSERT_GT(samples.size(),4u);const auto anchor=samples[samples.size()/2].To<Map>().at("anchor").To<Map>();
        Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor,{0.,.05,1.}));EXPECT_EQ(policy,Generated(scene).at("ordering"));
        GenerateAxis(scene);EXPECT_EQ(policy,Generated(scene).at("ordering"));EXPECT_EQ(expected,OrderedIDs(Generated(scene)));
    }
}
