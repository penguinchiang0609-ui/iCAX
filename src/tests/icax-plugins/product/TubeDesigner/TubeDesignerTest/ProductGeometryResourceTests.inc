namespace product_geometry_resource_acceptance {
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using iCAX::TemplateRuntime::CTemplateCodec;

ObjectMap document()
{
    return iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
        "schema":"icax.neutral-model","schemaVersion":2,
        "template":{"id":"test.product-resources","version":"1.0.0","packageDigest":"test"},
        "coordinateSystem":"right-handed-x-width-y-depth-z-height","lengthUnit":"mm","parameters":{"length":40},
        "resources":[
            {"key":"G.profile","operator":"profile2d","inputs":[],"arguments":{
                "placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},
                "contours":[{"kind":"path","closed":true,"segments":[
                    {"kind":"line","start":[0,0],"end":[10,0]},
                    {"kind":"line","start":[10,0],"end":[10,6]},
                    {"kind":"line","start":[10,6],"end":[0,6]},
                    {"kind":"line","start":[0,6],"end":[0,0]}]}]}},
            {"key":"G.solid","operator":"extrude","inputs":["G.profile"],"arguments":{"vector":[0,0,40]}}
        ],
        "items":[
            {"key":"part.a","displayName":"A","representations":{"display":{"resource":"G.solid","instanceKey":"part.a.instance",
                "placement":{"origin":[5,10,15],"xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}}}},
            {"key":"part.b","displayName":"B","representations":{"display":{"resource":"G.solid","instanceKey":"part.b.instance",
                "placement":{"origin":[70,20,30],"xAxis":[0,1,0],"yAxis":[-1,0,0],"zAxis":[0,0,1]}}}}
        ],
        "outputs":[{"key":"display.default","purpose":"display","items":["part.a","part.b"]}],
        "relationships":[],"tables":[],"diagnostics":[],"extensions":{}
    })json").To<ObjectMap>();
}

double volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
    return std::abs(properties.Mass());
}

void bounds(const TopoDS_Shape& shape,const std::array<double,6>& expected)
{
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);box.SetGap(0.0);
    double actual[6];box.Get(actual[0],actual[1],actual[2],actual[3],actual[4],actual[5]);
    for(std::size_t i=0;i<6;++i)EXPECT_NEAR(actual[i],expected[i],1.e-7);
}

void updateReference(ObjectMap& model,std::size_t index,const std::function<void(ObjectMap&)>& update)
{
    auto items=model.at("items").To<VariantArray>();auto item=items.at(index).To<ObjectMap>();
    auto representations=item.at("representations").To<ObjectMap>();
    auto reference=representations.at("display").To<ObjectMap>();update(reference);
    representations["display"]=reference;item["representations"]=representations;items[index]=item;model["items"]=items;
}

ObjectMap displayDocument()
{
    auto source=document();source["schema"]=std::string("icax.display-model");source["schemaVersion"]=2;
    for(const auto* key:{"template","parameters","outputs","relationships","tables","diagnostics","extensions"})
        source.erase(key);
    auto items=source.at("items").To<VariantArray>();
    for(auto& value:items) {
        auto item=value.To<ObjectMap>();item["geometry"]=item.at("representations").To<ObjectMap>().at("display");
        item.erase("representations");item["children"]=VariantArray{};
        item["properties"]=ObjectMap{{"partKind",std::string("tube")},{"materialCategory",std::string("linear")}};value=item;
    }
    source["items"]=items;source["roots"]=VariantArray{std::string("part.a"),std::string("part.b")};
    source["annotations"]=VariantArray{};return source;
}

ObjectMap manufacturingDocument()
{
    auto source=displayDocument();source["schema"]=std::string("icax.manufacturing-model");
    source["schemaVersion"]=2;
    source.erase("annotations");
    source["sourceMappings"]=VariantArray{ObjectMap{{"itemKey",std::string("part.a")},
        {"sources",VariantArray{ObjectMap{{"itemKey",std::string("design.left")}}}}}};
    source["processes"]=VariantArray{
        ObjectMap{{"key",std::string("cut.1")},{"kind",std::string("assembly-process")},
            {"definition",ObjectMap{{"templateId",std::string("local.geometry")},
                {"processInput",ObjectMap{{"schema",std::string("icax.assembly-process-input")},{"schemaVersion",2},
                    {"parts",ObjectMap{{"stock",ObjectMap{{"scope",std::string("manufacturing")},
                        {"itemKey",std::string("part.a")},{"state",std::string("initial")}}}}},{"geometry",ObjectMap{}}}},
                {"parameters",ObjectMap{{"offset",3.0}}},{"processDrafts",ObjectMap{}},
                {"targets",ObjectMap{{"stock",std::string("part.a")}}},{"dependencies",VariantArray{}}}}}};
    return source;
}

ObjectMap designManufacturingDocument()
{
    auto source=manufacturingDocument();source["schemaVersion"]=3;
    auto items=source.at("items").To<VariantArray>();
    for(std::size_t index=0;index<items.size();++index) {
        auto item=items[index].To<ObjectMap>();item.erase("geometry");
        item["properties"]=ObjectMap{{"tubeDesigner.profile",ObjectMap{{"sectionResource",std::string("G.profile")}}}};
        const double x=static_cast<double>(index)*20.0;
        item["designInput"]=ObjectMap{{"kind",std::string("tube-member")},{"profileResource",std::string("G.profile")},
            {"start",VariantArray{x,0.0,0.0}},{"end",VariantArray{x,0.0,40.0}},
            {"sectionFrame",ObjectMap{{"origin",VariantArray{x,0.0,0.0}},{"xAxis",VariantArray{1.0,0.0,0.0}},
                {"yAxis",VariantArray{0.0,1.0,0.0}},{"zAxis",VariantArray{0.0,0.0,1.0}}}}};
        items[index]=item;
    }
    source["items"]=items;
    source["sourceMappings"]=VariantArray{
        ObjectMap{{"itemKey",std::string("part.a")},{"sources",VariantArray{ObjectMap{{"itemKey",std::string("design.left")}}}}},
        ObjectMap{{"itemKey",std::string("part.b")},{"sources",VariantArray{ObjectMap{{"itemKey",std::string("design.right")}}}}}};
    source["processes"]=VariantArray{ObjectMap{{"key",std::string("frame.route")},{"kind",std::string("assembly-process")},
        {"definition",ObjectMap{{"templateId",std::string("security-window-assembly")},
            {"processInput",ObjectMap{{"schema",std::string("icax.assembly-process-input")},{"schemaVersion",3},
                {"parts",ObjectMap{{"members",ObjectMap{{"scope",std::string("design")},
                    {"itemKeys",VariantArray{std::string("part.a"),std::string("part.b")}}}}}},
                {"geometry",ObjectMap{{"layout",ObjectMap{{"faceType",std::string("three")}}}}}}},
            {"parameters",ObjectMap{{"frameManufacturingMethod",std::string("plane_v_notch")}}},{"processDrafts",ObjectMap{}},
            {"outputs",ObjectMap{{"manufacturing",ObjectMap{{"kind",std::string("manufacturing-set")},
                {"key",std::string("window.manufacturing")}}}}},{"dependencies",VariantArray{}}}}}};
    return source;
}

ObjectMap generatedExecutionDocument()
{
    auto source=displayDocument();auto items=source.at("items").To<VariantArray>();items.resize(1);
    auto item=items.front().To<ObjectMap>();item["key"]=std::string("manufactured.u-frame");items.front()=item;
    source["items"]=items;source["roots"]=VariantArray{std::string("manufactured.u-frame")};
    auto result=CTemplateCodec::AdaptDisplayModel(source);
    result["extensions"]=ObjectMap{{"tubeDesigner.geometryPurpose",std::string("manufacturing")},
        {"tubeDesigner.manufacturingPartCount",1ull},{"tubeDesigner.assemblyOutputSets",VariantArray{
            ObjectMap{{"processKey",std::string("frame.route")},{"outputKey",std::string("manufacturing")},
                {"key",std::string("window.manufacturing")},{"kind",std::string("manufacturing-set")},
                {"itemKeys",VariantArray{std::string("manufactured.u-frame")}},
                {"sourceItemKeys",VariantArray{std::string("part.a"),std::string("part.b")}}}}}};
    return result;
}
}

TEST(ProductGeometryResources, OneDefinitionEvaluatesOnceForTwoIndependentPlacements)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=document();const auto before=source;
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_EQ(source,before);ASSERT_EQ(source.at("resources").To<VariantArray>().size(),2u);
    EXPECT_FALSE(source.contains("geometry"));ASSERT_EQ(model.Items.size(),2u);
    const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,
        {model.Items[0].Representations.at("display"),model.Items[1].Representations.at("display")});
    EXPECT_EQ(evaluated.Geometry.size(),4u); // Two definitions plus two placement nodes.
    const auto& prototype=evaluated.At("G.solid");
    const auto& first=evaluated.At(model.Items[0].Representations.at("display"));
    const auto& second=evaluated.At(model.Items[1].Representations.at("display"));
    EXPECT_TRUE(prototype.IsPartner(first));EXPECT_TRUE(prototype.IsPartner(second));
    EXPECT_TRUE(first.IsPartner(second)); // Same TShape: no repeated extrusion.
    EXPECT_FALSE(first.IsSame(second));
    EXPECT_TRUE(BRepCheck_Analyzer(first).IsValid());EXPECT_TRUE(BRepCheck_Analyzer(second).IsValid());
    EXPECT_NEAR(volume(first),2400.0,1.e-6);EXPECT_NEAR(volume(second),2400.0,1.e-6);
    bounds(first,{5,10,15,15,16,55});bounds(second,{64,20,30,70,30,70});
    const auto expanded=CTemplateCodec::ExpandNeutralModelResources(source);
    EXPECT_FALSE(expanded.contains("resources"));EXPECT_EQ(expanded.at("geometry").To<VariantArray>().size(),4u);
    EXPECT_EQ(expanded.at("parameters"),source.at("parameters"));
}

TEST(ProductGeometryResources, DifferentLengthsAndSectionsKeepDistinctPrototypeShapes)
{
    using namespace product_geometry_resource_acceptance;
    auto source=document();auto resources=source.at("resources").To<VariantArray>();
    auto longer=resources[1].To<ObjectMap>();longer["key"]=std::string("G.longer");
    longer["arguments"]=ObjectMap{{"vector",VariantArray{0.0,0.0,41.0}}};resources.emplace_back(longer);
    auto different=resources[0].To<ObjectMap>();different["key"]=std::string("G.other.profile");
    auto arguments=different.at("arguments").To<ObjectMap>();
    const auto line=[](VariantArray start,VariantArray end) {
        return ObjectMap{{"kind",std::string("line")},{"start",std::move(start)},{"end",std::move(end)}};
    };
    arguments["contours"]=VariantArray{ObjectMap{{"kind",std::string("path")},{"closed",true},
        {"segments",VariantArray{line({0.0,0.0},{12.0,0.0}),line({12.0,0.0},{12.0,6.0}),
            line({12.0,6.0},{0.0,6.0}),line({0.0,6.0},{0.0,0.0})}}}};
    different["arguments"]=arguments;resources.emplace_back(different);
    auto other=resources[1].To<ObjectMap>();other["key"]=std::string("G.other.solid");
    other["inputs"]=VariantArray{std::string("G.other.profile")};resources.emplace_back(other);source["resources"]=resources;
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,{"G.solid","G.longer","G.other.solid"});
    EXPECT_FALSE(evaluated.At("G.solid").IsPartner(evaluated.At("G.longer")));
    EXPECT_FALSE(evaluated.At("G.solid").IsPartner(evaluated.At("G.other.solid")));
    EXPECT_NEAR(volume(evaluated.At("G.solid")),2400.0,1.e-6);
    EXPECT_NEAR(volume(evaluated.At("G.longer")),2460.0,1.e-6);
    EXPECT_NEAR(volume(evaluated.At("G.other.solid")),2880.0,1.e-6);
}

TEST(ProductGeometryResources, UnknownDuplicateAndCyclicResourceReferencesAreRejected)
{
    using namespace product_geometry_resource_acceptance;
    auto missing=document();updateReference(missing,0,[](auto& reference){reference["resource"]=std::string("missing");});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(missing),std::invalid_argument);
    auto duplicate=document();auto resources=duplicate.at("resources").To<VariantArray>();
    resources.emplace_back(resources.front());duplicate["resources"]=resources;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(duplicate),std::invalid_argument);
    auto unknown=document();resources=unknown.at("resources").To<VariantArray>();
    auto node=resources[1].To<ObjectMap>();node["inputs"]=VariantArray{std::string("missing")};
    resources[1]=node;unknown["resources"]=resources;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(unknown),std::invalid_argument);
    auto cyclic=document();resources=cyclic.at("resources").To<VariantArray>();
    node=resources[0].To<ObjectMap>();node["inputs"]=VariantArray{std::string("G.solid")};
    resources[0]=node;cyclic["resources"]=resources;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(cyclic),std::invalid_argument);
    auto mixed=document();mixed["geometry"]=VariantArray{};
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(mixed),std::invalid_argument);
}

TEST(ProductGeometryResources, InvalidOrConflictingItemFramesAreRejected)
{
    using namespace product_geometry_resource_acceptance;
    for(const auto& axis:std::vector<std::pair<std::string,VariantArray>>{
        {"xAxis",{2.0,0.0,0.0}},{"yAxis",{1.0,1.0,0.0}},{"zAxis",{0.0,0.0,-1.0}},
        {"origin",{std::numeric_limits<double>::infinity(),0.0,0.0}}}) {
        auto source=document();updateReference(source,0,[&](auto& reference){
            auto placement=reference.at("placement").template To<ObjectMap>();placement[axis.first]=axis.second;reference["placement"]=placement;
        });
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<axis.first;
    }
    auto conflicting=document();updateReference(conflicting,1,[](auto& reference){reference["instanceKey"]=std::string("part.a.instance");});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(conflicting),std::invalid_argument);
    auto colliding=document();updateReference(colliding,0,[](auto& reference){reference["instanceKey"]=std::string("G.solid");});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(colliding),std::invalid_argument);
}

TEST(ProductGeometryResources, DisplayAndManufacturingOutputTopologiesRemainIndependent)
{
    using namespace product_geometry_resource_acceptance;
    const auto display=document();auto manufacturing=document();
    manufacturing["items"]=VariantArray{ObjectMap{{"key",std::string("manufacturing.continuous")},
        {"displayName",std::string("Continuous processed part")},
        {"representations",ObjectMap{{"export",ObjectMap{{"resource",std::string("G.solid")}}}}}}};
    manufacturing["outputs"]=VariantArray{ObjectMap{{"key",std::string("export.manufacturing")},
        {"purpose",std::string("export")},{"items",VariantArray{std::string("manufacturing.continuous")}}}};
    const auto designModel=CTemplateCodec::ParseNeutralModel(display);
    const auto manufacturingModel=CTemplateCodec::ParseNeutralModel(manufacturing);
    EXPECT_EQ(designModel.Items.size(),2u);EXPECT_EQ(manufacturingModel.Items.size(),1u);
    EXPECT_EQ(designModel.Items[0].Key,"part.a");EXPECT_EQ(manufacturingModel.Items[0].Key,"manufacturing.continuous");
    const auto designGeometry=iCAX::OpenCascade::EvaluateNeutralModel(designModel,
        {designModel.Items[0].Representations.at("display"),designModel.Items[1].Representations.at("display")});
    const auto manufacturingGeometry=iCAX::OpenCascade::EvaluateNeutralModel(manufacturingModel,
        {manufacturingModel.Items[0].Representations.at("export")});
    EXPECT_EQ(designGeometry.Geometry.size(),4u);EXPECT_EQ(manufacturingGeometry.Geometry.size(),2u);
    EXPECT_EQ(display.at("resources"),manufacturing.at("resources"));
    EXPECT_NEAR(volume(manufacturingGeometry.At("G.solid")),2400.0,1.e-6);
}

TEST(ProductDisplayModel, PureDesignHasNoTemplateOrParametersAndKeepsOneSharedShape)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=displayDocument();const auto before=source;
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_EQ(source,before);EXPECT_EQ(source.size(),8u);
    EXPECT_TRUE(model.TemplateID.empty());EXPECT_TRUE(model.TemplateVersion.empty());
    EXPECT_TRUE(model.PackageDigest.empty());EXPECT_TRUE(model.Parameters.empty());
    ASSERT_EQ(model.Items.size(),2u);ASSERT_EQ(model.Outputs.size(),1u);
    EXPECT_EQ(model.Outputs.front().Purpose,"result");
    ASSERT_TRUE(model.Items[0].Representations.contains("result"));
    ASSERT_TRUE(model.Items[1].Representations.contains("result"));
    const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,
        {model.Items[0].Representations.at("result"),model.Items[1].Representations.at("result")});
    const auto& first=evaluated.At(model.Items[0].Representations.at("result"));
    const auto& second=evaluated.At(model.Items[1].Representations.at("result"));
    EXPECT_EQ(evaluated.Geometry.size(),4u);EXPECT_TRUE(evaluated.At("G.solid").IsPartner(first));
    EXPECT_TRUE(first.IsPartner(second));EXPECT_FALSE(first.IsSame(second));
    EXPECT_NEAR(volume(first),2400.0,1.e-6);EXPECT_NEAR(volume(second),2400.0,1.e-6);
    bounds(first,{5,10,15,15,16,55});bounds(second,{64,20,30,70,30,70});
    const auto adapted=CTemplateCodec::AdaptDisplayModel(source);
    if(adapted.contains("parameters"))EXPECT_TRUE(adapted.at("parameters").To<ObjectMap>().empty());
    EXPECT_EQ(adapted.at("resources"),source.at("resources"));
}

TEST(ProductDisplayModel, NeutralEnvelopeAndManufacturingPropertiesAreRejected)
{
    using namespace product_geometry_resource_acceptance;
    for(const auto* key:{"template","parameters","outputs","tables","diagnostics","extensions","geometry","connections"}) {
        auto source=displayDocument();source[key]=ObjectMap{};
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<key;
    }
    for(const auto* key:{"manufacturing.stockId","tubeDesigner.assemblyProcessStockId","tubeDesigner.endProcess"}) {
        auto source=displayDocument();auto items=source.at("items").To<VariantArray>();
        auto item=items.front().To<ObjectMap>();item["properties"]=ObjectMap{{key,std::string("process")}};
        items.front()=item;source["items"]=items;
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<key;
    }
    auto valid=displayDocument();auto items=valid.at("items").To<VariantArray>();auto item=items.front().To<ObjectMap>();
    item["properties"]=ObjectMap{{"tubeDesigner.profile",ObjectMap{
        {"templateId",std::string("tube-section")},{"parameters",ObjectMap{{"wall",2.0}}}}}};
    items.front()=item;valid["items"]=items;
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(valid));
}

TEST(ProductDisplayModel, HierarchyHasRealRootsAndOutputsOnlyGeometryDescendants)
{
    using namespace product_geometry_resource_acceptance;
    auto source=displayDocument();auto items=source.at("items").To<VariantArray>();
    items.insert(items.begin(),ObjectMap{{"key",std::string("frame")},{"displayName",std::string("Frame")},
        {"children",VariantArray{std::string("part.a"),std::string("part.b")}},{"properties",ObjectMap{}}});
    source["items"]=items;source["roots"]=VariantArray{std::string("frame")};
    source["annotations"]=VariantArray{ObjectMap{{"id",std::string("length")},{"kind",std::string("linear")},
        {"start",VariantArray{0.0,0.0,0.0}},{"end",VariantArray{0.0,0.0,40.0}},{"label",std::string("Length")}}};
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    ASSERT_EQ(model.Items.size(),3u);EXPECT_TRUE(model.Items.front().Representations.empty());
    EXPECT_EQ(model.Items.front().Children,(std::vector<std::string>{"part.a","part.b"}));
    ASSERT_EQ(model.Outputs.size(),1u);EXPECT_EQ(model.Outputs.front().ItemKeys,(std::vector<std::string>{"part.a","part.b"}));
    const auto adapted=CTemplateCodec::AdaptDisplayModel(source);
    EXPECT_TRUE(adapted.at("relationships").To<VariantArray>().empty());
    EXPECT_EQ(adapted.at("extensions").To<ObjectMap>().at("tubeDesigner.specificationAnnotations"),source.at("annotations"));
    for(const auto& roots:std::vector<VariantArray>{{std::string("missing")},{std::string("part.a")},
                                                  {std::string("frame"),std::string("part.a")}}) {
        auto invalid=source;invalid["roots"]=roots;
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(invalid),std::invalid_argument);
    }
    auto cyclic=source;items=cyclic.at("items").To<VariantArray>();auto child=items[1].To<ObjectMap>();
    child["children"]=VariantArray{std::string("frame")};items[1]=child;cyclic["items"]=items;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(cyclic),std::invalid_argument);
}

TEST(ProductManufacturingModel, PendingCallsKeepSharedInitialGeometryAndHaveNoExecutedProcess)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=manufacturingDocument();const auto before=source;
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_EQ(source,before);EXPECT_EQ(source.size(),9u);
    EXPECT_TRUE(model.TemplateID.empty());EXPECT_TRUE(model.TemplateVersion.empty());
    EXPECT_TRUE(model.PackageDigest.empty());EXPECT_TRUE(model.Parameters.empty());
    ASSERT_EQ(model.Items.size(),2u);ASSERT_EQ(model.Outputs.size(),1u);
    EXPECT_EQ(model.Outputs.front().Purpose,"result");
    const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,
        {model.Items[0].Representations.at("result"),model.Items[1].Representations.at("result")});
    const auto& first=evaluated.At(model.Items[0].Representations.at("result"));
    const auto& second=evaluated.At(model.Items[1].Representations.at("result"));
    EXPECT_EQ(evaluated.Geometry.size(),4u);EXPECT_TRUE(first.IsPartner(second));EXPECT_FALSE(first.IsSame(second));
    bounds(first,{5,10,15,15,16,55});bounds(second,{64,20,30,70,30,70});
    EXPECT_FALSE(model.Items[0].Properties.contains("manufacturing.sourceMembers"));
    EXPECT_FALSE(model.Items[1].Properties.contains("manufacturing.sourceMembers"));
    EXPECT_EQ(source.at("processes"),before.at("processes"));
    EXPECT_EQ(source.at("processes").To<VariantArray>().size(),1u);
    EXPECT_FALSE(model.Extensions.contains("tubeDesigner.assemblyGeometryProcesses"));
    EXPECT_FALSE(model.Extensions.contains("tubeDesigner.assemblyProcessSource"));
    EXPECT_TRUE(model.Tables.empty());EXPECT_TRUE(model.Diagnostics.empty());
    const auto adapted=CTemplateCodec::AdaptManufacturingModel(source);
    ASSERT_TRUE(adapted.contains("resources"));
    EXPECT_EQ(adapted.at("resources"),source.at("resources"));
}

TEST(ProductManufacturingModel, InvalidEnvelopeMappingProcessAndPlacementAreRejected)
{
    using namespace product_geometry_resource_acceptance;
    for(const auto* key:{"template","parameters","outputs","tables","diagnostics","extensions","geometry","partLists","checks"}) {
        auto source=manufacturingDocument();source[key]=ObjectMap{};
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<key;
    }
    auto unknown=manufacturingDocument();auto mappings=unknown.at("sourceMappings").To<VariantArray>();
    auto mapping=mappings.front().To<ObjectMap>();mapping["itemKey"]=std::string("missing");
    mappings.front()=mapping;unknown["sourceMappings"]=mappings;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(unknown),std::invalid_argument);
    auto duplicate=manufacturingDocument();mappings=duplicate.at("sourceMappings").To<VariantArray>();
    mappings.emplace_back(mappings.front());duplicate["sourceMappings"]=mappings;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(duplicate),std::invalid_argument);
    auto duplicateProcess=manufacturingDocument();auto processes=duplicateProcess.at("processes").To<VariantArray>();
    processes.emplace_back(processes.front());duplicateProcess["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(duplicateProcess),std::invalid_argument);
    auto unknownKind=manufacturingDocument();processes=unknownKind.at("processes").To<VariantArray>();
    auto process=processes.front().To<ObjectMap>();process["kind"]=std::string("unknown");
    processes.front()=process;unknownKind["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(unknownKind),std::invalid_argument);
    auto badTarget=manufacturingDocument();processes=badTarget.at("processes").To<VariantArray>();
    process=processes.front().To<ObjectMap>();auto definition=process.at("definition").To<ObjectMap>();
    auto input=definition.at("processInput").To<ObjectMap>();auto roles=input.at("parts").To<ObjectMap>();
    auto stock=roles.at("stock").To<ObjectMap>();stock["itemKey"]=std::string("missing");roles["stock"]=stock;
    input["parts"]=roles;definition["processInput"]=input;process["definition"]=definition;
    processes.front()=process;badTarget["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(badTarget),std::invalid_argument);
    auto badIdentity=manufacturingDocument();processes=badIdentity.at("processes").To<VariantArray>();
    process=processes.front().To<ObjectMap>();definition=process.at("definition").To<ObjectMap>();
    definition["result"]=ObjectMap{{"applicable",true}};process["definition"]=definition;
    processes.front()=process;badIdentity["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(badIdentity),std::invalid_argument);
    auto duplicateSourceProperty=manufacturingDocument();auto sourceItems=duplicateSourceProperty.at("items").To<VariantArray>();
    auto sourceItem=sourceItems.front().To<ObjectMap>();sourceItem["properties"]=ObjectMap{{"manufacturing.sourceMembers",VariantArray{}}};
    sourceItems.front()=sourceItem;duplicateSourceProperty["items"]=sourceItems;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(duplicateSourceProperty),std::invalid_argument);
    auto scaled=manufacturingDocument();auto items=scaled.at("items").To<VariantArray>();auto item=items.front().To<ObjectMap>();
    auto geometry=item.at("geometry").To<ObjectMap>();auto frame=geometry.at("placement").To<ObjectMap>();
    frame["xAxis"]=VariantArray{2.0,0.0,0.0};geometry["placement"]=frame;item["geometry"]=geometry;
    items.front()=item;scaled["items"]=items;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(scaled),std::invalid_argument);
}

TEST(ProductManufacturingModel, HierarchyKeepsInitialPartsAndDerivesNoMaterialListsOrChecks)
{
    using namespace product_geometry_resource_acceptance;
    auto source=manufacturingDocument();auto items=source.at("items").To<VariantArray>();
    items.insert(items.begin(),ObjectMap{{"key",std::string("batch")},{"displayName",std::string("Batch")},
        {"children",VariantArray{std::string("part.a"),std::string("part.b")}},{"properties",ObjectMap{}}});
    source["items"]=items;source["roots"]=VariantArray{std::string("batch")};
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_TRUE(model.Tables.empty());EXPECT_TRUE(model.Diagnostics.empty());
    ASSERT_EQ(model.Outputs.size(),1u);EXPECT_EQ(model.Outputs.front().ItemKeys,(std::vector<std::string>{"part.a","part.b"}));
    const auto adapted=CTemplateCodec::AdaptManufacturingModel(source);
    EXPECT_FALSE(adapted.contains("tables"));
    EXPECT_FALSE(adapted.contains("diagnostics"));
    ASSERT_TRUE(model.Extensions.contains("tubeDesigner.manufacturingPartCount"));
    EXPECT_EQ(model.Extensions.at("tubeDesigner.manufacturingPartCount").To<unsigned long long>(),2u);
}

TEST(ProductManufacturingModel, DesignTubePathIsAcceptedWithoutInventingAllocatedGeometry)
{
    using namespace product_geometry_resource_acceptance;
    auto source=manufacturingDocument();auto item=source.at("items").To<VariantArray>().front().To<ObjectMap>();
    item.erase("geometry");
    item["properties"]=ObjectMap{{"tubeDesigner.profile",ObjectMap{{"sectionResource",std::string("G.profile")}}}};
    item["manufacturingInput"]=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(R"json({
        "kind":"tube-path","profileResource":"G.profile",
        "path":{"vertices":[{"key":"start","point":[0,0,0]},{"key":"corner","point":[0,0,40]},
            {"key":"end","point":[40,0,40]}],
            "segments":[{"key":"a","from":"start","to":"corner","sectionFrame":{
                "origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}},
                {"key":"b","from":"corner","to":"end","sectionFrame":{
                "origin":[0,0,40],"xAxis":[0,1,0],"yAxis":[0,0,1],"zAxis":[1,0,0]}}],
            "closed":false,"startVertex":"start"},"formingOrder":["corner"]
    })json");
    source["items"]=VariantArray{item};source["roots"]=VariantArray{std::string("part.a")};
    const auto before=source;const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_EQ(source,before);ASSERT_EQ(model.Items.size(),1u);
    EXPECT_TRUE(model.Items.front().Representations.empty());
    ASSERT_EQ(model.Outputs.size(),1u);EXPECT_EQ(model.Outputs.front().ItemKeys,(std::vector<std::string>{"part.a"}));
    EXPECT_EQ(model.Extensions.at("tubeDesigner.manufacturingPartCount").To<unsigned long long>(),1u);
    EXPECT_TRUE(model.Tables.empty());EXPECT_TRUE(model.Diagnostics.empty());
    EXPECT_FALSE(model.Extensions.contains("tubeDesigner.assemblyGeometryProcesses"));
    auto solved=source;auto solvedItems=solved.at("items").To<VariantArray>();auto solvedItem=solvedItems.front().To<ObjectMap>();
    auto material=solvedItem.at("manufacturingInput").To<ObjectMap>();material["length"]=80.0;
    solvedItem["manufacturingInput"]=material;solvedItems.front()=solvedItem;solved["items"]=solvedItems;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(solved),std::invalid_argument);
    auto malformed=source;auto badItems=malformed.at("items").To<VariantArray>();auto badItem=badItems.front().To<ObjectMap>();
    material=badItem.at("manufacturingInput").To<ObjectMap>();auto path=material.at("path").To<ObjectMap>();
    auto segments=path.at("segments").To<VariantArray>();auto segment=segments.front().To<ObjectMap>();
    segment["to"]=std::string("missing");segments.front()=segment;path["segments"]=segments;material["path"]=path;
    badItem["manufacturingInput"]=material;badItems.front()=badItem;malformed["items"]=badItems;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(malformed),std::invalid_argument);
}

TEST(ProductManufacturingModel, StableDesignMembersLeaveManufacturingCountUnresolved)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=designManufacturingDocument();const auto before=source;
    const auto model=CTemplateCodec::ParseNeutralModel(source);
    EXPECT_EQ(source,before);ASSERT_EQ(model.Items.size(),2u);ASSERT_EQ(model.Outputs.size(),1u);
    EXPECT_TRUE(model.Outputs.front().ItemKeys.empty());
    EXPECT_EQ(model.Extensions.at("tubeDesigner.manufacturingPartCount").To<unsigned long long>(),0u);
    EXPECT_FALSE(model.Extensions.at("tubeDesigner.manufacturingPartCountKnown").To<bool>());
    EXPECT_TRUE(model.Tables.empty());EXPECT_TRUE(model.Diagnostics.empty());
    for(const auto& item:model.Items) EXPECT_TRUE(item.Representations.empty());
    auto spatial=source;auto processes=spatial.at("processes").To<VariantArray>();auto process=processes.front().To<ObjectMap>();
    auto definition=process.at("definition").To<ObjectMap>();
    definition["parameters"]=ObjectMap{{"frameManufacturingMethod",std::string("space_v_notch")}};
    process["definition"]=definition;processes.front()=process;spatial["processes"]=processes;
    EXPECT_EQ(source.at("items"),spatial.at("items"));EXPECT_EQ(source.at("sourceMappings"),spatial.at("sourceMappings"));
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(spatial));
}

TEST(ProductManufacturingModel, DesignMembersRejectManufacturingFieldsAndInvalidSectionsOrFrames)
{
    using namespace product_geometry_resource_acceptance;
    const auto mutate=[](const std::function<void(ObjectMap&)>& update) {
        auto source=designManufacturingDocument();auto items=source.at("items").To<VariantArray>();
        auto item=items.front().To<ObjectMap>();update(item);items.front()=item;source["items"]=items;return source;
    };
    for(const auto* field:{"length","stockLength","stockStart","startReserve","bendAllowance","formingOrder"}) {
        const auto source=mutate([&](auto& item){auto props=item.at("properties").template To<ObjectMap>();props[field]=40.0;item["properties"]=props;});
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<field;
    }
    for(const auto* profile:{"missing","G.solid"}) {
        const auto source=mutate([&](auto& item){auto input=item.at("designInput").template To<ObjectMap>();input["profileResource"]=std::string(profile);item["designInput"]=input;});
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<profile;
    }
    const auto wrongFrame=mutate([](auto& item){auto input=item.at("designInput").template To<ObjectMap>();
        auto frame=input.at("sectionFrame").template To<ObjectMap>();frame["origin"]=VariantArray{1.0,0.0,0.0};input["sectionFrame"]=frame;item["designInput"]=input;});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(wrongFrame),std::invalid_argument);
    const auto zeroLength=mutate([](auto& item){auto input=item.at("designInput").template To<ObjectMap>();input["end"]=input.at("start");item["designInput"]=input;});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(zeroLength),std::invalid_argument);
    const auto routePath=mutate([](auto& item){item["manufacturingInput"]=item.at("designInput");item.erase("designInput");});
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(routePath),std::invalid_argument);
    auto unconsumed=designManufacturingDocument();unconsumed["processes"]=VariantArray{};
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(unconsumed),std::invalid_argument);
    auto allocatedMapping=designManufacturingDocument();auto mappings=allocatedMapping.at("sourceMappings").To<VariantArray>();
    auto mapping=mappings.front().To<ObjectMap>();mapping["sources"]=VariantArray{ObjectMap{{"itemKey",std::string("design.left")},{"segments",VariantArray{}}}};
    mappings.front()=mapping;allocatedMapping["sourceMappings"]=mappings;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(allocatedMapping),std::invalid_argument);
}

TEST(ProductManufacturingModel, GeneratedOutputsRequireDistinctNamespacesAndExplicitProducerDependencies)
{
    using namespace product_geometry_resource_acceptance;
    auto source=designManufacturingDocument();auto processes=source.at("processes").To<VariantArray>();
    auto process=processes.front().To<ObjectMap>();auto definition=process.at("definition").To<ObjectMap>();
    definition["targets"]=ObjectMap{{"members",std::string("part.a")}};process["definition"]=definition;
    auto both=source;both["processes"]=VariantArray{process};
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(both),std::invalid_argument);
    auto colliding=source;definition.erase("targets");definition["outputs"]=ObjectMap{{"manufacturing",ObjectMap{
        {"kind",std::string("manufacturing-set")},{"key",std::string("part.a")}}}};process["definition"]=definition;
    colliding["processes"]=VariantArray{process};EXPECT_THROW(CTemplateCodec::ParseNeutralModel(colliding),std::invalid_argument);
    auto downstream=processes.front().To<ObjectMap>();downstream["key"]=std::string("followup.route");
    definition=downstream.at("definition").To<ObjectMap>();auto input=definition.at("processInput").To<ObjectMap>();
    input["parts"]=ObjectMap{{"stock",ObjectMap{{"scope",std::string("process-output")},
        {"processKey",std::string("frame.route")},{"outputKey",std::string("manufacturing")}}}};definition["processInput"]=input;
    definition["outputs"]=ObjectMap{{"manufacturing",ObjectMap{{"kind",std::string("manufacturing-set")},
        {"key",std::string("window.finished")}}}};definition["dependencies"]=VariantArray{std::string("frame.route")};
    downstream["definition"]=definition;processes.emplace_back(downstream);source["processes"]=processes;
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(source));
    auto execution=generatedExecutionDocument();auto extensions=execution.at("extensions").To<ObjectMap>();
    auto sets=extensions.at("tubeDesigner.assemblyOutputSets").To<VariantArray>();
    auto producer=sets.front().To<ObjectMap>();producer["itemKeys"]=VariantArray{std::string("intermediate.frame")};sets.front()=producer;
    auto consumer=producer;consumer["processKey"]=std::string("followup.route");consumer["key"]=std::string("window.finished");
    consumer["itemKeys"]=VariantArray{std::string("manufactured.u-frame")};sets.emplace_back(consumer);
    extensions["tubeDesigner.assemblyOutputSets"]=sets;execution["extensions"]=extensions;
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution));
    definition["dependencies"]=VariantArray{};downstream["definition"]=definition;processes.back()=downstream;source["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument);
}

TEST(ProductManufacturingModel, AssemblyGeneratedPartsCanChangeCountAndRetainVerifiedDesignProvenance)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=designManufacturingDocument(), execution=generatedExecutionDocument();
    ASSERT_EQ(CTemplateCodec::ParseNeutralModel(source).Items.size(),2u);
    ASSERT_EQ(CTemplateCodec::ParseNeutralModel(execution).Outputs.front().ItemKeys.size(),1u);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution));
    auto mixed=source;auto designItems=mixed.at("items").To<VariantArray>();auto fixed=displayDocument().at("items").To<VariantArray>().back().To<ObjectMap>();
    fixed["key"]=std::string("fixed.accessory");designItems.emplace_back(fixed);mixed["items"]=designItems;
    mixed["roots"]=VariantArray{std::string("part.a"),std::string("part.b"),std::string("fixed.accessory")};
    auto actual=execution;auto actualItems=actual.at("items").To<VariantArray>();fixed.erase("geometry");
    fixed["representations"]=ObjectMap{{"result",ObjectMap{{"resource",std::string("G.solid")}}}};actualItems.emplace_back(fixed);actual["items"]=actualItems;
    actual["outputs"]=VariantArray{ObjectMap{{"key",std::string("result")},{"purpose",std::string("result")},
        {"items",VariantArray{std::string("manufactured.u-frame"),std::string("fixed.accessory")}}}};
    auto extensions=actual.at("extensions").To<ObjectMap>();extensions["tubeDesigner.manufacturingPartCount"]=2ull;actual["extensions"]=extensions;
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(mixed,actual));
}

TEST(ProductManufacturingModel, GeneratedExecutionRejectsUnknownNamespacesMissingSourcesAndFakeCounts)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=designManufacturingDocument();
    for(const auto* field:{"processKey","outputKey","key","kind"}) {
        auto execution=generatedExecutionDocument();auto extensions=execution.at("extensions").To<ObjectMap>();
        auto sets=extensions.at("tubeDesigner.assemblyOutputSets").To<VariantArray>();auto output=sets.front().To<ObjectMap>();output[field]=std::string("unknown");
        sets.front()=output;extensions["tubeDesigner.assemblyOutputSets"]=sets;execution["extensions"]=extensions;
        EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution),std::invalid_argument)<<field;
    }
    for(const auto* field:{"itemKeys","sourceItemKeys"}) {
        auto execution=generatedExecutionDocument();auto extensions=execution.at("extensions").To<ObjectMap>();
        auto sets=extensions.at("tubeDesigner.assemblyOutputSets").To<VariantArray>();auto output=sets.front().To<ObjectMap>();output[field]=VariantArray{std::string("missing")};
        sets.front()=output;extensions["tubeDesigner.assemblyOutputSets"]=sets;execution["extensions"]=extensions;
        EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution),std::invalid_argument)<<field;
    }
    auto execution=generatedExecutionDocument();auto extensions=execution.at("extensions").To<ObjectMap>();
    extensions["tubeDesigner.manufacturingPartCount"]=2ull;execution["extensions"]=extensions;
    EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution),std::invalid_argument);
    extensions.erase("tubeDesigner.assemblyOutputSets");execution["extensions"]=extensions;
    EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution),std::invalid_argument);
}

TEST(ProductManufacturingModel, CurrentVersionTwoAndThreeFixedInputsShareExecutionContract)
{
    using namespace product_geometry_resource_acceptance;
    auto source=manufacturingDocument();auto execution=CTemplateCodec::AdaptManufacturingModel(source);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution));
    source["schemaVersion"]=3;
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(source));
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution));
    auto items=execution.at("items").To<VariantArray>();items.pop_back();execution["items"]=items;
    execution["outputs"]=VariantArray{ObjectMap{{"key",std::string("result")},{"purpose",std::string("result")},
        {"items",VariantArray{std::string("part.a")}}}};
    EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(source,execution),std::invalid_argument);
}

namespace product_geometry_resource_acceptance {
ObjectMap sharedDesignDocument()
{
    auto source=displayDocument();source["schemaVersion"]=2;source.erase("connections");
    const auto members=designManufacturingDocument().at("items").To<VariantArray>();
    auto items=source.at("items").To<VariantArray>();
    for(std::size_t index=0;index<items.size();++index) {
        auto item=items[index].To<ObjectMap>();const auto input=members[index].To<ObjectMap>().at("designInput").To<ObjectMap>();
        auto frame=input.at("sectionFrame").To<ObjectMap>();frame["originAtStart"]=frame.at("origin");frame.erase("origin");
        item["properties"]=ObjectMap{{"tubeDesigner.profile",ObjectMap{{"sectionResource",input.at("profileResource")}}},
            {"assemblyFrame.member",ObjectMap{{"start",input.at("start")},{"end",input.at("end")},{"sectionFrame",frame}}}};
        items[index]=item;
    }
    source["items"]=items;return source;
}
ObjectMap sharedManufacturingDocument()
{
    return ObjectMap{{"schema",std::string("icax.manufacturing-model")},{"schemaVersion",4},
        {"connections",VariantArray{ObjectMap{{"key",std::string("joint")},{"kind",std::string("assembly_node")},
            {"items",VariantArray{std::string("part.a"),std::string("part.b")}},
            {"properties",ObjectMap{{"point",VariantArray{0.0,0.0,40.0}}}}}}},
        {"processes",designManufacturingDocument().at("processes")}};
}

ObjectMap withAssemblyResourceReference(ObjectMap source,const ObjectMap& reference)
{
    auto processes=source.at("processes").To<VariantArray>();auto process=processes.front().To<ObjectMap>();
    auto definition=process.at("definition").To<ObjectMap>();auto input=definition.at("processInput").To<ObjectMap>();
    input["resources"]=ObjectMap{{"outerFrameGroove",ObjectMap{{"ref",reference}}}};
    definition["processInput"]=input;process["definition"]=definition;processes.front()=process;
    source["processes"]=processes;return source;
}
}

TEST(ProductDisplayModel, VersionTwoOwnsSharedDesignWithoutManufacturingConnections)
{
    using namespace product_geometry_resource_acceptance;
    const auto source=sharedDesignDocument();EXPECT_EQ(source.size(),8u);
    const auto model=CTemplateCodec::ParseNeutralModel(source);EXPECT_TRUE(model.Relationships.empty());
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(displayDocument()));
    auto invalid=source;invalid["connections"]=VariantArray{};
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(invalid),std::invalid_argument);
}

TEST(ProductManufacturingModel, ThinDeclarationsUseSharedDesignWithoutCopyingPublicGeometry)
{
    using namespace product_geometry_resource_acceptance;
    const auto design=sharedDesignDocument(), declaration=sharedManufacturingDocument();
    const auto designBefore=design,declarationBefore=declaration;
    ASSERT_EQ(declaration.size(),4u);
    EXPECT_THROW(CTemplateCodec::ParseNeutralModel(declaration),std::invalid_argument);
    const auto privateInput=CTemplateCodec::ComposeManufacturingModel(declaration,design);
    EXPECT_EQ(design,designBefore);EXPECT_EQ(declaration,declarationBefore);
    EXPECT_EQ(privateInput.at("schemaVersion").To<unsigned long long>(),3u);
    EXPECT_EQ(privateInput.at("sourceMappings").To<VariantArray>().size(),2u);
    auto mixedDesign=design;auto mixedItems=mixedDesign.at("items").To<VariantArray>();
    auto fixed=displayDocument().at("items").To<VariantArray>().back().To<ObjectMap>();fixed["key"]=std::string("fixed.accessory");
    mixedItems.emplace_back(fixed);mixedDesign["items"]=mixedItems;
    mixedDesign["roots"]=VariantArray{std::string("part.a"),std::string("part.b"),std::string("fixed.accessory")};
    const auto mixedInput=CTemplateCodec::ComposeManufacturingModel(declaration,mixedDesign);
    EXPECT_EQ(mixedInput.at("items").To<VariantArray>().size(),3u);
    EXPECT_EQ(mixedInput.at("sourceMappings").To<VariantArray>().size(),3u);
    const auto mappings=mixedInput.at("sourceMappings").To<VariantArray>();
    EXPECT_EQ(mappings.back().To<ObjectMap>().at("sources").To<VariantArray>().front().To<ObjectMap>().at("itemKey"),
        iCAX::Data::Variant(std::string("fixed.accessory")));
    const auto model=CTemplateCodec::ParseNeutralModel(privateInput);
    ASSERT_EQ(model.Items.size(),2u);EXPECT_TRUE(model.Outputs.front().ItemKeys.empty());
    const auto process=privateInput.at("processes").To<VariantArray>().front().To<ObjectMap>().at("definition").To<ObjectMap>();
    EXPECT_EQ(process.at("processInput").To<ObjectMap>().at("geometry").To<ObjectMap>().at("connections"),declaration.at("connections"));
    const auto execution=generatedExecutionDocument();const iCAX::Data::Variant designValue(design);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(declaration,execution,&designValue));
    EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(declaration,execution),std::invalid_argument);
}

TEST(ProductManufacturingModel, AssemblyResourcePinsPreserveCurrentPublicInputs)
{
    using namespace product_geometry_resource_acceptance;
    const auto design=sharedDesignDocument(),designBefore=design;
    const std::string digest(64,'a');
    for(const auto& reference:std::vector<ObjectMap>{
        {{"scope",std::string("system")},{"id",std::string("v-groove-90")}},
        {{"scope",std::string("system")},{"id",std::string("v-groove-90")},{"version",std::string("1.0.0")}},
        {{"scope",std::string("system")},{"id",std::string("v-groove-90")},{"digest",digest}},
        {{"scope",std::string("system")},{"id",std::string("v-groove-90")},{"version",std::string("1.0.0")},{"digest",digest}},
        {{"scope",std::string("user")},{"id",std::string("my-v-groove")},{"version",std::string("1.0.0")},{"digest",digest}},
        {{"scope",std::string("template")},{"templateId",std::string("test.product-resources")},
            {"id",std::string("v-groove-90")},{"version",std::string("1.0.0")},{"digest",digest}}}) {
        const auto declaration=withAssemblyResourceReference(sharedManufacturingDocument(),reference),before=declaration;
        const ObjectMap parameters{{"tubeDesignerToolBindings",ObjectMap{{"outerFrameGroove",ObjectMap{
            {"ref",reference},{"parameters",ObjectMap{{"rootRadius",2.5}}}}}}}};
        const auto parametersBefore=parameters;
        iCAX::TemplateRuntime::STemplateDescriptor descriptor;descriptor.ID="test.product-resources";descriptor.Version="1.0.0";
        const auto request=CTemplateCodec::MakeEvaluationRequest(descriptor,parameters,"test-template");
        const auto composed=CTemplateCodec::ComposeManufacturingModel(declaration,design);
        EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(composed));
        EXPECT_EQ(declaration,before);EXPECT_EQ(design,designBefore);EXPECT_EQ(parameters,parametersBefore);
        EXPECT_EQ(request.at("parameters"),iCAX::Data::Variant(parametersBefore));
        const auto privateCall=composed.at("processes").To<VariantArray>().front().To<ObjectMap>().at("definition").To<ObjectMap>();
        const auto publicCall=declaration.at("processes").To<VariantArray>().front().To<ObjectMap>().at("definition").To<ObjectMap>();
        EXPECT_EQ(privateCall.at("parameters"),publicCall.at("parameters"));
        EXPECT_EQ(privateCall.at("processInput").To<ObjectMap>().at("resources"),
                  publicCall.at("processInput").To<ObjectMap>().at("resources"));
    }
}

TEST(ProductManufacturingModel, AssemblyResourcePinsRejectInvalidValuesAndUnknownFields)
{
    using namespace product_geometry_resource_acceptance;
    const ObjectMap reference{{"scope",std::string("system")},{"id",std::string("v-groove-90")},
        {"version",std::string("1.0.0")},{"digest",std::string(64,'a')}};
    for(const auto* pin:{"version","digest"})
        for(const auto& value:std::vector<iCAX::Data::Variant>{std::string{},iCAX::Data::Variant{},true,1,1.5,ObjectMap{},VariantArray{}}) {
            auto invalid=reference;invalid[pin]=value;
            const auto source=withAssemblyResourceReference(designManufacturingDocument(),invalid),before=source;
            EXPECT_THROW(CTemplateCodec::ParseNeutralModel(source),std::invalid_argument)<<pin;
            EXPECT_EQ(source,before);
        }
    for(const auto* scope:{"system","user","template"}) {
        auto invalid=reference;invalid["scope"]=std::string(scope);
        if(std::string(scope)=="template") invalid["templateId"]=std::string("test.product-resources");
        invalid["unsupportedPin"]=std::string("unexpected");
        EXPECT_THROW(CTemplateCodec::ParseNeutralModel(withAssemblyResourceReference(designManufacturingDocument(),invalid)),std::invalid_argument);
        if(std::string(scope)!="template") {
            invalid.erase("unsupportedPin");invalid["templateId"]=std::string("test.product-resources");
            EXPECT_THROW(CTemplateCodec::ParseNeutralModel(withAssemblyResourceReference(designManufacturingDocument(),invalid)),std::invalid_argument);
        }
    }
}

TEST(ProductManufacturingModel, ThinDeclarationsRejectCopiedItemsMissingReferencesAndDuplicatedConnections)
{
    using namespace product_geometry_resource_acceptance;
    const auto design=sharedDesignDocument();
    for(const auto* field:{"resources","items","roots","sourceMappings","parameters","coordinateSystem","lengthUnit"}) {
        auto raw=sharedManufacturingDocument();raw[field]=VariantArray{};
        EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,design),std::invalid_argument)<<field;
    }
    auto raw=sharedManufacturingDocument();auto connections=raw.at("connections").To<VariantArray>();
    auto connection=connections.front().To<ObjectMap>();connection["items"]=VariantArray{std::string("part.a"),std::string("missing")};
    connections.front()=connection;raw["connections"]=connections;
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,design),std::invalid_argument);
    raw=sharedManufacturingDocument();auto processes=raw.at("processes").To<VariantArray>();auto process=processes.front().To<ObjectMap>();
    auto definition=process.at("definition").To<ObjectMap>();auto input=definition.at("processInput").To<ObjectMap>();
    auto geometry=input.at("geometry").To<ObjectMap>();geometry["connections"]=raw.at("connections");input["geometry"]=geometry;
    definition["processInput"]=input;process["definition"]=definition;processes.front()=process;raw["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,design),std::invalid_argument);
    geometry.erase("connections");input["geometry"]=geometry;input["parts"]=ObjectMap{{"members",ObjectMap{{"scope",std::string("design")},
        {"itemKeys",VariantArray{std::string("missing")}}}}};definition["processInput"]=input;process["definition"]=definition;processes.front()=process;raw["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,design),std::invalid_argument);
}

TEST(ProductManufacturingModel, SharedInitialGeometryPreservesLocalPlacementWithoutPublicResourceRecipes)
{
    using namespace product_geometry_resource_acceptance;
    auto raw=sharedManufacturingDocument();raw["connections"]=VariantArray{};raw["processes"]=manufacturingDocument().at("processes");
    auto processes=raw.at("processes").To<VariantArray>();auto process=processes.front().To<ObjectMap>();auto definition=process.at("definition").To<ObjectMap>();
    auto input=definition.at("processInput").To<ObjectMap>();auto parts=input.at("parts").To<ObjectMap>();auto stock=parts.at("stock").To<ObjectMap>();
    stock["data"]=ObjectMap{{"initialGeometry",ObjectMap{{"itemPlacement",ObjectMap{{"origin",VariantArray{0.0,0.0,0.0}},
        {"xAxis",VariantArray{1.0,0.0,0.0}},{"yAxis",VariantArray{0.0,1.0,0.0}},{"zAxis",VariantArray{0.0,0.0,1.0}}}}}}};
    parts["stock"]=stock;input["parts"]=parts;definition["processInput"]=input;process["definition"]=definition;processes.front()=process;raw["processes"]=processes;
    auto design=displayDocument();design["schemaVersion"]=2;design.erase("connections");
    const auto before=raw;const auto composed=CTemplateCodec::ComposeManufacturingModel(raw,design);
    EXPECT_EQ(raw,before);EXPECT_EQ(composed.at("resources").To<VariantArray>().size(),3u);
    const auto resource=composed.at("resources").To<VariantArray>().back().To<ObjectMap>();
    EXPECT_EQ(resource.at("key").To<std::string>(),"manufacturing.input.cut.1.stock");
    EXPECT_EQ(resource.at("arguments").To<ObjectMap>().at("placement").To<ObjectMap>().at("origin"),(iCAX::Data::Variant(VariantArray{5.0,10.0,15.0})));
    auto data=stock.at("data").To<ObjectMap>();auto initial=data.at("initialGeometry").To<ObjectMap>();initial["resource"]=std::string("undeclared");
    data["initialGeometry"]=initial;stock["data"]=data;parts["stock"]=stock;input["parts"]=parts;definition["processInput"]=input;
    process["definition"]=definition;processes.front()=process;raw["processes"]=processes;
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,design),std::invalid_argument);
}

TEST(ProductManufacturingModel, ExplicitMemberSelectionExcludesDisplayOnlyLeavesWithoutGuessingTheirMaterial)
{
    using namespace product_geometry_resource_acceptance;
    auto raw=sharedManufacturingDocument();raw["connections"]=VariantArray{};
    auto selector=manufacturingDocument().at("processes").To<VariantArray>().front().To<ObjectMap>();
    selector["key"]=std::string("manufacturing.members");auto selection=selector.at("definition").To<ObjectMap>();
    selection["templateId"]=std::string("product-manufacturing-members");selection["parameters"]=ObjectMap{};
    selector["definition"]=selection;raw["processes"]=VariantArray{selector};
    auto design=displayDocument();design["schemaVersion"]=2;design.erase("connections");
    const auto before=design;const auto composed=CTemplateCodec::ComposeManufacturingModel(raw,design);
    EXPECT_EQ(design,before);EXPECT_TRUE(composed.at("processes").To<VariantArray>().empty());
    const auto model=CTemplateCodec::ParseNeutralModel(composed);
    ASSERT_EQ(model.Items.size(),1u);EXPECT_EQ(model.Items.front().Key,"part.a");
    EXPECT_EQ(model.Outputs.front().ItemKeys,(std::vector<std::string>{"part.a"}));
    const auto execution=CTemplateCodec::AdaptManufacturingModel(composed);const iCAX::Data::Variant designValue(design);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(raw,execution,&designValue));
    auto bad=raw;auto calls=bad.at("processes").To<VariantArray>();calls.emplace_back(calls.front());bad["processes"]=calls;
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(bad,design),std::invalid_argument);
    auto process=selector;auto definition=selection;definition["targets"]=ObjectMap{{"stock",std::string("missing")}};
    process["definition"]=definition;bad["processes"]=VariantArray{process};
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(bad,design),std::invalid_argument);
    auto other=manufacturingDocument().at("processes").To<VariantArray>().front().To<ObjectMap>();
    definition=other.at("definition").To<ObjectMap>();auto input=definition.at("processInput").To<ObjectMap>();
    auto parts=input.at("parts").To<ObjectMap>();auto stock=parts.at("stock").To<ObjectMap>();stock["itemKey"]=std::string("part.b");
    parts["stock"]=stock;input["parts"]=parts;definition["processInput"]=input;definition["targets"]=ObjectMap{{"stock",std::string("part.b")}};
    other["definition"]=definition;bad["processes"]=VariantArray{selector,other};
    EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(bad,design),std::invalid_argument);
}

TEST(ProductManufacturingModel, SharedPlateFactsAndLocalStockSectionsPermitDownstreamBlankPreparation)
{
    using namespace product_geometry_resource_acceptance;
    auto design=displayDocument();design["schemaVersion"]=2;design.erase("connections");
    const auto withPlate=[&](const ObjectMap& plate) {
        auto source=design;auto items=source.at("items").To<VariantArray>();auto item=items.front().To<ObjectMap>();
        item["properties"]=ObjectMap{{"partKind",std::string("plate")},{"plate",plate}};
        items.front()=item;source["items"]=items;return source;
    };
    const ObjectMap dimensions{{"width",900.0},{"height",2100.0},{"thickness",40.0},{"areaMm2",1890000.0}};
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(withPlate(dimensions)));
    auto plate=dimensions;plate["center"]=VariantArray{450.0,0.0,1050.0};
    plate["xAxis"]=VariantArray{1.0,0.0,0.0};plate["yAxis"]=VariantArray{0.0,0.0,1.0};
    auto raw=sharedManufacturingDocument();raw["connections"]=VariantArray{};raw["processes"]=manufacturingDocument().at("processes");
    const auto source=withPlate(plate),before=source,rawBefore=raw;const auto composed=CTemplateCodec::ComposeManufacturingModel(raw,source);
    EXPECT_EQ(source,before);EXPECT_EQ(raw,rawBefore);EXPECT_EQ(raw.size(),4u);
    EXPECT_EQ(composed.at("items").To<VariantArray>().front().To<ObjectMap>().at("properties").To<ObjectMap>().at("manufacturing.plate"),
        iCAX::Data::Variant(plate));
    EXPECT_EQ(composed.at("resources"),source.at("resources"));
    const VariantArray expectedMappings{
        ObjectMap{{"itemKey",std::string("part.a")},{"sources",VariantArray{ObjectMap{{"itemKey",std::string("part.a")}}}}},
        ObjectMap{{"itemKey",std::string("part.b")},{"sources",VariantArray{ObjectMap{{"itemKey",std::string("part.b")}}}}}};
    EXPECT_EQ(composed.at("sourceMappings").To<VariantArray>(),expectedMappings);
    const auto fixedExecution=CTemplateCodec::AdaptManufacturingModel(composed);const iCAX::Data::Variant sourceValue(source);
    EXPECT_NO_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(raw,fixedExecution,&sourceValue));
    auto missing=fixedExecution;auto outputs=missing.at("outputs").To<VariantArray>();auto result=outputs.front().To<ObjectMap>();
    result["items"]=VariantArray{std::string("part.a")};outputs.front()=result;missing["outputs"]=outputs;
    EXPECT_THROW(CTemplateCodec::ValidateManufacturingExecutionModel(raw,missing,&sourceValue),std::invalid_argument);
    const auto rejected=[&](const ObjectMap& invalid) {
        EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,withPlate(invalid)),std::invalid_argument);
    };
    auto invalid=plate;invalid["width"]=-1.0;rejected(invalid);
    invalid=plate;invalid["thickness"]=0.0;rejected(invalid);
    invalid=plate;invalid["areaMm2"]=1.0;rejected(invalid);
    invalid=plate;invalid.erase("center");rejected(invalid);
    invalid=plate;invalid["xAxis"]=VariantArray{2.0,0.0,0.0};rejected(invalid);
    invalid=plate;invalid["yAxis"]=VariantArray{1.0,0.0,0.0};rejected(invalid);
    invalid=plate;invalid["geometry"]=VariantArray{};rejected(invalid);
    auto processes=raw.at("processes").To<VariantArray>();auto process=processes.front().To<ObjectMap>();
    auto definition=process.at("definition").To<ObjectMap>();auto input=definition.at("processInput").To<ObjectMap>();
    auto parts=input.at("parts").To<ObjectMap>();auto stock=parts.at("stock").To<ObjectMap>();
    const ObjectMap section{{"placement",ObjectMap{{"origin",VariantArray{-313.0,0.0,0.0}},
        {"xAxis",VariantArray{1.0,0.0,0.0}},{"yAxis",VariantArray{0.0,1.0,0.0}}}},
        {"contours",design.at("resources").To<VariantArray>().front().To<ObjectMap>().at("arguments").To<ObjectMap>().at("contours")},
        {"vector",VariantArray{0.0,0.0,6304.0}}};
    stock["data"]=ObjectMap{{"section",section},{"length",6304.0}};parts["stock"]=stock;input["parts"]=parts;
    definition["processInput"]=input;process["definition"]=definition;processes.front()=process;raw["processes"]=processes;
    const auto blankDesignBefore=design,blankDeclarationBefore=raw;
    const auto blankInput=CTemplateCodec::ComposeManufacturingModel(raw,design);
    EXPECT_EQ(design,blankDesignBefore);EXPECT_EQ(raw,blankDeclarationBefore);
    EXPECT_EQ(blankInput.at("resources"),design.at("resources"));
    EXPECT_EQ(blankInput.at("sourceMappings").To<VariantArray>(),expectedMappings);
    EXPECT_EQ(blankInput.at("processes").To<VariantArray>().front().To<ObjectMap>().at("definition").To<ObjectMap>()
        .at("processInput").To<ObjectMap>().at("parts").To<ObjectMap>().at("stock").To<ObjectMap>().at("data").To<ObjectMap>().at("section"),
        iCAX::Data::Variant(section));
}

TEST(ProductDisplayModel, SharedPlateAllowsIntrinsicOutlineAndPositiveThickDimensions)
{
    using namespace product_geometry_resource_acceptance;
    auto design=displayDocument();design["schemaVersion"]=2;design.erase("connections");
    const auto withPlate=[&](const ObjectMap& plate) {
        auto source=design;auto items=source.at("items").To<VariantArray>();auto item=items.front().To<ObjectMap>();
        item["properties"]=ObjectMap{{"partKind",std::string("plate")},{"plate",plate}};
        items.front()=item;source["items"]=items;return source;
    };
    const ObjectMap thick{{"width",30.0},{"height",20.0},{"thickness",30.0},{"areaMm2",600.0}};
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(withPlate(thick)));
    const VariantArray outline{VariantArray{0.0,0.0},VariantArray{900.0,200.0},
        VariantArray{900.0,2100.0},VariantArray{0.0,1900.0}};
    ObjectMap plate{{"width",900.0},{"height",2100.0},{"thickness",3000.0},
        {"outline",outline},{"areaMm2",1710000.0}};
    const auto source=withPlate(plate),before=source;
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(source));
    auto raw=sharedManufacturingDocument();raw["connections"]=VariantArray{};
    raw["processes"]=manufacturingDocument().at("processes");
    const auto composed=CTemplateCodec::ComposeManufacturingModel(raw,source);
    EXPECT_EQ(source,before);
    EXPECT_EQ(composed.at("items").To<VariantArray>().front().To<ObjectMap>().at("properties").To<ObjectMap>()
        .at("manufacturing.plate"),iCAX::Data::Variant(plate));
    const VariantArray clockwise{outline[3],outline[2],outline[1],outline[0]};
    auto valid=plate;valid["outline"]=clockwise;
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(withPlate(valid)));
    valid=plate;valid.erase("areaMm2");
    EXPECT_NO_THROW(CTemplateCodec::ParseNeutralModel(withPlate(valid)));
    const auto rejected=[&](const ObjectMap& invalid) {
        EXPECT_THROW(CTemplateCodec::ComposeManufacturingModel(raw,withPlate(invalid)),std::invalid_argument);
    };
    auto invalid=plate;invalid["areaMm2"]=1890000.0;rejected(invalid);
    invalid=plate;invalid["outline"]=ObjectMap{};rejected(invalid);
    invalid=plate;invalid["outline"]=VariantArray{VariantArray{0.0,0.0},VariantArray{1.0,0.0}};rejected(invalid);
    invalid=plate;invalid["outline"]=VariantArray{VariantArray{0.0,0.0},VariantArray{1.0,0.0},VariantArray{0.0,0.0}};rejected(invalid);
    invalid=plate;invalid["outline"]=VariantArray{VariantArray{0.0,0.0},VariantArray{1.0,0.0},VariantArray{2.0,0.0}};rejected(invalid);
    invalid=plate;auto points=outline;points.front()=VariantArray{0.0,0.0,0.0};invalid["outline"]=points;rejected(invalid);
    invalid=plate;points=outline;points.front()=VariantArray{std::numeric_limits<double>::infinity(),0.0};
    invalid["outline"]=points;rejected(invalid);
    invalid=plate;invalid["thickness"]=-1.0;rejected(invalid);
}
