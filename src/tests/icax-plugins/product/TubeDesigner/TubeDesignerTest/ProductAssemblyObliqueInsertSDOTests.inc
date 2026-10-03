// The hidden product fixture exposes a real, committed oblique T node. These
// cases exercise the public binding SDO, including verified source BRep replay.
TEST(ProductAssemblyBindingSDO, SingleFaceInsertAcceptsRealOrthogonalAndObliqueTNodes)
{
    for (const double angle : {90.0, 60.0}) {
        SCOPED_TRACE(angle);
        const auto projectID = iCAX::Data::GenerateNewUUID();
        Scene scene(projectID, projectID);
        const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("assembly-frame-lt")},
            {"parameters", ObjectMap{{"middleAxisAngle", angle}}},
        });
        const auto normalized = nativePreview.at("parameters").To<ObjectMap>();
        EXPECT_DOUBLE_EQ(angle, assemblyBindingNumber(normalized.at("middleAxisAngle")));
        ASSERT_EQ(5u, nativePreview.at("items").To<VariantArray>().size());
        const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
            {"templateId", std::string("assembly-frame-lt")},
            {"middleAxisAngle", angle},
        }).at("tubeDesigner").To<ObjectMap>();
        const auto product = generated.at("product").To<ObjectMap>();
        const auto productID = product.at("entityId").To<std::string>();
        EXPECT_EQ(normalized, assemblyBindingProduct(scene, productID)->GetParameters());
        const ObjectMap identity{{"productEntityId", product.at("entityId")},
            {"generationRunId", product.at("activeGenerationRunId")}};
        invoke(scene, "DisassembleSelected", ObjectMap{
            {"productEntityIds", VariantArray{product.at("entityId")}}});
        const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
        auto request = assemblyBindingTeePreviewRequest(
            identity, listed, "frame.left.0001", "singleInsert");
        request["templateId"] = std::string("t-profile-insert");
        request["parameters"] = ObjectMap{{"intersectionAngle", angle},
            {"insertDepth", 12.0}, {"fitGap", 0.2}};

        auto wrongAngle = request;
        wrongAngle["parameters"] = ObjectMap{{"intersectionAngle", angle == 90.0 ? 60.0 : 90.0},
            {"insertDepth", 12.0}, {"fitGap", 0.2}};
        EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", wrongAngle), std::exception);
        auto wrongDirection = request;
        auto participants = wrongDirection.at("participants").To<VariantArray>();
        for (auto& value : participants) {
            auto participant = value.To<ObjectMap>();
            if (participant.at("role").To<std::string>() != "branch") continue;
            auto anchor = participant.at("anchor").To<ObjectMap>();
            anchor["end"] = std::string("end");
            participant["anchor"] = std::move(anchor);
            value = std::move(participant);
        }
        wrongDirection["participants"] = std::move(participants);
        EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", wrongDirection), std::exception);

        const auto checked = invoke(scene, "PreviewProductAssemblyBinding", request);
        ASSERT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        const auto measurement = checked.at("interfaceMeasurement").To<ObjectMap>();
        ASSERT_EQ("measured", measurement.at("status").To<std::string>());
        EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")), 1e-4);
        ASSERT_EQ(2u, checked.at("geometryChanges").To<VariantArray>().size());
        auto applyRequest = identity;
        applyRequest["candidateToken"] = checked.at("candidateToken");
        const auto applied = invoke(scene, "ApplyProductAssemblyBinding", applyRequest);
        EXPECT_TRUE(applied.at("applied").To<bool>());
        EXPECT_TRUE(applied.at("manufacturingUpdated").To<bool>());
    }
}
