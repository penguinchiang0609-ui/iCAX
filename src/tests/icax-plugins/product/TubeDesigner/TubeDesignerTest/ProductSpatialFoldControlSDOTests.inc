namespace {
gp_Pnt previewFramePoint(const Variant& value) {
    const auto coordinates=value.To<VariantArray>();
    if(coordinates.size()!=3)throw std::runtime_error("Preview frame point requires three coordinates");
    return gp_Pnt(finishedProductNumber(coordinates[0]),finishedProductNumber(coordinates[1]),finishedProductNumber(coordinates[2]));
}

std::string previewFrameRole(const std::string& key) {
    if(key.starts_with("outer_frame."))return "outer";
    if(key.starts_with("access_door.fixed_frame."))return "fixed";
    if(key.starts_with("access_door.leaf.frame."))return "leaf";
    return {};
}

void dumpFramePreviewMeshes(Scene& scene,const ObjectMap& preview,const std::string& face,const VariantArray& corners) {
    ObjectMap meshes;
    for(const auto& itemValue:preview.at("items").To<VariantArray>()) {
        const auto item=itemValue.To<ObjectMap>();
        const auto geometry=item.at("geometry").To<ObjectMap>();
        const auto url=geometry.at("url").To<std::string>();
        if(meshes.contains(url))continue;
        // The browser reference points to the derived FlatBuffer. Read the
        // exact native mesh named by that resource's source, at its version.
        const auto info=scene.Resources().GetInfo(url);
        if(!info||info->Source.empty())throw std::runtime_error("Native preview source resource is missing");
        const auto mesh=scene.Resources().Get<iCAX::GeometryData::CTriangleMeshResource>(info->Source,geometry.at("version").To<unsigned long long>());
        if(!mesh)throw std::runtime_error("Actual native template preview mesh is missing");
        VariantArray vertices,normals,triangles;
        for(const auto& point:mesh->Mesh.Vertices)vertices.emplace_back(VariantArray{point.X,point.Y,point.Z});
        for(const auto& normal:mesh->Mesh.Normals)normals.emplace_back(VariantArray{normal.X,normal.Y,normal.Z});
        for(const auto& triangle:mesh->Mesh.Triangles)triangles.emplace_back(VariantArray{
            static_cast<unsigned int>(triangle[0]),static_cast<unsigned int>(triangle[1]),static_cast<unsigned int>(triangle[2])});
        meshes.emplace(url,ObjectMap{{"vertices",vertices},{"normals",normals},{"triangles",triangles}});
    }
    const auto directory=std::filesystem::current_path()/"output/tests/preview-frame-corners";
    std::filesystem::create_directories(directory);
    std::ofstream(directory/(face+".json"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"geometrySource",std::string("native-generated-display-triangle-mesh")},{"preview",preview},{"meshes",meshes},{"cornerChecks",corners}});
}
}

TEST(ProductSpatialFoldControlSDO, RealDisplayCornersAreCompleteComplementaryAndKeepHollowProfiles)
{
    struct FrameMember { std::string key,role;TopoDS_Shape shape;gp_Pnt start,end;double width,wall; };
    for(const auto* face:{"single","two","three","five"}) {
        SCOPED_TRACE(face);
        Scene scene;
        const ObjectMap supplied{{"faceType",std::string(face)},{"width",1200.0},{"height",1800.0},
            {"sideWidth",600.0},{"leftWidth",600.0},{"rightWidth",600.0},{"depth",600.0},
            {"accessDoorEnabled",true},{"frameLayout",std::string("four_sides")},
            {"frameManufacturingMode",std::string(std::string(face)=="two"||std::string(face)=="three"?"spatial_v_notch":"plane_v_notch")},
            {"outerFrameGrooveTool",std::string("system:v-notch-sharp")},{"outerFrameVGrooveBottomStrategy",std::string("rounded")},
            {"doorFrameJoinType",std::string("v_groove_90:tool_library")},{"doorFrameGrooveTool",std::string("system:v-notch-sharp")},
            {"doorFrameVGrooveBottomStrategy",std::string("rounded")},
            {"doorLeafFrameJoinType",std::string("v_groove_90:tool_library")},{"doorLeafFrameGrooveTool",std::string("system:v-notch-sharp")},
            {"doorLeafFrameVGrooveBottomStrategy",std::string("rounded")}};
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied);
        const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("single-face-security-window")},{"parameters",supplied}});
        const auto parameters=preview.at("parameters").To<ObjectMap>();
        for(const auto& [key,value]:supplied)EXPECT_EQ(parameters.at(key),value);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied),before);
        const auto repeated=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("single-face-security-window")},{"parameters",parameters}});
        EXPECT_EQ(repeated.at("parameters"),Variant(parameters));
        EXPECT_EQ(repeated.at("items"),preview.at("items"));
        auto request=parameters;
        request["templateId"]=std::string("single-face-security-window");
        const auto product=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
        const auto document=disassemblyCountRun(scene,product)->GetNeutralModel();
        const auto model=iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(document));
        std::vector<std::string> frameRoots;
        for(const auto& item:model.Items)if(!previewFrameRole(item.Key).empty())
            frameRoots.push_back(item.Representations.at("result"));
        const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,frameRoots);
        std::vector<FrameMember> members;
        Bnd_Box outerBounds;
        for(const auto& item:model.Items) {
            const auto role=previewFrameRole(item.Key);
            if(role.empty())continue;
            const auto member=item.Properties.at("assemblyFrame.member").To<ObjectMap>();
            const auto profile=item.Properties.at("tubeDesigner.profile").To<ObjectMap>();
            const auto shape=evaluated.At(item.Representations.at("result"));
            EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid())<<item.Key;
            int solids=0;for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next())++solids;
            EXPECT_EQ(solids,1)<<item.Key;
            const auto start=previewFramePoint(member.at("start")),end=previewFramePoint(member.at("end"));
            const auto midpoint=start.Translated(gp_Vec(start,end)*.5);
            BRepClass3d_SolidClassifier hollow(shape,midpoint,1.e-7);
            EXPECT_EQ(hollow.State(),TopAbs_OUT)<<item.Key<<" must retain its original profile void";
            members.push_back({item.Key,role,shape,start,end,finishedProductNumber(profile.at("width")),finishedProductNumber(profile.at("wallThickness"))});
            if(role=="outer")BRepBndLib::Add(shape,outerBounds,false);
        }
        ASSERT_FALSE(members.empty());
        double minX,minY,minZ,maxX,maxY,maxZ;
        outerBounds.Get(minX,minY,minZ,maxX,maxY,maxZ);
        // The single-face datum is its lower-left corner; multi-face frames
        // use the front-face centre and retain that existing design datum.
        const bool single=std::string(face)=="single";
        EXPECT_NEAR(minX,single?0.0:-600.0,1.e-5);EXPECT_NEAR(maxX,single?1200.0:600.0,1.e-5);
        EXPECT_NEAR(minZ,0.0,1.e-5);EXPECT_NEAR(maxZ,1800.0,1.e-5);
        int contacts=0,fullCorners=0;
        VariantArray corners;
        for(std::size_t a=0;a<members.size();++a)for(std::size_t b=a+1;b<members.size();++b) {
            const auto& first=members[a];const auto& second=members[b];
            if(first.role!=second.role)continue;
            for(const auto& joint:{first.start,first.end})for(const auto& mate:{second.start,second.end}) {
                if(joint.Distance(mate)>1.e-6)continue;
                const auto firstFar=joint.Distance(first.start)<1.e-6?first.end:first.start;
                const auto secondFar=mate.Distance(second.start)<1.e-6?second.end:second.start;
                gp_Vec d1(joint,firstFar),d2(mate,secondFar);d1.Normalize();d2.Normalize();
                if(std::abs(d1.Dot(d2))>1.e-7)continue;
                SCOPED_TRACE(first.key+" / "+second.key);
                BRepAlgoAPI_Common common;
                common.SetNonDestructive(true);common.SetRunParallel(false);common.SetFuzzyValue(1.e-7);
                common.SetToFillHistory(false);
                NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(first.shape);tools.Append(second.shape);
                common.SetArguments(arguments);common.SetTools(tools);common.Build();
                ASSERT_TRUE(common.IsDone());
                GProp_GProps overlap;
                if(!common.Shape().IsNull())BRepGProp::VolumeProperties(common.Shape(),overlap);
                const auto volume=overlap.Mass();
                EXPECT_NEAR(volume,0.0,1.e-5)<<"Display corner shells must have complementary end planes";
                BRepExtrema_DistShapeShape contact(first.shape,second.shape);
                ASSERT_TRUE(contact.IsDone());
                EXPECT_NEAR(contact.Value(),0.0,1.e-5)<<"Display corner must meet";
                ++contacts;
                // Planar two-member corners fill the outer diagonal. Vertical
                // posts in multi-face products deliberately stop at rail skins.
                const bool planar=first.role!="outer"||single||
                    (std::abs(d1.Z())<1.e-7&&std::abs(d2.Z())<1.e-7);
                ObjectMap record{{"first",first.key},{"second",second.key},{"commonVolume",volume},{"distance",contact.Value()}};
                if(planar&&std::abs(first.width-second.width)<1.e-7) {
                    const auto nearSkin=first.width*.5-first.wall*.5;
                    const auto point=joint.Translated(-(d1+d2)*nearSkin+d1*.05);
                    BRepClass3d_SolidClassifier one(first.shape,point,1.e-7),two(second.shape,point,1.e-7);
                    const bool filled=one.State()==TopAbs_IN||one.State()==TopAbs_ON||two.State()==TopAbs_IN||two.State()==TopAbs_ON;
                    EXPECT_TRUE(filled)<<"Outer corner must extend past the logical centre-line endpoint";
                    record["outerCornerWitness"]=VariantArray{point.X(),point.Y(),point.Z()};record["filled"]=filled;
                    ++fullCorners;
                }
                corners.emplace_back(record);
            }
        }
        EXPECT_GE(contacts,12);EXPECT_GE(fullCorners,8);
        dumpFramePreviewMeshes(scene,preview,face,corners);
    }
}

TEST(ProductSpatialFoldControlSDO, FramePreviewPrototypesInvalidateWhenSectionOrLengthChanges)
{
    Scene scene;
    const auto original=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"parameters",ObjectMap{{"faceType",std::string("single")},{"accessDoorEnabled",false}}}});
    const auto parameters=original.at("parameters").To<ObjectMap>();
    const auto frameItem=[](const ObjectMap& preview) {
        for(const auto& value:preview.at("items").To<VariantArray>()) {
            const auto item=value.To<ObjectMap>();
            if(item.at("key").To<std::string>()=="outer_frame.left.0001")return item;
        }
        throw std::runtime_error("Native frame preview is missing the left frame");
    };
    const auto before=frameItem(original);
    for(const auto* key:{"frameWidth","height"}) {
        SCOPED_TRACE(key);
        auto changed=parameters;
        changed[key]=finishedProductNumber(parameters.at(key))+10.0;
        const auto edited=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("single-face-security-window")},{"parameters",changed}});
        EXPECT_EQ(edited.at("parameters"),Variant(changed));
        const auto after=frameItem(edited);
        EXPECT_NE(after.at("geometry").To<ObjectMap>().at("url"),before.at("geometry").To<ObjectMap>().at("url"));
        EXPECT_NE(after.at("bounds"),before.at("bounds"));
        const auto repeated=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("single-face-security-window")},{"parameters",changed}});
        EXPECT_EQ(repeated.at("items"),edited.at("items"));
        const auto reverted=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
            {"templateId",std::string("single-face-security-window")},{"parameters",parameters}});
        EXPECT_EQ(reverted.at("items"),original.at("items"));
    }
}

TEST(ProductSpatialFoldControlSDO, OuterFrameStructureAndBendTypeStayIndependentInNormalizedInputs)
{
    Scene scene;
    struct BendChoice { const char* tool; const char* bottom; };
    for (const auto* face : {"single", "two", "three", "five"}) {
        const auto initial = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")},
            {"parameters", ObjectMap{{"faceType", std::string(face)}, {"accessDoorEnabled", false}}}});
        const auto normalized = initial.at("parameters").To<ObjectMap>();
        for (const auto* mode : {"segment_weld", "plane_v_notch", "spatial_v_notch"}) {
            if (std::string(mode) == "spatial_v_notch" && std::string(face) != "two" && std::string(face) != "three") continue;
            for (const auto& bend : {BendChoice{"system:v-notch-sharp", "sharp"},
                    BendChoice{"system:v-notch-sharp", "rounded"}, BendChoice{"system:edge-arc-groove", "sharp"}}) {
                SCOPED_TRACE(std::string(face)+"/"+mode+"/"+bend.tool+"/"+bend.bottom);
                auto inputs = normalized;
                inputs["frameManufacturingMode"] = std::string(mode);
                inputs["outerFrameGrooveTool"] = std::string(bend.tool);
                inputs["outerFrameVGrooveBottomStrategy"] = std::string(bend.bottom);
                inputs["outerFrameVGrooveRoundRadius"] = 3.0;
                inputs["outerFrameBendKFactor"] = .62;
                inputs["outerFrameFoldBridge"] = .8;
                inputs["outerFrameVGrooveMaleFemale"] = false;
                const auto before = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(inputs);
                const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
                    {"templateId", std::string("single-face-security-window")}, {"parameters", inputs}});
                EXPECT_EQ(preview.at("parameters"), Variant(inputs));
                EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(inputs), before);
                if (std::string(face) == "five" || preview.at("items") != initial.at("items")) {
                    const auto directory = std::filesystem::current_path()/"output/tests/spatial-fold-validation";
                    std::filesystem::create_directories(directory);
                    const auto toolName = std::string(bend.tool) == "system:edge-arc-groove" ? "edge" : "v";
                    std::ofstream(directory/(std::string("structure-display-diff-")+face+"-"+mode+"-"+toolName+"-"+bend.bottom+".json"))
                        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
                            {"initial", initial}, {"preview", preview}, {"inputs", inputs}});
                }
                EXPECT_EQ(preview.at("items"), initial.at("items"));
                EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview.at("items")),
                    iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(initial.at("items")));
                const auto echoed = preview.at("parameters").To<ObjectMap>();
                EXPECT_FALSE(echoed.contains("outerFrameStructure"));
                EXPECT_FALSE(echoed.contains("outerFrameBendType"));
                EXPECT_FALSE(echoed.contains("outerFrameConnection"));
                EXPECT_FALSE(echoed.contains("outerFrameFoldLayout"));
            }
        }
    }
}

TEST(ProductSpatialFoldControlSDO, CurrentWindowTemplatePreviewsBothSpatialFacesWithoutChangingNormalizedInputs)
{
    Scene scene;
    for (const auto* face : {"two", "three"}) {
        for (const auto* side : {"right", "left"}) {
            SCOPED_TRACE(std::string(face) + "/" + side);
            const ObjectMap supplied{{"faceType", std::string(face)}, {"sidePosition", std::string(side)},
                {"width", 1200.0}, {"sideWidth", 600.0}, {"height", 1800.0},
                {"frameManufacturingMode", std::string("spatial_v_notch")},
                {"outerFrameGrooveTool", std::string("system:edge-arc-groove")},
                {"foldedPostJoint", std::string("tabs")}, {"accessDoorEnabled", false}};
            const auto unchanged = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied);
            const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
                {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}});
            ASSERT_FALSE(preview.at("items").To<VariantArray>().empty());
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied), unchanged);
            const auto normalized = preview.at("parameters").To<ObjectMap>();
            EXPECT_EQ(normalized.at("frameManufacturingMode").To<std::string>(), "spatial_v_notch");
            EXPECT_EQ(normalized.at("outerFrameGrooveTool").To<std::string>(), "system:edge-arc-groove");
            EXPECT_EQ(normalized.at("foldedPostJoint").To<std::string>(), "tabs");
            EXPECT_FALSE(normalized.contains("outerFrameConnection"));
            EXPECT_FALSE(normalized.contains("outerFrameFoldLayout"));
            const auto repeated = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
                {"templateId", std::string("single-face-security-window")}, {"parameters", normalized}});
            EXPECT_EQ(repeated.at("parameters"), preview.at("parameters"));
            EXPECT_EQ(repeated.at("items"), preview.at("items"));

            auto plane = normalized;
            plane["frameManufacturingMode"] = std::string("plane_v_notch");
            const auto separate = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
                {"templateId", std::string("single-face-security-window")}, {"parameters", plane}});
            EXPECT_EQ(separate.at("parameters"), Variant(plane));
            EXPECT_EQ(separate.at("items"), preview.at("items"));
            // The native preview contract exposes display items (including
            // geometry, placement and bounds), not a neutral model document.
            EXPECT_EQ(separate.at("material"), preview.at("material"));
        }
    }
}

TEST(ProductSpatialFoldControlSDO, IndependentFoldParametersPreviewEveryFaceAndEchoExactly)
{
    Scene scene;
    for (const auto* face : {"single", "two", "three", "five"}) {
        SCOPED_TRACE(face);
        const ObjectMap supplied{{"faceType", std::string(face)}, {"accessDoorEnabled", true},
            {"frameManufacturingMode", std::string("plane_v_notch")},
            {"outerFrameGrooveTool", std::string("system:v-notch-sharp")},
            {"outerFrameVGrooveBottomStrategy", std::string("rounded")},
            {"outerFrameVGrooveRoundRadius", 2.0}, {"outerFrameFoldBridge", 1.0},
            {"outerFrameBendKFactor", .8}, {"outerFrameVGrooveMaleFemale", true},
            {"doorFrameJoinType", std::string("v_groove_90:tool_library")},
            {"doorFrameGrooveTool", std::string("system:v-notch-sharp")},
            {"doorFrameVGrooveBottomStrategy", std::string("rounded")},
            {"doorFrameVGrooveRoundRadius", 3.0}, {"doorFrameFoldBridge", .8},
            {"doorFrameBendKFactor", .5}, {"doorFrameVGrooveMaleFemale", false},
            {"doorLeafFrameJoinType", std::string("v_groove_90:tool_library")},
            {"doorLeafFrameGrooveTool", std::string("system:v-notch-sharp")},
            {"doorLeafFrameVGrooveBottomStrategy", std::string("rounded")},
            {"doorLeafFrameVGrooveRoundRadius", 1.0}, {"doorLeafFrameFoldBridge", .6},
            {"doorLeafFrameBendKFactor", .2}, {"doorLeafFrameVGrooveMaleFemale", true}};
        const auto unchanged = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied);
        const auto first = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}});
        ASSERT_FALSE(first.at("items").To<VariantArray>().empty());
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(supplied), unchanged);
        const auto normalized = first.at("parameters").To<ObjectMap>();
        for (const auto& [key, value] : supplied) EXPECT_EQ(normalized.at(key), value);
        const auto second = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")}, {"parameters", normalized}});
        EXPECT_EQ(second.at("parameters"), Variant(normalized));
        EXPECT_EQ(second.at("items"), first.at("items"));
        auto changed = normalized;
        for (const auto* prefix : {"outerFrame", "doorFrame", "doorLeafFrame"}) {
            changed[std::string(prefix)+"VGrooveRoundRadius"] = 4.0;
            changed[std::string(prefix)+"FoldBridge"] = .5;
            changed[std::string(prefix)+"VGrooveMaleFemale"] = false;
            changed[std::string(prefix)+"BendKFactor"] = .62;
            changed[std::string(prefix)+"VGrooveBottomStrategy"] = std::string("sharp");
        }
        const auto edited = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
            {"templateId", std::string("single-face-security-window")}, {"parameters", changed}});
        EXPECT_EQ(edited.at("parameters"), Variant(changed));
        EXPECT_EQ(edited.at("items"), first.at("items"));
    }
}

TEST(ProductSpatialFoldControlSDO, SingleFaceRoundedVUsesEachFramesActualRadiusAndBridge)
{
    Scene scene;
    const ObjectMap supplied{{"faceType", std::string("single")}, {"accessDoorEnabled", true},
        {"frameManufacturingMode", std::string("plane_v_notch")},
        {"outerFrameGrooveTool", std::string("system:v-notch-sharp")},
        {"outerFrameVGrooveBottomStrategy", std::string("rounded")},
        {"outerFrameVGrooveRoundRadius", 2.0}, {"outerFrameFoldBridge", 1.0},
        {"outerFrameBendKFactor", .8}, {"outerFrameVGrooveMaleFemale", true},
        {"doorFrameJoinType", std::string("v_groove_90:tool_library")},
        {"doorFrameGrooveTool", std::string("system:v-notch-sharp")},
        {"doorFrameVGrooveBottomStrategy", std::string("rounded")},
        {"doorFrameVGrooveRoundRadius", 3.0}, {"doorFrameFoldBridge", .8},
        {"doorFrameBendKFactor", .5}, {"doorFrameVGrooveMaleFemale", false},
        {"doorLeafFrameJoinType", std::string("v_groove_90:tool_library")},
        {"doorLeafFrameGrooveTool", std::string("system:v-notch-sharp")},
        {"doorLeafFrameVGrooveBottomStrategy", std::string("rounded")},
        {"doorLeafFrameVGrooveRoundRadius", 1.0}, {"doorLeafFrameFoldBridge", .6},
        {"doorLeafFrameBendKFactor", .2}, {"doorLeafFrameVGrooveMaleFemale", true},
        {"horizontalEndConnection", std::string("insert")},
        {"verticalEndConnection", std::string("insert")}};
    const auto parameters = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}})
        .at("parameters").To<ObjectMap>();
    auto request = parameters;
    request["templateId"] = std::string("single-face-security-window");
    const auto product = invoke(scene, "GeneratePreview", request)
        .at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    const auto run = disassemblyCountRun(scene, product);
    const auto display = run->GetNeutralModel();
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    EXPECT_EQ(run->GetNeutralModel(), display);
    EXPECT_EQ(run->GetManufacturingParameters(), parameters);
    const auto native = run->GetManufacturingModel().at("extensions").To<ObjectMap>()
        .at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    int cuts=0;
    for (const auto& value : native.at("plan").To<ObjectMap>().at("operations").To<VariantArray>()) {
        const auto operation=value.To<ObjectMap>();
        if (!operation.contains("requestFeature") || operation.at("toolRef").To<ObjectMap>().at("id").To<std::string>() != "v-notch-sharp") continue;
        ++cuts;
        const auto selected=operation.at("requestFeature").To<ObjectMap>().at("toolParameters").To<ObjectMap>();
        const auto identity=operation.at("instanceId").To<std::string>();
        const bool outer=identity.starts_with("outer_frame.");
        const bool fixed=identity.starts_with("access_door.fixed_frame.");
        EXPECT_EQ(selected.at("bottomStrategy").To<std::string>(), "rounded");
        EXPECT_DOUBLE_EQ(finishedProductNumber(selected.at("roundRadius")), outer ? 2. : fixed ? 3. : 1.);
        EXPECT_DOUBLE_EQ(finishedProductNumber(selected.at("leaveBottom")), outer ? 1. : fixed ? .8 : .6);
        EXPECT_EQ(selected.at("maleFemale").To<bool>(), !fixed);
        EXPECT_FALSE(selected.at("bendCompensation").To<bool>());
        EXPECT_EQ(selected.at("bottomReference").To<std::string>(), "outer");
    }
    EXPECT_EQ(cuts, 12);
    for (const auto& [key, value] : native.at("stocks").To<ObjectMap>()) {
        if (key.find(".continuous.") == std::string::npos) continue;
        const auto shape=continuousRoundTabsShape(scene, value.To<ObjectMap>().at("processedGeometry").To<ObjectMap>());
        EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    }
}

TEST(ProductSpatialFoldControlSDO, ThreeFaceEdgeArcUsesRealRootArcAndIndependentFrameSettings)
{
    Scene scene;
    const ObjectMap supplied{{"faceType", std::string("three")},
        {"width", 1200.0}, {"sideWidth", 600.0}, {"height", 1800.0},
        {"accessDoorEnabled", true}, {"frameManufacturingMode", std::string("spatial_v_notch")},
        {"outerFrameGrooveTool", std::string("system:edge-arc-groove")},
        {"outerFrameBendKFactor", 0.62}, {"outerFrameFoldBridge", 1.0},
        {"doorFrameJoinType", std::string("v_groove_90:tool_library")},
        {"doorFrameGrooveTool", std::string("system:edge-arc-groove")},
        {"doorFrameBendKFactor", 0.4}, {"doorFrameFoldBridge", 0.8},
        {"doorLeafFrameJoinType", std::string("v_groove_90:tool_library")},
        {"doorLeafFrameGrooveTool", std::string("system:edge-arc-groove")},
        {"doorLeafFrameBendKFactor", 0.2}, {"doorLeafFrameFoldBridge", 0.6},
        {"foldedPostJoint", std::string("tabs")},
        {"horizontalEndConnection", std::string("insert")},
        {"verticalEndConnection", std::string("insert")}, {"assemblyClearance", 0.1}};
    const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}});
    const auto parameters = preview.at("parameters").To<ObjectMap>();
    auto request = parameters;
    request["templateId"] = std::string("single-face-security-window");
    const auto product = invoke(scene, "GeneratePreview", request)
        .at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    const auto run = disassemblyCountRun(scene, product);
    const auto display = run->GetNeutralModel();
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    EXPECT_EQ(run->GetNeutralModel(), display);
    EXPECT_EQ(run->GetManufacturingParameters(), parameters);
    const auto extensions = run->GetManufacturingModel().at("extensions").To<ObjectMap>();
    const auto native = extensions.at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto stock = native.at("stocks").To<ObjectMap>().at("outer_frame.spatial.continuous").To<ObjectMap>();
    const auto finished = continuousRoundTabsShape(scene, stock.at("processedGeometry").To<ObjectMap>());
    EXPECT_TRUE(BRepCheck_Analyzer(finished).IsValid());
    EXPECT_NEAR(finishedProductNumber(stock.at("request").To<ObjectMap>().at("length")),
        8096.+8.*((37.*std::acos(-1.)/2.)-38.+std::acos(-1.)/2.*0.62*1.2), 1e-5);
    for (const auto& value : extensions.at("tubeDesigner.assemblyMaterialPlan").To<ObjectMap>().at("stocks").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (!row.contains("targetSpanCheck")) continue;
        EXPECT_EQ(row.at("targetSpanCheck").To<ObjectMap>().at("status").To<std::string>(), "pass");
        EXPECT_LT(finishedProductNumber(row.at("targetSpanCheck").To<ObjectMap>().at("maximumDeviation")), 1e-4);
    }
    VariantArray witnesses;
    for (const auto& value : native.at("plan").To<ObjectMap>().at("forming").To<VariantArray>()) {
        const auto fold = value.To<ObjectMap>();
        EXPECT_EQ(fold.at("kind").To<std::string>(), "distributed-root-arc");
        EXPECT_EQ(fold.at("hingePointKind").To<std::string>(), "equivalent-final-pose-centre");
        const auto arc = fold.at("rootArc").To<ObjectMap>();
        const auto rawEnd = arc.at("flatRootEnd").To<VariantArray>();
        const auto formedEnd = arc.at("formedRootEnd").To<VariantArray>();
        const auto interval = arc.at("flatRootInterval").To<VariantArray>();
        const double compensation = finishedProductNumber(arc.at("compensationLength"));
        EXPECT_NEAR(finishedProductNumber(interval[1])-finishedProductNumber(interval[0]),
            finishedProductNumber(arc.at("rootArcLength"))+compensation, 1e-6);
        const auto stockId = fold.at("stockId").To<std::string>();
        const double factor = stockId.starts_with("outer_frame.") ? .62
            : stockId.starts_with("access_door.fixed_frame.") ? .4 : .2;
        for (const auto& operationValue : native.at("plan").To<ObjectMap>().at("operations").To<VariantArray>()) {
            const auto operation = operationValue.To<ObjectMap>();
            if (operation.at("instanceId") != fold.at("instanceId") || !operation.contains("requestFeature")) continue;
            const auto selected = operation.at("requestFeature").To<ObjectMap>().at("toolParameters").To<ObjectMap>();
            EXPECT_DOUBLE_EQ(finishedProductNumber(selected.at("kFactor")), factor);
            EXPECT_TRUE(selected.at("bendCompensation").To<bool>());
            EXPECT_NEAR(compensation, std::acos(-1.)/2.*factor*finishedProductNumber(selected.at("wallThickness")), 1e-6);
        }
        const auto transform = fold.at("targetTransform").To<VariantArray>();
        const auto coordinate = [&](const int row) {
            return finishedProductNumber(transform[row*4])*finishedProductNumber(rawEnd[0])
                +finishedProductNumber(transform[row*4+1])*finishedProductNumber(rawEnd[1])
                +finishedProductNumber(transform[row*4+2])*finishedProductNumber(rawEnd[2])
                +finishedProductNumber(transform[row*4+3]);
        };
        for (int index=0; index<3; ++index) EXPECT_NEAR(coordinate(index), finishedProductNumber(formedEnd[index]), 1e-6);
        witnesses.emplace_back(ObjectMap{{"station", fold.at("station")},
            {"stockId", stockId}, {"rootArc", arc}, {"targetTransform", transform}});
    }
    ASSERT_EQ(witnesses.size(), 16u);
    const auto directory = std::filesystem::current_path()/"output/tests/spatial-fold-validation";
    std::filesystem::create_directories(directory);
    std::ofstream(directory/"native-edge-arc-final-pose-proof.json")
        << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
            {"passed", !::testing::Test::HasFailure()}, {"parameters", parameters},
            {"finalPoseChecks", witnesses}});
}

TEST(ProductSpatialFoldControlSDO, ThreeFaceSharpVActualCutsKeepEachDeclaredHingeWall)
{
    Scene scene;
    const ObjectMap supplied{{"faceType", std::string("three")},
        {"width", 1200.0}, {"sideWidth", 600.0}, {"height", 1800.0},
        {"accessDoorEnabled", true}, {"frameManufacturingMode", std::string("spatial_v_notch")},
        {"outerFrameGrooveTool", std::string("system:v-notch-sharp")},
        {"foldedPostJoint", std::string("tabs")},
        {"horizontalEndConnection", std::string("insert")},
        {"verticalEndConnection", std::string("insert")}, {"assemblyClearance", 0.1}};
    const auto preview = invoke(scene, "GenerateProductTemplatePreview", ObjectMap{
        {"templateId", std::string("single-face-security-window")}, {"parameters", supplied}});
    const auto parameters = preview.at("parameters").To<ObjectMap>();
    auto request = parameters;
    request["templateId"] = std::string("single-face-security-window");
    const auto product = invoke(scene, "GeneratePreview", request)
        .at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    const auto run = disassemblyCountRun(scene, product);
    const auto display = run->GetNeutralModel();
    invoke(scene, "DisassembleSelected", disassemblyCountRequest(product));
    EXPECT_EQ(run->GetNeutralModel(), display);
    EXPECT_EQ(run->GetManufacturingParameters(), parameters);
    const auto extensions = run->GetManufacturingModel().at("extensions").To<ObjectMap>();
    const auto native = extensions.at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    ObjectMap materialPlan;
    for (const auto& value : extensions.at("tubeDesigner.assemblyMaterialPlan").To<ObjectMap>()
            .at("stocks").To<VariantArray>()) {
        const auto row = value.To<ObjectMap>();
        if (row.at("stockId").To<std::string>() == "outer_frame.spatial.continuous") materialPlan = row;
    }
    ASSERT_FALSE(materialPlan.empty());
    EXPECT_EQ(materialPlan.at("formingMotionCheck").To<ObjectMap>().at("status").To<std::string>(), "pass");
    EXPECT_TRUE(materialPlan.at("formingMotionCheck").To<ObjectMap>().at("includesClosureSeam").To<bool>());
    const auto stock = native.at("stocks").To<ObjectMap>().at("outer_frame.spatial.continuous").To<ObjectMap>();
    const auto finished = continuousRoundTabsShape(scene, stock.at("processedGeometry").To<ObjectMap>());
    EXPECT_TRUE(BRepCheck_Analyzer(finished).IsValid());
    EXPECT_DOUBLE_EQ(finishedProductNumber(stock.at("request").To<ObjectMap>().at("length")), 8384.0);
    VariantArray probes;
    for (const auto& value : native.at("plan").To<ObjectMap>().at("forming").To<VariantArray>()) {
        const auto fold = value.To<ObjectMap>();
        if (fold.at("stockId").To<std::string>() != "outer_frame.spatial.continuous") continue;
        SCOPED_TRACE(fold.at("instanceId").To<std::string>());
        const auto frame = fold.at("frame").To<ObjectMap>();
        const auto origin = frame.at("origin").To<VariantArray>();
        const auto y = frame.at("yAxis").To<VariantArray>();
        const auto z = frame.at("zAxis").To<VariantArray>();
        const auto point = [&](const double localY, const double localZ) {
            return gp_Pnt(finishedProductNumber(origin[0]) + localY*finishedProductNumber(y[0]) + localZ*finishedProductNumber(z[0]),
                finishedProductNumber(origin[1]) + localY*finishedProductNumber(y[1]) + localZ*finishedProductNumber(z[1]),
                finishedProductNumber(origin[2]) + localY*finishedProductNumber(y[2]) + localZ*finishedProductNumber(z[2]));
        };
        // Classify the saved final BRep, including all later grid apertures.
        // Only the root wall can remain at the bend station, regardless of roll.
        EXPECT_EQ(BRepClass3d_SolidClassifier(finished, point(0, -18.4), 1e-5).State(), TopAbs_IN);
        EXPECT_EQ(BRepClass3d_SolidClassifier(finished, point(0, 18.4), 1e-5).State(), TopAbs_OUT);
        EXPECT_EQ(BRepClass3d_SolidClassifier(finished, point(-18.4, 0), 1e-5).State(), TopAbs_OUT);
        EXPECT_EQ(BRepClass3d_SolidClassifier(finished, point(18.4, 0), 1e-5).State(), TopAbs_OUT);
        probes.emplace_back(ObjectMap{{"station", fold.at("station")}, {"frame", frame},
            {"hingePoint", fold.at("hingePoint")}, {"axis", fold.at("axis")}});
    }
    EXPECT_EQ(probes.size(), 8u);
    const std::array<double, 8> expectedStations{{8085., 6287., 5689., 4491., 3893., 2095., 299., 1497.}};
    for (std::size_t index = 0; index < probes.size(); ++index)
        EXPECT_DOUBLE_EQ(finishedProductNumber(probes[index].To<ObjectMap>().at("station")), expectedStations[index]);
    VariantArray seamWitnesses;
    // Independent swept-shell intersections from the declared tail-first order.
    // Classify both inverse images against the complete processed BRep, so a
    // hollow section or a later grid aperture cannot create a false collision.
    const std::array<std::array<double, 7>, 3> witnesses{{
        {{83.0, 1.100830999, -18.287882146, 0.0, 8382.898945860, 18.287378352, 0.0}},
        {{87.5, 0.392868240, -6.524204960, -18.4, 8383.607345268, 6.524196799, -18.4}},
        {{87.5, 0.275007768, -9.606943472, -18.58, 8383.355129808, 3.449533373, -18.58}}
    }};
    for (const auto& witness : witnesses) {
        const auto first = BRepClass3d_SolidClassifier(finished,
            gp_Pnt(witness[1], witness[2], witness[3]), 1e-5).State();
        const auto last = BRepClass3d_SolidClassifier(finished,
            gp_Pnt(witness[4], witness[5], witness[6]), 1e-5).State();
        seamWitnesses.emplace_back(ObjectMap{{"angle", witness[0]},
            {"firstRawPoint", VariantArray{witness[1], witness[2], witness[3]}},
            {"lastRawPoint", VariantArray{witness[4], witness[5], witness[6]}},
            {"firstIsMaterial", first == TopAbs_IN}, {"lastIsMaterial", last == TopAbs_IN}});
    }
    const auto directory = std::filesystem::current_path()/"output/tests/spatial-fold-validation";
    std::filesystem::create_directories(directory);
    VariantArray edges;
    for (TopExp_Explorer edge(finished, TopAbs_EDGE); edge.More(); edge.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
        const double start = curve.FirstParameter(), end = curve.LastParameter();
        if (!std::isfinite(start) || !std::isfinite(end)) continue;
        VariantArray points;
        const int segments = curve.GetType() == GeomAbs_Line ? 1 : 32;
        for (int index = 0; index <= segments; ++index) {
            const auto point = curve.Value(start+(end-start)*index/segments);
            points.emplace_back(VariantArray{point.X(), point.Y(), point.Z()});
        }
        edges.emplace_back(points);
    }
    std::ofstream(directory/"native-cut-edges.json") << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
        ObjectMap{{"edges", edges}, {"geometrySource", std::string("saved-processed-native-brep")}});
    std::ofstream(directory/"native-cut-proof.json") << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
        ObjectMap{{"passed", !::testing::Test::HasFailure()}, {"length", 8384.0}, {"hingeWallProbes", probes},
            {"parameters", parameters}, {"formingPlan", native.at("plan")}, {"materialPlan", materialPlan},
            {"seamWitnesses", seamWitnesses}});
}
