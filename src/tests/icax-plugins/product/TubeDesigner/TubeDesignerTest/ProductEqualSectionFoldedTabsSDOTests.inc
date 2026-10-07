// The real right-front product uses one material for the horizontal frames
// and the shared vertical post. Only the retained ears enter the receivers.
namespace {
gp_Pnt equalSectionTabsPoint(const VariantArray& values) {
    return {finishedProductNumber(values[0]),finishedProductNumber(values[1]),finishedProductNumber(values[2])};
}

TopoDS_Shape equalSectionTabsBeforeCorner(Scene& scene,const ObjectMap& stock,const std::string& key) {
    auto request=stock.at("request").To<ObjectMap>();
    VariantArray features;
    for(const auto& value:request.at("features").To<VariantArray>()) {
        const auto feature=value.To<ObjectMap>();
        if(feature.at("id").To<std::string>().find(".corner.")==std::string::npos)features.push_back(value);
    }
    request["features"]=std::move(features);
    // The public drawing endpoint requires a fixed-section envelope. Keep
    // the saved actual contours and dimensions, without regenerating them.
    auto profile=stock.at("profile").To<ObjectMap>();
    profile["schema"]=std::string("icax.imported-tube-profile");profile["schemaVersion"]=1ull;
    profile["kind"]=std::string("fixed-section");profile["profileForm"]=std::string("fixed");
    request["drawing"]=ObjectMap{{"schemaVersion",1ull},{"length",request.at("length")},
        {"section",ObjectMap{{"profile",profile}}}};
    return finishedProductNativeShape(scene,finishedProductOptimizedNativePreview(scene,request,key));
}
}

TEST(ProductDisassemblyCountSDO, RightFrontContinuousFramesKeepEqualSectionPostTabsAndMatchingSockets) {
    Scene scene;
    const ObjectMap input{{"faceType",std::string("two")},{"sidePosition",std::string("right")},
        {"width",1200.0},{"sideWidth",600.0},{"height",1800.0},{"accessDoorEnabled",true},
        {"frameManufacturingMode",std::string("plane_v_notch")},
        {"outerFrameGrooveTool",std::string("system:edge-arc-groove")},
        {"doorFrameJoinType",std::string("miter_45")},{"doorLeafFrameJoinType",std::string("miter_45")},
        {"foldedPostJoint",std::string("tabs")},{"horizontalEndConnection",std::string("insert")},
        {"verticalEndConnection",std::string("insert")},{"assemblyClearance",0.1}};
    const auto inputJson=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input);
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},{"parameters",input}});
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input),inputJson);
    const auto parameters=preview.at("parameters").To<ObjectMap>();
    for(const auto& [key,value]:input)EXPECT_EQ(parameters.at(key),value)<<key;
    EXPECT_FALSE(parameters.contains("outerFramePostMaterial"));
    ASSERT_EQ(preview.at("items").To<VariantArray>().size(),53u);
    auto request=parameters;request["templateId"]=std::string("single-face-security-window");
    const auto product=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    const auto run=disassemblyCountRun(scene,product);
    const auto display=run->GetNeutralModel();
    const auto result=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(run->GetNeutralModel(),display);
    EXPECT_EQ(run->GetManufacturingParameters(),parameters);
    EXPECT_EQ(result.at("product").To<ObjectMap>().at("parameters"),preview.at("parameters"));
    disassemblyCountChecksParts(scene,result,*run);
    const auto model=run->GetManufacturingModel();
    const auto native=model.at("extensions").To<ObjectMap>().at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto stocks=native.at("stocks").To<ObjectMap>();
    ASSERT_EQ(stocks.size(),51u);
    EXPECT_EQ(disassemblyCountUnitQuantity(result),51u);
    std::set<std::string> expectedSources,actualSources;
    for(const auto& value:preview.at("items").To<VariantArray>())expectedSources.insert(value.To<ObjectMap>().at("key").To<std::string>());
    for(const auto& value:result.at("parts").To<VariantArray>())
        for(const auto& source:value.To<ObjectMap>().at("properties").To<ObjectMap>().at("manufacturing.sourceMembers").To<VariantArray>())
            EXPECT_TRUE(actualSources.insert(source.To<ObjectMap>().at("itemKey").To<std::string>()).second);
    EXPECT_EQ(actualSources,expectedSources);
    EXPECT_EQ(actualSources.size(),53u);

    const auto post=stocks.at("outer_frame.vertical.0002").To<ObjectMap>();
    const auto profile=post.at("profile").To<ObjectMap>();
    for(const auto* stockId:{"outer_frame.plane.top","outer_frame.plane.bottom","outer_frame.vertical.0002"}) {
        const auto row=stocks.at(stockId).To<ObjectMap>();
        const auto section=row.at("profile").To<ObjectMap>();
        EXPECT_DOUBLE_EQ(finishedProductNumber(section.at("width")),38.0);
        EXPECT_DOUBLE_EQ(finishedProductNumber(section.at("depth")),38.0);
        EXPECT_DOUBLE_EQ(finishedProductNumber(section.at("wallThickness")),1.2);
        EXPECT_EQ(section.at("contours"),profile.at("contours"));
        const auto shape=continuousRoundTabsShape(scene,row.at("processedGeometry").To<ObjectMap>());
        EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
        unsigned int count=0;for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next())++count;
        EXPECT_EQ(count,1u)<<stockId;
    }
    const auto blankPost=continuousRoundTabsShape(scene,post.at("baseGeometry").To<ObjectMap>());
    const auto finishedPost=continuousRoundTabsShape(scene,post.at("processedGeometry").To<ObjectMap>());
    const auto postRequest=post.at("request").To<ObjectMap>();
    const auto ends=postRequest.at("ends").To<ObjectMap>();
    const double length=finishedProductNumber(postRequest.at("length"));
    const double skin=38.0/2-1.2/2;
    for(const auto* end:{"start","end"}) {
        SCOPED_TRACE(end);
        const auto recipe=ends.at(end).To<ObjectMap>();
        EXPECT_EQ(recipe.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"paired-end-tabs");
        EXPECT_DOUBLE_EQ(finishedProductNumber(recipe.at("toolParameters").To<ObjectMap>().at("pairCount")),2.0);
        const double trim=finishedProductNumber(recipe.at("trim"));
        const double depth=finishedProductNumber(recipe.at("toolParameters").To<ObjectMap>().at("tabLength"));
        const auto at=[&](double distance){return std::string(end)=="start"?distance:length-distance;};
        for(const double sign:{-1.0,1.0}) {
            const gp_Pnt ear(at(trim+depth/2),0,sign*skin);
            EXPECT_EQ(BRepClass3d_SolidClassifier(blankPost,ear,1e-5).State(),TopAbs_IN);
            EXPECT_EQ(BRepClass3d_SolidClassifier(finishedPost,ear,1e-5).State(),TopAbs_IN);
            const gp_Pnt removed(at(trim+depth/2),sign*skin,0);
            EXPECT_EQ(BRepClass3d_SolidClassifier(blankPost,removed,1e-5).State(),TopAbs_IN);
            EXPECT_EQ(BRepClass3d_SolidClassifier(finishedPost,removed,1e-5).State(),TopAbs_OUT);
            const gp_Pnt body(at(trim+depth+2),sign*skin,0);
            EXPECT_EQ(BRepClass3d_SolidClassifier(finishedPost,body,1e-5).State(),TopAbs_IN);
        }
    }

    VariantArray witnesses;
    unsigned int totalFeatures=0,totalSockets=0;
    for(const auto* stockId:{"outer_frame.plane.top","outer_frame.plane.bottom"}) {
        SCOPED_TRACE(stockId);
        const auto stock=stocks.at(stockId).To<ObjectMap>();
        const auto stockRequest=stock.at("request").To<ObjectMap>();
        const auto finished=continuousRoundTabsShape(scene,stock.at("processedGeometry").To<ObjectMap>());
        // Replay the same real groove, infill cuts and boundary operations,
        // removing only the corner slots. This distinguishes a new socket
        // from material already removed by the edge-arc bend groove.
        const auto before=equalSectionTabsBeforeCorner(scene,stock,
            std::string(stockId)=="outer_frame.plane.top"?"equal-tabs-top-before":"equal-tabs-bottom-before");
        unsigned int newSocketCuts=0,preservedOppositeSkins=0;
        for(const auto& value:stockRequest.at("features").To<VariantArray>()) {
            const auto feature=value.To<ObjectMap>();
            if(feature.at("id").To<std::string>().find(".corner.")==std::string::npos)continue;
            ++totalFeatures;
            EXPECT_EQ(feature.at("toolRef").To<ObjectMap>().at("id").To<std::string>(),"paired-side-slots");
            const auto values=feature.at("toolParameters").To<ObjectMap>();
            EXPECT_TRUE(values.at("allowSideOpening").To<bool>());
            EXPECT_FALSE(values.at("allowEndOpening").To<bool>());
            const auto frozen=feature.at("frozenTool").To<ObjectMap>();
            EXPECT_EQ(frozen.at("parameters").To<ObjectMap>().at("allowSideOpening"),values.at("allowSideOpening"));
            for(const auto& node:frozen.at("geometry").To<ObjectMap>().at("model").To<ObjectMap>().at("geometry").To<VariantArray>()) {
                const auto geometry=node.To<ObjectMap>();
                if(geometry.at("operator").To<std::string>()!="profile2d")continue;
                ++totalSockets;
                const auto placement=geometry.at("arguments").To<ObjectMap>().at("placement").To<ObjectMap>();
                const auto origin=equalSectionTabsPoint(placement.at("origin").To<VariantArray>());
                const double normalSign=origin.Z()>0?1.0:-1.0;
                const gp_Pnt nearWall(origin.X(),origin.Y(),normalSign*skin);
                const gp_Pnt oppositeWall(origin.X(),origin.Y(),-normalSign*skin);
                const auto prior=BRepClass3d_SolidClassifier(before,nearWall,1e-5).State();
                const auto after=BRepClass3d_SolidClassifier(finished,nearWall,1e-5).State();
                EXPECT_EQ(after,TopAbs_OUT);
                if(prior==TopAbs_IN)++newSocketCuts;
                if(BRepClass3d_SolidClassifier(before,oppositeWall,1e-5).State()==TopAbs_IN) {
                    EXPECT_EQ(BRepClass3d_SolidClassifier(finished,oppositeWall,1e-5).State(),TopAbs_IN);
                    ++preservedOppositeSkins;
                }
                witnesses.emplace_back(ObjectMap{{"stockId",std::string(stockId)},{"featureId",feature.at("id")},
                    {"origin",placement.at("origin")},{"materialBeforeCorner",prior==TopAbs_IN},
                    {"removedAfterCorner",after==TopAbs_OUT}});
            }
        }
        EXPECT_GE(newSocketCuts,2u)<<"Must remove actual retained material, not only overlap the pre-existing bend groove";
        EXPECT_GE(preservedOppositeSkins,2u);
        // Away from the bend, all four continuous frame skins stay intact.
        for(const auto point:{gp_Pnt(30,skin,0),gp_Pnt(30,-skin,0),gp_Pnt(30,0,skin),gp_Pnt(30,0,-skin)})
            EXPECT_EQ(BRepClass3d_SolidClassifier(finished,point,1e-5).State(),TopAbs_IN);
    }
    EXPECT_EQ(totalFeatures,4u);
    EXPECT_EQ(totalSockets,8u);
    const auto directory=std::filesystem::current_path()/"output/tests/corner-equal-tabs";
    std::filesystem::create_directories(directory);
    std::ofstream(directory/"native.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"passed",!::testing::Test::HasFailure()},{"parameters",parameters},{"stockCount",51ull},
        {"sourceMemberCount",static_cast<unsigned long long>(actualSources.size())},{"socketWitnesses",witnesses}});
}
