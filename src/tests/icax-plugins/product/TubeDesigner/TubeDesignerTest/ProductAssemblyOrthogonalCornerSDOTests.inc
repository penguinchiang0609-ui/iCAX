// Real three-member source fixture and public product/assembly/manufacturing
// APIs. The template owns the operation combination; the host must retain one
// physical blank per member and check every final member pair.
namespace {
ObjectMap cornerBindingRequest(const ObjectMap& identity,const ObjectMap& listed,
    const std::string& abJoint,const std::string& cJoint)
{
    ObjectMap connection;
    for (const auto& value : listed.at("connections").To<VariantArray>()) {
        const auto candidate = value.To<ObjectMap>();
        if (candidate.at("key").To<std::string>() == "corner.center") connection = candidate;
    }
    if (connection.empty()) throw std::runtime_error("Committed three-way corner is missing");
    std::map<std::string,ObjectMap> declarations;
    for (const auto& value : connection.at("properties").To<ObjectMap>()
            .at("participantAnchors").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        declarations.emplace(row.at("itemKey").To<std::string>(),row.at("anchor").To<ObjectMap>());
    }
    VariantArray participants;
    for (const auto& value : connection.at("participants").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        const auto key = member.at("itemKey").To<std::string>();
        const std::string role = key == "corner.a.0001" ? "memberA"
            : key == "corner.b.0001" ? "memberB" : "memberC";
        participants.emplace_back(ObjectMap{{"role",role},{"memberEntityId",member.at("memberEntityId")},
            {"itemKey",key},{"anchor",declarations.at(key)}});
    }
    auto request = identity;
    request["sourceRevision"] = listed.at("sourceRevision");
    request["revision"] = listed.at("revision");
    request["connectionKey"] = std::string("corner.center");
    request["templateId"] = std::string("orthogonal-corner");
    request["participants"] = std::move(participants);
    request["parameters"] = ObjectMap{{"abJoint",abJoint},{"cJoint",cJoint}};
    return request;
}

void cornerExpectThreePairs(const ObjectMap& node)
{
    ASSERT_EQ(node.at("status").To<std::string>(),"measured")
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(node);
    ASSERT_EQ(node.at("checkedPairCount").To<std::uint64_t>(),3u);
    const auto pairs = node.at("pairs").To<VariantArray>();
    ASSERT_EQ(pairs.size(),3u);
    std::set<std::pair<std::string,std::string>> unique;
    for (const auto& value : pairs) {
        const auto pair = value.To<ObjectMap>();
        unique.emplace(pair.at("firstItemKey").To<std::string>(),pair.at("secondItemKey").To<std::string>());
        EXPECT_EQ(pair.at("status").To<std::string>(),"measured");
        EXPECT_LE(assemblyBindingNumber(pair.at("intersectionVolumeMm3")),
            assemblyBindingNumber(pair.at("penetrationToleranceMm3")));
    }
    EXPECT_EQ(unique.size(),3u);
    ASSERT_GE(node.at("contacts").To<VariantArray>().size(),2u);
    for (const auto& value : node.at("contacts").To<VariantArray>()) {
        const auto contact = value.To<ObjectMap>();
        EXPECT_EQ(contact.at("status").To<std::string>(),"measured");
        EXPECT_LE(assemblyBindingNumber(contact.at("nearestDistanceMm")),
            assemblyBindingNumber(contact.at("maximumSeparationMm"))+1e-6);
        EXPECT_FALSE(contact.at("interfaceClearanceCertified").To<bool>());
    }
    EXPECT_FALSE(node.at("interfaceFitCertified").To<bool>());
}

double cornerVolume(const TopoDS_Shape& shape)
{
    EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    unsigned int count = 0;
    for (TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next()) ++count;
    EXPECT_EQ(count,1u);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    EXPECT_GT(properties.Mass(),1.0);
    return properties.Mass();
}

void cornerNativeBlanksAtAssemblyPose(Scene& scene,const ObjectMap& plan,const std::string& prefix)
{
    ASSERT_EQ(plan.at("manufacturingParts").To<VariantArray>().size(),3u);
    std::vector<TopoDS_Shape> placed;
    for (const auto& value : plan.at("manufacturingParts").To<VariantArray>()) {
        const auto part = value.To<ObjectMap>();
        const auto shape = finishedProductNativeShape(scene,finishedProductOptimizedNativePreview(scene,
            part.at("request").To<ObjectMap>(),prefix+"-"+part.at("id").To<std::string>()));
        const auto matrix = part.at("matrix").To<VariantArray>();
        gp_Trsf pose;
        pose.SetValues(finishedProductNumber(matrix[0]),finishedProductNumber(matrix[1]),finishedProductNumber(matrix[2]),finishedProductNumber(matrix[3]),
            finishedProductNumber(matrix[4]),finishedProductNumber(matrix[5]),finishedProductNumber(matrix[6]),finishedProductNumber(matrix[7]),
            finishedProductNumber(matrix[8]),finishedProductNumber(matrix[9]),finishedProductNumber(matrix[10]),finishedProductNumber(matrix[11]));
        placed.push_back(BRepBuilderAPI_Transform(shape,pose,true).Shape());
    }
    for (std::size_t a=0;a<placed.size();++a) for (std::size_t b=a+1;b<placed.size();++b) {
        BRepAlgoAPI_Common common(placed[a],placed[b]);
        ASSERT_TRUE(common.IsDone());
        GProp_GProps intersection; BRepGProp::VolumeProperties(common.Shape(),intersection);
        EXPECT_NEAR(intersection.Mass(),0.0,1e-4) << "pair " << a << "/" << b;
    }
}
}

TEST(ProductAssemblyOrthogonalCornerSDO, SixCombinationsCutThreeRealMembersAndReopen)
{
    for (const std::string ab : {"miter","wrap"}) for (const std::string c : {"weld","insert","tabs"}) {
        SCOPED_TRACE(ab+"/"+c);
        const auto projectID = iCAX::Data::GenerateNewUUID();
        Scene scene(projectID,projectID);
        const auto preview = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("assembly-orthogonal-corner")},{"parameters",ObjectMap{}}});
        ASSERT_EQ(preview.at("items").To<VariantArray>().size(),3u);
        const auto input = preview.at("parameters").To<ObjectMap>();
        auto generate = input;
        generate["templateId"] = std::string("assembly-orthogonal-corner");
        const auto generated = invoke(scene,"GeneratePreview",generate).at("tubeDesigner").To<ObjectMap>();
        const auto product = generated.at("product").To<ObjectMap>();
        EXPECT_EQ(input,assemblyBindingProduct(scene,product.at("entityId").To<std::string>())->GetParameters());
        const ObjectMap identity{{"productEntityId",product.at("entityId")},
            {"generationRunId",product.at("activeGenerationRunId")}};
        const auto initial = assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
            {"productEntityIds",VariantArray{product.at("entityId")}}}).at("tubeDesigner").To<ObjectMap>());
        ASSERT_EQ(initial.size(),3u);
        const auto listed = invoke(scene,"GetProductAssemblyBindings",identity);
        const auto connection = listed.at("connections").To<VariantArray>().front().To<ObjectMap>();
        EXPECT_EQ(connection.at("nodeGeometry").To<ObjectMap>().at("nodeGeometryStatus").To<std::string>(),"verified");
        const auto request = cornerBindingRequest(identity,listed,ab,c);
        const auto checked = invoke(scene,"PreviewProductAssemblyBinding",request);
        ASSERT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        cornerExpectThreePairs(checked.at("interfaceMeasurement").To<ObjectMap>());
        for (const auto& [key,value] : request.at("parameters").To<ObjectMap>())
            EXPECT_EQ(checked.at("parameters").To<ObjectMap>().at(key),value);
        auto apply = identity;
        apply["candidateToken"] = checked.at("candidateToken");
        ASSERT_TRUE(invoke(scene,"ApplyProductAssemblyBinding",apply).at("applied").To<bool>());
        const auto after = invoke(scene,"GetProductAssemblyBindings",identity);
        ASSERT_EQ(after.at("bindings").To<VariantArray>().size(),1u);
        const auto plan = after.at("manufacturingPlan").To<ObjectMap>();
        EXPECT_EQ(plan.at("readiness").To<std::string>(),"not-ready");
        EXPECT_EQ(plan.at("fitStatus").To<std::string>(),"not-verified");
        const auto audit = plan.at("nodeFinalAudit").To<ObjectMap>();
        ASSERT_EQ(audit.at("nodes").To<VariantArray>().size(),1u);
        cornerExpectThreePairs(audit.at("nodes").To<VariantArray>().front().To<ObjectMap>());
        const auto rows = assemblyBindingPartRows(invoke(scene,"List",ObjectMap{
            {"includeManufacturingGeometry",true}}).at("tubeDesigner").To<ObjectMap>());
        ASSERT_EQ(rows.size(),3u);
        std::map<std::string,TopoDS_Shape> processed;
        for (const auto& [key,row] : rows) {
            const auto shape = assemblyBindingRowShape(scene,row);
            processed.emplace(key,shape);
            EXPECT_LE(cornerVolume(shape),cornerVolume(assemblyBindingRowShape(scene,initial.at(key)))+1e-4);
        }
        const auto directory = std::filesystem::current_path()/"tmp/orthogonal-corner-native";
        std::filesystem::create_directories(directory);
        const auto path = directory/(ab+"-"+c+".ictd");
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
            .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;
        info.ProjectID = scene.project; info.MainSceneID = scene.GetSceneID(); info.ProjectName = "三向直角节点";
        file.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.project,scene.GetSceneID(),false,true);
        file.Open(path,reopened.Database(),reopened.Resources());
        const auto restored = invoke(reopened,"GetProductAssemblyBindings",identity);
        EXPECT_EQ(restored.at("bindings"),after.at("bindings"));
        cornerExpectThreePairs(restored.at("manufacturingPlan").To<ObjectMap>()
            .at("nodeFinalAudit").To<ObjectMap>().at("nodes").To<VariantArray>().front().To<ObjectMap>());
        const auto restoredRows = assemblyBindingPartRows(invoke(reopened,"List",ObjectMap{
            {"includeManufacturingGeometry",true}}).at("tubeDesigner").To<ObjectMap>());
        ASSERT_EQ(restoredRows.size(),3u);
        for (const auto& [key,shape] : processed)
            finishedProductSameNativeShape(assemblyBindingRowShape(reopened,restoredRows.at(key)),shape);
        // A three-member connection cannot be reinterpreted as one pair, nor
        // can a second binding be added under its already-applied identity.
        EXPECT_THROW(invoke(reopened,"PreviewProductAssemblyBinding",request),std::exception);
    }
}

TEST(ProductAssemblyOrthogonalCornerSDO, MissingDuplicateAndInconsistentAnchorsAreRejected)
{
    Scene scene;
    const auto generated = invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("assembly-orthogonal-corner")}}).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto listed = invoke(scene,"GetProductAssemblyBindings",identity);
    const auto valid = cornerBindingRequest(identity,listed,"miter","weld");
    auto request = valid;
    auto participants = valid.at("participants").To<VariantArray>();
    participants.pop_back(); request["participants"] = participants;
    EXPECT_THROW(invoke(scene,"PreviewProductAssemblyBinding",request),std::exception);
    participants = valid.at("participants").To<VariantArray>();
    participants[2] = participants[1]; request["participants"] = participants;
    EXPECT_THROW(invoke(scene,"PreviewProductAssemblyBinding",request),std::exception);
    participants = valid.at("participants").To<VariantArray>();
    auto wrong = participants[2].To<ObjectMap>();
    auto anchor = wrong.at("anchor").To<ObjectMap>(); anchor["end"] = std::string("end");
    wrong["anchor"] = anchor; participants[2] = wrong; request["participants"] = participants;
    EXPECT_THROW(invoke(scene,"PreviewProductAssemblyBinding",request),std::exception);
    EXPECT_TRUE(invoke(scene,"GetProductAssemblyBindings",identity).at("bindings").To<VariantArray>().empty());
}

TEST(AssemblyTemplateSDOTest, OrthogonalCornerExamplesUseThreeStrictNativeBlanks)
{
    Scene scene;
    for (const std::string ab : {"miter","wrap"}) for (const std::string c : {"weld","insert","tabs"}) {
        SCOPED_TRACE(ab+"/"+c);
        const ObjectMap parameters{{"abJoint",ab},{"cJoint",c}};
        const auto example = invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"parameters",parameters}});
        const auto product = example.at("finishedProduct").To<ObjectMap>();
        const auto before = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto applicability = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(applicability.at("applicable").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(applicability);
        const auto plan = invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},
            {"parameters",parameters},{"manufacturingOnly",true}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        cornerNativeBlanksAtAssemblyPose(scene,plan,"corner-"+ab+"-"+c);
    }
}

TEST(AssemblyTemplateSDOTest, OrthogonalCornerExtremeTabExamplesBuildThreeNativeSolidsWithoutInterference)
{
    Scene scene;
    for (const std::string ab : {"miter","wrap"}) {
        SCOPED_TRACE(ab);
        const ObjectMap parameters{{"abJoint",ab},{"cJoint",std::string("tabs")},
            {"tabWidth",30.0},{"tabLength",60.0},{"sideClearance",3.0}};
        const auto product = invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"parameters",parameters}})
            .at("finishedProduct").To<ObjectMap>();
        const auto check = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>()) << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto plan = invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},
            {"parameters",parameters},{"manufacturingOnly",true}});
        cornerNativeBlanksAtAssemblyPose(scene,plan,"corner-extreme-"+ab);
    }
}

TEST(ProductAssemblyOrthogonalCornerSDO, NonorthogonalCommittedAxesAndThreeMemberLegacyTopologyFailClosed)
{
    Scene scene;
    const auto generated = invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("assembly-orthogonal-corner")}}).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto run = disassemblyCountRun(scene,product);
    const auto original = run->GetNeutralModel();
    auto altered = original;
    auto items = altered.at("items").To<VariantArray>();
    for (auto& value : items) {
        auto item = value.To<ObjectMap>();
        if (item.at("key").To<std::string>() != "corner.c.0001") continue;
        auto properties = item.at("properties").To<ObjectMap>();
        auto frame = properties.at("assemblyFrame.member").To<ObjectMap>();
        const double length = assemblyBindingNumber(frame.at("axisLength"));
        frame["end"] = VariantArray{length*0.1,length*std::sqrt(0.99),0.0};
        properties["assemblyFrame.member"] = std::move(frame);
        item["properties"] = std::move(properties); value = std::move(item);
    }
    altered["items"] = std::move(items);
    ASSERT_TRUE(run->SetNeutralModel(altered));
    auto connections = invoke(scene,"GetProductAssemblyConnections",identity).at("connections").To<VariantArray>();
    ASSERT_EQ(connections.size(),1u);
    EXPECT_EQ(connections.front().To<ObjectMap>().at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>(),"invalid");
    altered = original;
    auto relationships = altered.at("relationships").To<VariantArray>();
    auto relationship = relationships.front().To<ObjectMap>();
    auto properties = relationship.at("properties").To<ObjectMap>();
    properties["topology"] = std::string("L"); relationship["properties"] = properties;
    relationships.front() = relationship; altered["relationships"] = relationships;
    ASSERT_TRUE(run->SetNeutralModel(altered));
    connections = invoke(scene,"GetProductAssemblyConnections",identity).at("connections").To<VariantArray>();
    EXPECT_EQ(connections.front().To<ObjectMap>().at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>(),"invalid");
    ASSERT_TRUE(run->SetNeutralModel(original));
    connections = invoke(scene,"GetProductAssemblyConnections",identity).at("connections").To<VariantArray>();
    EXPECT_EQ(connections.front().To<ObjectMap>().at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>(),"verified");
}

TEST(AssemblyTemplateSDOTest, OrthogonalCornerRejectsImpossibleEntrancesAndKeepsInactiveDrafts)
{
    Scene scene;
    const auto product = invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
        {"templateId",std::string("orthogonal-corner")}}).at("finishedProduct").To<ObjectMap>();
    for (const auto& parameters : std::vector<ObjectMap>{
            {{"cJoint",std::string("insert")},{"insertDepth",100.0}},
            {{"cJoint",std::string("tabs")},{"tabWidth",15.0}},
            {{"abJoint",std::string("miter")},{"cJoint",std::string("tabs")},
                {"tabWidth",12.0},{"sideClearance",1.0}}}) {
        const auto check = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_FALSE(check.at("applicable").To<bool>()) << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        EXPECT_FALSE(check.at("reason").To<std::string>().empty());
    }
    auto oversized = product;
    auto spans = oversized.at("spans").To<ObjectMap>();
    auto c = spans.at("armC").To<ObjectMap>();
    auto dimensions = c.at("parameters").To<ObjectMap>();
    dimensions["width"] = 40.0; dimensions["depth"] = 40.0; c["parameters"] = dimensions;
    spans["armC"] = c; oversized["spans"] = spans;
    const auto rejected = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"finishedProduct",oversized},
        {"parameters",ObjectMap{{"cJoint",std::string("insert")}}}});
    EXPECT_FALSE(rejected.at("applicable").To<bool>());
    EXPECT_FALSE(rejected.at("reason").To<std::string>().empty());
    const ObjectMap draft{{"abJoint",std::string("miter")},{"cJoint",std::string("weld")},
        {"insertDepth",100.0},{"tabWidth",15.0},{"sideClearance",1.0}};
    const auto allowed = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},{"parameters",draft}});
    ASSERT_TRUE(allowed.at("applicable").To<bool>()) << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(allowed);
    const auto plan = invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"finishedProduct",product},
        {"parameters",draft},{"manufacturingOnly",true}});
    for (const auto& [key,value] : draft) EXPECT_EQ(plan.at("parameters").To<ObjectMap>().at(key),value);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product));
    for (const auto& part : plan.at("manufacturingParts").To<VariantArray>())
        finishedProductNativeShape(scene,finishedProductOptimizedNativePreview(scene,
            part.To<ObjectMap>().at("request").To<ObjectMap>(),"corner-inactive-"+part.To<ObjectMap>().at("id").To<std::string>()));
}

TEST(ProductAssemblyOrthogonalCornerSDO, RejectedEntranceDoesNotModifyActualPartsOrBindings)
{
    Scene scene;
    const auto generated = invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("assembly-orthogonal-corner")}}).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto before = assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{product.at("entityId")}}}).at("tubeDesigner").To<ObjectMap>());
    const auto listed = invoke(scene,"GetProductAssemblyBindings",identity);
    for (const auto& parameters : std::vector<ObjectMap>{
            {{"abJoint",std::string("miter")},{"cJoint",std::string("insert")},{"insertDepth",100.0}},
            {{"abJoint",std::string("miter")},{"cJoint",std::string("tabs")},{"tabWidth",15.0}}}) {
        auto request = cornerBindingRequest(identity,listed,"miter","weld");
        request["parameters"] = parameters;
        bool failed = false;
        try {
            const auto checked = invoke(scene,"PreviewProductAssemblyBinding",request);
            failed = !checked.at("canApply").To<bool>();
            EXPECT_FALSE(checked.at("detail").To<std::string>().empty());
        } catch (const std::exception& error) {
            failed = true;
            EXPECT_FALSE(std::string(error.what()).empty());
        }
        EXPECT_TRUE(failed);
        EXPECT_TRUE(invoke(scene,"GetProductAssemblyBindings",identity).at("bindings").To<VariantArray>().empty());
        const auto after = assemblyBindingPartRows(invoke(scene,"List",ObjectMap{
            {"includeManufacturingGeometry",true}}).at("tubeDesigner").To<ObjectMap>());
        ASSERT_EQ(after.size(),3u);
        for (const auto& [key,row] : before)
            finishedProductSameNativeShape(assemblyBindingRowShape(scene,after.at(key)),assemblyBindingRowShape(scene,row));
    }
}

TEST(ProductAssemblyOrthogonalCornerSDO, RectangularBranchTabsUseActualManufacturingQuarterTurn)
{
    Scene scene;
    const auto input = invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("assembly-orthogonal-corner")},
        {"parameters",ObjectMap{{"cWidth",24.0},{"cDepth",20.0}}}}).at("parameters").To<ObjectMap>();
    auto generate = input;
    generate["templateId"] = std::string("assembly-orthogonal-corner");
    const auto product = invoke(scene,"GeneratePreview",generate).at("tubeDesigner").To<ObjectMap>()
        .at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")}};
    const auto raw = assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{product.at("entityId")}}}).at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(raw.size(),3u);
    const auto listed = invoke(scene,"GetProductAssemblyBindings",identity);
    const auto candidate = invoke(scene,"PreviewProductAssemblyBinding",
        cornerBindingRequest(identity,listed,"miter","tabs"));
    ASSERT_TRUE(candidate.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(candidate);
    cornerExpectThreePairs(candidate.at("interfaceMeasurement").To<ObjectMap>());
    auto apply = identity;
    apply["candidateToken"] = candidate.at("candidateToken");
    ASSERT_TRUE(invoke(scene,"ApplyProductAssemblyBinding",apply).at("applied").To<bool>());
    const auto final = invoke(scene,"GetProductAssemblyBindings",identity);
    cornerExpectThreePairs(final.at("manufacturingPlan").To<ObjectMap>().at("nodeFinalAudit").To<ObjectMap>()
        .at("nodes").To<VariantArray>().front().To<ObjectMap>());
    const auto rows = assemblyBindingPartRows(invoke(scene,"List",ObjectMap{
        {"includeManufacturingGeometry",true}}).at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(rows.size(),3u);
    for (const auto& [key,row] : rows)
        EXPECT_LE(cornerVolume(assemblyBindingRowShape(scene,row)),
            cornerVolume(assemblyBindingRowShape(scene,raw.at(key)))+1e-4);

    const ObjectMap parameters{{"abJoint",std::string("miter")},{"cJoint",std::string("tabs")}};
    auto example = invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"parameters",parameters}})
        .at("finishedProduct").To<ObjectMap>();
    auto spans = example.at("spans").To<ObjectMap>();
    auto branch = spans.at("armC").To<ObjectMap>();
    auto dimensions = branch.at("parameters").To<ObjectMap>();
    dimensions["width"] = 24.0; dimensions["depth"] = 20.0;
    branch["parameters"] = dimensions; spans["armC"] = branch; example["spans"] = spans;
    const auto check = invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"parameters",parameters},{"finishedProduct",example}});
    ASSERT_TRUE(check.at("applicable").To<bool>()) << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
    const auto plan = invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("orthogonal-corner")},{"parameters",parameters},
        {"finishedProduct",example},{"manufacturingOnly",true}});
    cornerNativeBlanksAtAssemblyPose(scene,plan,"corner-rectangular-branch");
}
