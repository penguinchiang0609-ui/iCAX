namespace {
std::filesystem::path finishedProductAssemblyTemplateRoot() {
    return acceptanceRuntimePath({"apps/tube-designer/templates/assembly",
        "src/apps/tube-designer/templates/assembly"});
}
ObjectMap finishedProductFixture(Scene& scene, const std::string& shapeId) {
    const auto catalogue=invoke(scene,"GetAssemblyTemplates",ObjectMap{});
    for(const auto& value:catalogue.at("finishedShapes").To<VariantArray>()) {
        const auto shape=value.To<ObjectMap>();
        if(shape.at("id").To<std::string>()!=shapeId)continue;
        ObjectMap parameters,spans;
        for(const auto& entry:shape.at("parameters").To<VariantArray>()) {
            const auto p=entry.To<ObjectMap>();
            parameters[p.at("key").To<std::string>()]=p.at("defaultValue");
        }
        for(const auto& entry:shape.at("spans").To<VariantArray>()) {
            const auto span=entry.To<ObjectMap>();
            spans[span.at("id").To<std::string>()]=ObjectMap{
                {"profileRef",span.at("profileRef")},{"parameters",span.at("parameters")},{"length",span.at("length")}};
        }
        return {{"schema",std::string("icax.finished-product")},{"schemaVersion",1ull},
            {"shapeId",shapeId},{"parameters",parameters},{"spans",spans}};
    }
    throw std::runtime_error("Finished product shape was not found");
}
ObjectMap finishedProductAssemblyDescriptor(const std::string& id) {
    std::ifstream file(finishedProductAssemblyTemplateRoot()/id/"assembly.json");
    if(!file)throw std::runtime_error("Assembly descriptor was not found: "+id);
    const std::string json((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    return iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
}
std::map<std::string,ObjectMap> finishedProductStoredAssemblyDescriptors(Scene& scene) {
    const std::set<std::string> originalIds{
        "bend","cross-through","end-side-tab-slot","flexible-slit-bend-integrated",
        "four-end-end-end-end","insert-sleeve","mechanical-fastener","node-edge-arc-integrated",
        "node-embedded-arc-integrated","node-v-notch-integrated","saddle-weld","segmented-bend",
        "slot-bolt-adjustable","t-contact-fit","t-profile-insert","tab-slot-lock","through-bolt",
        "two-end-end-angle","two-end-middle","weld-interface","wrap-a-over-b"};
    const auto root=finishedProductAssemblyTemplateRoot();
    if(!std::filesystem::is_directory(root))throw std::runtime_error("Assembly template root is missing");
    std::map<std::string,ObjectMap> descriptors;
    std::set<std::string> expectedVisible;
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        const auto path=entry.path()/"assembly.json";
        if(!entry.is_directory()||!std::filesystem::is_regular_file(path))continue;
        std::ifstream file(path);
        if(!file)throw std::runtime_error("Assembly descriptor cannot be read: "+path.string());
        const std::string json((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        const auto descriptor=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
        const auto id=descriptor.at("id").To<std::string>();
        if(id!=entry.path().filename().string()||!descriptors.emplace(id,descriptor).second)
            throw std::runtime_error("Assembly descriptor identity is invalid or duplicated: "+path.string());
        if(!descriptor.contains("catalogueHidden")||!descriptor.at("catalogueHidden").To<bool>())
            expectedVisible.insert(id);
    }
    for(const auto& id:originalIds)if(!descriptors.contains(id))
        throw std::runtime_error("Original assembly template coverage is missing: "+id);
    const auto catalogue=invoke(scene,"GetAssemblyTemplates",ObjectMap{});
    const auto errors=catalogue.at("errors").To<VariantArray>();
    if(!errors.empty())throw std::runtime_error("Native assembly catalogue rejected stored templates: "
        +iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(errors));
    std::set<std::string> actualVisible;
    for(const auto& value:catalogue.at("assemblies").To<VariantArray>()) {
        const auto id=value.To<ObjectMap>().at("id").To<std::string>();
        if(!actualVisible.insert(id).second)throw std::runtime_error("Native catalogue repeats template: "+id);
    }
    if(actualVisible!=expectedVisible)
        throw std::runtime_error("Native catalogue does not exactly match all visible stored templates");
    return descriptors;
}
ObjectMap finishedProductProcessDefaults(const ObjectMap& descriptor) {
    ObjectMap parameters;
    for(const auto& value:descriptor.at("parameters").To<VariantArray>()) {
        const auto definition=value.To<ObjectMap>();
        if(definition.contains("scope")&&definition.at("scope").To<std::string>()=="scene")continue;
        parameters[definition.at("key").To<std::string>()]=definition.at("defaultValue");
    }
    return parameters;
}
double finishedProductNumber(const iCAX::Data::Variant& value) {
    if(value.Is<double>())return value.To<double>();
    if(value.Is<std::int64_t>())return static_cast<double>(value.To<std::int64_t>());
    if(value.Is<std::uint64_t>())return static_cast<double>(value.To<std::uint64_t>());
    if(value.Is<int>())return static_cast<double>(value.To<int>());
    return static_cast<double>(value.To<unsigned int>());
}
TopoDS_Shape finishedProductNativeShape(Scene& scene,const ObjectMap& preview,
        const std::string& field="geometry") {
    // Native preview publishes a derived display mesh. Follow its locked
    // source dependency, including its version, rather than guessing a BRep
    // resource name. An untouched blank deliberately reuses baseGeometry.
    const auto url=preview.at(field).To<ObjectMap>().at("url").To<std::string>();
    const auto info=scene.Resources().GetInfo(url);
    if(!info||info->Dependencies.size()!=1)
        throw std::runtime_error("Preview mesh has no unique source dependency: "+url);
    const auto source=info->Dependencies.front();
    const auto model=scene.Resources().Get<BRepModel>(source.URL,source.nVersion);
    if(!model)throw std::runtime_error("Preview mesh source BRep is missing: "+source.URL);
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
    if(!rebuilt.bOK||rebuilt.Shape.IsNull())
        throw std::runtime_error("Preview source BRep cannot rebuild: "+source.URL);
    EXPECT_TRUE(BRepCheck_Analyzer(rebuilt.Shape).IsValid());
    GProp_GProps volume;
    BRepGProp::VolumeProperties(rebuilt.Shape,volume);
    EXPECT_GT(volume.Mass(),1.0);
    return rebuilt.Shape;
}
ObjectMap finishedProductOptimizedNativePreview(Scene& scene,const ObjectMap& request,const std::string& key) {
    const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(request);
    std::set<std::string> resourcesBefore;
    for(const auto& info:scene.Resources().GetInfos())resourcesBefore.insert(info.Key.Source);
    auto payload=request;
    payload["previewResourceKey"]=key;
    const auto preview=invoke(scene,"PreviewAssemblyManufacturingPart",payload);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(request),before);
    EXPECT_TRUE(preview.at("previewComputed").To<bool>());
    EXPECT_TRUE(preview.at("resultValid").To<bool>());
    EXPECT_EQ(finishedProductNumber(preview.at("solidCount")),1.0);
    const std::set<std::string> fields{"geometry","material","bounds","length","resultValid",
        "previewComputed","solidCount","performanceMs"};
    EXPECT_EQ(preview.size(),fields.size());
    for(const auto& [field,value]:preview)EXPECT_TRUE(fields.contains(field))<<field;
    const auto phases=preview.at("performanceMs").To<ObjectMap>();
    for(const auto* phase:{"profile","blank","sectionAnalysis","punchPrepare","punchGeometry","brepStore","mesh","total"}) {
        const double value=finishedProductNumber(phases.at(phase));
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value,0.0)<<phase;
    }
    const auto meshURL=preview.at("geometry").To<ObjectMap>().at("url").To<std::string>();
    const auto mesh=scene.Resources().GetInfo(meshURL);
    if(!mesh||mesh->Dependencies.size()!=1)throw std::runtime_error("Assembly mesh source dependency is missing");
    const auto materialURL=preview.at("material").To<ObjectMap>().at("url").To<std::string>();
    std::set<std::string> allowedResources{meshURL,mesh->Dependencies.front().URL,materialURL};
    const auto material=scene.Resources().GetInfo(materialURL);
    if(material)for(const auto& dependency:material->Dependencies)allowedResources.insert(dependency.URL);
    std::size_t added=0;
    for(const auto& info:scene.Resources().GetInfos())if(!resourcesBefore.contains(info.Key.Source)) {
        ++added;
        // Enumerate the store, not just response fields: there must be no
        // separately persisted blank, knife, or frozen-cut geometry.
        EXPECT_TRUE(allowedResources.contains(info.Key.Source))<<info.Key.Source<<" "<<info.Name;
    }
    EXPECT_GE(added,2u);
    EXPECT_LE(added,4u); // Final BRep/mesh plus native/frontend material on first use.
    return preview;
}
void finishedProductSameNativeShape(const TopoDS_Shape& actual,const TopoDS_Shape& expected) {
    GProp_GProps actualVolume,expectedVolume;
    BRepGProp::VolumeProperties(actual,actualVolume);
    BRepGProp::VolumeProperties(expected,expectedVolume);
    const double tolerance=std::max(1e-5,std::abs(expectedVolume.Mass())*1e-7);
    EXPECT_NEAR(actualVolume.Mass(),expectedVolume.Mass(),tolerance);
    BRepAlgoAPI_Common common(actual,expected);
    ASSERT_TRUE(common.IsDone());
    GProp_GProps intersection;
    BRepGProp::VolumeProperties(common.Shape(),intersection);
    EXPECT_NEAR(intersection.Mass(),expectedVolume.Mass(),tolerance);
    Bnd_Box actualBox,expectedBox;
    BRepBndLib::AddOptimal(actual,actualBox,false,false);
    BRepBndLib::AddOptimal(expected,expectedBox,false,false);
    std::array<double,6> actualBounds,expectedBounds;
    actualBox.Get(actualBounds[0],actualBounds[1],actualBounds[2],actualBounds[3],actualBounds[4],actualBounds[5]);
    expectedBox.Get(expectedBounds[0],expectedBounds[1],expectedBounds[2],expectedBounds[3],expectedBounds[4],expectedBounds[5]);
    for(std::size_t index=0;index<actualBounds.size();++index)EXPECT_NEAR(actualBounds[index],expectedBounds[index],1e-5);
    std::size_t solids=0;
    for(TopExp_Explorer explorer(actual,TopAbs_SOLID);explorer.More();explorer.Next())++solids;
    EXPECT_EQ(solids,1u);
}
ObjectMap finishedProductComparedNativePreview(Scene& scene,const ObjectMap& request,const std::string& key) {
    const auto preview=finishedProductOptimizedNativePreview(scene,request,key+"-final");
    const auto legacy=previewAssemblyManufacturingPart(scene,request,key+"-legacy");
    EXPECT_TRUE(legacy.at("resultValid").To<bool>());
    EXPECT_EQ(finishedProductNumber(legacy.at("solidCount")),1.0);
    EXPECT_TRUE(legacy.contains("baseGeometry"));
    EXPECT_TRUE(legacy.contains("toolPreviews"));
    finishedProductSameNativeShape(finishedProductNativeShape(scene,preview),finishedProductNativeShape(scene,legacy));
    EXPECT_EQ(preview.at("material"),legacy.at("baseMaterial"));
    return preview;
}
void finishedProductChecksNativeBlanks(Scene& scene,const ObjectMap& plan,const std::string& keyPrefix) {
    const auto parts=plan.at("manufacturingParts").To<VariantArray>();
    ASSERT_FALSE(parts.empty());
    std::size_t index=0;
    for(const auto& value:parts) {
        SCOPED_TRACE(keyPrefix+" blank="+std::to_string(index));
        const auto preview=finishedProductComparedNativePreview(scene,value.To<ObjectMap>().at("request").To<ObjectMap>(),
            keyPrefix+"-"+std::to_string(index++));
        ASSERT_FALSE(preview.contains("resultError"))
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
        ASSERT_TRUE(preview.at("previewComputed").To<bool>());
        EXPECT_TRUE(preview.at("resultValid").To<bool>());
        EXPECT_EQ(finishedProductNumber(preview.at("solidCount")),1.0);
        finishedProductNativeShape(scene,preview);
    }
}
}

TEST(AssemblyTemplateSDOTest, FinishedProductIsIndependentAndImmutableProcessInput)
{
    Scene scene;
    auto product=finishedProductFixture(scene,"l");
    auto dimensions=product.at("parameters").To<ObjectMap>();
    dimensions["angle"]=110.0;
    product["parameters"]=dimensions;
    const auto serialized=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
    const auto finished=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"finishedOnly",true},{"finishedProduct",product}});
    EXPECT_EQ(finished.at("schema").To<std::string>(),"icax.finished-product-preview");
    EXPECT_FALSE(finished.contains("templateId"));
    EXPECT_FALSE(finished.contains("manufacturingParts"));
    const auto expectedGeometry=finished.at("designParts").To<VariantArray>();
    for(const auto& id:{"bend","two-end-end-angle","segmented-bend"}) {
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",ObjectMap{}}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),serialized);
        const auto designs=plan.at("designParts").To<VariantArray>();
        ASSERT_EQ(designs.size(),expectedGeometry.size());
        for(std::size_t i=0;i<designs.size();++i) {
            const auto actual=designs[i].To<ObjectMap>(),expected=expectedGeometry[i].To<ObjectMap>();
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(actual.at("request")),
                      iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expected.at("request")));
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(actual.at("matrix")),
                      iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expected.at("matrix")));
        }
        const auto blanks=plan.at("manufacturingParts").To<VariantArray>();
        EXPECT_EQ(blanks.size(),std::string(id)=="two-end-end-angle"?2u:1u);
        EXPECT_FALSE(plan.at("parameters").To<ObjectMap>().contains("angle"));
        EXPECT_FALSE(plan.at("parameters").To<ObjectMap>().contains("jointAngle"));
        for(const auto& blank:blanks) {
            const auto preview=previewAssemblyManufacturingPart(scene,blank.To<ObjectMap>().at("request").To<ObjectMap>());
            EXPECT_FALSE(preview.contains("resultError")) << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
            EXPECT_TRUE(preview.at("previewComputed").To<bool>());
        }
    }
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),serialized);
}

TEST(AssemblyTemplateSDOTest, ApplicabilityAcceptsTheSameProductWithoutChangingItsGeometry)
{
    Scene scene;
    auto product=finishedProductFixture(scene,"l");
    auto dimensions=product.at("parameters").To<ObjectMap>();
    dimensions["angle"]=110.0;
    product["parameters"]=dimensions;
    const ObjectMap parameters{{"fitGap",0.3}};
    const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
    const auto parametersBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
    for(const auto& id:{"bend","two-end-end-angle","segmented-bend"}) {
        SCOPED_TRACE(id);
        const auto result=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",ObjectMap{}}});
        EXPECT_EQ(result.at("schema").To<std::string>(),"icax.assembly-applicability");
        EXPECT_EQ(result.at("schemaVersion").To<std::int64_t>(),1ll);
        EXPECT_EQ(result.at("templateId").To<std::string>(),id);
        EXPECT_TRUE(result.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result);
        EXPECT_TRUE(result.at("reason").To<std::string>().empty());
        EXPECT_FALSE(result.contains("designParts"));
        EXPECT_FALSE(result.contains("manufacturingParts"));
    }
    const auto withParameters=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},{"finishedProduct",product},{"parameters",parameters}});
    EXPECT_TRUE(withParameters.at("applicable").To<bool>());
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters),parametersBefore);

    dimensions["angle"]=170.0;
    product["parameters"]=dimensions;
    const auto steepBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
    const auto miter=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},{"finishedProduct",product}});
    EXPECT_FALSE(miter.at("applicable").To<bool>());
    EXPECT_FALSE(miter.at("reason").To<std::string>().empty());
    EXPECT_NE(miter.at("reason").To<std::string>().find("不支持成品参数"),std::string::npos);
    EXPECT_THROW(invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},{"finishedProduct",product}}),std::runtime_error);
    const auto bend=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("bend")},{"finishedProduct",product}});
    EXPECT_TRUE(bend.at("applicable").To<bool>());
    const auto bendPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("bend")},{"finishedProduct",product}});
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(bendPlan.at("finishedProduct")),steepBefore);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),steepBefore);
}

TEST(AssemblyTemplateSDOTest, ApplicabilityRejectsWrongShapeAndDifferentIntegratedSections)
{
    Scene scene;
    const auto tProduct=finishedProductFixture(scene,"t");
    const auto wrongShape=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("bend")},{"finishedProduct",tProduct}});
    EXPECT_FALSE(wrongShape.at("applicable").To<bool>());
    EXPECT_FALSE(wrongShape.at("reason").To<std::string>().empty());
    EXPECT_NE(wrongShape.at("reason").To<std::string>().find("成品造型"),std::string::npos);

    auto product=finishedProductFixture(scene,"l");
    auto spans=product.at("spans").To<ObjectMap>();
    auto armB=spans.at("armB").To<ObjectMap>();
    auto sectionParameters=armB.at("parameters").To<ObjectMap>();
    sectionParameters["width"]=50.0;
    armB["parameters"]=sectionParameters;
    spans["armB"]=armB;
    product["spans"]=spans;
    const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
    const auto result=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("bend")},{"finishedProduct",product}});
    EXPECT_FALSE(result.at("applicable").To<bool>());
    EXPECT_FALSE(result.at("reason").To<std::string>().empty());
    EXPECT_NE(result.at("reason").To<std::string>().find("相同管型及截面参数"),std::string::npos);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
}

TEST(AssemblyTemplateSDOTest, EveryStoredTemplateChecksItsFinishedProductThroughNativeInterface)
{
    Scene scene;
    const auto descriptors=finishedProductStoredAssemblyDescriptors(scene);
    for(const auto& [id,descriptor]:descriptors) {
        SCOPED_TRACE(id);
        EXPECT_TRUE(descriptor.contains("inputContract"));
        EXPECT_TRUE(descriptor.contains("exampleInput"));
        EXPECT_FALSE(descriptor.contains("productInput"));
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{{"templateId",id}})
            .at("finishedProduct").To<ObjectMap>();
        if(id=="insert-sleeve") {
            auto spans=product.at("spans").To<ObjectMap>();
            for(const auto& [role,width]:std::vector<std::pair<std::string,double>>{{"first",30.0},{"second",34.6}}) {
                auto span=spans.at(role).To<ObjectMap>();
                span["profileRef"]=ObjectMap{{"scope",std::string("system")},{"id",std::string("round")}};
                span["parameters"]=ObjectMap{{"width",width},{"wallThickness",2.0},{"innerOffsetX",0.0},{"innerOffsetY",0.0}};
                spans[role]=span;
            }
            product["spans"]=spans;
        }
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto result=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",id},{"finishedProduct",product}});
        EXPECT_EQ(result.at("templateId").To<std::string>(),id);
        EXPECT_TRUE(result.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(result);
        EXPECT_TRUE(result.at("reason").To<std::string>().empty());
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",id},{"finishedProduct",product},{"parameters",ObjectMap{}}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        const auto processParameters=plan.at("parameters").To<ObjectMap>();
        const auto definitions=descriptor.at("parameters").To<VariantArray>();
        EXPECT_EQ(processParameters.size(),definitions.size());
        for(const auto& definition:definitions)
            EXPECT_TRUE(processParameters.contains(definition.To<ObjectMap>().at("key").To<std::string>()));
        const auto contract=descriptor.at("inputContract").To<ObjectMap>();
        for(const auto& definition:contract.at("geometryParameters").To<VariantArray>())
            EXPECT_FALSE(processParameters.contains(definition.To<ObjectMap>().at("key").To<std::string>()));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
    }
}

TEST(AssemblyTemplateSDOTest, EveryTemplateProvidesApplicableExampleProductWithoutManufacturing)
{
    Scene scene;
    const auto descriptors=finishedProductStoredAssemblyDescriptors(scene);
    std::size_t count=0;
    for(const auto& [id,descriptor]:descriptors) {
        ++count;
        SCOPED_TRACE(id);
        const auto example=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{{"templateId",id}});
        EXPECT_EQ(example.at("schema").To<std::string>(),"icax.assembly-example-product");
        EXPECT_EQ(example.at("schemaVersion").To<std::int64_t>(),1ll);
        EXPECT_EQ(example.at("templateId").To<std::string>(),id);
        EXPECT_EQ(example.size(),4u);
        const auto product=example.at("finishedProduct").To<ObjectMap>();
        EXPECT_EQ(product.size(),5u);
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",id},{"finishedProduct",product}});
        EXPECT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto finished=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"finishedOnly",true},{"finishedProduct",product}});
        EXPECT_EQ(finished.at("schema").To<std::string>(),"icax.finished-product-preview");
        EXPECT_FALSE(finished.contains("manufacturingParts"));
        EXPECT_FALSE(finished.contains("formedPreviewRecipe"));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(finished.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
    }
    EXPECT_EQ(count,descriptors.size());
}

TEST(AssemblyTemplateSDOTest, ExampleProductDimensionsRemainSeparateFromNativeManufacturing)
{
    Scene scene;
    const std::vector<std::pair<std::string,ObjectMap>> cases{
        {"wrap-a-over-b",{{"maleFemale",true},{"pairCount",std::string("four")},
            {"tabWidth",24.0},{"tabLength",30.0},{"sideClearance",1.5}}},
        {"insert-sleeve",{{"fitClearance",1.2}}},
        {"end-side-tab-slot",{{"pairCount",std::string("four")}}}
    };
    for(const auto& [id,parameters]:cases) {
        SCOPED_TRACE(id);
        const auto parametersBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
        const auto example=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",id},{"parameters",parameters}});
        const auto product=example.at("finishedProduct").To<ObjectMap>();
        const auto productBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",id},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>());
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",id},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),productBefore);
        for(const auto& [key,value]:parameters)
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters").To<ObjectMap>().at(key)),
                      iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(value));
        for(const auto& value:plan.at("manufacturingParts").To<VariantArray>()) {
            const auto preview=previewAssemblyManufacturingPart(scene,value.To<ObjectMap>().at("request").To<ObjectMap>());
            EXPECT_FALSE(preview.contains("resultError"))
                <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(preview);
            EXPECT_TRUE(preview.at("previewComputed").To<bool>());
        }
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),productBefore);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters),parametersBefore);
    }
    EXPECT_THROW(invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
        {"templateId",std::string("bend")},{"parameters",ObjectMap{{"angle",90.0}}}}),std::runtime_error);
}

TEST(AssemblyTemplateSDOTest, InsertAllowancesPreserveFinishedLengthsAndBuildNativeBlanks)
{
    for(const auto* id:{"insert-sleeve","tab-slot-lock"}) {
        SCOPED_TRACE(id);
        Scene scene;
        auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        auto spans=product.at("spans").To<ObjectMap>();
        for(const auto& [spanId,length]:std::vector<std::pair<std::string,double>>{{"first",280.0},{"second",360.0}}) {
            auto span=spans.at(spanId).To<ObjectMap>();
            span["length"]=length;
            spans[spanId]=span;
        }
        product["spans"]=spans;
        const auto original=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto finished=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"finishedOnly",true},{"finishedProduct",product}});
        const auto independentParts=finished.at("designParts").To<VariantArray>();
        ASSERT_EQ(independentParts.size(),2u);
        EXPECT_DOUBLE_EQ(finishedProductNumber(independentParts[0].To<ObjectMap>().at("request").To<ObjectMap>().at("length"))
            +finishedProductNumber(independentParts[1].To<ObjectMap>().at("request").To<ObjectMap>().at("length")),640.0);
        for(int variant=0;variant<2;++variant) {
            const bool sleeve=std::string(id)=="insert-sleeve";
            const double insertion=sleeve?(variant==0?120.0:180.0):(variant==0?20.0:35.0);
            parameters[sleeve?"insertDepth":"straightDepth"]=insertion;
            if(!sleeve)parameters["nominalWidth"]=20.0;
            const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
                {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
            ASSERT_TRUE(check.at("applicable").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
            const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
                {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),original);
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters")),
                iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters));
            const auto designs=plan.at("designParts").To<VariantArray>();
            ASSERT_EQ(designs.size(),2u);
            for(std::size_t i=0;i<designs.size();++i) {
                const auto actual=designs[i].To<ObjectMap>(),expected=independentParts[i].To<ObjectMap>();
                EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(actual.at("request")),
                    iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expected.at("request")));
                EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(actual.at("matrix")),
                    iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expected.at("matrix")));
            }
            const auto blanks=plan.at("manufacturingParts").To<VariantArray>();
            ASSERT_EQ(blanks.size(),2u);
            const double allowance=insertion+(sleeve?0.0:10.0);
            EXPECT_DOUBLE_EQ(finishedProductNumber(blanks[0].To<ObjectMap>().at("request").To<ObjectMap>().at("length")),
                280.0+allowance);
            EXPECT_DOUBLE_EQ(finishedProductNumber(blanks[1].To<ObjectMap>().at("request").To<ObjectMap>().at("length")),360.0);
            for(std::size_t i=0;i<blanks.size();++i) {
                const auto request=blanks[i].To<ObjectMap>().at("request").To<ObjectMap>();
                const auto preview=finishedProductOptimizedNativePreview(scene,request,
                    std::string("insert-length-")+id+"-"+std::to_string(variant)+"-"+std::to_string(i));
                const auto shape=finishedProductNativeShape(scene,preview);
                Bnd_Box bounds;
                BRepBndLib::AddOptimal(shape,bounds,false,false);
                double xmin,ymin,zmin,xmax,ymax,zmax;
                bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                EXPECT_NEAR(xmax-xmin,finishedProductNumber(request.at("length")),1e-5);
            }
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),original);
        }
    }
}

TEST(AssemblyTemplateSDOTest, EveryTemplateExampleBuildsAllNativeManufacturingGeometry)
{
    Scene scene;
    const auto descriptors=finishedProductStoredAssemblyDescriptors(scene);
    std::size_t templates=0,blanks=0,expectedBlanks=0;
    for(const auto& [id,descriptor]:descriptors) {
        ++templates;
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(descriptor);
        const auto parametersBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
        const auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",id},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto applicability=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",id},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(applicability.at("applicable").To<bool>());
        const auto finished=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"finishedOnly",true},{"finishedProduct",product}});
        EXPECT_FALSE(finished.contains("manufacturingParts"));
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",id},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters")),parametersBefore);
        finishedProductChecksNativeBlanks(scene,plan,"finished-default-"+id);
        blanks+=plan.at("manufacturingParts").To<VariantArray>().size();
        const auto declaredBlanks=descriptor.at("manufacturingPlan").To<ObjectMap>().at("blankParts").To<VariantArray>();
        expectedBlanks+=declaredBlanks.size();
        EXPECT_EQ(plan.at("manufacturingParts").To<VariantArray>().size(),declaredBlanks.size());
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters),parametersBefore);
    }
    EXPECT_EQ(templates,descriptors.size());
    EXPECT_EQ(blanks,expectedBlanks);
}

TEST(AssemblyTemplateSDOTest, RoundFinishedProductsBuildNativeBlanksWithoutDerivedInputFields)
{
    Scene scene;
    for(const auto& id:{"saddle-weld","t-profile-insert","two-end-middle","wrap-a-over-b",
            "through-bolt","mechanical-fastener","slot-bolt-adjustable","weld-interface"}) {
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        const auto parametersBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        auto spans=product.at("spans").To<ObjectMap>();
        for(auto& [role,value]:spans) {
            auto span=value.To<ObjectMap>();
            span["profileRef"]=ObjectMap{{"scope",std::string("system")},{"id",std::string("round")}};
            span["parameters"]=ObjectMap{{"width",40.0},{"wallThickness",2.0},
                {"innerOffsetX",0.0},{"innerOffsetY",0.0}};
            value=span;
        }
        product["spans"]=spans;
        if(std::string(id)=="t-profile-insert") {
            const auto sameDiameterBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
            const auto sameDiameter=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
                {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
            EXPECT_FALSE(sameDiameter.at("applicable").To<bool>());
            EXPECT_FALSE(sameDiameter.at("reason").To<std::string>().empty());
            EXPECT_THROW(invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
                {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}}),std::runtime_error);
            EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),sameDiameterBefore);
            auto branch=spans.at("branch").To<ObjectMap>();
            auto section=branch.at("parameters").To<ObjectMap>();
            section["width"]=30.0;
            branch["parameters"]=section;
            spans["branch"]=branch;
            product["spans"]=spans;
        }
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters")),parametersBefore);
        finishedProductChecksNativeBlanks(scene,plan,"finished-round-"+std::string(id));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters),parametersBefore);
    }
}

TEST(AssemblyTemplateSDOTest, MiterFinishedProductNativeCutsMeetAtDeclaredGap)
{
    const auto descriptor=finishedProductAssemblyDescriptor("two-end-end-angle");
    std::map<std::string,double> explosion;
    for(const auto& value:descriptor.at("previewScene").To<ObjectMap>().at("manufacturingParts").To<VariantArray>()) {
        const auto part=value.To<ObjectMap>();
        explosion[part.at("sourceRole").To<std::string>()]=finishedProductNumber(
            part.at("explodedPose").To<ObjectMap>().at("axialTranslation"));
    }
    for(const double angle:{60.0,90.0,120.0})for(const double plane:{0.0,35.0})for(const double gap:{0.0,2.0}) {
        SCOPED_TRACE("angle="+std::to_string(angle)+" plane="+std::to_string(plane)+" gap="+std::to_string(gap));
        Scene scene;
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string("two-end-end-angle")}}).at("finishedProduct").To<ObjectMap>();
        auto dimensions=product.at("parameters").To<ObjectMap>();
        dimensions["angle"]=angle;
        dimensions["planeRotation"]=plane;
        product["parameters"]=dimensions;
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const ObjectMap parameters{{"fitGap",gap}};
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string("two-end-end-angle")},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters")),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters));
        const auto parts=plan.at("manufacturingParts").To<VariantArray>();
        ASSERT_EQ(parts.size(),2u);
        std::array<TopoDS_Shape,2> shapes;
        for(std::size_t index=0;index<shapes.size();++index) {
            const auto part=parts[index].To<ObjectMap>();
            const auto preview=finishedProductComparedNativePreview(scene,part.at("request").To<ObjectMap>(),
                "finished-miter-"+std::to_string(index));
            ASSERT_FALSE(preview.contains("resultError"));
            ASSERT_TRUE(preview.at("resultValid").To<bool>());
            const auto local=finishedProductNativeShape(scene,preview);
            std::array<double,16> matrix;
            const auto declared=part.at("matrix").To<VariantArray>();
            for(std::size_t i=0;i<matrix.size();++i)matrix[i]=finishedProductNumber(declared[i]);
            // Reassemble from the returned blank registration by removing
            // only the display explosion declared by this template.
            const auto axialExplosion=explosion.at(part.at("sourceRole").To<std::string>());
            for(std::size_t axis=0;axis<3;++axis)matrix[axis*4+3]-=axialExplosion*matrix[axis*4];
            gp_Trsf transform;
            transform.SetValues(matrix[0],matrix[1],matrix[2],matrix[3],matrix[4],matrix[5],matrix[6],matrix[7],
                matrix[8],matrix[9],matrix[10],matrix[11]);
            shapes[index]=BRepBuilderAPI_Transform(local,transform,true).Shape();
        }
        BRepAlgoAPI_Common common(shapes[0],shapes[1]);
        ASSERT_TRUE(common.IsDone());
        GProp_GProps overlap;
        BRepGProp::VolumeProperties(common.Shape(),overlap);
        EXPECT_NEAR(overlap.Mass(),0.0,0.1);
        BRepExtrema_DistShapeShape distance(shapes[0],shapes[1]);
        distance.Perform();
        ASSERT_TRUE(distance.IsDone());
        EXPECT_NEAR(distance.Value(),gap,0.05);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
    }
}

TEST(AssemblyTemplateSDOTest, FixedPlaneProcessesRejectRotatedProductAndSleeveRejectsUndeclaredLocks)
{
    Scene scene;
    for(const auto& id:{"flexible-slit-bend-integrated","segmented-bend","node-v-notch-integrated",
            "node-edge-arc-integrated","node-embedded-arc-integrated","wrap-a-over-b"}) {
        SCOPED_TRACE(id);
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)}}).at("finishedProduct").To<ObjectMap>();
        auto dimensions=product.at("parameters").To<ObjectMap>();
        dimensions["planeRotation"]=90.0;
        product["parameters"]=dimensions;
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product}});
        EXPECT_FALSE(check.at("applicable").To<bool>());
        EXPECT_FALSE(check.at("reason").To<std::string>().empty());
        EXPECT_THROW(invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product}}),std::runtime_error);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product),before);
    }
    for(const auto& unsupported:{"bolt","pin"}) {
        SCOPED_TRACE(unsupported);
        EXPECT_THROW(invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string("insert-sleeve")},
            {"parameters",ObjectMap{{"lockMethod",std::string(unsupported)}}}}),std::runtime_error);
    }
}

TEST(AssemblyTemplateSDOTest, AssemblyBoundaryExamplesPassNativeContractsAndRejectOverlongCutStock)
{
    Scene scene;
    const auto descriptors=finishedProductStoredAssemblyDescriptors(scene);
    EXPECT_GE(descriptors.size(),21u);
    struct RadiusCase { std::string id,key; std::vector<double> radii; };
    const std::vector<RadiusCase> radiusCases{
        {"bend","bendRadius",{0.01,100000.0}},
        {"flexible-slit-bend-integrated","bendRadius",{5.0,40.0,100000.0}},
        {"segmented-bend","bendRadius",{1.5,40.0,100000.0}},
    };
    for(const auto& item:radiusCases)for(const double radius:item.radii) {
        SCOPED_TRACE(item.id+" radius="+std::to_string(radius));
        auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(item.id));
        parameters[item.key]=radius;
        const auto parametersBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters);
        const auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",item.id},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(product);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",item.id},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",item.id},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(plan.at("parameters")),parametersBefore);
        if(radius==100000.0) {
            const auto dimensions=product.at("parameters").To<ObjectMap>();
            EXPECT_GE(finishedProductNumber(dimensions.at("angle")),1.0);
        }
        for(const auto& value:plan.at("manufacturingParts").To<VariantArray>()) {
            const auto length=finishedProductNumber(value.To<ObjectMap>().at("request").To<ObjectMap>().at("length"));
            EXPECT_GE(length,1.0);
            EXPECT_LE(length,100000.0);
        }
    }
    for(const auto& id:{"end-side-tab-slot","t-profile-insert"}) {
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        auto spans=product.at("spans").To<ObjectMap>();
        auto branch=spans.at("branch").To<ObjectMap>();
        branch["length"]=99988.0;
        spans["branch"]=branch;
        product["spans"]=spans;
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
        bool branchChecked=false;
        for(const auto& value:plan.at("manufacturingParts").To<VariantArray>()) {
            const auto part=value.To<ObjectMap>();
            if(part.at("sourceRole").To<std::string>()!="branch")continue;
            branchChecked=true;
            EXPECT_NEAR(finishedProductNumber(part.at("request").To<ObjectMap>().at("length")),100000.0,1e-7);
        }
        EXPECT_TRUE(branchChecked);
        branch["length"]=99988.001;
        spans["branch"]=branch;
        product["spans"]=spans;
        const auto overlong=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}});
        EXPECT_FALSE(overlong.at("applicable").To<bool>());
        EXPECT_FALSE(overlong.at("reason").To<std::string>().empty());
        EXPECT_THROW(invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}}),std::runtime_error);
    }
    const ObjectMap fourParameters{{"miterAngle",80.0}};
    const auto fourProduct=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
        {"templateId",std::string("four-end-end-end-end")},{"parameters",fourParameters}})
        .at("finishedProduct").To<ObjectMap>();
    const auto fourCheck=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("four-end-end-end-end")},{"finishedProduct",fourProduct},{"parameters",fourParameters}});
    EXPECT_TRUE(fourCheck.at("applicable").To<bool>());
    const auto fourPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("four-end-end-end-end")},{"finishedProduct",fourProduct},{"parameters",fourParameters}});
    EXPECT_EQ(fourPlan.at("manufacturingParts").To<VariantArray>().size(),4u);
    auto miterProduct=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
        {"templateId",std::string("two-end-end-angle")}}).at("finishedProduct").To<ObjectMap>();
    auto dimensions=miterProduct.at("parameters").To<ObjectMap>();
    dimensions["planeRotation"]=90.0;
    miterProduct["parameters"]=dimensions;
    EXPECT_TRUE(invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},{"finishedProduct",miterProduct}}).at("applicable").To<bool>());
    EXPECT_NO_THROW(invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
        {"templateId",std::string("two-end-end-angle")},{"finishedProduct",miterProduct}}));
}

TEST(AssemblyTemplateSDOTest, InactiveRectRadiusDraftsKeepIntegratedStockAndNativeGeometry)
{
    std::ifstream file(finishedProductAssemblyTemplateRoot().parent_path()/"profile/rect/profile.json");
    ASSERT_TRUE(file);
    const std::string json((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto profileDescriptor=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
    ObjectMap profileDefaults;
    for(const auto& value:profileDescriptor.at("parameters").To<VariantArray>()) {
        const auto definition=value.To<ObjectMap>();
        profileDefaults[definition.at("key").To<std::string>()]=definition.at("defaultValue");
    }
    Scene scene;
    for(const auto& id:{"bend","flexible-slit-bend-integrated","segmented-bend",
            "node-v-notch-integrated","node-edge-arc-integrated","node-embedded-arc-integrated"}) {
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        auto baseline=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        auto spans=baseline.at("spans").To<ObjectMap>();
        for(auto& [role,value]:spans) {
            auto span=value.To<ObjectMap>();
            auto complete=profileDefaults;
            for(const auto& [key,parameter]:span.at("parameters").To<ObjectMap>())complete[key]=parameter;
            span["parameters"]=complete;
            value=span;
        }
        baseline["spans"]=spans;
        auto changed=baseline;
        auto branch=spans.at("armB").To<ObjectMap>();
        auto draft=branch.at("parameters").To<ObjectMap>();
        draft["useOuterRadii"]=false;
        draft["useInnerRadii"]=false;
        draft["outerRadius1"]=4.0;
        draft["innerRadius1"]=2.0;
        branch["parameters"]=draft;
        spans["armB"]=branch;
        changed["spans"]=spans;
        const auto changedBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(changed);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",changed},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto baselinePlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",baseline},{"parameters",parameters}});
        const auto changedPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",changed},{"parameters",parameters}});
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(changedPlan.at("finishedProduct")),changedBefore);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(changedPlan.at("parameters")),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters));
        const auto baselineParts=baselinePlan.at("manufacturingParts").To<VariantArray>();
        const auto changedParts=changedPlan.at("manufacturingParts").To<VariantArray>();
        ASSERT_EQ(baselineParts.size(),changedParts.size());
        for(std::size_t index=0;index<baselineParts.size();++index) {
            const auto prefix="inactive-rect-"+std::string(id)+"-"+std::to_string(index);
            const auto original=previewAssemblyManufacturingPart(scene,
                baselineParts[index].To<ObjectMap>().at("request").To<ObjectMap>(),prefix+"-original");
            const auto modified=previewAssemblyManufacturingPart(scene,
                changedParts[index].To<ObjectMap>().at("request").To<ObjectMap>(),prefix+"-modified");
            ASSERT_FALSE(original.contains("resultError"));
            ASSERT_FALSE(modified.contains("resultError"));
            ASSERT_TRUE(original.at("resultValid").To<bool>());
            ASSERT_TRUE(modified.at("resultValid").To<bool>());
            const auto originalShape=finishedProductNativeShape(scene,original);
            const auto modifiedShape=finishedProductNativeShape(scene,modified);
            GProp_GProps originalVolume,modifiedVolume,commonVolume;
            BRepGProp::VolumeProperties(originalShape,originalVolume);
            BRepGProp::VolumeProperties(modifiedShape,modifiedVolume);
            BRepAlgoAPI_Common common(originalShape,modifiedShape);
            ASSERT_TRUE(common.IsDone());
            BRepGProp::VolumeProperties(common.Shape(),commonVolume);
            EXPECT_NEAR(originalVolume.Mass(),modifiedVolume.Mass(),1e-4);
            EXPECT_NEAR(commonVolume.Mass(),originalVolume.Mass(),1e-4);
        }
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(changed),changedBefore);
    }
}

TEST(AssemblyTemplateSDOTest, IndependentEqualCornerRadiiKeepFormedMeshesAndNativeBlanks)
{
    std::ifstream file(finishedProductAssemblyTemplateRoot().parent_path()/"profile/rect/profile.json");
    ASSERT_TRUE(file);
    const std::string json((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto profileDescriptor=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(json).To<ObjectMap>();
    ObjectMap profileDefaults;
    for(const auto& value:profileDescriptor.at("parameters").To<VariantArray>()) {
        const auto definition=value.To<ObjectMap>();
        profileDefaults[definition.at("key").To<std::string>()]=definition.at("defaultValue");
    }
    Scene scene;
    for(const auto& id:{"flexible-slit-bend-integrated","segmented-bend","node-embedded-arc-integrated"}) {
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        auto baseline=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        auto spans=baseline.at("spans").To<ObjectMap>();
        for(auto& [role,value]:spans) {
            auto span=value.To<ObjectMap>();
            auto section=profileDefaults;
            for(const auto& [key,parameter]:span.at("parameters").To<ObjectMap>())section[key]=parameter;
            section["cornerRadius"]=7.0;
            section["innerRadius"]=5.0;
            section["useOuterRadii"]=false;
            section["useInnerRadii"]=false;
            span["parameters"]=section;
            value=span;
        }
        baseline["spans"]=spans;
        auto independent=baseline;
        for(auto& [role,value]:spans) {
            auto span=value.To<ObjectMap>();
            auto section=span.at("parameters").To<ObjectMap>();
            section["useOuterRadii"]=true;
            section["useInnerRadii"]=true;
            for(int corner=1;corner<=4;++corner) {
                section["outerRadius"+std::to_string(corner)]=7.0;
                section["innerRadius"+std::to_string(corner)]=5.0;
            }
            section["cornerRadius"]=role=="armA"?3.0:6.0;
            section["innerRadius"]=role=="armA"?1.0:4.0;
            span["parameters"]=section;
            value=span;
        }
        independent["spans"]=spans;
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(independent);
        const auto check=invoke(scene,"CheckAssemblyTemplateApplicability",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",independent},{"parameters",parameters}});
        ASSERT_TRUE(check.at("applicable").To<bool>())
            <<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(check);
        const auto baselinePlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",baseline},{"parameters",parameters}});
        const auto independentPlan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"finishedProduct",independent},{"parameters",parameters}});
        ASSERT_TRUE(baselinePlan.contains("formedPreviewMesh"));
        ASSERT_TRUE(independentPlan.contains("formedPreviewMesh"));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(independentPlan.at("formedPreviewMesh")),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(baselinePlan.at("formedPreviewMesh")));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(independentPlan.at("finishedProduct")),before);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(independentPlan.at("parameters")),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(parameters));
        finishedProductChecksNativeBlanks(scene,independentPlan,"independent-equal-radii-"+std::string(id));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(independent),before);
    }
}

TEST(AssemblyTemplateSDOTest, ManufacturingOnlyRetainsCompleteRequestsAndSkipsFormedMeshes)
{
    Scene scene;
    for(const auto* id:{"flexible-slit-bend-integrated","segmented-bend","node-embedded-arc-integrated"}) {
        SCOPED_TRACE(id);
        const auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        const auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        ObjectMap payload{{"templateId",std::string(id)},{"finishedProduct",product},{"parameters",parameters}};
        const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload);
        const auto full=invoke(scene,"ResolveAssemblyTemplatePreview",payload);
        payload["manufacturingOnly"]=true;
        const auto liteBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload);
        const auto lite=invoke(scene,"ResolveAssemblyTemplatePreview",payload);
        ASSERT_TRUE(full.contains("formedPreviewMesh"));
        EXPECT_FALSE(lite.contains("formedPreviewMesh"));
        auto expected=full;
        expected.erase("formedPreviewMesh");
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(lite),
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(expected));
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload),liteBefore);
        payload.erase("manufacturingOnly");
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload),before);
    }
}

TEST(AssemblyTemplateSDOTest, OptimizedManufacturingPreviewRejectsDisconnectedAndEmptyResults)
{
    Scene scene;
    const ObjectMap profileRef{{"scope",std::string("system")},{"id",std::string("round")}};
    const ObjectMap round40{{"width",40.0},{"wallThickness",2.0}};
    ObjectMap feature{{"id",std::string("severing-openings")},{"toolTarget",std::string("part")},
        {"recordKind",std::string("branch")},{"toolRef",ObjectMap{{"id",std::string("branch-profile")}}},
        {"toolParameters",ObjectMap{}},{"angle",90.0},{"azimuth",0.0},{"direction",std::string("through")},
        {"section",ObjectMap{{"profileRef",profileRef},{"parameters",ObjectMap{{"width",60.0},{"wallThickness",2.0}}}}},
        {"reference",std::string("center")},{"layoutReference",std::string("center")},
        {"station",100.0},{"face",std::string("round")},{"layoutDatum",std::string("base")},
        {"arrayCount",2ull},{"arrayPitch",100.0},{"arrayOffsets",VariantArray{0.0,100.0}},
        {"rowCount",1ull},{"rowOffsets",VariantArray{0.0}}};
    ObjectMap planned{{"profileRef",profileRef},{"parameters",round40},{"length",1000.0},
        {"features",VariantArray{feature}},{"ends",ObjectMap{}},{"diagnosticBooleanPreview",true}};
    const auto before=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(planned);
    const auto diagnostic=previewAssemblyManufacturingPart(scene,planned,"optimized-disconnected-old");
    ASSERT_TRUE(diagnostic.at("previewComputed").To<bool>());
    EXPECT_FALSE(diagnostic.at("resultValid").To<bool>());
    EXPECT_EQ(finishedProductNumber(diagnostic.at("solidCount")),3.0);
    try {
        invoke(scene,"PreviewAssemblyManufacturingPart",planned);
        FAIL()<<"Disconnected result was accepted";
    } catch(const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("3"),std::string::npos)<<error.what();
    }
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(planned),before);
    auto removal=end("end-profile",{},0,
        ObjectMap{{"profileRef",profileRef},{"parameters",ObjectMap{{"width",100.0},{"wallThickness",2.0}}}});
    removal["angle"]=0.0;
    planned["features"]=VariantArray{};
    planned["ends"]=ObjectMap{{"start",removal}};
    const auto emptyBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(planned);
    const auto empty=previewAssemblyManufacturingPart(scene,planned,"optimized-empty-old");
    ASSERT_TRUE(empty.at("previewComputed").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(empty);
    EXPECT_FALSE(empty.at("resultValid").To<bool>());
    EXPECT_EQ(finishedProductNumber(empty.at("solidCount")),0.0);
    EXPECT_THROW(invoke(scene,"PreviewAssemblyManufacturingPart",planned),std::exception);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(planned),emptyBefore);
}

TEST(AssemblyTemplateSDOTest, OptimizedManufacturingPreviewPerformanceProbe)
{
    struct InvocationLoggingGuard {
        bool previous=logInvocations;
        InvocationLoggingGuard(){logInvocations=false;}
        ~InvocationLoggingGuard(){logInvocations=previous;}
    } loggingGuard;
    // Both timed calls receive the same already resolved native payload.
    // Profile/section evaluation and fixture setup are outside the stopwatch.
    // Unique preview identities force real resource publication on every run.
    for(const auto* id:{"two-end-end-angle","wrap-a-over-b","segmented-bend","flexible-slit-bend-integrated"}) {
        SCOPED_TRACE(id);
        Scene scene;
        auto parameters=finishedProductProcessDefaults(finishedProductAssemblyDescriptor(id));
        if(std::string(id)=="wrap-a-over-b") {
            parameters["maleFemale"]=true;
            parameters["pairCount"]=std::string("four");
        }
        const auto product=invoke(scene,"GetAssemblyTemplateExampleProduct",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters}}).at("finishedProduct").To<ObjectMap>();
        const auto plan=invoke(scene,"ResolveAssemblyTemplatePreview",ObjectMap{
            {"templateId",std::string(id)},{"parameters",parameters},{"finishedProduct",product},{"manufacturingOnly",true}});
        const auto planned=resolveAssemblyRequestSections(plan.at("manufacturingParts").To<VariantArray>().front()
            .To<ObjectMap>().at("request").To<ObjectMap>());
        const auto reference=planned.at("profileRef").To<ObjectMap>();
        ObjectMap payload{{"name",std::string("装配下料性能对照")},{"quantity",1ull},
            {"drawing",ObjectMap{{"schemaVersion",1ull},{"length",planned.at("length")},
                {"section",section(systemProfile(reference.at("id").To<std::string>(),planned.at("parameters").To<ObjectMap>()))}}},
            {"features",planned.at("features")},{"ends",planned.at("ends")},{"diagnosticBooleanPreview",true}};
        const auto original=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload);
        std::array<double,3> oldTimes{},newTimes{};
        VariantArray samples;
        for(int repeat=-1;repeat<3;++repeat) {
            ObjectMap oldPreview,newPreview;
            double oldMilliseconds=0,newMilliseconds=0;
            const auto run=[&](bool optimized) {
                auto input=payload;
                input["previewResourceKey"]=std::string("performance-")+id+"-"+(repeat<0?"warm":std::to_string(repeat))
                    +(optimized?"-new":"-old");
                const auto started=std::chrono::steady_clock::now();
                const auto result=invoke(scene,optimized?"PreviewAssemblyManufacturingPart":"PreviewPunchWizard",input);
                const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                if(optimized){newMilliseconds=elapsed;newPreview=result;}
                else{oldMilliseconds=elapsed;oldPreview=result;}
            };
            // Alternate ordering after warming both paths once.
            run(repeat%2==0);
            run(repeat%2!=0);
            ASSERT_TRUE(oldPreview.at("resultValid").To<bool>());
            ASSERT_TRUE(newPreview.at("resultValid").To<bool>());
            if(repeat<0)continue;
            oldTimes[repeat]=oldMilliseconds;
            newTimes[repeat]=newMilliseconds;
            EXPECT_GT(oldMilliseconds,0.0);
            EXPECT_GT(newMilliseconds,0.0);
            samples.emplace_back(ObjectMap{{"oldWallMs",oldMilliseconds},{"newWallMs",newMilliseconds},
                {"newPhasesMs",newPreview.at("performanceMs")}});
        }
        const auto median=[](const std::array<double,3>& values) {
            return values[0]+values[1]+values[2]-std::min(values[0],std::min(values[1],values[2]))
                -std::max(values[0],std::max(values[1],values[2]));
        };
        const double oldMedian=median(oldTimes),newMedian=median(newTimes);
        std::cout<<"ASSEMBLY_PERFORMANCE "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
            {"templateId",std::string(id)},{"samples",samples},{"oldMedianMs",oldMedian},{"newMedianMs",newMedian},
            {"ratio",newMedian/oldMedian}})<<std::endl;
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(payload),original);
    }
}
