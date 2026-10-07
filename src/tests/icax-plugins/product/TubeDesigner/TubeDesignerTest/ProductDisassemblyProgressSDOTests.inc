TEST(ProductDisassemblyProgress, ReportsLongStageAndStopsImmediatelyOnException)
{
    std::atomic<std::uint64_t> calls{0};
    std::mutex reportedMutex;
    std::condition_variable reported;
    const auto started=std::chrono::steady_clock::now();
    try {
        iCAX::TubeDesigner::CDisassemblyProgress progress([&](const auto& state) {
            EXPECT_EQ(state.Phase,"geometry");
            EXPECT_EQ(state.Completed,0);
            EXPECT_EQ(state.Total,41);
            { const std::lock_guard lock(reportedMutex); ++calls; }
            reported.notify_all();
            throw std::runtime_error("closed report channel");
        },std::chrono::milliseconds(5));
        progress.Set("geometry",0,41,"正在计算零件实体");
        std::unique_lock lock(reportedMutex);
        EXPECT_TRUE(reported.wait_for(lock,std::chrono::milliseconds(500),[&] { return calls.load()>=2; }));
        throw std::runtime_error("CAD failure");
    } catch(const std::runtime_error&) {}
    EXPECT_GE(calls.load(),2);
    EXPECT_LT(std::chrono::steady_clock::now()-started,std::chrono::seconds(1));
    const auto stopped=calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EXPECT_EQ(calls.load(),stopped);

    // The normal five-second interval cannot delay either completion or unwind.
    const auto immediate=std::chrono::steady_clock::now();
    { iCAX::TubeDesigner::CDisassemblyProgress progress([](const auto&) {});
      progress.Set("parts",1,1,"零件整理完成"); }
    EXPECT_LT(std::chrono::steady_clock::now()-immediate,std::chrono::seconds(1));
}

TEST(ProductInstanceSDO, FortyOnePartTabsDisassemblyReportsActivityAndValidSolids)
{
    Scene scene;
    const auto initial=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"frameManufacturingMode",std::string("segment_weld")},
        {"frameJoinType",std::string("miter_45")},
        {"horizontalEndConnection",std::string("tabs")},
        {"verticalEndConnection",std::string("tabs")}
    }).at("tubeDesigner").To<ObjectMap>();
    const auto members=initial.at("members").To<VariantArray>();
    ASSERT_EQ(members.size(),41);
    const auto id=initial.at("product").To<ObjectMap>().at("entityId").To<std::string>();
    std::mutex reportsMutex;
    std::vector<ObjectMap> reports;
    std::vector<std::chrono::steady_clock::time_point> received;
    const auto start=std::chrono::steady_clock::now();
    const auto result=invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{id}}},nullptr,[&](const auto& payload) {
        const auto report=iCAX::Data::VariantSerializer::Deserialize(
            std::string(payload.begin(),payload.end())).To<ObjectMap>();
        const std::lock_guard lock(reportsMutex);
        reports.push_back(report);received.push_back(std::chrono::steady_clock::now());
    }).at("tubeDesigner").To<ObjectMap>();
    const auto finished=std::chrono::steady_clock::now();
    double disassemblyBudget=10.0;
    char* budgetText=nullptr;std::size_t budgetLength=0;
    (void)_dupenv_s(&budgetText,&budgetLength,"ICAX_DISASSEMBLY_BUDGET_SECONDS");
    if(budgetText) {
        const auto budget=std::strtod(budgetText,nullptr);
        EXPECT_GT(budget,0);
        disassemblyBudget=budget;
        std::free(budgetText);
    }
    EXPECT_LE(std::chrono::duration<double>(finished-start).count(),disassemblyBudget);
    const auto parts=result.at("parts").To<VariantArray>();
    EXPECT_LT(parts.size(),41u);
    std::uint64_t unitQuantity=0;
    for(const auto& value:parts)unitQuantity+=value.To<ObjectMap>().at("unitQuantity").To<std::uint64_t>();
    ASSERT_EQ(unitQuantity,41u);
    const auto actualMembers=result.at("members").To<VariantArray>();
    ASSERT_EQ(actualMembers.size(),members.size());
    for(std::size_t index=0;index<members.size();++index) {
        const auto before=members[index].To<ObjectMap>(),after=actualMembers[index].To<ObjectMap>();
        for(const char* field: {"entityId","stableKey","previewGeometryResourceId","previewGeometryResourceVersion","transform"})
            EXPECT_EQ(iCAX::Data::VariantSerializer::Serialize(before.at(field)),
                iCAX::Data::VariantSerializer::Serialize(after.at(field)))<<field;
    }
    ASSERT_FALSE(reports.empty());
    std::set<std::string> phases;
    auto previous=start;
    double maximumGap=0;
    for(std::size_t index=0;index<reports.size();++index) {
        const auto& report=reports[index];
        EXPECT_EQ(report.at("kind").To<std::string>(),"disassembly");
        const auto completed=report.at("completed").To<std::uint64_t>();
        const auto total=report.at("total").To<std::uint64_t>();
        EXPECT_LE(completed,total);
        phases.insert(report.at("phase").To<std::string>());
        maximumGap=std::max(maximumGap,std::chrono::duration<double>(received[index]-previous).count());
        previous=received[index];
    }
    maximumGap=std::max(maximumGap,std::chrono::duration<double>(finished-previous).count());
    EXPECT_LT(maximumGap,10.0);
    for(const char* phase: {"geometry","validation","parts","convert","mesh","commit","snapshot","response","completed"})
        EXPECT_TRUE(phases.contains(phase))<<phase;
    EXPECT_EQ(reports.back().at("phase").To<std::string>(),"completed");
    EXPECT_EQ(reports.back().at("completed").To<std::uint64_t>(),reports.back().at("total").To<std::uint64_t>());
    std::uint64_t validSingleSolidCount=0;
    for(const auto& value:result.at("parts").To<VariantArray>()) {
        const auto part=value.To<ObjectMap>();
        const auto resource=scene.Resources().Get<BRepModel>(part.at("manufacturingGeometryResourceId").To<std::string>(),
            part.at("manufacturingGeometryResourceVersion").To<std::uint64_t>());
        ASSERT_TRUE(resource)<<part.at("stableKey").To<std::string>();
        const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
        ASSERT_TRUE(built.bOK&&!built.Shape.IsNull());
        const auto valid=BRepCheck_Analyzer(built.Shape).IsValid();
        EXPECT_TRUE(valid);
        std::size_t solids=0;for(TopExp_Explorer solid(built.Shape,TopAbs_SOLID);solid.More();solid.Next())++solids;
        EXPECT_EQ(solids,1);
        GProp_GProps volume;BRepGProp::VolumeProperties(built.Shape,volume);
        EXPECT_GT(volume.Mass(),0);
        if(valid&&solids==1&&volume.Mass()>0)++validSingleSolidCount;
    }
    // A rejected subsequent invocation must release its reporting scope too.
    std::atomic<std::uint64_t> rejectedReports{0};
    const auto rejectedStart=std::chrono::steady_clock::now();
    EXPECT_THROW(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{std::string("missing-product")}}},nullptr,
        [&](const auto&) { ++rejectedReports; }),std::exception);
    EXPECT_LT(std::chrono::steady_clock::now()-rejectedStart,std::chrono::seconds(1));
    const auto rejectedStopped=rejectedReports.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    EXPECT_EQ(rejectedReports.load(),rejectedStopped);
    const auto evidence=std::filesystem::current_path()/"output/tests/disassembly-progress";
    std::filesystem::create_directories(evidence);
    VariantArray reportValues;for(auto& report:reports)reportValues.emplace_back(std::move(report));
    std::ofstream(evidence/"native-41-parts.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"passed",!::testing::Test::HasFailure()},{"partCount",static_cast<unsigned long long>(parts.size())},
        {"unitQuantity",unitQuantity},{"reportCount",static_cast<unsigned long long>(reports.size())},
        {"validSingleSolidCount",validSingleSolidCount},{"unchangedPreviewMemberCount",static_cast<unsigned long long>(actualMembers.size())},
        {"maximumReportGapSeconds",maximumGap},
        {"disassemblySeconds",std::chrono::duration<double>(finished-start).count()},
        {"reports",std::move(reportValues)}});
}
