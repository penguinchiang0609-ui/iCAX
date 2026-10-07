TEST(ProductDisassemblyCountSDO, CachedExecutionIsRevalidatedEachRequestAndFailuresKeepCommittedParts)
{
    Scene scene;
    const auto initial=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"width",400.0},{"height",400.0},{"accessDoorEnabled",false},
        {"frameManufacturingMode",std::string("segment_weld")},
        {"frameJoinType",std::string("miter_45")}
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product=initial.at("product").To<ObjectMap>();
    const auto run=disassemblyCountRun(scene,product);
    const auto baseline=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product))
        .at("tubeDesigner").To<ObjectMap>();
    const auto current=baseline.at("product").To<ObjectMap>();
    const auto rows=disassemblyCountRows(baseline);
    const auto identities=disassemblyCountIdentities(baseline);
    const auto count=run->GetPartCount();
    const auto valid=run->GetManufacturingModel();
    const auto frozen=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetAssemblyManufacturingModel());
    const auto display=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel());
    const auto same=current.at("parameters").To<ObjectMap>();
    auto changed=same;changed["assemblyClearance"]=0.2;
    std::vector<ObjectMap> poisoned;
    auto graph=valid;
    auto& resources=std::get<VariantArray>(graph.at("resources").m_Value);
    ASSERT_FALSE(resources.empty());
    std::get<ObjectMap>(resources.front().m_Value)["inputs"]=VariantArray{std::string("missing-owned-resource")};
    poisoned.push_back(std::move(graph));
    auto identity=valid;
    std::get<ObjectMap>(identity.at("template").m_Value)["packageDigest"]=std::string("changed-owned-package");
    poisoned.push_back(std::move(identity));
    auto parameters=valid;
    std::get<ObjectMap>(parameters.at("parameters").m_Value)["assemblyClearance"]=0.9;
    poisoned.push_back(std::move(parameters));
    for(std::size_t index=0;index<poisoned.size();++index) {
        SCOPED_TRACE(index);
        ASSERT_TRUE(run->SetManufacturingModel(poisoned[index]));
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel());
        // A previous successful parse must never turn into a cross-request
        // cache or allow a same/changed draft to hide damaged committed data.
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current)),std::exception);
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&same)),std::exception);
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&changed)),std::exception);
        EXPECT_EQ(disassemblyCountRows(invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>()),rows);
        EXPECT_EQ(run->GetPartCount(),count);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel()),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),display);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetAssemblyManufacturingModel()),frozen);
    }
    auto mapping=valid;
    auto& first=std::get<ObjectMap>(std::get<VariantArray>(mapping.at("items").m_Value).front().m_Value);
    ASSERT_EQ(std::get<ObjectMap>(first.at("properties").m_Value).erase("manufacturing.sourceMembers"),1u);
    ASSERT_TRUE(run->SetManufacturingModel(mapping));
    EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current)),std::exception);
    EXPECT_EQ(disassemblyCountRows(invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>()),rows);
    ASSERT_TRUE(run->SetManufacturingModel(valid));
    const auto repeated=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountIdentities(repeated),identities);
    disassemblyCountChecksParts(scene,repeated,*run);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),display);
    EXPECT_EQ(run->GetManufacturingParameters(),same);

    // The freshly validated model is moved into preparation rather than parsed
    // from the execution document again. Both owning forms must receive the
    // validated manufacturing-only metadata, including its removal on revert.
    const auto updated=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&changed))
        .at("tubeDesigner").To<ObjectMap>();
    const std::string metadataKey="tubeDesigner.validatedManufacturingOnlyParameterKeys";
    const auto& updatedExtensions=std::get<ObjectMap>(run->GetManufacturingModel().at("extensions").m_Value);
    ASSERT_TRUE(updatedExtensions.contains(metadataKey));
    const auto& keys=std::get<VariantArray>(updatedExtensions.at(metadataKey).m_Value);
    EXPECT_NE(std::find(keys.begin(),keys.end(),Variant(std::string("assemblyClearance"))),keys.end());
    const auto parsed=iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(run->GetManufacturingModel());
    EXPECT_EQ(parsed.Extensions.at(metadataKey),updatedExtensions.at(metadataKey));
    EXPECT_EQ(parsed.Parameters,run->GetManufacturingParameters());
    const auto updatedCurrent=updated.at("product").To<ObjectMap>();
    EXPECT_NO_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(updatedCurrent)));
    const auto reverted=invoke(scene,"DisassembleSelected",disassemblyCountRequest(updatedCurrent,&same))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountIdentities(reverted),identities);
    EXPECT_FALSE(std::get<ObjectMap>(run->GetManufacturingModel().at("extensions").m_Value).contains(metadataKey));
    EXPECT_FALSE(iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(run->GetManufacturingModel())
        .Extensions.contains(metadataKey));
    EXPECT_EQ(run->GetManufacturingParameters(),same);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),display);
}
