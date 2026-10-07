namespace {
TopoDS_Shape continuousRoundTabsShape(Scene& scene,const ObjectMap& reference) {
    const auto resource=scene.Resources().Get<BRepModel>(reference.at("url").To<std::string>(),
        reference.at("version").To<unsigned long long>());
    if(!resource)throw std::runtime_error("Continuous round tabs lost a saved native BRep");
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
    if(!built.bOK||built.Shape.IsNull())throw std::runtime_error("Continuous round tabs BRep cannot rebuild");
    return built.Shape;
}

void continuousRoundTabsCase(const double diameter,const double wall) {
    Scene scene;
    const ObjectMap input{{"faceType",std::string("single")},{"width",1200.0},{"height",1800.0},
        {"accessDoorEnabled",true},{"frameManufacturingMode",std::string("plane_v_notch")},
        {"outerFrameGrooveTool",std::string("system:edge-arc-groove")},
        {"doorFrameJoinType",std::string("v_groove_90:tool_library")},
        {"doorLeafFrameJoinType",std::string("v_groove_90:tool_library")},
        {"doorFrameGrooveTool",std::string("system:edge-arc-groove")},
        {"doorLeafFrameGrooveTool",std::string("system:edge-arc-groove")},
        {"horizontalEndConnection",std::string("tabs")},{"verticalEndConnection",std::string("tabs")},
        {"assemblyClearance",0.1},{"verticalProfileType",std::string("round")},
        {"verticalWidth",diameter},{"verticalDepth",diameter},{"verticalWallThickness",wall},
        {"verticalMaximumCenterSpacing",120.0},{"doorVerticalProfileType",std::string("round")},
        {"doorVerticalWidth",16.0},{"doorVerticalWallThickness",0.8}};
    const auto originalInput=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input);
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},{"parameters",input}});
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input),originalInput);
    const auto parameters=preview.at("parameters").To<ObjectMap>();
    for(const auto& [key,value]:input)EXPECT_EQ(parameters.at(key),value)<<key;
    ASSERT_EQ(preview.at("items").To<VariantArray>().size(),41u);
    auto request=parameters;request["templateId"]=std::string("single-face-security-window");
    const auto product=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
    EXPECT_EQ(product.at("parameters"),preview.at("parameters"));
    const auto run=disassemblyCountRun(scene,product);
    const auto display=run->GetNeutralModel();
    const auto result=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(run->GetNeutralModel(),display);
    EXPECT_EQ(run->GetManufacturingParameters(),parameters);
    EXPECT_EQ(result.at("product").To<ObjectMap>().at("parameters"),preview.at("parameters"));
    disassemblyCountChecksParts(scene,result,*run);
    const auto manufacturing=run->GetManufacturingModel();
    const auto extensions=manufacturing.at("extensions").To<ObjectMap>();
    const auto source=extensions.at("tubeDesigner.assemblyProcessSource").To<ObjectMap>();
    const auto native=extensions.at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto stocks=native.at("stocks").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountUnitQuantity(result),stocks.size());
    std::set<std::string> coveredMembers,displayMembers;
    for(const auto& value:preview.at("items").To<VariantArray>())
        displayMembers.insert(value.To<ObjectMap>().at("key").To<std::string>());
    for(const auto& value:result.at("parts").To<VariantArray>())
        for(const auto& member:value.To<ObjectMap>().at("properties").To<ObjectMap>()
            .at("manufacturing.sourceMembers").To<VariantArray>())
            EXPECT_TRUE(coveredMembers.insert(member.To<ObjectMap>().at("itemKey").To<std::string>()).second);
    EXPECT_EQ(coveredMembers,displayMembers);
    EXPECT_EQ(coveredMembers.size(),41u);
    std::map<std::string,ObjectMap> instances;
    for(const auto& value:source.at("instances").To<VariantArray>()) {
        const auto instance=value.To<ObjectMap>();
        instances.emplace(instance.at("instanceId").To<std::string>(),instance);
    }
    const std::array<std::pair<std::string,std::string>,2> receivers{{
        {"outer_frame.continuous.0001","start"},
        {"access_door.fixed_frame.continuous.0001","end"}}};
    VariantArray probes;
    for(const auto& [stockId,branchEnd]:receivers) {
        SCOPED_TRACE(stockId);
        ASSERT_TRUE(stocks.contains(stockId));
        const auto stock=stocks.at(stockId).To<ObjectMap>();
        const auto blank=continuousRoundTabsShape(scene,stock.at("baseGeometry").To<ObjectMap>());
        const auto finished=continuousRoundTabsShape(scene,stock.at("processedGeometry").To<ObjectMap>());
        ASSERT_TRUE(BRepCheck_Analyzer(finished).IsValid());
        const auto profile=stock.at("profile").To<ObjectMap>();
        const double receiverWall=finishedProductNumber(profile.at("wallThickness"));
        Bnd_Box bounds;BRepBndLib::Add(blank,bounds,false);
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        const double length=x1-x0;
        std::set<std::string> frozenOperations;
        for(const auto& value:stock.at("operationIds").To<VariantArray>())frozenOperations.insert(value.To<std::string>());
        for(int side=0;side<2;++side) {
            SCOPED_TRACE(side);
            const auto id=stockId+".grid-tab-aperture.main_grid.vertical.bottom.0004."+branchEnd+"."+std::to_string(side);
            ASSERT_TRUE(instances.contains(id))<<id;
            const auto instance=instances.at(id);
            EXPECT_EQ(instance.at("templateId").To<std::string>(),"tube-post-aperture");
            const auto process=instance.at("processInput").To<ObjectMap>();
            const auto geometry=process.at("geometry").To<ObjectMap>();
            EXPECT_EQ(geometry.at("endOpening").To<std::string>(),side?"end":"start");
            const auto clip=geometry.at("clipInterval").To<VariantArray>();
            ASSERT_EQ(clip.size(),2u);
            EXPECT_NEAR(finishedProductNumber(clip[side]),side?length:0.0,1e-4);
            const auto node=geometry.at("node").To<VariantArray>();
            EXPECT_NEAR(finishedProductNumber(node[0]),side?length:0.0,1e-4);
            EXPECT_EQ(instance.at("parameters").To<ObjectMap>().at("joint").To<std::string>(),"tabs");
            bool frozen=false;
            for(const auto& operation:native.at("plan").To<ObjectMap>().at("operations").To<VariantArray>()) {
                const auto row=operation.To<ObjectMap>();
                if(row.at("instanceId").To<std::string>()==id) {
                    EXPECT_EQ(row.at("stockId").To<std::string>(),stockId);
                    EXPECT_TRUE(frozenOperations.contains(row.at("operationId").To<std::string>()));
                    frozen=true;
                }
            }
            EXPECT_TRUE(frozen)<<id;
            // Both actual curved ear skins cross the closed frame's split seam.
            // Each half must cut only its own end/near wall, preserving the
            // center bridge and opposite wall of the receiving rectangular tube.
            const double x=side?x1-0.5:x0+0.5;
            const double y=std::sqrt(diameter*diameter/4-0.25);
            const bool upper=finishedProductNumber(node[2])>(z0+z1)/2;
            const double nearZ=upper?z1-receiverWall/2:z0+receiverWall/2;
            const double farZ=upper?z0+receiverWall/2:z1-receiverWall/2;
            for(const double sign:{-1.0,1.0}) {
                const gp_Pnt socketPoint(x,sign*y,nearZ);
                EXPECT_EQ(BRepClass3d_SolidClassifier(blank,socketPoint,1e-5).State(),TopAbs_IN);
                EXPECT_EQ(BRepClass3d_SolidClassifier(finished,socketPoint,1e-5).State(),TopAbs_OUT)<<id;
                const gp_Pnt oppositePoint(x,sign*y,farZ);
                EXPECT_EQ(BRepClass3d_SolidClassifier(blank,oppositePoint,1e-5).State(),TopAbs_IN);
                EXPECT_EQ(BRepClass3d_SolidClassifier(finished,oppositePoint,1e-5).State(),TopAbs_IN)<<id;
            }
            const gp_Pnt bridgePoint(x,0,nearZ);
            EXPECT_EQ(BRepClass3d_SolidClassifier(blank,bridgePoint,1e-5).State(),TopAbs_IN);
            EXPECT_EQ(BRepClass3d_SolidClassifier(finished,bridgePoint,1e-5).State(),TopAbs_IN)<<id;
            probes.emplace_back(ObjectMap{{"instanceId",id},{"clipInterval",clip},
                {"station",x},{"skinY",y},{"nearWallZ",nearZ},{"oppositeWallZ",farZ}});
        }
    }
    ASSERT_TRUE(stocks.contains("access_door.leaf.frame.continuous.0001"));
    const auto directory=std::filesystem::current_path()/"output/tests/round-tab-seam";
    std::filesystem::create_directories(directory);
    std::ofstream(directory/("native-"+std::to_string(static_cast<int>(diameter))+".json"))
        <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
            {"passed",!::testing::Test::HasFailure()},{"parameters",parameters},
            {"stockCount",static_cast<unsigned long long>(stocks.size())},
            {"types",static_cast<unsigned long long>(result.at("parts").To<VariantArray>().size())},
            {"sourceMemberCount",static_cast<unsigned long long>(coveredMembers.size())},{"seamProbes",probes}});
}
}

TEST(ProductDisassemblyCountSDO, ContinuousEdgeArcFramesKeepRoundTabsAtBothSplitSeamEnds) {
    continuousRoundTabsCase(19.0,1.0);
}

TEST(ProductDisassemblyCountSDO, ContinuousEdgeArcFramesKeepSixteenMillimeterRoundTabsAtBothSplitSeamEnds) {
    continuousRoundTabsCase(16.0,0.8);
}
