namespace {
using DisassemblyCountRun=iCAX::TubeDesigner::CGenerationRunComponent;
std::shared_ptr<DisassemblyCountRun> disassemblyCountRun(Scene& scene,const ObjectMap& product) {
    const auto id=iCAX::Data::uuid::from_string(product.at("activeGenerationRunId").To<std::string>());
    if(!id)throw std::runtime_error("Missing generation run identity");
    const auto entity=scene.Database().GetEntity(*id);
    if(!entity)throw std::runtime_error("Missing generation run entity");
    const auto run=std::dynamic_pointer_cast<DisassemblyCountRun>(entity->GetComponent(DisassemblyCountRun::S_ClassName));
    if(!run)throw std::runtime_error("Missing generation run component");
    return run;
}
ObjectMap disassemblyCountProduct(Scene& scene,const std::string& mode="segment_weld") {
    const ObjectMap input{{"faceType",std::string("single")},{"accessDoorEnabled",false},
        {"frameManufacturingMode",mode},{"frameLayout",std::string("four_sides")},
        {"infillPattern",std::string("grid")}};
    // Exercise the library preview used before adding a real product. This
    // API itself does not commit a product or generation run.
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},{"parameters",input}});
    EXPECT_FALSE(preview.at("items").To<VariantArray>().empty());
    auto request=preview.at("parameters").To<ObjectMap>();
    request["templateId"]=std::string("single-face-security-window");
    request["receiptOnly"]=true;
    const auto generated=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>();
    return generated.at("product").To<ObjectMap>();
}
ObjectMap disassemblyCountRequest(const ObjectMap& product,const ObjectMap* updated=nullptr) {
    ObjectMap request{{"productEntityIds",VariantArray{product.at("entityId")}}};
    if(updated)request["productParametersByEntityId"]=ObjectMap{
        {product.at("entityId").To<std::string>(),*updated}};
    return request;
}
ObjectMap disassemblyCountRows(const ObjectMap& snapshot) {
    ObjectMap result;
    for(const auto& value:snapshot.at("parts").To<VariantArray>()) {
        const auto part=value.To<ObjectMap>();
        result[part.at("stableKey").To<std::string>()]=ObjectMap{
            {"entityId",part.at("entityId")},{"geometry",part.at("manufacturingGeometryResourceId")},
            {"version",part.at("manufacturingGeometryResourceVersion")}};
    }
    return result;
}
ObjectMap disassemblyCountIdentities(const ObjectMap& snapshot) {
    ObjectMap result;
    for(const auto& value:std::get<VariantArray>(snapshot.at("parts").m_Value)) {
        const auto& part=std::get<ObjectMap>(value.m_Value);
        result[part.at("stableKey").To<std::string>()]=part.at("entityId");
    }
    return result;
}
std::map<std::string,double> disassemblyCountVolumes(Scene& scene,const ObjectMap& snapshot) {
    std::map<std::string,double> values;
    for(const auto& value:std::get<VariantArray>(snapshot.at("parts").m_Value)) {
        const auto& part=std::get<ObjectMap>(value.m_Value);
        const auto resource=scene.Resources().Get<BRepModel>(part.at("manufacturingGeometryResourceId").To<std::string>(),
            static_cast<std::uint64_t>(finishedProductNumber(part.at("manufacturingGeometryResourceVersion"))));
        if(!resource)throw std::runtime_error("Missing manufacturing geometry");
        const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
        EXPECT_TRUE(shape.bOK);
        GProp_GProps properties;BRepGProp::VolumeProperties(shape.Shape,properties);
        values[part.at("stableKey").To<std::string>()]=properties.Mass();
    }
    return values;
}
std::size_t disassemblyCountFrames(const ObjectMap& snapshot) {
    std::size_t result=0;
    for(const auto& value:snapshot.at("parts").To<VariantArray>())
        if(value.To<ObjectMap>().at("stableKey").To<std::string>().starts_with("outer_frame."))
            result+=value.To<ObjectMap>().at("unitQuantity").To<unsigned long long>();
    return result;
}
std::size_t disassemblyCountUnitQuantity(const ObjectMap& snapshot) {
    std::size_t quantity=0;
    for(const auto& value:snapshot.at("parts").To<VariantArray>())
        quantity+=value.To<ObjectMap>().at("unitQuantity").To<unsigned long long>();
    return quantity;
}
void disassemblyCountChecksMemberLinks(const ObjectMap& snapshot,bool requireSplitFrameLinks=false) {
    std::set<std::string> partIds;
    std::map<std::string,std::string> partIdsByKey;
    for(const auto& value:snapshot.at("parts").To<VariantArray>()) {
        const auto part=value.To<ObjectMap>();
        const auto id=part.at("entityId").To<std::string>();
        partIds.insert(id);
        partIdsByKey.emplace(part.at("stableKey").To<std::string>(),id);
        for(const auto& source:part.at("properties").To<ObjectMap>().at("manufacturing.sourceMembers").To<VariantArray>())
            partIdsByKey[source.To<ObjectMap>().at("itemKey").To<std::string>()]=id;
    }
    std::size_t splitFrameLinks=0;
    for(const auto& value:snapshot.at("members").To<VariantArray>()) {
        const auto member=value.To<ObjectMap>();
        const auto link=member.at("manufacturingPartId").To<std::string>();
        const auto id=iCAX::Data::uuid::from_string(link);
        EXPECT_TRUE(link.empty()||(id&&(id->is_nil()||partIds.contains(link))))
            <<member.at("stableKey").To<std::string>()<<" link="<<link;
        const auto key=member.at("stableKey").To<std::string>();
        if(requireSplitFrameLinks&&key.starts_with("outer_frame.")) {
            const auto expected=partIdsByKey.find(key);
            ASSERT_NE(expected,partIdsByKey.end());
            EXPECT_EQ(link,expected->second);
            ++splitFrameLinks;
        }
    }
    if(requireSplitFrameLinks)EXPECT_EQ(splitFrameLinks,4u);
}
void disassemblyCountChecksParts(Scene& scene,const ObjectMap& snapshot,const DisassemblyCountRun& run) {
    const auto parts=snapshot.at("parts").To<VariantArray>();
    EXPECT_EQ(parts.size(),run.GetPartCount());
    const auto document=run.GetManufacturingModel();
    ASSERT_FALSE(document.empty());
    const auto declared=document.at("extensions").To<ObjectMap>();
    EXPECT_EQ(finishedProductNumber(declared.at("tubeDesigner.manufacturingPartCount")),static_cast<double>(parts.size()));
    const auto outputs=document.at("outputs").To<VariantArray>();
    ASSERT_EQ(outputs.size(),1u);
    EXPECT_EQ(outputs.front().To<ObjectMap>().at("items").To<VariantArray>().size(),parts.size());
    EXPECT_FALSE(document.at("parameters").To<ObjectMap>().contains("tubeDesigner.validatedManufacturingOnlyParameterKeys"));
    disassemblyCountChecksMemberLinks(snapshot);
    for(const auto& value:parts) {
        const auto part=value.To<ObjectMap>();
        const auto model=scene.Resources().Get<BRepModel>(part.at("manufacturingGeometryResourceId").To<std::string>(),
            static_cast<std::uint64_t>(finishedProductNumber(part.at("manufacturingGeometryResourceVersion"))));
        ASSERT_TRUE(model)<<part.at("stableKey").To<std::string>();
        const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
        ASSERT_TRUE(shape.bOK);
        EXPECT_TRUE(BRepCheck_Analyzer(shape.Shape).IsValid());
        std::size_t solids=0;
        for(TopExp_Explorer explorer(shape.Shape,TopAbs_SOLID);explorer.More();explorer.Next())++solids;
        EXPECT_EQ(solids,1u)<<part.at("stableKey").To<std::string>();
    }
}
}

TEST(ProductDisassemblyCountSDO, FortyOneTabsKeepsRecipesUndoRedoAndClearanceAfterReopen)
{
    Scene scene;
    ObjectMap timings;
    const auto generationStart=std::chrono::steady_clock::now();
    const auto initial=invoke(scene,"GeneratePreview",ObjectMap{
        {"templateId",std::string("single-face-security-window")},
        {"frameManufacturingMode",std::string("segment_weld")},
        {"frameJoinType",std::string("miter_45")},
        {"horizontalEndConnection",std::string("tabs")},
        {"verticalEndConnection",std::string("tabs")}
    }).at("tubeDesigner").To<ObjectMap>();
    timings["generateProduct"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-generationStart).count();
    EXPECT_LE(timings.at("generateProduct").To<double>(),3.0);
    const auto product=initial.at("product").To<ObjectMap>();
    const auto run=disassemblyCountRun(scene,product);
    const auto display=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel());
    const auto timed=[&](Scene& target,const char* phase,const ObjectMap& request) {
        const auto start=std::chrono::steady_clock::now();
        auto snapshot=invoke(target,"DisassembleSelected",request).at("tubeDesigner").To<ObjectMap>();
        const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        timings[phase]=seconds;
        char* text=nullptr;std::size_t length=0;
        (void)_dupenv_s(&text,&length,"ICAX_DISASSEMBLY_BUDGET_SECONDS");
        const auto budget=text?std::strtod(text,nullptr):10.0;
        std::free(text);EXPECT_LE(seconds,budget)<<phase;
        EXPECT_EQ(disassemblyCountUnitQuantity(snapshot),41u)<<phase;
        EXPECT_LT(snapshot.at("parts").To<VariantArray>().size(),41u)<<phase;
        disassemblyCountChecksMemberLinks(snapshot,true);
        return snapshot;
    };
    const auto first=timed(scene,"cold",disassemblyCountRequest(product));
    const auto rows=disassemblyCountRows(first);
    const auto identities=disassemblyCountIdentities(first);
    const auto firstVolumes=disassemblyCountVolumes(scene,first);
    const auto manufacturing=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel());
    const auto frozen=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetAssemblyManufacturingModel());
    const auto native=run->GetManufacturingModel().at("extensions").To<ObjectMap>()
        .at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto stocks=native.at("stocks").To<ObjectMap>();
    ASSERT_EQ(stocks.size(),41u);
    std::set<std::string> stockItems;
    for(const auto& [id,value]:stocks) {
        const auto stock=value.To<ObjectMap>();
        EXPECT_FALSE(id.empty());
        stockItems.insert(stock.at("itemKey").To<std::string>());
        EXPECT_FALSE(stock.at("request").To<ObjectMap>().empty());
        for(const char* name:{"baseGeometry","processedGeometry"}) {
            const auto reference=stock.at(name).To<ObjectMap>();
            EXPECT_TRUE(scene.Resources().Get<BRepModel>(reference.at("url").To<std::string>(),
                static_cast<std::uint64_t>(finishedProductNumber(reference.at("version")))));
        }
    }
    EXPECT_EQ(stockItems.size(),first.at("parts").To<VariantArray>().size());
    ASSERT_TRUE(scene.Database().CanUndo());
    ASSERT_TRUE(scene.Database().Undo());
    const auto listStart=std::chrono::steady_clock::now();
    const auto undone=invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    timings["listProducts"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-listStart).count();
    EXPECT_LE(timings.at("listProducts").To<double>(),3.0);
    EXPECT_TRUE(undone.at("parts").To<VariantArray>().empty());
    EXPECT_TRUE(run->GetAssemblyManufacturingModel().empty());
    ASSERT_TRUE(scene.Database().CanRedo());
    ASSERT_TRUE(scene.Database().Redo());
    const auto redone=invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountRows(redone),rows);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel()),manufacturing);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetAssemblyManufacturingModel()),frozen);
    const auto current=first.at("product").To<ObjectMap>();
    const auto repeated=timed(scene,"warm",disassemblyCountRequest(current));
    // Regeneration publishes new resource versions while retaining each real
    // part's identity and physical result. Undo/redo above restores exact refs.
    EXPECT_EQ(disassemblyCountIdentities(repeated),identities);
    for(const auto& [key,volume]:disassemblyCountVolumes(scene,repeated))
        EXPECT_NEAR(volume,firstVolumes.at(key),1e-6)<<key;
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),display);

    const auto directory=std::filesystem::current_path()/"tmp/product-disassembly-performance";
    std::filesystem::create_directories(directory);
    const auto path=directory/"forty-one-tabs.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="公母加工性能回归";
    const auto saveStart=std::chrono::steady_clock::now();
    file.Save(path,info,scene.Database(),scene.Resources());
    timings["projectSave"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-saveStart).count();
    EXPECT_LE(timings.at("projectSave").To<double>(),3.0);
    Scene reopened(scene.project,scene.GetSceneID(),false);
    const auto openStart=std::chrono::steady_clock::now();
    file.Open(path,reopened.Database(),reopened.Resources());
    timings["projectOpen"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-openStart).count();
    EXPECT_LE(timings.at("projectOpen").To<double>(),3.0);
    const auto restoredRun=disassemblyCountRun(reopened,current);
    const auto restored=timed(reopened,"reopened",disassemblyCountRequest(current));
    EXPECT_EQ(disassemblyCountIdentities(restored),identities);
    for(const auto& [key,volume]:disassemblyCountVolumes(reopened,restored))
        EXPECT_NEAR(volume,firstVolumes.at(key),1e-6)<<key;
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restoredRun->GetNeutralModel()),display);

    const auto volumes=[&](Scene& target,const ObjectMap& snapshot) {
        std::map<std::string,double> values;
        for(const auto& value:snapshot.at("parts").To<VariantArray>()) {
            const auto part=value.To<ObjectMap>();
            const auto resource=target.Resources().Get<BRepModel>(part.at("manufacturingGeometryResourceId").To<std::string>(),
                static_cast<std::uint64_t>(finishedProductNumber(part.at("manufacturingGeometryResourceVersion"))));
            const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*resource);
            EXPECT_TRUE(shape.bOK);
            GProp_GProps properties;BRepGProp::VolumeProperties(shape.Shape,properties);
            values[part.at("stableKey").To<std::string>()]=properties.Mass();
        }
        return values;
    };
    const auto beforeVolumes=volumes(reopened,restored);
    auto changed=current.at("parameters").To<ObjectMap>();changed["assemblyClearance"]=0.2;
    const auto updated=timed(reopened,"clearanceChanged",disassemblyCountRequest(current,&changed));
    const auto afterVolumes=volumes(reopened,updated);
    std::size_t changedParts=0;
    for(const auto& [key,volume]:beforeVolumes)if(std::abs(afterVolumes.at(key)-volume)>0.001)++changedParts;
    EXPECT_GT(changedParts,0u);
    EXPECT_EQ(restoredRun->GetManufacturingParameters().at("assemblyClearance").To<double>(),0.2);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restoredRun->GetNeutralModel()),display);
    disassemblyCountChecksParts(reopened,updated,*restoredRun);
    const auto evidence=std::filesystem::current_path()/"output/tests/disassembly-performance";
    std::filesystem::create_directories(evidence);
    std::ofstream(evidence/"replay.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
        {"passed",!::testing::Test::HasFailure()},{"partCount",41ull},
        {"clearanceChangedParts",static_cast<unsigned long long>(changedParts)},{"seconds",std::move(timings)}});
}

TEST(ProductDisassemblyCountSDO, ManufacturingOnlyFrameCountShrinksGrowsRepeatsAndReopens)
{
    Scene scene;
    const auto product=disassemblyCountProduct(scene);
    const auto run=disassemblyCountRun(scene,product);
    EXPECT_EQ(run->GetPartCount(),0u);
    const auto displayBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel());
    const auto original=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    const auto originalCount=run->GetPartCount();
    const auto originalQuantity=disassemblyCountUnitQuantity(original);
    ASSERT_EQ(disassemblyCountFrames(original),4u);
    disassemblyCountChecksMemberLinks(original,true);
    ASSERT_GT(originalCount,4u);
    auto parameters=product.at("parameters").To<ObjectMap>();
    parameters["frameManufacturingMode"]=std::string("plane_v_notch");
    const auto smaller=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&parameters))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(smaller),1u);
    const auto smallerCount=run->GetPartCount();
    EXPECT_EQ(disassemblyCountUnitQuantity(smaller),originalQuantity-3);
    disassemblyCountChecksParts(scene,smaller,*run);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),displayBefore);
    auto current=smaller.at("product").To<ObjectMap>();
    const auto repeated=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(repeated),1u);
    disassemblyCountChecksParts(scene,repeated,*run);
    const auto sameValues=current.at("parameters").To<ObjectMap>();
    const auto repeatedWithDraft=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&sameValues))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(repeatedWithDraft),1u);
    disassemblyCountChecksParts(scene,repeatedWithDraft,*run);

    const auto directory=std::filesystem::current_path()/"tmp/product-disassembly-count";
    std::filesystem::create_directories(directory);
    const auto path=directory/"continuous-frame.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="制造件数变化回归";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    const auto restoredRun=disassemblyCountRun(reopened,current);
    const auto restoredModel=!restoredRun->GetManufacturingModel().empty()
        ?restoredRun->GetManufacturingModel():restoredRun->GetAssemblyManufacturingModel();
    std::cerr<<"[disassembly-count] reopened run="<<restoredRun->GetPartCount()
        <<" original="<<originalCount<<" expected="<<smallerCount
        <<" manufacturing="<<(restoredModel.empty()?-1.0:finishedProductNumber(restoredModel.at("extensions").To<ObjectMap>()
            .at("tubeDesigner.manufacturingPartCount")))
        <<" generatedInputs="<<restoredRun->GetGeneratedParameters().size()<<'\n';
    ASSERT_FALSE(restoredRun->GetAssemblyManufacturingModel().empty());
    EXPECT_EQ(finishedProductNumber(restoredModel.at("extensions").To<ObjectMap>()
        .at("tubeDesigner.manufacturingPartCount")),static_cast<double>(restoredRun->GetPartCount()));
    const auto openingSnapshot=invoke(reopened,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_FALSE(openingSnapshot.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    const auto restored=invoke(reopened,"DisassembleSelected",disassemblyCountRequest(current))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(restored),1u);
    EXPECT_EQ(restoredRun->GetPartCount(),smallerCount);
    EXPECT_EQ(disassemblyCountUnitQuantity(restored),originalQuantity-3);
    disassemblyCountChecksParts(reopened,restored,*restoredRun);
    auto largerParameters=restored.at("product").To<ObjectMap>().at("parameters").To<ObjectMap>();
    largerParameters["frameManufacturingMode"]=std::string("segment_weld");
    const auto larger=invoke(reopened,"DisassembleSelected",disassemblyCountRequest(current,&largerParameters))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(larger),4u);
    disassemblyCountChecksMemberLinks(larger,true);
    EXPECT_EQ(restoredRun->GetPartCount(),originalCount);
    disassemblyCountChecksParts(reopened,larger,*restoredRun);
    EXPECT_FALSE(restoredRun->GetAssemblyManufacturingModel().empty());
    EXPECT_FALSE(restoredRun->GetManufacturingDefinition().empty());
    EXPECT_EQ(restoredRun->GetManufacturingParameters(),largerParameters);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restoredRun->GetNeutralModel()),displayBefore);
    const auto finalRepeat=invoke(reopened,"DisassembleSelected",disassemblyCountRequest(larger.at("product").To<ObjectMap>()))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(finalRepeat),4u);
    disassemblyCountChecksMemberLinks(finalRepeat,true);
    disassemblyCountChecksParts(reopened,finalRepeat,*restoredRun);
    const auto revertedPath=directory/"reverted-frame.ictd";
    file.Save(revertedPath,info,reopened.Database(),reopened.Resources());
    Scene reverted(scene.project,scene.GetSceneID(),false);
    file.Open(revertedPath,reverted.Database(),reverted.Resources());
    const auto revertedProduct=finalRepeat.at("product").To<ObjectMap>();
    const auto revertedRun=disassemblyCountRun(reverted,revertedProduct);
    EXPECT_EQ(revertedRun->GetPartCount(),originalCount);
    EXPECT_FALSE(revertedRun->GetAssemblyManufacturingModel().empty());
    EXPECT_EQ(revertedRun->GetManufacturingParameters(),largerParameters);
    EXPECT_FALSE(invoke(reverted,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>()
        .at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    const auto revertedSplit=invoke(reverted,"DisassembleSelected",disassemblyCountRequest(revertedProduct))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(revertedSplit),4u);
    disassemblyCountChecksMemberLinks(revertedSplit,true);
    disassemblyCountChecksParts(reverted,revertedSplit,*revertedRun);
}

TEST(ProductDisassemblyCountSDO, InvalidManufacturingUpdateRollsBackRealPartsAndRun)
{
    Scene scene;
    const auto product=disassemblyCountProduct(scene);
    const auto run=disassemblyCountRun(scene,product);
    const auto baseline=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product)).at("tubeDesigner").To<ObjectMap>();
    const auto rows=disassemblyCountRows(baseline);
    const auto manufacturing=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel());
    const auto count=run->GetPartCount();
    const auto parameters=baseline.at("product").To<ObjectMap>().at("parameters").To<ObjectMap>();
    for(const bool invalidEnum:{true,false}) {
        auto invalid=parameters;
        if(invalidEnum)invalid["frameManufacturingMode"]=std::string("invalid_mode");
        else invalid["width"]=finishedProductNumber(parameters.at("width"))+100.0;
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&invalid)),std::exception);
        const auto unchanged=invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
        EXPECT_EQ(disassemblyCountRows(unchanged),rows);
        EXPECT_EQ(unchanged.at("product").To<ObjectMap>().at("parameters"),iCAX::Data::Variant(parameters));
        EXPECT_EQ(run->GetPartCount(),count);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel()),manufacturing);
    }
}

TEST(ProductDisassemblyCountSDO, CorruptRunCountCannotBeBypassedBySameOrChangedDraft)
{
    for(const bool cached:{false,true}) {
        SCOPED_TRACE(cached?"cached manufacturing model":"display run only");
        Scene scene;
        const auto product=disassemblyCountProduct(scene);
        const auto run=disassemblyCountRun(scene,product);
        if(cached)invoke(scene,"DisassembleSelected",disassemblyCountRequest(product));
        const auto baseline=invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
        const auto rows=disassemblyCountRows(baseline);
        const auto oldModel=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel());
        const auto poisoned=run->GetPartCount()+1;
        ASSERT_TRUE(run->SetPartCount(poisoned));
        const auto same=product.at("parameters").To<ObjectMap>();
        auto changed=same;changed["frameManufacturingMode"]=std::string("plane_v_notch");
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(product)),std::exception);
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&same)),std::exception);
        EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&changed)),std::exception);
        const auto after=invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
        EXPECT_EQ(disassemblyCountRows(after),rows);
        EXPECT_EQ(run->GetPartCount(),poisoned);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetManufacturingModel()),oldModel);
    }
}

TEST(ProductDisassemblyCountSDO, CachedManufacturingDifferenceNeedsValidatedParameterMetadata)
{
    Scene scene;
    const auto product=disassemblyCountProduct(scene);
    const auto run=disassemblyCountRun(scene,product);
    auto changed=product.at("parameters").To<ObjectMap>();
    changed["frameManufacturingMode"]=std::string("plane_v_notch");
    const auto smaller=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&changed))
        .at("tubeDesigner").To<ObjectMap>();
    const auto current=smaller.at("product").To<ObjectMap>();
    const auto rows=disassemblyCountRows(smaller);
    const auto valid=run->GetManufacturingModel();
    auto unvalidated=valid;
    auto extensions=unvalidated.at("extensions").To<ObjectMap>();
    extensions.erase("tubeDesigner.validatedManufacturingOnlyParameterKeys");
    unvalidated["extensions"]=extensions;
    ASSERT_TRUE(run->SetManufacturingModel(unvalidated));
    EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current)),std::exception);
    EXPECT_EQ(disassemblyCountRows(invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>()),rows);
    auto wrongGeometry=valid;
    auto parameters=wrongGeometry.at("parameters").To<ObjectMap>();
    parameters["width"]=finishedProductNumber(parameters.at("width"))+100.0;
    wrongGeometry["parameters"]=parameters;
    ASSERT_TRUE(run->SetManufacturingModel(wrongGeometry));
    EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current)),std::exception);
    EXPECT_EQ(disassemblyCountRows(invoke(scene,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>()),rows);
}

TEST(ProductDisassemblyCountSDO, EquivalentIntegerStorageReplaysAndRealStructureDeltaIsRejected)
{
    Scene scene;
    const std::string templateId="modular-guardrail";
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{
        {"templateId",templateId},{"parameters",ObjectMap{
            {"layout",std::string("straight")},{"barOrientation",std::string("vertical")},
            {"barDistribution",std::string("manual_count")},{"fixedBarCount",2ll},
            {"sideLength1",600.0},{"sideBayCount1",1ll}}}});
    auto request=preview.at("parameters").To<ObjectMap>();
    request["templateId"]=templateId;request["receiptOnly"]=true;
    const auto product=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>()
        .at("product").To<ObjectMap>();
    const auto run=disassemblyCountRun(scene,product);
    const auto baseline=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product))
        .at("tubeDesigner").To<ObjectMap>();
    const auto count=run->GetPartCount();
    auto materialParameters=product.at("parameters").To<ObjectMap>();
    materialParameters["materialGrade"]=std::string("304 stainless steel");
    const auto materialUpdated=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&materialParameters))
        .at("tubeDesigner").To<ObjectMap>();
    const auto current=materialUpdated.at("product").To<ObjectMap>();
    // A structural integer is neither a manufacturing-only key nor a draft
    // hidden by an inactive parent. Pure display geometry has no parameter
    // echo; change storage alone in its authoritative host input snapshot.
    const auto display=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel());
    auto displayParameters=run->GetGeneratedParameters();
    displayParameters["fixedBarCount"]=2ll;
    ASSERT_TRUE(run->SetGeneratedParameters(displayParameters));
    EXPECT_EQ(run->GetGeneratedParameters(),displayParameters);
    auto manufacturing=run->GetManufacturingModel();
    auto manufacturingParameters=manufacturing.at("parameters").To<ObjectMap>();
    manufacturingParameters["fixedBarCount"]=2ull;manufacturing["parameters"]=manufacturingParameters;
    ASSERT_TRUE(run->SetManufacturingModel(manufacturing));
    ASSERT_TRUE(run->SetManufacturingParameters(manufacturingParameters));
    EXPECT_EQ(run->GetManufacturingParameters(),manufacturingParameters);
    ASSERT_FALSE(displayParameters.at("fixedBarCount")==manufacturingParameters.at("fixedBarCount"));
    const auto repeated=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(run->GetPartCount(),count);
    EXPECT_EQ(repeated.at("parts").To<VariantArray>().size(),baseline.at("parts").To<VariantArray>().size());
    for(const auto& integer:VariantArray{2ll,2ull}) {
        auto equal=current.at("parameters").To<ObjectMap>();equal["fixedBarCount"]=integer;
        const auto replay=invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&equal))
            .at("tubeDesigner").To<ObjectMap>();
        EXPECT_EQ(replay.at("parts").To<VariantArray>().size(),count);
    }
    auto changed=current.at("parameters").To<ObjectMap>();changed["fixedBarCount"]=3ull;
    EXPECT_THROW(invoke(scene,"DisassembleSelected",disassemblyCountRequest(current,&changed)),std::exception);
    EXPECT_EQ(run->GetPartCount(),count);
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(run->GetNeutralModel()),display);

    const auto directory=std::filesystem::current_path()/"tmp/product-disassembly-count";
    std::filesystem::create_directories(directory);
    const auto path=directory/"equivalent-integer.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="同值整数存储回归";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    const auto restored=invoke(reopened,"DisassembleSelected",disassemblyCountRequest(current))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(restored.at("parts").To<VariantArray>().size(),count);
    EXPECT_THROW(invoke(reopened,"DisassembleSelected",disassemblyCountRequest(current,&changed)),std::exception);
    EXPECT_EQ(disassemblyCountRun(reopened,current)->GetPartCount(),count);
}

TEST(ProductDisassemblyCountSDO, StoredTaskRecipesNeverBecomeProductManufacturingAuthority)
{
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape,properties);
        return properties.Mass();
    };
    Scene scene;
    const auto product=disassemblyCountProduct(scene);
    const auto originalParameters=product.at("parameters").To<ObjectMap>();
    const auto run=disassemblyCountRun(scene,product);
    const auto original=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product))
        .at("tubeDesigner").To<ObjectMap>();
    const auto originalCount=run->GetPartCount();
    const auto originalRecipe=run->GetManufacturingModel();
    const auto originalQuantity=disassemblyCountUnitQuantity(original);
    // Preserve an actual independent nesting BRep while product recipes change.
    const auto staged=invoke(scene,"StageNestingParts",ObjectMap{{"partEntityIds",VariantArray{
        original.at("parts").To<VariantArray>().front().To<ObjectMap>().at("entityId")}}});
    const auto nestingId=staged.at("partEntityIds").To<VariantArray>().front().To<std::string>();
    const auto nestingPart=part(scene,nestingId);
    const auto nestingGeometry=nestingPart->GetManufacturingGeometryResourceID();
    const auto nestingVersion=nestingPart->GetManufacturingGeometryResourceVersion();
    const auto nestingBRep=scene.Resources().Get<BRepModel>(nestingGeometry,nestingVersion);
    ASSERT_TRUE(nestingBRep);
    const auto nestingShape=iCAX::OpenCascade::BuildOpenCascadeShape(*nestingBRep);
    ASSERT_TRUE(nestingShape.bOK);
    const auto nestingVolume=volume(nestingShape.Shape);
    const auto setTaskRecipe=[&](Scene& target,const ObjectMap& recipe) {
        const auto root=std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
            target.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName));
        if(!root)throw std::runtime_error("Missing nesting root");
        auto task=root->GetNestingTask();
        task["manufacturingModels"]=ObjectMap{{product.at("activeGenerationRunId").To<std::string>(),recipe}};
        if(!root->SetNestingTask(task))throw std::runtime_error("Cannot inject unrelated nesting task recipe");
    };
    const auto checksNesting=[&](Scene& target) {
        const auto saved=part(target,nestingId);
        EXPECT_EQ(saved->GetManufacturingGeometryResourceID(),nestingGeometry);
        EXPECT_EQ(saved->GetManufacturingGeometryResourceVersion(),nestingVersion);
        const auto model=target.Resources().Get<BRepModel>(nestingGeometry,nestingVersion);
        ASSERT_TRUE(model);
        const auto shape=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
        ASSERT_TRUE(shape.bOK);
        EXPECT_TRUE(BRepCheck_Analyzer(shape.Shape).IsValid());
        EXPECT_NEAR(volume(shape.Shape),nestingVolume,1e-5);
    };
    const auto directory=std::filesystem::current_path()/"tmp/product-disassembly-count";
    std::filesystem::create_directories(directory);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="排样任务配方防污染回归";
    const auto reopen=[&](Scene& target,const std::string& name) {
        const auto committed=disassemblyCountRun(target,product);
        const auto authoritative=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(committed->GetAssemblyManufacturingModel());
        EXPECT_FALSE(committed->GetAssemblyManufacturingModel().empty());
        const auto hostParameters=committed->GetManufacturingParameters();
        const auto display=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(committed->GetNeutralModel());
        const auto path=directory/name;
        file.Save(path,info,target.Database(),target.Resources());
        auto opened=std::make_unique<Scene>(scene.project,scene.GetSceneID(),false);
        file.Open(path,opened->Database(),opened->Resources());
        const auto restored=disassemblyCountRun(*opened,product);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restored->GetAssemblyManufacturingModel()),authoritative);
        EXPECT_EQ(restored->GetManufacturingParameters(),hostParameters);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restored->GetNeutralModel()),display);
        return opened;
    };
    // A valid task graph with matching item count and different manufacturing
    // inputs must not replace the product's authoritative persisted recipe.
    auto differentParameters=originalParameters;
    differentParameters["assemblyClearance"]=finishedProductNumber(originalParameters.at("assemblyClearance"))+0.2;
    invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&differentParameters));
    const auto sameCountDifferentParameters=run->GetManufacturingModel();
    ASSERT_EQ(run->GetPartCount(),originalCount);
    invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&originalParameters));
    auto continuous=originalParameters;continuous["frameManufacturingMode"]=std::string("plane_v_notch");
    invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&continuous));
    const auto oldContinuous=run->GetManufacturingModel();
    auto currentContinuous=continuous;
    currentContinuous["assemblyClearance"]=differentParameters.at("assemblyClearance");
    const auto changed=invoke(scene,"DisassembleSelected",disassemblyCountRequest(product,&currentContinuous))
        .at("tubeDesigner").To<ObjectMap>();
    ASSERT_EQ(disassemblyCountUnitQuantity(changed),originalQuantity-3);
    setTaskRecipe(scene,oldContinuous);
    auto opened=reopen(scene,"task-current-persistent.ictd");
    auto openedRun=disassemblyCountRun(*opened,product);
    const auto firstList=invoke(*opened,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_FALSE(firstList.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    EXPECT_EQ(openedRun->GetManufacturingModel().at("parameters"),iCAX::Data::Variant(currentContinuous));
    const auto stillContinuous=invoke(*opened,"DisassembleSelected",disassemblyCountRequest(changed.at("product").To<ObjectMap>()))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(stillContinuous),1u);
    checksNesting(*opened);

    const auto reverted=invoke(*opened,"DisassembleSelected",disassemblyCountRequest(product,&originalParameters))
        .at("tubeDesigner").To<ObjectMap>();
    ASSERT_FALSE(openedRun->GetAssemblyManufacturingModel().empty());
    EXPECT_EQ(openedRun->GetManufacturingParameters(),originalParameters);
    setTaskRecipe(*opened,oldContinuous);
    auto wrongCount=reopen(*opened,"task-stale-count.ictd");
    auto wrongCountRun=disassemblyCountRun(*wrongCount,product);
    const auto countList=invoke(*wrongCount,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_FALSE(countList.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    EXPECT_FALSE(wrongCountRun->GetManufacturingModel().empty());
    EXPECT_EQ(wrongCountRun->GetManufacturingModel().at("parameters"),Variant(originalParameters));
    EXPECT_EQ(wrongCountRun->GetPartCount(),originalCount);
    const auto split=invoke(*wrongCount,"DisassembleSelected",disassemblyCountRequest(reverted.at("product").To<ObjectMap>()))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(disassemblyCountFrames(split),4u);
    checksNesting(*wrongCount);

    setTaskRecipe(*wrongCount,sameCountDifferentParameters);
    auto wrongInputs=reopen(*wrongCount,"task-stale-inputs.ictd");
    auto wrongInputsRun=disassemblyCountRun(*wrongInputs,product);
    const auto inputList=invoke(*wrongInputs,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_FALSE(inputList.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    EXPECT_FALSE(wrongInputsRun->GetManufacturingModel().empty());
    EXPECT_EQ(wrongInputsRun->GetManufacturingModel().at("parameters"),Variant(originalParameters));
    const auto correct=invoke(*wrongInputs,"DisassembleSelected",disassemblyCountRequest(product))
        .at("tubeDesigner").To<ObjectMap>();
    EXPECT_EQ(correct.at("parts").To<VariantArray>().size(),originalCount);
    EXPECT_EQ(wrongInputsRun->GetManufacturingModel().at("parameters"),iCAX::Data::Variant(originalParameters));
    checksNesting(*wrongInputs);

    setTaskRecipe(*wrongInputs,originalRecipe);
    auto sameInputsOpened=reopen(*wrongInputs,"task-same-inputs.ictd");
    const auto sameInputsList=invoke(*sameInputsOpened,"List",ObjectMap{}).at("tubeDesigner").To<ObjectMap>();
    EXPECT_FALSE(sameInputsList.at("product").To<ObjectMap>().at("partsOutdated").To<bool>());
    const auto sameInputsRun=disassemblyCountRun(*sameInputsOpened,product);
    EXPECT_FALSE(sameInputsRun->GetManufacturingModel().empty());
    EXPECT_EQ(sameInputsRun->GetManufacturingModel().at("parameters"),Variant(originalParameters));
    EXPECT_EQ(invoke(*sameInputsOpened,"DisassembleSelected",disassemblyCountRequest(product))
        .at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>().size(),originalCount);
    checksNesting(*sameInputsOpened);
}

#include "ProductContinuousRoundTabsSDOTests.inc"
#include "ProductEqualSectionFoldedTabsSDOTests.inc"
#include "ProductSpatialFoldControlSDOTests.inc"
#include "ProductEqualSectionPostChoicesSDOTests.inc"
