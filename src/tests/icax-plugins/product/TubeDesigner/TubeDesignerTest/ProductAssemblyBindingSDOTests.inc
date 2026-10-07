// Included inside punch_persistence_acceptance after the Scene/invoke helpers.
void assemblyBindingCheckSubset(const TopoDS_Shape& original,
    const TopoDS_Shape& processed);

double assemblyBindingNumber(const iCAX::Data::Variant& value)
{
    if (value.Is<double>()) return value.To<double>();
    if (value.Is<std::int64_t>()) return static_cast<double>(value.To<std::int64_t>());
    if (value.Is<std::uint64_t>()) return static_cast<double>(value.To<std::uint64_t>());
    if (value.Is<int>()) return static_cast<double>(value.To<int>());
    throw std::runtime_error("Assembly test numeric value is invalid");
}

std::shared_ptr<iCAX::TubeDesigner::CProductInstanceComponent> assemblyBindingProduct(
    Scene& scene, const std::string& productID)
{
    const auto id = iCAX::Data::uuid::from_string(productID);
    if (!id) throw std::runtime_error("Assembly binding test product ID is invalid");
    const auto entity = scene.Database().GetEntity(*id);
    if (!entity) throw std::runtime_error("Assembly binding test product is missing");
    const auto product = std::dynamic_pointer_cast<iCAX::TubeDesigner::CProductInstanceComponent>(
        entity->GetComponent(iCAX::TubeDesigner::CProductInstanceComponent::S_ClassName));
    if (!product) throw std::runtime_error("Assembly binding test product component is missing");
    return product;
}

std::map<std::string, ObjectMap> assemblyBindingPartRows(const ObjectMap& snapshot)
{
    std::map<std::string, ObjectMap> result;
    for (const auto& value : snapshot.at("parts").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (!result.emplace(row.at("stableKey").To<std::string>(), row).second)
            throw std::runtime_error("Duplicate manufacturing part stable key");
    }
    return result;
}

ObjectMap assemblyBindingPlanMember(const ObjectMap& plan, const std::string& itemKey)
{
    for (const auto& value : plan.at("members").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (row.at("itemKey").To<std::string>() == itemKey) return row;
    }
    throw std::runtime_error("Assembly manufacturing plan member is missing: " + itemKey);
}

TopoDS_Shape assemblyBindingRowShape(Scene& scene, const ObjectMap& row)
{
    const auto resource = scene.Resources().Get<BRepModel>(
        row.at("manufacturingGeometryResourceId").To<std::string>(),
        row.at("manufacturingGeometryResourceVersion").To<std::uint64_t>());
    if (!resource) throw std::runtime_error("Assembly binding final part resource is missing");
    const auto built = iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
    if (!built.bOK || built.Shape.IsNull())
        throw std::runtime_error("Assembly binding final part resource cannot rebuild");
    return built.Shape;
}

std::array<double, 6> assemblyBindingBounds(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return {x0, y0, z0, x1, y1, z1};
}

std::filesystem::path assemblyBindingUtf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

TopoDS_Shape assemblyBindingReadStep(const std::string& path)
{
    if (!std::filesystem::is_regular_file(assemblyBindingUtf8Path(path)))
        throw std::runtime_error("Assembly binding exported STEP file is missing");
    STEPControl_Reader reader;
    if (reader.ReadFile(path.c_str()) != IFSelect_RetDone
        || reader.TransferRoots() <= 0 || reader.OneShape().IsNull())
        throw std::runtime_error("Assembly binding manufacturing STEP cannot be read back");
    return reader.OneShape();
}

VariantArray assemblyBindingParticipants(const ObjectMap& listed)
{
    VariantArray connected, declared;
    for (const auto& value : listed.at("connections").To<VariantArray>()) {
        const auto connection = value.To<ObjectMap>();
        if (connection.at("key").To<std::string>() == "frame.corner.bottom-left") {
            connected = connection.at("participants").To<VariantArray>();
            declared = connection.at("properties").To<ObjectMap>()
                .at("participantAnchors").To<VariantArray>();
            break;
        }
    }
    if (connected.size() != 2 || declared.size() != 2)
        throw std::runtime_error("Product must expose the committed frame corner connection");
    VariantArray result;
    for (std::size_t index = 0; index < connected.size(); ++index) {
        const auto member = connected[index].To<ObjectMap>();
        const auto key = member.at("itemKey").To<std::string>();
        ObjectMap anchor;
        for (const auto& value : declared) {
            const auto row = value.To<ObjectMap>();
            if (row.at("itemKey").To<std::string>() == key)
                anchor = row.at("anchor").To<ObjectMap>();
        }
        if (anchor.empty()) throw std::runtime_error("Product corner lacks an end anchor");
        result.emplace_back(ObjectMap{
            {"role", std::string(index == 0 ? "memberA" : "memberB")},
            {"memberEntityId", member.at("memberEntityId")}, {"itemKey", member.at("itemKey")},
            {"anchor", std::move(anchor)},
        });
    }
    return result;
}

ObjectMap assemblyBindingPreviewRequest(const ObjectMap& identity, const ObjectMap& listed)
{
    auto request = identity;
    request["sourceRevision"] = listed.at("sourceRevision");
    request["revision"] = listed.at("revision");
    request["templateId"] = std::string("two-end-end-angle");
    request["parameters"] = ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0}, {"planeRotation", 0.0}};
    request["participants"] = assemblyBindingParticipants(listed);
    request["connectionKey"] = std::string("frame.corner.bottom-left");
    return request;
}

ObjectMap assemblyBindingTeePreviewRequest(const ObjectMap& identity,
    const ObjectMap& listed, const std::string& hostKey,
    const std::string& interfaceMode)
{
    ObjectMap teeConnection;
    for (const auto& value : listed.at("connections").To<VariantArray>()) {
        const auto candidate = value.To<ObjectMap>();
        if (candidate.at("properties").To<ObjectMap>()
                .at("topology").To<std::string>() != "T") continue;
        for (const auto& memberValue : candidate.at("participants").To<VariantArray>())
            if (memberValue.To<ObjectMap>().at("itemKey").To<std::string>() == hostKey)
                teeConnection = candidate;
        if (!teeConnection.empty()) break;
    }
    if (teeConnection.empty())
        throw std::runtime_error("Product has no T junction for " + hostKey);
    VariantArray teeParticipants;
    const auto teeDeclarations = teeConnection.at("properties").To<ObjectMap>()
        .at("participantAnchors").To<VariantArray>();
    for (const auto& memberValue : teeConnection.at("participants").To<VariantArray>()) {
        const auto member = memberValue.To<ObjectMap>();
        const auto itemKey = member.at("itemKey").To<std::string>();
        ObjectMap anchor;
        for (const auto& declarationValue : teeDeclarations) {
            const auto declaration = declarationValue.To<ObjectMap>();
            if (declaration.at("itemKey").To<std::string>() == itemKey)
                anchor = declaration.at("anchor").To<ObjectMap>();
        }
        if (anchor.empty())
            throw std::runtime_error("T junction lacks an anchor for " + itemKey);
        const auto role = anchor.at("kind").To<std::string>() == "side" ? "host" : "branch";
        teeParticipants.emplace_back(ObjectMap{{"role", std::string(role)},
            {"memberEntityId", member.at("memberEntityId")},
            {"itemKey", itemKey}, {"anchor", std::move(anchor)}});
    }
    auto request = identity;
    request["sourceRevision"] = listed.at("sourceRevision");
    request["revision"] = listed.at("revision");
    request["connectionKey"] = teeConnection.at("key");
    request["templateId"] = std::string("two-end-middle");
    request["participants"] = std::move(teeParticipants);
    request["parameters"] = ObjectMap{{"interfaceMode", interfaceMode},
        {"intersectionAngle", 90.0}, {"fitGap", 0.2}};
    return request;
}

TEST(ProductAssemblyBindingSDO, CommittedCornerWrapVariantsCutRealMembersAndReopen)
{
    Scene baselineScene;
    const auto baselineGenerated = invoke(baselineScene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
        {"frameType", std::string("closed_frame")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"frameWidth", 60.0}, {"frameDepth", 40.0}, {"frameWallThickness", 2.0},
        {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto baselineProductID = baselineGenerated.at("product").To<ObjectMap>().at("entityId");
    const auto baselineRows = assemblyBindingPartRows(invoke(baselineScene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{baselineProductID}}})
        .at("tubeDesigner").To<ObjectMap>());
    struct Variant { bool swapRoles; std::string mode; std::string cornerKey; };
    const std::vector<Variant> variants{
        {false, "plain", "frame.corner.bottom-left"},
        {false, "plain", "frame.corner.top-right"},
        {false, "two", "frame.corner.bottom-left"},
        {true, "four", "frame.corner.bottom-left"},
    };
    for (const auto& choice : variants) {
        const std::string templateID = "wrap-a-over-b";
        SCOPED_TRACE(choice.cornerKey + (choice.swapRoles ? "-swapped-" : "-default-") + choice.mode);
        const auto projectID = iCAX::Data::GenerateNewUUID();
        Scene scene(projectID, projectID);
        const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("minimal-protective-grille")},
            {"parameters", ObjectMap{
                {"frameType", std::string("closed_frame")},
                {"assemblyPlanningMode", std::string("external_templates")},
                {"frameWidth", 60.0}, {"frameDepth", 40.0},
                {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
            }},
        });
        EXPECT_EQ("external_templates", nativePreview.at("parameters").To<ObjectMap>()
            .at("assemblyPlanningMode").To<std::string>());
        EXPECT_EQ(5u, nativePreview.at("items").To<VariantArray>().size());
        const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
            {"templateId", std::string("minimal-protective-grille")},
            {"frameType", std::string("closed_frame")},
            {"assemblyPlanningMode", std::string("external_templates")},
            {"frameWidth", 60.0}, {"frameDepth", 40.0}, {"frameWallThickness", 2.0},
            {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
        }).at("tubeDesigner").To<ObjectMap>();
        const auto product = generated.at("product").To<ObjectMap>();
        const ObjectMap identity{{"productEntityId", product.at("entityId")},
            {"generationRunId", product.at("activeGenerationRunId")}};
        const auto committed = invoke(scene, "GetProductAssemblyConnections", identity);
        VariantArray connectionMembers;
        std::map<std::string, ObjectMap> declaredAnchors;
        for (const auto& value : committed.at("connections").To<VariantArray>()) {
            const auto row = value.To<ObjectMap>();
            if (row.at("key").To<std::string>() == choice.cornerKey) {
                connectionMembers = row.at("participants").To<VariantArray>();
                const auto props = row.at("properties").To<ObjectMap>();
                EXPECT_EQ("L", props.at("topology").To<std::string>());
                for (const auto& anchorValue : props.at("participantAnchors").To<VariantArray>()) {
                    const auto declaration = anchorValue.To<ObjectMap>();
                    declaredAnchors.emplace(declaration.at("itemKey").To<std::string>(),
                        declaration.at("anchor").To<ObjectMap>());
                }
                break;
            }
        }
        ASSERT_EQ(2u, connectionMembers.size());
        ASSERT_EQ(2u, declaredAnchors.size());
        ObjectMap longMember, shortMember;
        const auto horizontalKey = choice.cornerKey == "frame.corner.top-right"
            ? "frame.top.0001" : "frame.bottom.0001";
        const auto verticalKey = choice.cornerKey == "frame.corner.top-right"
            ? "frame.right.0001" : "frame.left.0001";
        for (const auto& value : connectionMembers) {
            const auto member = value.To<ObjectMap>();
            const auto key = member.at("itemKey").To<std::string>();
            if (key == horizontalKey) longMember = member;
            else if (key == verticalKey) shortMember = member;
        }
        ASSERT_FALSE(longMember.empty());
        ASSERT_FALSE(shortMember.empty());
        const auto shortKey = shortMember.at("itemKey").To<std::string>();
        const auto longKey = longMember.at("itemKey").To<std::string>();
        ASSERT_TRUE(baselineRows.contains(shortKey));
        ASSERT_TRUE(baselineRows.contains(longKey));
        const auto beforeShortShape = assemblyBindingRowShape(baselineScene, baselineRows.at(shortKey));
        const auto beforeLongShape = assemblyBindingRowShape(baselineScene, baselineRows.at(longKey));
        const auto& mainMember = choice.swapRoles ? shortMember : longMember;
        const auto& branchMember = choice.swapRoles ? longMember : shortMember;
        const auto role = [](const char* name, const ObjectMap& member, ObjectMap anchor) {
            return ObjectMap{{"role", std::string(name)},
                {"memberEntityId", member.at("memberEntityId")}, {"itemKey", member.at("itemKey")},
                {"anchor", std::move(anchor)}};
        };
        // The process uses the committed L end/end anchors. Its near-end
        // side slot is derived from the main member's approachFace.
        const auto mainKey = mainMember.at("itemKey").To<std::string>();
        const auto branchKey = branchMember.at("itemKey").To<std::string>();
        ASSERT_TRUE(declaredAnchors.contains(mainKey));
        ASSERT_TRUE(declaredAnchors.contains(branchKey));
        const auto mainAnchor = declaredAnchors.at(mainKey);
        const auto branchAnchor = declaredAnchors.at(branchKey);
        ASSERT_EQ("end", mainAnchor.at("kind").To<std::string>());
        ASSERT_EQ("end", branchAnchor.at("kind").To<std::string>());
        ASSERT_TRUE(mainAnchor.contains("approachFace"));
        if (choice.cornerKey == "frame.corner.top-right" && !choice.swapRoles) {
            EXPECT_EQ("start", mainAnchor.at("end").To<std::string>());
            EXPECT_EQ("bottom", mainAnchor.at("approachFace").To<std::string>());
        }
        const VariantArray participants{
            role("memberA", mainMember, mainAnchor),
            role("memberB", branchMember, branchAnchor),
        };
        const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
        auto request = identity;
        request["sourceRevision"] = listed.at("sourceRevision");
        request["revision"] = listed.at("revision");
        request["templateId"] = templateID;
        request["connectionKey"] = choice.cornerKey;
        request["participants"] = participants;
        ObjectMap parameters;
        if (choice.mode != "plain") {
            parameters["maleFemale"] = true;
            parameters["pairCount"] = choice.mode;
        }
        request["parameters"] = parameters;
        if (choice.mode != "plain") {
            // Equal-size frame tubes have no flat wall left around paired
            // side slots. Reject that impossible male/female configuration.
            const auto rejected = invoke(scene, "PreviewProductAssemblyBinding", request);
            EXPECT_FALSE(rejected.at("canApply").To<bool>());
            const auto reason=rejected.at("detail").To<std::string>();
            // A missing trusted wall direction rejects this impossible
            // configuration before the side-socket envelope is evaluated.
            EXPECT_TRUE(reason.find("缺少已核对的实体壁面方向")!=std::string::npos
                || (reason.find("母槽")!=std::string::npos&&reason.find("平直")!=std::string::npos))<<reason;
            EXPECT_TRUE(invoke(scene, "GetProductAssemblyBindings", identity)
                .at("bindings").To<VariantArray>().empty());
            const auto rejectedRows = assemblyBindingPartRows(invoke(scene,
                "DisassembleSelected", ObjectMap{{"productEntityIds",
                    VariantArray{product.at("entityId")}}})
                .at("tubeDesigner").To<ObjectMap>());
            ASSERT_TRUE(rejectedRows.contains(shortKey));
            ASSERT_TRUE(rejectedRows.contains(longKey));
            checkSameShape(beforeShortShape,
                assemblyBindingRowShape(scene, rejectedRows.at(shortKey)));
            checkSameShape(beforeLongShape,
                assemblyBindingRowShape(scene, rejectedRows.at(longKey)));
            continue;
        }
        auto invalidParticipants = participants;
        for (auto& value : invalidParticipants) {
            auto participant = value.To<ObjectMap>();
            if (participant.at("role").To<std::string>() != "memberB") continue;
            auto anchor = participant.at("anchor").To<ObjectMap>();
            anchor["trim"] = 1.0;
            participant["anchor"] = std::move(anchor);
            value = std::move(participant);
        }
        auto invalidRequest = request;
        invalidRequest["participants"] = std::move(invalidParticipants);
        EXPECT_THROW((void)invoke(scene, "PreviewProductAssemblyBinding", invalidRequest),
            std::exception) << "The process may not override the product's committed end anchor";
        invalidParticipants = participants;
        for (auto& value : invalidParticipants) {
            auto participant = value.To<ObjectMap>();
            if (participant.at("role").To<std::string>() != "memberB") continue;
            auto anchor = participant.at("anchor").To<ObjectMap>();
            anchor["contactInset"] = anchor.at("contactInset").To<double>() + 1.0;
            participant["anchor"] = std::move(anchor);
            value = std::move(participant);
        }
        invalidRequest["participants"] = std::move(invalidParticipants);
        EXPECT_THROW((void)invoke(scene, "PreviewProductAssemblyBinding", invalidRequest),
            std::exception) << "Near-side contact distance must come from the committed product";
        const auto checked = invoke(scene, "PreviewProductAssemblyBinding", request);
        ASSERT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        EXPECT_FALSE(checked.at("parameters").To<ObjectMap>().contains("setback"));
        EXPECT_EQ("incremental-cut", checked.at("manufacturingEffect").To<std::string>());
        const auto changes = checked.at("geometryChanges").To<VariantArray>();
        ASSERT_EQ(1u, changes.size());
        EXPECT_EQ(branchKey, changes.front().To<ObjectMap>().at("itemKey").To<std::string>());
        EXPECT_LT(changes.front().To<ObjectMap>().at("volumeAfter").To<double>(),
            changes.front().To<ObjectMap>().at("volumeBefore").To<double>());
        auto apply = identity;
        apply["candidateToken"] = checked.at("candidateToken");
        ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", apply).at("applied").To<bool>());
        const auto after = invoke(scene, "GetProductAssemblyBindings", identity);
        ASSERT_EQ(1u, after.at("bindings").To<VariantArray>().size());
        const auto saved = after.at("bindings").To<VariantArray>().front().To<ObjectMap>();
        EXPECT_FALSE(saved.contains("parts"));
        EXPECT_EQ(saved.at("manufacturingEffect").To<std::string>(), "incremental-cut");

        const auto output = std::filesystem::current_path() / "tmp/product-assembly-wrap";
        std::filesystem::create_directories(output);
        const auto path = output / (templateID + "-" + choice.cornerKey
            + (choice.swapRoles ? "-swapped-" : "-") + choice.mode + ".ictd");
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
            .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1", .nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;
        info.ProjectID = scene.project;
        info.MainSceneID = scene.GetSceneID();
        info.ProjectName = "现有窗框长短包接端对端连接";
        file.Save(path, info, scene.Database(), scene.Resources());
        Scene reopened(scene.project, scene.GetSceneID(), false, true);
        file.Open(path, reopened.Database(), reopened.Resources());
        const auto restored = invoke(reopened, "GetProductAssemblyBindings", identity);
        EXPECT_EQ(after.at("bindings"), restored.at("bindings"));
        const auto rows = assemblyBindingPartRows(invoke(reopened, "List",
            ObjectMap{{"includeManufacturingGeometry", true}}).at("tubeDesigner").To<ObjectMap>());
        ASSERT_TRUE(rows.contains(shortKey));
        ASSERT_TRUE(rows.contains(longKey));
        const auto afterShortShape = assemblyBindingRowShape(reopened, rows.at(shortKey));
        const auto afterLongShape = assemblyBindingRowShape(reopened, rows.at(longKey));
        const auto beforeBranch = choice.swapRoles ? beforeLongShape : beforeShortShape;
        const auto afterBranch = choice.swapRoles ? afterLongShape : afterShortShape;
        const auto beforeHost = choice.swapRoles ? beforeShortShape : beforeLongShape;
        const auto afterHost = choice.swapRoles ? afterShortShape : afterLongShape;
        checkSameShape(beforeHost, afterHost);
        EXPECT_LT(volume(afterBranch), volume(beforeBranch));
        const auto original = assemblyBindingBounds(beforeBranch);
        const auto actual = assemblyBindingBounds(afterBranch);
        const double allowance = branchAnchor.at("stockAllowance").To<double>();
        const double inset = branchAnchor.at("contactInset").To<double>();
        ASSERT_GT(allowance, 0.0);
        ASSERT_GT(inset, 0.0);
        EXPECT_NEAR((original[3]-original[0]) - (actual[3]-actual[0]),
            allowance + inset, 0.05)
            << "The branch must retreat from its physical stock end to the host side wall";
    }
}

TEST(ProductAssemblyBindingSDO, ExistingWindowCombinesCornerAndInnerBarOnOneUncutBlank)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const ObjectMap parameters{{"frameType", std::string("closed_frame")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"frameWidth", 60.0}, {"frameDepth", 40.0}, {"frameWallThickness", 2.0},
        {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull}};
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")}, {"parameters", parameters}});
    EXPECT_EQ("external_templates", nativePreview.at("parameters").To<ObjectMap>()
        .at("assemblyPlanningMode").To<std::string>());
    ASSERT_EQ(5u, nativePreview.at("items").To<VariantArray>().size());
    auto generateRequest = parameters;
    generateRequest["templateId"] = std::string("minimal-protective-grille");
    const auto generated = invoke(scene, "GeneratePreview", generateRequest)
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId");
    const ObjectMap identity{{"productEntityId", productID},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto originalParameters = assemblyBindingProduct(scene,
        productID.To<std::string>())->GetParameters();
    const auto originalRows = assemblyBindingPartRows(invoke(scene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{productID}}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(5u, originalRows.size());
    constexpr auto sharedKey = "frame.left.0001";
    constexpr auto cornerBranchKey = "frame.bottom.0001";
    constexpr auto teeBranchKey = "inner.bar.0001";
    const auto originalLeft = assemblyBindingRowShape(scene, originalRows.at(sharedKey));
    const auto originalBar = assemblyBindingRowShape(scene, originalRows.at(teeBranchKey));
    const auto barBounds = assemblyBindingBounds(originalBar);

    std::map<std::string, ObjectMap> connections;
    for (const auto& value : invoke(scene, "GetProductAssemblyConnections", identity)
            .at("connections").To<VariantArray>()) {
        auto connection = value.To<ObjectMap>();
        connections.emplace(connection.at("key").To<std::string>(), std::move(connection));
    }
    ASSERT_EQ(6u, connections.size());
    const std::set<std::string> expectedNodes{
        "frame.corner.bottom-left", "frame.corner.bottom-right",
        "frame.corner.top-left", "frame.corner.top-right",
        "frame.junction.bar-0001-left", "frame.junction.bar-0001-right"};
    std::set<std::string> actualNodes;
    for (const auto& [key, connection] : connections) {
        actualNodes.insert(key);
        const bool isTee = key.starts_with("frame.junction.");
        const auto properties = connection.at("properties").To<ObjectMap>();
        EXPECT_EQ(isTee ? "T" : "L", properties.at("topology").To<std::string>());
        const auto anchors = properties.at("participantAnchors").To<VariantArray>();
        ASSERT_EQ(2u, anchors.size());
        EXPECT_EQ(isTee ? "side" : "end", anchors.front().To<ObjectMap>()
            .at("anchor").To<ObjectMap>().at("kind").To<std::string>());
        EXPECT_EQ("end", anchors.back().To<ObjectMap>()
            .at("anchor").To<ObjectMap>().at("kind").To<std::string>());
    }
    EXPECT_EQ(expectedNodes, actualNodes);
    const auto& corner = connections.at("frame.corner.bottom-left");
    const auto& tee = connections.at("frame.junction.bar-0001-left");
    const auto nodeX = [](const ObjectMap& connection) {
        return connection.at("properties").To<ObjectMap>()
            .at("centerlinePoint").To<VariantArray>().front().To<double>();
    };
    const double leftNodeX = nodeX(tee);
    const double rightNodeX = nodeX(connections.at("frame.junction.bar-0001-right"));
    const double productMidX = (leftNodeX + rightNodeX) / 2;
    const auto endAllowance = [teeBranchKey](const ObjectMap& connection) {
        for (const auto& value : connection.at("properties").To<ObjectMap>()
                .at("participantAnchors").To<VariantArray>()) {
            const auto declaration = value.To<ObjectMap>();
            if (declaration.at("itemKey").To<std::string>() == teeBranchKey)
                return declaration.at("anchor").To<ObjectMap>()
                    .at("stockAllowance").To<double>();
        }
        throw std::runtime_error("T branch has no declared stock allowance");
    };
    const double leftAllowance = endAllowance(tee);
    const double rightAllowance = endAllowance(connections.at("frame.junction.bar-0001-right"));
    // The product T nodes remain logical centerline points. One physical blank
    // carries stock beyond both nodes until their independent process cuts.
    EXPECT_GT(leftAllowance, 0.0);
    EXPECT_GT(rightAllowance, 0.0);
    EXPECT_NEAR(leftNodeX - productMidX - leftAllowance, barBounds[0], 0.05);
    EXPECT_NEAR(rightNodeX - productMidX + rightAllowance, barBounds[3], 0.05);
    EXPECT_NEAR(rightNodeX - leftNodeX + leftAllowance + rightAllowance,
        barBounds[3] - barBounds[0], 0.05);
    const auto participant = [](const ObjectMap& connection, const char* key,
        const char* role) -> ObjectMap {
        ObjectMap member, anchor;
        for (const auto& value : connection.at("participants").To<VariantArray>()) {
            auto candidate = value.To<ObjectMap>();
            if (candidate.at("itemKey").To<std::string>() == key) member = std::move(candidate);
        }
        for (const auto& value : connection.at("properties").To<ObjectMap>()
                .at("participantAnchors").To<VariantArray>()) {
            const auto declaration = value.To<ObjectMap>();
            if (declaration.at("itemKey").To<std::string>() == key)
                anchor = declaration.at("anchor").To<ObjectMap>();
        }
        if (member.empty() || anchor.empty())
            throw std::runtime_error("Committed product node lacks a participant anchor");
        return ObjectMap{{"role", std::string(role)},
            {"memberEntityId", member.at("memberEntityId")},
            {"itemKey", member.at("itemKey")}, {"anchor", std::move(anchor)}};
    };
    const auto preview = [&](const ObjectMap& state, const char* templateID,
        const char* connectionKey, VariantArray members, ObjectMap processParameters) {
        auto request = identity;
        request["sourceRevision"] = state.at("sourceRevision");
        request["revision"] = state.at("revision");
        request["templateId"] = std::string(templateID);
        request["connectionKey"] = std::string(connectionKey);
        request["participants"] = std::move(members);
        request["parameters"] = std::move(processParameters);
        return invoke(scene, "PreviewProductAssemblyBinding", request);
    };
    const auto apply = [&](const ObjectMap& checked) {
        auto request = identity;
        request["candidateToken"] = checked.at("candidateToken");
        return invoke(scene, "ApplyProductAssemblyBinding", request);
    };

    const auto initial = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto initialPlan = initial.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(5u, initialPlan.at("verifiedCoordinateMappingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, initialPlan.at("unsupportedCoordinateMappingCount").To<std::uint64_t>());
    const auto cornerChecked = preview(initial, "two-end-end-angle",
        "frame.corner.bottom-left", VariantArray{
            participant(corner, cornerBranchKey, "memberA"),
            participant(corner, sharedKey, "memberB")},
        ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0}, {"planeRotation", 0.0}});
    ASSERT_TRUE(cornerChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(cornerChecked);
    const auto cornerInterface = cornerChecked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured", cornerInterface.at("status").To<std::string>());
    EXPECT_LE(cornerInterface.at("intersectionVolumeMm3").To<double>(), 1e-4);
    ASSERT_EQ(2u, cornerChecked.at("geometryChanges").To<VariantArray>().size());
    ASSERT_TRUE(apply(cornerChecked).at("applied").To<bool>());
    const auto cornerRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(originalRows.size(), cornerRows.size());
    const auto afterCorner = assemblyBindingRowShape(scene, cornerRows.at(sharedKey));
    EXPECT_LT(volume(afterCorner), volume(originalLeft));

    const auto afterCornerState = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto teeChecked = preview(afterCornerState, "two-end-middle",
        "frame.junction.bar-0001-left", VariantArray{
            participant(tee, sharedKey, "host"),
            participant(tee, teeBranchKey, "branch")},
        ObjectMap{{"interfaceMode", std::string("singleInsert")},
            {"intersectionAngle", 90.0}, {"fitGap", 0.2}});
    ASSERT_TRUE(teeChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(teeChecked);
    const auto teeInterface = teeChecked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured", teeInterface.at("status").To<std::string>());
    EXPECT_LE(teeInterface.at("intersectionVolumeMm3").To<double>(), 1e-4);
    ASSERT_EQ(2u, teeChecked.at("geometryChanges").To<VariantArray>().size());
    std::set<std::string> teeChangedKeys;
    for (const auto& value : teeChecked.at("geometryChanges").To<VariantArray>())
        teeChangedKeys.insert(value.To<ObjectMap>().at("itemKey").To<std::string>());
    EXPECT_EQ((std::set<std::string>{sharedKey, teeBranchKey}), teeChangedKeys);
    ASSERT_TRUE(apply(teeChecked).at("applied").To<bool>());
    const auto finalRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(originalRows.size(), finalRows.size());
    ASSERT_TRUE(finalRows.contains(sharedKey));
    const auto combinedLeft = assemblyBindingRowShape(scene, finalRows.at(sharedKey));
    EXPECT_LT(volume(combinedLeft), volume(afterCorner));
    EXPECT_EQ(cornerRows.at(sharedKey).at("entityId"), finalRows.at(sharedKey).at("entityId"));
    const auto finalBar = assemblyBindingRowShape(scene, finalRows.at(teeBranchKey));
    EXPECT_LT(volume(finalBar), volume(originalBar));
    EXPECT_EQ(originalRows.at(teeBranchKey).at("entityId"),
        finalRows.at(teeBranchKey).at("entityId"));

    std::size_t sharedCutRecords = 0;
    const auto savedBindings = assemblyBindingProduct(scene,
        productID.To<std::string>())->GetAssemblyBindings();
    ASSERT_EQ(2u, savedBindings.size());
    for (const auto& [id, value] : savedBindings)
        for (const auto& partValue : value.To<ObjectMap>().at("parts").To<VariantArray>()) {
            const auto part = partValue.To<ObjectMap>();
            if (part.at("itemKey").To<std::string>() == sharedKey)
                sharedCutRecords += part.at("frozenCuts").To<VariantArray>().size();
        }
    EXPECT_EQ(2u, sharedCutRecords);
    EXPECT_EQ(originalParameters, assemblyBindingProduct(scene,
        productID.To<std::string>())->GetParameters());

    const auto path = std::filesystem::current_path()
        / "tmp/product-assembly-binding/existing-window-mixed-lt.ictd";
    std::filesystem::create_directories(path.parent_path());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1", .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID();
    info.ProjectName="现有窗框 L 与 T 混合加工";
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false, true);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto reopenedState = invoke(reopened, "GetProductAssemblyBindings", identity);
    ASSERT_EQ(2u, reopenedState.at("bindings").To<VariantArray>().size());
    EXPECT_EQ(originalParameters, assemblyBindingProduct(reopened,
        productID.To<std::string>())->GetParameters());
    const auto reopenedRows = assemblyBindingPartRows(invoke(reopened, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(originalRows.size(), reopenedRows.size());
    checkSameShape(combinedLeft, assemblyBindingRowShape(reopened, reopenedRows.at(sharedKey)));
}

TEST(ProductAssemblyBindingSDO, DISABLED_DeferredReference_ExistingAluminiumWindowAppliesIndependentCornerAndTeeCuts)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    // The catalogue's demonstration series is deliberately forbidden for
    // production disassembly. Use an isolated synthetic verified fixture to
    // exercise the native part mapping, never a deployed manufacturer series.
    const auto demonstration = std::filesystem::current_path()
        / "src/apps/tube-designer/templates/product/aluminium_window/systems/demonstration.json";
    std::ifstream source(demonstration);
    ASSERT_TRUE(source.good());
    const std::string sourceJson{std::istreambuf_iterator<char>(source),
        std::istreambuf_iterator<char>()};
    auto series = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(sourceJson).To<ObjectMap>();
    series["status"] = std::string("verified");
    series["manufacturer"] = std::string("UNIT TEST ONLY");
    series["source"] = std::string("Synthetic regression fixture, not a production series");
    const auto fixtureDir = std::filesystem::current_path() / "tmp/product-assembly-binding";
    std::filesystem::create_directories(fixtureDir);
    const auto seriesPath = fixtureDir / "verified-aluminium-series.json";
    {
        std::ofstream output(seriesPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        output << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(series);
    }
    const ObjectMap parameters{{"assemblyPlanningMode", std::string("external_templates")},
        {"systemSource", std::string("file")}, {"systemFile", seriesPath.string()},
        {"windowType", std::string("fixed")}, {"columns", std::int64_t{2}},
        {"rows", std::int64_t{2}},
        {"screenEnabled", false}};
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("aluminium-window")}, {"parameters", parameters}});
    const auto normalized = nativePreview.at("parameters").To<ObjectMap>();
    for (const auto& [key, value] : parameters) {
        ASSERT_TRUE(normalized.contains(key));
        EXPECT_EQ(value, normalized.at(key)) << key;
    }
    ASSERT_GE(nativePreview.at("items").To<VariantArray>().size(), 7u);

    auto generateRequest = parameters;
    generateRequest["templateId"] = std::string("aluminium-window");
    const auto generated = invoke(scene, "GeneratePreview", generateRequest)
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto connections = invoke(scene, "GetProductAssemblyConnections", identity)
        .at("connections").To<VariantArray>();
    ASSERT_EQ(10u, connections.size());
    std::size_t corners = 0, tees = 0;
    for (const auto& value : connections) {
        const auto connection = value.To<ObjectMap>();
        const auto props = connection.at("properties").To<ObjectMap>();
        ASSERT_TRUE(props.contains("participantAnchors"));
        ASSERT_EQ(2u, props.at("participantAnchors").To<VariantArray>().size());
        const auto topology = props.at("topology").To<std::string>();
        if (topology == "L") ++corners;
        else if (topology == "T") ++tees;
        else FAIL() << "unexpected product connection topology: " << topology;
    }
    EXPECT_EQ(4u, corners);
    EXPECT_EQ(6u, tees);
    const auto bindingState = invoke(scene, "GetProductAssemblyBindings", identity);
    EXPECT_EQ(7u, bindingState.at("members").To<VariantArray>().size());
    invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{product.at("entityId")}}});
    const auto plan = invoke(scene, "GetProductAssemblyBindings", identity)
        .at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(7u, plan.at("verifiedCoordinateMappingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unsupportedCoordinateMappingCount").To<std::uint64_t>());
    const auto originalRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    const auto originalLeft = assemblyBindingRowShape(scene, originalRows.at("frame.left"));
    const auto initial = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto cornerChecked = invoke(scene, "PreviewProductAssemblyBinding",
        assemblyBindingPreviewRequest(identity, initial));
    ASSERT_TRUE(cornerChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(cornerChecked);
    ASSERT_EQ("measured", cornerChecked.at("interfaceMeasurement").To<ObjectMap>()
        .at("status").To<std::string>());
    auto applyRequest = identity;
    applyRequest["candidateToken"] = cornerChecked.at("candidateToken");
    ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", applyRequest)
        .at("applied").To<bool>());
    const auto afterCornerRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(originalRows.size(), afterCornerRows.size());
    const auto afterCornerLeft = assemblyBindingRowShape(scene, afterCornerRows.at("frame.left"));
    assemblyBindingCheckSubset(originalLeft, afterCornerLeft);

    const auto afterCornerState = invoke(scene, "GetProductAssemblyBindings", identity);
    ObjectMap teeConnection;
    for (const auto& value : afterCornerState.at("connections").To<VariantArray>()) {
        const auto candidate = value.To<ObjectMap>();
        if (candidate.at("properties").To<ObjectMap>()
                .at("topology").To<std::string>() != "T") continue;
        for (const auto& memberValue : candidate.at("participants").To<VariantArray>())
            if (memberValue.To<ObjectMap>().at("itemKey").To<std::string>() == "frame.left")
                teeConnection = candidate;
        if (!teeConnection.empty()) break;
    }
    ASSERT_FALSE(teeConnection.empty());
    VariantArray teeParticipants;
    const auto teeDeclarations = teeConnection.at("properties").To<ObjectMap>()
        .at("participantAnchors").To<VariantArray>();
    for (const auto& memberValue : teeConnection.at("participants").To<VariantArray>()) {
        const auto member = memberValue.To<ObjectMap>();
        const auto itemKey = member.at("itemKey").To<std::string>();
        ObjectMap anchor;
        for (const auto& declarationValue : teeDeclarations) {
            const auto declaration = declarationValue.To<ObjectMap>();
            if (declaration.at("itemKey").To<std::string>() == itemKey)
                anchor = declaration.at("anchor").To<ObjectMap>();
        }
        ASSERT_FALSE(anchor.empty());
        const auto role = anchor.at("kind").To<std::string>() == "side" ? "host" : "branch";
        teeParticipants.emplace_back(ObjectMap{{"role", std::string(role)},
            {"memberEntityId", member.at("memberEntityId")},
            {"itemKey", itemKey}, {"anchor", std::move(anchor)}});
    }
    auto teeRequest = identity;
    teeRequest["sourceRevision"] = afterCornerState.at("sourceRevision");
    teeRequest["revision"] = afterCornerState.at("revision");
    teeRequest["connectionKey"] = teeConnection.at("key");
    teeRequest["templateId"] = std::string("two-end-middle");
    teeRequest["participants"] = std::move(teeParticipants);
    teeRequest["parameters"] = ObjectMap{{"interfaceMode", std::string("singleInsert")},
        {"intersectionAngle", 90.0}, {"fitGap", 0.2}};
    // The demonstration transom has the same 120 mm outside depth as the
    // frame, but its 1.5 mm walls leave only 117 mm of clear depth inside the
    // frame. A one-sided insertion would physically overlap the frame walls.
    const auto incompatibleInsert = invoke(scene, "PreviewProductAssemblyBinding", teeRequest);
    EXPECT_FALSE(incompatibleInsert.at("canApply").To<bool>());
    bool foundSectionConflict = false;
    for (const auto& checkValue : incompatibleInsert.at("checks").To<VariantArray>()) {
        const auto check = checkValue.To<ObjectMap>();
        if (check.at("id").To<std::string>() != "actual-material-conflict") continue;
        foundSectionConflict |= check.at("detail").To<std::string>()
            .find("内孔") != std::string::npos;
    }
    EXPECT_TRUE(foundSectionConflict);
    teeRequest["parameters"] = ObjectMap{{"interfaceMode", std::string("saddle")},
        {"intersectionAngle", 90.0}, {"fitGap", 0.2}};
    const auto teeChecked = invoke(scene, "PreviewProductAssemblyBinding", teeRequest);
    ASSERT_TRUE(teeChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(teeChecked);
    ASSERT_EQ("measured", teeChecked.at("interfaceMeasurement").To<ObjectMap>()
        .at("status").To<std::string>());
    applyRequest["candidateToken"] = teeChecked.at("candidateToken");
    ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", applyRequest)
        .at("applied").To<bool>());
    const auto combinedRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(originalRows.size(), combinedRows.size());
    EXPECT_EQ(originalRows.at("frame.left").at("entityId"),
        combinedRows.at("frame.left").at("entityId"));
    // Saddle cutting shapes the branch against the unchanged host sidewall;
    // the already-cut corner on that host must survive the next binding.
    checkSameShape(afterCornerLeft,
        assemblyBindingRowShape(scene, combinedRows.at("frame.left")));
    assemblyBindingCheckSubset(
        assemblyBindingRowShape(scene, originalRows.at("transom.1.1")),
        assemblyBindingRowShape(scene, combinedRows.at("transom.1.1")));
}

TEST(ProductAssemblyBindingSDO, DISABLED_DeferredReference_TeeTabSlotsCutRealProductMembers)
{
    for(const auto& variant : {std::pair<double,const char*>{0.0,"two"},
            {90.0,"two"}, {0.0,"four"}, {90.0,"four"}}){
    SCOPED_TRACE(std::string("bladeRollAngle=")+std::to_string(variant.first)
        +" pairCount="+variant.second);
    const auto projectID=iCAX::Data::GenerateNewUUID();
    Scene scene(projectID,projectID);
    const auto generated=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("louver-window")},
        {"assemblyPlanningMode",std::string("external_templates")},
        {"arrayMode",std::string("count")}, {"bladeCount",std::int64_t{1}},
        {"bladeDirectionAngle",90.0}, {"bladeRollAngle",variant.first},
        {"frameWidth",80.0}, {"frameDepth",80.0},
        {"bladeWidth",40.0}, {"bladeDepth",40.0},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    const ObjectMap identity{
        {"productEntityId",product.at("entityId")},
        {"generationRunId",product.at("activeGenerationRunId")},
    };
    const auto originals=assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{product.at("entityId")}}
    }).at("tubeDesigner").To<ObjectMap>());
    const auto listed=invoke(scene,"GetProductAssemblyBindings",identity);
    auto request=assemblyBindingTeePreviewRequest(identity,listed,"frame.bottom","saddle");
    request["templateId"]=std::string("end-side-tab-slot");
    request["parameters"]=ObjectMap{{"pairCount",std::string(variant.second)},
        {"tabWidth",8.0},{"tabLength",12.0},{"sideClearance",0.2}};
    const auto checked=invoke(scene,"PreviewProductAssemblyBinding",request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        <<checked.at("detail").To<std::string>() << " "
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
            checked.at("interfaceMeasurement"));
    const auto measurement=checked.at("interfaceMeasurement").To<ObjectMap>();
    EXPECT_EQ(measurement.at("status").To<std::string>(),"measured");
    EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")),1.0e-4);
    const auto changes=checked.at("geometryChanges").To<VariantArray>();
    ASSERT_EQ(changes.size(),2u);
    for(const auto& value:changes){
        const auto change=value.To<ObjectMap>();
        EXPECT_GT(assemblyBindingNumber(change.at("volumeBefore")),
            assemblyBindingNumber(change.at("volumeAfter")));
    }
    auto apply=identity;
    apply["candidateToken"]=checked.at("candidateToken");
    const auto applied=invoke(scene,"ApplyProductAssemblyBinding",apply);
    ASSERT_TRUE(applied.at("applied").To<bool>());
    const auto rows=assemblyBindingPartRows(applied.at("tubeDesigner").To<ObjectMap>());
    for(const auto& key:{"frame.bottom","blade.1"}){
        ASSERT_TRUE(originals.contains(key));
        ASSERT_TRUE(rows.contains(key));
        assemblyBindingCheckSubset(assemblyBindingRowShape(scene,originals.at(key)),
            assemblyBindingRowShape(scene,rows.at(key)));
    }
    }
}

TEST(ProductAssemblyBindingSDO, DISABLED_DeferredReference_TeeProfileInsertCutsRealTopWallAndBranch)
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
    const auto originals=assemblyBindingPartRows(invoke(scene,"DisassembleSelected",ObjectMap{
        {"productEntityIds",VariantArray{product.at("entityId")}}
    }).at("tubeDesigner").To<ObjectMap>());
    const auto listed=invoke(scene,"GetProductAssemblyBindings",identity);
    auto request=assemblyBindingTeePreviewRequest(identity,listed,"frame.bottom","singleInsert");
    request["templateId"]=std::string("t-profile-insert");
    request["parameters"]=ObjectMap{{"intersectionAngle",90.0},
        {"insertDepth",12.0},{"fitGap",0.2}};
    const auto checked=invoke(scene,"PreviewProductAssemblyBinding",request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    const auto measurement=checked.at("interfaceMeasurement").To<ObjectMap>();
    EXPECT_EQ(measurement.at("status").To<std::string>(),"measured");
    EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")),1.0e-4);
    const auto changes=checked.at("geometryChanges").To<VariantArray>();
    ASSERT_EQ(changes.size(),2u);
    for(const auto& value:changes){
        const auto change=value.To<ObjectMap>();
        EXPECT_GT(assemblyBindingNumber(change.at("volumeBefore")),
            assemblyBindingNumber(change.at("volumeAfter")));
    }
    auto apply=identity;
    apply["candidateToken"]=checked.at("candidateToken");
    const auto applied=invoke(scene,"ApplyProductAssemblyBinding",apply);
    ASSERT_TRUE(applied.at("applied").To<bool>());
    const auto rows=assemblyBindingPartRows(applied.at("tubeDesigner").To<ObjectMap>());
    for(const auto& key:{"frame.bottom","blade.1"}){
        ASSERT_TRUE(originals.contains(key));
        ASSERT_TRUE(rows.contains(key));
        assemblyBindingCheckSubset(assemblyBindingRowShape(scene,originals.at(key)),
            assemblyBindingRowShape(scene,rows.at(key)));
    }
}

TEST(ProductAssemblyBindingSDO, DISABLED_DeferredReference_AluminiumWindowFitsNarrowBranchInTrustedTOpening)
{
    const auto demonstration = std::filesystem::current_path()
        / "src/apps/tube-designer/templates/product/aluminium_window/systems/demonstration.json";
    std::ifstream source(demonstration);
    ASSERT_TRUE(source.good());
    const std::string sourceJson{std::istreambuf_iterator<char>(source),
        std::istreambuf_iterator<char>()};
    auto series = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(sourceJson).To<ObjectMap>();
    series["status"] = std::string("verified");
    series["manufacturer"] = std::string("UNIT TEST ONLY");
    series["source"] = std::string("Synthetic narrow branch regression fixture");
    auto profiles = series.at("profiles").To<ObjectMap>();
    auto transom = profiles.at("transom").To<ObjectMap>();
    // The frame has a 117 mm inside depth. Only this synthetic test profile
    // narrows the transom outer depth to 110 mm, leaving 3.5 mm per side.
    transom["depth"] = 110.0;
    auto contours = transom.at("contours").To<VariantArray>();
    ASSERT_EQ(2u, contours.size());
    for (std::size_t index = 0; index < contours.size(); ++index) {
        auto contour = contours[index].To<ObjectMap>();
        auto segments = contour.at("segments").To<VariantArray>();
        const double halfDepth = index == 0 ? 55.0 : 53.5;
        for (auto& segmentValue : segments) {
            auto segment = segmentValue.To<ObjectMap>();
            for (const char* key : {"start", "end"}) {
                auto point = segment.at(key).To<VariantArray>();
                point[1] = assemblyBindingNumber(point[1]) < 0.0
                    ? -halfDepth : halfDepth;
                segment[key] = std::move(point);
            }
            segmentValue = std::move(segment);
        }
        contour["segments"] = std::move(segments);
        contours[index] = std::move(contour);
    }
    transom["contours"] = std::move(contours);
    profiles["transom"] = std::move(transom);
    series["profiles"] = std::move(profiles);
    const auto fixtureDir = std::filesystem::current_path() / "tmp/product-assembly-binding";
    std::filesystem::create_directories(fixtureDir);
    const auto seriesPath = fixtureDir / "verified-aluminium-narrow-transom.json";
    {
        std::ofstream output(seriesPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        output << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(series);
    }
    const ObjectMap parameters{{"assemblyPlanningMode", std::string("external_templates")},
        {"systemSource", std::string("file")}, {"systemFile", seriesPath.string()},
        {"windowType", std::string("fixed")}, {"columns", std::int64_t{2}},
        {"rows", std::int64_t{2}}, {"screenEnabled", false}};
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("aluminium-window")}, {"parameters", parameters}});
    const auto normalized = preview.at("parameters").To<ObjectMap>();
    for (const auto& [key, value] : parameters) {
        ASSERT_TRUE(normalized.contains(key));
        EXPECT_EQ(value, normalized.at(key)) << key;
    }
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("aluminium-window")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"systemSource", std::string("file")}, {"systemFile", seriesPath.string()},
        {"windowType", std::string("fixed")}, {"columns", std::int64_t{2}},
        {"rows", std::int64_t{2}}, {"screenEnabled", false}})
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{product.at("entityId")}}});
    const auto beforeRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto request = assemblyBindingTeePreviewRequest(
        identity, listed, "frame.left", "singleInsert");
    auto zeroGapRequest = request;
    auto zeroGapParameters = zeroGapRequest.at("parameters").To<ObjectMap>();
    zeroGapParameters["fitGap"] = 0.0;
    zeroGapRequest["parameters"] = std::move(zeroGapParameters);
    const auto zeroGapChecked = invoke(scene, "PreviewProductAssemblyBinding", zeroGapRequest);
    ASSERT_TRUE(zeroGapChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(zeroGapChecked);
    const auto checked = invoke(scene, "PreviewProductAssemblyBinding", request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    const auto measurement = checked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured", measurement.at("status").To<std::string>());
    EXPECT_LE(assemblyBindingNumber(measurement.at("intersectionVolumeMm3")), 1.0e-4);
    const auto removedFromHost = [](const ObjectMap& preview) {
        for (const auto& value : preview.at("geometryChanges").To<VariantArray>()) {
            const auto change = value.To<ObjectMap>();
            if (change.at("itemKey").To<std::string>() == "frame.left")
                return assemblyBindingNumber(change.at("volumeBefore"))
                    - assemblyBindingNumber(change.at("volumeAfter"));
        }
        throw std::runtime_error("T opening did not cut the host blank");
    };
    EXPECT_GT(removedFromHost(checked), removedFromHost(zeroGapChecked) + 1.0e-3)
        << "Increasing the template clearance must remove more real host material";
    auto applyRequest = identity;
    applyRequest["candidateToken"] = checked.at("candidateToken");
    ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", applyRequest)
        .at("applied").To<bool>());
    const auto afterRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    assemblyBindingCheckSubset(
        assemblyBindingRowShape(scene, beforeRows.at("frame.left")),
        assemblyBindingRowShape(scene, afterRows.at("frame.left")));
    assemblyBindingCheckSubset(
        assemblyBindingRowShape(scene, beforeRows.at("transom.1.1")),
        assemblyBindingRowShape(scene, afterRows.at("transom.1.1")));

    // The second T faces the opposite way. The same template must derive its
    // opening direction from that product anchor rather than a fixed sign.
    const auto rightListed = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto rightRequest = assemblyBindingTeePreviewRequest(
        identity, rightListed, "frame.right", "singleInsert");
    const auto rightChecked = invoke(scene, "PreviewProductAssemblyBinding", rightRequest);
    ASSERT_TRUE(rightChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(rightChecked);
    const auto rightMeasurement = rightChecked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured", rightMeasurement.at("status").To<std::string>());
    EXPECT_LE(assemblyBindingNumber(rightMeasurement.at("intersectionVolumeMm3")), 1.0e-4);
    applyRequest["candidateToken"] = rightChecked.at("candidateToken");
    ASSERT_TRUE(invoke(scene, "ApplyProductAssemblyBinding", applyRequest)
        .at("applied").To<bool>());
    const auto bothRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    assemblyBindingCheckSubset(
        assemblyBindingRowShape(scene, beforeRows.at("frame.right")),
        assemblyBindingRowShape(scene, bothRows.at("frame.right")));
    assemblyBindingCheckSubset(
        assemblyBindingRowShape(scene, beforeRows.at("transom.1.2")),
        assemblyBindingRowShape(scene, bothRows.at("transom.1.2")));
}

TEST(ProductAssemblyBindingSDO, ExistingSecurityWindowPublishesUncutFrameAndCornerTeeNodes)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const ObjectMap parameters{{"assemblyPlanningMode", std::string("external_templates")},
        {"faceType", std::string("single")},
        {"frameLayout", std::string("four_sides")},
        {"accessDoorEnabled", false},
        {"infillPattern", std::string("horizontal")}};
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")},
        {"parameters", parameters}});
    const auto normalized = nativePreview.at("parameters").To<ObjectMap>();
    for (const auto& [key, value] : parameters) {
        ASSERT_TRUE(normalized.contains(key));
        EXPECT_EQ(value, normalized.at(key)) << key;
    }
    ASSERT_EQ(8u, nativePreview.at("items").To<VariantArray>().size());

    auto generateRequest = parameters;
    generateRequest["templateId"] = std::string("single-face-security-window");
    const auto generated = invoke(scene, "GeneratePreview", generateRequest)
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto connections = invoke(scene, "GetProductAssemblyConnections", identity)
        .at("connections").To<VariantArray>();
    ASSERT_EQ(12u, connections.size());
    std::size_t corners = 0, tees = 0;
    for (const auto& value : connections) {
        const auto props = value.To<ObjectMap>().at("properties").To<ObjectMap>();
        const auto topology = props.at("topology").To<std::string>();
        if (topology == "L") ++corners;
        else if (topology == "T") ++tees;
        else FAIL() << "unexpected product connection topology: " << topology;
    }
    EXPECT_EQ(4u, corners);
    EXPECT_EQ(8u, tees);
    const auto bindingState = invoke(scene, "GetProductAssemblyBindings", identity);
    EXPECT_EQ(8u, bindingState.at("members").To<VariantArray>().size());
    const auto disassembled = invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{product.at("entityId")}}})
        .at("tubeDesigner").To<ObjectMap>();
    const auto rows = assemblyBindingPartRows(disassembled);
    ASSERT_FALSE(rows.empty());
    const auto plan = invoke(scene, "GetProductAssemblyBindings", identity)
        .at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(8u, plan.at("verifiedCoordinateMappingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unsupportedCoordinateMappingCount").To<std::uint64_t>());
    try {
        invoke(scene, "StageNestingParts", ObjectMap{
            {"partEntityIds", VariantArray{rows.begin()->second.at("entityId")}}});
        FAIL() << "unverified assembly must not enter production nesting";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string::npos, std::string(error.what()).find("装配工艺尚未完成整件配合验证"));
    }
    try {
        invoke(scene, "ExportSelected", ObjectMap{
            {"targetDirectory", (std::filesystem::current_path()
                / "tmp/product-assembly-binding/unverified-security-window").string()}});
        FAIL() << "unverified assembly must not be exported as production stock";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string::npos, std::string(error.what()).find("装配工艺尚未完成整件配合验证"));
    }
}

TEST(ProductAssemblyBindingSDO, DISABLED_DeferredReference_ExistingLouverWindowPublishesUncutFrameAndCornerTeeNodes)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const ObjectMap parameters{{"assemblyPlanningMode", std::string("external_templates")},
        {"arrayMode", std::string("count")}, {"bladeCount", std::int64_t{3}}};
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("louver-window")}, {"parameters", parameters}});
    const auto normalized = nativePreview.at("parameters").To<ObjectMap>();
    for (const auto& [key, value] : parameters) {
        ASSERT_TRUE(normalized.contains(key));
        EXPECT_EQ(value, normalized.at(key)) << key;
    }
    ASSERT_EQ(7u, nativePreview.at("items").To<VariantArray>().size());

    auto generateRequest = parameters;
    generateRequest["templateId"] = std::string("louver-window");
    const auto generated = invoke(scene, "GeneratePreview", generateRequest)
        .at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto connections = invoke(scene, "GetProductAssemblyConnections", identity)
        .at("connections").To<VariantArray>();
    ASSERT_EQ(10u, connections.size());
    std::size_t corners = 0, tees = 0;
    for (const auto& value : connections) {
        const auto props = value.To<ObjectMap>().at("properties").To<ObjectMap>();
        const auto topology = props.at("topology").To<std::string>();
        if (topology == "L") ++corners;
        else if (topology == "T") ++tees;
        else FAIL() << "unexpected product connection topology: " << topology;
    }
    EXPECT_EQ(4u, corners);
    EXPECT_EQ(6u, tees);
    const auto bindingState = invoke(scene, "GetProductAssemblyBindings", identity);
    EXPECT_EQ(7u, bindingState.at("members").To<VariantArray>().size());
    invoke(scene, "DisassembleSelected", ObjectMap{
        {"productEntityIds", VariantArray{product.at("entityId")}}});
    const auto plan = invoke(scene, "GetProductAssemblyBindings", identity)
        .at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(7u, plan.at("verifiedCoordinateMappingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unsupportedCoordinateMappingCount").To<std::uint64_t>());
}

void assemblyBindingCheckSubset(const TopoDS_Shape& original, const TopoDS_Shape& processed)
{
    ASSERT_TRUE(BRepCheck_Analyzer(processed).IsValid());
    ASSERT_GT(volume(original), volume(processed));
    BRepAlgoAPI_Common common;
    NCollection_List<TopoDS_Shape> originals, processedParts;
    originals.Append(original);
    processedParts.Append(processed);
    common.SetArguments(originals);
    common.SetTools(processedParts);
    common.SetNonDestructive(true);
    common.SetFuzzyValue(1e-6);
    common.Build();
    ASSERT_TRUE(common.IsDone());
    ASSERT_FALSE(common.Shape().IsNull());
    // Every remaining material point must have existed in the original part;
    // rebuilding a nominal tube and filling its old holes fails this check.
    EXPECT_NEAR(volume(processed), volume(common.Shape()), std::max(1.0, volume(processed)) * 1e-6);
}

TEST(ProductAssemblyBindingSDO, AppliesIncrementalCutsWithoutPriorDisassemblyAndReopensAndRemoves)
{
    // The desktop host requires its main scene ID to equal the project ID.
    // Keep this saved fixture valid for opening in the real TubeDesigner app.
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
        {"frameType", std::string("closed_frame")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const auto runID = product.at("activeGenerationRunId").To<std::string>();
    const auto parameters = assemblyBindingProduct(scene, productID)->GetParameters();
    const ObjectMap identity{{"productEntityId", productID}, {"generationRunId", runID}};
    ASSERT_TRUE(generated.at("parts").To<VariantArray>().empty());
    const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
    ASSERT_TRUE(listed.at("bindings").To<VariantArray>().empty());
    ASSERT_GE(listed.at("members").To<VariantArray>().size(), 2u);
    const auto listedPlan = listed.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(0u, listedPlan.at("selectedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ(listed.at("connections").To<VariantArray>().size(),
        listedPlan.at("unassignedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ("not-verified", listedPlan.at("fitStatus").To<std::string>());
    EXPECT_EQ("not-ready", listedPlan.at("readiness").To<std::string>());
    const auto leftBefore = assemblyBindingPlanMember(listedPlan, "frame.left.0001");
    EXPECT_EQ("not-present", leftBefore.at("partMappingStatus").To<std::string>());
    // One physical frame tube participates in both corner and bar joints.
    EXPECT_GT(leftBefore.at("connections").To<VariantArray>().size(), 2u);
    EXPECT_TRUE(leftBefore.at("operations").To<VariantArray>().empty());
    const auto previewRequest = assemblyBindingPreviewRequest(identity, listed);
    const auto checked = invoke(scene, "PreviewProductAssemblyBinding", previewRequest);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    ASSERT_FALSE(checked.at("candidateToken").To<std::string>().empty());
    ASSERT_EQ(2u, checked.at("geometryChanges").To<VariantArray>().size());
    std::map<std::string, std::pair<double, double>> checkedVolumes;
    for (const auto& value : checked.at("geometryChanges").To<VariantArray>()) {
        const auto change = value.To<ObjectMap>();
        const double before = change.at("volumeBefore").To<double>();
        const double after = change.at("volumeAfter").To<double>();
        EXPECT_GT(before, after);
        EXPECT_GT(after, 0.0);
        checkedVolumes.emplace(change.at("itemKey").To<std::string>(), std::make_pair(before, after));
    }
    EXPECT_TRUE(assemblyBindingProduct(scene, productID)->GetAssemblyBindings().empty());
    EXPECT_TRUE(invoke(scene, "List", {}).at("tubeDesigner").To<ObjectMap>()
        .at("parts").To<VariantArray>().empty());
    const auto staleCandidate = invoke(scene, "PreviewProductAssemblyBinding", previewRequest);
    ASSERT_TRUE(staleCandidate.at("canApply").To<bool>());

    // An independent product supplies the already-machined baseline without
    // pre-disassembling the scene whose first binding is being exercised.
    Scene baselineScene;
    const auto baselineGenerated = invoke(baselineScene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")},
        {"frameType", std::string("closed_frame")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto baselineID = baselineGenerated.at("product").To<ObjectMap>().at("entityId");
    const auto baselineRows = assemblyBindingPartRows(invoke(baselineScene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{baselineID}}}).at("tubeDesigner").To<ObjectMap>());

    auto applyRequest = identity;
    applyRequest["candidateToken"] = checked.at("candidateToken");
    const auto applied = invoke(scene, "ApplyProductAssemblyBinding", applyRequest);
    ASSERT_TRUE(applied.at("applied").To<bool>());
    ASSERT_TRUE(applied.at("manufacturingUpdated").To<bool>());
    const auto appliedRows = assemblyBindingPartRows(applied.at("tubeDesigner").To<ObjectMap>());
    const auto afterGet = invoke(scene, "GetProductAssemblyBindings", identity);
    ASSERT_EQ(1u, afterGet.at("bindings").To<VariantArray>().size());
    const auto afterPlan = afterGet.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(1u, afterPlan.at("selectedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ(listed.at("connections").To<VariantArray>().size()-1,
        afterPlan.at("unassignedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ("not-verified", afterPlan.at("fitStatus").To<std::string>());
    EXPECT_EQ("not-ready", afterPlan.at("readiness").To<std::string>());
    const auto leftAfter = assemblyBindingPlanMember(afterPlan, "frame.left.0001");
    EXPECT_EQ("frame.left.0001", leftAfter.at("itemKey").To<std::string>());
    EXPECT_EQ("persisted", leftAfter.at("partMappingStatus").To<std::string>());
    EXPECT_EQ(appliedRows.at("frame.left.0001").at("entityId"), leftAfter.at("partEntityId"));
    EXPECT_EQ(appliedRows.at("frame.left.0001").at("length"), leftAfter.at("partLength"));
    EXPECT_GT(leftAfter.at("connections").To<VariantArray>().size(), 2u);
    ASSERT_EQ(1u, leftAfter.at("operations").To<VariantArray>().size());
    EXPECT_EQ("frame.corner.bottom-left", leftAfter.at("operations").To<VariantArray>().front()
        .To<ObjectMap>().at("connectionKey").To<std::string>());
    const auto bindingID = afterGet.at("bindings").To<VariantArray>().front()
        .To<ObjectMap>().at("bindingId").To<std::string>();
    EXPECT_EQ(listed.at("sourceRevision"), afterGet.at("sourceRevision"));
    EXPECT_NE(listed.at("revision"), afterGet.at("revision"));
    EXPECT_EQ(parameters, assemblyBindingProduct(scene, productID)->GetParameters());
    std::map<std::string, std::vector<uint8_t>> appliedBytes;
    for (const auto& [key, expected] : checkedVolumes) {
        ASSERT_TRUE(baselineRows.contains(key));
        ASSERT_TRUE(appliedRows.contains(key));
        const auto original = assemblyBindingRowShape(baselineScene, baselineRows.at(key));
        const auto processed = assemblyBindingRowShape(scene, appliedRows.at(key));
        EXPECT_NEAR(expected.first, volume(original), std::max(1.0, expected.first) * 1e-6);
        EXPECT_NEAR(expected.second, volume(processed), std::max(1.0, expected.second) * 1e-6);
        assemblyBindingCheckSubset(original, processed);
        appliedBytes[key] = persistedShapeBytes(scene, part(scene,
            appliedRows.at(key).at("entityId").To<std::string>()));
    }
    auto staleApply = identity;
    staleApply["candidateToken"] = staleCandidate.at("candidateToken");
    EXPECT_THROW(invoke(scene, "ApplyProductAssemblyBinding", staleApply), std::exception);
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        assemblyBindingPreviewRequest(identity, afterGet)), std::exception);

    const auto directory = std::filesystem::current_path() / "tmp/product-assembly-binding";
    std::filesystem::create_directories(directory);
    const auto path = directory / "actual-cuts.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1", .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID(); info.ProjectName="实际构件装配加工";
    ASSERT_EQ(info.ProjectID, info.MainSceneID);
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false, true);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto reopenedGet = invoke(reopened, "GetProductAssemblyBindings", identity);
    EXPECT_EQ(afterGet.at("bindings"), reopenedGet.at("bindings"));
    EXPECT_EQ(afterGet.at("revision"), reopenedGet.at("revision"));
    EXPECT_EQ(parameters, assemblyBindingProduct(reopened, productID)->GetParameters());
    // Product manufacturing BReps are derived resources. Request the public
    // manufacturing-geometry restore path after opening the project.
    const auto reopenedRows = assemblyBindingPartRows(invoke(reopened, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    for (const auto& [key, row] : reopenedRows) {
        const auto url = row.at("thumbnailGeometryResourceId").To<std::string>();
        const auto version = row.at("thumbnailGeometryResourceVersion").To<std::uint64_t>();
        const auto render = reopened.Resources().Get<iCAX::Resource::CFlatBufferResource>(url, version);
        ASSERT_TRUE(render) << key << " missing render resource " << url << "@" << version;
    }
    const auto stepDirectory = directory / "step-readback";
    std::filesystem::create_directories(stepDirectory);
    VariantArray selectedPartIDs;
    for (const auto& [key, expected] : checkedVolumes) {
        ASSERT_TRUE(reopenedRows.contains(key));
        selectedPartIDs.emplace_back(reopenedRows.at(key).at("entityId"));
    }
    const auto exported = invoke(reopened, "ExportSelected", ObjectMap{
        {"targetDirectory", stepDirectory.string()}, {"generationRunId", runID},
        {"partEntityIds", selectedPartIDs},
        {"allowUnverifiedDraft", true},
    });
    EXPECT_TRUE(exported.at("draftOnly").To<bool>());
    const auto stepFiles = exported.at("exportedFiles").To<VariantArray>();
    ASSERT_EQ(checkedVolumes.size(), stepFiles.size());
    for (const auto& [key, expected] : checkedVolumes) {
        ASSERT_TRUE(reopenedRows.contains(key));
        const auto restored = assemblyBindingRowShape(reopened, reopenedRows.at(key));
        EXPECT_NEAR(expected.second, volume(restored),
            std::max(1.0, expected.second) * 1e-6);
        EXPECT_EQ(appliedBytes.at(key), persistedShapeBytes(reopened, part(reopened,
            reopenedRows.at(key).at("entityId").To<std::string>())));
        // ExportSelected must emit this precise manufacturing part, not an
        // unmachined product-preview shape or another selected part.
        const auto name = reopenedRows.at(key).at("fileName").To<std::string>();
        std::string stepPath;
        for (const auto& value : stepFiles) {
            const auto candidate = value.To<std::string>();
            if (candidate.size() >= name.size()
                && candidate.compare(candidate.size() - name.size(), name.size(), name) == 0)
                stepPath = candidate;
        }
        ASSERT_FALSE(stepPath.empty());
        const auto roundTrip = assemblyBindingReadStep(stepPath);
        EXPECT_TRUE(BRepCheck_Analyzer(roundTrip).IsValid());
        EXPECT_NEAR(volume(restored), volume(roundTrip),
            std::max(1.0, volume(restored)) * 1e-5);
        checkSameShape(restored, roundTrip);
        assemblyBindingCheckSubset(
            assemblyBindingRowShape(baselineScene, baselineRows.at(key)), roundTrip);
    }
    auto removeRequest = identity;
    removeRequest["bindingId"] = bindingID;
    const auto removed = invoke(reopened, "RemoveProductAssemblyBinding", removeRequest);
    ASSERT_TRUE(removed.at("removed").To<bool>());
    ASSERT_TRUE(removed.at("manufacturingUpdated").To<bool>());
    EXPECT_TRUE(invoke(reopened, "GetProductAssemblyBindings", identity).at("bindings").To<VariantArray>().empty());
    EXPECT_EQ(parameters, assemblyBindingProduct(reopened, productID)->GetParameters());
    const auto restoredRows = assemblyBindingPartRows(removed.at("tubeDesigner").To<ObjectMap>());
    for (const auto& [key, expected] : checkedVolumes)
        checkSameShape(assemblyBindingRowShape(baselineScene, baselineRows.at(key)),
            assemblyBindingRowShape(reopened, restoredRows.at(key)));

    const auto removableState = invoke(reopened, "GetProductAssemblyBindings", identity);
    const auto replacedCandidate = invoke(reopened, "PreviewProductAssemblyBinding",
        assemblyBindingPreviewRequest(identity, removableState));
    ASSERT_TRUE(replacedCandidate.at("canApply").To<bool>());
    const auto regenerated = invoke(reopened, "GeneratePreview", ObjectMap{
        {"templateId", std::string("minimal-protective-grille")}, {"productEntityId", productID},
        {"frameType", std::string("closed_frame")},
        {"assemblyPlanningMode", std::string("external_templates")},
        {"barLayoutMode", std::string("fixed_count")}, {"barCount", 1ull},
    }).at("tubeDesigner").To<ObjectMap>();
    EXPECT_NE(runID, regenerated.at("product").To<ObjectMap>().at("activeGenerationRunId").To<std::string>());
    EXPECT_THROW(invoke(reopened, "GetProductAssemblyBindings", identity), std::exception);
    auto replacedApply = identity;
    replacedApply["candidateToken"] = replacedCandidate.at("candidateToken");
    EXPECT_THROW(invoke(reopened, "ApplyProductAssemblyBinding", replacedApply), std::exception);
}

TEST(ProductAssemblyBindingSDO, OutdatedStoredTemplateDoesNotCountAsSelected)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const auto runID = product.at("activeGenerationRunId").To<std::string>();
    const ObjectMap identity{{"productEntityId", productID}, {"generationRunId", runID}};
    const auto initial = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto sourceRevision = initial.at("sourceRevision");
    const ObjectMap saved{{"legacy-corner", ObjectMap{
        {"schemaVersion", 1ull}, {"generationRunId", runID},
        {"sourceDigest", sourceRevision},
        {"connectionKey", std::string("frame.corner.bottom-left")},
        {"templateId", std::string("two-end-end-angle")},
        {"templateDigest", std::string("stale-descriptor-digest")},
        {"parameters", ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0}, {"planeRotation", 0.0}}},
        {"manufacturingEffect", std::string("incremental-cut")},
    }}};
    ASSERT_TRUE(assemblyBindingProduct(scene, productID)->SetAssemblyBindings(saved));
    const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto bindings = listed.at("bindings").To<VariantArray>();
    ASSERT_EQ(1u, bindings.size());
    const auto row = bindings.front().To<ObjectMap>();
    EXPECT_EQ("outdated", row.at("status").To<std::string>());
    EXPECT_TRUE(row.at("canRemove").To<bool>());
    const auto plan = listed.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(0u, plan.at("selectedConnectionCount").To<std::uint64_t>());
    EXPECT_GT(plan.at("unassignedConnectionCount").To<std::uint64_t>(), 0u);
}

TEST(ProductAssemblyBindingSDO, CombinesCornerAndMidspanProcessesOnOneFrameBlank)
{
    // The product declares geometry and L/T relationships, but leaves both
    // joints unmachined. Each joint below selects its own process template.
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    });
    EXPECT_EQ("assembly-frame-lt", nativePreview.at("templateId").To<std::string>());
    EXPECT_EQ(5u, nativePreview.at("items").To<VariantArray>().size());
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const auto runID = product.at("activeGenerationRunId").To<std::string>();
    const ObjectMap identity{{"productEntityId", productID}, {"generationRunId", runID}};
    const auto productParameters = assemblyBindingProduct(scene, productID)->GetParameters();
    const auto baselineRows = assemblyBindingPartRows(invoke(scene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{productID}}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(5u, baselineRows.size());
    constexpr auto sharedKey = "frame.left.0001";
    constexpr auto rightKey = "frame.right.0001";
    constexpr auto bottomKey = "frame.bottom.0001";
    constexpr auto middleKey = "frame.middle.0001";
    ASSERT_TRUE(baselineRows.contains(sharedKey));
    ASSERT_TRUE(baselineRows.contains(rightKey));
    ASSERT_TRUE(baselineRows.contains(bottomKey));
    ASSERT_TRUE(baselineRows.contains(middleKey));
    const auto baselineLeft = assemblyBindingRowShape(scene, baselineRows.at(sharedKey));
    const auto baselineRight = assemblyBindingRowShape(scene, baselineRows.at(rightKey));
    const auto baselineBottom = assemblyBindingRowShape(scene, baselineRows.at(bottomKey));
    const auto baselineMiddle = assemblyBindingRowShape(scene, baselineRows.at(middleKey));
    const auto baselineLeftBounds = assemblyBindingBounds(baselineLeft);

    const auto connections = invoke(scene, "GetProductAssemblyConnections", identity);
    std::map<std::string, std::map<std::string, ObjectMap>> connectionMembers;
    std::map<std::string, std::map<std::string, ObjectMap>> connectionAnchors;
    for (const auto& value : connections.at("connections").To<VariantArray>()) {
        const auto connection = value.To<ObjectMap>();
        const auto connectionKey = connection.at("key").To<std::string>();
        auto& members = connectionMembers[connectionKey];
        for (const auto& participant : connection.at("participants").To<VariantArray>()) {
            const auto member = participant.To<ObjectMap>();
            members.emplace(member.at("itemKey").To<std::string>(), member);
        }
        const auto properties = connection.at("properties").To<ObjectMap>();
        if (const auto found = properties.find("participantAnchors"); found != properties.end())
            for (const auto& anchorValue : found->second.To<VariantArray>()) {
                const auto declaration = anchorValue.To<ObjectMap>();
                connectionAnchors[connectionKey].emplace(
                    declaration.at("itemKey").To<std::string>(),
                    declaration.at("anchor").To<ObjectMap>());
            }
    }
    const auto& corner = connectionMembers.at("frame.corner.bottom-left");
    const auto& tee = connectionMembers.at("frame.junction.middle-left");
    const auto& rightTee = connectionMembers.at("frame.junction.middle-right");
    const auto& cornerAnchors = connectionAnchors.at("frame.corner.bottom-left");
    const auto& bottomRightAnchors = connectionAnchors.at("frame.corner.bottom-right");
    const auto& topLeftAnchors = connectionAnchors.at("frame.corner.top-left");
    const auto& teeAnchors = connectionAnchors.at("frame.junction.middle-left");
    const auto& rightTeeAnchors = connectionAnchors.at("frame.junction.middle-right");
    ASSERT_EQ(2u, corner.size());
    ASSERT_EQ(2u, tee.size());
    ASSERT_EQ(2u, rightTee.size());
    ASSERT_TRUE(corner.contains(sharedKey));
    ASSERT_TRUE(corner.contains(bottomKey));
    ASSERT_TRUE(tee.contains(sharedKey));
    ASSERT_TRUE(tee.contains(middleKey));
    ASSERT_TRUE(rightTee.contains(rightKey));
    ASSERT_TRUE(rightTee.contains(middleKey));
    ASSERT_TRUE(cornerAnchors.contains(sharedKey));
    ASSERT_TRUE(cornerAnchors.contains(bottomKey));
    ASSERT_TRUE(teeAnchors.contains(sharedKey));
    ASSERT_TRUE(teeAnchors.contains(middleKey));
    ASSERT_TRUE(rightTeeAnchors.contains(rightKey));
    ASSERT_TRUE(rightTeeAnchors.contains(middleKey));
    EXPECT_EQ("start", cornerAnchors.at(sharedKey).at("end").To<std::string>());
    EXPECT_EQ("start", cornerAnchors.at(bottomKey).at("end").To<std::string>());
    const double leftStartAllowance = cornerAnchors.at(sharedKey)
        .at("stockAllowance").To<double>();
    const double leftEndAllowance = topLeftAnchors.at(sharedKey)
        .at("stockAllowance").To<double>();
    const double rightStartAllowance = bottomRightAnchors.at(rightKey)
        .at("stockAllowance").To<double>();
    EXPECT_GT(leftStartAllowance, 0.0);
    EXPECT_GT(leftEndAllowance, 0.0);
    EXPECT_NEAR(1440.0 + leftStartAllowance + leftEndAllowance,
        baselineLeftBounds[3] - baselineLeftBounds[0], 0.05);
    EXPECT_EQ("bottom", teeAnchors.at(sharedKey).at("face").To<std::string>());
    EXPECT_NEAR(720.0, teeAnchors.at(sharedKey).at("station").To<double>(), 1e-6);
    EXPECT_EQ("start", teeAnchors.at(middleKey).at("end").To<std::string>());
    EXPECT_EQ("top", rightTeeAnchors.at(rightKey).at("face").To<std::string>());
    EXPECT_NEAR(720.0, rightTeeAnchors.at(rightKey).at("station").To<double>(), 1e-6);
    EXPECT_EQ("end", rightTeeAnchors.at(middleKey).at("end").To<std::string>());
    EXPECT_EQ(corner.at(sharedKey).at("memberEntityId"),
        tee.at(sharedKey).at("memberEntityId"));

    const auto endParticipant = [](const char* role, const ObjectMap& member,
        const ObjectMap& declaredAnchor) {
        auto anchor = declaredAnchor;
        anchor["trim"] = 0.0;
        anchor["rotation"] = 0.0;
        return ObjectMap{{"role", std::string(role)},
            {"memberEntityId", member.at("memberEntityId")},
            {"itemKey", member.at("itemKey")},
            {"anchor", std::move(anchor)}};
    };
    const auto sideParticipant = [](const char* role, const ObjectMap& member,
        const ObjectMap& declaredAnchor) {
        return ObjectMap{{"role", std::string(role)},
            {"memberEntityId", member.at("memberEntityId")},
            {"itemKey", member.at("itemKey")},
            {"anchor", ObjectMap{{"kind", std::string("side")},
                {"face", declaredAnchor.at("face")}, {"reference", std::string("start")},
                {"station", declaredAnchor.at("station")},
                {"offset", 0.0}, {"rotation", 0.0}}}};
    };
    const auto requestFor = [&](const ObjectMap& state, const char* templateID,
        const char* connectionKey, VariantArray participants, ObjectMap parameters) {
        auto request = identity;
        request["sourceRevision"] = state.at("sourceRevision");
        request["revision"] = state.at("revision");
        request["templateId"] = std::string(templateID);
        request["connectionKey"] = std::string(connectionKey);
        request["participants"] = std::move(participants);
        request["parameters"] = std::move(parameters);
        return request;
    };
    const auto applyChecked = [&](const ObjectMap& checked) {
        EXPECT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        auto request = identity;
        request["candidateToken"] = checked.at("candidateToken");
        const auto result = invoke(scene, "ApplyProductAssemblyBinding", request);
        EXPECT_TRUE(result.at("applied").To<bool>());
        EXPECT_TRUE(result.at("manufacturingUpdated").To<bool>());
        return result;
    };

    const auto initial = invoke(scene, "GetProductAssemblyBindings", identity);
    ASSERT_TRUE(initial.at("bindings").To<VariantArray>().empty());
    const VariantArray cornerParticipants{
        endParticipant("memberA", corner.at(bottomKey), cornerAnchors.at(bottomKey)),
        endParticipant("memberB", corner.at(sharedKey), cornerAnchors.at(sharedKey)),
    };
    // Both candidates have two end anchors, but the committed corner is L.
    // A coaxial tab/slot template or a 0-degree straight variant must not bind.
    const VariantArray straightParticipants{
        endParticipant("tab", corner.at(bottomKey), cornerAnchors.at(bottomKey)),
        endParticipant("slot", corner.at(sharedKey), cornerAnchors.at(sharedKey)),
    };
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "tab-slot-lock", "frame.corner.bottom-left",
            straightParticipants, ObjectMap{})), std::exception);
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            cornerParticipants, ObjectMap{{"jointAngle", 0.0}})), std::exception);
    EXPECT_TRUE(invoke(scene, "GetProductAssemblyBindings", identity)
        .at("bindings").To<VariantArray>().empty());
    auto wrongCornerParticipants = cornerParticipants;
    {
        auto member = wrongCornerParticipants[1].To<ObjectMap>();
        auto anchor = member.at("anchor").To<ObjectMap>();
        anchor["end"] = std::string("end");
        member["anchor"] = std::move(anchor);
        wrongCornerParticipants[1] = std::move(member);
    }
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            wrongCornerParticipants, ObjectMap{{"jointAngle", 90.0}})), std::exception);
    auto missingAllowanceParticipants = cornerParticipants;
    {
        auto member = missingAllowanceParticipants[1].To<ObjectMap>();
        auto anchor = member.at("anchor").To<ObjectMap>();
        anchor.erase("stockAllowance");
        member["anchor"] = std::move(anchor);
        missingAllowanceParticipants[1] = std::move(member);
    }
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            missingAllowanceParticipants, ObjectMap{{"jointAngle", 90.0}})), std::exception);
    auto wrongAllowanceParticipants = cornerParticipants;
    {
        auto member = wrongAllowanceParticipants[1].To<ObjectMap>();
        auto anchor = member.at("anchor").To<ObjectMap>();
        anchor["stockAllowance"] = leftStartAllowance + 1.0;
        member["anchor"] = std::move(anchor);
        wrongAllowanceParticipants[1] = std::move(member);
    }
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            wrongAllowanceParticipants, ObjectMap{{"jointAngle", 90.0}})), std::exception);
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            cornerParticipants, ObjectMap{{"jointAngle", 89.0}})), std::exception);
    const auto cornerChecked = invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(initial, "two-end-end-angle", "frame.corner.bottom-left",
            cornerParticipants,
            ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0}, {"planeRotation", 0.0}}));
    ASSERT_TRUE(cornerChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(cornerChecked);
    ASSERT_EQ(2u, cornerChecked.at("geometryChanges").To<VariantArray>().size());
    const auto afterCorner = applyChecked(cornerChecked);
    const auto cornerRows = assemblyBindingPartRows(afterCorner.at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baselineRows.size(), cornerRows.size());
    const auto cornerLeft = assemblyBindingRowShape(scene, cornerRows.at(sharedKey));
    const auto cornerBottom = assemblyBindingRowShape(scene, cornerRows.at(bottomKey));
    assemblyBindingCheckSubset(baselineLeft, cornerLeft);
    assemblyBindingCheckSubset(baselineBottom, cornerBottom);
    checkSameShape(baselineMiddle, assemblyBindingRowShape(scene, cornerRows.at(middleKey)));

    const auto afterCornerState = invoke(scene, "GetProductAssemblyBindings", identity);
    ASSERT_EQ(1u, afterCornerState.at("bindings").To<VariantArray>().size());
    const auto cornerBindingID = afterCornerState.at("bindings").To<VariantArray>().front()
        .To<ObjectMap>().at("bindingId").To<std::string>();
    const VariantArray teeParticipants{
        sideParticipant("host", tee.at(sharedKey), teeAnchors.at(sharedKey)),
        endParticipant("branch", tee.at(middleKey), teeAnchors.at(middleKey)),
    };
    const auto rejectWrongTeeAnchor = [&](const char* field, const iCAX::Data::Variant& value) {
        auto participants = teeParticipants;
        auto host = participants.front().To<ObjectMap>();
        auto anchor = host.at("anchor").To<ObjectMap>();
        anchor[field] = value;
        host["anchor"] = std::move(anchor);
        participants.front() = std::move(host);
        EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
            requestFor(afterCornerState, "two-end-middle", "frame.junction.middle-left",
                std::move(participants), ObjectMap{{"interfaceMode", std::string("singleInsert")}})),
            std::exception);
    };
    rejectWrongTeeAnchor("face", std::string("left"));
    rejectWrongTeeAnchor("station", 719.0);
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(afterCornerState, "two-end-middle", "frame.junction.middle-left",
            teeParticipants, ObjectMap{{"interfaceMode", std::string("singleInsert")},
                {"intersectionAngle", 80.0}})), std::exception);
    const auto teeChecked = invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(afterCornerState, "two-end-middle", "frame.junction.middle-left",
            teeParticipants,
            ObjectMap{{"interfaceMode", std::string("singleInsert")},
                {"intersectionAngle", 90.0}, {"fitGap", 0.2}}));
    ASSERT_TRUE(teeChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(teeChecked);
    const auto teeChanges = teeChecked.at("geometryChanges").To<VariantArray>();
    ASSERT_EQ(2u, teeChanges.size());
    std::set<std::string> teeChangedKeys;
    for (const auto& value : teeChanges)
        teeChangedKeys.insert(value.To<ObjectMap>().at("itemKey").To<std::string>());
    EXPECT_EQ((std::set<std::string>{sharedKey, middleKey}), teeChangedKeys);
    const auto afterTee = applyChecked(teeChecked);
    const auto combinedRows = assemblyBindingPartRows(afterTee.at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baselineRows.size(), combinedRows.size());
    const auto combinedLeft = assemblyBindingRowShape(scene, combinedRows.at(sharedKey));
    assemblyBindingCheckSubset(cornerLeft, combinedLeft);
    checkSameShape(cornerBottom, assemblyBindingRowShape(scene, combinedRows.at(bottomKey)));
    const auto combinedMiddle = assemblyBindingRowShape(scene, combinedRows.at(middleKey));
    assemblyBindingCheckSubset(baselineMiddle, combinedMiddle);
    EXPECT_LT(volume(combinedMiddle), volume(baselineMiddle) - 1.0);
    EXPECT_EQ(cornerRows.at(sharedKey).at("sourceMemberId"),
        combinedRows.at(sharedKey).at("sourceMemberId"));
    EXPECT_EQ(cornerRows.at(sharedKey).at("entityId"),
        combinedRows.at(sharedKey).at("entityId"));

    const auto combinedState = invoke(scene, "GetProductAssemblyBindings", identity);
    ASSERT_EQ(2u, combinedState.at("bindings").To<VariantArray>().size());
    std::string teeBindingID;
    std::set<std::string> boundKeys;
    std::size_t sharedCutRecords = 0;
    ObjectMap teeSharedCut;
    const auto persistedBindings = assemblyBindingProduct(scene, productID)->GetAssemblyBindings();
    ASSERT_EQ(2u, persistedBindings.size());
    for (const auto& [bindingID, value] : persistedBindings) {
        const auto binding = value.To<ObjectMap>();
        const auto key = binding.at("connectionKey").To<std::string>();
        boundKeys.insert(key);
        if (key == "frame.junction.middle-left")
            teeBindingID = bindingID;
        for (const auto& partValue : binding.at("parts").To<VariantArray>()) {
            const auto boundPart = partValue.To<ObjectMap>();
            if (boundPart.at("itemKey").To<std::string>() == sharedKey) {
                const auto frozenCuts = boundPart.at("frozenCuts").To<VariantArray>();
                sharedCutRecords += frozenCuts.size();
                if (key == "frame.junction.middle-left" && !frozenCuts.empty())
                    teeSharedCut = frozenCuts.front().To<ObjectMap>();
            }
        }
    }
    EXPECT_EQ((std::set<std::string>{"frame.corner.bottom-left", "frame.junction.middle-left"}),
        boundKeys);
    EXPECT_EQ(2u, sharedCutRecords);
    ASSERT_FALSE(teeBindingID.empty());
    ASSERT_FALSE(teeSharedCut.empty());
    const auto cutterResource = scene.Resources().Get<BRepModel>(
        teeSharedCut.at("url").To<std::string>(), teeSharedCut.at("version").To<std::uint64_t>());
    ASSERT_TRUE(cutterResource);
    const auto cutter = iCAX::OpenCascade::BuildOpenCascadeShape(*cutterResource);
    ASSERT_TRUE(cutter.bOK);
    BRepAlgoAPI_Common removedByTee;
    NCollection_List<TopoDS_Shape> stockShapes, cutterShapes;
    stockShapes.Append(cornerLeft);
    cutterShapes.Append(cutter.Shape);
    removedByTee.SetArguments(stockShapes);
    removedByTee.SetTools(cutterShapes);
    removedByTee.SetNonDestructive(true);
    removedByTee.Build();
    ASSERT_TRUE(removedByTee.IsDone());
    ASSERT_GT(volume(removedByTee.Shape()), 0.0);
    const auto hostBounds = assemblyBindingBounds(cornerLeft);
    const auto removedBounds = assemblyBindingBounds(removedByTee.Shape());
    // This vertical member rotates world +X (its declared right face) to
    // local -Z for manufacturing. The T tool must cut that wall at its node.
    EXPECT_LT(removedBounds[5], (hostBounds[2] + hostBounds[5]) / 2);
    EXPECT_NEAR(hostBounds[0] + leftStartAllowance
            + teeAnchors.at(sharedKey).at("station").To<double>(),
        (removedBounds[0] + removedBounds[3]) / 2, 1.0);
    EXPECT_EQ(productParameters, assemblyBindingProduct(scene, productID)->GetParameters());

    // The opposite T uses the same process template with a different declared
    // face. It must cut the right frame member's local top wall, then be
    // removable without disturbing the two bindings on the left member.
    const VariantArray rightTeeParticipants{
        sideParticipant("host", rightTee.at(rightKey), rightTeeAnchors.at(rightKey)),
        endParticipant("branch", rightTee.at(middleKey), rightTeeAnchors.at(middleKey)),
    };
    const auto rightTeeChecked = invoke(scene, "PreviewProductAssemblyBinding",
        requestFor(combinedState, "two-end-middle", "frame.junction.middle-right",
            rightTeeParticipants,
            ObjectMap{{"interfaceMode", std::string("singleInsert")},
                {"intersectionAngle", 90.0}, {"fitGap", 0.2}}));
    ASSERT_TRUE(rightTeeChecked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(rightTeeChecked);
    const auto afterRightTee = applyChecked(rightTeeChecked);
    const auto rightTeeRows = assemblyBindingPartRows(
        afterRightTee.at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baselineRows.size(), rightTeeRows.size());
    const auto rightTeeShape = assemblyBindingRowShape(scene, rightTeeRows.at(rightKey));
    assemblyBindingCheckSubset(baselineRight, rightTeeShape);
    checkSameShape(combinedLeft, assemblyBindingRowShape(scene, rightTeeRows.at(sharedKey)));
    const auto rightTeeMiddle = assemblyBindingRowShape(scene, rightTeeRows.at(middleKey));
    assemblyBindingCheckSubset(combinedMiddle, rightTeeMiddle);
    EXPECT_LT(volume(rightTeeMiddle), volume(combinedMiddle) - 1.0);
    std::string rightTeeBindingID;
    ObjectMap rightTeeCut;
    const auto rightBindings = assemblyBindingProduct(scene, productID)->GetAssemblyBindings();
    ASSERT_EQ(3u, rightBindings.size());
    for (const auto& [bindingID, value] : rightBindings) {
        const auto binding = value.To<ObjectMap>();
        if (binding.at("connectionKey").To<std::string>() != "frame.junction.middle-right")
            continue;
        rightTeeBindingID = bindingID;
        for (const auto& partValue : binding.at("parts").To<VariantArray>()) {
            const auto part = partValue.To<ObjectMap>();
            if (part.at("itemKey").To<std::string>() != rightKey) continue;
            const auto cuts = part.at("frozenCuts").To<VariantArray>();
            ASSERT_EQ(1u, cuts.size());
            rightTeeCut = cuts.front().To<ObjectMap>();
            const auto features = part.at("features").To<VariantArray>();
            ASSERT_EQ(1u, features.size());
            const auto feature = features.front().To<ObjectMap>();
            const auto operation = feature.at("operationParameterValues").To<ObjectMap>();
            EXPECT_EQ("negative", operation.at("direction").To<std::string>());
            const auto azimuth = operation.at("azimuth");
            ASSERT_TRUE(azimuth.Is<std::int64_t>() || azimuth.Is<std::uint64_t>()
                || azimuth.Is<double>());
            const double azimuthDegrees = azimuth.Is<double>() ? azimuth.To<double>()
                : azimuth.Is<std::int64_t>() ? static_cast<double>(azimuth.To<std::int64_t>())
                : static_cast<double>(azimuth.To<std::uint64_t>());
            EXPECT_DOUBLE_EQ(180.0, azimuthDegrees);
        }
    }
    ASSERT_FALSE(rightTeeBindingID.empty());
    ASSERT_FALSE(rightTeeCut.empty());
    const auto rightCutterResource = scene.Resources().Get<BRepModel>(
        rightTeeCut.at("url").To<std::string>(),
        rightTeeCut.at("version").To<std::uint64_t>());
    ASSERT_TRUE(rightCutterResource);
    const auto rightCutter = iCAX::OpenCascade::BuildOpenCascadeShape(*rightCutterResource);
    ASSERT_TRUE(rightCutter.bOK);
    BRepAlgoAPI_Common removedByRightTee;
    NCollection_List<TopoDS_Shape> rightStockShapes, rightCutterShapes;
    rightStockShapes.Append(baselineRight);
    rightCutterShapes.Append(rightCutter.Shape);
    removedByRightTee.SetArguments(rightStockShapes);
    removedByRightTee.SetTools(rightCutterShapes);
    removedByRightTee.SetNonDestructive(true);
    removedByRightTee.Build();
    ASSERT_TRUE(removedByRightTee.IsDone());
    ASSERT_GT(volume(removedByRightTee.Shape()), 0.0);
    const auto rightHostBounds = assemblyBindingBounds(baselineRight);
    const auto rightRemovedBounds = assemblyBindingBounds(removedByRightTee.Shape());
    EXPECT_GT(rightRemovedBounds[2], (rightHostBounds[2] + rightHostBounds[5]) / 2);
    EXPECT_NEAR(rightHostBounds[0] + rightStartAllowance
            + rightTeeAnchors.at(rightKey).at("station").To<double>(),
        (rightRemovedBounds[0] + rightRemovedBounds[3]) / 2, 1.0);
    auto removeRightTee = identity;
    removeRightTee["bindingId"] = rightTeeBindingID;
    const auto removedRightTee = invoke(scene, "RemoveProductAssemblyBinding", removeRightTee);
    ASSERT_TRUE(removedRightTee.at("removed").To<bool>());
    const auto rightRemovedRows = assemblyBindingPartRows(
        removedRightTee.at("tubeDesigner").To<ObjectMap>());
    checkSameShape(baselineRight, assemblyBindingRowShape(scene, rightRemovedRows.at(rightKey)));
    checkSameShape(combinedLeft, assemblyBindingRowShape(scene, rightRemovedRows.at(sharedKey)));
    checkSameShape(combinedMiddle, assemblyBindingRowShape(scene, rightRemovedRows.at(middleKey)));
    EXPECT_EQ(2u, invoke(scene, "GetProductAssemblyBindings", identity)
        .at("bindings").To<VariantArray>().size());

    const auto directory = std::filesystem::current_path() / "tmp/product-assembly-binding/mixed-lt";
    std::filesystem::create_directories(directory);
    const auto path = directory / "mixed-lt.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer", .CurrentFormatVersion="0.1", .nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID(); info.ProjectName="L 与 T 混合节点加工";
    file.Save(path, info, scene.Database(), scene.Resources());
    Scene reopened(scene.project, scene.GetSceneID(), false, true);
    file.Open(path, reopened.Database(), reopened.Resources());
    const auto reopenedState = invoke(reopened, "GetProductAssemblyBindings", identity);
    EXPECT_EQ(combinedState.at("bindings"), reopenedState.at("bindings"));
    EXPECT_EQ(productParameters, assemblyBindingProduct(reopened, productID)->GetParameters());
    const auto reopenedRows = assemblyBindingPartRows(invoke(reopened, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baselineRows.size(), reopenedRows.size());
    checkSameShape(combinedLeft, assemblyBindingRowShape(reopened, reopenedRows.at(sharedKey)));
    checkSameShape(combinedMiddle, assemblyBindingRowShape(reopened, reopenedRows.at(middleKey)));

    const auto stepDirectory = directory / "step-readback";
    std::filesystem::create_directories(stepDirectory);
    const auto exported = invoke(reopened, "ExportSelected", ObjectMap{
        {"targetDirectory", stepDirectory.string()}, {"generationRunId", runID},
        {"partEntityIds", VariantArray{reopenedRows.at(sharedKey).at("entityId")}},
        {"allowUnverifiedDraft", true},
    });
    EXPECT_TRUE(exported.at("draftOnly").To<bool>());
    const auto stepFiles = exported.at("exportedFiles").To<VariantArray>();
    ASSERT_EQ(1u, stepFiles.size());
    const auto roundTrip = assemblyBindingReadStep(stepFiles.front().To<std::string>());
    checkSameShape(combinedLeft, roundTrip);

    auto removeTee = identity;
    removeTee["bindingId"] = teeBindingID;
    const auto removedTee = invoke(reopened, "RemoveProductAssemblyBinding", removeTee);
    ASSERT_TRUE(removedTee.at("removed").To<bool>());
    const auto teeRemovedRows = assemblyBindingPartRows(removedTee.at("tubeDesigner").To<ObjectMap>());
    checkSameShape(cornerLeft, assemblyBindingRowShape(reopened, teeRemovedRows.at(sharedKey)));
    checkSameShape(cornerBottom, assemblyBindingRowShape(reopened, teeRemovedRows.at(bottomKey)));
    checkSameShape(baselineMiddle, assemblyBindingRowShape(reopened, teeRemovedRows.at(middleKey)));
    const auto teeRemovedState = invoke(reopened, "GetProductAssemblyBindings", identity);
    ASSERT_EQ(1u, teeRemovedState.at("bindings").To<VariantArray>().size());
    EXPECT_EQ(cornerBindingID, teeRemovedState.at("bindings").To<VariantArray>().front()
        .To<ObjectMap>().at("bindingId").To<std::string>());

    auto removeCorner = identity;
    removeCorner["bindingId"] = cornerBindingID;
    const auto removedCorner = invoke(reopened, "RemoveProductAssemblyBinding", removeCorner);
    ASSERT_TRUE(removedCorner.at("removed").To<bool>());
    const auto restoredRows = assemblyBindingPartRows(removedCorner.at("tubeDesigner").To<ObjectMap>());
    checkSameShape(baselineLeft, assemblyBindingRowShape(reopened, restoredRows.at(sharedKey)));
    checkSameShape(baselineBottom, assemblyBindingRowShape(reopened, restoredRows.at(bottomKey)));
    checkSameShape(baselineMiddle, assemblyBindingRowShape(reopened, restoredRows.at(middleKey)));
    EXPECT_TRUE(invoke(reopened, "GetProductAssemblyBindings", identity)
        .at("bindings").To<VariantArray>().empty());
}

TEST(ProductAssemblyBindingSDO, RejectsCommittedNodesWhoseGeometryContradictsTheirAnchors)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto runID = iCAX::Data::uuid::from_string(
        product.at("activeGenerationRunId").To<std::string>());
    ASSERT_TRUE(runID.has_value());
    const auto runEntity = scene.Database().GetEntity(*runID);
    ASSERT_TRUE(runEntity);
    const auto run = std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        runEntity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(run);
    const auto original = run->GetNeutralModel();
    const auto setCorruptedNode = [&](const std::string& key, bool sideStation) {
        auto document = original;
        auto relationships = document.at("relationships").To<VariantArray>();
        bool changed = false;
        for (auto& value : relationships) {
            auto relationship = value.To<ObjectMap>();
            if (relationship.at("key").To<std::string>() != key) continue;
            auto properties = relationship.at("properties").To<ObjectMap>();
            if (sideStation) {
                auto rows = properties.at("participantAnchors").To<VariantArray>();
                for (auto& rowValue : rows) {
                    auto row = rowValue.To<ObjectMap>();
                    auto anchor = row.at("anchor").To<ObjectMap>();
                    if (anchor.at("kind").To<std::string>() != "side") continue;
                    anchor["station"] = anchor.at("station").To<double>() + 1.0;
                    row["anchor"] = std::move(anchor);
                    rowValue = std::move(row);
                    changed = true;
                    break;
                }
                properties["participantAnchors"] = std::move(rows);
            } else {
                auto point = properties.at("centerlinePoint").To<VariantArray>();
                point[0] = point[0].To<double>() + 1.0;
                properties["centerlinePoint"] = std::move(point);
                changed = true;
            }
            relationship["properties"] = std::move(properties);
            value = std::move(relationship);
            break;
        }
        if (!changed) throw std::runtime_error("Expected fixture node was not found");
        document["relationships"] = std::move(relationships);
        if (!run->SetNeutralModel(document))
            throw std::runtime_error("Could not set the test generation model");
    };
    const auto findNode = [](const ObjectMap& listed, const std::string& key) {
        for (const auto& value : listed.at("connections").To<VariantArray>()) {
            const auto row = value.To<ObjectMap>();
            if (row.at("key").To<std::string>() == key) return row;
        }
        throw std::runtime_error("Fixture connection was not listed");
    };

    setCorruptedNode("frame.corner.bottom-left", false);
    const auto badCorner = invoke(scene, "GetProductAssemblyConnections", identity);
    EXPECT_EQ("invalid", findNode(badCorner, "frame.corner.bottom-left")
        .at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>());
    auto request = identity;
    request["connectionKey"] = std::string("frame.corner.bottom-left");
    request["templateId"] = std::string("two-end-end-angle");
    request["participants"] = assemblyBindingParticipants(badCorner);
    request["parameters"] = ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0},
        {"planeRotation", 0.0}};
    EXPECT_THROW(invoke(scene, "PreviewProductAssemblyBinding", request), std::exception);

    setCorruptedNode("frame.junction.middle-left", true);
    const auto badTee = invoke(scene, "GetProductAssemblyConnections", identity);
    EXPECT_EQ("invalid", findNode(badTee, "frame.junction.middle-left")
        .at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>());

    auto withoutFrame = original;
    auto items = withoutFrame.at("items").To<VariantArray>();
    bool removedFrame = false;
    for (auto& value : items) {
        auto item = value.To<ObjectMap>();
        if (item.at("key").To<std::string>() != "frame.left.0001") continue;
        auto properties = item.at("properties").To<ObjectMap>();
        removedFrame = properties.erase("assemblyFrame.member") == 1;
        item["properties"] = std::move(properties);
        value = std::move(item);
        break;
    }
    ASSERT_TRUE(removedFrame);
    withoutFrame["items"] = std::move(items);
    ASSERT_TRUE(run->SetNeutralModel(withoutFrame));
    const auto missingFrame = invoke(scene, "GetProductAssemblyConnections", identity);
    EXPECT_EQ("unsupported", findNode(missingFrame, "frame.corner.bottom-left")
        .at("nodeGeometry").To<ObjectMap>()
        .at("nodeGeometryStatus").To<std::string>());
}

TEST(ProductAssemblyBindingSDO, AssignsFourCornersAndTwoMidspanJointsToFiveFrameBlanks)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto nativePreview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    });
    ASSERT_EQ(5u, nativePreview.at("items").To<VariantArray>().size());
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const auto productID = product.at("entityId").To<std::string>();
    const ObjectMap identity{{"productEntityId", productID},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto baselineRows = assemblyBindingPartRows(invoke(scene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{productID}}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(5u, baselineRows.size());
    const auto listed = invoke(scene, "GetProductAssemblyConnections", identity);
    std::map<std::string, ObjectMap> connections;
    for (const auto& value : listed.at("connections").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        connections.emplace(row.at("key").To<std::string>(), row);
    }
    ASSERT_EQ(6u, connections.size());
    const std::array<std::string, 6> jointKeys{
        "frame.corner.bottom-left", "frame.corner.bottom-right",
        "frame.corner.top-left", "frame.corner.top-right",
        "frame.junction.middle-left", "frame.junction.middle-right",
    };
    for (std::size_t index = 0; index < jointKeys.size(); ++index) {
        SCOPED_TRACE(jointKeys[index]);
        const auto& joint = connections.at(jointKeys[index]);
        const auto nodeGeometry = joint.at("nodeGeometry").To<ObjectMap>();
        ASSERT_EQ("verified", nodeGeometry.at("nodeGeometryStatus").To<std::string>())
            << nodeGeometry.at("detail").To<std::string>();
        ASSERT_TRUE(nodeGeometry.contains("axisAngleDeg"));
        EXPECT_NEAR(90.0, assemblyBindingNumber(nodeGeometry.at("axisAngleDeg")), 1.0e-6);
        const bool isTee = jointKeys[index].starts_with("frame.junction.");
        std::map<std::string, ObjectMap> anchors;
        for (const auto& value : joint.at("properties").To<ObjectMap>()
                .at("participantAnchors").To<VariantArray>()) {
            const auto declaration = value.To<ObjectMap>();
            anchors.emplace(declaration.at("itemKey").To<std::string>(),
                declaration.at("anchor").To<ObjectMap>());
        }
        VariantArray participants;
        for (const auto& value : joint.at("participants").To<VariantArray>()) {
            const auto member = value.To<ObjectMap>();
            const auto itemKey = member.at("itemKey").To<std::string>();
            const auto& declared = anchors.at(itemKey);
            const auto kind = declared.at("kind").To<std::string>();
            ObjectMap anchor = declared;
            std::string role;
            if (kind == "end") {
                role = isTee ? "branch" : participants.empty() ? "memberA" : "memberB";
            } else {
                ASSERT_EQ("side", kind);
                role = "host";
            }
            participants.emplace_back(ObjectMap{{"role", role},
                {"memberEntityId", member.at("memberEntityId")}, {"itemKey", itemKey},
                {"anchor", std::move(anchor)}});
        }
        ASSERT_EQ(2u, participants.size());
        const auto state = invoke(scene, "GetProductAssemblyBindings", identity);
        auto request = identity;
        request["sourceRevision"] = state.at("sourceRevision");
        request["revision"] = state.at("revision");
        request["connectionKey"] = jointKeys[index];
        request["templateId"] = std::string(isTee ? "two-end-middle" : "two-end-end-angle");
        request["participants"] = std::move(participants);
        request["parameters"] = isTee
            ? ObjectMap{{"interfaceMode", std::string("singleInsert")},
                {"intersectionAngle", 90.0}, {"fitGap", 0.2}}
            : ObjectMap{{"jointAngle", 90.0}, {"fitGap", 0.0}, {"planeRotation", 0.0}};
        const auto checked = invoke(scene, "PreviewProductAssemblyBinding", request);
        ASSERT_TRUE(checked.at("canApply").To<bool>())
            << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
        const auto measuredInterface = checked.at("interfaceMeasurement").To<ObjectMap>();
        ASSERT_EQ("measured", measuredInterface.at("status").To<std::string>());
        EXPECT_LE(measuredInterface.at("intersectionVolumeMm3").To<double>(), 1e-4);
        const auto fitChecks = checked.at("fitChecks").To<VariantArray>();
        ASSERT_EQ(1u, fitChecks.size());
        EXPECT_EQ(isTee ? "end-side-single-insert" : "end-end-miter",
            fitChecks.front().To<ObjectMap>().at("kind").To<std::string>());
        auto applyRequest = identity;
        applyRequest["candidateToken"] = checked.at("candidateToken");
        const auto applied = invoke(scene, "ApplyProductAssemblyBinding", applyRequest);
        ASSERT_TRUE(applied.at("applied").To<bool>());
        ASSERT_TRUE(applied.at("manufacturingUpdated").To<bool>());
        const auto updated = invoke(scene, "GetProductAssemblyBindings", identity);
        ASSERT_EQ(index + 1, updated.at("bindings").To<VariantArray>().size());
        const auto plan = updated.at("manufacturingPlan").To<ObjectMap>();
        EXPECT_EQ(index + 1, plan.at("selectedConnectionCount").To<std::uint64_t>());
        EXPECT_EQ(jointKeys.size() - index - 1,
            plan.at("unassignedConnectionCount").To<std::uint64_t>());
        EXPECT_EQ(0u, plan.at("missingPartCount").To<std::uint64_t>());
        EXPECT_EQ(5u, plan.at("members").To<VariantArray>().size());
    }
    const auto finalState = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto plan = finalState.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(6u, plan.at("selectedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unassignedConnectionCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("missingPartCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unmatchedBindingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unverifiedNodeGeometryCount").To<std::uint64_t>());
    EXPECT_EQ(5u, plan.at("verifiedCoordinateMappingCount").To<std::uint64_t>());
    EXPECT_EQ(0u, plan.at("unsupportedCoordinateMappingCount").To<std::uint64_t>());
    for (const auto& value : plan.at("members").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        const auto mapping = row.at("coordinateMapping").To<ObjectMap>();
        ASSERT_EQ("verified", mapping.at("status").To<std::string>())
            << row.at("itemKey").To<std::string>() << ": "
            << (mapping.contains("reason") ? mapping.at("reason").To<std::string>() : "");
        const auto forward = mapping.at("sourceToPart").To<VariantArray>();
        const auto inverse = mapping.at("partToSource").To<VariantArray>();
        ASSERT_EQ(16u, forward.size());
        ASSERT_EQ(16u, inverse.size());
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j) {
                double product = 0.0;
                for (std::size_t k = 0; k < 4; ++k)
                    product += forward[i * 4 + k].To<double>() * inverse[k * 4 + j].To<double>();
                EXPECT_NEAR(i == j ? 1.0 : 0.0, product, 1e-8);
            }
        const double sourceLength = mapping.at("sourceLength").To<double>();
        const double startX = mapping.at("sourceStartPartX").To<double>();
        const double endX = mapping.at("sourceEndPartX").To<double>();
        const double stockStartX = mapping.at("stockStartPartX").To<double>();
        const double stockEndX = mapping.at("stockEndPartX").To<double>();
        const double stockLength = mapping.at("stockLength").To<double>();
        const double startAllowance = mapping.at("stockStartAllowance").To<double>();
        const double endAllowance = mapping.at("stockEndAllowance").To<double>();
        const double sign = mapping.at("stationToPartXSign").To<double>();
        EXPECT_TRUE(mapping.at("stockIntervalDeclared").To<bool>());
        EXPECT_NEAR(sign * sourceLength, endX - startX, 0.05);
        EXPECT_NEAR(sign * stockLength, stockEndX - stockStartX, 0.05);
        EXPECT_NEAR(sign * startAllowance, startX - stockStartX, 0.05);
        EXPECT_NEAR(sign * endAllowance, stockEndX - endX, 0.05);
        EXPECT_NEAR(stockLength, sourceLength + startAllowance + endAllowance, 0.05);
        EXPECT_EQ(sign > 0 ? "start" : "end",
            mapping.at("sourceStartPartEnd").To<std::string>());
    }
    EXPECT_EQ("not-verified", plan.at("fitStatus").To<std::string>());
    EXPECT_EQ("not-ready", plan.at("readiness").To<std::string>());
    const auto finalRows = assemblyBindingPartRows(invoke(scene, "List",
        ObjectMap{{"includeManufacturingGeometry", true}})
        .at("tubeDesigner").To<ObjectMap>());
    ASSERT_EQ(baselineRows.size(), finalRows.size());
    for (const auto& [itemKey, baseline] : baselineRows) {
        ASSERT_TRUE(finalRows.contains(itemKey));
        const auto before = assemblyBindingRowShape(scene, baseline);
        const auto after = assemblyBindingRowShape(scene, finalRows.at(itemKey));
        // Both ends of the middle bar participate in T joints, so all five
        // physical blanks must lose material while retaining their identities.
        assemblyBindingCheckSubset(before, after);
    }
}

TEST(ProductAssemblyBindingSDO, MapsLogicalNodesInsideAReversedPhysicalFrameBlank)
{
    const auto projectID = iCAX::Data::GenerateNewUUID();
    Scene scene(projectID, projectID);
    const auto generated = invoke(scene, "GeneratePreview", ObjectMap{
        {"templateId", std::string("assembly-frame-lt")},
    }).at("tubeDesigner").To<ObjectMap>();
    const auto product = generated.at("product").To<ObjectMap>();
    const ObjectMap identity{{"productEntityId", product.at("entityId")},
        {"generationRunId", product.at("activeGenerationRunId")}};
    const auto runID = iCAX::Data::uuid::from_string(
        product.at("activeGenerationRunId").To<std::string>());
    ASSERT_TRUE(runID.has_value());
    const auto runEntity = scene.Database().GetEntity(*runID);
    ASSERT_TRUE(runEntity);
    const auto run = std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        runEntity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(run);

    constexpr auto reversedKey = "frame.right.0001";
    double reversedLength = 0.0;
    const auto reverseFace = [](const std::string& face) {
        if (face == "top") return std::string("bottom");
        if (face == "bottom") return std::string("top");
        return face;
    };
    const auto reverseItem = [&](ObjectMap& document) {
        auto items = document.at("items").To<VariantArray>();
        bool found = false;
        for (auto& value : items) {
            auto item = value.To<ObjectMap>();
            if (item.at("key").To<std::string>() != reversedKey) continue;
            auto properties = item.at("properties").To<ObjectMap>();
            auto frame = properties.at("assemblyFrame.member").To<ObjectMap>();
            const auto start = frame.at("start").To<VariantArray>();
            const auto end = frame.at("end").To<VariantArray>();
            reversedLength = frame.at("axisLength").To<double>();
            auto section = frame.at("sectionFrame").To<ObjectMap>();
            auto origin = section.at("originAtStart").To<VariantArray>();
            auto yAxis = section.at("yAxis").To<VariantArray>();
            auto datum = section.at("centerlineUV").To<VariantArray>();
            for (std::size_t i = 0; i < 3; ++i) {
                origin[i] = origin[i].To<double>() + end[i].To<double>() - start[i].To<double>();
                yAxis[i] = -yAxis[i].To<double>();
            }
            datum[1] = -datum[1].To<double>();
            section["originAtStart"] = std::move(origin);
            section["yAxis"] = std::move(yAxis);
            section["centerlineUV"] = std::move(datum);
            auto faces = section.at("faceNormals").To<ObjectMap>();
            std::swap(faces["top"], faces["bottom"]);
            section["faceNormals"] = std::move(faces);
            frame["sectionFrame"] = std::move(section);
            auto interval = frame.at("stockInterval").To<ObjectMap>();
            const double oldStart = interval.at("startStation").To<double>();
            const double oldEnd = interval.at("endStation").To<double>();
            interval["startStation"] = reversedLength - oldEnd;
            interval["endStation"] = reversedLength - oldStart;
            frame["stockInterval"] = std::move(interval);
            std::swap(frame["start"], frame["end"]);
            properties["assemblyFrame.member"] = std::move(frame);
            auto direction = properties.at("tubeDesigner.manufacturingStartToEnd")
                .To<VariantArray>();
            for (auto& coordinate : direction)
                coordinate = -coordinate.To<double>();
            properties["tubeDesigner.manufacturingStartToEnd"] = std::move(direction);
            item["properties"] = std::move(properties);
            value = std::move(item);
            found = true;
            break;
        }
        if (!found) throw std::runtime_error("Reversed fixture member missing");
        document["items"] = std::move(items);
    };
    auto document = run->GetNeutralModel();
    reverseItem(document);
    // Topology remains the same; only the member's local directed datum is
    // reversed. Its committed end and side stations must follow that datum.
    auto relationships = document.at("relationships").To<VariantArray>();
    for (auto& value : relationships) {
        auto relationship = value.To<ObjectMap>();
        if (relationship.at("kind").To<std::string>() != "assembly") continue;
        auto properties = relationship.at("properties").To<ObjectMap>();
        auto rows = properties.at("participantAnchors").To<VariantArray>();
        bool changed = false;
        for (auto& rowValue : rows) {
            auto row = rowValue.To<ObjectMap>();
            if (row.at("itemKey").To<std::string>() != reversedKey) continue;
            auto anchor = row.at("anchor").To<ObjectMap>();
            if (anchor.at("kind").To<std::string>() == "end") {
                const auto oldEnd = anchor.at("end").To<std::string>();
                const auto newEnd = oldEnd == "start" ? "end" : "start";
                anchor["end"] = std::string(newEnd);
                if (anchor.contains("approachFace"))
                    anchor["approachFace"] = reverseFace(
                        anchor.at("approachFace").To<std::string>());
                row["end"] = std::string(newEnd);
                row["localAxialStation"] = oldEnd == "start" ? reversedLength : 0.0;
            } else {
                const double station = reversedLength - anchor.at("station").To<double>();
                anchor["station"] = station;
                anchor["face"] = reverseFace(anchor.at("face").To<std::string>());
                row["face"] = reverseFace(row.at("face").To<std::string>());
                row["localAxialStation"] = station;
            }
            row["anchor"] = std::move(anchor);
            rowValue = std::move(row);
            changed = true;
        }
        if (changed) {
            properties["participantAnchors"] = std::move(rows);
            relationship["properties"] = std::move(properties);
            value = std::move(relationship);
        }
    }
    document["relationships"] = std::move(relationships);
    ASSERT_TRUE(run->SetNeutralModel(document));
    // This fixture's display and export representations are the same raw
    // stock. Cache the matching manufacturing document before disassembly;
    // otherwise that path evaluates the original template again and restores
    // the unreversed member datum.
    auto manufacturing = document;
    auto extensions = manufacturing.at("extensions").To<ObjectMap>();
    extensions["tubeDesigner.geometryPurpose"] = std::string("manufacturing");
    manufacturing["extensions"] = std::move(extensions);
    ASSERT_TRUE(run->SetManufacturingModel(manufacturing));
    const auto disassembled = invoke(scene, "DisassembleSelected",
        ObjectMap{{"productEntityIds", VariantArray{product.at("entityId")}}});
    EXPECT_EQ(5u, assemblyBindingPartRows(disassembled.at("tubeDesigner").To<ObjectMap>()).size());
    const auto listed = invoke(scene, "GetProductAssemblyBindings", identity);
    const auto plan = listed.at("manufacturingPlan").To<ObjectMap>();
    EXPECT_EQ(0u, plan.at("unverifiedNodeGeometryCount").To<std::uint64_t>());
    ObjectMap reversed;
    for (const auto& value : plan.at("members").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (row.at("itemKey").To<std::string>() == reversedKey) reversed = row;
    }
    ASSERT_FALSE(reversed.empty());
    const auto mapping = reversed.at("coordinateMapping").To<ObjectMap>();
    ASSERT_EQ("verified", mapping.at("status").To<std::string>())
        << (mapping.contains("reason") ? mapping.at("reason").To<std::string>() : "");
    EXPECT_EQ(-1.0, mapping.at("stationToPartXSign").To<double>());
    EXPECT_EQ("end", mapping.at("sourceStartPartEnd").To<std::string>());
    EXPECT_TRUE(mapping.at("stockIntervalDeclared").To<bool>());
    const double stockStartX = mapping.at("stockStartPartX").To<double>();
    const double stockEndX = mapping.at("stockEndPartX").To<double>();
    const double logicalStartX = mapping.at("sourceStartPartX").To<double>();
    const double logicalEndX = mapping.at("sourceEndPartX").To<double>();
    EXPECT_GT(stockStartX, logicalStartX);
    EXPECT_GT(logicalEndX, stockEndX);
    EXPECT_NEAR(mapping.at("stockStartAllowance").To<double>(),
        stockStartX - logicalStartX, 0.05);
    EXPECT_NEAR(mapping.at("stockEndAllowance").To<double>(),
        logicalEndX - stockEndX, 0.05);
    EXPECT_EQ("not-ready", plan.at("readiness").To<std::string>());

    const auto connections = invoke(scene, "GetProductAssemblyConnections", identity);
    ObjectMap corner;
    for (const auto& value : connections.at("connections").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (row.at("key").To<std::string>() == "frame.corner.bottom-right") {
            corner = row;
            break;
        }
    }
    ASSERT_FALSE(corner.empty());
    const auto declarations = corner.at("properties").To<ObjectMap>()
        .at("participantAnchors").To<VariantArray>();
    VariantArray participants;
    bool reversedEndSelected = false;
    for (const auto& value : corner.at("participants").To<VariantArray>()) {
        const auto member = value.To<ObjectMap>();
        const auto itemKey = member.at("itemKey").To<std::string>();
        ObjectMap anchor;
        for (const auto& declarationValue : declarations) {
            const auto declaration = declarationValue.To<ObjectMap>();
            if (declaration.at("itemKey").To<std::string>() == itemKey)
                anchor = declaration.at("anchor").To<ObjectMap>();
        }
        ASSERT_FALSE(anchor.empty());
        if (itemKey == reversedKey)
            reversedEndSelected = anchor.at("end").To<std::string>() == "end";
        participants.emplace_back(ObjectMap{
            {"role", std::string(participants.empty() ? "memberA" : "memberB")},
            {"memberEntityId", member.at("memberEntityId")}, {"itemKey", itemKey},
            {"anchor", std::move(anchor)}});
    }
    ASSERT_TRUE(reversedEndSelected);
    auto request = identity;
    request["sourceRevision"] = listed.at("sourceRevision");
    request["revision"] = listed.at("revision");
    request["connectionKey"] = std::string("frame.corner.bottom-right");
    request["templateId"] = std::string("two-end-end-angle");
    request["participants"] = std::move(participants);
    request["parameters"] = ObjectMap{{"jointAngle", 90.0}, {"fitGap", 2.0},
        {"planeRotation", 0.0}};
    const auto checked = invoke(scene, "PreviewProductAssemblyBinding", request);
    ASSERT_TRUE(checked.at("canApply").To<bool>())
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(checked);
    const auto measuredInterface = checked.at("interfaceMeasurement").To<ObjectMap>();
    ASSERT_EQ("measured", measuredInterface.at("status").To<std::string>());
    EXPECT_LE(measuredInterface.at("intersectionVolumeMm3").To<double>(), 1e-4);
    EXPECT_NEAR(measuredInterface.at("nearestDistanceMm").To<double>(), 2.0, 0.05);
    ASSERT_EQ(2u, checked.at("geometryChanges").To<VariantArray>().size());
    const auto workflow = checked.at("workflow").To<ObjectMap>();
    const auto operations = workflow.at("partOperations").To<VariantArray>();
    ASSERT_EQ(2u, operations.size());
    for (const auto& value : operations) {
        const auto operation = value.To<ObjectMap>();
        const auto anchor = operation.at("anchor").To<ObjectMap>();
        const auto placement = operation.at("placement").To<ObjectMap>();
        const double expected = anchor.at("stockAllowance").To<double>() + std::sqrt(2.0);
        EXPECT_NEAR(placement.at("trim").To<double>(), expected, 1e-6);
        if (operation.at("itemKey").To<std::string>() == reversedKey) {
            // The product node is its logical end; the reversed physical
            // blank puts that same node at the manufacturing start.
            EXPECT_EQ("start", mapping.at("sourceEndPartEnd").To<std::string>());
            EXPECT_EQ(mapping.at("sourceEndPartEnd").To<std::string>(),
                anchor.at("end").To<std::string>());
            EXPECT_EQ(anchor.at("end").To<std::string>(),
                placement.at("end").To<std::string>());
        }
    }
}
