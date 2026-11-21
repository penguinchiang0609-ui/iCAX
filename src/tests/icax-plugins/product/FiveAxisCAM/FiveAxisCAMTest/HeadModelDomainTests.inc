namespace {
    Map Head(Scene& scene){const auto state=Read(scene);for(const auto& value:state.at("setups").To<Array>()){const auto setup=value.To<Map>();if(setup.at("id")==state.at("activeSetupId"))return setup.at("headModel").To<Map>();}throw std::runtime_error("Active head setup missing");}
    Map ImportTestHead(Scene& scene,const File& file){auto r=GenerateRequest(scene);r["sourcePath"]=file.Utf8();return r;}
    Map CalibrateTestHead(Scene& scene){auto r=GenerateRequest(scene);r["kind"]=Text("Imported");r["tcpLocalPosition"]=Array{1.,2.,3.};r["beamLocalDirection"]=Array{0.,0.,-2.};return r;}
}
TEST(FiveAxisHead, EmbeddedImportCalibrationUndoAndReopenDoNotAlterCutGeometry)
{
    Scene scene;PrepareOrderPart(scene,true,"TableTable");const auto paths=Generated(scene);const auto workpiece=Read(scene).at("workpieces");
    EXPECT_EQ("Builtin",Head(scene).at("kind").To<Text>());
    File source(BRepPrimAPI_MakeBox(10.,15.,50.).Shape());const auto request=ImportTestHead(scene,source);
    const auto imported=Wait(scene,Invoke(&scene,"CamHead","Import",request));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    EXPECT_EQ(imported,Invoke(&scene,"CamHead","Import",request));EXPECT_FALSE(Head(scene).at("calibrated").To<bool>());
    EXPECT_EQ(workpiece,Read(scene).at("workpieces"));EXPECT_TRUE(Read(scene).at("fixtures").To<Array>().empty());EXPECT_EQ(paths,Generated(scene));
    const auto calibration=CalibrateTestHead(scene),result=Invoke(&scene,"CamHead","Configure",calibration);
    EXPECT_EQ(result,Invoke(&scene,"CamHead","Configure",calibration));
    const auto head=Head(scene);EXPECT_TRUE(head.at("calibrated").To<bool>());EXPECT_EQ((Array{0.,0.,-1.}),head.at("beamLocalDirection").To<Array>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_FALSE(Head(scene).at("calibrated").To<bool>());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(head,Head(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Cutting head";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());std::filesystem::remove(source.Path);
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(head,Head(reopened));EXPECT_EQ(paths,Generated(reopened));
    const auto preview=head.at("previewRef").To<Map>();EXPECT_TRUE(reopened.Resources().Get<iCAX::Resource::CFlatBufferResource>(preview.at("url").To<Text>(),std::stoull(preview.at("version").To<Text>())));
    auto reset=GenerateRequest(reopened);reset["action"]=Text("select");reset["instanceId"]=Text();Invoke(&reopened,"CamHead","Instance",reset);EXPECT_EQ("Builtin",Head(reopened).at("kind").To<Text>());EXPECT_TRUE(Head(reopened).at("machineTypes").To<Array>().empty());
    ASSERT_TRUE(reopened.Database().Undo());EXPECT_EQ(head,Head(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisHead, InvalidCalibrationConflictAndCancellationNeverPublish)
{
    Scene scene;File stock(BRepPrimAPI_MakeBox(10.,20.,30.).Shape());Imported(scene,stock,"TableTable");File source(BRepPrimAPI_MakeBox(10.,15.,50.).Shape());const auto before=Head(scene);
    auto task=Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source));
    Invoke(&scene,"CamHead","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ(before,Head(scene));
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source))).at("status").To<Text>());
    const auto imported=Head(scene);auto invalid=CalibrateTestHead(scene);invalid["beamLocalDirection"]=Array{0.,0.,0.};
    EXPECT_THROW(Invoke(&scene,"CamHead","Configure",invalid),std::exception);EXPECT_EQ(imported,Head(scene));
    invalid=CalibrateTestHead(scene);invalid["tcpLocalPosition"]=Array{Text("bad"),0.,0.};EXPECT_THROW(Invoke(&scene,"CamHead","Configure",invalid),std::exception);
    auto stale=CalibrateTestHead(scene);Invoke(&scene,"CamHead","Configure",CalibrateTestHead(scene));
    EXPECT_THROW(Invoke(&scene,"CamHead","Configure",stale),std::exception);
}
