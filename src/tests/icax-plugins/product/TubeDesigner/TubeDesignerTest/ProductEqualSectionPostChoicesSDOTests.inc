// Every outer-frame member uses the same actual profile. Full-section
// insertion cannot fit; welding and retained end ears are valid alternatives.
TEST(ProductEqualSectionPostChoicesSDO, ThreeFaceVFoldUsesEqualSectionWeldOrTabPosts)
{
    for (const auto* joint : {"weld", "tabs"}) {
        SCOPED_TRACE(joint);
        Scene scene;
        const ObjectMap supplied{{"faceType", std::string("three")},
            {"width", 1200.0}, {"sideWidth", 600.0}, {"height", 1800.0},
            {"accessDoorEnabled", true}, {"frameManufacturingMode", std::string("plane_v_notch")},
            {"outerFrameGrooveTool", std::string("system:v-notch-sharp")},
            {"doorFrameJoinType", std::string("miter_45")},
            {"doorLeafFrameJoinType", std::string("miter_45")},
            {"foldedPostJoint", std::string(joint)},
            {"horizontalEndConnection", std::string("insert")},
            {"verticalEndConnection", std::string("insert")}, {"assemblyClearance", 0.1}};
        const auto unchanged = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied);
        const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied), unchanged);
        const auto parameters = preview.at("parameters").To<ObjectMap>();
        for (const auto& [key, value] : supplied) EXPECT_EQ(parameters.at(key), value) << key;
        EXPECT_FALSE(parameters.contains("foldedPostInsertDepth"));
        EXPECT_FALSE(parameters.contains("foldedPostFitGap"));
        ASSERT_EQ(preview.at("items").To<VariantArray>().size(), 64u);

        auto request = parameters;
        request["templateId"] = std::string("single-face-security-window");
        const auto product = invoke(scene, "GeneratePreview", request)
            .at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
        const auto run = disassemblyCountRun(scene, product);
        const auto displayed = run->GetNeutralModel();
        const auto result = invoke(scene, "DisassembleSelected", disassemblyCountRequest(product))
            .at("tubeDesigner").To<ObjectMap>();
        EXPECT_EQ(run->GetNeutralModel(), displayed);
        EXPECT_EQ(run->GetManufacturingParameters(), parameters);
        EXPECT_EQ(result.at("product").To<ObjectMap>().at("parameters"), preview.at("parameters"));
        disassemblyCountChecksParts(scene, result, *run);

        const auto stocks = run->GetManufacturingModel().at("extensions").To<ObjectMap>()
            .at("tubeDesigner.assemblyProcessNative").To<ObjectMap>().at("stocks").To<ObjectMap>();
        EXPECT_EQ(stocks.size(), 60u);
        EXPECT_EQ(disassemblyCountUnitQuantity(result), 60u);
        std::set<std::string> expected, actual;
        for (const auto& value : preview.at("items").To<VariantArray>())
            expected.insert(value.To<ObjectMap>().at("key").To<std::string>());
        for (const auto& value : result.at("parts").To<VariantArray>())
            for (const auto& source : value.To<ObjectMap>().at("properties").To<ObjectMap>()
                    .at("manufacturing.sourceMembers").To<VariantArray>())
                EXPECT_TRUE(actual.insert(source.To<ObjectMap>().at("itemKey").To<std::string>()).second);
        EXPECT_EQ(actual, expected);

        for (const auto* key : {"outer_frame.plane.top", "outer_frame.plane.bottom",
                "outer_frame.vertical.0002", "outer_frame.vertical.0003"}) {
            SCOPED_TRACE(key);
            const auto stock = stocks.at(key).To<ObjectMap>();
            const auto profile = stock.at("profile").To<ObjectMap>();
            EXPECT_DOUBLE_EQ(finishedProductNumber(profile.at("width")), 38.0);
            EXPECT_DOUBLE_EQ(finishedProductNumber(profile.at("depth")), 38.0);
            EXPECT_DOUBLE_EQ(finishedProductNumber(profile.at("wallThickness")), 1.2);
            if (std::string(joint) != "tabs" || !std::string(key).starts_with("outer_frame.vertical.")) continue;
            const auto finished = continuousRoundTabsShape(scene, stock.at("processedGeometry").To<ObjectMap>());
            const auto recipe = stock.at("request").To<ObjectMap>();
            const double length = finishedProductNumber(recipe.at("length"));
            for (const auto* end : {"start", "end"}) {
                const auto cut = recipe.at("ends").To<ObjectMap>().at(end).To<ObjectMap>();
                EXPECT_EQ(cut.at("toolRef").To<ObjectMap>().at("id").To<std::string>(), "paired-end-tabs");
                const double trim = finishedProductNumber(cut.at("trim"));
                const double depth = finishedProductNumber(cut.at("toolParameters").To<ObjectMap>().at("tabLength"));
                const double station = std::string(end) == "start" ? trim + depth/2 : length - trim - depth/2;
                for (const double sign : {-1.0, 1.0}) {
                    EXPECT_EQ(BRepClass3d_SolidClassifier(finished,
                        gp_Pnt(station, 0, sign*(19.0 - 0.6)), 1e-5).State(), TopAbs_IN);
                    EXPECT_EQ(BRepClass3d_SolidClassifier(finished,
                        gp_Pnt(station, sign*(19.0 - 0.6), 0), 1e-5).State(), TopAbs_OUT);
                }
            }
        }
    }
}

TEST(ProductEqualSectionPostChoicesSDO, NativePreviewRejectsFullSectionPostInsertion)
{
    Scene scene;
    EXPECT_THROW(invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", ObjectMap{{"faceType", std::string("three")},
            {"frameManufacturingMode", std::string("plane_v_notch")},
            {"foldedPostJoint", std::string("insert")}}}}), std::invalid_argument);
}
