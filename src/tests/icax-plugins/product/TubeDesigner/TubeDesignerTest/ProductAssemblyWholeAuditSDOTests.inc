// Included after ProductAssemblyBindingSDOTests.inc so the established native
// scene and binding helpers can exercise the independent whole-product audit.
TEST(ProductAssemblyWholeAuditSDO, ComposedCornerAndTeeCutsKeepAuditDiagnosticOnly)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID,projectID);
    const auto generated = invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("assembly-frame-lt")}})
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto baseline = assemblyBindingPartRows(invoke(scene,"DisassembleSelected",
        ObjectMap{{"productEntityIds",VariantArray{product.at("entityId")}}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baseline.size(),5u);

    const auto connectionResult = invoke(scene,"GetProductAssemblyConnections",identity);
    std::map<std::string,ObjectMap> connections;
    for (const auto& value : connectionResult.at("connections").To<VariantArray>()) {
        auto connection = value.To<ObjectMap>();
        connections.emplace(connection.at("key").To<std::string>(),std::move(connection));
    }
    const std::array<const char*,6> jointKeys{
        "frame.corner.bottom-left", "frame.corner.bottom-right",
        "frame.corner.top-left", "frame.corner.top-right",
        "frame.junction.middle-left", "frame.junction.middle-right"};
    ASSERT_EQ(connections.size(),jointKeys.size());
    for (std::size_t index=0; index<jointKeys.size(); ++index) {
        SCOPED_TRACE(jointKeys[index]);
        const auto& joint=connections.at(jointKeys[index]);
        const bool tee=std::string(jointKeys[index]).starts_with("frame.junction.");
        std::map<std::string,ObjectMap> declared;
        for (const auto& value:joint.at("properties").To<ObjectMap>()
                .at("participantAnchors").To<VariantArray>()) {
            const auto row=value.To<ObjectMap>();
            declared.emplace(row.at("itemKey").To<std::string>(),
                row.at("anchor").To<ObjectMap>());
        }
        VariantArray participants;
        for (const auto& value:joint.at("participants").To<VariantArray>()) {
            const auto member=value.To<ObjectMap>();
            const auto key=member.at("itemKey").To<std::string>();
            const auto anchor=declared.at(key);
            const auto kind=anchor.at("kind").To<std::string>();
            const std::string role=kind=="side" ? "host"
                : tee ? "branch" : participants.empty() ? "memberA" : "memberB";
            participants.emplace_back(ObjectMap{{"role",role},
                {"memberEntityId",member.at("memberEntityId")},
                {"itemKey",key},{"anchor",anchor}});
        }
        const auto state=invoke(scene,"GetProductAssemblyBindings",identity);
        auto request=identity;
        request["sourceRevision"]=state.at("sourceRevision");
        request["revision"]=state.at("revision");
        request["connectionKey"]=std::string(jointKeys[index]);
        request["templateId"]=std::string(tee ? "two-end-middle" : "two-end-end-angle");
        request["participants"]=std::move(participants);
        request["parameters"]=tee
            ? ObjectMap{{"interfaceMode",std::string("singleInsert")},
                {"intersectionAngle",90.0},{"fitGap",0.2}}
            : ObjectMap{{"jointAngle",90.0},{"fitGap",0.0},
                {"planeRotation",0.0}};
        const auto checked=invoke(scene,"PreviewProductAssemblyBinding",request);
        ASSERT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        const auto candidateAudit=checked.at("wholeGeometryAudit").To<ObjectMap>();
        EXPECT_EQ(candidateAudit.at("scope").To<std::string>(),
            "finished-product-non-neighbor-solids");
        EXPECT_FALSE(candidateAudit.at("interfaceFitCertified").To<bool>());
        auto apply=identity;
        apply["candidateToken"]=checked.at("candidateToken");
        ASSERT_TRUE(invoke(scene,"ApplyProductAssemblyBinding",apply)
            .at("applied").To<bool>());
    }
    const auto result=invoke(scene,"GetProductAssemblyBindings",identity);
    const auto plan=result.at("manufacturingPlan").To<ObjectMap>();
    ASSERT_EQ(plan.at("selectedConnectionCount").To<std::uint64_t>(),6u);
    ASSERT_EQ(plan.at("unassignedConnectionCount").To<std::uint64_t>(),0u);
    const auto audit=plan.at("wholeGeometryAudit").To<ObjectMap>();
    ASSERT_EQ(audit.at("status").To<std::string>(),"measured")
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(audit);
    EXPECT_EQ(audit.at("partCount").To<std::uint64_t>(),5u);
    EXPECT_EQ(audit.at("checkedNonNeighborPairCount").To<std::uint64_t>(),4u);
    EXPECT_TRUE(audit.at("conflicts").To<VariantArray>().empty());
    EXPECT_FALSE(audit.at("interfaceFitCertified").To<bool>());
    const auto nodeFinal=plan.at("nodeFinalAudit").To<ObjectMap>();
    ASSERT_EQ(nodeFinal.at("status").To<std::string>(),"measured")
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(nodeFinal);
    EXPECT_EQ(nodeFinal.at("nodeCount").To<std::uint64_t>(),6u);
    EXPECT_EQ(nodeFinal.at("measuredNodeCount").To<std::uint64_t>(),6u);
    EXPECT_EQ(nodeFinal.at("conflictNodeCount").To<std::uint64_t>(),0u);
    EXPECT_EQ(nodeFinal.at("unsupportedNodeCount").To<std::uint64_t>(),0u);
    EXPECT_FALSE(nodeFinal.at("interfaceFitCertified").To<bool>());
    for (const auto& value:nodeFinal.at("nodes").To<VariantArray>()) {
        const auto row=value.To<ObjectMap>();
        EXPECT_EQ(row.at("status").To<std::string>(),"measured")
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(row);
        EXPECT_LE(row.at("intersectionVolumeMm3").To<double>(),
            row.at("penetrationToleranceMm3").To<double>());
    }
    EXPECT_EQ(plan.at("fitStatus").To<std::string>(),"not-verified");
    EXPECT_EQ(plan.at("readiness").To<std::string>(),"not-ready");
    const auto again=invoke(scene,"GetProductAssemblyBindings",identity)
        .at("manufacturingPlan").To<ObjectMap>()
        .at("wholeGeometryAudit").To<ObjectMap>();
    EXPECT_EQ(again.at("status").To<std::string>(),"measured");
    EXPECT_EQ(again.at("checkedNonNeighborPairCount").To<std::uint64_t>(),4u);
    const auto againNodes=invoke(scene,"GetProductAssemblyBindings",identity)
        .at("manufacturingPlan").To<ObjectMap>()
        .at("nodeFinalAudit").To<ObjectMap>();
    EXPECT_EQ(againNodes.at("status").To<std::string>(),"measured");
    EXPECT_EQ(againNodes.at("measuredNodeCount").To<std::uint64_t>(),6u);
}
