TEST(ProductAssemblyBindingSDO, TeeFlatContactUsesVerifiedRealProductFaceDirection)
{
    const auto projectID=iCAX::Data::GenerateNewUUID();
    Scene scene(projectID,projectID);
    const auto generated=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("louver-window")},
        {"assemblyPlanningMode",std::string("external_templates")},
        {"arrayMode",std::string("count")}, {"bladeCount",std::int64_t{1}},
        {"bladeDirectionAngle",90.0}, {"bladeRollAngle",0.0},
        {"frameWidth",80.0}, {"frameDepth",80.0},
        {"bladeWidth",40.0}, {"bladeDepth",40.0},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    const ObjectMap identity{
        {"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")},
    };
    const auto listed=invoke(scene,"GetProductAssemblyBindings",identity);
    auto request=assemblyBindingTeePreviewRequest(identity,listed,"frame.bottom","saddle");
    request["templateId"]=std::string("t-contact-fit");
    request["parameters"]=ObjectMap{{"intersectionAngle",90.0},{"markContact",false}};
    auto wrongFace=request;
    auto wrongParticipants=wrongFace.at("participants").To<VariantArray>();
    for(auto& value:wrongParticipants) {
        auto participant=value.To<ObjectMap>();
        if(participant.at("role").To<std::string>()!="host")continue;
        auto anchor=participant.at("anchor").To<ObjectMap>();
        const auto declared=anchor.at("face").To<std::string>();
        anchor["face"]=declared=="top" ? std::string("bottom") : std::string("top");
        participant["anchor"]=std::move(anchor);
        value=std::move(participant);
    }
    wrongFace["participants"]=std::move(wrongParticipants);
    EXPECT_THROW(invoke(scene,"PreviewProductAssemblyBinding",wrongFace),std::exception);
    const auto checked=invoke(scene,"PreviewProductAssemblyBinding",request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    const auto checks=checked.at("fitChecks").To<VariantArray>();
    ASSERT_EQ(checks.size(),1u);
    EXPECT_EQ(checks.front().To<ObjectMap>().at("kind").To<std::string>(),
        "end-side-flat-contact");
    const auto measurement=checked.at("interfaceMeasurement").To<ObjectMap>();
    EXPECT_EQ(measurement.at("status").To<std::string>(),"measured");
    EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")),1.0e-4);
}
