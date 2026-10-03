// A hidden product fixture supplies two committed, continuous members whose
// actual centerlines meet at an X node. The independent assembly template must
// cut only the host and leave the penetrating tube physically continuous.
TEST(ProductAssemblyBindingSDO, CrossThroughCutsRealProductHostOnly)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID,projectID);
    const ObjectMap input{{"hostLength",400.0},{"throughLength",360.0}};
    const auto nativePreview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("assembly-cross-fixture")},
        {"parameters",input},
    });
    const auto normalized=nativePreview.at("parameters").To<ObjectMap>();
    ASSERT_EQ(2u,nativePreview.at("items").To<VariantArray>().size());
    EXPECT_DOUBLE_EQ(400.0,assemblyBindingNumber(normalized.at("hostLength")));
    EXPECT_DOUBLE_EQ(360.0,assemblyBindingNumber(normalized.at("throughLength")));
    const auto generated=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("assembly-cross-fixture")},
        {"hostLength",400.0},{"throughLength",360.0},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    const auto productID=product.at("entityId").To<std::string>();
    EXPECT_EQ(normalized,assemblyBindingProduct(scene,productID)->GetParameters());
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto baseline=assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{product.at("entityId")}}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(2u,baseline.size());
    const auto hostBefore=assemblyBindingRowShape(scene,baseline.at("cross.host.0001"));
    const auto throughBefore=assemblyBindingRowShape(scene,baseline.at("cross.through.0001"));
    const auto listed=invoke(scene,"GetProductAssemblyBindings",identity);
    ObjectMap connection;
    for(const auto& value:listed.at("connections").To<VariantArray>()){
        const auto candidate=value.To<ObjectMap>();
        if(candidate.at("key").To<std::string>()=="cross.center") connection=candidate;
    }
    ASSERT_FALSE(connection.empty());
    EXPECT_EQ("verified",connection.at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>());
    const auto properties=connection.at("properties").To<ObjectMap>();
    ASSERT_TRUE(properties.contains("participantAnchors"));
    const auto declared=properties.at("participantAnchors").To<VariantArray>();
    std::map<std::string,ObjectMap> anchors;
    for(const auto& value:declared){
        const auto row=value.To<ObjectMap>();
        ASSERT_TRUE(row.contains("itemKey"));
        ASSERT_TRUE(row.contains("anchor"));
        anchors.emplace(row.at("itemKey").To<std::string>(),
            row.at("anchor").To<ObjectMap>());
    }
    VariantArray participants;
    for(const auto& value:connection.at("participants").To<VariantArray>()){
        const auto member=value.To<ObjectMap>();
        const auto key=member.at("itemKey").To<std::string>();
        participants.emplace_back(ObjectMap{
            {"role",std::string(key=="cross.host.0001" ? "host" : "through")},
            {"memberEntityId",member.at("memberEntityId")},
            {"itemKey",key},{"anchor",anchors.at(key)},
        });
    }
    ASSERT_EQ(2u,participants.size());
    auto request=identity;
    request["sourceRevision"]=listed.at("sourceRevision");
    request["revision"]=listed.at("revision");
    request["connectionKey"]=std::string("cross.center");
    request["templateId"]=std::string("cross-through");
    request["participants"]=participants;
    request["parameters"]=ObjectMap{{"fitGap",0.2}};

    auto wrongAnchor=request;
    auto invalidParticipants=participants;
    for(auto& value:invalidParticipants){
        auto row=value.To<ObjectMap>();
        if(row.at("role").To<std::string>()!="through") continue;
        auto anchor=row.at("anchor").To<ObjectMap>();
        anchor["station"]=190.0;
        row["anchor"]=std::move(anchor);
        value=std::move(row);
    }
    wrongAnchor["participants"]=std::move(invalidParticipants);
    EXPECT_THROW(invoke(scene,"PreviewProductAssemblyBinding",wrongAnchor),std::exception);

    const auto checked=invoke(scene,"PreviewProductAssemblyBinding",request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    const auto measurement=checked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured",measurement.at("status").To<std::string>());
    EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")),1e-4);
    const auto changes=checked.at("geometryChanges").To<VariantArray>();
    ASSERT_EQ(1u,changes.size());
    const auto change=changes[0].To<ObjectMap>();
    EXPECT_EQ("cross.host.0001",change.at("itemKey").To<std::string>());
    EXPECT_GT(assemblyBindingNumber(change.at("volumeBefore")),
        assemblyBindingNumber(change.at("volumeAfter")));

    auto apply=identity;
    apply["candidateToken"]=checked.at("candidateToken");
    const auto applied=invoke(scene,"ApplyProductAssemblyBinding",apply);
    ASSERT_TRUE(applied.at("applied").To<bool>());
    const auto rows=assemblyBindingPartRows(applied.at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(2u,rows.size());
    assemblyBindingCheckSubset(hostBefore,
        assemblyBindingRowShape(scene,rows.at("cross.host.0001")));
    checkSameShape(throughBefore,
        assemblyBindingRowShape(scene,rows.at("cross.through.0001")));
    const auto plan=invoke(scene,"GetProductAssemblyBindings",identity)
        .at("manufacturingPlan").To<ObjectMap>();
    const auto nodeAudit=plan.at("nodeFinalAudit").To<ObjectMap>();
    ASSERT_EQ("measured",nodeAudit.at("status").To<std::string>())
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(nodeAudit);
    ASSERT_EQ(1u,nodeAudit.at("nodes").To<VariantArray>().size());
    const auto finalNode=nodeAudit.at("nodes").To<VariantArray>()[0].To<ObjectMap>();
    EXPECT_LE(assemblyBindingNumber(finalNode.at("intersectionVolumeMm3")),1e-4);
}
