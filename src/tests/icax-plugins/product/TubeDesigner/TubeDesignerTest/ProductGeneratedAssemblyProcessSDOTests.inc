ObjectMap generatedSecurityProcessParameters(const std::string& Mode="spatial_v_notch")
{
    return {{"faceType",std::string("three")},{"frameManufacturingMode",Mode},
        {"accessDoorEnabled",false},{"verticalMaximumCenterSpacing",600.0},
        {"sideVerticalMaximumCenterSpacing",600.0},{"topBottomRodMaximumCenterSpacing",600.0}};
}

void checkGeneratedStockAlias(const TopoDS_Shape& actual,const TopoDS_Shape& frozen,
    const TopoDS_Shape& raw,const ObjectMap& stock)
{
    checkSameShape(frozen,actual);
    for(const auto type:{TopAbs_FACE,TopAbs_EDGE}) {
        std::size_t expected=0,found=0;
        for(TopExp_Explorer it(frozen,type);it.More();it.Next())++expected;
        for(TopExp_Explorer it(actual,type);it.More();it.Next())++found;
        EXPECT_EQ(found,expected)<<stock.at("itemKey").To<std::string>();
    }
    const auto profile=stock.at("profile").To<ObjectMap>();
    const auto request=stock.at("request").To<ObjectMap>();
    const double width=profile.at("width").To<double>(),depth=profile.at("depth").To<double>();
    const double wall=profile.at("wallThickness").To<double>();
    bool witnessedRemoval=false;
    const auto probe=[&](double station,double localY,double localZ,double rotation) {
        const auto expected=assemblyProcessPlanMaterial(frozen,station,localY,localZ,rotation);
        EXPECT_EQ(assemblyProcessPlanMaterial(actual,station,localY,localZ,rotation),expected)
            <<stock.at("itemKey").To<std::string>()<<" station="<<station<<" rotation="<<rotation;
        if(assemblyProcessPlanMaterial(raw,station,localY,localZ,rotation)==TopAbs_IN&&expected==TopAbs_OUT)
            witnessedRemoval=true;
    };
    for(const auto& value:request.at("features").To<VariantArray>()) {
        const auto feature=value.To<ObjectMap>();
        const double station=feature.at("station").To<double>();
        const double rotation=feature.at("rotation").To<double>();
        const double radians=rotation*3.14159265358979323846/180;
        const double halfDepth=std::abs(std::sin(radians))>0.5?width/2:depth/2;
        probe(station,0,halfDepth-wall/2,rotation); // Removed upper wall at each real V station.
        probe(station,0,-halfDepth+0.2*wall,rotation); // Retained bottom material in the cutter frame.
        probe(station+30,0,halfDepth-wall/2,rotation); // Nearby material outside this groove.
    }
    if(request.at("features").To<VariantArray>().empty()) {
        const double length=request.at("length").To<double>();
        probe(0.5,0,-depth/2+wall/2,0);
        probe(length-0.5,0,-depth/2+wall/2,0);
        probe(length/2,0,-depth/2+wall/2,0);
    }
    EXPECT_TRUE(witnessedRemoval)<<stock.at("itemKey").To<std::string>()<<" actual machining disappeared";
}

TEST(ProductGeneratedAssemblyProcessSDO, ThreeFaceSpatialKeepsTenLogicalMembersAndThreeRealStocksAfterReopenAndUpdate)
{
    Scene scene;
    const std::string templateID="single-face-security-window";
    const auto parameters=generatedSecurityProcessParameters();
    const auto preview=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{{"templateId",templateID},{"parameters",parameters}});
    std::size_t displayOuter=0;
    for(const auto& value:preview.at("items").To<VariantArray>())
        if(value.To<ObjectMap>().at("key").To<std::string>().starts_with("outer_frame."))++displayOuter;
    ASSERT_EQ(displayOuter,10u);
    auto generation=preview.at("parameters").To<ObjectMap>();generation["templateId"]=templateID;
    const auto generated=invoke(scene,"GeneratePreview",generation).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    EXPECT_EQ(product.at("parameters"),preview.at("parameters"));
    const auto productID=product.at("entityId").To<std::string>();
    const auto runID=product.at("activeGenerationRunId").To<std::string>();
    const ObjectMap identity{{"productEntityId",productID},{"generationRunId",runID}};
    invoke(scene,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{productID}}});
    const auto planned=invoke(scene,"GetProductAssemblyBindings",identity);
    std::size_t plannedOuter=0;
    for(const auto& value:planned.at("manufacturingPlan").To<ObjectMap>().at("stocks").To<VariantArray>())
        if(value.To<ObjectMap>().at("itemKey").To<std::string>().starts_with("outer_frame."))
            plannedOuter+=value.To<ObjectMap>().at("sourceStockIds").To<VariantArray>().size();
    ASSERT_EQ(plannedOuter,3u);
    const auto listed=invoke(scene,"List",{}).at("tubeDesigner").To<ObjectMap>();
    std::map<std::string,std::string> ids;
    std::map<std::string,std::pair<std::string,unsigned long long>> originalRefs;
    std::size_t manufacturingOuter=0;
    for(const auto& value:listed.at("parts").To<VariantArray>()) {
        const auto row=value.To<ObjectMap>();
        const auto id=row.at("entityId").To<std::string>();const auto component=part(scene,id);
        if(!component->GetStableKey().starts_with("outer_frame."))continue;
        manufacturingOuter+=component->GetQuantity();ids[component->GetStableKey()]=id;
        const auto solid=shape(scene,component);
        ASSERT_TRUE(BRepCheck_Analyzer(solid).IsValid());
        std::size_t count=0;for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
        EXPECT_EQ(count,1u);EXPECT_GT(volume(solid),0);
        Bnd_Box box;BRepBndLib::AddOptimal(solid,box,false,false);box.SetGap(0.0);
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        EXPECT_NEAR(component->GetLength(),x1-x0,1e-5);
        const auto properties=component->GetItemProperties();
        ASSERT_TRUE(properties.contains("manufacturing.rawStockLength"));
        const auto rawLength=properties.at("manufacturing.rawStockLength").To<double>();
        EXPECT_GE(rawLength+1e-5,component->GetLength());
        if(properties.contains("tubeDesigner.assemblyProcessStockLength"))
            EXPECT_NEAR(rawLength,properties.at("tubeDesigner.assemblyProcessStockLength").To<double>(),1e-5);
        EXPECT_NEAR(properties.at("manufacturing.materialDatum").To<ObjectMap>().at("rawStockLength").To<double>(),rawLength,1e-5);
        if(component->GetStableKey().find("post")!=std::string::npos) {
            EXPECT_NEAR(rawLength,1762.0,1e-5);
            EXPECT_NEAR(component->GetLength(),1724.0,1e-5);
            EXPECT_NEAR(x0,19.0,1e-5);EXPECT_NEAR(x1,1743.0,1e-5);
        }
        originalRefs[component->GetStableKey()]={component->GetManufacturingGeometryResourceID(),component->GetManufacturingGeometryResourceVersion()};
    }
    ASSERT_EQ(manufacturingOuter,3u);
    const auto run=std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        scene.Database().GetEntity(iCAX::Data::uuid::from_string(runID).value())->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(run);
    const auto native=run->GetManufacturingModel().at("extensions").To<ObjectMap>().at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    const auto plan=native.at("plan").To<ObjectMap>();
    ASSERT_EQ(plan.at("manufacturingParts").To<VariantArray>().size(),disassemblyCountUnitQuantity(listed));
    ASSERT_EQ(native.at("stocks").To<ObjectMap>().size(),disassemblyCountUnitQuantity(listed));
    ASSERT_EQ(plan.at("nativeContactChecks").To<VariantArray>().size(),8u);
    for(const auto& value:plan.at("nativeContactChecks").To<VariantArray>()) {
        const auto check=value.To<ObjectMap>();
        EXPECT_EQ(check.at("status").To<std::string>(),"pass");
        EXPECT_GT(check.at("contactAreaMm2").To<double>(),0.0);
        EXPECT_LE(check.at("intersectionVolumeMm3").To<double>(),1e-5+1e-7);
    }
    bool repeated=false;
    for(const auto& value:plan.at("manufacturingParts").To<VariantArray>())
        if(value.To<ObjectMap>().at("request").To<ObjectMap>().at("features").To<VariantArray>().size()>1)repeated=true;
    EXPECT_TRUE(repeated);
    for(const auto& [stockID,value]:native.at("stocks").To<ObjectMap>()) {
        const auto stock=value.To<ObjectMap>();
        const auto raw=stock.at("baseGeometry").To<ObjectMap>();
        const auto processed=stock.at("processedGeometry").To<ObjectMap>();
        const auto rawData=scene.Resources().Get<BRepModel>(raw.at("url").To<std::string>(),raw.at("version").To<unsigned long long>());
        const auto finalData=scene.Resources().Get<BRepModel>(processed.at("url").To<std::string>(),processed.at("version").To<unsigned long long>());
        ASSERT_TRUE(rawData);ASSERT_TRUE(finalData);
        const auto rawShape=iCAX::OpenCascade::BuildOpenCascadeShape(*rawData).Shape;
        const auto finalShape=iCAX::OpenCascade::BuildOpenCascadeShape(*finalData).Shape;
        const auto request=stock.at("request").To<ObjectMap>();
        bool cutsMaterial=!request.at("features").To<VariantArray>().empty();
        for(const auto& [end,value]:request.at("ends").To<ObjectMap>())
            cutsMaterial=cutsMaterial||value.To<ObjectMap>().at("type").To<std::string>()!="keep";
        if(request.contains("neutralOperations"))
            cutsMaterial=cutsMaterial||!request.at("neutralOperations").To<VariantArray>().empty();
        if(cutsMaterial) {
            EXPECT_GT(volume(rawShape)-volume(finalShape),1.0)<<stockID;
        } else {
            checkSameShape(rawShape,finalShape); // Inserting straight material needs no end cut.
        }
        EXPECT_EQ(stock.at("matrix").To<VariantArray>().size(),16u);
    }
    const auto before=invoke(scene,"GetProductAssemblyBindings",identity);
    std::size_t sources=0;
    for(const auto& value:before.at("manufacturingPlan").To<ObjectMap>().at("stocks").To<VariantArray>())
        if(value.To<ObjectMap>().at("itemKey").To<std::string>().starts_with("outer_frame."))
            sources+=value.To<ObjectMap>().at("sourceMembers").To<VariantArray>().size();
    EXPECT_EQ(sources,10u);
    const auto directory=std::filesystem::current_path()/"tmp/product-assembly-process-acceptance";
    std::filesystem::create_directories(directory);const auto path=directory/"three-face-spatial.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="product-process";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());
    std::size_t restoredCanonical=0;
    for(const auto& [stockID,value]:native.at("stocks").To<ObjectMap>()) {
        const auto stock=value.To<ObjectMap>();
        for(const auto* field:{"baseGeometry","processedGeometry"}) {
            const auto reference=stock.at(field).To<ObjectMap>();
            const auto url=reference.at("url").To<std::string>();
            const auto version=reference.at("version").To<unsigned long long>();
            const auto saved=reopened.Resources().Get<BRepModel>(url,version);
            const auto original=scene.Resources().Get<BRepModel>(url,version);
            const auto savedInfo=reopened.Resources().GetInfo(url,version);
            ASSERT_TRUE(saved)<<stockID<<" "<<field<<" frozen BRep missing before cache hydration";
            ASSERT_TRUE(original);ASSERT_TRUE(savedInfo&&savedInfo->IsPersistent());
            EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*saved),iCAX::GeometryData::Persistence::Serialize(*original))
                <<stockID<<" "<<field<<" frozen BRep changed after save/open";
            ++restoredCanonical;
        }
    }
    EXPECT_EQ(restoredCanonical,2*native.at("stocks").To<ObjectMap>().size());
    std::cout<<"[product-stock-persistence] "<<restoredCanonical
        <<" canonical raw/processed BReps restored before hydration"<<std::endl;
    // Committed function-derived part aliases persist with the canonical stock
    // BReps. Project-open List must retain their exact geometry and identity.
    invoke(reopened,"List",ObjectMap{{"includeManufacturingGeometry",true}});
    const auto after=invoke(reopened,"GetProductAssemblyBindings",identity);
    EXPECT_EQ(after.at("manufacturingPlan").To<ObjectMap>().at("stocks"),before.at("manufacturingPlan").To<ObjectMap>().at("stocks"));
    for(const auto& [key,id]:ids) {
        const auto component=part(reopened,id);
        ASSERT_TRUE(reopened.Resources().Get<BRepModel>(component->GetManufacturingGeometryResourceID(),component->GetManufacturingGeometryResourceVersion()));
        EXPECT_EQ(component->GetManufacturingGeometryResourceID(),originalRefs.at(key).first);
        EXPECT_EQ(component->GetManufacturingGeometryResourceVersion(),originalRefs.at(key).second);
        const auto original=scene.Resources().Get<BRepModel>(originalRefs.at(key).first,originalRefs.at(key).second);
        const auto alias=reopened.Resources().Get<BRepModel>(originalRefs.at(key).first,originalRefs.at(key).second);
        EXPECT_EQ(iCAX::GeometryData::Persistence::Serialize(*original),iCAX::GeometryData::Persistence::Serialize(*alias))
            <<key<<" unmodified manufacturing alias must preserve the exact frozen BRep";
        std::size_t changedFaceDomains=0,changedVertexTolerances=0;
        for(std::size_t i=0;i<std::min(original->Faces.size(),alias->Faces.size());++i)
            if(original->Faces[i].Domain.UFirst!=alias->Faces[i].Domain.UFirst||original->Faces[i].Domain.ULast!=alias->Faces[i].Domain.ULast
                ||original->Faces[i].Domain.VFirst!=alias->Faces[i].Domain.VFirst||original->Faces[i].Domain.VLast!=alias->Faces[i].Domain.VLast)++changedFaceDomains;
        for(std::size_t i=0;i<std::min(original->Vertices.size(),alias->Vertices.size());++i)
            if(original->Vertices[i].Tolerance!=alias->Vertices[i].Tolerance)++changedVertexTolerances;
        std::cout<<"[product-stock-alias-conversion] "<<key<<" faces="<<original->Faces.size()<<"/"<<alias->Faces.size()
            <<" edges="<<original->Edges.size()<<"/"<<alias->Edges.size()<<" triangulations="<<original->Triangulations3.size()<<"/"<<alias->Triangulations3.size()
            <<" changedFaceDomains="<<changedFaceDomains<<" changedVertexTolerances="<<changedVertexTolerances<<std::endl;
        for(const auto& [stockID,value]:native.at("stocks").To<ObjectMap>()) {
            const auto stock=value.To<ObjectMap>();if(stock.at("itemKey").To<std::string>()!=key)continue;
            const auto base=stock.at("baseGeometry").To<ObjectMap>(),processed=stock.at("processedGeometry").To<ObjectMap>();
            const auto rawModel=reopened.Resources().Get<BRepModel>(base.at("url").To<std::string>(),base.at("version").To<unsigned long long>());
            const auto processedModel=reopened.Resources().Get<BRepModel>(processed.at("url").To<std::string>(),processed.at("version").To<unsigned long long>());
            checkGeneratedStockAlias(shape(reopened,component),iCAX::OpenCascade::BuildOpenCascadeShape(*processedModel).Shape,
                iCAX::OpenCascade::BuildOpenCascadeShape(*rawModel).Shape,stock);
        }
    }
    invoke(reopened,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{productID}}});
    for(const auto& [key,id]:ids)checkSameShape(shape(reopened,part(reopened,id)),shape(scene,part(scene,id)));
    std::map<std::string,std::vector<uint8_t>> beforeFailureBytes;
    std::map<std::string,std::pair<std::string,unsigned long long>> beforeFailureRefs;
    std::map<std::string,ObjectMap> beforeFailureProperties;
    for(const auto& [key,id]:ids) {
        const auto component=part(reopened,id);
        beforeFailureBytes[key]=persistedShapeBytes(reopened,component);
        beforeFailureRefs[key]={component->GetManufacturingGeometryResourceID(),component->GetManufacturingGeometryResourceVersion()};
        beforeFailureProperties[key]=component->GetItemProperties();
    }
    // A valid source interval can still describe disconnected material. The
    // actual-face check must reject zero area even when its requested lower
    // bound is smaller than the numerical area comparison tolerance.
    const auto reopenedRun=std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        reopened.Database().GetEntity(iCAX::Data::uuid::from_string(runID).value())->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    const auto frozen=reopenedRun->GetManufacturingModel();
    const auto committedCount=reopenedRun->GetPartCount();
    auto disconnected=frozen;
    auto extensions=disconnected.at("extensions").To<ObjectMap>();
    const auto raw=extensions.at("tubeDesigner.identicalManufacturingSource").To<ObjectMap>();
    // Contact validation consumes the complete physical stocks, before any
    // aggregation. Exercise that owned source document, rather than deleting
    // the frozen stock evidence required to validate an aggregated snapshot.
    for(const auto* key:{"items","outputs"})disconnected[key]=raw.at(key);
    for(const auto* key:{"roots","tables"}) {
        if(raw.contains(key))disconnected[key]=raw.at(key);
        else disconnected.erase(key);
    }
    extensions["tubeDesigner.manufacturingPartCount"]=raw.at("partCount");
    if(raw.contains("assemblyOutputSets"))extensions["tubeDesigner.assemblyOutputSets"]=raw.at("assemblyOutputSets");
    else extensions.erase("tubeDesigner.assemblyOutputSets");
    extensions.erase("tubeDesigner.identicalManufacturingSource");
    extensions.erase("tubeDesigner.assemblyProcessNative");disconnected["extensions"]=extensions;
    auto items=disconnected.at("items").To<VariantArray>();
    const auto requiredPost=plan.at("contactRequirements").To<VariantArray>().front().To<ObjectMap>()
        .at("partIds").To<ObjectMap>().at("memberC").To<std::string>();
    bool shifted=false;
    for(auto& itemValue:items) {
        auto item=itemValue.To<ObjectMap>();auto properties=item.at("properties").To<ObjectMap>();
        if(!properties.contains("manufacturing.sourceMembers"))continue;
        auto sources=properties.at("manufacturing.sourceMembers").To<VariantArray>();
        for(auto& sourceValue:sources) {
            auto source=sourceValue.To<ObjectMap>();
            if(source.at("itemKey").To<std::string>()!=requiredPost)continue;
            auto spans=source.at("spans").To<VariantArray>();
            for(auto& spanValue:spans) {
                auto span=spanValue.To<ObjectMap>();auto placement=span.at("placement").To<ObjectMap>();
                auto origin=placement.at("origin").To<VariantArray>();origin[1]=origin[1].To<double>()+1000.0;
                placement["origin"]=origin;span["placement"]=placement;spanValue=span;
            }
            source["spans"]=spans;sourceValue=source;shifted=true;
        }
        properties["manufacturing.sourceMembers"]=sources;item["properties"]=properties;itemValue=item;
    }
    ASSERT_TRUE(shifted);disconnected["items"]=items;
    const auto rawCount=raw.at("partCount").To<unsigned long long>();
    ASSERT_TRUE(reopenedRun->SetPartCount(rawCount));
    ASSERT_TRUE(reopenedRun->SetManufacturingModel(disconnected));
    try {
        invoke(reopened,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{productID}}});
        FAIL()<<"disconnected material incorrectly satisfied positive-area contact";
    } catch(const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("实际节点接触不适用"),std::string::npos)<<error.what();
        EXPECT_NE(std::string(error.what()).find("memberC: " + requiredPost),std::string::npos)<<error.what();
    }
    EXPECT_EQ(reopenedRun->GetPartCount(),rawCount);
    EXPECT_EQ(reopenedRun->GetManufacturingModel(),disconnected);
    for(const auto& [key,id]:ids) {
        const auto component=part(reopened,id);
        EXPECT_EQ(component->GetManufacturingGeometryResourceID(),beforeFailureRefs.at(key).first);
        EXPECT_EQ(component->GetManufacturingGeometryResourceVersion(),beforeFailureRefs.at(key).second);
        EXPECT_EQ(component->GetItemProperties(),beforeFailureProperties.at(key));
        EXPECT_EQ(persistedShapeBytes(reopened,component),beforeFailureBytes.at(key));
    }
    ASSERT_TRUE(reopenedRun->SetManufacturingModel(frozen));
    ASSERT_TRUE(reopenedRun->SetPartCount(committedCount));
    auto updated=product.at("parameters").To<ObjectMap>();updated["frameManufacturingMode"]=std::string("plane_v_notch");
    invoke(reopened,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{productID}},
        {"productParametersByEntityId",ObjectMap{{productID,updated}}}});
    const auto updatedRun=std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
        reopened.Database().GetEntity(iCAX::Data::uuid::from_string(runID).value())->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    EXPECT_EQ(updatedRun->GetNeutralModel(),run->GetNeutralModel());
    const auto updatedStocks=invoke(reopened,"GetProductAssemblyBindings",identity).at("manufacturingPlan").To<ObjectMap>().at("stocks").To<VariantArray>();
    std::size_t newOuter=0;
    for(const auto& value:updatedStocks)if(value.To<ObjectMap>().at("itemKey").To<std::string>().starts_with("outer_frame."))
        newOuter+=value.To<ObjectMap>().at("sourceStockIds").To<VariantArray>().size();
    EXPECT_EQ(newOuter,6u);
}

TEST(ProductGeneratedAssemblyProcessSDO, UnequalRectangularPlaneStockUsesAuthoritativeRotatedSection)
{
    Scene scene;
    auto parameters=generatedSecurityProcessParameters("plane_v_notch");
    parameters["frameWidth"]=60.0;parameters["frameDepth"]=40.0;parameters["frameWallThickness"]=2.0;
    auto requested=parameters;requested["templateId"]=std::string("single-face-security-window");
    const auto generated=invoke(scene,"GeneratePreview",requested).at("tubeDesigner").To<ObjectMap>();
    const auto product=generated.at("product").To<ObjectMap>();
    const auto runID=product.at("activeGenerationRunId").To<std::string>();
    const auto result=invoke(scene,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{product.at("entityId")}}}).at("tubeDesigner").To<ObjectMap>();
    std::size_t count=0;
    for(const auto& value:result.at("parts").To<VariantArray>()) {
        const auto row=value.To<ObjectMap>();const auto component=part(scene,row.at("entityId").To<std::string>());
        if(!component->GetStableKey().starts_with("outer_frame."))continue;
        const auto solid=shape(scene,component);ASSERT_TRUE(BRepCheck_Analyzer(solid).IsValid());
        std::size_t solids=0;for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++solids;
        EXPECT_EQ(solids,1u);count+=component->GetQuantity();
        if(component->GetStableKey().find("post")!=std::string::npos) {
            Bnd_Box box;BRepBndLib::AddOptimal(solid,box,false,false);box.SetGap(0.0);
            double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
            EXPECT_NEAR(component->GetItemProperties().at("manufacturing.rawStockLength").To<double>(),1740.0,1e-5);
            EXPECT_NEAR(component->GetLength(),1680.0,1e-5);
            EXPECT_NEAR(x0,30.0,1e-5);EXPECT_NEAR(x1,1710.0,1e-5);
        }
    }
    EXPECT_EQ(count,6u);
    const auto run=std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(scene.Database().GetEntity(
        iCAX::Data::uuid::from_string(runID).value())->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName));
    ASSERT_TRUE(run);
    const auto native=run->GetManufacturingModel().at("extensions").To<ObjectMap>().at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
    ASSERT_EQ(native.at("plan").To<ObjectMap>().at("nativeContactChecks").To<VariantArray>().size(),8u);
    std::size_t rotatedFrameStocks=0;
    for(const auto& [id,value]:native.at("stocks").To<ObjectMap>()) {
        const auto stock=value.To<ObjectMap>();
        if(!stock.at("itemKey").To<std::string>().starts_with("outer_frame."))continue;
        ++rotatedFrameStocks;
        const auto profile=stock.at("profile").To<ObjectMap>();
        const auto width=profile.at("width").To<double>(),depth=profile.at("depth").To<double>();
        EXPECT_NEAR(std::min(width,depth),40.0,0.001);EXPECT_NEAR(std::max(width,depth),60.0,0.001);
    }
    EXPECT_EQ(rotatedFrameStocks,6u);
}

TEST(ProductGeneratedAssemblyProcessSDO, AllFaceModesUseNativeDescriptorsAndKeepDisplayProcessIndependent)
{
    for(const auto& [face,expected]:std::vector<std::pair<std::string,std::size_t>>{{"single",4},{"two",7},{"three",10},{"five",12}})
        for(const auto* mode:{"segment_weld","plane_v_notch","spatial_v_notch"}) {
            Scene scene;
            SCOPED_TRACE(face+"/"+mode);
            auto parameters=generatedSecurityProcessParameters(mode);parameters["faceType"]=face;
            const auto response=invoke(scene,"GenerateProductTemplatePreview",ObjectMap{{"templateId",std::string("single-face-security-window")},
                {"parameters",parameters}});
            EXPECT_EQ(response.at("parameters").To<ObjectMap>().at("frameManufacturingMode").To<std::string>(),mode);
            std::size_t count=0;for(const auto& value:response.at("items").To<VariantArray>())
                if(value.To<ObjectMap>().at("key").To<std::string>().starts_with("outer_frame."))++count;
            EXPECT_EQ(count,expected);
            if(face=="three" && std::string(mode)!="segment_weld")continue; // The two preceding cases execute these combinations.
            auto request=response.at("parameters").To<ObjectMap>();request["templateId"]=std::string("single-face-security-window");
            const auto product=invoke(scene,"GeneratePreview",request).at("tubeDesigner").To<ObjectMap>().at("product").To<ObjectMap>();
            const auto actual=invoke(scene,"DisassembleSelected",ObjectMap{{"productEntityIds",VariantArray{product.at("entityId")}}})
                .at("tubeDesigner").To<ObjectMap>();
            std::size_t outer=0;
            for(const auto& value:actual.at("parts").To<VariantArray>()) {
                const auto row=value.To<ObjectMap>();const auto component=part(scene,row.at("entityId").To<std::string>());
                const auto solid=shape(scene,component);ASSERT_TRUE(BRepCheck_Analyzer(solid).IsValid());
                std::size_t solids=0;for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++solids;
                EXPECT_EQ(solids,1u);EXPECT_GT(volume(solid),0.0);
                if(component->GetStableKey().starts_with("outer_frame."))outer+=component->GetQuantity();
            }
            const std::size_t expectedStock=std::string(mode)=="segment_weld"?expected
                :face=="single"?1u:face=="two"?(std::string(mode)=="plane_v_notch"?5u:2u)
                :face=="three"?(std::string(mode)=="plane_v_notch"?6u:3u):6u;
            EXPECT_EQ(outer,expectedStock);
            const auto bindings=invoke(scene,"GetProductAssemblyBindings",ObjectMap{{"productEntityId",product.at("entityId")},
                {"generationRunId",product.at("activeGenerationRunId")}});
            std::set<std::string> logicalSources;
            std::size_t mappedStocks=0;
            for(const auto& value:bindings.at("manufacturingPlan").To<ObjectMap>().at("stocks").To<VariantArray>()) {
                const auto stock=value.To<ObjectMap>();
                if(!stock.at("itemKey").To<std::string>().starts_with("outer_frame."))continue;
                mappedStocks+=stock.at("sourceStockIds").To<VariantArray>().size();
                EXPECT_EQ(stock.at("partMappingStatus").To<std::string>(),"persisted");
                EXPECT_FALSE(stock.at("stockEntityId").To<std::string>().empty());
                for(const auto& source:stock.at("sourceMembers").To<VariantArray>())
                    logicalSources.insert(source.To<ObjectMap>().at("itemKey").To<std::string>());
            }
            EXPECT_EQ(mappedStocks,expectedStock);EXPECT_EQ(logicalSources.size(),expected);
        }
}
