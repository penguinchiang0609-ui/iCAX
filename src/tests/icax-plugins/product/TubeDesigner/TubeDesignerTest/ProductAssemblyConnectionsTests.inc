TEST(ProductAssemblyConnectionsSDO, ExplicitIdentityMappingRejectsUnknownAndMissingPartKinds)
{
    Scene scene;
    const auto generated=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"width",400.0},{"height",400.0},{"accessDoorEnabled",false},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    const ObjectMap request{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto runID=iCAX::Data::uuid::from_string(product.at("activeGenerationRunId").To<std::string>());
    ASSERT_TRUE(runID.has_value());
    const auto entity=scene.Database().GetEntity(*runID);
    ASSERT_TRUE(entity);
    const auto run=std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        entity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(run);
    EXPECT_NO_THROW(invoke(scene,"GetProductAssemblyConnections",request));
    auto identity=iCAX::TemplateRuntime::CTemplateCodec::AdaptDisplayModel(run->GetNeutralModel());
    identity["template"]=ObjectMap{{"id",run->GetTemplateID()},{"version",run->GetTemplateVersion()},
        {"packageDigest",run->GetPackageDigest()}};
    identity["parameters"]=product.at("parameters");
    auto extensions=identity.at("extensions").To<ObjectMap>();
    extensions["tubeDesigner.geometryPurpose"]=std::string("manufacturing");
    extensions["tubeDesigner.productStockMappingVersion"]=1ull;
    identity["extensions"]=extensions;
    for(const bool missing:{false,true}) {
        auto rejected=identity;auto items=rejected.at("items").To<VariantArray>();
        for(auto& value:items) {
            auto item=value.To<ObjectMap>();auto properties=item.at("properties").To<ObjectMap>();
            properties["manufacturing.sourceMembers"]=VariantArray{ObjectMap{
                {"itemKey",item.at("key")},{"mappingKind",std::string("identity")}}};
            if(missing)properties.erase("manufacturing.partKind");
            else properties["manufacturing.partKind"]=std::string("bogus");
            item["properties"]=properties;value=item;
        }
        rejected["items"]=items;
        ASSERT_TRUE(run->SetManufacturingModel(rejected));
        try {
            (void)invoke(scene,"GetProductAssemblyConnections",request);
            FAIL()<<"unknown or missing kind accepted with a complete explicit identity mapping";
        } catch(const std::exception& error) {
            EXPECT_NE(std::string::npos,std::string(error.what()).find(
                missing ? "manufacturing.partKind" : "制造构件类型不支持"))<<error.what();
        }
    }
}

TEST(ProductAssemblyConnectionsSDO, CommittedRelationshipsTrackActualMembersPartsAndRunVersion)
{
    Scene scene;
    const auto added = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = added.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const auto runID = product.at("activeGenerationRunId").To<std::string>();
    const ObjectMap request{{"productEntityId", productID}, {"generationRunId", runID}};
    const auto initial = invoke(scene, "GetProductAssemblyConnections", request);
    EXPECT_EQ(productID, initial.at("productEntityId").To<std::string>());
    EXPECT_EQ(runID, initial.at("generationRunId").To<std::string>());
    EXPECT_EQ("committed-product-model", initial.at("source").To<std::string>());
    EXPECT_FALSE(initial.at("modelOutdated").To<bool>());
    std::map<std::string, std::string> memberIDs;
    for (const auto& value : added.at("members").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        memberIDs.emplace(member.at("stableKey").To<std::string>(),
            member.at("entityId").To<std::string>());
    }
    const auto connections = initial.at("connections").To<VariantArray>();
    ASSERT_FALSE(connections.empty());
    for (const auto& value : connections) {
        const auto connection = value.To<ObjectMap>();
        EXPECT_EQ("template-declared", connection.at("origin").To<std::string>());
        EXPECT_FALSE(connection.at("key").To<std::string>().empty());
        ASSERT_GE(connection.at("participants").To<VariantArray>().size(), 2u);
        for (const auto& participantValue : connection.at("participants").To<VariantArray>()) {
            const auto participant = participantValue.To<ObjectMap>();
            const auto key = participant.at("itemKey").To<std::string>();
            ASSERT_TRUE(memberIDs.contains(key));
            EXPECT_EQ(memberIDs.at(key), participant.at("memberEntityId").To<std::string>());
            EXPECT_EQ("not-present", participant.at("manufacturingMappingStatus").To<std::string>());
            EXPECT_FALSE(participant.contains("manufacturingPartEntityId"));
        }
    }

    const auto manufactured = invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{productID}},
    }).at("tubeDesigner").To<ObjectMap>();
    std::map<std::string, std::string> partIDs;
    for (const auto& value : manufactured.at("parts").To<VariantArray>()) {
        const auto part = value.To<ObjectMap>();
        partIDs.emplace(part.at("stableKey").To<std::string>(),
            part.at("entityId").To<std::string>());
    }
    const auto mapped = invoke(scene, "GetProductAssemblyConnections", request);
    for (const auto& value : mapped.at("connections").To<VariantArray>()) {
        for (const auto& participantValue : value.To<ObjectMap>().at("participants").To<VariantArray>()) {
            const auto participant = participantValue.To<ObjectMap>();
            const auto key = participant.at("itemKey").To<std::string>();
            ASSERT_TRUE(partIDs.contains(key));
            EXPECT_EQ("persisted", participant.at("manufacturingMappingStatus").To<std::string>());
            EXPECT_EQ(partIDs.at(key), participant.at("manufacturingPartEntityId").To<std::string>());
        }
    }

    const auto directory = std::filesystem::current_path() / "tmp/product-assembly-connections";
    std::filesystem::create_directories(directory);
    const auto path = directory / "committed-relationships.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1",
        .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;
    info.MainSceneID=scene.GetSceneID();
    info.ProjectName="装配关系持久化";
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto restored = invoke(reopened, "GetProductAssemblyConnections", request);
    EXPECT_EQ(mapped.at("connections"), restored.at("connections"));
    EXPECT_EQ(mapped.at("packageDigest"), restored.at("packageDigest"));

    const auto regenerated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
        {"productEntityId", productID},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto nextRunID = regenerated.at("product").To<ObjectMap>()
        .at("activeGenerationRunId").To<std::string>();
    EXPECT_NE(runID, nextRunID);
    EXPECT_THROW(invoke(scene, "GetProductAssemblyConnections", request), std::exception);
    const auto next = invoke(scene, "GetProductAssemblyConnections", ObjectMap{
        {"productEntityId", productID}, {"generationRunId", nextRunID},
    });
    EXPECT_FALSE(next.at("connections").To<VariantArray>().empty());
    EXPECT_THROW(invoke(scene, "GetProductAssemblyConnections", ObjectMap{
        {"productEntityId", productID}, {"generationRunId", std::string("invalid")},
    }), std::exception);

    const auto participant = next.at("connections").To<VariantArray>().front()
        .To<ObjectMap>().at("participants").To<VariantArray>().front().To<ObjectMap>();
    const auto memberID = iCAX::Data::uuid::from_string(
        participant.at("memberEntityId").To<std::string>());
    ASSERT_TRUE(memberID.has_value());
    auto undo = scene.Database().BeginUndoCommand("Invalidate assembly member test");
    auto& transaction = scene.Database().BeginTransaction("Remove referenced member");
    transaction.DisposeEntity(*memberID);
    std::string error;
    ASSERT_TRUE(scene.Database().CommitTransaction(transaction, error)) << error;
    undo->End();
    EXPECT_THROW(invoke(scene, "GetProductAssemblyConnections", ObjectMap{
        {"productEntityId", productID}, {"generationRunId", nextRunID},
    }), std::exception);
}

TEST(ProductAssemblyConnectionsSDO, BoundProcessKeepsValidatedCommittedConnectionIdentity)
{
    Scene scene;
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
        {"frameType", std::string("closed_frame")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const auto runID = product.at("activeGenerationRunId").To<std::string>();
    const ObjectMap identity{{"productEntityId", productID}, {"generationRunId", runID}};
    const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
    std::map<std::string, ObjectMap> members;
    for (const auto& value : listed.at("members").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        members.emplace(member.at("memberEntityId").To<std::string>(), member);
    }
    std::string connectionKey;
    VariantArray connectionParticipants;
    for (const auto& value : listed.at("connections").To<VariantArray>()) {
        const auto connection = value.To<ObjectMap>();
        if (connection.at("key").To<std::string>() == "frame.corner.0001") {
            connectionKey = connection.at("key").To<std::string>();
            connectionParticipants = connection.at("participants").To<VariantArray>();
            break;
        }
    }
    ASSERT_EQ("frame.corner.0001", connectionKey);
    ASSERT_EQ(2u, connectionParticipants.size());
    const auto firstID = connectionParticipants[0].To<ObjectMap>()
        .at("memberEntityId").To<std::string>();
    const auto secondID = connectionParticipants[1].To<ObjectMap>()
        .at("memberEntityId").To<std::string>();
    ASSERT_TRUE(members.contains(firstID));
    ASSERT_TRUE(members.contains(secondID));
    const auto participant = [](const char* role, const std::string& id) {
        return ObjectMap{{"role", std::string(role)}, {"memberEntityId", id},
            {"anchor", ObjectMap{{"kind", std::string("side")}, {"face", std::string("top")},
                {"reference", std::string("start")}, {"station", 100.0}}}};
    };
    auto preview = identity;
    preview["templateId"] = std::string("mechanical-fastener");
    preview["parameters"] = ObjectMap{{"fastenerType", std::string("bolt")},
        {"nominalDiameter", 4.0}, {"holeClearance", 0.5},
        {"count", 1ull}, {"pitch", 20.0}};
    preview["participants"] = VariantArray{
        participant("fastenedPart", firstID), participant("basePart", secondID)};
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", preview), std::exception);
    preview["connectionKey"] = std::string();
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", preview), std::exception);
    preview["connectionKey"] = std::string("not-a-committed-connection");
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", preview), std::exception);
    std::string unrelatedID;
    for (const auto& [id, member] : members)
        if (id != firstID && id != secondID) { unrelatedID = id; break; }
    ASSERT_FALSE(unrelatedID.empty());
    preview["connectionKey"] = connectionKey;
    preview["participants"] = VariantArray{
        participant("fastenedPart", firstID), participant("basePart", unrelatedID)};
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", preview), std::exception);
    preview["participants"] = VariantArray{
        participant("fastenedPart", firstID), participant("basePart", secondID)};
    const auto checked = invoke(scene, "PreviewProductAssemblyBinding", preview);
    EXPECT_EQ(connectionKey, checked.at("connectionKey").To<std::string>());
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    auto apply = identity;
    apply["candidateToken"] = checked.at("candidateToken");
    ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", apply).at("applied").To<bool>());
    const auto saved = invoke(scene, "GetProductAssemblyBindings", identity)
        .at("bindings").To<VariantArray>();
    ASSERT_EQ(1u, saved.size());
    const auto binding = saved.front().To<ObjectMap>();
    EXPECT_EQ(connectionKey, binding.at("connectionKey").To<std::string>());
    EXPECT_FALSE(binding.at("connectionLabel").To<std::string>().empty());
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", preview), std::exception);
    auto replacement = preview;
    replacement["replaceBindingId"] = binding.at("bindingId");
    const auto replacementCheck = invoke(scene, "PreviewProductAssemblyBinding", replacement);
    EXPECT_TRUE(replacementCheck.at("canApply").To<bool>());
    EXPECT_EQ(connectionKey, replacementCheck.at("connectionKey").To<std::string>());

    const auto directory = std::filesystem::current_path() / "tmp/product-assembly-connection-binding";
    std::filesystem::create_directories(directory);
    const auto path = directory / "committed-connection.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1",
        .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;
    info.MainSceneID=scene.GetSceneID();
    info.ProjectName="装配连接绑定持久化";
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto restored = invoke(reopened, "GetProductAssemblyBindings", identity)
        .at("bindings").To<VariantArray>();
    ASSERT_EQ(1u, restored.size());
    EXPECT_EQ(binding.at("connectionKey"), restored.front().To<ObjectMap>().at("connectionKey"));
    EXPECT_EQ(binding.at("connectionLabel"), restored.front().To<ObjectMap>().at("connectionLabel"));
}

TEST(ProductAssemblyConnectionsSDO, EveryFormalProductDiscoversOnlyCurrentDisplayedMembers)
{
    const std::vector<std::pair<std::string, ObjectMap>> cases{
        {"single-face-security-window", {}},
        {"minimal-protective-grille", {}},
        {"modular-guardrail", {{"sideLength1", 800.0}}},
        {"modular-guardrail-cross-straight", {{"sideLength1", 800.0}}},
        {"modular-guardrail-diamond-straight", {{"sideLength1", 800.0}}},
        {"modular-guardrail-glass-straight", {{"sideLength1", 800.0}}},
        {"straight-steel-staircase", {{"totalRiserCount", 4ull},
            {"floorHeight", 680.0}, {"railingSide", std::string("none")}}},
    };
    for (const auto& [templateID, parameters] : cases) {
        SCOPED_TRACE(templateID);
        Scene scene;
        ObjectMap generation{{"templateId", templateID}};
        generation.insert(parameters.begin(), parameters.end());
        const auto added = invoke(scene, "GeneratePreview", generation)
            .at("tubeDesigner").To<ObjectMap>();
        const auto product = added.at("product").To<ObjectMap>();
        const auto discovered = invoke(scene, "GetProductAssemblyConnections", ObjectMap{
            {"productEntityId", product.at("entityId")},
            {"generationRunId", product.at("activeGenerationRunId")},
        });
        EXPECT_EQ("committed-product-model", discovered.at("source").To<std::string>());
        if (templateID == "single-face-security-window"
            || templateID == "minimal-protective-grille"
            || templateID == "modular-guardrail"
            || templateID == "modular-guardrail-cross-straight"
            || templateID == "modular-guardrail-diamond-straight"
            || templateID == "modular-guardrail-glass-straight"
            || templateID == "straight-steel-staircase")
            EXPECT_FALSE(discovered.at("connections").To<VariantArray>().empty());
        std::set<std::string> memberIDs;
        for (const auto& value : added.at("members").To<VariantArray>())
            memberIDs.insert(value.To<ObjectMap>().at("entityId").To<std::string>());
        for (const auto& value : discovered.at("connections").To<VariantArray>())
            for (const auto& participantValue : value.To<ObjectMap>()
                    .at("participants").To<VariantArray>())
                EXPECT_TRUE(memberIDs.contains(participantValue.To<ObjectMap>()
                    .at("memberEntityId").To<std::string>()));
    }
}
