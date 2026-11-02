Map PreviewRequest(const File& file, const Text& key = "native-preview")
{ return {{"sourcePath",file.Utf8()},{"previewKey",key}}; }
Map PreviewIdentity(const Map& task)
{ return {{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}}; }
Map WaitPreview(Scene& scene, const Map& task)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        const auto state = Invoke(&scene,"CamWorkpiece","QueryFilePreview",PreviewIdentity(task));
        if (state.at("status").To<Text>() != "Running") return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("File preview task timed out");
}
TEST(FiveAxisFilePreview, CompletesWithoutChangingEngineeringStateAndReleasesItsReceipt)
{
    Scene scene; File source;
    const auto initial=Read(scene);
    const auto task=Invoke(&scene,"CamWorkpiece","PreviewFile",PreviewRequest(source));
    EXPECT_EQ(task.at("status").To<Text>(),"Running");
    EXPECT_EQ(Read(scene),initial);
    const auto result=WaitPreview(scene,task);
    ASSERT_EQ(result.at("status").To<Text>(),"Completed");
    EXPECT_FALSE(result.at("result").To<Map>().at("wireframe").To<Map>().at("polylines").To<Array>().empty());
    EXPECT_EQ(Read(scene),initial);
    EXPECT_FALSE(scene.Database().Undo());
    EXPECT_TRUE(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(task)).at("released").To<bool>());
    EXPECT_FALSE(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(task)).at("released").To<bool>());
    EXPECT_THROW(Invoke(&scene,"CamWorkpiece","QueryFilePreview",PreviewIdentity(task)),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamWorkpiece","PreviewFile",PreviewRequest(source)),std::exception);
}
TEST(FiveAxisFilePreview, SupersededAndCancelledPreviewsDoNotBlockImportOrPublish)
{
    Scene scene; File source;
    const auto initial=Read(scene);
    const auto old=Invoke(&scene,"CamWorkpiece","PreviewFile",PreviewRequest(source));
    const auto current=Invoke(&scene,"CamWorkpiece","PreviewFile",PreviewRequest(source));
    EXPECT_NE(old.at("taskId"),current.at("taskId"));
    EXPECT_FALSE(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(old)).at("released").To<bool>());
    auto stale=PreviewIdentity(current);stale["epoch"]=Text("another-scene");
    EXPECT_THROW(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",stale),std::exception);
    EXPECT_EQ(Read(scene),initial);
    const auto imported=Imported(scene,source);
    EXPECT_EQ(Read(scene).at("workpieces").To<Array>().size(),1u);
    EXPECT_TRUE(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(current)).at("released").To<bool>());
    EXPECT_THROW(Invoke(&scene,"CamWorkpiece","QueryFilePreview",PreviewIdentity(current)),std::exception);
    EXPECT_TRUE(scene.Database().Undo());
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
    EXPECT_FALSE(scene.Database().Undo());
}
TEST(FiveAxisFilePreview, ReportsParserFailureWithoutChangingTheScene)
{
    Scene scene; File source;
    { std::ofstream file(source.Path,std::ios::binary|std::ios::trunc); file << "invalid STEP"; }
    const auto initial=Read(scene);
    const auto task=Invoke(&scene,"CamWorkpiece","PreviewFile",PreviewRequest(source));
    const auto result=WaitPreview(scene,task);
    EXPECT_EQ(result.at("status").To<Text>(),"Failed");
    EXPECT_FALSE(result.at("error").To<Text>().empty());
    EXPECT_EQ(Read(scene),initial);
    EXPECT_TRUE(Invoke(&scene,"CamWorkpiece","ReleaseFilePreview",PreviewIdentity(task)).at("released").To<bool>());
}
